#include "app_usb_serial.h"
#include <string.h>
#include <stdio.h>
#include "app_net.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define USB_SERIAL_LINE_BUFFER_SIZE 512
#define USB_SERIAL_TASK_STACK_SIZE 3072
#define USB_SERIAL_TASK_PRIORITY 4
#define USB_SERIAL_READ_TIMEOUT_MS 50

static const char *TAG = "USB_SERIAL";
static usb_msg_cb_t g_msg_callback = NULL;
static char g_line_buffer[USB_SERIAL_LINE_BUFFER_SIZE];
static size_t g_line_buffer_pos = 0;
static bool g_task_running = false;

// Process received bytes into lines and dispatch JSON
static void process_received(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            if (g_line_buffer_pos > 0) {
                g_line_buffer[g_line_buffer_pos] = '\0';
                ESP_LOGI(TAG, "Received line: %s", g_line_buffer);
                cJSON *json = cJSON_Parse(g_line_buffer);
                if (json != NULL && g_msg_callback != NULL) {
                    g_msg_callback(json);
                    cJSON_Delete(json);
                }
                g_line_buffer_pos = 0;
                g_line_buffer[0] = '\0';
            }
        } else {
            if (g_line_buffer_pos < (USB_SERIAL_LINE_BUFFER_SIZE - 1)) {
                g_line_buffer[g_line_buffer_pos++] = c;
            } else {
                ESP_LOGW(TAG, "Line buffer overflow, resetting");
                g_line_buffer_pos = 0;
                g_line_buffer[0] = '\0';
            }
        }
    }
}

static void usb_serial_task(void *pvParameters)
{
    uint8_t read_buffer[64];

    ESP_LOGI(TAG, "USB Serial task started (driver API)");

    while (g_task_running) {
        // Use driver API directly — bypasses VFS entirely
        int bytes_read = usb_serial_jtag_read_bytes(read_buffer, sizeof(read_buffer) - 1, pdMS_TO_TICKS(USB_SERIAL_READ_TIMEOUT_MS));
        if (bytes_read > 0) {
            process_received((const char *)read_buffer, bytes_read);
        }
    }

    ESP_LOGI(TAG, "USB Serial task ended");
    vTaskDelete(NULL);
}

void app_usb_serial_init(void)
{
    ESP_LOGI(TAG, "Initializing USB Serial driver");

    g_line_buffer[0] = '\0';
    g_line_buffer_pos = 0;
    g_msg_callback = NULL;
    g_task_running = true;

    usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 2048,
    };
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "USB Serial JTAG driver installed");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "USB Serial JTAG driver already installed (by console)");
    } else {
        ESP_LOGW(TAG, "USB Serial JTAG driver install: %s", esp_err_to_name(ret));
    }

    xTaskCreate(usb_serial_task, "usb_serial_task",
                USB_SERIAL_TASK_STACK_SIZE, NULL,
                USB_SERIAL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "USB Serial driver initialized");
}

void app_usb_register_cb(usb_msg_cb_t cb)
{
    g_msg_callback = cb;
}

/* TCP 端点（app_net.c）入线共用：一行 JSON → 与 USB 同一分发。 */
void app_usb_dispatch_line(const char *line)
{
    if (line == NULL || line[0] == '\0') return;
    ESP_LOGI(TAG, "NET line: %s", line);
    cJSON *json = cJSON_Parse(line);
    if (json != NULL) {
        if (g_msg_callback != NULL) {
            g_msg_callback(json);
        }
        cJSON_Delete(json);
    }
}

void app_usb_send_json(cJSON *json)
{
    if (json == NULL) return;

    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str != NULL) {
        // Use driver API directly for TX
        usb_serial_jtag_write_bytes(json_str, strlen(json_str), 100);
        usb_serial_jtag_write_bytes("\n", 1, 100);
        app_net_out_str(json_str); /* 双宿：TCP 同步收（app_net 补换行） */
        free(json_str);
    }
}

void app_usb_send_str(const char *str)
{
    if (str == NULL) return;
    usb_serial_jtag_write_bytes(str, strlen(str), 100);
    app_net_out_str(str); /* 协议行双宿：USB + TCP（str 已含换行） */
}
