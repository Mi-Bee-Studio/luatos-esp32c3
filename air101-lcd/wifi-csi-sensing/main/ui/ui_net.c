/*
 * ui_net.c — NET 页：WiFi 与链路信息独占一页（v2.3 三页拆分，
 * 原 Node 页的 WIFI/IP 行搬来并放宽——SSID 整串可显）。
 *   y= 0  WIFI <ssid 整串（超宽省略号）>
 *   y=13  IP   <ip 地址>
 *   y=26  LINK <PC/LOC>          STRM ON/OFF 右对齐
 *   y=39  RSSI <-50 dBm>
 *   y=55  （提示行归 SYS 页）
 */
#include "ui_common.h"
#include "../app_sense.h"
#include "../app_wifi.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

UI_ROW_ASSERT("net_ssid", 0, UI_LH_12)
UI_ROW_ASSERT("net_ip", 13, UI_LH_12)
UI_ROW_ASSERT("net_link", 26, UI_LH_12)
UI_ROW_ASSERT("net_rssi", 39, UI_LH_12)

/* SSID 值宽：无 RSSI 同行竞争，右缘留 4px → x36..156 = 120px；
 * 12 字符 SSID 实测 ~86px 整串放得下，超长 SSID 由省略号兜底 */
#define NET_SSID_W 120

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_ip_label = NULL;
static lv_obj_t *s_link_label = NULL;
static lv_obj_t *s_strm_label = NULL;
static lv_obj_t *s_rssi_label = NULL;
static lv_timer_t *s_timer = NULL;

static void net_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sense_status_t st;
    app_sense_get_status(&st);
    const bool wifi = app_wifi_is_connected();

    if (s_ssid_label) {
        const char *ssid = wifi ? app_wifi_get_ssid() : NULL;
        lv_label_set_text(s_ssid_label, (ssid && ssid[0]) ? ssid : "--");
    }
    if (s_ip_label) {
        char ip[16];
        if (wifi && app_wifi_get_ip_str(ip, sizeof(ip)) && ip[0]) {
            lv_label_set_text(s_ip_label, ip);
        } else {
            lv_label_set_text(s_ip_label, "--");
        }
    }
    if (s_link_label) {
        lv_label_set_text(s_link_label, st.pc_online ? "PC" : "LOC");
        lv_obj_set_style_text_color(s_link_label,
                                    st.pc_online ? UI_STATUS_OK : UI_STATUS_WARN, 0);
    }
    if (s_strm_label) {
        ui_label_fmt(s_strm_label, "STRM %s", st.streaming ? "ON" : "OFF");
        lv_obj_set_style_text_color(s_strm_label,
                                    st.streaming ? UI_TEXT_COLOR : UI_STATUS_DIM, 0);
    }
    if (s_rssi_label) {
        if (wifi) {
            ui_label_fmt(s_rssi_label, "%d dBm", (int)app_wifi_get_rssi());
        } else {
            lv_label_set_text(s_rssi_label, "--");
        }
    }
}

void ui_net_destroy(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_ssid_label = NULL;
    s_ip_label = NULL;
    s_link_label = NULL;
    s_strm_label = NULL;
    s_rssi_label = NULL;
    s_root = NULL;
}

lv_obj_t* ui_net_create(void)
{
    s_root = ui_content_root();
    if (!s_root) return NULL;

    /* y0：SSID 独占一行（120px 宽，整串可显；无右邻，绝不重叠） */
    lv_obj_t *k = ui_row_label_x(s_root, UI_COL_L_LABEL_X, 0, UI_STATUS_DIM, "WIFI");
    (void)k;
    s_ssid_label = ui_row_label_x(s_root, UI_COL_L_VALUE_X, 0, UI_TEXT_COLOR, "--");
    if (s_ssid_label) {
        lv_obj_set_width(s_ssid_label, NET_SSID_W);
        lv_label_set_long_mode(s_ssid_label, LV_LABEL_LONG_DOT);
    }

    /* y13：IP 独占一行 */
    k = ui_row_label_x(s_root, UI_COL_L_LABEL_X, 13, UI_STATUS_DIM, "IP");
    (void)k;
    s_ip_label = ui_row_label_x(s_root, UI_COL_L_VALUE_X, 13, UI_TEXT_COLOR, "--");

    /* y26：链路来源 + 右对齐串流态（"STRM OFF" ~50px 起 x106，LINK 值 x36..50） */
    k = ui_row_label_x(s_root, UI_COL_L_LABEL_X, 26, UI_STATUS_DIM, "LINK");
    (void)k;
    s_link_label = ui_row_label_x(s_root, UI_COL_L_VALUE_X, 26, UI_TEXT_COLOR, "--");
    s_strm_label = lv_label_create(s_root);
    if (s_strm_label) {
        lv_label_set_text(s_strm_label, "STRM --");
        lv_obj_set_style_text_font(s_strm_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_strm_label, UI_STATUS_DIM, 0);
        lv_obj_align(s_strm_label, LV_ALIGN_TOP_RIGHT, -2, 26);
    }

    /* y39：RSSI 独占一行 */
    k = ui_row_label_x(s_root, UI_COL_L_LABEL_X, 39, UI_STATUS_DIM, "RSSI");
    (void)k;
    s_rssi_label = ui_row_label_x(s_root, UI_COL_L_VALUE_X, 39, UI_TEXT_COLOR, "--");

    s_timer = lv_timer_create(net_timer_cb, 1000, NULL);
    net_timer_cb(NULL); /* 首帧立即填充 */

    return s_root;
}
