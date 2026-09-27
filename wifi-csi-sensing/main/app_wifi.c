#include "app_wifi.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_net.h"
#include "app_protocol.h"
#include "app_sense.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "APP_WIFI";

#define WIFI_NVS_NAMESPACE "wifi-config"
#define WIFI_NVS_SSID_KEY  "ssid"
#define WIFI_NVS_PASS_KEY  "pass"
#define WIFI_MAX_SSID_LEN  32
#define WIFI_MAX_PASS_LEN  64
#define MAX_SCAN_AP_COUNT  10
#define RECONNECT_DELAY_S  5

static bool s_is_connected = false;
static wifi_ap_record_t s_scan_results[MAX_SCAN_AP_COUNT];
static uint16_t s_scan_count = 0;
static bool s_reconnect_pending = false;
static esp_timer_handle_t s_reconnect_timer = NULL;

/* Forward declarations for protocol handlers */
static void wifi_scan_handler(cJSON *msg, cJSON *response);
static void wifi_connect_handler(cJSON *msg, cJSON *response);
static void reconnect_timer_cb(void *arg);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected to AP");
            s_reconnect_pending = false;
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            int reason = ((wifi_event_sta_disconnected_t *)event_data)->reason;
            ESP_LOGW(TAG, "WiFi disconnected, reason: %d", reason);
            s_is_connected = false;
            s_reconnect_pending = false;
            app_net_up(false);
            app_sense_on_wifi(false);

            /* Delete any existing reconnect timer to prevent multiple timers */
            if (s_reconnect_timer != NULL) {
                esp_timer_stop(s_reconnect_timer);
                esp_timer_delete(s_reconnect_timer);
                s_reconnect_timer = NULL;
            }

            /* Set reconnect pending and create one-shot timer */
            ESP_LOGI(TAG, "Reconnecting in %d seconds...", RECONNECT_DELAY_S);
            s_reconnect_pending = true;
            
            const esp_timer_create_args_t reconnect_timer_args = {
                .callback = reconnect_timer_cb,
                .name = "wifi_reconnect"
            };
            esp_timer_create(&reconnect_timer_args, &s_reconnect_timer);
            esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_S * 1000000);
            break;
        }
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            s_is_connected = true;
            app_net_up(true); /* WFP TCP 端点开始服务（homepulse 可脱串口直连） */
            app_sense_on_wifi(true);
        }
    }
}

void app_wifi_init(void)
{
    /* Initialize netif and event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    app_net_init(); /* WFP TCP 端点任务（随 WiFi 起停，见 app_net_up） */

    /* WiFi init config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    /* Configure STA mode with empty credentials (will be set from NVS if available) */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Try to load stored credentials from NVS */
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        char ssid[WIFI_MAX_SSID_LEN] = {0};
        char pass[WIFI_MAX_PASS_LEN] = {0};
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(pass);

        err = nvs_get_str(nvs_handle, WIFI_NVS_SSID_KEY, ssid, &ssid_len);
        if (err == ESP_OK) {
            err = nvs_get_str(nvs_handle, WIFI_NVS_PASS_KEY, pass, &pass_len);
        }

        nvs_close(nvs_handle);

        if (err == ESP_OK && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Found stored credentials, connecting to \"%s\"...", ssid);
            app_wifi_connect(ssid, pass);
        } else {
            ESP_LOGI(TAG, "No stored WiFi credentials found");
        }
    } else {
        ESP_LOGI(TAG, "WiFi NVS namespace not found");
    }

    /* Register protocol handlers */
    app_protocol_register_handler("wifi_scan", wifi_scan_handler);
    app_protocol_register_handler("wifi_connect", wifi_connect_handler);
}

bool app_wifi_is_connected(void)
{
    return s_is_connected;
}

int8_t app_wifi_get_rssi(void)
{
    if (!s_is_connected) {
        return 0;
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return 0;
    }
    return ap_info.rssi;
}

const char* app_wifi_get_ssid(void)
{
    if (!s_is_connected) {
        return NULL;
    }
    static wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return NULL;
    }
    return (const char *)ap_info.ssid;
}

char* app_wifi_get_ip_str(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return NULL;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        buf[0] = '\0';
        return buf;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        buf[0] = '\0';
        return buf;
    }
    snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return buf;
}

int app_wifi_get_scan_count(void)
{
    return s_scan_count;
}

wifi_ap_record_t* app_wifi_get_scan_result(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return NULL;
    }
    return &s_scan_results[index];
}

void app_wifi_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "Cannot connect: SSID is empty");
        return;
    }

    /* Store credentials in NVS */
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, WIFI_NVS_SSID_KEY, ssid);
        if (password != NULL) {
            nvs_set_str(nvs_handle, WIFI_NVS_PASS_KEY, password);
        } else {
            nvs_set_str(nvs_handle, WIFI_NVS_PASS_KEY, "");
        }
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing: 0x%x", err);
    }

    /* Configure and connect */
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    s_reconnect_pending = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
}

void app_wifi_scan(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    ESP_LOGI(TAG, "Starting WiFi scan...");
    s_scan_count = 0;
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: 0x%x", ret);
        return;
    }

    uint16_t ap_count = MAX_SCAN_AP_COUNT;
    ret = esp_wifi_scan_get_ap_records(&ap_count, s_scan_results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: 0x%x", ret);
        s_scan_count = 0;
        return;
    }
    s_scan_count = ap_count;
    ESP_LOGI(TAG, "Scan complete, found %d APs", s_scan_count);
}

/* ---- Protocol handlers ---- */
static void wifi_scan_handler(cJSON *msg, cJSON *response) {
    (void)msg;
    (void)response;
    app_wifi_scan();
    cJSON *aps = cJSON_CreateArray();
    for (int i = 0; i < app_wifi_get_scan_count(); i++) {
        wifi_ap_record_t *ap = app_wifi_get_scan_result(i);
        if (ap) {
            cJSON *ap_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(ap_obj, "ssid", (const char *)ap->ssid);
            cJSON_AddNumberToObject(ap_obj, "rssi", ap->rssi);
            cJSON_AddItemToArray(aps, ap_obj);
        }
    }
    app_protocol_send_data("wifi_scan", aps);
}

static void wifi_connect_handler(cJSON *msg, cJSON *response) {
    (void)response;
    cJSON *ssid = cJSON_GetObjectItem(msg, "ssid");
    cJSON *pass = cJSON_GetObjectItem(msg, "pass");
    if (!ssid || !cJSON_IsString(ssid)) {
        app_protocol_send_error("wifi_connect", "missing ssid");
        return;
    }
    app_wifi_connect(ssid->valuestring, pass ? pass->valuestring : NULL);
    app_protocol_send_ok("wifi_connect");
}

/* Reconnect timer callback */
static void reconnect_timer_cb(void *arg) {
    (void)arg;
    
    if (s_reconnect_pending) {
        ESP_LOGI(TAG, "Performing WiFi reconnect...");
        esp_wifi_connect();
    }
    
    /* Clean up timer */
    if (s_reconnect_timer != NULL) {
        esp_timer_stop(s_reconnect_timer);
        esp_timer_delete(s_reconnect_timer);
        s_reconnect_timer = NULL;
    }
}
