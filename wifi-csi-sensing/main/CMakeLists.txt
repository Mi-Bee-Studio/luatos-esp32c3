idf_component_register(
    SRCS "main.c" "app_lcd.c" "app_lvgl.c" "app_usb_serial.c" "app_protocol.c" "app_wifi.c" "app_button.c" "app_sense.c" "app_web.c" "app_net.c"
    "ui/ui_common.c" "ui/ui_main.c" "ui/ui_sense.c" "ui/ui_net.c" "ui/ui_sys.c" "ui/ui_splash.c"
    "fonts/ui_font_lat8.c"
    "fonts/ui_font_lat8.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES esp_lcd esp_wifi nvs_flash esp_driver_usb_serial_jtag lvgl driver lwip esp_timer esp_netif cjson esp_lcd_st7735 esp_http_server app_update)
