#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_wifi.h"
#include "ui_icons.h"
#include "ui_theme.h"

#define FLOW_THRESH_W 80.0f
#define FLOW_DOTS     2

typedef struct {
    lv_point_t a;
    lv_point_t b;
    lv_obj_t *dot[FLOW_DOTS];
    int dir;
    float phase;
    float speed;
} flow_seg_t;

static lv_obj_t *s_site;
static lv_obj_t *s_status;
static lv_obj_t *s_wifi;
static lv_obj_t *s_err;
static lv_obj_t *s_solar_v;
static lv_obj_t *s_home_v;
static lv_obj_t *s_pw_v;
static lv_obj_t *s_grid_v;
static lv_obj_t *s_today;
static lv_obj_t *s_batt_fill;
static lv_point_t s_pts_sh[2];
static lv_point_t s_pts_hp[2];
static lv_point_t s_pts_hg[2];
static lv_obj_t *s_line_sh;
static lv_obj_t *s_line_hp;
static lv_obj_t *s_line_hg;
static flow_seg_t s_flow[3];
static bool s_batt_charging;

static lv_obj_t *make_dot(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, 12, 12);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_pad_all(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    return d;
}

static void flow_place(flow_seg_t *f, int i, float t)
{
    if (t < 0) {
        t += 1.0f;
    }
    if (t > 1.0f) {
        t -= 1.0f;
    }
    float x = f->a.x + (f->b.x - f->a.x) * t;
    float y = f->a.y + (f->b.y - f->a.y) * t;
    lv_obj_set_pos(f->dot[i], (lv_coord_t)(x - 6), (lv_coord_t)(y - 6));
}

static void flow_timer_cb(lv_timer_t *tm)
{
    (void)tm;
    for (int s = 0; s < 3; s++) {
        flow_seg_t *f = &s_flow[s];
        if (f->dir == 0) {
            continue;
        }
        f->phase += f->speed * (float)f->dir;
        if (f->phase >= 1.0f) {
            f->phase -= 1.0f;
        }
        if (f->phase < 0) {
            f->phase += 1.0f;
        }
        for (int i = 0; i < FLOW_DOTS; i++) {
            float t = f->phase + (float)i / (float)FLOW_DOTS;
            if (t >= 1.0f) {
                t -= 1.0f;
            }
            flow_place(f, i, t);
        }
    }
}

static void flow_set(flow_seg_t *f, int dir, lv_color_t color, float watts)
{
    f->dir = dir;
    float mag = watts < 0 ? -watts : watts;
    float kw = mag / 1000.0f;
    f->speed = 0.012f + 0.018f * (kw > 6.0f ? 6.0f : kw) / 6.0f;
    for (int i = 0; i < FLOW_DOTS; i++) {
        lv_obj_set_style_bg_color(f->dot[i], color, 0);
        if (dir == 0) {
            lv_obj_add_flag(f->dot[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(f->dot[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void batt_pulse_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void batt_set_charging(bool charging)
{
    if (!s_batt_fill) {
        return;
    }
    if (charging == s_batt_charging) {
        return;
    }
    s_batt_charging = charging;
    lv_anim_del(s_batt_fill, batt_pulse_cb);
    if (charging) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_batt_fill);
        lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
        lv_anim_set_time(&a, 650);
        lv_anim_set_playback_time(&a, 650);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&a, batt_pulse_cb);
        lv_anim_start(&a);
    } else {
        lv_obj_set_style_bg_opa(s_batt_fill, LV_OPA_COVER, 0);
    }
}

static void make_caption(lv_obj_t *parent, lv_coord_t cx, lv_coord_t top_y, const char *title,
                         lv_obj_t **value_out)
{
    lv_obj_t *t = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, cx - 400, top_y);

    lv_obj_t *v = ui_label(parent, &lv_font_montserrat_20, COL_TEXT);
    lv_label_set_text(v, "--.- kW");
    lv_obj_align(v, LV_ALIGN_TOP_MID, cx - 400, top_y + 94);
    *value_out = v;
}

static lv_obj_t *make_line(lv_obj_t *parent, lv_point_t *pts, lv_coord_t x1, lv_coord_t y1,
                           lv_coord_t x2, lv_coord_t y2)
{
    pts[0].x = x1;
    pts[0].y = y1;
    pts[1].x = x2;
    pts[1].y = y2;
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, COL_LINE, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

void ui_live_create(lv_obj_t *parent)
{
    s_site = ui_label(parent, &lv_font_montserrat_24, COL_TEXT);
    lv_label_set_text(s_site, APP_SITE_NAME_DEFAULT);
    lv_obj_align(s_site, LV_ALIGN_TOP_LEFT, 20, 12);

    s_status = ui_label(parent, &lv_font_montserrat_16, COL_STATUS);
    lv_label_set_text(s_status, "In attesa");
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 20, 44);

    s_wifi = ui_label(parent, &lv_font_montserrat_20, COL_MUTED);
    lv_label_set_text(s_wifi, "WiFi: --");
    lv_obj_align(s_wifi, LV_ALIGN_TOP_RIGHT, -20, 10);

    s_err = ui_label(parent, &lv_font_montserrat_14, COL_DANGER);
    lv_label_set_text(s_err, "");
    lv_obj_align(s_err, LV_ALIGN_TOP_MID, 0, 68);
    lv_label_set_long_mode(s_err, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_err, 760);

    const lv_coord_t sx = 400, sy = 148;
    const lv_coord_t hx = 400, hy = 268;
    const lv_coord_t px = 175, py = 338;
    const lv_coord_t gx = 625, gy = 338;

    s_line_sh = make_line(parent, s_pts_sh, sx, sy + 36, hx, hy - 36);
    s_line_hp = make_line(parent, s_pts_hp, hx - 36, hy + 8, px + 36, py - 12);
    s_line_hg = make_line(parent, s_pts_hg, hx + 36, hy + 8, gx - 36, py - 12);

    s_flow[0].a = s_pts_sh[0];
    s_flow[0].b = s_pts_sh[1];
    s_flow[1].a = s_pts_hp[0];
    s_flow[1].b = s_pts_hp[1];
    s_flow[2].a = s_pts_hg[0];
    s_flow[2].b = s_pts_hg[1];
    for (int s = 0; s < 3; s++) {
        lv_color_t c = s == 0 ? COL_SOLAR : (s == 1 ? COL_PW : COL_GRID);
        for (int i = 0; i < FLOW_DOTS; i++) {
            s_flow[s].dot[i] = make_dot(parent, c);
        }
        s_flow[s].phase = (float)s * 0.2f;
        s_flow[s].speed = 0.02f;
    }
    lv_timer_create(flow_timer_cb, 50, NULL);

    ui_icon_sun(parent, sx, sy);
    ui_icon_house(parent, hx, hy);
    ui_icon_battery(parent, px, py, &s_batt_fill);
    ui_icon_pylon(parent, gx, gy);

    make_caption(parent, sx, sy - 54, "Solare", &s_solar_v);
    make_caption(parent, hx, hy - 54, "Casa", &s_home_v);
    make_caption(parent, px, py - 54, "Powerwall", &s_pw_v);
    make_caption(parent, gx, gy - 54, "Rete", &s_grid_v);

    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < FLOW_DOTS; i++) {
            lv_obj_move_foreground(s_flow[s].dot[i]);
        }
    }

    s_today = ui_label(parent, &lv_font_montserrat_16, COL_MUTED);
    lv_label_set_text(s_today, "Oggi: -- kWh");
    lv_obj_align(s_today, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void ui_live_update(const energy_state_t *st)
{
    if (!s_site) {
        return;
    }

    const energy_live_t *l = &st->live;
    lv_label_set_text(s_site, l->site_name[0] ? l->site_name : APP_SITE_NAME_DEFAULT);
    lv_label_set_text(s_status, l->status[0] ? l->status : "In attesa");
    lv_obj_set_style_text_color(s_status, COL_STATUS, 0);

    if (app_wifi_is_connected() && app_wifi_ip()[0]) {
        lv_label_set_text(s_wifi, app_wifi_ip());
        lv_obj_set_style_text_color(s_wifi, COL_PW, 0);
    } else if (app_wifi_ap_is_up()) {
        lv_label_set_text(s_wifi, APP_WIFI_AP_SSID);
        lv_obj_set_style_text_color(s_wifi, COL_STATUS, 0);
    } else {
        lv_label_set_text(s_wifi, "WiFi: offline");
        lv_obj_set_style_text_color(s_wifi, COL_DANGER, 0);
    }

    if (st->fetching) {
        lv_label_set_text(s_status, "Lettura in corso...");
        lv_obj_set_style_text_color(s_status, COL_STATUS, 0);
    } else if (app_wifi_is_connected() && !l->status[0]) {
        lv_label_set_text(s_status, "In rete");
    }

    if (st->last_error[0]) {
        lv_label_set_text(s_err, st->last_error);
        lv_obj_set_style_text_color(s_err, COL_DANGER, 0);
    } else if (app_wifi_is_connected() && app_wifi_ip()[0] && !app_config_has_token()) {
        char hint[96];
        snprintf(hint, sizeof(hint), "Token: apri http://%s dal browser", app_wifi_ip());
        lv_label_set_text(s_err, hint);
        lv_obj_set_style_text_color(s_err, COL_STATUS, 0);
    } else if (app_wifi_is_connected() && app_wifi_ip()[0]) {
        char hint[80];
        snprintf(hint, sizeof(hint), "Config: http://%s", app_wifi_ip());
        lv_label_set_text(s_err, hint);
        lv_obj_set_style_text_color(s_err, COL_MUTED, 0);
    } else if (app_wifi_ap_is_up()) {
        char hint[128];
        snprintf(hint, sizeof(hint), "Collegati a %s  (password %s)  poi apri http://%s", APP_WIFI_AP_SSID,
                 APP_WIFI_AP_PASS, APP_WIFI_AP_IP);
        lv_label_set_text(s_err, hint);
        lv_obj_set_style_text_color(s_err, COL_STATUS, 0);
    } else if (app_wifi_last_error()[0]) {
        lv_label_set_text(s_err, app_wifi_last_error());
        lv_obj_set_style_text_color(s_err, COL_DANGER, 0);
    } else {
        lv_label_set_text(s_err, "");
    }

    char buf[48];
    energy_fmt_kw(buf, sizeof(buf), l->valid ? l->solar_w : 0);
    if (!l->valid) {
        strlcpy(buf, "--.- kW", sizeof(buf));
    }
    lv_label_set_text(s_solar_v, buf);

    energy_fmt_kw(buf, sizeof(buf), l->valid ? l->home_w : 0);
    if (!l->valid) {
        strlcpy(buf, "--.- kW", sizeof(buf));
    }
    lv_label_set_text(s_home_v, buf);

    if (l->valid) {
        char kw[24];
        energy_fmt_kw(kw, sizeof(kw), l->battery_w < 0 ? -l->battery_w : l->battery_w);
        const char *dir = "";
        if (l->battery_w < -FLOW_THRESH_W) {
            dir = "  carica";
        } else if (l->battery_w > FLOW_THRESH_W) {
            dir = "  scarica";
        }
        float soc = l->soc_pct;
        if (soc < 0) {
            soc = 0;
        }
        if (soc > 100) {
            soc = 100;
        }
        snprintf(buf, sizeof(buf), "%s · %.0f%%%s", kw, soc, dir);
        ui_icon_battery_set_soc(s_batt_fill, l->soc_pct);
        batt_set_charging(l->battery_w < -FLOW_THRESH_W);
    } else {
        strlcpy(buf, "--.- kW", sizeof(buf));
        ui_icon_battery_set_soc(s_batt_fill, 0);
        batt_set_charging(false);
    }
    lv_label_set_text(s_pw_v, buf);

    energy_fmt_kw(buf, sizeof(buf), l->valid ? l->grid_w : 0);
    if (!l->valid) {
        strlcpy(buf, "--.- kW", sizeof(buf));
    }
    lv_label_set_text(s_grid_v, buf);

    bool ok = l->valid;
    int solar_dir = (ok && l->solar_w > FLOW_THRESH_W) ? 1 : 0;
    int batt_dir = 0;
    if (ok && l->battery_w > FLOW_THRESH_W) {
        batt_dir = -1; /* scarica: batteria → casa */
    } else if (ok && l->battery_w < -FLOW_THRESH_W) {
        batt_dir = 1; /* carica: casa → batteria */
    }
    int grid_dir = 0;
    if (ok && l->grid_w > FLOW_THRESH_W) {
        grid_dir = -1; /* import: rete → casa */
    } else if (ok && l->grid_w < -FLOW_THRESH_W) {
        grid_dir = 1; /* export: casa → rete */
    }

    lv_obj_set_style_line_color(s_line_sh, solar_dir ? COL_SOLAR : COL_LINE, 0);
    lv_obj_set_style_line_width(s_line_sh, solar_dir ? 4 : 3, 0);
    lv_obj_set_style_line_color(s_line_hp, batt_dir ? COL_PW : COL_LINE, 0);
    lv_obj_set_style_line_width(s_line_hp, batt_dir ? 4 : 3, 0);
    lv_obj_set_style_line_color(s_line_hg, grid_dir ? COL_GRID : COL_LINE, 0);
    lv_obj_set_style_line_width(s_line_hg, grid_dir ? 4 : 3, 0);

    flow_set(&s_flow[0], solar_dir, COL_SOLAR, ok ? l->solar_w : 0);
    flow_set(&s_flow[1], batt_dir, COL_PW, ok ? l->battery_w : 0);
    flow_set(&s_flow[2], grid_dir, grid_dir < 0 ? COL_HOME_BLUE : COL_GRID, ok ? l->grid_w : 0);

    if (st->today.valid) {
        char line[64];
        snprintf(line, sizeof(line), "Solare oggi  %.1f kWh", st->today.solar_gen_kwh);
        lv_label_set_text(s_today, line);
    }
}
