/*
 * blink — luatos-esp32c3 基线工程（裸核心板测试固件，不接 Air101 LCD）。
 *
 * 板载 LED D4（GPIO12）每秒翻转；BOOT 键（GPIO9）按住常亮、松开恢复闪烁；
 * 每 10 秒一条心跳日志（uptime/heap），供 serialtap 持续采集验证。
 * 同构拷贝自 esp32-s3-zero/blink：去 WS2812（本板是普通 LED）、S3→C3。
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "app_web.h"

static const char *TAG = "BLINK";

#define LED_D4_GPIO  GPIO_NUM_12  /* 板载 LED D4（低有效与否不影响闪烁；见根 README） */
#define BOOT_GPIO    GPIO_NUM_9   /* 板载 BOOT 键：按下接地；上电按住进下载模式 */

static void led_set(bool on)
{
    gpio_set_level(LED_D4_GPIO, on ? 1 : 0);
}

static bool boot_pressed(void)
{
    return gpio_get_level(BOOT_GPIO) == 0; // 按下接地
}

void app_main(void)
{
    // LED：输出；初始灭
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << LED_D4_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    led_set(false);

    // BOOT 键：输入 + 上拉（按下为 0）
    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    ESP_LOGI(TAG, "blink ready: led=D4/GPIO%d boot=GPIO%d heap=%uK",
             LED_D4_GPIO, BOOT_GPIO,
             (unsigned)(esp_get_free_heap_size() / 1024));

    /* 板端维护页 :80（WiFi 配网 / OTA 刷机 / 状态），自带 APSTA 热点兜底 */
    app_web_init();

    /* 主循环看门狗：1s 一拍喂狗，卡死 >5s 触发 panic 重启自恢复 */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 5000,
        .idle_core_mask = 0, /* IDLE 由默认配置照看，这里只挂主任务 */
        .trigger_panic = true,
    };
    esp_err_t werr = esp_task_wdt_init(&wdt_cfg);
    if (werr != ESP_OK && werr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(werr);
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    int beat = 0;
    while (true) {
        esp_task_wdt_reset();
        if (boot_pressed()) {
            led_set(true); // 按住 BOOT 常亮（交互自检）
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        static bool on;
        on = !on;
        led_set(on);
        if (++beat >= 10) {
            beat = 0;
            ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uK",
                     (long long)(esp_timer_get_time() / 1000000),
                     (unsigned)(esp_get_free_heap_size() / 1024));
        }
    }
}
