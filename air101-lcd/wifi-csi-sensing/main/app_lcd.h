#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_st7735.h"

/**
 * Initialize SPI bus, panel IO, and ST7735 LCD panel.
 * Pin mapping: SCLK=GPIO2, MOSI=GPIO3, RST=GPIO10, DC=GPIO6, CS=GPIO7
 */
void app_lcd_init(void);

/**
 * Get the initialized LCD panel handle.
 */
esp_lcd_panel_handle_t app_lcd_get_panel(void);

/**
 * Get the initialized LCD panel IO handle.
 */
esp_lcd_panel_io_handle_t app_lcd_get_io(void);
