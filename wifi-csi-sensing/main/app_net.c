/* app_net.c: 板端 WFP TCP 端点（homepulse 脱串口直连）。
 * 与 env-station 的 wfp_tcp.c 同构（板间一致性三步之"拷贝改"）：
 * 服务生命周期随 WiFi——GOT_IP 起 listen，断网收口；单客户端；
 * 入线复用 USB 的 JSON 分发，出线由 app_usb_send_str 扇出到这里。 */
#include "app_net.h"

#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "app_usb_serial.h"

static const char *TAG = "app_net";
#define APP_NET_PORT 7788

static int s_srv = -1;
static int s_cli = -1;
static volatile bool s_want;      /* WiFi 在网 */
static SemaphoreHandle_t s_tx_mu; /* 并发写 TCP；同时守护 s_cli 生命周期 */

/* cli_close: 收口当前客户端。调用方必须已持 s_tx_mu —— s_cli 会被
 * net_task（accept 顶替/断开）、app_net_out_str 各调用任务（写失败收口）、
 * WiFi 事件（app_net_up）三方触达，曾经无锁：两条路径并发时按读到的
 * 旧值各 close 一次，期间 lwIP 把该 fd 号复用给 httpd 的新套接字，
 * 第二次 close 就误杀了它 —— 现场：httpd accept 报 EBADF(23) 后永久
 * 失灵而 WiFi/CSI/USB 一切正常（2026-09-27 env-station 排障实录，
 * 板间一致性同款修复）。 */
static void cli_close(void)
{
    if (s_cli >= 0) {
        close(s_cli);
        s_cli = -1;
    }
}

void app_net_up(bool up)
{
    s_want = up;
    if (!up && s_tx_mu != NULL) {
        if (xSemaphoreTake(s_tx_mu, pdMS_TO_TICKS(1000)) == pdTRUE) {
            cli_close();
            xSemaphoreGive(s_tx_mu);
        }
    }
}

void app_net_out_str(const char *line)
{
    if (line == NULL || s_cli < 0) {
        return;
    }
    /* 行协议：无换行的（JSON 应答）补上；超长截断保护 */
    char buf[513];
    size_t n = strlen(line);
    if (n > sizeof(buf) - 2) {
        n = sizeof(buf) - 2;
    }
    memcpy(buf, line, n);
    if (n == 0 || buf[n - 1] != '\n') {
        buf[n++] = '\n';
    }
    /* 2026-09-27 03:01 事故加固：对端不读时 write 曾无限阻塞并持互斥，
     * 拖死 USB 遥测（sense 任务未挂狗即无声卡死）。互斥限时获取，拿不到
     * 丢弃本行；发送超时由 SO_SNDTIMEO 兜底。 */
    if (s_tx_mu != NULL) {
        if (xSemaphoreTake(s_tx_mu, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return;
        }
    }
    if (s_cli >= 0 && write(s_cli, buf, n) < 0) {
        cli_close(); /* 客户端已死/不读（SNDTIMEO 超时）：收口，accept 等重连 */
    }
    if (s_tx_mu != NULL) {
        xSemaphoreGive(s_tx_mu);
    }
}

static void net_task(void *arg)
{
    (void)arg;
    for (;;) {
        while (!s_want) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (s < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct sockaddr_in sa = { 0 };
        sa.sin_family = AF_INET;
        sa.sin_port = htons(APP_NET_PORT);
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(s, 1) != 0) {
            ESP_LOGE(TAG, "bind/listen :%d 失败 errno=%d", APP_NET_PORT, errno);
            close(s);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int nb = 1;
        ioctl(s, FIONBIO, &nb); /* 非阻塞 accept：断网时能退出收口 */
        s_srv = s;
        ESP_LOGI(TAG, "WFP TCP 端点 :%d 就绪（homepulse 可脱串口直连）", APP_NET_PORT);
        while (s_want) {
            struct sockaddr_in ca;
            socklen_t cl = sizeof(ca);
            int c = accept(s, (struct sockaddr *)&ca, &cl);
            if (c < 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            ESP_LOGI(TAG, "TCP 客户端接入（单客户端，后连顶前连）");
            struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            /* 发送同样限时 1s：对端不读（读者挂死）时 write 报错而非永久阻塞 */
            setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            /* 顶替前收口旧 fd（防 socket 表泄漏）：s_cli 生命周期全程持锁 */
            if (xSemaphoreTake(s_tx_mu, pdMS_TO_TICKS(1000)) == pdTRUE) {
                cli_close();
                s_cli = c;
                xSemaphoreGive(s_tx_mu);
            } else {
                close(c); /* 拿不到锁（极端）：宁可放弃本连接也不留下竞态 */
                continue;
            }
            char line[256];
            size_t ln = 0;
            while (s_want) {
                uint8_t ch;
                int r = recv(c, &ch, 1, 0);
                if (r < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    break;
                }
                if (r == 0) {
                    break;
                }
                if (ch == '\n' || ch == '\r') {
                    if (ln > 0) {
                        line[ln] = 0;
                        app_usb_dispatch_line(line); /* 与 USB 同一 JSON 分发 */
                        ln = 0;
                    }
                    continue;
                }
                if (ln < sizeof(line) - 1) {
                    line[ln++] = ch;
                }
            }
            if (xSemaphoreTake(s_tx_mu, pdMS_TO_TICKS(1000)) == pdTRUE) {
                cli_close(); /* 客户端已断开：收口（若 out_str 已收过则空操作） */
                xSemaphoreGive(s_tx_mu);
            }
            ESP_LOGI(TAG, "TCP 客户端断开");
        }
        close(s);
        s_srv = -1;
        ESP_LOGI(TAG, "WiFi 离网，TCP 端点收口");
    }
}

void app_net_init(void)
{
    s_tx_mu = xSemaphoreCreateMutex();
    xTaskCreate(net_task, "app_net", 4096, NULL, 4, NULL);
}
