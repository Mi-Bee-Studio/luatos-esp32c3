#ifndef UI_SPLASH_H
#define UI_SPLASH_H

/**
 * Show splash screen and auto-transition to main UI.
 * Must be called AFTER app_lvgl_init() but BEFORE ui_manager_init().
 * The splash displays for ~2 seconds, then fades to the clock page.
 */
void ui_splash_show(void);

#endif