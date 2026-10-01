#include "app_lvgl.h"
#include "app_lcd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "lvgl.h"
#include "draw/sw/lv_draw_sw_utils.h"

static const char *TAG = "APP_LVGL";

/* Display dimensions (must match LCD panel) */
#define DISP_HOR_RES  160
#define DISP_VER_RES  80

/* Draw buffer: 160 * 8 * 2 = 2560 bytes (1/10 screen, partial refresh) */
#define DRAW_BUF_LINES   8
#define DRAW_BUF_SIZE    (DISP_HOR_RES * DRAW_BUF_LINES * sizeof(uint16_t))

/* Semaphore to signal flush completion from DMA callback */


/* Flush completion callback registered with esp_lcd panel IO */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/* LVGL flush callback - sends rendered pixels to LCD via esp_lcd */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    lv_draw_sw_rgb565_swap(px_map, w * h);
    esp_lcd_panel_handle_t panel = app_lcd_get_panel();
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static void lvgl_tick_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL tick task started");
    /* 挂看门狗：渲染循环 5ms 一拍喂狗，卡死（对象树竞争/渲染挂起）>5s
     * panic 重启自恢复——sense 终端无人值守的关键兜底 */
    esp_task_wdt_add(NULL);
    int64_t last_tick = esp_timer_get_time();

    while (1) {
        esp_task_wdt_reset();
        int64_t now = esp_timer_get_time();
        int64_t elapsed_ms = (now - last_tick) / 1000;
        if (elapsed_ms > 0) {
            lv_tick_inc((uint32_t)elapsed_ms);
            last_tick += elapsed_ms * 1000;
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL...");

    /* Initialize LVGL core */
    lv_init();


    /* Create LVGL display */
    lv_display_t *disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
    assert(disp != NULL);

    /* Set color format to RGB565 (16-bit) */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    /* Allocate two draw buffers for partial refresh */
    void *buf1 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    assert(buf1 != NULL);
    void *buf2 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA);
    assert(buf2 != NULL);

    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Set flush callback */
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    /* Register DMA done callback on panel IO to release flush semaphore */
    esp_lcd_panel_io_handle_t io = app_lcd_get_io();
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    esp_lcd_panel_io_register_event_callbacks(io, &cbs, disp);

    ESP_LOGI(TAG, "LVGL display initialized（渲染任务待 app_lvgl_start 启动）");
}

void app_lvgl_start(void)
{
    /* LVGL 无 OS 模式下 lv_lock 是空操作：渲染任务一旦启动，其他任务再动
     * 对象树就是裸竞争（实测堆损坏 Guru Meditation）。所以任务必须最后启动，
     * 之前的所有 lv_* 构建都是单线程安全的。 */
    if (xTaskCreate(lvgl_tick_task, "lvgl_tick", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "LVGL tick task create failed");
        return;
    }
}
