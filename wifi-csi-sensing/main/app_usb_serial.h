#ifndef APP_USB_SERIAL_H
#define APP_USB_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "cJSON.h"

// Initialize USB Serial/JTAG driver
void app_usb_serial_init(void);

// Register callback for received JSON messages
typedef void (*usb_msg_cb_t)(cJSON *msg);
void app_usb_register_cb(usb_msg_cb_t cb);

// Send JSON response over USB serial
void app_usb_send_json(cJSON *json);

// Send raw string over USB serial
void app_usb_send_str(const char *str);

// Dispatch one received line of JSON (shared ingress point for USB and TCP (app_net))
void app_usb_dispatch_line(const char *line);

#endif