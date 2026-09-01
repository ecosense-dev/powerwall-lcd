#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_SITE_ID_DEFAULT   "1689571507463295"
#define APP_SITE_NAME_DEFAULT ""
#define APP_API_HOST_DEFAULT  "https://api.myteslamate.com"
#define APP_POLL_S_DEFAULT    20
#define APP_POLL_S_MIN        5
#define APP_POLL_S_MAX        300
#define APP_WX_POLL_MIN_DEFAULT 15
#define APP_WX_POLL_MIN_MIN     1
#define APP_WX_POLL_MIN_MAX     120
#define APP_SETTINGS_PIN      "7678"

#define APP_WIFI_SSID_MAX     33
#define APP_WIFI_PASS_MAX     65
#define APP_API_HOST_MAX      128
#define APP_API_TOKEN_MAX     768
#define APP_SITE_ID_MAX       32
#define APP_SITE_NAME_MAX     64
#define APP_GPS_COORD_MAX     16
#define APP_LTE_APN_MAX       48
#define APP_LTE_PIN_MAX       12
#define APP_LTE_USER_MAX      32
#define APP_LTE_PASS_MAX      32

typedef struct {
    char wifi_ssid[APP_WIFI_SSID_MAX];
    char wifi_pass[APP_WIFI_PASS_MAX];
    char api_host[APP_API_HOST_MAX];
    char api_token[APP_API_TOKEN_MAX];
    char site_id[APP_SITE_ID_MAX];
    char site_name[APP_SITE_NAME_MAX];
    char gps_lat[APP_GPS_COORD_MAX];
    char gps_lon[APP_GPS_COORD_MAX];
    char lte_apn[APP_LTE_APN_MAX];
    char lte_pin[APP_LTE_PIN_MAX];
    char lte_user[APP_LTE_USER_MAX];
    char lte_pass[APP_LTE_PASS_MAX];
    uint16_t poll_s;
    uint16_t wx_poll_min;
} app_config_t;

esp_err_t app_config_init(void);
esp_err_t app_config_load(void);
esp_err_t app_config_save(void);

void app_config_get(app_config_t *out);
esp_err_t app_config_set(const app_config_t *in);

bool app_config_has_wifi(void);
bool app_config_has_lte(void);
bool app_config_has_token(void);
bool app_config_needs_wizard(void);

void app_config_set_wifi(const char *ssid, const char *pass);
void app_config_set_token(const char *token);
void app_config_set_host(const char *host);
void app_config_set_site_id(const char *site_id);
void app_config_set_site_name(const char *site_name);
void app_config_set_gps(const char *lat, const char *lon);
void app_config_set_lte(const char *apn, const char *pin, const char *user, const char *pass);
void app_config_set_polls(uint16_t tesla_s, uint16_t wx_min);
bool app_config_has_gps(void);
bool app_config_parse_gps(float *lat, float *lon);
