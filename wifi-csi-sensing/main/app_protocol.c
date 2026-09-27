#include "app_protocol.h"
#include "app_usb_serial.h"
#include <string.h>
#include <stdio.h>

#define MAX_HANDLERS 16

typedef struct {
    const char *cmd_name;
    protocol_handler_t handler;
} protocol_handler_entry_t;

static protocol_handler_entry_t handlers[MAX_HANDLERS];
static int handler_count = 0;

// Internal callback for USB serial messages
static void usb_msg_received_cb(cJSON *msg);

void app_protocol_init(void) {
    // Register callback with USB serial module
    app_usb_register_cb(usb_msg_received_cb);
}

void app_protocol_register_handler(const char *cmd_name, protocol_handler_t handler) {
    if (handler_count < MAX_HANDLERS) {
        handlers[handler_count].cmd_name = cmd_name;
        handlers[handler_count].handler = handler;
        handler_count++;
    }
}

void app_protocol_send_ok(const char *cmd) {
    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddStringToObject(response, "cmd", cmd);
        cJSON_AddStringToObject(response, "status", "ok");
        app_usb_send_json(response);
        cJSON_Delete(response);
    }
}

void app_protocol_send_error(const char *cmd, const char *error) {
    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddStringToObject(response, "cmd", cmd);
        cJSON_AddStringToObject(response, "status", "error");
        cJSON_AddStringToObject(response, "error", error);
        app_usb_send_json(response);
        cJSON_Delete(response);
    }
}

void app_protocol_send_data(const char *cmd, cJSON *data) {
    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddStringToObject(response, "cmd", cmd);
        cJSON_AddStringToObject(response, "status", "ok");
        if (data) {
            cJSON_AddItemToObject(response, "data", data);
        } else {
            cJSON_AddNullToObject(response, "data");
        }
        app_usb_send_json(response);
        // Don't delete data here as it's owned by the caller
        cJSON_Delete(response);
    }
}

static void usb_msg_received_cb(cJSON *msg) {
    if (!msg) {
        return;
    }
    
    // Extract "cmd" field
    cJSON *cmd_field = cJSON_GetObjectItem(msg, "cmd");
    if (!cmd_field || !cJSON_IsString(cmd_field)) {
        app_protocol_send_error("unknown", "missing or invalid cmd field");
        return;
    }
    
    const char *cmd = cmd_field->valuestring;
    
    // Look up handler
    protocol_handler_t handler = NULL;
    for (int i = 0; i < handler_count; i++) {
        if (strcmp(handlers[i].cmd_name, cmd) == 0) {
            handler = handlers[i].handler;
            break;
        }
    }
    
    if (!handler) {
        app_protocol_send_error(cmd, "unknown command");
        return;
    }
    
    // Call handler — handlers manage their own responses
    handler(msg, NULL);
}