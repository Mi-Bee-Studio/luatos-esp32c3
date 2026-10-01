#ifndef APP_BUTTON_H
#define APP_BUTTON_H

#include <stdint.h>
#include <stdbool.h>

// ESP-IDF includes
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"

// Initialize button GPIOs and LVGL input device
void app_button_init(void);

// L+R 同按 ≥1s 组合键回调（息屏/唤醒由 UI 层决定）。LVGL 上下文调用。
typedef void (*app_button_combo_cb_t)(void);
void app_button_set_combo_cb(app_button_combo_cb_t cb);

#endif
