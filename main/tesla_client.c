#include "tesla_client.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "app_config.h"
#include "app_net.h"
#include "app_wifi.h"
#include "energy_model.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mbedtls/error.h"

static const char *TAG = "tesla";
#define HTTP_BODY_MAX   (256 * 1024)
#define HTTP_TIMEOUT_MS 12000
#define HTTP_FD_MAX     16
#define HTTP_FD_SCAN    64

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    int64_t t0;
    bool saw_data;
} http_acc_t;

typedef struct {
    esp_err_t err;
    int status;
    int sock_errno;
    int tls_code;
    int tls_flags;
    esp_err_t tls_esp;
    int elapsed_ms;
    int attempt;
} http_diag_t;

static http_diag_t s_http_diag;
static esp_timer_handle_t s_http_wd;
static volatile bool s_http_killed;
static int s_fds_before[HTTP_FD_MAX];
static int s_n_before;

static void json_id_to_str(const cJSON *item, char *buf, size_t n)
{
    buf[0] = '\0';
    if (!item) {
        return;
    }
    if (cJSON_IsNumber(item)) {
        snprintf(buf, n, "%.0f", item->valuedouble);
    } else if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(buf, item->valuestring, n);
    }
}

static void url_encode(const char *in, char *out, size_t n)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (; in && *in && o + 4 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static void end_date_iso(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    if (t > 1700000000 && localtime_r(&t, &tm)) {
        snprintf(buf, n, "%04d-%02d-%02dT23:59:59+02:00",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    } else {
        strlcpy(buf, "2026-08-31T23:59:59+02:00", n);
    }
}

static void trim_slash(char *host)
{
    size_t n = strlen(host);
    while (n > 0 && host[n - 1] == '/') {
        host[--n] = '\0';
    }
}

static const char *http_ev_name(esp_http_client_event_id_t id)
{
    switch (id) {
    case HTTP_EVENT_ERROR:
        return "ERROR";
    case HTTP_EVENT_ON_CONNECTED:
        return "CONNECTED";
    case HTTP_EVENT_HEADERS_SENT:
        return "HDR_SENT";
    case HTTP_EVENT_ON_HEADER:
        return "HEADER";
    case HTTP_EVENT_ON_DATA:
        return "DATA";
    case HTTP_EVENT_ON_FINISH:
        return "FINISH";
    case HTTP_EVENT_DISCONNECTED:
        return "DISCONNECTED";
    case HTTP_EVENT_REDIRECT:
        return "REDIRECT";
    default:
        return "?";
    }
}

static bool http_transient(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_HTTP_CONNECT:
    case ESP_ERR_HTTP_CONNECTING:
    case ESP_ERR_HTTP_FETCH_HEADER:
    case ESP_ERR_HTTP_EAGAIN:
    case ESP_ERR_HTTP_CONNECTION_CLOSED:
    case ESP_ERR_HTTP_READ_TIMEOUT:
    case ESP_ERR_TIMEOUT:
        return true;
    default:
        return false;
    }
}

static int http_elapsed_ms(const http_acc_t *acc)
{
    if (!acc || acc->t0 == 0) {
        return 0;
    }
    return (int)((esp_timer_get_time() - acc->t0) / 1000);
}

static int http_snap_fds(int *out, int max)
{
    int n = 0;
    /* ESP-IDF mette i socket lwIP in alto (tipicamente 48..63), non 0..15. */
    for (int fd = 0; fd < HTTP_FD_SCAN && n < max; fd++) {
        int type = 0;
        socklen_t len = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0 && type == SOCK_STREAM) {
            out[n++] = fd;
        }
    }
    return n;
}

static void http_shutdown_new_sockets(void)
{
    for (int fd = 0; fd < HTTP_FD_SCAN; fd++) {
        int type = 0;
        socklen_t len = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 || type != SOCK_STREAM) {
            continue;
        }
        bool known = false;
        for (int i = 0; i < s_n_before; i++) {
            if (s_fds_before[i] == fd) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        ESP_LOGW(TAG, "watchdog shutdown fd=%d", fd);
        shutdown(fd, SHUT_RDWR);
    }
}

static void http_wd_cb(void *arg)
{
    (void)arg;
    s_http_killed = true;
    ESP_LOGW(TAG, "watchdog %dms: chiudo i socket della GET", HTTP_TIMEOUT_MS);
    http_shutdown_new_sockets();
    /* cancel_request in connect riapre un socket; close() è no-op se state==INIT. */
}

static void http_session_init(void)
{
    if (s_http_wd) {
        return;
    }
    const esp_timer_create_args_t args = {
        .callback = http_wd_cb,
        .name = "http_wd",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&args, &s_http_wd) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create http_wd failed");
    }
}

static esp_err_t http_perform_deadline(esp_http_client_handle_t client, const http_acc_t *acc)
{
    http_session_init();
    s_n_before = http_snap_fds(s_fds_before, HTTP_FD_MAX);
    s_http_killed = false;
    ESP_LOGI(TAG, "GET start, %d socket TCP già aperti", s_n_before);
    if (s_http_wd) {
        esp_timer_stop(s_http_wd);
        esp_timer_start_once(s_http_wd, (uint64_t)HTTP_TIMEOUT_MS * 1000ULL);
    }
    esp_err_t err = esp_http_client_perform(client);
    if (s_http_wd) {
        esp_timer_stop(s_http_wd);
    }
    if (s_http_killed) {
        ESP_LOGW(TAG, "GET interrotta dal watchdog dopo %dms", http_elapsed_ms(acc));
        return ESP_ERR_TIMEOUT;
    }
    return err;
}

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    http_acc_t *acc = (http_acc_t *)evt->user_data;
    if (!acc) {
        return ESP_OK;
    }
    int ms = http_elapsed_ms(acc);
    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (evt->header_key && evt->header_value &&
            (strcasecmp(evt->header_key, "Content-Length") == 0 ||
             strcasecmp(evt->header_key, "Content-Type") == 0 ||
             strcasecmp(evt->header_key, "cf-ray") == 0 ||
             strcasecmp(evt->header_key, "server") == 0)) {
            ESP_LOGI(TAG, "http +%dms hdr %s: %s", ms, evt->header_key, evt->header_value);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        if (!evt->data) {
            break;
        }
        if (!acc->saw_data) {
            ESP_LOGI(TAG, "http +%dms DATA first %d bytes", ms, evt->data_len);
            acc->saw_data = true;
        }
        if (acc->len + evt->data_len + 1 > acc->cap) {
            ESP_LOGW(TAG, "http +%dms body overflow cap=%u", ms, (unsigned)acc->cap);
            return ESP_ERR_NO_MEM;
        }
        memcpy(acc->buf + acc->len, evt->data, evt->data_len);
        acc->len += evt->data_len;
        acc->buf[acc->len] = '\0';
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGW(TAG, "http +%dms ERROR", ms);
        break;
    default:
        ESP_LOGI(TAG, "http +%dms %s", ms, http_ev_name(evt->event_id));
        break;
    }
    return ESP_OK;
}

static const char *tls_alert_name(int code)
{
    switch (code) {
    case 40:
        return "handshake_failure";
    case 47:
        return "illegal_parameter";
    case 80:
        return "internal_error";
    case 86:
        return "inappropriate_fallback";
    case 90:
        return "user_canceled";
    case 112:
        return "unrecognized_name";
    default:
        return NULL;
    }
}

static void format_net_error(char *msg, size_t n, esp_err_t err)
{
    int ms = s_http_diag.elapsed_ms;
    int tls = s_http_diag.tls_code;
    switch (err) {
    case ESP_ERR_TIMEOUT:
    case ESP_ERR_HTTP_READ_TIMEOUT:
        snprintf(msg, n, "Timeout HTTP (%d ms)", ms);
        break;
    case ESP_ERR_HTTP_CONNECT:
    case ESP_ERR_HTTP_CONNECTING: {
        const char *alert = tls_alert_name(tls);
        if (alert) {
            snprintf(msg, n, "TLS %s (%d ms)", alert, ms);
        } else if (tls) {
            snprintf(msg, n, "TLS/connect (%d ms, tls=%d)", ms, tls);
        } else if (ms >= HTTP_TIMEOUT_MS - 500) {
            snprintf(msg, n, "Timeout connect (%d ms)", ms);
        } else {
            snprintf(msg, n, "Connessione HTTP (%d ms)", ms);
        }
        break;
    }
    case ESP_ERR_HTTP_CONNECTION_CLOSED:
        snprintf(msg, n, "HTTP chiuso dal server (%d ms)", ms);
        break;
    case ESP_ERR_HTTP_FETCH_HEADER:
        snprintf(msg, n, "Timeout header HTTP (%d ms)", ms);
        break;
    case ESP_ERR_HTTP_EAGAIN:
        snprintf(msg, n, "HTTP EAGAIN (%d ms)", ms);
        break;
    case ESP_ERR_NO_MEM:
        snprintf(msg, n, "Memoria HTTP insufficiente");
        break;
    default:
        snprintf(msg, n, "Rete: %s (%d ms)", esp_err_to_name(err), ms);
        break;
    }
}

static void log_http_fail(const char *url, int attempt)
{
    char tls_str[48] = {0};
    const char *alert = tls_alert_name(s_http_diag.tls_code);
    if (alert) {
        snprintf(tls_str, sizeof(tls_str), "alert %s", alert);
    } else if (s_http_diag.tls_code < 0) {
        mbedtls_strerror(s_http_diag.tls_code, tls_str, sizeof(tls_str));
    } else if (s_http_diag.tls_code) {
        snprintf(tls_str, sizeof(tls_str), "code=%d", s_http_diag.tls_code);
    }
    ESP_LOGW(TAG,
             "GET fail attempt=%d %s elapsed=%dms err=%s (%d) errno=%d (%s) tls_esp=%s tls=%d (%s) "
             "flags=0x%x status=%d net=%s ip=%s rssi=%d heap_int=%u%s url=%s",
             attempt, http_transient(s_http_diag.err) ? "transient" : "fatal", s_http_diag.elapsed_ms,
             esp_err_to_name(s_http_diag.err), (int)s_http_diag.err, s_http_diag.sock_errno,
             (s_http_diag.sock_errno && strerror(s_http_diag.sock_errno)) ? strerror(s_http_diag.sock_errno) : "-",
             esp_err_to_name(s_http_diag.tls_esp), s_http_diag.tls_code, tls_str[0] ? tls_str : "-",
             s_http_diag.tls_flags, s_http_diag.status, app_net_kind_name(), app_net_ip(), (int)app_wifi_rssi(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             s_http_diag.elapsed_ms >= HTTP_TIMEOUT_MS - 500 ? " [TIMEOUT]" : "", url);
}

static esp_err_t http_get(const char *url, const char *token, char **out_body, int *out_status)
{
    *out_body = NULL;
    *out_status = 0;
    memset(&s_http_diag, 0, sizeof(s_http_diag));

    ESP_LOGI(TAG, "heap int=%u psram=%u net=%s ip=%s rssi=%d",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), app_net_kind_name(), app_net_ip(),
             (int)app_wifi_rssi());

    char *buf = heap_caps_malloc(HTTP_BODY_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = malloc(HTTP_BODY_MAX);
    }
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    size_t auth_n = strlen(token) + 16;
    char *auth = heap_caps_malloc(auth_n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!auth) {
        auth = malloc(auth_n);
    }
    if (!auth) {
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    snprintf(auth, auth_n, "Bearer %s", token);

    http_acc_t acc = {.buf = buf, .cap = HTTP_BODY_MAX, .len = 0};
    esp_err_t err = ESP_FAIL;

    for (int attempt = 1; attempt <= 2; attempt++) {
        buf[0] = '\0';
        acc.len = 0;
        acc.saw_data = false;
        acc.t0 = esp_timer_get_time();

        esp_http_client_config_t cfg = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = HTTP_TIMEOUT_MS,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = http_evt,
            .user_data = &acc,
            .buffer_size = 4096,
            .keep_alive_enable = false,
            .user_agent = "PowerwallLCD/1.0",
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            err = ESP_FAIL;
            s_http_diag.err = err;
            s_http_diag.attempt = attempt;
            ESP_LOGW(TAG, "esp_http_client_init failed");
            break;
        }
        esp_http_client_set_header(client, "Authorization", auth);
        esp_http_client_set_header(client, "X-Authorization", auth);
        esp_http_client_set_header(client, "Accept", "application/json");

        ESP_LOGI(TAG, "GET attempt=%d deadline=%dms %s", attempt, HTTP_TIMEOUT_MS, url);
        app_net_http_lock();
        err = http_perform_deadline(client, &acc);
        int elapsed = http_elapsed_ms(&acc);
        int status = esp_http_client_get_status_code(client);
        int sock_errno = esp_http_client_get_errno(client);
        int tls_code = 0;
        int tls_flags = 0;
        esp_err_t tls_esp = ESP_OK;
        if (err != ESP_OK) {
            tls_esp = esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);
        }
        s_http_diag.err = err;
        s_http_diag.status = status;
        s_http_diag.sock_errno = sock_errno;
        s_http_diag.tls_code = tls_code;
        s_http_diag.tls_flags = tls_flags;
        s_http_diag.tls_esp = tls_esp;
        s_http_diag.elapsed_ms = elapsed;
        s_http_diag.attempt = attempt;
        *out_status = status;
        (void)esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (s_http_killed) {
            http_shutdown_new_sockets();
        }
        app_net_http_unlock();

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "GET ok attempt=%d %dms HTTP %d %u bytes %s", attempt, elapsed, status,
                     (unsigned)acc.len, url);
            free(auth);
            *out_body = buf;
            return ESP_OK;
        }

        log_http_fail(url, attempt);
        if (attempt == 1 && http_transient(err)) {
            ESP_LOGW(TAG, "retry GET tra 500ms");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        break;
    }

    free(auth);
    free(buf);
    return err;
}

static void set_http_error(int status, esp_err_t err, const char *fallback)
{
    char msg[128];
    if (status == 401 || status == 403) {
        snprintf(msg, sizeof(msg), "Token non valido (%d)", status);
    } else if (status == 404) {
        snprintf(msg, sizeof(msg), "Impianto non trovato (404)");
    } else if (status == 429) {
        snprintf(msg, sizeof(msg), "Troppe richieste (429)");
    } else if (status > 0) {
        snprintf(msg, sizeof(msg), "Errore API HTTP %d", status);
    } else if (err != ESP_OK) {
        format_net_error(msg, sizeof(msg), err);
    } else {
        strlcpy(msg, fallback ? fallback : "Errore di rete", sizeof(msg));
    }
    ESP_LOGW(TAG, "display: %s", msg);
    energy_model_set_error(msg);
}

static float json_num(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) {
        return 0;
    }
    if (cJSON_IsNumber(v)) {
        return (float)v->valuedouble;
    }
    if (cJSON_IsString(v) && v->valuestring) {
        return (float)atof(v->valuestring);
    }
    return 0;
}

static float json_num_keys(const cJSON *obj, const char *const *keys)
{
    for (int i = 0; keys && keys[i]; i++) {
        const cJSON *v = cJSON_GetObjectItem(obj, keys[i]);
        if (!v) {
            continue;
        }
        if (cJSON_IsNumber(v)) {
            return (float)v->valuedouble;
        }
        if (cJSON_IsString(v) && v->valuestring) {
            return (float)atof(v->valuestring);
        }
    }
    return 0;
}

static bool parse_live(const char *body, energy_live_t *live)
{
    memset(live, 0, sizeof(*live));
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGW(TAG, "live JSON parse fail");
        return false;
    }
    cJSON *resp = cJSON_GetObjectItem(root, "response");
    if (!cJSON_IsObject(resp)) {
        resp = root;
    }

    static const char *k_solar[] = {"solar_power", "solarPower", "solar", NULL};
    static const char *k_home[] = {"load_power", "loadPower", "home_power", "instantaneous_power", NULL};
    static const char *k_batt[] = {"battery_power", "batteryPower", "battery", NULL};
    static const char *k_grid[] = {"grid_power", "gridPower", "grid", NULL};
    static const char *k_soc[] = {"percentage_charged", "percentageCharged", "battery_soc", NULL};

    live->solar_w = json_num_keys(resp, k_solar);
    live->home_w = json_num_keys(resp, k_home);
    live->battery_w = json_num_keys(resp, k_batt);
    live->grid_w = json_num_keys(resp, k_grid);
    live->soc_pct = json_num_keys(resp, k_soc);
    live->energy_left_wh = json_num(resp, "energy_left");
    const cJSON *storm = cJSON_GetObjectItem(resp, "storm_mode_active");
    live->storm = cJSON_IsTrue(storm);
    live->grid_active = true;
    const cJSON *gs = cJSON_GetObjectItem(resp, "grid_status");
    if (cJSON_IsString(gs) && gs->valuestring) {
        live->grid_active = strcasecmp(gs->valuestring, "Inactive") != 0;
    }

    const cJSON *sn = cJSON_GetObjectItem(resp, "site_name");
    if (cJSON_IsString(sn) && sn->valuestring && sn->valuestring[0]) {
        strlcpy(live->site_name, sn->valuestring, sizeof(live->site_name));
        app_config_set_site_name(sn->valuestring);
    } else {
        app_config_t cfg;
        app_config_get(&cfg);
        strlcpy(live->site_name, cfg.site_name, sizeof(live->site_name));
    }

    time_t t = time(NULL);
    struct tm tm;
    if (t > 1700000000 && localtime_r(&t, &tm)) {
        strftime(live->updated, sizeof(live->updated), "%H:%M:%S", &tm);
    } else {
        strlcpy(live->updated, "--:--", sizeof(live->updated));
    }

    energy_model_derive_status(live);
    live->valid = true;
    ESP_LOGI(TAG, "live solar=%.0fW load=%.0fW batt=%.0fW grid=%.0fW soc=%.0f%%",
             live->solar_w, live->home_w, live->battery_w, live->grid_w, live->soc_pct);
    cJSON_Delete(root);
    return true;
}

static int iso_today_slot(const char *ts, const char *today_ymd)
{
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0;
    if (!ts || sscanf(ts, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se) < 5) {
        return -1;
    }
    /* UTC 'Z' or +00:00 → Europe/Rome CEST (+2) in summer. */
    if (strchr(ts, 'Z') || strstr(ts, "+00:00")) {
        h += 2;
        if (h >= 24) {
            h -= 24;
            D++;
        }
    }
    char ymd[16];
    struct tm tsm = {0};
    tsm.tm_year = Y - 1900;
    tsm.tm_mon = Mo - 1;
    tsm.tm_mday = D;
    strftime(ymd, sizeof(ymd), "%Y-%m-%d", &tsm);
    if (today_ymd[0] && strncmp(ymd, today_ymd, 10) != 0) {
        return -1;
    }
    int slot = (h * 60 + mi) / 15;
    if (slot < 0) {
        slot = 0;
    }
    if (slot >= ENERGY_DAY_MAX_POINTS) {
        slot = ENERGY_DAY_MAX_POINTS - 1;
    }
    return slot;
}

static bool parse_day_power(const char *body, energy_day_t *day)
{
    memset(day, 0, sizeof(*day));
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return false;
    }
    cJSON *resp = cJSON_GetObjectItem(root, "response");
    cJSON *series = resp ? cJSON_GetObjectItem(resp, "time_series") : NULL;
    if (!cJSON_IsArray(series)) {
        cJSON_Delete(root);
        return false;
    }

    char today_ymd[16] = {0};
    time_t tnow = time(NULL);
    struct tm tmnow;
    if (tnow > 1700000000 && localtime_r(&tnow, &tmnow)) {
        strftime(today_ymd, sizeof(today_ymd), "%Y-%m-%d", &tmnow);
    }

    int n_all = cJSON_GetArraySize(series);
    int filled = 0;
    const char *first_ts = "";
    const char *last_ts = "";
    bool any_ts = false;

    for (int i = 0; i < n_all; i++) {
        cJSON *it = cJSON_GetArrayItem(series, i);
        const cJSON *tsj = cJSON_GetObjectItem(it, "timestamp");
        const char *tss = (cJSON_IsString(tsj) && tsj->valuestring) ? tsj->valuestring : "";
        if (tss[0]) {
            any_ts = true;
            if (!first_ts[0]) {
                first_ts = tss;
            }
            last_ts = tss;
        }
        int slot = tss[0] ? iso_today_slot(tss, today_ymd) : -1;
        if (slot < 0) {
            continue;
        }
        const cJSON *s = cJSON_GetObjectItem(it, "solar_power");
        const cJSON *b = cJSON_GetObjectItem(it, "battery_power");
        const cJSON *g = cJSON_GetObjectItem(it, "grid_power");
        const cJSON *l = cJSON_GetObjectItem(it, "load_power");
        float solar = s && cJSON_IsNumber(s) ? (float)s->valuedouble : 0;
        float batt = b && cJSON_IsNumber(b) ? (float)b->valuedouble : 0;
        float grid = g && cJSON_IsNumber(g) ? (float)g->valuedouble : 0;
        day->solar_w[slot] = solar;
        day->battery_w[slot] = batt;
        day->grid_w[slot] = grid;
        if (l && cJSON_IsNumber(l)) {
            day->home_w[slot] = (float)l->valuedouble;
        } else {
            day->home_w[slot] = solar + batt + grid;
        }
        filled++;
    }

    if (!any_ts && n_all > 0) {
        /* No timestamps: align the series to "now" on a 24h axis, not to midnight. */
        int now_slot = ENERGY_DAY_MAX_POINTS - 1;
        if (tnow > 1700000000 && localtime_r(&tnow, &tmnow)) {
            now_slot = (tmnow.tm_hour * 60 + tmnow.tm_min) / 15;
            if (now_slot >= ENERGY_DAY_MAX_POINTS) {
                now_slot = ENERGY_DAY_MAX_POINTS - 1;
            }
        }
        int use = n_all;
        if (use > ENERGY_DAY_MAX_POINTS) {
            use = ENERGY_DAY_MAX_POINTS;
        }
        int start_slot = now_slot - use + 1;
        int src0 = n_all - use;
        if (start_slot < 0) {
            src0 += -start_slot;
            use += start_slot;
            start_slot = 0;
        }
        for (int i = 0; i < use; i++) {
            cJSON *it = cJSON_GetArrayItem(series, src0 + i);
            int slot = start_slot + i;
            const cJSON *s = cJSON_GetObjectItem(it, "solar_power");
            const cJSON *b = cJSON_GetObjectItem(it, "battery_power");
            const cJSON *g = cJSON_GetObjectItem(it, "grid_power");
            const cJSON *l = cJSON_GetObjectItem(it, "load_power");
            float solar = s && cJSON_IsNumber(s) ? (float)s->valuedouble : 0;
            float batt = b && cJSON_IsNumber(b) ? (float)b->valuedouble : 0;
            float grid = g && cJSON_IsNumber(g) ? (float)g->valuedouble : 0;
            day->solar_w[slot] = solar;
            day->battery_w[slot] = batt;
            day->grid_w[slot] = grid;
            day->home_w[slot] = (l && cJSON_IsNumber(l)) ? (float)l->valuedouble : (solar + batt + grid);
            filled++;
        }
    }

    day->count = ENERGY_DAY_MAX_POINTS;
    day->valid = filled > 0;
    ESP_LOGI(TAG, "power day filled=%d/%d ts=%s .. %s 8h=%.0fW 12h=%.0fW 18h=%.0fW",
             filled, n_all, first_ts[0] ? first_ts : "-", last_ts[0] ? last_ts : "-",
             day->solar_w[32], day->solar_w[48], day->solar_w[72]);
    cJSON_Delete(root);
    return day->valid;
}

static double json_d(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    if (v && cJSON_IsNumber(v)) {
        return v->valuedouble;
    }
    return 0;
}

static float apply_energy_scale(double v, double scale)
{
    return (float)(v / scale);
}

static bool iso_ymd_local(const char *ts, char *ymd, size_t n)
{
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0;
    if (!ts || !ymd || n < 11) {
        return false;
    }
    int got = sscanf(ts, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se);
    if (got < 3) {
        got = sscanf(ts, "%d-%d-%d", &Y, &Mo, &D);
    }
    if (got < 3) {
        return false;
    }
    if (strchr(ts, 'Z') || strstr(ts, "+00:00")) {
        h += 2;
    }
    struct tm tsm = {0};
    tsm.tm_year = Y - 1900;
    tsm.tm_mon = Mo - 1;
    tsm.tm_mday = D;
    tsm.tm_hour = h;
    tsm.tm_min = mi;
    tsm.tm_sec = se;
    tsm.tm_isdst = -1;
    mktime(&tsm);
    strftime(ymd, n, "%Y-%m-%d", &tsm);
    return ymd[0] != '\0';
}

static int month_days(int year, int month0)
{
    static const int md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = md[month0];
    if (month0 == 1) {
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
            d = 29;
        }
    }
    return d;
}

typedef struct {
    char ymd[11];
    float solar;
    float to_home;
    float to_batt;
    float to_grid;
    float from_batt;
    float from_grid;
} energy_day_bucket_t;

static int bucket_find(const energy_day_bucket_t *b, int n, const char *ymd)
{
    for (int i = 0; i < n; i++) {
        if (strncmp(b[i].ymd, ymd, 10) == 0) {
            return i;
        }
    }
    return -1;
}

static int bucket_cmp(const void *a, const void *b)
{
    return strncmp(((const energy_day_bucket_t *)a)->ymd,
                   ((const energy_day_bucket_t *)b)->ymd, 10);
}

static void weekday_label(const char *ymd, char *out, size_t n)
{
    static const char *wd[] = {"dom", "lun", "mar", "mer", "gio", "ven", "sab"};
    int Y = 0, Mo = 0, D = 0;
    struct tm t = {0};
    if (sscanf(ymd, "%d-%d-%d", &Y, &Mo, &D) >= 3) {
        t.tm_year = Y - 1900;
        t.tm_mon = Mo - 1;
        t.tm_mday = D;
        t.tm_hour = 12;
        t.tm_isdst = -1;
        mktime(&t);
    }
    strlcpy(out, wd[t.tm_wday % 7], n);
}

static void set_day_label(char *buf, size_t n, int day)
{
    unsigned d = 1;
    if (day >= 1 && day <= 31) {
        d = (unsigned)day;
    }
    snprintf(buf, n, "%u", d);
}

static void copy_bucket_slot(energy_period_t *p, int slot, const energy_day_bucket_t *b)
{
    p->solar_kwh[slot] = b->solar;
    p->to_home_kwh[slot] = b->to_home;
    p->to_battery_kwh[slot] = b->to_batt;
    p->to_grid_kwh[slot] = b->to_grid;
    p->from_solar_kwh[slot] = b->to_home;
    p->from_battery_kwh[slot] = b->from_batt;
    p->from_grid_kwh[slot] = b->from_grid;
}

static void period_totals_from_slots(energy_period_t *p)
{
    p->solar_gen_kwh = 0;
    p->solar_to_home_kwh = 0;
    p->solar_to_battery_kwh = 0;
    p->solar_to_grid_kwh = 0;
    p->home_from_solar_kwh = 0;
    p->home_from_battery_kwh = 0;
    p->home_from_grid_kwh = 0;
    for (int i = 0; i < p->count; i++) {
        p->solar_gen_kwh += p->solar_kwh[i];
        p->solar_to_home_kwh += p->to_home_kwh[i];
        p->solar_to_battery_kwh += p->to_battery_kwh[i];
        p->solar_to_grid_kwh += p->to_grid_kwh[i];
        p->home_from_solar_kwh += p->from_solar_kwh[i];
        p->home_from_battery_kwh += p->from_battery_kwh[i];
        p->home_from_grid_kwh += p->from_grid_kwh[i];
    }
    p->home_kwh = p->home_from_solar_kwh + p->home_from_battery_kwh + p->home_from_grid_kwh;
}

static void fill_week_from_month(energy_period_t *week, const energy_period_t *month)
{
    memset(week, 0, sizeof(*week));
    if (!month || month->count < 1) {
        return;
    }
    time_t tnow = time(NULL);
    struct tm tmnow = {0};
    int last = month->count;
    if (tnow > 1700000000 && localtime_r(&tnow, &tmnow)) {
        if (tmnow.tm_mday < last) {
            last = tmnow.tm_mday;
        }
    }
    if (last < 1) {
        last = 1;
    }
    int start = last - 7;
    if (start < 0) {
        start = 0;
    }
    week->count = last - start;
    for (int i = 0; i < week->count; i++) {
        int src = start + i;
        week->solar_kwh[i] = month->solar_kwh[src];
        week->to_home_kwh[i] = month->to_home_kwh[src];
        week->to_battery_kwh[i] = month->to_battery_kwh[src];
        week->to_grid_kwh[i] = month->to_grid_kwh[src];
        week->from_solar_kwh[i] = month->from_solar_kwh[src];
        week->from_battery_kwh[i] = month->from_battery_kwh[src];
        week->from_grid_kwh[i] = month->from_grid_kwh[src];
        struct tm t = tmnow;
        t.tm_mday = src + 1;
        t.tm_hour = 12;
        t.tm_min = 0;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        mktime(&t);
        char ymd[16];
        strftime(ymd, sizeof(ymd), "%Y-%m-%d", &t);
        weekday_label(ymd, week->labels[i], sizeof(week->labels[i]));
    }
    period_totals_from_slots(week);
    week->valid = week->count > 0;
}

typedef enum {
    ENERGY_RANGE_TODAY = 0,
    ENERGY_RANGE_WEEK,
    ENERGY_RANGE_MONTH,
} energy_range_t;

static bool parse_period_energy(const char *body, energy_period_t *p, energy_range_t range)
{
    memset(p, 0, sizeof(*p));
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        size_t n = body ? strlen(body) : 0;
        ESP_LOGW(TAG, "energy JSON parse fail len=%u head=%.80s", (unsigned)n, body ? body : "");
        return false;
    }
    cJSON *resp = cJSON_GetObjectItem(root, "response");
    cJSON *series = resp ? cJSON_GetObjectItem(resp, "time_series") : NULL;
    if (!cJSON_IsArray(series)) {
        ESP_LOGW(TAG, "energy time_series assente head=%.120s", body ? body : "");
        cJSON_Delete(root);
        return false;
    }

    int n_all = cJSON_GetArraySize(series);
    char today_ymd[16] = {0};
    time_t tnow = time(NULL);
    struct tm tmnow = {0};
    if (tnow > 1700000000 && localtime_r(&tnow, &tmnow)) {
        strftime(today_ymd, sizeof(today_ymd), "%Y-%m-%d", &tmnow);
    }

    double max_abs = 0;
    for (int i = 0; i < n_all; i++) {
        cJSON *it = cJSON_GetArrayItem(series, i);
        double vals[] = {
            json_d(it, "solar_energy_exported"),
            json_d(it, "battery_energy_imported_from_solar"),
            json_d(it, "grid_energy_exported_from_solar"),
            json_d(it, "consumer_energy_imported_from_solar"),
            json_d(it, "consumer_energy_imported_from_battery"),
            json_d(it, "consumer_energy_imported_from_grid"),
        };
        for (size_t k = 0; k < sizeof(vals) / sizeof(vals[0]); k++) {
            double a = vals[k] < 0 ? -vals[k] : vals[k];
            if (a > max_abs) {
                max_abs = a;
            }
        }
    }
    /* Tesla Owner API energy is watt-hours; some proxies already send kWh. */
    double scale = (max_abs > 400.0) ? 1000.0 : 1.0;

    energy_day_bucket_t days[ENERGY_PERIOD_MAX_POINTS];
    memset(days, 0, sizeof(days));
    int nd = 0;
    const char *first_ts = "";
    int matched = 0;

    for (int i = 0; i < n_all; i++) {
        cJSON *it = cJSON_GetArrayItem(series, i);
        const cJSON *ts = cJSON_GetObjectItem(it, "timestamp");
        if (!cJSON_IsString(ts) || !ts->valuestring) {
            ts = cJSON_GetObjectItem(it, "date");
        }
        const char *tss = (cJSON_IsString(ts) && ts->valuestring) ? ts->valuestring : "";
        if (tss[0] && !first_ts[0]) {
            first_ts = tss;
        }
        char ymd[16] = {0};
        if (!iso_ymd_local(tss, ymd, sizeof(ymd))) {
            if (strlen(tss) >= 10) {
                memcpy(ymd, tss, 10);
                ymd[10] = '\0';
            }
        }
        if (ymd[0] == '\0') {
            continue;
        }
        int slot = bucket_find(days, nd, ymd);
        if (slot < 0) {
            if (nd >= ENERGY_PERIOD_MAX_POINTS) {
                continue;
            }
            slot = nd++;
            strlcpy(days[slot].ymd, ymd, sizeof(days[slot].ymd));
        }

        double solar = json_d(it, "solar_energy_exported");
        double to_home = json_d(it, "consumer_energy_imported_from_solar");
        double to_batt = json_d(it, "battery_energy_imported_from_solar");
        double to_grid = json_d(it, "grid_energy_exported_from_solar");
        double from_batt = json_d(it, "consumer_energy_imported_from_battery");
        double from_grid = json_d(it, "consumer_energy_imported_from_grid");
        if (to_grid <= 0 && solar > 0) {
            double rest = solar - to_home - to_batt;
            if (rest > 0) {
                to_grid = rest;
            }
        }
        days[slot].solar += apply_energy_scale(solar, scale);
        days[slot].to_home += apply_energy_scale(to_home, scale);
        days[slot].to_batt += apply_energy_scale(to_batt, scale);
        days[slot].to_grid += apply_energy_scale(to_grid, scale);
        days[slot].from_batt += apply_energy_scale(from_batt, scale);
        days[slot].from_grid += apply_energy_scale(from_grid, scale);
        matched++;
    }

    if (nd > 1) {
        qsort(days, (size_t)nd, sizeof(days[0]), bucket_cmp);
    }

    if (range == ENERGY_RANGE_TODAY) {
        p->count = 1;
        strlcpy(p->labels[0], "oggi", sizeof(p->labels[0]));
        int idx = bucket_find(days, nd, today_ymd);
        if (idx < 0 && nd > 0) {
            idx = nd - 1;
        }
        if (idx >= 0) {
            copy_bucket_slot(p, 0, &days[idx]);
        }
    } else if (range == ENERGY_RANGE_WEEK) {
        char keys[7][11];
        memset(keys, 0, sizeof(keys));
        int hits = 0;
        p->count = 7;
        for (int i = 0; i < 7; i++) {
            struct tm t = tmnow;
            t.tm_hour = 12;
            t.tm_min = 0;
            t.tm_sec = 0;
            t.tm_isdst = -1;
            time_t x = mktime(&t);
            if (x > 0) {
                x -= (time_t)(6 - i) * 86400;
                localtime_r(&x, &t);
            }
            strftime(keys[i], sizeof(keys[i]), "%Y-%m-%d", &t);
            weekday_label(keys[i], p->labels[i], sizeof(p->labels[i]));
            int idx = bucket_find(days, nd, keys[i]);
            if (idx >= 0) {
                copy_bucket_slot(p, i, &days[idx]);
                if (days[idx].solar > 0.01f) {
                    hits++;
                }
            }
        }
        /* Tesla "week" may be a calendar week that does not match last-7. Use API days. */
        if (hits == 0 && nd > 0) {
            memset(p->solar_kwh, 0, sizeof(p->solar_kwh));
            memset(p->to_home_kwh, 0, sizeof(p->to_home_kwh));
            memset(p->to_battery_kwh, 0, sizeof(p->to_battery_kwh));
            memset(p->to_grid_kwh, 0, sizeof(p->to_grid_kwh));
            memset(p->from_solar_kwh, 0, sizeof(p->from_solar_kwh));
            memset(p->from_battery_kwh, 0, sizeof(p->from_battery_kwh));
            memset(p->from_grid_kwh, 0, sizeof(p->from_grid_kwh));
            int start = nd > 7 ? nd - 7 : 0;
            p->count = nd - start;
            for (int i = 0; i < p->count; i++) {
                copy_bucket_slot(p, i, &days[start + i]);
                weekday_label(days[start + i].ymd, p->labels[i], sizeof(p->labels[i]));
            }
        }
    } else {
        int y = tmnow.tm_year + 1900;
        int mdays = month_days(y, tmnow.tm_mon);
        if (mdays > ENERGY_PERIOD_MAX_POINTS) {
            mdays = ENERGY_PERIOD_MAX_POINTS;
        }
        p->count = mdays;
        char prefix[16];
        int yy = y;
        int mm = tmnow.tm_mon + 1;
        if (yy < 2000 || yy > 2099) {
            yy = 2026;
        }
        if (mm < 1 || mm > 12) {
            mm = 1;
        }
        snprintf(prefix, sizeof(prefix), "%04d-%02d", yy, mm);
        int hits = 0;
        for (int i = 0; i < mdays; i++) {
            struct tm t = tmnow;
            t.tm_mday = i + 1;
            t.tm_hour = 12;
            t.tm_min = 0;
            t.tm_sec = 0;
            t.tm_isdst = -1;
            mktime(&t);
            char key[11];
            strftime(key, sizeof(key), "%Y-%m-%d", &t);
            set_day_label(p->labels[i], sizeof(p->labels[i]), i + 1);
            int idx = bucket_find(days, nd, key);
            if (idx >= 0) {
                copy_bucket_slot(p, i, &days[idx]);
                if (days[idx].solar > 0.01f) {
                    hits++;
                }
            }
        }
        if (hits == 0 && nd > 0) {
            memset(p->solar_kwh, 0, sizeof(p->solar_kwh));
            memset(p->to_home_kwh, 0, sizeof(p->to_home_kwh));
            memset(p->to_battery_kwh, 0, sizeof(p->to_battery_kwh));
            memset(p->to_grid_kwh, 0, sizeof(p->to_grid_kwh));
            memset(p->from_solar_kwh, 0, sizeof(p->from_solar_kwh));
            memset(p->from_battery_kwh, 0, sizeof(p->from_battery_kwh));
            memset(p->from_grid_kwh, 0, sizeof(p->from_grid_kwh));
            int nuse = nd > ENERGY_PERIOD_MAX_POINTS ? ENERGY_PERIOD_MAX_POINTS : nd;
            /* Prefer days in the current month if any ymd prefix matches. */
            int mcount = 0;
            for (int i = 0; i < nd; i++) {
                if (strncmp(days[i].ymd, prefix, 7) == 0) {
                    mcount++;
                }
            }
            if (mcount > 0) {
                p->count = 0;
                for (int i = 0; i < nd && p->count < ENERGY_PERIOD_MAX_POINTS; i++) {
                    if (strncmp(days[i].ymd, prefix, 7) != 0) {
                        continue;
                    }
                    copy_bucket_slot(p, p->count, &days[i]);
                    int day = p->count + 1;
                    if (strlen(days[i].ymd) >= 10) {
                        day = (days[i].ymd[8] - '0') * 10 + (days[i].ymd[9] - '0');
                    }
                    set_day_label(p->labels[p->count], sizeof(p->labels[p->count]), day);
                    p->count++;
                }
            } else {
                p->count = nuse;
                for (int i = 0; i < nuse; i++) {
                    copy_bucket_slot(p, i, &days[i]);
                    int day = i + 1;
                    if (strlen(days[i].ymd) >= 10) {
                        day = (days[i].ymd[8] - '0') * 10 + (days[i].ymd[9] - '0');
                    }
                    set_day_label(p->labels[i], sizeof(p->labels[i]), day);
                }
            }
        }
    }

    period_totals_from_slots(p);
    p->valid = p->count > 0;
    const char *rname = range == ENERGY_RANGE_TODAY ? "oggi" : (range == ENERGY_RANGE_WEEK ? "settimana" : "mese");
    ESP_LOGI(TAG, "energy %s n=%d days=%d samples=%d ts=%s scale=%.0f solar=%.1f home=%.1f to_pw=%.1f to_grid=%.1f",
             rname, p->count, nd, matched, first_ts[0] ? first_ts : "-", scale, p->solar_gen_kwh, p->home_kwh,
             p->solar_to_battery_kwh, p->solar_to_grid_kwh);
    cJSON_Delete(root);
    return p->valid;
}

static bool is_energy_product(const cJSON *prod)
{
    if (cJSON_GetObjectItem(prod, "energy_site_id")) {
        return true;
    }
    const cJSON *rt = cJSON_GetObjectItem(prod, "resource_type");
    if (cJSON_IsString(rt) && rt->valuestring) {
        return strcmp(rt->valuestring, "battery") == 0 || strcmp(rt->valuestring, "solar") == 0 ||
               strcmp(rt->valuestring, "wall_connector") == 0;
    }
    return false;
}

static int energy_rank(const cJSON *prod)
{
    const cJSON *rt = cJSON_GetObjectItem(prod, "resource_type");
    if (cJSON_IsString(rt) && rt->valuestring) {
        if (strcmp(rt->valuestring, "battery") == 0) {
            return 3;
        }
        if (strcmp(rt->valuestring, "solar") == 0) {
            return 2;
        }
        if (strcmp(rt->valuestring, "wall_connector") == 0) {
            return 1;
        }
    }
    return 2;
}

static bool match_product(const cJSON *prod, const app_config_t *cfg)
{
    char id[APP_SITE_ID_MAX] = {0};
    json_id_to_str(cJSON_GetObjectItem(prod, "energy_site_id"), id, sizeof(id));
    return id[0] && cfg->site_id[0] && strcmp(id, cfg->site_id) == 0;
}

static void apply_site(const cJSON *prod)
{
    char id[APP_SITE_ID_MAX];
    json_id_to_str(cJSON_GetObjectItem(prod, "energy_site_id"), id, sizeof(id));
    const cJSON *name = cJSON_GetObjectItem(prod, "site_name");
    const cJSON *rt = cJSON_GetObjectItem(prod, "resource_type");
    if (id[0]) {
        app_config_set_site_id(id);
    }
    if (cJSON_IsString(name) && name->valuestring) {
        app_config_set_site_name(name->valuestring);
    }
    app_config_save();
    ESP_LOGI(TAG, "resolved site %s (%s, %s)", id,
             (cJSON_IsString(name) && name->valuestring) ? name->valuestring : "?",
             (cJSON_IsString(rt) && rt->valuestring) ? rt->valuestring : "?");
}

static esp_err_t resolve_site(const char *host, const char *token)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/1/products", host);
    char *body = NULL;
    int status = 0;
    esp_err_t err = http_get(url, token, &body, &status);
    if (err != ESP_OK) {
        return err;
    }
    if (status != 200 || !body) {
        set_http_error(status, ESP_OK, "Impossibile elencare i dispositivi");
        free(body);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return ESP_FAIL;
    }
    cJSON *resp = cJSON_GetObjectItem(root, "response");
    if (!cJSON_IsArray(resp)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    app_config_t cfg;
    app_config_get(&cfg);
    int n = cJSON_GetArraySize(resp);
    const cJSON *matched = NULL;
    const cJSON *fallback = NULL;
    int fallback_rank = -1;
    int energy_n = 0;

    for (int i = 0; i < n; i++) {
        cJSON *prod = cJSON_GetArrayItem(resp, i);
        if (!is_energy_product(prod)) {
            continue;
        }
        energy_n++;
        char id[APP_SITE_ID_MAX] = {0};
        json_id_to_str(cJSON_GetObjectItem(prod, "energy_site_id"), id, sizeof(id));
        const cJSON *name = cJSON_GetObjectItem(prod, "site_name");
        const cJSON *rt = cJSON_GetObjectItem(prod, "resource_type");
        ESP_LOGI(TAG, "energy product[%d] id=%s name=%s type=%s", i, id,
                 (cJSON_IsString(name) && name->valuestring) ? name->valuestring : "?",
                 (cJSON_IsString(rt) && rt->valuestring) ? rt->valuestring : "?");

        if (match_product(prod, &cfg)) {
            int r = energy_rank(prod);
            if (!matched || r > energy_rank(matched)) {
                matched = prod;
            }
        }
        int r = energy_rank(prod);
        if (r > fallback_rank) {
            fallback_rank = r;
            fallback = prod;
        }
    }

    const cJSON *chosen = matched;
    if (!chosen && !cfg.site_id[0]) {
        chosen = fallback;
    }
    if (chosen) {
        apply_site(chosen);
    } else if (!cfg.site_id[0]) {
        ESP_LOGW(TAG, "nessun energy_site_id in %d prodotti", n);
        energy_model_set_error("Nessun Powerwall nell'account");
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    } else {
        ESP_LOGW(TAG, "site_id %s non trovato in /products, tengo l'id", cfg.site_id);
    }
    cJSON_Delete(root);
    (void)energy_n;
    return ESP_OK;
}

static void build_history_url(char *url, size_t n, const char *host, const char *site,
                              const char *kind, const char *period)
{
    char end[40];
    char end_enc[80];
    end_date_iso(end, sizeof(end));
    url_encode(end, end_enc, sizeof(end_enc));
    if (period) {
        snprintf(url, n,
                 "%s/api/1/energy_sites/%s/calendar_history?kind=%s&period=%s&end_date=%s&time_zone=Europe%%2FRome",
                 host, site, kind, period, end_enc);
    } else {
        snprintf(url, n,
                 "%s/api/1/energy_sites/%s/calendar_history?kind=%s&end_date=%s&time_zone=Europe%%2FRome",
                 host, site, kind, end_enc);
    }
}

static bool s_site_resolved;
static char s_resolved_id[APP_SITE_ID_MAX];

esp_err_t tesla_client_fetch_live(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (!cfg.api_token[0]) {
        energy_model_set_error("Token API mancante");
        return ESP_ERR_INVALID_STATE;
    }
    trim_slash(cfg.api_host);

    if (!s_site_resolved || strcmp(s_resolved_id, cfg.site_id) != 0) {
        if (resolve_site(cfg.api_host, cfg.api_token) == ESP_OK) {
            s_site_resolved = true;
            app_config_get(&cfg);
            trim_slash(cfg.api_host);
            strlcpy(s_resolved_id, cfg.site_id, sizeof(s_resolved_id));
        }
    }

    ESP_LOGI(TAG, "chiamo live_status site=%s (%s)", cfg.site_id, cfg.site_name);

    char url[320];
    snprintf(url, sizeof(url), "%s/api/1/energy_sites/%s/live_status", cfg.api_host, cfg.site_id);

    char *body = NULL;
    int status = 0;
    esp_err_t err = http_get(url, cfg.api_token, &body, &status);
    if (err != ESP_OK) {
        set_http_error(0, err, "Errore di rete");
        return err;
    }
    if (status != 200 || !body) {
        if (status == 404) {
            s_site_resolved = false;
            s_resolved_id[0] = '\0';
        }
        set_http_error(status, ESP_OK, "live_status fallito");
        ESP_LOGW(TAG, "live_status HTTP %d site=%s body=%.160s", status, cfg.site_id, body ? body : "");
        free(body);
        return ESP_FAIL;
    }

    energy_live_t live;
    bool ok = parse_live(body, &live);
    if (!ok) {
        ESP_LOGW(TAG, "live parse fail body=%.160s", body);
        free(body);
        energy_model_set_error("Risposta live non valida");
        return ESP_FAIL;
    }
    free(body);
    energy_model_set_live(&live);
    return ESP_OK;
}

esp_err_t tesla_client_fetch_history(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (!cfg.api_token[0]) {
        return ESP_ERR_INVALID_STATE;
    }
    trim_slash(cfg.api_host);

    char url[400];
    char *body = NULL;
    int status = 0;

    build_history_url(url, sizeof(url), cfg.api_host, cfg.site_id, "power", "day");
    if (http_get(url, cfg.api_token, &body, &status) == ESP_OK && status == 200 && body) {
        energy_day_t day;
        if (parse_day_power(body, &day)) {
            energy_model_set_day(&day);
        }
    }
    free(body);
    body = NULL;

    build_history_url(url, sizeof(url), cfg.api_host, cfg.site_id, "energy", "day");
    if (http_get(url, cfg.api_token, &body, &status) == ESP_OK && status == 200 && body) {
        energy_period_t today;
        if (parse_period_energy(body, &today, ENERGY_RANGE_TODAY)) {
            energy_model_set_today(&today);
        }
    }
    free(body);
    return ESP_OK;
}

esp_err_t tesla_client_test(char *msg, size_t msg_len)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (!cfg.api_token[0]) {
        snprintf(msg, msg_len, "Token mancante");
        return ESP_ERR_INVALID_STATE;
    }
    trim_slash(cfg.api_host);
    char url[256];
    snprintf(url, sizeof(url), "%s/api/1/products", cfg.api_host);
    char *body = NULL;
    int status = 0;
    esp_err_t err = http_get(url, cfg.api_token, &body, &status);
    if (err != ESP_OK) {
        format_net_error(msg, msg_len, err);
        return err;
    }
    if (status != 200) {
        snprintf(msg, msg_len, "HTTP %d", status);
        free(body);
        return ESP_FAIL;
    }
    resolve_site(cfg.api_host, cfg.api_token);
    s_site_resolved = true;
    app_config_get(&cfg);
    strlcpy(s_resolved_id, cfg.site_id, sizeof(s_resolved_id));
    cJSON *root = cJSON_Parse(body);
    free(body);
    int count = 0;
    int energy_n = 0;
    if (root) {
        cJSON *resp = cJSON_GetObjectItem(root, "response");
        if (cJSON_IsArray(resp)) {
            count = cJSON_GetArraySize(resp);
            for (int i = 0; i < count; i++) {
                if (is_energy_product(cJSON_GetArrayItem(resp, i))) {
                    energy_n++;
                }
            }
        }
        cJSON_Delete(root);
    }
    snprintf(msg, msg_len, "OK, %d disp. (%d energy) sito %s", count, energy_n, cfg.site_id);
    return ESP_OK;
}
