#include "ui_common.h"
#include "../app_sense.h"
#include "lvgl.h"
#include <stdio.h>

/* ---------- File-scope static objects ---------- */
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_presence_card = NULL;   // 状态卡片（描边随状态着色）
static lv_obj_t *s_presence_label = NULL;
static lv_obj_t *s_bpm_label = NULL;
static lv_obj_t *s_motion_bar = NULL;
static lv_obj_t *s_motion_label = NULL;
static lv_obj_t *s_cls_label = NULL;
static lv_timer_t *s_timer = NULL;

/* ---------- 布局（内容区 65px，行边界编译期断言） ----------
 * y= 0..26  状态卡片    montserrat_22 卡片（描边色=状态色）
 * y=31..45  呼吸/倒计时 montserrat_12（v2.1 起 14 号字体裁掉省 flash）
 * y=48..62  运动       montserrat_12：MOT [bar] N% class
 * y=65..    toast 悬浮区（ui_main，不与内容重叠）
 */
UI_ROW_ASSERT("presence_card", 0, 27)
UI_ROW_ASSERT("breath", 31, UI_LH_12)
UI_ROW_ASSERT("motion", 48, UI_LH_12)

#define ROW_CARD_H     27
#define ROW_BREATH_Y   31
#define ROW_MOTION_Y   48

/* ---------- Timer callback: refresh UI every second ---------- */
static void sense_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sense_status_t st;
    app_sense_get_status(&st);
    const int64_t cal_pending = app_sense_calib_pending_ms();

    /* 存在状态（卡片大字 + 描边色）；布防倒计时优先展示 */
    if (s_presence_label) {
        lv_color_t color = UI_STATUS_DIM;
        if (!st.has_data) {
            lv_label_set_text(s_presence_label, st.streaming ? "WAIT PC" : "NO SIGNAL");
        } else if (!st.streaming) {
            lv_label_set_text(s_presence_label, "* PAUSED");
            color = UI_STATUS_WARN;
        } else if (st.calibrating) {
            lv_label_set_text(s_presence_label, "CALIBRATING");
            color = UI_STATUS_WARN;
        } else if (cal_pending > 0) {
            ui_label_fmt(s_presence_label, "CAL IN %ds", (int)((cal_pending + 999) / 1000));
            color = UI_STATUS_WARN;
        } else if (st.present) {
            lv_label_set_text(s_presence_label, "* PRESENT");
            color = UI_STATUS_OK;
        } else {
            lv_label_set_text(s_presence_label, "- ABSENT");
        }
        lv_obj_set_style_text_color(s_presence_label, color, 0);
        if (s_presence_card) {
            lv_obj_set_style_border_color(s_presence_card, color, 0);
        }
    }

    /* 呼吸行：布防时让位给说明文案；精确值来自 PC 引擎，本地模式明说 */
    if (s_bpm_label) {
        if (cal_pending > 0) {
            lv_label_set_text(s_bpm_label, "needs empty room");
            lv_obj_set_style_text_color(s_bpm_label, UI_STATUS_DIM, 0);
        } else if (st.pc_online && st.bpm > 0) {
            ui_label_fmt(s_bpm_label, "BR %.1f  Q%.0f%%", st.bpm, st.quality * 100.0f);
            lv_obj_set_style_text_color(s_bpm_label, UI_TEXT_COLOR, 0);
        } else if (st.pc_online) {
            lv_label_set_text(s_bpm_label, "BR -- (unstable)");
            lv_obj_set_style_text_color(s_bpm_label, UI_STATUS_DIM, 0);
        } else {
            lv_label_set_text(s_bpm_label, "BR -- local mode");
            lv_obj_set_style_text_color(s_bpm_label, UI_STATUS_DIM, 0);
        }
    }

    /* 运动强度（条 + 数值 + 分类） */
    if (s_motion_bar) {
        const int mv = (int)(st.motion * 100.0f);
        lv_bar_set_value(s_motion_bar, mv, LV_ANIM_OFF);
        lv_color_t color = mv < 60 ? UI_BAR_LOW : (mv < 80 ? UI_BAR_MID : UI_BAR_HIGH);
        lv_obj_set_style_bg_color(s_motion_bar, color, LV_PART_INDICATOR);
    }
    if (s_motion_label) {
        if (!st.has_data) {
            lv_label_set_text(s_motion_label, "--");
        } else {
            ui_label_fmt(s_motion_label, "%d", (int)(st.motion * 100.0f));
        }
    }
    if (s_cls_label) {
        lv_label_set_text(s_cls_label, st.has_data ? st.motion_cls : "");
    }
}

/* ---------- Public API ---------- */
void ui_sense_destroy(void) {
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_presence_card = NULL;
    s_presence_label = NULL;
    s_bpm_label = NULL;
    s_motion_bar = NULL;
    s_motion_label = NULL;
    s_cls_label = NULL;
    s_root = NULL;
}

lv_obj_t* ui_sense_create(void) {
    s_root = ui_content_root();
    if (!s_root) return NULL;

    /* ---- 状态卡片：圆角 + 1px 描边（颜色随状态）；宽 158——最坏文案
     * "CALIBRATING" 实测 155px，156 宽会顶到描边 ---- */
    s_presence_card = lv_obj_create(s_root);
    if (s_presence_card) {
        lv_obj_remove_style_all(s_presence_card);
        lv_obj_set_size(s_presence_card, UI_SCREEN_W - 2, ROW_CARD_H);
        lv_obj_align(s_presence_card, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(s_presence_card, 6, 0);
        lv_obj_set_style_bg_color(s_presence_card, UI_TITLE_BG, 0);
        lv_obj_set_style_bg_opa(s_presence_card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_presence_card, UI_STATUS_DIM, 0);
        lv_obj_set_style_border_width(s_presence_card, 1, 0);
        lv_obj_clear_flag(s_presence_card, LV_OBJ_FLAG_SCROLLABLE);

        s_presence_label = lv_label_create(s_presence_card);
        lv_label_set_text(s_presence_label, "NO SIGNAL");
        lv_obj_set_style_text_font(s_presence_label, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(s_presence_label, UI_STATUS_DIM, 0);
        lv_obj_center(s_presence_label);
    }

    /* ---- 呼吸/倒计时行：居中 ---- */
    s_bpm_label = lv_label_create(s_root);
    if (s_bpm_label) {
        lv_label_set_text(s_bpm_label, "BR -- local mode");
        lv_obj_set_style_text_font(s_bpm_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_bpm_label, UI_STATUS_DIM, 0);
        lv_obj_align(s_bpm_label, LV_ALIGN_TOP_MID, 0, ROW_BREATH_Y);
    }

    /* ---- 运动行：MOT [==] 42 motion —— 条缩到 48px、数值去 %（"100" 20px），
     * 给右对齐分类词（最坏 "motion" 实测 40px）让出 ≥12px 间隙；
     * 旧排法 "100%"(31px)×"motion" 必撞（2026-09-27 用户复核报重叠） ---- */
    lv_obj_t *mot_name = ui_row_label(s_root, ROW_MOTION_Y, UI_STATUS_DIM, "MOT");
    (void)mot_name;

    s_motion_bar = lv_bar_create(s_root);
    if (s_motion_bar) {
        lv_obj_set_size(s_motion_bar, 48, 7);
        lv_obj_align(s_motion_bar, LV_ALIGN_TOP_LEFT, 32, ROW_MOTION_Y + 4);
        lv_bar_set_range(s_motion_bar, 0, 100);
        lv_bar_set_value(s_motion_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_motion_bar, UI_BAR_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_motion_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_motion_bar, UI_BAR_LOW, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_motion_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    }

    s_motion_label = lv_label_create(s_root);
    if (s_motion_label) {
        lv_label_set_text(s_motion_label, "--");
        lv_obj_set_style_text_font(s_motion_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_motion_label, UI_TEXT_COLOR, 0);
        lv_obj_align(s_motion_label, LV_ALIGN_TOP_LEFT, 84, ROW_MOTION_Y);
    }

    s_cls_label = lv_label_create(s_root);
    if (s_cls_label) {
        lv_label_set_text(s_cls_label, "");
        lv_obj_set_style_text_font(s_cls_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_cls_label, UI_STATUS_DIM, 0);
        /* 右对齐：左锚 x122 会撞 3 位数 "100%"（x94 起实测 31px → 到 125） */
        lv_obj_align(s_cls_label, LV_ALIGN_TOP_RIGHT, -2, ROW_MOTION_Y);
    }

    /* Create timer for periodic updates */
    s_timer = lv_timer_create(sense_timer_cb, 1000, NULL);

    return s_root;
}
