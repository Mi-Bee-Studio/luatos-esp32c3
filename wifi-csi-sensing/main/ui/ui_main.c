#include "ui_main.h"
#include "ui_common.h"
#include "../app_sense.h"
#include "../app_wifi.h"
#include "../app_lcd.h"
#include "../app_button.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UI_MAIN";

// Page registry: create and destroy functions
static page_create_fn s_page_create[PAGE_COUNT] = {0};
static page_destroy_fn s_page_destroy[PAGE_COUNT] = {0};
// Page names for title bar
static const char *s_page_names[PAGE_COUNT] = {
    "Sense", "NET", "SYS"
};

// Current page state
static page_id_t s_current_page = PAGE_SENSE;
// 跨线程切页请求（USB 任务写，LVGL 定时器读；单字节宽原子足够）
static volatile int s_requested_page = -1;
static lv_timer_t *s_page_req_timer = NULL;
// 标题栏右侧链路状态（1s 刷新）
static lv_timer_t *s_link_timer = NULL;
static lv_obj_t *s_link_label = NULL;
static lv_obj_t *s_page_content = NULL;  // Reference to current page's root content
static lv_obj_t *s_title_label = NULL;    // Title bar label

// ---- 按键语义状态 ----
// ENTER 短按延迟分发：KEY 在按下沿就到，短按动作必须等 RELEASED 才执行，
// 否则长按（700ms LONG_PRESSED）会先触发短按动作（双触发）。
static bool s_enter_pending = false;
// 息屏（L+R 组合键手动 / 空闲超时自动；任意键唤醒）。
// 熄屏期间"随机窥视"：每 1-5min 亮 1-60s，随机显示当前页数据或表情动画。
static bool s_lcd_off = false;
static lv_timer_t *s_lcd_off_timer = NULL;
static uint32_t s_wake_tick = 0;
static uint32_t s_last_activity = 0;   // 最近一次按键（屏保空闲计时）
static uint32_t s_next_peek = 0;       // 下次窥视起始 tick（0=未排程）
static uint32_t s_peek_until = 0;      // 本次窥视结束 tick（0=不在窥视中）
static bool s_peek_expr = false;       // 窥视内容：true=表情动画 false=当前页数据
static lv_obj_t *s_expr_root = NULL;   // 表情覆盖层（窥视结束/唤醒时销毁）
static lv_timer_t *s_expr_timer = NULL;

// 表情动画（250ms 一帧）：两只眼 + 眨眼 + 随机视线；kind=1 为"开心"弧线眼。
static lv_obj_t *s_eye_l = NULL, *s_eye_r = NULL;
static int s_expr_kind = 0;

static void expr_destroy(void)
{
    if (s_expr_timer) {
        lv_timer_del(s_expr_timer);
        s_expr_timer = NULL;
    }
    if (s_expr_root) {
        lv_obj_del(s_expr_root);
        s_expr_root = NULL;
    }
    s_eye_l = s_eye_r = NULL;
}

static void expr_anim_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_expr_root || !s_eye_l || !s_eye_r) {
        return;
    }
    static int gaze = 0, blink_left = 0, bounce = 0;
    if (rand() % 8 == 0) {
        gaze = (rand() % 7) - 3;                    // 视线 ±3px
    }
    if (blink_left == 0 && rand() % 6 == 0) {
        blink_left = 2;                             // 眨眼 ~500ms
    }
    int h = (blink_left > 0) ? 4 : 16;
    if (blink_left > 0) {
        blink_left--;
    }
    if (s_expr_kind == 1) {                          // 开心眼：整体轻跳
        bounce = (rand() % 4 == 0) ? -(rand() % 5) : 0;
        h = 6;                                       // 弧线眼用矮圆片
    }
    lv_obj_set_size(s_eye_l, 16, h);
    lv_obj_set_size(s_eye_r, 16, h);
    lv_obj_align(s_eye_l, LV_ALIGN_TOP_MID, -34 + gaze, 30 + bounce + (16 - h) / 2);
    lv_obj_align(s_eye_r, LV_ALIGN_TOP_MID, 18 + gaze, 30 + bounce + (16 - h) / 2);
}

static void expr_start(void)
{
    s_expr_kind = rand() % 2;
    s_expr_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_expr_root);
    lv_obj_set_size(s_expr_root, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_align(s_expr_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_expr_root, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(s_expr_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_expr_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mouth = lv_label_create(s_expr_root);
    lv_label_set_text(mouth, s_expr_kind == 1 ? "^ ^   ^ ^" : "-   -");
    lv_obj_set_style_text_font(mouth, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(mouth, UI_STATUS_DIM, 0);
    lv_obj_align(mouth, LV_ALIGN_BOTTOM_MID, 0, -6);

    if (s_expr_kind == 0) {                          // 圆眼（会眨）
        s_eye_l = lv_obj_create(s_expr_root);
        s_eye_r = lv_obj_create(s_expr_root);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *e = (i == 0) ? s_eye_l : s_eye_r;
            lv_obj_remove_style_all(e);
            lv_obj_set_size(e, 16, 16);
            lv_obj_set_style_radius(e, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(e, UI_TEXT_COLOR, 0);
            lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
        }
        expr_anim_cb(NULL);
        s_expr_timer = lv_timer_create(expr_anim_cb, 250, NULL);
    } else {                                         // 开心弧线眼：两个矮圆片
        s_eye_l = lv_obj_create(s_expr_root);
        s_eye_r = lv_obj_create(s_expr_root);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *e = (i == 0) ? s_eye_l : s_eye_r;
            lv_obj_remove_style_all(e);
            lv_obj_set_size(e, 16, 6);
            lv_obj_set_style_radius(e, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(e, UI_ACCENT_COLOR, 0);
            lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
            lv_obj_align(e, LV_ALIGN_TOP_MID, (i == 0) ? -34 : 18, 30);
        }
        expr_anim_cb(NULL);
        s_expr_timer = lv_timer_create(expr_anim_cb, 250, NULL);
    }
    ESP_LOGI(TAG, "peek：表情动画（kind=%d）", s_expr_kind);
}

// Forward declarations for page create functions
extern lv_obj_t* ui_sense_create(void);
extern void ui_sense_destroy(void);
extern lv_obj_t* ui_net_create(void);
extern void ui_net_destroy(void);
extern lv_obj_t* ui_sys_create(void);
extern void ui_sys_destroy(void);

// ---------------------------------------------------------------------------
// Toast：屏幕底部悬浮条，1.5s 自毁。所有按键动作的统一视觉回执。
// ---------------------------------------------------------------------------
#define UI_TOAST_H   14
#define UI_TOAST_MS  1500
static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_toast_timer = NULL;

static void toast_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    s_toast_timer = NULL;
}

void ui_toast(const char *text, lv_color_t color)
{
    if (s_toast == NULL) {
        s_toast = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_toast);
        lv_obj_set_size(s_toast, UI_SCREEN_W - 8, UI_TOAST_H);
        lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -1);
        lv_obj_set_style_bg_color(s_toast, UI_TITLE_BG, 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_toast, UI_ACCENT_DIM, 0);
        lv_obj_set_style_border_width(s_toast, 1, 0);
        lv_obj_set_style_radius(s_toast, 4, 0);
        lv_obj_set_style_pad_all(s_toast, 0, 0);
        lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = lv_label_create(s_toast);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
    }
    lv_obj_t *lbl = lv_obj_get_child(s_toast, 0);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_move_foreground(s_toast);
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
    }
    s_toast_timer = lv_timer_create(toast_timer_cb, UI_TOAST_MS, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

static void toast_kill(void)
{
    if (s_toast_timer) {
        lv_timer_del(s_toast_timer);
        s_toast_timer = NULL;
    }
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

// ---------------------------------------------------------------------------
// 息屏：L+R 同按 1s → 提示 0.8s 后关屏（ST7735 DISP OFF，GRAM 保留）；
// 任意键唤醒。息屏期间按键只做唤醒，不透传动作。
// ---------------------------------------------------------------------------
static uint32_t peek_every_rand(void)
{
    return UI_PEEK_EVERY_MIN +
           rand() % (UI_PEEK_EVERY_MAX - UI_PEEK_EVERY_MIN);
}

static uint32_t peek_len_rand(void)
{
    return UI_PEEK_LEN_MIN + rand() % (UI_PEEK_LEN_MAX - UI_PEEK_LEN_MIN);
}

// 进入熄屏（手动组合键 / 空闲超时共用）；窥视排程从这里起算
static void screen_enter_off(void)
{
    s_lcd_off = true;
    s_peek_until = 0;
    uint32_t every = peek_every_rand();
    s_next_peek = lv_tick_get() + every;
    esp_lcd_panel_disp_on_off(app_lcd_get_panel(), false);
    ESP_LOGI(TAG, "screensaver：熄屏，窥视排程 %us 后", (unsigned)(every / 1000));
}

static void lcd_off_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lcd_off_timer = NULL;
    screen_enter_off();
}

static void lcd_combo_cb(void)
{
    s_last_activity = lv_tick_get();
    if (s_lcd_off) {
        s_lcd_off = false;
        s_peek_until = 0;
        expr_destroy();
        esp_lcd_panel_disp_on_off(app_lcd_get_panel(), true);
        return;
    }
    ui_toast("LCD OFF", UI_STATUS_DIM);
    s_lcd_off_timer = lv_timer_create(lcd_off_timer_cb, 800, NULL);
    lv_timer_set_repeat_count(s_lcd_off_timer, 1);
}

static void lcd_wake(void)
{
    s_lcd_off = false;
    s_peek_until = 0;
    expr_destroy();
    esp_lcd_panel_disp_on_off(app_lcd_get_panel(), true);
    ui_toast("LCD ON", UI_STATUS_OK);
}

// ---------------------------------------------------------------------------
// 页面切换与请求消费
// ---------------------------------------------------------------------------
// page_req_timer_cb: LVGL 上下文里消费跨线程切页请求（LVGL 无 OS 模式下
// lv_lock 是空操作，外部任务直接动对象树会堆损坏 —— 实测 Guru Meditation）
static void page_req_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const int req = s_requested_page;
    if (req >= 0 && req < PAGE_COUNT) {
        s_requested_page = -1;
        ui_manager_switch_page((page_id_t)req);
    }
}

// link_timer_cb: 标题栏右侧显示采样率/RSSI/数据来源（两页通用，按链路着色）；
// 顺带做周期性屏保检查（1s 一拍，LVGL 上下文）
static void link_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_link_label == NULL) {
        return;
    }
    /* 周期性屏保：空闲 10min 熄屏；熄屏期间随机窥视（1-5min 间隔、1-60s
     * 时长、内容随机=当前页数据/表情动画），任意键随时完全唤醒。
     * 手动息屏（L+R）与自动走同一条 screen_enter_off 路径。 */
    if (!s_lcd_off) {
        if (s_lcd_off_timer == NULL &&
            lv_tick_diff(lv_tick_get(), s_last_activity) > UI_SCREENSAVER_MS) {
            screen_enter_off();
            ESP_LOGI(TAG, "screensaver：空闲 %u 分钟触发",
                     (unsigned)(UI_SCREENSAVER_MS / 60000));
        }
    } else {
        uint32_t now = lv_tick_get();
        if (s_peek_until) {                              // 窥视中
            if ((int32_t)(now - s_peek_until) >= 0) {    // 到点收屏（回绕安全）
                expr_destroy();
                s_peek_until = 0;
                s_next_peek = now + peek_every_rand();
                esp_lcd_panel_disp_on_off(app_lcd_get_panel(), false);
                ESP_LOGI(TAG, "peek 结束，回熄屏");
            }
        } else if ((int32_t)(now - s_next_peek) >= 0) {  // 到点窥视
            s_peek_expr = (rand() & 1) != 0;
            s_peek_until = now + peek_len_rand();
            esp_lcd_panel_disp_on_off(app_lcd_get_panel(), true);
            if (s_peek_expr) {
                expr_start();
            }
            ESP_LOGI(TAG, "peek：%s %us",
                     s_peek_expr ? "表情动画" : "采集数据",
                     (unsigned)(s_peek_until - now) / 1000);
        }
    }
    sense_status_t st;
    app_sense_get_status(&st);
    lv_color_t color = st.has_data ? (st.pc_online ? UI_STATUS_OK : UI_STATUS_WARN)
                                   : UI_STATUS_DIM;
    lv_obj_set_style_text_color(s_link_label, color, 0);
    /* 不带 dBm 单位（实测 montserrat_12 下 "21Hz -46dBm PC" 宽 101px，右对齐
     * 会撞页点；去掉单位 71px，RSSI<-100dBm 也留 20px 余量）。颜色即链路：
     * PC 绿 / 本地黄 / 无数据灰 */
    if (st.has_data) {
        ui_label_fmt(s_link_label, "%.0fHz %d %s",
                     st.rate_hz, (int)st.rssi, st.pc_online ? "PC" : "LOC");
    } else {
        lv_label_set_text(s_link_label, st.streaming ? "WAIT" : "IDLE");
    }
}

void ui_manager_request_page(page_id_t page)
{
    if (page >= 0 && page < PAGE_COUNT) {
        s_requested_page = (int)page;
    }
}

int ui_page_count(void) {
    return PAGE_COUNT;
}

const char* ui_page_name(page_id_t page) {
    if (page < PAGE_COUNT) {
        return s_page_names[page];
    }
    return "?";
}

// ---------------------------------------------------------------------------
// 按键语义（五键，两页通用 + 页内上下文）
//
//   LEFT/RIGHT  短按  翻页（Sense → NET → SYS）
//   UP          短按  串流开关（全局）
//   DOWN        短按  诊断快照 → 串口日志（全局）
//   CENTER      短按  页主操作：Sense=立即重校准 / NET、SYS=诊断快照
//   CENTER      长按  布防 10s 延迟校准（有人在场按不了"房间无人"的矛盾解；
//                     再长按取消）；Sense 页呼吸行显示倒计时
//   LEFT+RIGHT  同按  息屏 / 唤醒（app_button 组合键）
// 语义速查印在 Node 页底部提示行。
// ---------------------------------------------------------------------------
static void page_key_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    s_last_activity = lv_tick_get();   // 任何按键活动都重置屏保计时
    if (s_lcd_off) {
        lcd_wake();
        s_wake_tick = lv_tick_get();
        return;
    }
    /* 唤醒后 800ms 内吞掉同一次按住的后续事件（如长按 ENTER 唤醒会跟来
     * LONG_PRESSED，不能顺带触发布防） */
    if (lv_tick_diff(lv_tick_get(), s_wake_tick) < 800) {
        s_enter_pending = false;
        return;
    }

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) {
            s_enter_pending = true;   // 短按动作延迟到 RELEASED
        } else if (key == LV_KEY_LEFT) {
            ui_manager_prev_page();
        } else if (key == LV_KEY_RIGHT) {
            ui_manager_next_page();
        } else if (key == LV_KEY_UP) {
            app_sense_toggle_streaming();
            sense_status_t st;
            app_sense_get_status(&st);
            ui_toast(st.streaming ? "STREAM ON" : "STREAM OFF", UI_STATUS_OK);
        } else if (key == LV_KEY_DOWN) {
            app_sense_dump_diag();
            ui_toast("DIAG LOGGED", UI_TEXT_COLOR);
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        /* keypad+group 下 LONG_PRESSED 只可能来自 ENTER（lv_indev 只对 ENTER 发）；
         * 且 lv_event_get_key 对非 KEY 事件恒返回 0，不能拿来判键值 */
        s_enter_pending = false;
        if (app_sense_calib_pending_ms() > 0) {
            app_sense_cancel_calibrate();
            ui_toast("CAL CANCELLED", UI_STATUS_WARN);
        } else {
            app_sense_arm_calibrate(10);
            ui_toast("CAL IN 10s", UI_STATUS_WARN);
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (s_enter_pending) {
            s_enter_pending = false;
            if (s_current_page == PAGE_SENSE) {
                app_sense_request_recalibrate();
                ui_toast("RECALIBRATING", UI_STATUS_WARN);
            } else {
                app_sense_dump_diag();
                ui_toast("DIAG LOGGED", UI_TEXT_COLOR);
            }
        }
    }
}

static void create_title_bar(void) {
    /* Title bar: UI_TITLE_H px at top（= montserrat_12 行高，标题文字恰好放满） */
    lv_obj_t *title_bar = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, UI_SCREEN_W, UI_TITLE_H);
    lv_obj_align(title_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(title_bar, UI_TITLE_BG, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 0, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Page name left（强调色） */
    s_title_label = lv_label_create(title_bar);
    lv_label_set_text(s_title_label, ui_page_name(s_current_page));
    lv_obj_set_style_text_color(s_title_label, UI_TITLE_COLOR, 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 2, 0);

    /* Page dots after the page name（当前页=7px 亮点，其余=3px 暗点。
     * v0.1.0 用户实测：5px 点靠 ACCENT/ACCENT_DIM 颜色区分在 ST7735 上
     * 完全看不出来——改为尺寸差+明度差双保险。槽距 11px、x40 起三槽到
     * 最坏链路串 "25Hz -100 LOC"(x71 起) 留 2px） */
    const int dot_on = 7, dot_off = 3, pitch = 11;
    for (int i = 0; i < PAGE_COUNT; i++) {
        bool cur = (i == s_current_page);
        int d = cur ? dot_on : dot_off;
        lv_obj_t *d_obj = lv_obj_create(title_bar);
        lv_obj_remove_style_all(d_obj);
        lv_obj_set_size(d_obj, d, d);
        lv_obj_align(d_obj, LV_ALIGN_TOP_LEFT, 40 + i * pitch + (dot_on - d) / 2,
                     (UI_TITLE_H - d) / 2);
        lv_obj_set_style_radius(d_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d_obj, cur ? UI_ACCENT_COLOR
                                             : lv_color_hex(0x39415E), 0);
    }

    /* Link status right（由 link_timer_cb 刷新并着色） */
    s_link_label = lv_label_create(title_bar);
    lv_label_set_text(s_link_label, "");
    lv_obj_set_style_text_color(s_link_label, UI_STATUS_DIM, 0);
    lv_obj_set_style_text_font(s_link_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_link_label, LV_ALIGN_RIGHT_MID, -2, 0);
}

static void set_screen_bg(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, UI_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

void ui_manager_init(void) {
    ESP_LOGI(TAG, "Initializing UI page manager (v%s)", APP_FW_VERSION);

    /* Create default group for encoder (physical button) input */
    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);

    /* Register page create/destroy functions */
    s_page_create[PAGE_SENSE] = ui_sense_create;
    s_page_destroy[PAGE_SENSE] = ui_sense_destroy;
    s_page_create[PAGE_NET]   = ui_net_create;
    s_page_destroy[PAGE_NET]  = ui_net_destroy;
    s_page_create[PAGE_SYS]   = ui_sys_create;
    s_page_destroy[PAGE_SYS]  = ui_sys_destroy;

    /* Set dark background */
    set_screen_bg();
    s_last_activity = lv_tick_get();   // 屏保计时从开机起算
    srand((unsigned)(esp_timer_get_time() & 0x7fffffff)); /* 窥视随机序列 */

    /* 跨线程切页请求的消费定时器 */
    s_page_req_timer = lv_timer_create(page_req_timer_cb, 100, NULL);
    /* 标题栏链路状态刷新 */
    s_link_timer = lv_timer_create(link_timer_cb, 1000, NULL);

    /* Create first page */
    s_current_page = PAGE_SENSE;
    create_title_bar();

    if (s_page_create[s_current_page]) {
        s_page_content = s_page_create[s_current_page]();
    }

    /* Add first page content to default group for key events */
    if (s_page_content) {
        lv_obj_add_flag(s_page_content, LV_OBJ_FLAG_CLICKABLE);
        lv_group_add_obj(g, s_page_content);
        lv_group_focus_obj(s_page_content);
        lv_obj_add_event_cb(s_page_content, page_key_handler, LV_EVENT_KEY, NULL);
        /* KEY 只注册了 LV_EVENT_KEY；LONG_PRESSED/RELEASED 走同一个回调
         * （handler 内按 code 分发），必须把事件也挂上 */
        lv_obj_add_event_cb(s_page_content, page_key_handler, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_add_event_cb(s_page_content, page_key_handler, LV_EVENT_RELEASED, NULL);
    }

    /* L+R 息屏组合键 */
    app_button_set_combo_cb(lcd_combo_cb);

    ESP_LOGI(TAG, "UI page manager ready, starting on: %s", ui_page_name(s_current_page));
}

void ui_manager_switch_page(page_id_t page) {
    if (page >= PAGE_COUNT || page == s_current_page) {
        return;
    }

    ESP_LOGI(TAG, "Switching page: %s -> %s",
             ui_page_name(s_current_page), ui_page_name(page));

    page_id_t old_page = s_current_page;
    s_current_page = page;
    s_enter_pending = false;  // 换页后 RELEASED 落在新页，丢弃旧按住的短按意图
    expr_destroy();           // 远程切页可发生在表情窥视中——先拆覆盖层防悬垂
    s_peek_until = 0;         // 窥视随之终止（屏在哪种状态就保持哪种）

    /* Destroy OLD page's timers BEFORE cleaning objects */
    if (s_page_destroy[old_page]) {
        s_page_destroy[old_page]();
    }

    /* Clean screen - removes all children including old title bar and page content */
    lv_obj_clean(lv_screen_active());
    s_link_label = NULL;  // 被 clean 掉了，create_title_bar 会重建
    toast_kill();         // toast 也是屏幕子对象，clean 后指针失效

    /* Set background again after clean */
    set_screen_bg();

    /* Create new title bar */
    create_title_bar();

    /* Create new page content */
    if (s_page_create[s_current_page]) {
        s_page_content = s_page_create[s_current_page]();
    } else {
        s_page_content = NULL;
    }

    /* Add new page content to default group for key events */
    lv_group_t *g = lv_group_get_default();
    if (g && s_page_content) {
        lv_obj_add_flag(s_page_content, LV_OBJ_FLAG_CLICKABLE);
        lv_group_add_obj(g, s_page_content);
        lv_group_focus_obj(s_page_content);
        lv_obj_add_event_cb(s_page_content, page_key_handler, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(s_page_content, page_key_handler, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_add_event_cb(s_page_content, page_key_handler, LV_EVENT_RELEASED, NULL);
    }
}

void ui_manager_next_page(void) {
    page_id_t next = (s_current_page + 1) % PAGE_COUNT;
    ui_manager_switch_page(next);
}

void ui_manager_prev_page(void) {
    page_id_t prev = (s_current_page - 1 + PAGE_COUNT) % PAGE_COUNT;
    ui_manager_switch_page(prev);
}

page_id_t ui_manager_get_current_page(void) {
    return s_current_page;
}
