#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "ui_common.h"

// Initialize page manager
void ui_manager_init(void);

// Switch to a specific page
void ui_manager_switch_page(page_id_t page);

// 线程安全的切页请求（任意任务可调；由 LVGL 定时器在自身上下文消费）
void ui_manager_request_page(page_id_t page);

// Switch to next page (wraps around)
void ui_manager_next_page(void);

// Switch to previous page (wraps around)
void ui_manager_prev_page(void);

// Get current page ID
page_id_t ui_manager_get_current_page(void);

#endif
