#ifndef APP_WEB_H
#define APP_WEB_H

// 板端 Web 服务（esp_http_server :80）：手机/电脑连同一 WiFi 即可访问，
// 在房间外就能看状态、点校准（含布防-离开延迟校准）、配 WiFi——解开
// "校准要求房间无人、按钮要求有人在场"的死结。Node 页会显示本机 IP。
void app_web_init(void);

#endif // APP_WEB_H
