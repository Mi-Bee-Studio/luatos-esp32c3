#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

// LVGL 内置 printf 不支持 %f（实测格式串错位 → 野指针 Guru Meditation）。
// 带浮点的标签一律走本助手（newlib vsnprintf 完整支持）。
static inline void ui_label_fmt(lv_obj_t *label, const char *fmt, ...)
{
    if (label == NULL) {
        return;
    }
    char buf[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(label, buf);
}

// ---------------------------------------------------------------------------
// 布局体系 —— 溢出屏幕问题的结构性修复
//
// 历史 bug：各页面手写 y 偏移，行高按"字号≈占位"估算；实际 LVGL 行高大于
// 字号（如 montserrat_12 行高 15），末行底部越过 80px 屏幕边缘被裁掉。
// 规则：
//   1. 页面行 y 与行高一律用 UI_LH_* 常量声明，禁止裸数字；
//   2. 每行用 UI_ROW_ASSERT(name, y, lh) 做编译期断言，超界直接编译失败；
//   3. 行内容创建用 ui_row_label()/ui_row_bar()，根容器用 ui_content_root()。
// ---------------------------------------------------------------------------
#define UI_SCREEN_W     160
#define UI_SCREEN_H     80
#define UI_TITLE_H      15                 // 标题栏 = montserrat_12 行高
#define UI_CONTENT_Y    UI_TITLE_H         // 内容区起点
#define UI_CONTENT_H    (UI_SCREEN_H - UI_TITLE_H)

// LVGL v9.5 montserrat 实际行高（取自 font/lv_font_montserrat_*.c 的
// .line_height 字段，实测 > 字号 —— 布局必须按这个算，不是字号）
#define UI_LH_8         10
#define UI_LH_12        15
#define UI_LH_14        16
#define UI_LH_16        18
#define UI_LH_22        24
// 自制 8px 子集字体 ui_lat8 的行高（lv_font_conv 生成，见 fonts/）
#define UI_LH_LAT8      7

// 周期性屏保（2026-09-27 规范：所有带屏项目必配，防 TFT 残影/OLED 烧屏）：
// 空闲（无任何按键）超时自动熄屏，任意键唤醒。luatos LCD 30 分钟。
// 周期性屏保（2026-09-27 规范：所有带屏项目必配，防 TFT 残影/OLED 烧屏）：
// 空闲 10min 熄屏；熄屏期间每 1-5min 随机"窥视"一次（1-60s），内容随机
// 二选一：当前页采集数据 或 表情小动画；任意键随时完全唤醒。
#define UI_SCREENSAVER_MS  (10UL * 60 * 1000)
#define UI_PEEK_EVERY_MIN  (60UL * 1000)    /* 窥视间隔下限 1min */
#define UI_PEEK_EVERY_MAX  (300UL * 1000)   /* 窥视间隔上限 5min */
#define UI_PEEK_LEN_MIN    (1000UL)         /* 窥视时长下限 1s */
#define UI_PEEK_LEN_MAX    (60UL * 1000)    /* 窥视时长上限 60s */

// 编译期行边界断言：y + 行高 ≤ 内容区高度，否则构建失败（宏自带分号，
// 调用处不写分号 —— 允许出现在任何声明位置）
#define UI_ROW_ASSERT(name, y, lh) \
    _Static_assert((y) + (lh) <= UI_CONTENT_H, "row '" name "' overflows " #y "+" #lh " > UI_CONTENT_H");

// 运行期行边界守卫（y 为变量时用）：超界打 ERROR 日志并拒绝创建
bool ui_row_fits(int y, int lh);

// 标准内容根容器：160x65，位于标题栏下方，无样式无边框不可滚动。
// 所有页面的对象都挂它下面 —— 任何越界坐标在日志里一眼可见。
lv_obj_t *ui_content_root(void);

// 在根容器上创建一行文本（12px 字号，y 为相对内容区顶部的偏移）
lv_obj_t *ui_row_label(lv_obj_t *root, int y, lv_color_t color, const char *text);

// 同上但带 x 锚（双列网格的值列/右列用；x 为相对内容区左缘的偏移）
lv_obj_t *ui_row_label_x(lv_obj_t *root, int x, int y, lv_color_t color, const char *text);

// 快捷键提示行（8px 暗色，内容区最后一行；Node 页专用，全页语义见此一行）
lv_obj_t *ui_hint_label(lv_obj_t *root, int y, const char *text);

// 动作反馈 toast：屏幕底部悬浮 1.5s 自毁（按键动作的统一视觉回执）。
// 须在 LVGL 上下文调用（按键事件/定时器回调里）。
void ui_toast(const char *text, lv_color_t color);

// Page IDs（三页：Sense 感知 / NET 网络 / SYS 系统——信息分页铺开，少打架）
typedef enum {
    PAGE_SENSE,
    PAGE_NET,
    PAGE_SYS,
    PAGE_COUNT,
} page_id_t;

// 双列网格共享锚点（12px 字号；"WIFI"/"LINK" 标签 ~30px，值锚必须避开；
// 右列键 x86：左值最坏 "99h59m" 50px 到 x86 恰好让位——v2.2.2 复核）
#define UI_COL_L_LABEL_X 2
#define UI_COL_L_VALUE_X 36
#define UI_COL_R_LABEL_X 86
#define UI_COL_R_VALUE_X 122

// Page create/destroy function types
typedef lv_obj_t* (*page_create_fn)(void);
typedef void (*page_destroy_fn)(void);

// Get page count
int ui_page_count(void);

// Get page name for display
const char* ui_page_name(page_id_t page);

// 固件版本（Node 页与启动日志展示）
#define APP_FW_VERSION "0.1.0"

// Common screen background color
#define UI_BG_COLOR    lv_color_hex(0x1A1A2E)
#define UI_TITLE_COLOR lv_color_hex(0x00D4AA)
#define UI_TEXT_COLOR  lv_color_white()

// Accent colors
#define UI_ACCENT_COLOR    lv_color_hex(0x00D4AA)
#define UI_ACCENT_DIM      lv_color_hex(0x007755)

// Status colors
#define UI_STATUS_OK       lv_color_hex(0x00D4AA)
#define UI_STATUS_WARN     lv_color_hex(0xFFD700)
#define UI_STATUS_ERROR    lv_color_hex(0xFF4444)
#define UI_STATUS_DIM      lv_color_hex(0x888888)

// Title bar
#define UI_TITLE_BG        lv_color_hex(0x0F1525)

// Progress bar
#define UI_BAR_BG          lv_color_hex(0x2A2A4A)
#define UI_BAR_LOW         lv_color_hex(0x00D4AA)
#define UI_BAR_MID         lv_color_hex(0xFFD700)
#define UI_BAR_HIGH        lv_color_hex(0xFF4444)
#endif
