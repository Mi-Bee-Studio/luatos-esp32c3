/*
 * app_sense.c — WiFi CSI 感知节点引擎。
 *
 * 职责（重计算在 PC 侧 wifipulse，本模块只做轻量采集与显示服务）：
 *   1. WiFi 关联后使能 CSI，回调里只提取 16 个选定子载波的 I/Q 入队
 *      （回调跑在 WiFi 任务，必须 O(1) 且无阻塞）；
 *   2. 流任务把队列记录编成 "#S1 seq t_ms rssi hex64" 行写 USB 串口，
 *      支持 max_hz 限频（超限丢弃）；
 *   3. 协议命令：sense_start / sense_stop / sense_calibrate / sense_info，
 *      以及接收 PC 推送的 sense_status（驱动显示页，不回 ack 防 1 Hz 刷屏）；
 *   4. 可选主动激励 stimulus_hz：向网关单播 1 字节 UDP，靠 AP 的 ACK 帧
 *      产生 CSI（需 dump_ack_en，低流量环境的采样率兜底）；
 *   5. 本地独立模式：对中位子载波做相位差分标准差的存在估计
 *      （无 PC 时页面仍有内容；精度有限，精确值以 wifipulse 为准）。
 */
#include "app_sense.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "esp_netif_ip_addr.h"

#include "app_protocol.h"
#include "app_usb_serial.h"
#include "cJSON.h"

static const char *TAG = "APP_SENSE";

#define SENSE_NSEL      16   // 选定子载波数（与协议 hex64 对应）
#define SENSE_QUEUE_LEN 64   // CSI 回调 → 流任务队列深度
#define SENSE_TASK_STACK 4096
#define SENSE_TASK_PRIO  3
#define LOC_WIN          40  // 本地存在估计：2 s @20Hz 的差分窗口
#define LOC_CALIB_S      30  // 本地基线采集时长
#define SENSE_PI         3.14159265358979f

// —— 单包 CSI 记录（队列元素） ——
typedef struct {
    uint32_t seq;
    int64_t  t_ms;
    int8_t   rssi;
    int8_t   iq[SENSE_NSEL * 2]; // 每子载波 (I,Q)
    int      nsc;                // 设备报告的复数子载波总数（首个样本后用于 HELLO）
} sense_rec_t;

// —— 模块状态 ——
static bool s_wifi_up;            // WiFi 已关联
static bool s_csi_on;             // CSI 已使能
static bool s_streaming = true;   // 上电即流（与 env-station 同构）。sense_start
                                  // 只更新限频/激励参数；sense_stop/UP 键可停。
                                  // 默认关会踩坑：板子重启后 homepulse 不会重发
                                  // sense_start（会话未断只推 status）→ 永久哑流
static int  s_max_hz = 50;        // 流限频
static int64_t s_last_sent_ms;    // 限频参考
static int  s_stimulus_hz;        // 主动激励频率（0 = 关）
static esp_ping_handle_t s_ping; // 激励：ping 网关，回包是下行单播数据帧 → 稳定 CSI 源
static volatile uint32_t s_csi_total;  // CSI 回调总数（诊断：回调率 vs 串流率）
static volatile uint32_t s_qdrop;      // 队列满丢弃数

static QueueHandle_t s_queue;
static TaskHandle_t  s_task;

// —— PC 推送状态（usb_serial 任务写，LVGL 任务读） ——
static portMUX_TYPE s_pc_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool  valid;
    bool  present;
    float motion;
    char  cls[8];
    float bpm, quality, rate;
    int8_t rssi;
    bool  calibrating;
    int64_t last_ms;
} s_pc;

// —— 本地估计（流任务内更新，快照给 UI） ——
static portMUX_TYPE s_loc_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    float rate_hz;
    int8_t rssi;
    bool  present, calibrating, has;
    float motion;      // std 相对阈值粗略归一
    uint32_t seq;
} s_loc_snap;

// 本地估计原始数据（仅流任务访问）
static float   loc_diffs[LOC_WIN];
static int     loc_n, loc_head;
static float   loc_base_sum;  static int loc_base_n; // 校准期累计
static float   loc_thr = 0.02f;
static int     loc_sec_counter;
static int     loc_rate_count;

// ---------------------------------------------------------------------------
// CSI 接收回调（WiFi 任务上下文：只做选取与入队，绝不阻塞/打印）
// ---------------------------------------------------------------------------
static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    const int nsc = info->len / 2;
    if (nsc < SENSE_NSEL) {
        return;
    }
    sense_rec_t rec = {
        .seq  = 0, // 由流任务统一编号（回调侧省一个原子）
        .t_ms = esp_timer_get_time() / 1000,
        .rssi = info->rx_ctrl.rssi,
        .nsc  = nsc,
    };
    // 16 个子载波均匀取自 [nsc/8, nsc*7/8]：避开边缘空载波与直流
    const int lo = nsc / 8;
    const int span = (nsc * 3) / 4;
    for (int k = 0; k < SENSE_NSEL; k++) {
        int idx = lo + (k * span) / (SENSE_NSEL - 1);
        if (idx >= nsc) idx = nsc - 1;
        rec.iq[2 * k]     = info->buf[2 * idx];
        rec.iq[2 * k + 1] = info->buf[2 * idx + 1];
    }
    s_csi_total++;
    if (xQueueSend(s_queue, &rec, 0) != pdTRUE) {
        s_qdrop++; // 队列满丢弃；wifipulse 侧重采样对 <5% 缺样鲁棒
    }
}

// ---------------------------------------------------------------------------
// 主动激励：向网关单播 1 字节 UDP（AP 回 ACK，ACK 帧带出 CSI）
// ---------------------------------------------------------------------------
static void stimulus_start(int hz)
{
    if (hz <= 0 || s_ping) {
        return;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.gw.addr == 0) {
        return;
    }
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 0; // 无限
    cfg.interval_ms = 1000 / (uint32_t)hz;
    cfg.timeout_ms = 1000;
    cfg.data_size = 8; // 小包即可
    char gws[18];
    snprintf(gws, sizeof(gws), IPSTR, IP2STR(&ip.gw));
    if (ipaddr_aton(gws, &cfg.target_addr) == 0) {
        return;
    }
    esp_ping_callbacks_t cbs = {0};
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
        esp_ping_start(s_ping);
        ESP_LOGI(TAG, "主动激励 ping %s 每 %d ms（回包=下行数据帧 → CSI）", gws, 1000 / hz);
    } else {
        s_ping = NULL;
        ESP_LOGW(TAG, "ping 会话创建失败");
    }
}

static void stimulus_stop(void)
{
    if (s_ping) {
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }
}

// ---------------------------------------------------------------------------
// CSI 使能（WiFi 关联后调用一次）
// ---------------------------------------------------------------------------
static void csi_enable(void)
{
    if (s_csi_on) {
        return;
    }
    /* 注意：勿用 WIFI_PS_NONE —— 实测繁忙信道上 WiFi 帧处理会吃满单核，
     * 饿死 IDLE（task_wdt 刷屏）并拖垮所有应用任务。保持默认 MIN_MODEM；
     * 采样率靠 ping 激励（TX 后无线电清醒，ACK/回包即刻可达）。 */
    wifi_csi_config_t cfg = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = false, // 保持子载波独立性（感知分析需要）
        .manu_scale = false,
        .dump_ack_en = true,        // 激励模式靠 ACK 帧出 CSI
    };
    esp_err_t ret = esp_wifi_set_csi_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CSI 配置失败: %s（sdkconfig 需 CONFIG_ESP_WIFI_CSI_ENABLED）", esp_err_to_name(ret));
        return;
    }
    ret = esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_csi(true);
    }
    if (ret == ESP_OK) {
        s_csi_on = true;
        ESP_LOGI(TAG, "CSI 采集已使能");
    } else {
        ESP_LOGW(TAG, "CSI 使能失败: %s", esp_err_to_name(ret));
    }
}

// ---------------------------------------------------------------------------
// 本地存在估计（流任务每秒一次）
// ---------------------------------------------------------------------------
static void local_tick(float std_now, int samples_last_sec)
{
    loc_sec_counter++;
    // 采样率快照
    portENTER_CRITICAL(&s_loc_mux);
    s_loc_snap.rate_hz = s_loc_snap.rate_hz * 0.5f + samples_last_sec * 0.5f;
    s_loc_snap.has = true;
    portEXIT_CRITICAL(&s_loc_mux);

    if (loc_sec_counter <= LOC_CALIB_S) {
        loc_base_sum += std_now;
        loc_base_n++;
        if (loc_sec_counter == LOC_CALIB_S) {
            float base = loc_base_sum / loc_base_n;
            loc_thr = (base * 3.0f > 0.01f) ? base * 3.0f : 0.01f;
            ESP_LOGI(TAG, "本地基线完成: std=%.4f 阈值=%.4f", base, loc_thr);
        }
        portENTER_CRITICAL(&s_loc_mux);
        s_loc_snap.calibrating = true;
        s_loc_snap.present = false;
        portEXIT_CRITICAL(&s_loc_mux);
        return;
    }
    const bool present = std_now > loc_thr;
    const float motion = (std_now / (loc_thr * 2.0f) > 1.0f) ? 1.0f : std_now / (loc_thr * 2.0f);
    portENTER_CRITICAL(&s_loc_mux);
    s_loc_snap.calibrating = false;
    s_loc_snap.present = present;
    s_loc_snap.motion = motion;
    portEXIT_CRITICAL(&s_loc_mux);
}

// ---------------------------------------------------------------------------
// 流任务：出队 → 限频 → "#S1" 行 → USB；并维护本地估计
// ---------------------------------------------------------------------------
static const char HEXD[] = "0123456789abcdef";

static void format_hex64(const int8_t *iq, char *out /*≥65*/)
{
    for (int i = 0; i < SENSE_NSEL * 2; i++) {
        uint8_t v = (uint8_t)iq[i];
        out[2 * i]     = HEXD[v >> 4];
        out[2 * i + 1] = HEXD[v & 0xF];
    }
    out[SENSE_NSEL * 4] = '\0';
}

static void sense_stream_task(void *arg)
{
    (void)arg;
    static char line[96];
    static char hexbuf[SENSE_NSEL * 4 + 1];
    bool hello_sent = false;
    int  sel_tab[SENSE_NSEL];
    int  hello_nsc = 0;
    uint32_t seq = 0;

    ESP_LOGI(TAG, "sense 流任务启动");
    /* 挂任务看门狗（03:01 事故教训：输出路径死锁时遥测无声停摆 3 小时+；
     * 队列等待本就 200ms 限时，空闲也周期喂狗） */
    esp_task_wdt_add(NULL);
    while (true) {
        esp_task_wdt_reset();
        sense_rec_t rec;
        if (xQueueReceive(s_queue, &rec, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }

        // —— 本地统计（幅度比值法） ——
        // 相位受每包随机公共旋转支配（实测差分 std≈0.77 rad），本地粗估
        // 改用子载波平均幅度：不受公共相位影响，呼吸/运动经多径干涉
        // 同样调制幅度（百分比级）
        float amp_sum = 0.0f;
        for (int k = 0; k < SENSE_NSEL; k++) {
            amp_sum += hypotf((float)rec.iq[2 * k], (float)rec.iq[2 * k + 1]);
        }
        const float amp_mean = amp_sum / SENSE_NSEL;
        static float amp_base = 0.0f; // 慢均值：吸收硬件增益漂移
        if (amp_base <= 0.0f) {
            amp_base = amp_mean;
        }
        amp_base += (amp_mean - amp_base) * 0.001f; // ~50 s 时间常数
        loc_diffs[loc_head] = (amp_base > 0.0f) ? (amp_mean / amp_base - 1.0f) : 0.0f;
        loc_head = (loc_head + 1) % LOC_WIN;
        if (loc_n < LOC_WIN) loc_n++;
        loc_rate_count++;

        // 每满一秒：本地估计 tick
        static int64_t last_sec_ms = 0;
        const int64_t now_ms = rec.t_ms;
        if (last_sec_ms == 0) {
            last_sec_ms = now_ms;
        } else if (now_ms - last_sec_ms >= 1000) {
            // 窗口 std
            float std_now = 0.0f;
            if (loc_n > 2) {
                float mean = 0.0f;
                for (int i = 0; i < loc_n; i++) mean += loc_diffs[i];
                mean /= loc_n;
                for (int i = 0; i < loc_n; i++) {
                    const float d = loc_diffs[i] - mean;
                    std_now += d * d;
                }
                std_now = sqrtf(std_now / (loc_n - 1));
            }
            local_tick(std_now, loc_rate_count);
            loc_rate_count = 0;
            last_sec_ms = now_ms;
            static int diag_ctr;
            if (++diag_ctr >= 10) {
                diag_ctr = 0;
                uint32_t replies = 0;
                if (s_ping) {
                    esp_ping_get_profile(s_ping, ESP_PING_PROF_REPLY, &replies, sizeof(replies));
                }
                ESP_LOGI(TAG, "diag csi=%u qdrop=%u ping_replies=%u local_rate=%.1f",
                         (unsigned)s_csi_total, (unsigned)s_qdrop, (unsigned)replies,
                         s_loc_snap.rate_hz);
            }
            portENTER_CRITICAL(&s_loc_mux);
            s_loc_snap.rssi = rec.rssi;
            s_loc_snap.seq = seq;
            portEXIT_CRITICAL(&s_loc_mux);
        }

        // —— 限频下的串流 ——
        if (!s_streaming) {
            hello_sent = false;
            continue;
        }
        if (s_max_hz > 0 && rec.t_ms - s_last_sent_ms < 1000 / s_max_hz) {
            continue; // 超限丢弃（时间戳连续性由 PC 侧重采样兜底）
        }
        s_last_sent_ms = rec.t_ms;

        if (!hello_sent) {
            // 会话头：告知 PC 实际子载波选择与限频
            hello_nsc = rec.nsc;
            const int lo = hello_nsc / 8;
            const int span = (hello_nsc * 3) / 4;
            for (int k = 0; k < SENSE_NSEL; k++) {
                int idx = lo + (k * span) / (SENSE_NSEL - 1);
                if (idx >= hello_nsc) idx = hello_nsc - 1;
                sel_tab[k] = idx;
            }
            int n = snprintf(line, sizeof(line), "#S1-HELLO nsc=%d sel=", hello_nsc);
            for (int k = 0; k < SENSE_NSEL && n < (int)sizeof(line) - 6; k++) {
                n += snprintf(line + n, sizeof(line) - n, k ? ",%d" : "%d", sel_tab[k]);
            }
            snprintf(line + n, sizeof(line) - n, " rate=%d\n", s_max_hz);
            app_usb_send_str(line);
            hello_sent = true;
        }

        format_hex64(rec.iq, hexbuf);
        snprintf(line, sizeof(line), "#S1 %lu %lld %d %s\n",
                 (unsigned long)seq, (long long)rec.t_ms, (int)rec.rssi, hexbuf);
        app_usb_send_str(line);
        seq++;
        portENTER_CRITICAL(&s_loc_mux);
        s_loc_snap.seq = seq;
        portEXIT_CRITICAL(&s_loc_mux);
    }
}

// ---------------------------------------------------------------------------
// 协议命令
// ---------------------------------------------------------------------------
static void sense_start_handler(cJSON *msg, cJSON *response)
{
    (void)response;
    cJSON *max_hz = cJSON_GetObjectItem(msg, "max_hz");
    cJSON *stim = cJSON_GetObjectItem(msg, "stimulus_hz");
    s_max_hz = cJSON_IsNumber(max_hz) ? (int)max_hz->valuedouble : 50;
    if (s_max_hz <= 0 || s_max_hz > 100) {
        s_max_hz = 50;
    }
    s_stimulus_hz = cJSON_IsNumber(stim) ? (int)stim->valuedouble : 0;
    s_last_sent_ms = 0;
    s_streaming = true;
    if (s_wifi_up) {
        stimulus_start(s_stimulus_hz);
    }
    ESP_LOGI(TAG, "sense_start: max_hz=%d stimulus=%d", s_max_hz, s_stimulus_hz);
    app_protocol_send_ok("sense_start");
}

static void sense_stop_handler(cJSON *msg, cJSON *response)
{
    (void)msg; (void)response;
    s_streaming = false;
    stimulus_stop();
    app_protocol_send_ok("sense_stop");
}

static void sense_calibrate_handler(cJSON *msg, cJSON *response)
{
    (void)response;
    // 可选 delay_s：>0 = 布防延迟校准（布防-离开模式），缺省 = 立即
    cJSON *delay = cJSON_GetObjectItem(msg, "delay_s");
    if (cJSON_IsNumber(delay) && delay->valuedouble > 0) {
        int s = (int)delay->valuedouble;
        if (s > 3600) s = 3600;
        app_sense_arm_calibrate(s);
        app_protocol_send_ok("sense_calibrate");
        return;
    }
    // 本地基线重置（PC 侧引擎自行重学）
    loc_base_sum = 0;
    loc_base_n = 0;
    loc_sec_counter = 0;
    portENTER_CRITICAL(&s_loc_mux);
    s_loc_snap.calibrating = true;
    portEXIT_CRITICAL(&s_loc_mux);
    ESP_LOGI(TAG, "本地重校准开始（%d s）", LOC_CALIB_S);
    app_protocol_send_ok("sense_calibrate");
}

// PC 推送状态：只更新不回 ack（1 Hz 推送回 ack 会刷屏）
static void sense_status_handler(cJSON *msg, cJSON *response)
{
    (void)response;
    portENTER_CRITICAL(&s_pc_mux);
    s_pc.valid = true;
    s_pc.last_ms = esp_timer_get_time() / 1000;
    cJSON *it;
    if ((it = cJSON_GetObjectItem(msg, "present")) && cJSON_IsBool(it)) {
        s_pc.present = cJSON_IsTrue(it);
    }
    if ((it = cJSON_GetObjectItem(msg, "motion")) && cJSON_IsNumber(it)) {
        s_pc.motion = (float)it->valuedouble;
    }
    if ((it = cJSON_GetObjectItem(msg, "motion_cls")) && cJSON_IsString(it)) {
        snprintf(s_pc.cls, sizeof(s_pc.cls), "%s", it->valuestring);
    } else {
        s_pc.cls[0] = '\0';
    }
    if ((it = cJSON_GetObjectItem(msg, "bpm")) && cJSON_IsNumber(it)) {
        s_pc.bpm = (float)it->valuedouble;
    }
    if ((it = cJSON_GetObjectItem(msg, "quality")) && cJSON_IsNumber(it)) {
        s_pc.quality = (float)it->valuedouble;
    }
    if ((it = cJSON_GetObjectItem(msg, "rate_hz")) && cJSON_IsNumber(it)) {
        s_pc.rate = (float)it->valuedouble;
    }
    if ((it = cJSON_GetObjectItem(msg, "rssi")) && cJSON_IsNumber(it)) {
        s_pc.rssi = (int8_t)it->valuedouble;
    }
    if ((it = cJSON_GetObjectItem(msg, "state")) && cJSON_IsString(it)) {
        s_pc.calibrating = strstr(it->valuestring, "calibrat") != NULL;
    }
    portEXIT_CRITICAL(&s_pc_mux);
}

static void sense_info_handler(cJSON *msg, cJSON *response)
{
    (void)msg; (void)response;
    sense_status_t st;
    app_sense_get_status(&st);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "streaming", st.streaming);
    cJSON_AddBoolToObject(data, "present", st.present);
    cJSON_AddNumberToObject(data, "rate_hz", st.rate_hz);
    cJSON_AddNumberToObject(data, "rssi", st.rssi);
    cJSON_AddNumberToObject(data, "seq", st.seq);
    cJSON_AddStringToObject(data, "source", st.pc_online ? "pc" : "local");
    cJSON_AddNumberToObject(data, "csi_total", (double)s_csi_total);
    cJSON_AddNumberToObject(data, "qdrop", (double)s_qdrop);
    app_protocol_send_data("sense_info", data);
}

// pc_rssi 0 视为未填（wifipulse 不带 rssi 时回退本地值）
static inline int8_t pc_rsi_safe(int8_t pc, int8_t loc)
{
    return pc != 0 ? pc : loc;
}

// ---------------------------------------------------------------------------
// 公共 API
// ---------------------------------------------------------------------------
void app_sense_init(void)
{
    memset(&s_pc, 0, sizeof(s_pc));
    memset(&s_loc_snap, 0, sizeof(s_loc_snap));

    s_queue = xQueueCreate(SENSE_QUEUE_LEN, sizeof(sense_rec_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "CSI 队列创建失败");
        return;
    }
    if (xTaskCreate(sense_stream_task, "sense_stream", SENSE_TASK_STACK, NULL,
                    SENSE_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "sense 流任务创建失败");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }

    app_protocol_register_handler("sense_start", sense_start_handler);
    app_protocol_register_handler("sense_stop", sense_stop_handler);
    app_protocol_register_handler("sense_calibrate", sense_calibrate_handler);
    app_protocol_register_handler("sense_status", sense_status_handler);
    app_protocol_register_handler("sense_info", sense_info_handler);
    ESP_LOGI(TAG, "sense 节点就绪（等待 WiFi 关联后使能 CSI）");
}

void app_sense_on_wifi(bool connected)
{
    s_wifi_up = connected;
    if (connected) {
        csi_enable();
        if (s_streaming && s_stimulus_hz > 0) {
            stimulus_start(s_stimulus_hz);
        }
    } else {
        stimulus_stop();
    }
}

void app_sense_request_recalibrate(void)
{
    // 本地重置 + 让 PC 侧重开流（重新校准环境基线）
    loc_base_sum = 0;
    loc_base_n = 0;
    loc_sec_counter = 0;
    cJSON *msg = cJSON_CreateObject();
    if (msg) {
        cJSON_AddStringToObject(msg, "cmd", "sense_calibrate");
        app_usb_send_json(msg);
        cJSON_Delete(msg);
    }
    ESP_LOGI(TAG, "用户请求重校准");
}

// —— 延迟校准（布防-离开）：esp_timer 一次性定时，到点等同按下 CENTER ——
static esp_timer_handle_t s_calib_timer;
static int64_t s_calib_fire_us; // 0 = 无布防

static void calib_timer_cb(void *arg)
{
    (void)arg;
    s_calib_fire_us = 0;
    app_sense_request_recalibrate();
}

void app_sense_arm_calibrate(int delay_s)
{
    if (s_calib_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = calib_timer_cb,
            .name = "calib_arm",
        };
        if (esp_timer_create(&args, &s_calib_timer) != ESP_OK) {
            ESP_LOGE(TAG, "校准定时器创建失败");
            return;
        }
    }
    esp_timer_stop(s_calib_timer); // 重布防 = 重新计时
    s_calib_fire_us = esp_timer_get_time() + (int64_t)delay_s * 1000000;
    esp_timer_start_once(s_calib_timer, (uint64_t)delay_s * 1000000ULL);
    ESP_LOGI(TAG, "延迟校准已布防：%d 秒后自动开始（请离开房间）", delay_s);
}

void app_sense_cancel_calibrate(void)
{
    if (s_calib_timer) {
        esp_timer_stop(s_calib_timer);
    }
    if (s_calib_fire_us) {
        s_calib_fire_us = 0;
        ESP_LOGI(TAG, "延迟校准已取消");
    }
}

int64_t app_sense_calib_pending_ms(void)
{
    if (s_calib_fire_us == 0) {
        return 0;
    }
    int64_t left_ms = (s_calib_fire_us - esp_timer_get_time()) / 1000;
    return left_ms > 0 ? left_ms : 0;
}

void app_sense_toggle_streaming(void)
{
    if (s_streaming) {
        // 与 sense_stop 协议处理器同路径：停发 #S1 + 停激励 ping
        s_streaming = false;
        stimulus_stop();
        ESP_LOGI(TAG, "用户按键：串流已暂停（sense_stop）");
    } else {
        // 与 sense_start 协议处理器同路径：沿用最近一次的 max_hz/stimulus_hz
        s_last_sent_ms = 0;
        s_streaming = true;
        if (s_wifi_up) {
            stimulus_start(s_stimulus_hz);
        }
        ESP_LOGI(TAG, "用户按键：串流已恢复（sense_start max_hz=%d stimulus=%d）",
                 s_max_hz, s_stimulus_hz);
    }
}

void app_sense_dump_diag(void)
{
    sense_status_t st;
    if (!app_sense_get_status(&st)) {
        return;
    }
    uint32_t csi_total = 0, qdrop = 0;
    app_sense_get_diag(&csi_total, &qdrop);
    ESP_LOGI(TAG, "sense_diag: present=%d motion=%.2f(%s) bpm=%.1f q=%.2f | "
             "rate=%.1fHz rssi=%d pc_online=%d streaming=%d calibrating=%d | "
             "csi_total=%lu qdrop=%lu",
             (int)st.present, (double)st.motion, st.motion_cls,
             (double)st.bpm, (double)st.quality,
             (double)st.rate_hz, (int)st.rssi,
             (int)st.pc_online, (int)st.streaming, (int)st.calibrating,
             (unsigned long)csi_total, (unsigned long)qdrop);
}

bool app_sense_get_status(sense_status_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const int64_t now_ms = esp_timer_get_time() / 1000;

    bool loc_has = false, loc_cal = false, loc_present = false;
    float loc_rate = 0, loc_motion = 0;
    int8_t loc_rssi = 0;
    uint32_t seq = 0;
    portENTER_CRITICAL(&s_loc_mux);
    loc_has = s_loc_snap.has;
    loc_cal = s_loc_snap.calibrating;
    loc_present = s_loc_snap.present;
    loc_rate = s_loc_snap.rate_hz;
    loc_motion = s_loc_snap.motion;
    loc_rssi = s_loc_snap.rssi;
    seq = s_loc_snap.seq;
    portEXIT_CRITICAL(&s_loc_mux);

    bool pc_valid = false, pc_present = false, pc_cal = false;
    float pc_motion = 0, pc_bpm = 0, pc_quality = 0, pc_rate = 0;
    int8_t pc_rssi = 0;
    char pc_cls[8] = {0};
    int64_t pc_last = 0;
    portENTER_CRITICAL(&s_pc_mux);
    pc_valid = s_pc.valid;
    pc_present = s_pc.present;
    pc_cal = s_pc.calibrating;
    pc_motion = s_pc.motion;
    pc_bpm = s_pc.bpm;
    pc_quality = s_pc.quality;
    pc_rate = s_pc.rate;
    pc_rssi = s_pc.rssi;
    pc_last = s_pc.last_ms;
    memcpy(pc_cls, s_pc.cls, sizeof(pc_cls));
    portEXIT_CRITICAL(&s_pc_mux);

    const bool pc_online = pc_valid && (now_ms - pc_last) < 10000;
    out->has_data = loc_has || pc_valid;
    out->streaming = s_streaming;
    out->seq = seq;
    out->rate_hz = loc_rate;
    out->rssi = loc_rssi;

    if (pc_online) {
        out->pc_online = true;
        out->present = pc_present;
        out->motion = pc_motion;
        out->bpm = pc_bpm;
        out->quality = pc_quality;
        out->calibrating = pc_cal;
        out->rate_hz = pc_rate > 0 ? pc_rate : loc_rate;
        out->rssi = pc_rsi_safe(pc_rssi, loc_rssi);
        snprintf(out->motion_cls, sizeof(out->motion_cls), "%s",
                 pc_cls[0] ? pc_cls : "idle");
    } else {
        out->pc_online = false;
        out->present = loc_present;
        out->motion = loc_motion;
        out->bpm = 0;
        out->quality = 0;
        out->calibrating = loc_cal;
        snprintf(out->motion_cls, sizeof(out->motion_cls), "%s",
                 loc_motion >= 0.6f ? "walk"
                 : loc_motion >= 0.25f ? "light" : "idle");
    }
    return out->has_data;
}

void app_sense_get_diag(uint32_t *csi_total, uint32_t *qdrop)
{
    if (csi_total) {
        *csi_total = s_csi_total;
    }
    if (qdrop) {
        *qdrop = s_qdrop;
    }
}

