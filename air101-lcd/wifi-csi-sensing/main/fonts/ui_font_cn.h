/**
 * @file ui_font_cn.h
 * @brief Chinese sub-font declarations and fallback setup for LVGL v9.5.0
 *
 * Provides two Chinese sub-fonts (12px and 14px) containing 48 unique
 * Chinese characters used throughout the UI.
 *
 * Fallback chain: ui_font_cn (Chinese) -> lv_font_montserrat (Latin).
 * Use &ui_font_cn_14 / &ui_font_cn_12 as the primary font for labels
 * that need to display Chinese text. Latin characters automatically
 * fall through to montserrat.
 *
 * Call ui_font_init() once during startup (optional, fallback is set
 * at compile time in the font C files).
 */

#ifndef UI_FONT_CN_H
#define UI_FONT_CN_H

#include "lvgl.h"

/* Declare Chinese sub-fonts (non-const to allow fallback modification) */
extern lv_font_t ui_font_cn_12;
extern lv_font_t ui_font_cn_14;

/**
 * Initialize font fallback chains (optional — fallbacks are set at
 * compile time in the font C files).
 */
void ui_font_init(void);

#endif /* UI_FONT_CN_H */
