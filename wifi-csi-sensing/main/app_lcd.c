#include "app_lcd.h"
#include "esp_log.h"
#include "driver/spi_master.h"

static const char *TAG = "APP_LCD";

/* Pin definitions */
#define PIN_NUM_SCLK   2
#define PIN_NUM_MOSI   3
#define PIN_NUM_DC     6
#define PIN_NUM_CS     7
#define PIN_NUM_RST    10

/* Display dimensions */
#define LCD_H_RES      160
#define LCD_V_RES      80

/* SPI pixel clock */
#define LCD_PIXEL_CLOCK_HZ  (10 * 1000 * 1000)

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;

void app_lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing LCD...");

    /* SPI bus configuration */
    spi_bus_config_t buscfg = {
        .sclk_io_num     = PIN_NUM_SCLK,
        .mosi_io_num     = PIN_NUM_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");

    /* Panel IO SPI configuration */
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num       = PIN_NUM_CS,
        .dc_gpio_num       = PIN_NUM_DC,
        .spi_mode          = 0,
        .pclk_hz           = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx          = NULL,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_io_handle));
    ESP_LOGI(TAG, "Panel IO initialized");

    /* ST7735 panel configuration */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = false,
        },
        .vendor_config = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7735(s_io_handle, &panel_config, &s_panel_handle));
    ESP_LOGI(TAG, "ST7735 panel created");

    /* Reset and initialize the panel */
    esp_lcd_panel_reset(s_panel_handle);
    esp_lcd_panel_init(s_panel_handle);

    /* Configure for MINI160x80 landscape mode (rotation 3) */
    esp_lcd_panel_set_gap(s_panel_handle, 1, 26);  // Green Tab 160x80 rotation 3: _xstart=1, _ystart=26
    esp_lcd_panel_swap_xy(s_panel_handle, true);     // Rotation: swap X/Y
    esp_lcd_panel_mirror(s_panel_handle, true, false); // Rotation 3: MX only (MV set by swap_xy)

    esp_lcd_panel_invert_color(s_panel_handle, false);
    esp_lcd_panel_invert_color(s_panel_handle, false);
    esp_lcd_panel_disp_on_off(s_panel_handle, true);

    ESP_LOGI(TAG, "LCD initialized (%dx%d)", LCD_H_RES, LCD_V_RES);
}

esp_lcd_panel_handle_t app_lcd_get_panel(void)
{
    return s_panel_handle;
}

esp_lcd_panel_io_handle_t app_lcd_get_io(void)
{
    return s_io_handle;
}
