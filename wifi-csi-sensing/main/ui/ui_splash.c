/*
 * ui_splash.c — 开机画面。
 * 注意：本页在 LVGL 渲染任务启动【之前】由 app_main 直接构建（main.c 顺序
 * 保证），无跨线程对象树竞争；转场 timer 回调则天然运行在 LVGL 任务里。
 */
#include "ui_splash.h"
#include "ui_common.h"
#include "ui/ui_main.h"
#include "app_button.h"
#include "esp_log.h"

static const char *TAG = "UI_SPLASH";

// Forward declaration for UI manager and button init
void ui_manager_init(void);
void app_button_init(void);

// Timer callback for splash screen transition
static void splash_transition_cb(lv_timer_t *timer) {
    ESP_LOGI(TAG, "Splash transition complete, initializing main UI");

    // Clean splash screen残留元素
    lv_obj_clean(lv_screen_active());
    // 重新设置背景色
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Initialize the main UI manager and button input
    ui_manager_init();
    app_button_init();

    // Delete the timer since it's a one-shot
    lv_timer_del(timer);

    ESP_LOGI(TAG, "Main UI initialized successfully");
}

void ui_splash_show(void) {
    ESP_LOGI(TAG, "Showing splash screen");

    // Create splash screen
    lv_obj_t *splash = lv_obj_create(NULL);
    lv_obj_remove_style_all(splash);

    // Set background color
    lv_obj_set_style_bg_color(splash, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(splash, 0, 0);
    lv_obj_set_style_pad_all(splash, 0, 0);
    lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

    // Load splash screen
    lv_screen_load(splash);

    /* 大标题：y=26，行高 24 → 26..50 */
    lv_obj_t *title_label = lv_label_create(splash);
    lv_label_set_text(title_label, "SENSE NODE");
    lv_obj_set_style_text_color(title_label, UI_TITLE_COLOR, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 26);

    /* 副标题：y=60，行高 15 → 60..75（与大标题无重叠，底部留 5px） */
    lv_obj_t *sub_label = lv_label_create(splash);
    lv_label_set_text(sub_label, "ESP32-C3  v" APP_FW_VERSION);
    lv_obj_set_style_text_color(sub_label, UI_STATUS_DIM, 0);
    lv_obj_set_style_text_font(sub_label, &lv_font_montserrat_12, 0);
    lv_obj_align(sub_label, LV_ALIGN_TOP_MID, 0, 60);

    ESP_LOGI(TAG, "Splash screen displayed");

    // Create one-shot timer for transition (2 seconds)
    lv_timer_t *timer = lv_timer_create(splash_transition_cb, 2000, NULL);
    lv_timer_set_repeat_count(timer, 1);

    ESP_LOGI(TAG, "Splash transition timer started (2s delay)");
}
