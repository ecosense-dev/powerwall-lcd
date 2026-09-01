#pragma once

#include "esp_err.h"

#define APP_WIFI_AP_SSID "Powerwall-LCD"
#define APP_WIFI_AP_PASS "powerwall"
#define APP_WIFI_AP_IP   "192.168.4.1"

esp_err_t app_dns_start(void);
void app_dns_stop(void);
