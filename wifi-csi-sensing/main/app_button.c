#include "app_button.h"

static const char *TAG = "app_button";

// GPIO pin assignments
#define BTN_LEFT_PIN    5
#define BTN_UP_PIN      8
#define BTN_CENTER_PIN  4
#define BTN_DOWN_PIN    13
#define BTN_RIGHT_PIN   9

// LVGL input device
static lv_indev_t *indev = NULL;

// L+R 同按组合键（息屏）：组合期间吞掉单键事件，避免翻页/重复触发
static app_button_combo_cb_t s_combo_cb = NULL;
static uint32_t s_combo_start = 0;
static bool s_combo_fired = false;

void app_button_set_combo_cb(app_button_combo_cb_t cb)
{
    s_combo_cb = cb;
}

// LVGL read callback — reads GPIO directly, called periodically by LVGL
static void lvgl_read_callback(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->key = 0;
    data->state = LV_INDEV_STATE_REL;

    int l = gpio_get_level(BTN_LEFT_PIN);
    int r = gpio_get_level(BTN_RIGHT_PIN);
    int u = gpio_get_level(BTN_UP_PIN);
    int d = gpio_get_level(BTN_DOWN_PIN);
    int c = gpio_get_level(BTN_CENTER_PIN);

    // Debug: log GPIO states every 100 calls
    static int call_count = 0;
    if (++call_count >= 200) {
        call_count = 0;
        ESP_LOGD(TAG, "GPIO L=%d R=%d U=%d D=%d C=%d", l, r, u, d, c);
    }

    if (l == 0 && r == 0) {
        /* 组合键：两键同按 ≥1s 触发一次；期间不产单键事件 */
        uint32_t now = lv_tick_get();
        if (s_combo_start == 0) {
            s_combo_start = now;
        } else if (!s_combo_fired && lv_tick_diff(now, s_combo_start) >= 1000) {
            s_combo_fired = true;
            ESP_LOGI(TAG, "COMBO L+R 触发");
            if (s_combo_cb) {
                s_combo_cb();
            }
        }
        return;
    }
    s_combo_start = 0;
    s_combo_fired = false;

    if (l == 0) {
        data->key = LV_KEY_LEFT;
        data->state = LV_INDEV_STATE_PR;
    } else if (r == 0) {
        data->key = LV_KEY_RIGHT;
        data->state = LV_INDEV_STATE_PR;
    } else if (u == 0) {
        data->key = LV_KEY_UP;
        data->state = LV_INDEV_STATE_PR;
    } else if (d == 0) {
        data->key = LV_KEY_DOWN;
        data->state = LV_INDEV_STATE_PR;
    } else if (c == 0) {
        data->key = LV_KEY_ENTER;
        data->state = LV_INDEV_STATE_PR;
    }

    if (data->key != 0) {
        ESP_LOGD(TAG, "KEY pressed: %d", data->key);
    }
}

void app_button_init(void) {
    ESP_LOGI(TAG, "Initializing button input driver");

    // GPIO pin assignments
    const uint8_t button_pins[] = {
        BTN_LEFT_PIN,    // GPIO5
        BTN_UP_PIN,      // GPIO8
        BTN_CENTER_PIN,  // GPIO4
        BTN_DOWN_PIN,    // GPIO13
        BTN_RIGHT_PIN    // GPIO9
    };

    // Configure each button GPIO as input with pullup (no interrupts)
    for (size_t i = 0; i < sizeof(button_pins) / sizeof(button_pins[0]); i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << button_pins[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        ESP_LOGI(TAG, "Configured button %d on GPIO%d", (int)i, button_pins[i]);
    }

    // Create LVGL input device
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lvgl_read_callback);
    /* 长按语义：CENTER 700ms 触发 LONG_PRESSED（布防校准）；
     * 重复间隔拉到 60s = 关掉按住连发（否则按住 UP 会反复开关串流、
     * 按住 LEFT 反复翻页——LVGL keypad 默认 100ms 连发） */
    lv_indev_set_long_press_time(indev, 700);
    lv_indev_set_long_press_repeat_time(indev, 60000);
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_indev_set_group(indev, g);
    }

    ESP_LOGI(TAG, "Button input driver initialized, group=%p", (void*)g);
}
