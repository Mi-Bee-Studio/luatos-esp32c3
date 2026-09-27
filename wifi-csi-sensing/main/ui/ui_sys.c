/*
 * ui_sys.c — SYS 页：系统健康（v2.3 三页拆分，原 Node 页瘦身而来——
 * WIFI/IP/RSSI 行迁往 NET 页，本页只留运行时与固件信息，行行有余量）。
 *   y= 0  UP 9h52m        HEAP 296K
 *   y=13  CSI 231k dr0    v2.2.2 右对齐
 *   y=26  FW v2.2.2       ESP32-C3 右对齐
 *   y=39  <留白>
 *   y=55  L/R PAGE U STRM D DIAG L+R LCD  8px 提示行
 */
#include "ui_common.h"
#include "../app_sense.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

UI_ROW_ASSERT("sys_up", 0, UI_LH_12)
UI_ROW_ASSERT("sys_csi", 13, UI_LH_12)
UI_ROW_ASSERT("sys_fw", 26, UI_LH_12)
UI_ROW_ASSERT("sys_hints", 55, UI_LH_8)

#define ROW_Y0 0
#define ROW_Y1 13
#define ROW_Y2 26
#define ROW_HINTS_Y 55

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_up_label = NULL;
static lv_obj_t *s_heap_label = NULL;
static lv_obj_t *s_diag_label = NULL;
static lv_timer_t *s_timer = NULL;

static void format_uptime(char *buf, size_t len)
{
    const int64_t s = esp_timer_get_time() / 1000000;
    /* ≤6 字（"99h59m"）：左值区到右列键 x84 只有 48px，带秒必撞 */
    snprintf(buf, len, "%2dh%02dm",
             (int)((s / 3600) % 100), (int)((s / 60) % 60));
}

static void sys_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_up_label) {
        char up[24];
        format_uptime(up, sizeof(up));
        ui_label_fmt(s_up_label, "%s", up);
    }
    if (s_heap_label) {
        ui_label_fmt(s_heap_label, "%uK",
                     (unsigned)(esp_get_free_heap_size() / 1024));
    }
    if (s_diag_label) {
        uint32_t csi_total = 0, qdrop = 0;
        app_sense_get_diag(&csi_total, &qdrop);
        /* 钳位保宽：右端 v2.x.x 右对齐 ~37px（起 x121），值区须 ≤83px——
         * "99999k dr999" 实测 ~80px 恰好放得下；serialtap 日志有全量 */
        uint32_t k = csi_total / 1000;
        if (k > 99999) k = 99999;
        if (qdrop > 999) qdrop = 999;
        ui_label_fmt(s_diag_label, "%luk dr%lu",
                     (unsigned long)k, (unsigned long)qdrop);
    }
}

void ui_sys_destroy(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_up_label = NULL;
    s_heap_label = NULL;
    s_diag_label = NULL;
    s_root = NULL;
}

/* 标签（暗）+ 值（亮）的一对静态文本，按双列锚点铺位。
 * 锚位必须显式落位——不落位就全堆在 x2（v2.2.1 教训）。
 * out 指针可为 NULL（FW 行只展示不回填）——v0.1.0 修复：NULL 直接解引用
 * 会在切到 SYS 页时必崩（Store access fault → 看门狗重启，真机复现）。 */
static void key_value_row(lv_obj_t *root, int y,
                          const char *key_l, const char *val_l,
                          const char *key_r, const char *val_r,
                          lv_obj_t **out_val_l, lv_obj_t **out_val_r)
{
    lv_obj_t *k = ui_row_label_x(root, UI_COL_L_LABEL_X, y, UI_STATUS_DIM, key_l);
    (void)k;
    lv_obj_t *v = ui_row_label_x(root, UI_COL_L_VALUE_X, y, UI_TEXT_COLOR, val_l);
    if (out_val_l) {
        *out_val_l = v;
    }
    if (key_r) {
        k = ui_row_label_x(root, UI_COL_R_LABEL_X, y, UI_STATUS_DIM, key_r);
        (void)k;
        v = ui_row_label_x(root, UI_COL_R_VALUE_X, y, UI_TEXT_COLOR, val_r);
        if (val_r && out_val_r) {
            *out_val_r = v;
        }
    }
}

lv_obj_t* ui_sys_create(void)
{
    s_root = ui_content_root();
    if (!s_root) return NULL;

    key_value_row(s_root, ROW_Y0, "UP", "--", "HEAP", "--",
                  &s_up_label, &s_heap_label);

    key_value_row(s_root, ROW_Y1, "CSI", "--", NULL, NULL, &s_diag_label, NULL);

    /* FW 行：值=版本；右侧芯片名右对齐（ESP32-C3 实测 ~58px → 起 x98，
     * FW 值 x36..73，互不重叠） */
    key_value_row(s_root, ROW_Y2, "FW", "v" APP_FW_VERSION, NULL, NULL,
                  NULL, NULL);
    lv_obj_t *chip = lv_label_create(s_root);
    if (chip) {
        lv_label_set_text(chip, "ESP32-C3");
        lv_obj_set_style_text_font(chip, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(chip, UI_STATUS_DIM, 0);
        lv_obj_align(chip, LV_ALIGN_TOP_RIGHT, -2, ROW_Y2);
    }

    /* 快捷键速查（8px 暗色，底部居中） */
    ui_hint_label(s_root, ROW_HINTS_Y,
                  "L/R PAGE U STRM D DIAG L+R LCD");

    s_timer = lv_timer_create(sys_timer_cb, 1000, NULL);
    sys_timer_cb(NULL); /* 首帧立即填充 */

    return s_root;
}
