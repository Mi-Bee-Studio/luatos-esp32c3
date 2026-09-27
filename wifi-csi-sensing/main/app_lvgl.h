#pragma once

/**
 * Initialize LVGL display driver with LCD backend.
 * Creates draw buffers, flush callback — but NOT the render task.
 * Object-tree construction (splash) is safe only before app_lvgl_start(),
 * because lv_lock is a no-op in LVGL's no-OS mode.
 */
void app_lvgl_init(void);

/** Start the LVGL tick/render task. Call once, after all initial lv_* setup. */
void app_lvgl_start(void);
