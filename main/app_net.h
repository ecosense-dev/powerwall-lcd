#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef void (*app_net_ip_cb_t)(const char *ip);

typedef enum {
    APP_NET_NONE = 0,
    APP_NET_WIFI,
    APP_NET_LTE,
} app_net_kind_t;

void app_net_set_ip_callback(app_net_ip_cb_t cb);
void app_net_notify_got_ip(const char *ip);
void app_net_start_sntp(void);

bool app_net_is_online(void);
app_net_kind_t app_net_kind(void);
const char *app_net_kind_name(void);
const char *app_net_ip(void);
const char *app_net_portal_ip(void);

/* Serializza Tesla e meteo HTTPS, così un watchdog non chiude il socket dell'altro. */
void app_net_http_lock(void);
void app_net_http_unlock(void);
