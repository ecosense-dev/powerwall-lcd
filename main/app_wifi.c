#include "app_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_dns.h"
#include "app_net.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "app_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_eg;
static int s_retry;
static bool s_connected;
static bool s_sta_ssid_set;
static bool s_ap_up;
static bool s_connecting;
static char s_ip[16];
static char s_last_err[64];
static uint8_t s_disconnect_reason;

static void set_err(const char *msg)
{
    strlcpy(s_last_err, msg ? msg : "", sizeof(s_last_err));
}

static const char *reason_str(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:
        return "rete non trovata (serve WiFi 2.4 GHz)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "password errata o WPA incompatibile";
    case WIFI_REASON_ASSOC_LEAVE:
        return "disconnesso";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "segnale troppo debole";
    default:
        return NULL;
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
    s_connected = true;
    s_retry = 0;
    s_last_err[0] = '\0';
    ESP_LOGI(TAG, "got ip %s", s_ip);

    /* Backup DNS so Cloudflare hostnames still resolve if the router DNS is flaky. */
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
    esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
    esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_FALLBACK, &dns);

    esp_netif_set_default_netif(event->esp_netif);
    xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    app_net_notify_got_ip(s_ip);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        if (s_sta_ssid_set) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
        s_disconnect_reason = ev ? ev->reason : 0;
        s_connected = false;
        s_ip[0] = '\0';
        const char *why = reason_str(s_disconnect_reason);
        if (why) {
            set_err(why);
        } else {
            snprintf(s_last_err, sizeof(s_last_err), "WiFi errore %u", (unsigned)s_disconnect_reason);
        }
        ESP_LOGW(TAG, "disconnected reason=%u (%s)", s_disconnect_reason, s_last_err);
        if (s_retry < 5) {
            s_retry++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "config AP %s up, http://%s/", APP_WIFI_AP_SSID, APP_WIFI_AP_IP);
    }
}

esp_err_t app_wifi_init(void)
{
    if (s_wifi_eg) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* Channels 1-13 (Italy / ETSI). */
    esp_wifi_set_country_code("IT", true);

    s_wifi_eg = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Power save drops TLS handshakes to Cloudflare (ESP_ERR_HTTP_CONNECT). */
    esp_wifi_set_ps(WIFI_PS_NONE);
    return ESP_OK;
}

esp_err_t app_wifi_start_ap(void)
{
    if (s_ap_up) {
        return ESP_OK;
    }

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, APP_WIFI_AP_SSID, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, APP_WIFI_AP_PASS, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(APP_WIFI_AP_SSID);
    ap.ap.channel = 6;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.beacon_interval = 100;
    ap.ap.pmf_cfg.capable = false;
    ap.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AP config: %s", esp_err_to_name(err));
        return err;
    }
    s_ap_up = true;
    app_dns_start();
    ESP_LOGI(TAG, "AP %s pass=%s", APP_WIFI_AP_SSID, APP_WIFI_AP_PASS);
    return ESP_OK;
}

esp_err_t app_wifi_stop_ap(void)
{
    if (!s_ap_up) {
        return ESP_OK;
    }
    app_dns_stop();
    s_ap_up = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "AP stopped (%s)", esp_err_to_name(err));
    return err;
}

bool app_wifi_ap_is_up(void)
{
    return s_ap_up;
}

esp_err_t app_wifi_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_connecting) {
        return ESP_ERR_INVALID_STATE;
    }
    s_connecting = true;

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.failure_retry_cnt = 4;

    s_retry = 0;
    s_connected = false;
    s_sta_ssid_set = true;
    set_err("connessione in corso...");
    xEventGroupClearBits(s_wifi_eg, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    if (s_ap_up) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    }

    esp_err_t cfg_err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (cfg_err != ESP_OK) {
        ESP_LOGE(TAG, "set_config: %s", esp_err_to_name(cfg_err));
        set_err("config WiFi non valida");
        s_connecting = false;
        return cfg_err;
    }
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        set_err(esp_err_to_name(err));
        s_connecting = false;
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(25000));
    s_connecting = false;
    if (bits & WIFI_CONNECTED_BIT) {
        s_last_err[0] = '\0';
        return ESP_OK;
    }
    if (!s_last_err[0]) {
        set_err("connessione WiFi fallita");
    }
    return ESP_FAIL;
}

esp_err_t app_wifi_connect_saved(void)
{
    if (!app_config_has_wifi()) {
        return ESP_ERR_INVALID_STATE;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    return app_wifi_connect(cfg.wifi_ssid, cfg.wifi_pass);
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t app_wifi_scan(app_wifi_ap_t *out, uint16_t max_count, uint16_t *found)
{
    if (!out || !found || max_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = 0;

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK || ap_num == 0) {
        return err == ESP_OK ? ESP_OK : err;
    }

    wifi_ap_record_t *recs = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!recs) {
        return ESP_ERR_NO_MEM;
    }
    uint16_t to_get = ap_num;
    err = esp_wifi_scan_get_ap_records(&to_get, recs);
    if (err != ESP_OK) {
        free(recs);
        return err;
    }

    uint16_t n = 0;
    for (uint16_t i = 0; i < to_get && n < max_count; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;
        }
        strlcpy(out[n].ssid, (char *)recs[i].ssid, sizeof(out[n].ssid));
        out[n].rssi = recs[i].rssi;
        out[n].open = recs[i].authmode == WIFI_AUTH_OPEN;
        n++;
    }
    *found = n;
    free(recs);
    return ESP_OK;
}

int8_t app_wifi_rssi(void)
{
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) == ESP_OK) {
        return rec.rssi;
    }
    return 0;
}

const char *app_wifi_ip(void)
{
    return s_ip;
}

const char *app_wifi_portal_ip(void)
{
    if (s_connected && s_ip[0]) {
        return s_ip;
    }
    if (s_ap_up) {
        return APP_WIFI_AP_IP;
    }
    return "";
}

const char *app_wifi_last_error(void)
{
    return s_last_err;
}

void app_wifi_set_ip_callback(app_wifi_ip_cb_t cb)
{
    app_net_set_ip_callback(cb);
}
