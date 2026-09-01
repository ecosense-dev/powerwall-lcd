#include "app_httpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_lte.h"
#include "app_net.h"
#include "app_ota.h"
#include "app_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ui.h"
#include "weather_client.h"

static const char *TAG = "httpd";
static httpd_handle_t s_srv;
static SemaphoreHandle_t s_mu;

static void httpd_lock(void)
{
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
}

static void httpd_unlock(void)
{
    xSemaphoreGive(s_mu);
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t o = 0;
    for (size_t i = 0; i < src_len && o + 1 < dst_len; i++) {
        char c = src[i];
        if (c == '+') {
            dst[o++] = ' ';
        } else if (c == '%' && i + 2 < src_len) {
            int hi = hex_nibble(src[i + 1]);
            int lo = hex_nibble(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[o++] = (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
}

static bool web_pin_cookie(httpd_req_t *req)
{
    char val[8] = {0};
    size_t sz = sizeof(val);
    if (httpd_req_get_cookie_val(req, "lcdpin", val, &sz) != ESP_OK) {
        return false;
    }
    return val[0] == '1';
}

static bool web_unlocked(httpd_req_t *req)
{
    return app_config_needs_wizard() || web_pin_cookie(req);
}

static esp_err_t send_pin_page(httpd_req_t *req, const char *err)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    const char *head =
        "<!DOCTYPE html><html lang=\"it\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>PIN</title><style>"
        "body{font-family:-apple-system,sans-serif;background:#000;color:#f5f5f5;"
        "margin:0;padding:48px 24px;max-width:360px}"
        "h1{font-size:22px;margin:0 0 8px}p{color:#9a9a9a;font-size:14px}"
        "input{width:100%;box-sizing:border-box;background:#1c1c1e;color:#f5f5f5;"
        "border:1px solid #2a2a2a;border-radius:8px;padding:12px;font-size:24px;"
        "letter-spacing:8px;text-align:center;margin:16px 0}"
        "button{background:#2ee56b;color:#000;border:0;border-radius:8px;padding:12px 20px;"
        "font-size:16px;font-weight:600;width:100%}"
        ".err{color:#ff5a5a}"
        "</style></head><body><h1>Impostazioni</h1><p>Inserisci il PIN</p>";
    httpd_resp_sendstr_chunk(req, head);
    if (err && err[0]) {
        httpd_resp_sendstr_chunk(req, "<p class=\"err\">");
        httpd_resp_sendstr_chunk(req, err);
        httpd_resp_sendstr_chunk(req, "</p>");
    }
    httpd_resp_sendstr_chunk(req,
                             "<form method=\"post\" action=\"/pin\">"
                             "<input name=\"pin\" type=\"password\" inputmode=\"numeric\" "
                             "pattern=\"[0-9]*\" maxlength=\"4\" autocomplete=\"off\" autofocus>"
                             "<button type=\"submit\">Sblocca</button></form></body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t n = amp ? (size_t)(amp - p) : strlen(p);
        if (n > key_len && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            url_decode(out, out_len, p + key_len + 1, n - key_len - 1);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    if (out_len) {
        out[0] = '\0';
    }
    return false;
}

static void html_escape(char *dst, size_t dst_len, const char *src)
{
    size_t o = 0;
    if (!src) {
        src = "";
    }
    for (size_t i = 0; src[i] && o + 1 < dst_len; i++) {
        const char *rep = NULL;
        switch (src[i]) {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        default:
            dst[o++] = src[i];
            continue;
        }
        size_t rl = strlen(rep);
        if (o + rl >= dst_len) {
            break;
        }
        memcpy(dst + o, rep, rl);
        o += rl;
    }
    dst[o] = '\0';
}

static void after_web_save_task(void *arg)
{
    (void)arg;
    if (app_config_has_gps()) {
        weather_client_kick();
    }
    ui_lock();
    ui_settings_reload();
    ui_refresh();
    ui_settings_show_message("Salvato dal browser", false);
    if (!app_config_needs_wizard() && app_net_is_online()) {
        ui_show_wizard(false);
    }
    ui_unlock();
    vTaskDelete(NULL);
}

static void connect_from_web_task(void *arg)
{
    (void)arg;
    app_config_t cfg;
    app_config_get(&cfg);
    esp_err_t err = ESP_FAIL;
    if (cfg.wifi_ssid[0]) {
        err = app_wifi_connect(cfg.wifi_ssid, cfg.wifi_pass);
    }
    if (!app_wifi_is_connected() && app_config_has_lte()) {
        app_lte_request();
    }
    ESP_LOGI(TAG, "web connect: %s net=%s", esp_err_to_name(err), app_net_kind_name());
    ui_lock();
    ui_settings_reload();
    ui_refresh();
    if (app_net_is_online()) {
        ui_show_wizard(false);
        ui_settings_show_message("Rete connessa", false);
    } else {
        ui_settings_show_message(app_wifi_last_error()[0] ? app_wifi_last_error() : app_lte_last_error(), true);
    }
    ui_unlock();
    vTaskDelete(NULL);
}

static esp_err_t recv_body(httpd_req_t *req, char **out)
{
    *out = NULL;
    if (req->content_len <= 0 || req->content_len > 4096) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        got += n;
    }
    *out = body;
    return ESP_OK;
}

static void append(char **buf, size_t *len, size_t *cap, const char *s)
{
    size_t n = strlen(s);
    if (*len + n + 1 > *cap) {
        size_t nc = (*cap + n + 1024) * 2;
        char *nb = realloc(*buf, nc);
        if (!nb) {
            return;
        }
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, s, n + 1);
    *len += n;
}

static char *build_page(const char *flash)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const char *ip = app_net_portal_ip();
    if (!ip[0]) {
        ip = APP_WIFI_AP_IP;
    }

    char host_esc[APP_API_HOST_MAX * 2];
    char site_esc[APP_SITE_ID_MAX * 2];
    char ssid_esc[APP_WIFI_SSID_MAX * 2];
    char lat_esc[APP_GPS_COORD_MAX * 2];
    char lon_esc[APP_GPS_COORD_MAX * 2];
    char apn_esc[APP_LTE_APN_MAX * 2];
    html_escape(host_esc, sizeof(host_esc), cfg.api_host);
    html_escape(site_esc, sizeof(site_esc), cfg.site_id);
    html_escape(ssid_esc, sizeof(ssid_esc), cfg.wifi_ssid);
    html_escape(lat_esc, sizeof(lat_esc), cfg.gps_lat);
    html_escape(lon_esc, sizeof(lon_esc), cfg.gps_lon);
    html_escape(apn_esc, sizeof(apn_esc), cfg.lte_apn);

    size_t cap = 4096;
    size_t len = 0;
    char *html = malloc(cap);
    if (!html) {
        return NULL;
    }
    html[0] = '\0';

    append(&html, &len, &cap,
           "<!DOCTYPE html><html lang=\"it\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>Powerwall LCD</title><style>"
           "body{font-family:-apple-system,sans-serif;background:#000;color:#f5f5f5;"
           "margin:0;padding:24px;max-width:560px}"
           "h1{font-size:22px;margin:0 0 8px}h2{font-size:16px;margin:24px 0 8px}"
           "p,label{color:#9a9a9a;font-size:14px}"
           "input,select,textarea{width:100%;box-sizing:border-box;background:#1c1c1e;color:#f5f5f5;"
           "border:1px solid #2a2a2a;border-radius:8px;padding:12px;font-size:16px;margin:6px 0 16px}"
           "textarea{min-height:100px}"
           "button{background:#2ee56b;color:#000;border:0;border-radius:8px;padding:12px 20px;"
           "font-size:16px;font-weight:600}"
           ".ok{color:#2ee56b}.warn{color:#f5c542}.err{color:#ff5a5a}"
           "</style></head><body><h1>Powerwall LCD</h1>");

    char line[384];
    snprintf(line, sizeof(line), "<p>Pagina: http://%s</p>", ip);
    append(&html, &len, &cap, line);

    if (app_wifi_is_connected()) {
        snprintf(line, sizeof(line), "<p class=\"ok\">WiFi %s &middot; IP %s</p>",
                 cfg.wifi_ssid[0] ? cfg.wifi_ssid : "WiFi", app_wifi_ip());
        append(&html, &len, &cap, line);
    } else if (app_lte_is_connected()) {
        snprintf(line, sizeof(line), "<p class=\"ok\">4G LTE &middot; IP %s</p>", app_lte_ip());
        append(&html, &len, &cap, line);
    } else if (app_wifi_ap_is_up()) {
        snprintf(line, sizeof(line),
                 "<p class=\"warn\">Access point <b>%s</b> &middot; password <b>%s</b></p>",
                 APP_WIFI_AP_SSID, APP_WIFI_AP_PASS);
        append(&html, &len, &cap, line);
    } else {
        snprintf(line, sizeof(line), "<p class=\"err\">Rete offline</p>");
        append(&html, &len, &cap, line);
    }

    if (flash) {
        snprintf(line, sizeof(line), "<p class=\"ok\">%s</p>", flash);
        append(&html, &len, &cap, line);
    } else if (app_wifi_last_error()[0] && !app_wifi_is_connected()) {
        char err_esc[128];
        html_escape(err_esc, sizeof(err_esc), app_wifi_last_error());
        snprintf(line, sizeof(line), "<p class=\"err\">%s</p>", err_esc);
        append(&html, &len, &cap, line);
    }

    append(&html, &len, &cap, "<h2>Rete WiFi di casa (2.4 GHz)</h2>"
                              "<form method=\"post\" action=\"/wifi\">"
                              "<label>SSID</label>");
    snprintf(line, sizeof(line),
             "<input name=\"ssid\" value=\"%s\" placeholder=\"nome rete 2.4 GHz\">", ssid_esc);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap,
           "<label>Password WiFi</label>"
           "<input type=\"password\" name=\"pass\" placeholder=\"password della rete\">"
           "<button type=\"submit\">Connetti WiFi</button></form>");

    append(&html, &len, &cap, "<h2>4G LTE (A7670E)</h2>"
                              "<p>Usato se la WiFi non c'è. In Italia APN tipici: internet, ibox.tim.it, web.omnitel.it, iliad.</p>"
                              "<form method=\"post\" action=\"/save\">");
    snprintf(line, sizeof(line),
             "<label>APN</label><input name=\"apn\" value=\"%s\" placeholder=\"internet\">", apn_esc);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap,
           "<label>PIN SIM (se richiesto)</label>"
           "<input type=\"password\" name=\"simpin\" placeholder=\"vuoto = nessun PIN\">"
           "<button type=\"submit\">Salva APN</button></form>");

    append(&html, &len, &cap, "<h2>Token MyTeslaMate</h2><form method=\"post\" action=\"/save\">");
    snprintf(line, sizeof(line), "<p class=\"%s\">%s</p>", cfg.api_token[0] ? "ok" : "warn",
             cfg.api_token[0] ? "Token già impostato." : "Token mancante.");
    append(&html, &len, &cap, line);
    append(&html, &len, &cap,
           "<label>Token</label>"
           "<textarea name=\"token\" placeholder=\"Incolla il token (vuoto = non cambiare)\"></textarea>"
           "<label>Host API</label>");
    snprintf(line, sizeof(line), "<input name=\"host\" value=\"%s\">", host_esc);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap, "<label>Energy site id</label>");
    snprintf(line, sizeof(line), "<input name=\"site\" value=\"%s\">", site_esc);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap, "<label>Latitudine</label>");
    snprintf(line, sizeof(line),
             "<input name=\"lat\" value=\"%s\" placeholder=\"45.1234\" inputmode=\"decimal\">", lat_esc);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap, "<label>Longitudine</label>");
    snprintf(line, sizeof(line),
             "<input name=\"lon\" value=\"%s\" placeholder=\"9.1234\" inputmode=\"decimal\">", lon_esc);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap, "<label>Frequenza API TeslaMate (secondi, 5–300)</label>");
    snprintf(line, sizeof(line),
             "<input name=\"poll\" value=\"%u\" inputmode=\"numeric\" min=\"5\" max=\"300\">",
             (unsigned)cfg.poll_s);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap, "<label>Aggiornamento meteo (minuti, 1–120)</label>");
    snprintf(line, sizeof(line),
             "<input name=\"wxmin\" value=\"%u\" inputmode=\"numeric\" min=\"1\" max=\"120\">",
             (unsigned)cfg.wx_poll_min);
    append(&html, &len, &cap, line);
    append(&html, &len, &cap,
           "<button type=\"submit\">Salva token</button></form>");

    append(&html, &len, &cap, "<h2>Aggiornamento firmware (OTA)</h2>");
    snprintf(line, sizeof(line), "<p>Versione attuale: <b>%s</b></p>", app_ota_version());
    append(&html, &len, &cap, line);
    append(&html, &len, &cap,
           "<form method=\"post\" action=\"/ota\" enctype=\"multipart/form-data\">"
           "<label>File .bin (idf.py build)</label>"
           "<input type=\"file\" name=\"firmware\" accept=\".bin,application/octet-stream\">"
           "<button type=\"submit\">Carica e riavvia</button></form>"
           "<p>Il pannello vede solo reti 2.4 GHz. Meteo e 4G restano spenti finché non imposti GPS e APN. "
           "L'IP resta in Impostazioni sul display.</p></body></html>");
    return html;
}

static esp_err_t send_page(httpd_req_t *req, const char *flash)
{
    char *html = build_page(flash);
    if (!html) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "mem");
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return err;
}

static esp_err_t on_get_root(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET %s", req->uri ? req->uri : "/");
    httpd_resp_set_hdr(req, "Connection", "close");
    if (!web_unlocked(req)) {
        return send_pin_page(req, NULL);
    }
    return send_page(req, NULL);
}

static esp_err_t on_post_pin(httpd_req_t *req)
{
    char *body = NULL;
    if (recv_body(req, &body) != ESP_OK) {
        return send_pin_page(req, "Richiesta non valida.");
    }
    char pin[12];
    form_get(body, "pin", pin, sizeof(pin));
    free(body);

    if (strcmp(pin, APP_SETTINGS_PIN) != 0) {
        return send_pin_page(req, "PIN errato.");
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Set-Cookie", "lcdpin=1; Path=/; HttpOnly; Max-Age=14400; SameSite=Lax");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t on_captive(httpd_req_t *req)
{
    const char *ip = app_net_portal_ip();
    if (!ip[0]) {
        ip = APP_WIFI_AP_IP;
    }
    char loc[48];
    snprintf(loc, sizeof(loc), "http://%s/", ip);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", loc);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t on_post_save(httpd_req_t *req)
{
    if (!web_unlocked(req)) {
        return send_pin_page(req, NULL);
    }
    char *body = NULL;
    if (recv_body(req, &body) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return ESP_FAIL;
    }
    char token[APP_API_TOKEN_MAX];
    char host[APP_API_HOST_MAX];
    char site[APP_SITE_ID_MAX];
    char lat[APP_GPS_COORD_MAX];
    char lon[APP_GPS_COORD_MAX];
    char apn[APP_LTE_APN_MAX];
    char simpin[APP_LTE_PIN_MAX];
    char poll[8];
    char wxmin[8];
    form_get(body, "token", token, sizeof(token));
    form_get(body, "host", host, sizeof(host));
    form_get(body, "site", site, sizeof(site));
    form_get(body, "lat", lat, sizeof(lat));
    form_get(body, "lon", lon, sizeof(lon));
    bool has_apn = form_get(body, "apn", apn, sizeof(apn));
    form_get(body, "simpin", simpin, sizeof(simpin));
    bool has_poll = form_get(body, "poll", poll, sizeof(poll));
    bool has_wx = form_get(body, "wxmin", wxmin, sizeof(wxmin));
    free(body);

    if (token[0]) {
        app_config_set_token(token);
    }
    if (host[0]) {
        app_config_set_host(host);
    }
    if (site[0]) {
        app_config_set_site_id(site);
    }
    if (lat[0] || lon[0]) {
        app_config_set_gps(lat, lon);
    }
    if (has_apn) {
        app_config_t cfg;
        app_config_get(&cfg);
        app_config_set_lte(apn, simpin[0] ? simpin : cfg.lte_pin, cfg.lte_user, cfg.lte_pass);
    }
    if (has_poll || has_wx) {
        app_config_t cfg;
        app_config_get(&cfg);
        uint16_t ps = (has_poll && poll[0]) ? (uint16_t)atoi(poll) : cfg.poll_s;
        uint16_t wm = (has_wx && wxmin[0]) ? (uint16_t)atoi(wxmin) : cfg.wx_poll_min;
        app_config_set_polls(ps, wm);
    }
    if (app_config_has_gps()) {
        weather_client_kick();
    }
    esp_err_t err = app_config_save();
    xTaskCreate(after_web_save_task, "web_save", 8192, NULL, 4, NULL);
    return send_page(req, err == ESP_OK ? "Salvato." : "Errore NVS.");
}

static esp_err_t on_post_wifi(httpd_req_t *req)
{
    if (!web_unlocked(req)) {
        return send_pin_page(req, NULL);
    }
    char *body = NULL;
    if (recv_body(req, &body) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");
        return ESP_FAIL;
    }
    char ssid[APP_WIFI_SSID_MAX];
    char pass[APP_WIFI_PASS_MAX];
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "pass", pass, sizeof(pass));
    free(body);

    if (!ssid[0]) {
        return send_page(req, "Seleziona una rete.");
    }
    app_config_set_wifi(ssid, pass);
    app_config_save();
    xTaskCreate(connect_from_web_task, "web_wifi", 16384, NULL, 5, NULL);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Refresh", "8;url=/");
    const char *msg =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"8;url=/\">"
        "<title>Connessione</title></head><body style=\"background:#000;color:#f5f5f5;font-family:sans-serif;padding:24px\">"
        "<h1>Connessione in corso…</h1><p>Attendi, la pagina si aggiorna da sola.</p>"
        "<p>Se il telefono perde la rete, torna sul WiFi di casa e apri l'IP in Impostazioni sul display.</p>"
        "</body></html>";
    return httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t on_get_health(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /health");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, "ok\n");
}

static esp_err_t on_post_ota(httpd_req_t *req)
{
    if (!web_unlocked(req)) {
        return send_pin_page(req, NULL);
    }
    return app_ota_httpd_post(req);
}

static void register_uris(httpd_handle_t srv)
{
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = on_get_root};
    const httpd_uri_t pin = {.uri = "/pin", .method = HTTP_POST, .handler = on_post_pin};
    const httpd_uri_t health = {.uri = "/health", .method = HTTP_GET, .handler = on_get_health};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = on_post_save};
    const httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_POST, .handler = on_post_wifi};
    const httpd_uri_t ota = {.uri = "/ota", .method = HTTP_POST, .handler = on_post_ota};
    const httpd_uri_t captive = {.uri = "/*", .method = HTTP_GET, .handler = on_captive};
    httpd_register_uri_handler(srv, &root);
    httpd_register_uri_handler(srv, &pin);
    httpd_register_uri_handler(srv, &health);
    httpd_register_uri_handler(srv, &save);
    httpd_register_uri_handler(srv, &wifi);
    httpd_register_uri_handler(srv, &ota);
    httpd_register_uri_handler(srv, &captive);
}

esp_err_t app_httpd_start(void)
{
    httpd_lock();
    if (s_srv) {
        httpd_unlock();
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    /* Default is 7; needs LWIP_MAX_SOCKETS >= max_open_sockets + 3. */
    config.max_open_sockets = 7;
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 60;
    config.send_wait_timeout = 30;

    esp_err_t err = httpd_start(&s_srv, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        s_srv = NULL;
        httpd_unlock();
        return err;
    }
    register_uris(s_srv);
    ESP_LOGI(TAG, "HTTP :80 su http://%s/", app_net_portal_ip()[0] ? app_net_portal_ip() : "0.0.0.0");
    httpd_unlock();
    return ESP_OK;
}

esp_err_t app_httpd_restart(void)
{
    app_httpd_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    return app_httpd_start();
}

void app_httpd_stop(void)
{
    httpd_lock();
    if (s_srv) {
        httpd_handle_t srv = s_srv;
        /* Keep the handle until stop returns so a concurrent start cannot bind twice. */
        esp_err_t err = httpd_stop(srv);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "httpd_stop: %s (server may still be running)", esp_err_to_name(err));
            httpd_unlock();
            return;
        }
        s_srv = NULL;
        ESP_LOGI(TAG, "HTTP stopped");
    }
    httpd_unlock();
}
