#pragma once
#include <stdbool.h>

/* WFP TCP 端点（端口约定 7788，见 homepulse/docs/wfp-protocol.md）：
 * WiFi 在网后 homepulse 可直连——板子脱 USB 仅供电时的常态链路。
 * 与 USB 串口同一份文本行协议；协议出线在 app_usb_send_str 内扇出
 * （USB + TCP 双宿），入线经 app_usb_dispatch_line 走同一 JSON 分发。 */

void app_net_init(void);      /* app_wifi_init 里调一次：起服务任务 */
void app_net_up(bool up);     /* GOT_IP 置 true / WiFi 断开置 false */
void app_net_out_str(const char *line); /* 已带换行的完整协议行 → TCP 客户端 */
