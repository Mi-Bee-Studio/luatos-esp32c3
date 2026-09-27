#include "app_lcd.h"
#include "app_lvgl.h"
#include "app_usb_serial.h"
#include "app_protocol.h"
#include "app_wifi.h"
#include "app_sense.h"
#include "app_button.h"
#include "app_web.h"
#include "ui/ui_main.h"
#include "ui/ui_common.h"
#include "ui/ui_splash.h"
#include "esp_log.h"
#include <string.h>
#include "nvs_flash.h"
#include "lvgl.h"

static const char *TAG = "MAIN";

// Hello handshake handler — responds to serial client connection
static void hello_handler(cJSON *msg, cJSON *response) {
    (void)msg;
    (void)response;
    app_protocol_send_ok("hello");
}

// 远程切页（serialtap 面板/脚本管理显示；也用于无按键环境的页面验证）
static void ui_page_handler(cJSON *msg, cJSON *response) {
    (void)response;
    cJSON *page = cJSON_GetObjectItem(msg, "page");
    if (!page || !cJSON_IsString(page)) {
        app_protocol_send_error("ui_page", "missing page");
        return;
    }
    page_id_t target;
    if (strcmp(page->valuestring, "sense") == 0) {
        target = PAGE_SENSE;
    } else if (strcmp(page->valuestring, "net") == 0) {
        target = PAGE_NET;
    } else if (strcmp(page->valuestring, "sys") == 0 ||
               strcmp(page->valuestring, "node") == 0 /* 旧名兼容 */) {
        target = PAGE_SYS;
    } else {
        app_protocol_send_error("ui_page", "unknown page (sense|net|sys)");
        return;
    }
    ui_manager_request_page(target); // LVGL 上下文异步消费（线程安全）
    app_protocol_send_ok("ui_page");
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C3 Sense Node v%s starting...", APP_FW_VERSION);

    // 1. NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. LCD + LVGL display（只建 display/buffer，渲染任务未启动 ——
    //    lv_lock 在无 OS 模式下是空操作，见 app_lvgl.c，对象树构建必须在其前）
    app_lcd_init();
    app_lvgl_init();

    // 3. USB Serial + Protocol + 感知节点 + WiFi + 板端 Web
    app_usb_serial_init();
    app_protocol_init();
    app_protocol_register_handler("hello", hello_handler);
    app_protocol_register_handler("ui_page", ui_page_handler);
    app_sense_init();   // 感知节点：协议注册（CSI 在 WiFi 关联后使能）
    app_wifi_init();
    app_web_init();     // 板端 Web :80 —— 手机连同一 WiFi，房间外也能校准

    // 4. Splash（渲染任务启动前构建，转场 timer 首次调度已在 LVGL 任务里）
    ui_splash_show();

    // 5. 启动 LVGL 渲染循环
    app_lvgl_start();
    ESP_LOGI(TAG, "ESP32-C3 Sense Node ready!");
}
