#include "weather_client.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app_config.h"
#include "app_net.h"
#include "app_tz.h"
#include "app_wifi.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/error.h"
#include "ui.h"

static const char *TAG = "weather";
#define HTTP_BODY_MAX     (24 * 1024)

const char *weather_wmo_text(int code)
{
    if (code == 0) {
        return "Sereno";
    }
    if (code == 1) {
        return "Poco nuv.";
    }
    if (code == 2) {
        return "Variabile";
    }
    if (code == 3) {
        return "Coperto";
    }
    if (code == 45 || code == 48) {
        return "Nebbia";
    }
    if (code >= 51 && code <= 57) {
        return "Pioviggine";
    }
    if (code >= 61 && code <= 67) {
        return "Pioggia";
    }
    if (code >= 71 && code <= 77) {
        return "Neve";
    }
    if (code >= 80 && code <= 82) {
        return "Rovesci";
    }
    if (code == 85 || code == 86) {
        return "Neve";
    }
    if (code >= 95) {
        return "Temporale";
    }
    return "Meteo";
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    int64_t t0;
    bool saw_data;
} http_acc_t;

static weather_t s_wx;
static SemaphoreHandle_t s_mu;
static TaskHandle_t s_task;
static int64_t s_last_us;
static float s_lat = 999.0f;
static float s_lon = 999.0f;

static void *cj_malloc(size_t n)
{
    void *p = heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n ? n : 1);
}

static int slot_for_hour(int h)
{
    if (h == 8) {
        return 0;
    }
    if (h == 15) {
        return 1;
    }
    if (h == 21) {
        return 2;
    }
    return -1;
}

static void parse_forecast(cJSON *root, weather_t *wx)
{
    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    cJSON *times = hourly ? cJSON_GetObjectItem(hourly, "time") : NULL;
    cJSON *temps = hourly ? cJSON_GetObjectItem(hourly, "temperature_2m") : NULL;
    cJSON *codes = hourly ? cJSON_GetObjectItem(hourly, "weather_code") : NULL;
    if (!cJSON_IsArray(times) || !cJSON_IsArray(temps) || !cJSON_IsArray(codes)) {
        return;
    }
    int n = cJSON_GetArraySize(times);
    char ymds[WEATHER_DAYS][11];
    memset(ymds, 0, sizeof(ymds));
    int nd = 0;
    static const char *titles[WEATHER_DAYS] = {"Oggi", "Domani", "Dopodomani"};
    for (int i = 0; i < n; i++) {
        cJSON *tsj = cJSON_GetArrayItem(times, i);
        if (!cJSON_IsString(tsj) || !tsj->valuestring) {
            continue;
        }
        int Y = 0, Mo = 0, D = 0, h = 0, mi = 0;
        if (sscanf(tsj->valuestring, "%d-%d-%dT%d:%d", &Y, &Mo, &D, &h, &mi) < 4) {
            continue;
        }
        char ymd[11];
        snprintf(ymd, sizeof(ymd), "%04d-%02d-%02d", Y, Mo, D);
        int di = -1;
        for (int d = 0; d < nd; d++) {
            if (strcmp(ymds[d], ymd) == 0) {
                di = d;
                break;
            }
        }
        if (di < 0) {
            if (nd >= WEATHER_DAYS) {
                continue;
            }
            di = nd++;
            strlcpy(ymds[di], ymd, sizeof(ymds[di]));
            strlcpy(wx->day[di].title, titles[di], sizeof(wx->day[di].title));
        }
        int slot = slot_for_hour(h);
        if (slot < 0) {
            continue;
        }
        cJSON *tj = cJSON_GetArrayItem(temps, i);
        cJSON *cj = cJSON_GetArrayItem(codes, i);
        if (!cJSON_IsNumber(tj) || !cJSON_IsNumber(cj)) {
            continue;
        }
        weather_slot_t *s = &wx->day[di].slot[slot];
        s->valid = true;
        s->temp_c = (float)tj->valuedouble;
        s->wmo_code = (int)cj->valuedouble;
        strlcpy(s->condition, weather_wmo_text(s->wmo_code), sizeof(s->condition));
    }
}

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    http_acc_t *acc = (http_acc_t *)evt->user_data;
    if (!acc) {
        return ESP_OK;
    }
    int ms = acc->t0 ? (int)((esp_timer_get_time() - acc->t0) / 1000) : 0;
    if (evt->event_id == HTTP_EVENT_ERROR) {
        ESP_LOGW(TAG, "http +%dms ERROR", ms);
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED || evt->event_id == HTTP_EVENT_HEADERS_SENT ||
        evt->event_id == HTTP_EVENT_ON_FINISH || evt->event_id == HTTP_EVENT_DISCONNECTED) {
        const char *n = "?";
        if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
            n = "CONNECTED";
        } else if (evt->event_id == HTTP_EVENT_HEADERS_SENT) {
            n = "HDR_SENT";
        } else if (evt->event_id == HTTP_EVENT_ON_FINISH) {
            n = "FINISH";
        } else {
            n = "DISCONNECTED";
        }
        ESP_LOGI(TAG, "http +%dms %s", ms, n);
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value &&
        (strcasecmp(evt->header_key, "Content-Length") == 0 ||
         strcasecmp(evt->header_key, "cf-ray") == 0)) {
        ESP_LOGI(TAG, "http +%dms hdr %s: %s", ms, evt->header_key, evt->header_value);
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->data) {
        return ESP_OK;
    }
    if (!acc->saw_data) {
        ESP_LOGI(TAG, "http +%dms DATA first %d bytes", ms, evt->data_len);
        acc->saw_data = true;
    }
    if (acc->len + evt->data_len + 1 > acc->cap) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(acc->buf + acc->len, evt->data, evt->data_len);
    acc->len += evt->data_len;
    acc->buf[acc->len] = '\0';
    return ESP_OK;
}

static void set_invalid(void)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_wx.valid = false;
    xSemaphoreGive(s_mu);
}

static esp_err_t fetch(float lat, float lon)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
             "&current=temperature_2m,weather_code"
             "&hourly=temperature_2m,weather_code&forecast_days=3&timezone=auto",
             lat, lon);

    char *buf = heap_caps_malloc(HTTP_BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = malloc(HTTP_BODY_MAX);
    }
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    buf[0] = '\0';
    http_acc_t acc = {.buf = buf, .cap = HTTP_BODY_MAX, .len = 0, .t0 = esp_timer_get_time()};

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_evt,
        .user_data = &acc,
        .buffer_size = 512,
        .keep_alive_enable = false,
        .user_agent = "PowerwallLCD/1.0",
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(buf);
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Accept", "application/json");

    ESP_LOGI(TAG, "GET meteo heap int=%u net=%s ip=%s rssi=%d timeout=12000ms",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), app_net_kind_name(), app_net_ip(),
             (int)app_wifi_rssi());
    app_net_http_lock();
    acc.t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int elapsed = (int)((esp_timer_get_time() - acc.t0) / 1000);
    int status = esp_http_client_get_status_code(client);
    int sock_errno = esp_http_client_get_errno(client);
    int tls_code = 0;
    int tls_flags = 0;
    if (err != ESP_OK) {
        esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);
    }
    (void)esp_http_client_close(client);
    esp_http_client_cleanup(client);
    app_net_http_unlock();
    if (err != ESP_OK || status != 200) {
        char tls_str[48] = {0};
        if (tls_code) {
            mbedtls_strerror(tls_code, tls_str, sizeof(tls_str));
        }
        ESP_LOGW(TAG,
                 "GET meteo fallito: %s (%d) status=%d errno=%d tls=%d (%s) flags=0x%x %dms "
                 "net=%s ip=%s rssi=%d%s",
                 esp_err_to_name(err), (int)err, status, sock_errno, tls_code, tls_str[0] ? tls_str : "-",
                 tls_flags, elapsed, app_net_kind_name(), app_net_ip(), (int)app_wifi_rssi(),
                 elapsed >= 10500 ? " [TIMEOUT?]" : "");
        free(buf);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    ESP_LOGI(TAG, "GET meteo ok %dms HTTP %d %u bytes", elapsed, status, (unsigned)acc.len);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *current = cJSON_GetObjectItem(root, "current");
    cJSON *temp = current ? cJSON_GetObjectItem(current, "temperature_2m") : NULL;
    cJSON *code = current ? cJSON_GetObjectItem(current, "weather_code") : NULL;
    if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(code)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    weather_t wx;
    memset(&wx, 0, sizeof(wx));
    wx.valid = true;
    wx.temp_c = (float)temp->valuedouble;
    wx.wmo_code = (int)code->valuedouble;
    strlcpy(wx.condition, weather_wmo_text(wx.wmo_code), sizeof(wx.condition));
    parse_forecast(root, &wx);

    char tz_log[48] = "-";
    cJSON *tzname = cJSON_GetObjectItem(root, "timezone");
    cJSON *off = cJSON_GetObjectItem(root, "utc_offset_seconds");
    if (cJSON_IsString(tzname) && tzname->valuestring && tzname->valuestring[0]) {
        strlcpy(tz_log, tzname->valuestring, sizeof(tz_log));
        if (!app_tz_apply_iana(tzname->valuestring) && cJSON_IsNumber(off)) {
            app_tz_apply_offset((int)off->valuedouble);
        }
    } else if (cJSON_IsNumber(off)) {
        app_tz_apply_offset((int)off->valuedouble);
    }

    cJSON_Delete(root);

    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_wx = wx;
    s_lat = lat;
    s_lon = lon;
    s_last_us = esp_timer_get_time();
    xSemaphoreGive(s_mu);
    ESP_LOGI(TAG, "%.1fC %s (wmo=%d) forecast=%s/%s/%s @ %.4f,%.4f tz=%s",
             wx.temp_c, wx.condition, wx.wmo_code,
             wx.day[0].slot[1].valid ? "ok" : "-",
             wx.day[1].slot[1].valid ? "ok" : "-",
             wx.day[2].slot[1].valid ? "ok" : "-",
             lat, lon, tz_log);
    return ESP_OK;
}

static int64_t wx_interval_us(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    uint32_t min = cfg.wx_poll_min;
    if (min < APP_WX_POLL_MIN_MIN) {
        min = APP_WX_POLL_MIN_DEFAULT;
    }
    if (min > APP_WX_POLL_MIN_MAX) {
        min = APP_WX_POLL_MIN_MAX;
    }
    return (int64_t)min * 60LL * 1000000LL;
}

static uint32_t wx_wait_ms(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    uint32_t min = cfg.wx_poll_min;
    if (min < APP_WX_POLL_MIN_MIN) {
        min = APP_WX_POLL_MIN_DEFAULT;
    }
    return min * 60 * 1000;
}

static esp_err_t poll_now(bool *fetched)
{
    if (fetched) {
        *fetched = false;
    }
    float lat = 0, lon = 0;
    if (!app_config_parse_gps(&lat, &lon)) {
        set_invalid();
        static bool logged;
        if (!logged) {
            ESP_LOGI(TAG, "GPS non impostato, skip meteo");
            logged = true;
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (!app_net_is_online()) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (heap < 8000) {
        ESP_LOGW(TAG, "meteo rimandato, heap int=%u", (unsigned)heap);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mu, portMAX_DELAY);
    int64_t last = s_last_us;
    float prev_lat = s_lat;
    float prev_lon = s_lon;
    xSemaphoreGive(s_mu);

    int64_t now = esp_timer_get_time();
    bool moved = fabsf(lat - prev_lat) > 0.0002f || fabsf(lon - prev_lon) > 0.0002f;
    bool stale = (now - last) > wx_interval_us() || last == 0;
    if (!moved && !stale) {
        return ESP_OK;
    }

    esp_err_t err = fetch(lat, lon);
    if (err == ESP_OK && fetched) {
        *fetched = true;
    }
    if (err != ESP_OK && moved) {
        set_invalid();
    }
    return err;
}

static void wx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "task meteo avviato");
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wx_wait_ms()));
        bool fetched = false;
        if (poll_now(&fetched) == ESP_OK && fetched) {
            ui_lock();
            ui_refresh();
            ui_unlock();
        }
    }
}

void weather_client_init(void)
{
    cJSON_Hooks hooks = {.malloc_fn = cj_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
    }
    memset(&s_wx, 0, sizeof(s_wx));
    if (s_task) {
        return;
    }
    BaseType_t ok = xTaskCreateWithCaps(wx_task, "wx", 24576, NULL, 3, &s_task,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "stack PSRAM fallito, uso RAM");
        ok = xTaskCreate(wx_task, "wx", 12288, NULL, 3, &s_task);
    }
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "task meteo NON creato");
        s_task = NULL;
    }
}

void weather_client_get(weather_t *out)
{
    if (!out) {
        return;
    }
    if (!s_mu) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    *out = s_wx;
    xSemaphoreGive(s_mu);
}

void weather_client_invalidate(void)
{
    if (!s_mu) {
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_last_us = 0;
    s_lat = 999.0f;
    s_lon = 999.0f;
    xSemaphoreGive(s_mu);
}

void weather_client_request(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

void weather_client_kick(void)
{
    weather_client_invalidate();
    weather_client_request();
}
