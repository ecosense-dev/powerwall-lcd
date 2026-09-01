#include "app_net.h"

#include <string.h>

#include "app_lte.h"
#include "app_wifi.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_net";

static app_net_ip_cb_t s_ip_cb;
static bool s_sntp_started;
static SemaphoreHandle_t s_http_mux;

void app_net_set_ip_callback(app_net_ip_cb_t cb)
{
    s_ip_cb = cb;
}

void app_net_start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = NULL;
    if (esp_netif_sntp_init(&config) == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP started");
    }
}

void app_net_notify_got_ip(const char *ip)
{
    app_net_start_sntp();
    if (s_ip_cb && ip && ip[0]) {
        s_ip_cb(ip);
    }
}

bool app_net_is_online(void)
{
    return app_wifi_is_connected() || app_lte_is_connected();
}

app_net_kind_t app_net_kind(void)
{
    if (app_wifi_is_connected()) {
        return APP_NET_WIFI;
    }
    if (app_lte_is_connected()) {
        return APP_NET_LTE;
    }
    return APP_NET_NONE;
}

const char *app_net_kind_name(void)
{
    switch (app_net_kind()) {
    case APP_NET_WIFI:
        return "WiFi";
    case APP_NET_LTE:
        return "4G";
    default:
        return "offline";
    }
}

const char *app_net_ip(void)
{
    if (app_wifi_is_connected() && app_wifi_ip()[0]) {
        return app_wifi_ip();
    }
    if (app_lte_is_connected() && app_lte_ip()[0]) {
        return app_lte_ip();
    }
    return "";
}

const char *app_net_portal_ip(void)
{
    const char *ip = app_net_ip();
    if (ip[0]) {
        return ip;
    }
    if (app_wifi_ap_is_up()) {
        return APP_WIFI_AP_IP;
    }
    return "";
}

void app_net_http_lock(void)
{
    if (!s_http_mux) {
        s_http_mux = xSemaphoreCreateMutex();
    }
    if (s_http_mux) {
        xSemaphoreTake(s_http_mux, portMAX_DELAY);
    }
}

void app_net_http_unlock(void)
{
    if (s_http_mux) {
        xSemaphoreGive(s_http_mux);
    }
}
