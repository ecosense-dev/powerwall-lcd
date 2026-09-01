#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_lte.h"
#include "app_net.h"
#include "app_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tesla_client.h"
#include "ui_theme.h"
#include "weather_client.h"

typedef struct {
    lv_obj_t *ssid;
    lv_obj_t *pass;
    lv_obj_t *host;
    lv_obj_t *token;
    lv_obj_t *site;
    lv_obj_t *lat;
    lv_obj_t *lon;
    lv_obj_t *apn;
    lv_obj_t *simpin;
    lv_obj_t *poll;
    lv_obj_t *wxpoll;
    lv_obj_t *msg;
    lv_obj_t *ip;
    lv_obj_t *list;
    lv_obj_t *reads;
    bool wizard;
} settings_form_t;

static settings_form_t s_tab;
static settings_form_t s_wiz;

static lv_obj_t *make_ta(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, bool pwd)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, pwd);
    lv_obj_set_style_bg_color(ta, COL_ELEV, 0);
    lv_obj_set_style_text_color(ta, COL_TEXT, 0);
    lv_obj_set_style_border_color(ta, COL_LINE, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    ui_attach_textarea(ta);
    return ta;
}

static lv_obj_t *make_ta_gps(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *ph)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 12);
    lv_textarea_set_accepted_chars(ta, "0123456789.,-");
    lv_textarea_set_placeholder_text(ta, ph);
    lv_obj_set_style_bg_color(ta, COL_ELEV, 0);
    lv_obj_set_style_text_color(ta, COL_TEXT, 0);
    lv_obj_set_style_border_color(ta, COL_LINE, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    ui_attach_textarea_num(ta);
    return ta;
}

static lv_obj_t *make_ta_int(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *ph, uint8_t max_len)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_textarea_set_placeholder_text(ta, ph);
    lv_obj_set_style_bg_color(ta, COL_ELEV, 0);
    lv_obj_set_style_text_color(ta, COL_TEXT, 0);
    lv_obj_set_style_border_color(ta, COL_LINE, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    ui_attach_textarea_num(ta);
    return ta;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, COL_ELEV, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = ui_label(btn, &lv_font_montserrat_14, COL_TEXT);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return btn;
}

static void load_form(settings_form_t *f)
{
    app_config_t cfg;
    app_config_get(&cfg);
    lv_textarea_set_text(f->ssid, cfg.wifi_ssid);
    lv_textarea_set_text(f->pass, cfg.wifi_pass);
    lv_textarea_set_text(f->host, cfg.api_host);
    lv_textarea_set_text(f->token, cfg.api_token);
    lv_textarea_set_text(f->site, cfg.site_id);
    lv_textarea_set_text(f->lat, cfg.gps_lat);
    lv_textarea_set_text(f->lon, cfg.gps_lon);
    if (f->apn) {
        lv_textarea_set_text(f->apn, cfg.lte_apn);
    }
    if (f->simpin) {
        lv_textarea_set_text(f->simpin, cfg.lte_pin);
    }
    if (f->poll) {
        char b[8];
        snprintf(b, sizeof(b), "%u", (unsigned)cfg.poll_s);
        lv_textarea_set_text(f->poll, b);
    }
    if (f->wxpoll) {
        char b[8];
        snprintf(b, sizeof(b), "%u", (unsigned)cfg.wx_poll_min);
        lv_textarea_set_text(f->wxpoll, b);
    }
}

static void save_form(settings_form_t *f)
{
    app_config_set_wifi(lv_textarea_get_text(f->ssid), lv_textarea_get_text(f->pass));
    app_config_set_host(lv_textarea_get_text(f->host));
    app_config_set_token(lv_textarea_get_text(f->token));
    app_config_set_site_id(lv_textarea_get_text(f->site));
    app_config_set_gps(lv_textarea_get_text(f->lat), lv_textarea_get_text(f->lon));
    app_config_t cfg;
    app_config_get(&cfg);
    app_config_set_lte(f->apn ? lv_textarea_get_text(f->apn) : cfg.lte_apn,
                       f->simpin ? lv_textarea_get_text(f->simpin) : cfg.lte_pin, cfg.lte_user, cfg.lte_pass);
    uint16_t poll = cfg.poll_s;
    uint16_t wxm = cfg.wx_poll_min;
    if (f->poll) {
        poll = (uint16_t)atoi(lv_textarea_get_text(f->poll));
    }
    if (f->wxpoll) {
        wxm = (uint16_t)atoi(lv_textarea_get_text(f->wxpoll));
    }
    app_config_set_polls(poll, wxm);
    app_config_save();
}

static void set_msg(settings_form_t *f, const char *msg, bool err)
{
    if (!f->msg) {
        return;
    }
    lv_label_set_text(f->msg, msg);
    lv_obj_set_style_text_color(f->msg, err ? COL_DANGER : COL_PW, 0);
}

void ui_settings_show_message(const char *msg, bool error)
{
    set_msg(&s_tab, msg, error);
    set_msg(&s_wiz, msg, error);
}

static void on_ap_click(lv_event_t *e);

static void connect_task(void *arg)
{
    settings_form_t *f = (settings_form_t *)arg;
    app_config_t cfg;
    app_config_get(&cfg);
    esp_err_t err = ESP_FAIL;
    if (cfg.wifi_ssid[0]) {
        err = app_wifi_connect(cfg.wifi_ssid, cfg.wifi_pass);
    }
    if (!app_wifi_is_connected() && app_config_has_lte()) {
        app_lte_request();
    }
    if (app_net_is_online() && app_config_has_gps()) {
        weather_client_kick();
    }
    ui_lock();
    ui_refresh();
    if (app_net_is_online()) {
        char line[96];
        snprintf(line, sizeof(line), "Connesso (%s)  http://%s", app_net_kind_name(), app_net_ip());
        set_msg(f, line, false);
        if (!app_config_needs_wizard()) {
            ui_show_wizard(false);
        }
    } else {
        set_msg(f, err == ESP_OK ? "Rete: connessione fallita" : "WiFi/4G: connessione fallita", true);
    }
    ui_unlock();
    vTaskDelete(NULL);
}

static void scan_task(void *arg)
{
    settings_form_t *f = (settings_form_t *)arg;
    app_wifi_ap_t aps[16];
    uint16_t n = 0;
    esp_err_t err = app_wifi_scan(aps, 16, &n);
    ui_lock();
    lv_obj_clean(f->list);
    if (err != ESP_OK) {
        set_msg(f, "Scan WiFi fallito (avvia WiFi)", true);
    } else if (n == 0) {
        set_msg(f, "Nessuna rete trovata", true);
    } else {
        char line[48];
        snprintf(line, sizeof(line), "%u reti trovate", n);
        set_msg(f, line, false);
        for (uint16_t i = 0; i < n; i++) {
            char lab[48];
            snprintf(lab, sizeof(lab), "%s  %ddBm", aps[i].ssid, aps[i].rssi);
            lv_obj_t *btn = lv_list_add_btn(f->list, NULL, lab);
            lv_obj_set_style_bg_color(btn, COL_ELEV, 0);
            lv_obj_set_style_text_color(btn, COL_TEXT, 0);
            lv_obj_add_event_cb(btn, on_ap_click, LV_EVENT_CLICKED, f);
        }
    }
    ui_unlock();
    vTaskDelete(NULL);
}

static void on_ap_click(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    settings_form_t *f = (settings_form_t *)lv_event_get_user_data(e);
    const char *txt = lv_list_get_btn_text(f->list, btn);
    if (!txt || !f) {
        return;
    }
    char ssid[33] = {0};
    const char *sp = strstr(txt, "  ");
    size_t n = sp ? (size_t)(sp - txt) : strlen(txt);
    if (n >= sizeof(ssid)) {
        n = sizeof(ssid) - 1;
    }
    memcpy(ssid, txt, n);
    ssid[n] = '\0';
    lv_textarea_set_text(f->ssid, ssid);
}

static void on_scan(lv_event_t *e)
{
    settings_form_t *f = lv_event_get_user_data(e);
    set_msg(f, "Ricerca reti...", false);
    xTaskCreate(scan_task, "wifi_scan", 4096, f, 5, NULL);
}

static void on_save(lv_event_t *e)
{
    settings_form_t *f = lv_event_get_user_data(e);
    save_form(f);
    set_msg(f, "Salvato in NVS", false);
    xTaskCreate(connect_task, "wifi_conn", 16384, f, 5, NULL);
}

static void test_api_task(void *arg)
{
    settings_form_t *f = (settings_form_t *)arg;
    char msg[128];
    tesla_client_test(msg, sizeof(msg));
    ui_lock();
    set_msg(f, msg, strstr(msg, "OK") == NULL);
    ui_unlock();
    vTaskDelete(NULL);
}

static void on_test_api(lv_event_t *e)
{
    settings_form_t *f = lv_event_get_user_data(e);
    save_form(f);
    set_msg(f, "Test API...", false);
    xTaskCreate(test_api_task, "api_test", 8192, f, 5, NULL);
}

static void populate_list_clicks(settings_form_t *f)
{
    /* scan_task adds buttons without event; attach after scan in a simpler way:
       handle clicks on list children in a later patch. For now add event on list. */
    lv_obj_add_event_cb(f->list, on_ap_click, LV_EVENT_CLICKED, f);
}

static void build_form(lv_obj_t *parent, settings_form_t *f, bool wizard)
{
    memset(f, 0, sizeof(*f));
    f->wizard = wizard;

    lv_obj_t *title = ui_label(parent, &lv_font_montserrat_24, COL_TEXT);
    lv_label_set_text(title, wizard ? "Configurazione iniziale" : "Impostazioni");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sub = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(sub, "WiFi 2.4 GHz preferito; 4G (A7670E) se manca la WiFi.");
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, 32);

    f->ip = ui_label(parent, &lv_font_montserrat_16, COL_STATUS);
    lv_label_set_text(f->ip, "WiFi: offline");
    lv_obj_align(f->ip, LV_ALIGN_TOP_RIGHT, 0, 4);

    lv_obj_t *l_ssid = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_ssid, "SSID WiFi");
    lv_obj_set_pos(l_ssid, 0, 64);
    f->ssid = make_ta(parent, 360, 40, false);
    lv_obj_set_pos(f->ssid, 0, 84);

    lv_obj_t *l_pass = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_pass, "Password WiFi");
    lv_obj_set_pos(l_pass, 380, 64);
    f->pass = make_ta(parent, 360, 40, true);
    lv_obj_set_pos(f->pass, 380, 84);

    lv_obj_t *scan = make_btn(parent, "Cerca reti", on_scan, f);
    lv_obj_set_size(scan, 140, 36);
    lv_obj_set_pos(scan, 0, 132);

    f->list = lv_list_create(parent);
    lv_obj_set_size(f->list, 740, 56);
    lv_obj_set_pos(f->list, 0, 172);
    lv_obj_set_style_bg_color(f->list, COL_CARD, 0);
    lv_obj_set_style_border_width(f->list, 0, 0);
    lv_obj_set_style_text_color(f->list, COL_TEXT, 0);
    populate_list_clicks(f);

    lv_obj_t *l_host = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_host, "Host API");
    lv_obj_set_pos(l_host, 0, 236);
    f->host = make_ta(parent, 740, 40, false);
    lv_obj_set_pos(f->host, 0, 256);

    lv_obj_t *l_tok = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_tok, "Token MyTeslaMate");
    lv_obj_set_pos(l_tok, 0, 300);
    f->token = make_ta(parent, 740, 40, true);
    lv_obj_set_pos(f->token, 0, 320);

    lv_obj_t *l_apn = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_apn, "APN 4G (es. internet)");
    lv_obj_set_pos(l_apn, 0, 364);
    f->apn = make_ta(parent, 480, 40, false);
    lv_obj_set_pos(f->apn, 0, 384);

    lv_obj_t *l_pin = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_pin, "PIN SIM");
    lv_obj_set_pos(l_pin, 500, 364);
    f->simpin = make_ta(parent, 240, 40, true);
    lv_obj_set_pos(f->simpin, 500, 384);

    lv_obj_t *l_site = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_site, "Energy site id");
    lv_obj_set_pos(l_site, 0, 428);
    f->site = make_ta(parent, 240, 40, false);
    lv_obj_set_pos(f->site, 0, 448);

    lv_obj_t *l_lat = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_lat, "Latitudine");
    lv_obj_set_pos(l_lat, 256, 428);
    f->lat = make_ta_gps(parent, 230, 40, "45.1234");
    lv_obj_set_pos(f->lat, 256, 448);

    lv_obj_t *l_lon = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_lon, "Longitudine");
    lv_obj_set_pos(l_lon, 502, 428);
    f->lon = make_ta_gps(parent, 238, 40, "9.1234");
    lv_obj_set_pos(f->lon, 502, 448);

    lv_obj_t *l_poll = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_poll, "Poll Tesla (s)");
    lv_obj_set_pos(l_poll, 0, 492);
    f->poll = make_ta_int(parent, 240, 40, "20", 3);
    lv_obj_set_pos(f->poll, 0, 512);

    lv_obj_t *l_wx = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_wx, "Meteo (min)");
    lv_obj_set_pos(l_wx, 256, 492);
    f->wxpoll = make_ta_int(parent, 230, 40, "15", 3);
    lv_obj_set_pos(f->wxpoll, 256, 512);

    lv_obj_t *save = make_btn(parent, "Salva e connetti", on_save, f);
    lv_obj_set_size(save, 180, 40);
    lv_obj_set_pos(save, 0, 564);
    lv_obj_set_style_bg_color(save, COL_PW, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), COL_BG, 0);

    lv_obj_t *test = make_btn(parent, "Test API", on_test_api, f);
    lv_obj_set_size(test, 140, 40);
    lv_obj_set_pos(test, 192, 564);

    f->msg = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(f->msg, "");
    lv_obj_set_pos(f->msg, 0, 612);

    lv_obj_t *l_log = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(l_log, "Ultime 15 letture");
    lv_obj_set_pos(l_log, 0, 636);
    f->reads = ui_label(parent, &lv_font_montserrat_12, COL_TEXT);
    lv_label_set_text(f->reads, "Nessuna lettura ancora.");
    lv_label_set_long_mode(f->reads, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(f->reads, 740);
    lv_obj_set_pos(f->reads, 0, 658);

    load_form(f);

    lv_obj_set_style_min_height(parent, 1100, 0);
}

void ui_settings_create(lv_obj_t *parent, bool wizard)
{
    build_form(parent, wizard ? &s_wiz : &s_tab, wizard);
}

void ui_settings_reload(void)
{
    if (s_tab.ssid) {
        load_form(&s_tab);
    }
    if (s_wiz.ssid) {
        load_form(&s_wiz);
    }
    ui_settings_on_wifi();
}

static void set_ip_label(settings_form_t *f)
{
    if (!f->ip) {
        return;
    }
    if (app_net_is_online() && app_net_ip()[0]) {
        char line[80];
        snprintf(line, sizeof(line), "%s  http://%s", app_net_kind_name(), app_net_ip());
        if (!f->ip || !lv_label_get_text(f->ip) || strcmp(lv_label_get_text(f->ip), line) != 0) {
            lv_label_set_text(f->ip, line);
        }
        lv_obj_set_style_text_color(f->ip, COL_PW, 0);
    } else if (app_wifi_ap_is_up()) {
        char line[80];
        snprintf(line, sizeof(line), "AP http://%s", APP_WIFI_AP_IP);
        if (!f->ip || !lv_label_get_text(f->ip) || strcmp(lv_label_get_text(f->ip), line) != 0) {
            lv_label_set_text(f->ip, line);
        }
        lv_obj_set_style_text_color(f->ip, COL_STATUS, 0);
    } else {
        const char *off = "Rete: offline";
        if (!f->ip || !lv_label_get_text(f->ip) || strcmp(lv_label_get_text(f->ip), off) != 0) {
            lv_label_set_text(f->ip, off);
        }
        lv_obj_set_style_text_color(f->ip, COL_MUTED, 0);
    }
}

static void set_reads_label(settings_form_t *f)
{
    if (!f->reads) {
        return;
    }
    static char buf[2400];
    energy_model_format_reads(buf, sizeof(buf));
    const char *cur = lv_label_get_text(f->reads);
    if (!cur || strcmp(cur, buf) != 0) {
        lv_label_set_text(f->reads, buf);
    }
}

void ui_settings_on_wifi(void)
{
    set_ip_label(&s_tab);
    set_ip_label(&s_wiz);
    set_reads_label(&s_tab);
    set_reads_label(&s_wiz);
}

void ui_settings_set_scan_busy(bool busy)
{
    ui_settings_show_message(busy ? "Ricerca reti..." : "", false);
}
