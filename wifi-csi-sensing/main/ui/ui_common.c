/*
 * ui_common.c — 布局体系运行期部分（见 ui_common.h 的规则说明）。
 */
#include "ui_common.h"
#include "esp_log.h"

/* 8px ASCII 子集字体（fonts/ui_font_lat8.c，lv_font_conv 生成）。
 * 不用内置 montserrat_8：其 cmap/kern 全量表占 7.7KB flash——2MB 双槽
 * 装不下；子集（0x20-0x5F、1bpp、免解压）只要 ~2KB。 */
extern lv_font_t ui_lat8;

static const char *TAG = "UI_COMMON";

bool ui_row_fits(int y, int lh)
{
    if (y < 0 || y + lh > UI_CONTENT_H) {
        ESP_LOGE(TAG, "row overflow: y=%d lh=%d (content %dpx) — 拒绝创建",
                 y, lh, UI_CONTENT_H);
        return false;
    }
    return true;
}

lv_obj_t *ui_content_root(void)
{
    lv_obj_t *root = lv_obj_create(lv_screen_active());
    if (root == NULL) {
        return NULL;
    }
    // remove_style_all 必须在 set_size 之前（LVGL v9 顺序反了会把尺寸清掉）
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_align(root, LV_ALIGN_TOP_LEFT, 0, UI_CONTENT_Y);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

lv_obj_t *ui_row_label(lv_obj_t *root, int y, lv_color_t color, const char *text)
{
    return ui_row_label_x(root, 2, y, color, text);
}

lv_obj_t *ui_row_label_x(lv_obj_t *root, int x, int y, lv_color_t color, const char *text)
{
    if (!ui_row_fits(y, UI_LH_12)) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(root);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
    return label;
}

lv_obj_t *ui_hint_label(lv_obj_t *root, int y, const char *text)
{
    if (!ui_row_fits(y, UI_LH_8)) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(root);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &ui_lat8, 0);
    lv_obj_set_style_text_color(label, UI_STATUS_DIM, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}
