
/**
 * @file ui_font_cn.c
 * @brief Chinese sub-font initialization
 *
 * Fallback chains are set at compile time in ui_font_cn_12.c and
 * ui_font_cn_14.c (.fallback = &lv_font_montserrat_*).
 * This function exists as a lifecycle hook for future initialization.
 */

#include "ui_font_cn.h"
#include "lvgl.h"

void ui_font_init(void)
{
    /* Fallback chains set at compile time:
     *   ui_font_cn_14 -> lv_font_montserrat_14
     *   ui_font_cn_12 -> lv_font_montserrat_12
     *
     * Use &ui_font_cn_14 / &ui_font_cn_12 as the primary font
     * for any label that may contain Chinese text.
     */
}
