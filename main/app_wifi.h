#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_WIFI_AP_SSID "Powerwall-LCD"
#define APP_WIFI_AP_PASS "powerwall"
#define APP_WIFI_AP_IP   "192.168.4.1"

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} app_wifi_ap_t;

typedef void (*app_wifi_ip_cb_t)(const char *ip);

esp_err_t app_wifi_init(void);
esp_err_t app_wifi_connect_saved(void);
esp_err_t app_wifi_connect(const char *ssid, const char *pass);
bool app_wifi_is_connected(void);
esp_err_t app_wifi_scan(app_wifi_ap_t *out, uint16_t max_count, uint16_t *found);
int8_t app_wifi_rssi(void);
const char *app_wifi_ip(void);
const char *app_wifi_portal_ip(void);
void app_wifi_set_ip_callback(app_wifi_ip_cb_t cb);

esp_err_t app_wifi_start_ap(void);
esp_err_t app_wifi_stop_ap(void);
bool app_wifi_ap_is_up(void);
const char *app_wifi_last_error(void);
