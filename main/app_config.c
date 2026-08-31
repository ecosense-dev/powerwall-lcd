#include "app_config.h"

#include <ctype.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "app_config";
static const char *NVS_NS = "pw";

static app_config_t s_cfg;

static void apply_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strlcpy(cfg->api_host, APP_API_HOST_DEFAULT, sizeof(cfg->api_host));
    strlcpy(cfg->site_id, APP_SITE_ID_DEFAULT, sizeof(cfg->site_id));
    strlcpy(cfg->site_name, APP_SITE_NAME_DEFAULT, sizeof(cfg->site_name));
    cfg->poll_s = APP_POLL_S_DEFAULT;
}

static void nvs_read_str(nvs_handle_t h, const char *key, char *dst, size_t dst_len)
{
    size_t len = dst_len;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) {
        dst[0] = '\0';
    }
}

static void str_trim(char *s)
{
    if (!s || !s[0]) {
        return;
    }
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

esp_err_t app_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    return app_config_load();
}

esp_err_t app_config_load(void)
{
    apply_defaults(&s_cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no NVS namespace yet, using defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    nvs_read_str(h, "wifi_ssid", s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid));
    nvs_read_str(h, "wifi_pass", s_cfg.wifi_pass, sizeof(s_cfg.wifi_pass));
    nvs_read_str(h, "api_host", s_cfg.api_host, sizeof(s_cfg.api_host));
    nvs_read_str(h, "api_token", s_cfg.api_token, sizeof(s_cfg.api_token));
    nvs_read_str(h, "site_id", s_cfg.site_id, sizeof(s_cfg.site_id));
    nvs_read_str(h, "site_name", s_cfg.site_name, sizeof(s_cfg.site_name));

    uint16_t poll = 0;
    if (nvs_get_u16(h, "poll_s", &poll) == ESP_OK && poll >= 5 && poll <= 300) {
        s_cfg.poll_s = poll;
    }

    if (s_cfg.api_host[0] == '\0') {
        strlcpy(s_cfg.api_host, APP_API_HOST_DEFAULT, sizeof(s_cfg.api_host));
    }
    if (s_cfg.site_id[0] == '\0') {
        strlcpy(s_cfg.site_id, APP_SITE_ID_DEFAULT, sizeof(s_cfg.site_id));
    }
    if (s_cfg.site_name[0] == '\0') {
        strlcpy(s_cfg.site_name, APP_SITE_NAME_DEFAULT, sizeof(s_cfg.site_name));
    }

    nvs_close(h);
    ESP_LOGI(TAG, "loaded ssid='%s' host='%s' site='%s' token=%s",
             s_cfg.wifi_ssid, s_cfg.api_host, s_cfg.site_id,
             s_cfg.api_token[0] ? "set" : "missing");
    return ESP_OK;
}

esp_err_t app_config_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(h, "wifi_ssid", s_cfg.wifi_ssid);
    err = err ? err : nvs_set_str(h, "wifi_pass", s_cfg.wifi_pass);
    err = err ? err : nvs_set_str(h, "api_host", s_cfg.api_host);
    err = err ? err : nvs_set_str(h, "api_token", s_cfg.api_token);
    err = err ? err : nvs_set_str(h, "site_id", s_cfg.site_id);
    err = err ? err : nvs_set_str(h, "site_name", s_cfg.site_name);
    err = err ? err : nvs_set_u16(h, "poll_s", s_cfg.poll_s);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "saved to NVS (%s) ssid='%s'", esp_err_to_name(err), s_cfg.wifi_ssid);
    return err;
}

void app_config_get(app_config_t *out)
{
    if (out) {
        *out = s_cfg;
    }
}

esp_err_t app_config_set(const app_config_t *in)
{
    if (!in) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *in;
    if (s_cfg.poll_s < 5) {
        s_cfg.poll_s = APP_POLL_S_DEFAULT;
    }
    return ESP_OK;
}

bool app_config_has_wifi(void)
{
    return s_cfg.wifi_ssid[0] != '\0';
}

bool app_config_has_token(void)
{
    return s_cfg.api_token[0] != '\0';
}

bool app_config_needs_wizard(void)
{
    /* Token is optional at first boot: the overlay must not trap the user
       after a valid WiFi save. Token is entered from Impostazioni / serial. */
    return !app_config_has_wifi();
}

void app_config_set_wifi(const char *ssid, const char *pass)
{
    strlcpy(s_cfg.wifi_ssid, ssid ? ssid : "", sizeof(s_cfg.wifi_ssid));
    strlcpy(s_cfg.wifi_pass, pass ? pass : "", sizeof(s_cfg.wifi_pass));
    str_trim(s_cfg.wifi_ssid);
    str_trim(s_cfg.wifi_pass);
}

void app_config_set_token(const char *token)
{
    strlcpy(s_cfg.api_token, token ? token : "", sizeof(s_cfg.api_token));
    str_trim(s_cfg.api_token);
}

void app_config_set_host(const char *host)
{
    if (host && host[0]) {
        strlcpy(s_cfg.api_host, host, sizeof(s_cfg.api_host));
    }
}

void app_config_set_site_id(const char *site_id)
{
    if (site_id && site_id[0]) {
        strlcpy(s_cfg.site_id, site_id, sizeof(s_cfg.site_id));
    }
}

void app_config_set_site_name(const char *site_name)
{
    if (site_name && site_name[0]) {
        strlcpy(s_cfg.site_name, site_name, sizeof(s_cfg.site_name));
    }
}
