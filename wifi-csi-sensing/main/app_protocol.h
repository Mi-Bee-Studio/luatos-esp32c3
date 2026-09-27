#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include "cJSON.h"

// Protocol handler function type
typedef void (*protocol_handler_t)(cJSON *msg, cJSON *response);

// Initialize protocol parser
void app_protocol_init(void);

// Register a command handler
void app_protocol_register_handler(const char *cmd_name, protocol_handler_t handler);

// Send a success response
void app_protocol_send_ok(const char *cmd);

// Send an error response
void app_protocol_send_error(const char *cmd, const char *error);

// Send a data response (appends data to response JSON)
void app_protocol_send_data(const char *cmd, cJSON *data);

#endif