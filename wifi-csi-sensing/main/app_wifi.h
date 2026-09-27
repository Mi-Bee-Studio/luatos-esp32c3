#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

// Initialize WiFi station mode
void app_wifi_init(void);

// Check if WiFi is connected
bool app_wifi_is_connected(void);

// Get WiFi RSSI in dBm (returns 0 if not connected)
int8_t app_wifi_get_rssi(void);

// Get WiFi SSID (returns NULL if not connected)
const char* app_wifi_get_ssid(void);

// Get local IP address as string
char* app_wifi_get_ip_str(char *buf, size_t buf_len);

// Get number of scanned APs
int app_wifi_get_scan_count(void);

// Get scanned AP info by index
wifi_ap_record_t* app_wifi_get_scan_result(int index);

// Connect to WiFi with given SSID and password (stores in NVS)
void app_wifi_connect(const char *ssid, const char *password);

// Start WiFi scan
void app_wifi_scan(void);

#endif
