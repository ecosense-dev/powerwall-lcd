#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_i18n.h"
#include "app_net.h"
#include "app_tz.h"
#include "app_wifi.h"
#include "ui_icons.h"
#include "ui_theme.h"
#include "weather_client.h"

#define FLOW_THRESH_W 80.0f
#define FLOW_DOTS     2
#define WX_COL_X      516
#define WX_COL_W      276

typedef struct {
    lv_point_t a;
    lv_point_t b;
    lv_obj_t *dot[FLOW_DOTS];
    int dir;
    float phase;
    float speed;
} flow_seg_t;

static lv_obj_t *s_site;
static lv_obj_t *s_clock;
static lv_obj_t *s_clock_date;
static lv_obj_t *s_status;
static lv_obj_t *s_wx_box;
static lv_obj_t *s_wx_icon;
static lv_obj_t *s_wx_temp;
static lv_obj_t *s_wx_cond;
static lv_obj_t *s_err;
static lv_obj_t *s_solar_v;
static lv_obj_t *s_home_v;
static lv_obj_t *s_pw_v;
static lv_obj_t *s_grid_v;
static lv_obj_t *s_t_solar;
static lv_obj_t *s_t_home;
static lv_obj_t *s_t_grid;
static lv_obj_t *s_batt_fill;
static lv_point_t s_pts_sh[2];
static lv_point_t s_pts_hp[2];
static lv_point_t s_pts_hg[2];
static lv_obj_t *s_line_sh;
static lv_obj_t *s_line_hp;
static lv_obj_t *s_line_hg;
static flow_seg_t s_flow[3];
static bool s_batt_charging;

typedef struct {
    lv_obj_t *when;
    lv_obj_t *icon;
    lv_obj_t *temp;
    lv_obj_t *desc;
} fc_slot_ui_t;

typedef struct {
    lv_obj_t *title;
    fc_slot_ui_t slot[WEATHER_SLOTS];
} fc_day_ui_t;

static fc_day_ui_t s_fc[WEATHER_DAYS];

static void label_set(lv_obj_t *obj, const char *txt)
{
    if (!obj || !txt) {
        return;
    }
    const char *cur = lv_label_get_text(obj);
    if (cur && strcmp(cur, txt) == 0) {
        return;
    }
    lv_label_set_text(obj, txt);
}

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

static lv_obj_t *make_caption(lv_obj_t *parent, lv_coord_t cx, lv_coord_t top_y, const char *title,
                              lv_obj_t **value_out)
{
    lv_obj_t *t = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, cx - 400, top_y);

    lv_obj_t *v = ui_label(parent, &lv_font_montserrat_20, COL_TEXT);
    lv_label_set_text(v, "--.- kW");
    lv_obj_align(v, LV_ALIGN_TOP_MID, cx - 400, top_y + 94);
    *value_out = v;
    return t;
}

static void caption_right_of_line(lv_obj_t *obj, lv_coord_t line_x, lv_coord_t y)
{
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(obj, LV_ALIGN_TOP_LEFT, line_x + 22, y);
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

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void make_fc_slot(lv_obj_t *card, fc_slot_ui_t *s, lv_coord_t y, const char *when)
{
    s->icon = ui_wx_icon_create(card, 24);
    lv_obj_set_pos(s->icon, 8, y);

    s->when = ui_label(card, &lv_font_montserrat_12, COL_MUTED);
    lv_label_set_text(s->when, when);
    lv_obj_set_pos(s->when, 38, y + 4);
    lv_obj_set_width(s->when, 68);

    s->temp = ui_label(card, &lv_font_montserrat_16, COL_TEXT);
    lv_label_set_text(s->temp, "--C");
    lv_obj_set_pos(s->temp, 108, y + 2);
    lv_obj_set_width(s->temp, 48);

    s->desc = ui_label(card, &lv_font_montserrat_12, COL_MUTED);
    lv_label_set_text(s->desc, "--");
    lv_obj_set_pos(s->desc, 158, y + 5);
    lv_obj_set_width(s->desc, 108);
    lv_label_set_long_mode(s->desc, LV_LABEL_LONG_DOT);
}

static void clock_paint(void)
{
    if (!s_clock) {
        return;
    }
    char hm[8];
    char date[24];
    app_tz_format(hm, sizeof(hm), date, sizeof(date));
    label_set(s_clock, hm);
    label_set(s_clock_date, date);
}

static void clock_timer_cb(lv_timer_t *tm)
{
    (void)tm;
    clock_paint();
}

void ui_live_create(lv_obj_t *parent)
{
    s_wx_box = lv_obj_create(parent);
    lv_obj_set_size(s_wx_box, WX_COL_W, 52);
    lv_obj_set_pos(s_wx_box, WX_COL_X, 6);
    lv_obj_set_style_bg_opa(s_wx_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_wx_box, 0, 0);
    lv_obj_set_style_pad_all(s_wx_box, 0, 0);
    lv_obj_clear_flag(s_wx_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_wx_box, LV_OBJ_FLAG_CLICKABLE);

    s_wx_icon = ui_wx_icon_create(s_wx_box, 36);
    lv_obj_set_pos(s_wx_icon, 0, 8);

    s_wx_temp = ui_label(s_wx_box, &lv_font_montserrat_24, COL_MUTED);
    lv_label_set_text(s_wx_temp, "--C");
    lv_obj_set_pos(s_wx_temp, 44, 0);

    s_wx_cond = ui_label(s_wx_box, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(s_wx_cond, "");
    lv_obj_set_pos(s_wx_cond, 44, 28);

    static const char *when_lbl[WEATHER_SLOTS];
    when_lbl[0] = app_tr(STR_MORNING);
    when_lbl[1] = app_tr(STR_AFTERNOON);
    when_lbl[2] = app_tr(STR_EVENING);
    static const char *day_lbl[WEATHER_DAYS];
    day_lbl[0] = app_tr(STR_TODAY);
    day_lbl[1] = app_tr(STR_TOMORROW);
    day_lbl[2] = app_tr(STR_DAY_AFTER);
    for (int d = 0; d < WEATHER_DAYS; d++) {
        lv_obj_t *card = make_card(parent, WX_COL_X, 64 + d * 118, WX_COL_W, 112);
        s_fc[d].title = ui_label(card, &lv_font_montserrat_14, COL_TEXT);
        lv_label_set_text(s_fc[d].title, day_lbl[d]);
        lv_obj_set_pos(s_fc[d].title, 10, 4);
        for (int k = 0; k < WEATHER_SLOTS; k++) {
            make_fc_slot(card, &s_fc[d].slot[k], 24 + k * 28, when_lbl[k]);
        }
    }

    s_site = ui_label(parent, &lv_font_montserrat_24, COL_TEXT);
    lv_label_set_text(s_site, app_tr(STR_SITE));
    lv_obj_align(s_site, LV_ALIGN_TOP_LEFT, 16, 8);
    lv_obj_set_width(s_site, 268);
    lv_label_set_long_mode(s_site, LV_LABEL_LONG_DOT);

    s_clock = ui_label(parent, &lv_font_montserrat_28, COL_TEXT);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 4);

    s_clock_date = ui_label(parent, &lv_font_montserrat_12, COL_MUTED);
    lv_label_set_text(s_clock_date, "");
    lv_obj_align(s_clock_date, LV_ALIGN_TOP_MID, 0, 36);
    lv_timer_create(clock_timer_cb, 1000, NULL);
    clock_paint();

    s_status = ui_label(parent, &lv_font_montserrat_16, COL_STATUS);
    lv_label_set_text(s_status, app_tr(STR_WAITING));
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 16, 38);

    s_err = ui_label(parent, &lv_font_montserrat_12, COL_DANGER);
    lv_label_set_text(s_err, "");
    lv_obj_align(s_err, LV_ALIGN_TOP_LEFT, 16, 58);
    lv_label_set_long_mode(s_err, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_err, 200);

    const lv_coord_t sx = 252, sy = 118;
    const lv_coord_t hx = 252, hy = 250;
    const lv_coord_t px = 400, py = 340;
    const lv_coord_t gx = 104, gy = 340;

    s_line_sh = make_line(parent, s_pts_sh, sx, sy + 36, hx, hy - 36);
    s_line_hp = make_line(parent, s_pts_hp, hx + 36, hy + 8, px - 36, py - 12);
    s_line_hg = make_line(parent, s_pts_hg, hx - 36, hy + 8, gx + 36, py - 12);

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

    s_t_solar = make_caption(parent, sx, sy - 50, app_tr(STR_SOLAR), &s_solar_v);
    s_t_home = make_caption(parent, hx, hy - 50, app_tr(STR_HOME), &s_home_v);
    (void)make_caption(parent, px, py - 50, "Powerwall", &s_pw_v);
    s_t_grid = make_caption(parent, gx, gy - 50, app_tr(STR_GRID), &s_grid_v);
    caption_right_of_line(s_solar_v, sx, sy - 50 + 94);
    caption_right_of_line(s_t_home, hx, hy - 50);
    lv_obj_set_width(s_pw_v, 156);
    lv_label_set_long_mode(s_pw_v, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_pw_v, LV_TEXT_ALIGN_CENTER, 0);

    for (int s = 0; s < 3; s++) {
        for (int i = 0; i < FLOW_DOTS; i++) {
            lv_obj_move_foreground(s_flow[s].dot[i]);
        }
    }
}

static lv_color_t wx_color(int code)
{
    if (code == 0 || code == 1) {
        return COL_SOLAR;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return COL_HOME_BLUE;
    }
    if (code >= 95) {
        return COL_STATUS;
    }
    return COL_TEXT;
}

static void wx_set(const char *temp, lv_color_t temp_c, const char *cond, lv_color_t cond_c, int wmo,
                   bool show_icon)
{
    static bool icon_layout;
    label_set(s_wx_temp, temp);
    lv_obj_set_style_text_color(s_wx_temp, temp_c, 0);
    label_set(s_wx_cond, cond);
    lv_obj_set_style_text_color(s_wx_cond, cond_c, 0);
    if (show_icon) {
        ui_wx_icon_set(s_wx_icon, wmo);
        lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
        if (!icon_layout) {
            lv_obj_set_pos(s_wx_temp, 44, 0);
            lv_obj_set_pos(s_wx_cond, 44, 28);
            icon_layout = true;
        }
    } else {
        lv_obj_add_flag(s_wx_icon, LV_OBJ_FLAG_HIDDEN);
        if (icon_layout) {
            lv_obj_set_pos(s_wx_temp, 8, 0);
            lv_obj_set_pos(s_wx_cond, 8, 28);
            icon_layout = false;
        }
    }
}

static void forecast_set(const weather_t *wx)
{
    const char *fallback[WEATHER_DAYS] = {app_tr(STR_TODAY), app_tr(STR_TOMORROW), app_tr(STR_DAY_AFTER)};
    for (int d = 0; d < WEATHER_DAYS; d++) {
        label_set(s_fc[d].title, fallback[d]);
        for (int k = 0; k < WEATHER_SLOTS; k++) {
            const weather_slot_t *sl = wx ? &wx->day[d].slot[k] : NULL;
            if (sl && sl->valid) {
                char t[12];
                snprintf(t, sizeof(t), "%.0fC", sl->temp_c);
                label_set(s_fc[d].slot[k].temp, t);
                lv_obj_set_style_text_color(s_fc[d].slot[k].temp, wx_color(sl->wmo_code), 0);
                label_set(s_fc[d].slot[k].desc, sl->condition);
                ui_wx_icon_set(s_fc[d].slot[k].icon, sl->wmo_code);
                lv_obj_clear_flag(s_fc[d].slot[k].icon, LV_OBJ_FLAG_HIDDEN);
            } else {
                label_set(s_fc[d].slot[k].temp, "--C");
                lv_obj_set_style_text_color(s_fc[d].slot[k].temp, COL_MUTED, 0);
                label_set(s_fc[d].slot[k].desc, "--");
                lv_obj_add_flag(s_fc[d].slot[k].icon, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void ui_live_update(const energy_state_t *st)
{
    if (!s_site) {
        return;
    }

    const energy_live_t *l = &st->live;
    label_set(s_site, l->site_name[0] ? l->site_name : app_tr(STR_SITE));
    label_set(s_status, l->status[0] ? l->status : app_tr(STR_WAITING));
    lv_obj_set_style_text_color(s_status, COL_STATUS, 0);
    clock_paint();

    weather_t wx;
    weather_client_get(&wx);
    if (app_wifi_ap_is_up() && !app_net_is_online()) {
        wx_set(APP_WIFI_AP_SSID, COL_STATUS, app_tr(STR_SETUP_WIFI), COL_MUTED, 0, false);
        forecast_set(NULL);
    } else if (wx.valid) {
        char t[16];
        snprintf(t, sizeof(t), "%.0fC", wx.temp_c);
        wx_set(t, wx_color(wx.wmo_code), wx.condition, COL_MUTED, wx.wmo_code, true);
        forecast_set(&wx);
    } else if (!app_config_has_gps()) {
        wx_set(app_tr(STR_WX), COL_MUTED, app_tr(STR_SET_GPS), COL_MUTED, 0, false);
        forecast_set(NULL);
    } else if (!app_net_is_online()) {
        wx_set(app_tr(STR_WX), COL_DANGER, app_tr(STR_NET_OFFLINE), COL_DANGER, 0, false);
        forecast_set(NULL);
    } else {
        wx_set(app_tr(STR_WX), COL_MUTED, "...", COL_MUTED, 0, false);
        forecast_set(&wx);
    }

    if (st->fetching) {
        label_set(s_status, app_tr(STR_READING));
        lv_obj_set_style_text_color(s_status, COL_STATUS, 0);
    } else if (app_net_is_online() && !l->status[0]) {
        label_set(s_status, app_net_kind() == APP_NET_LTE ? app_tr(STR_ONLINE_4G) : app_tr(STR_ONLINE));
    }

    if (st->last_error[0]) {
        label_set(s_err, st->last_error);
        lv_obj_set_style_text_color(s_err, COL_DANGER, 0);
    } else if (app_net_is_online() && app_net_ip()[0] && !app_config_has_token()) {
        char hint[96];
        snprintf(hint, sizeof(hint), app_tr(STR_TOKEN_HINT), app_net_ip());
        label_set(s_err, hint);
        lv_obj_set_style_text_color(s_err, COL_STATUS, 0);
    } else if (app_net_is_online() && app_net_ip()[0]) {
        char hint[80];
        snprintf(hint, sizeof(hint), app_tr(STR_CONFIG_HINT), app_net_ip());
        label_set(s_err, hint);
        lv_obj_set_style_text_color(s_err, COL_MUTED, 0);
    } else if (app_wifi_ap_is_up()) {
        char hint[128];
        snprintf(hint, sizeof(hint), app_tr(STR_AP_HINT), APP_WIFI_AP_SSID, APP_WIFI_AP_PASS, APP_WIFI_AP_IP);
        label_set(s_err, hint);
        lv_obj_set_style_text_color(s_err, COL_STATUS, 0);
    } else if (app_wifi_last_error()[0]) {
        label_set(s_err, app_wifi_last_error());
        lv_obj_set_style_text_color(s_err, COL_DANGER, 0);
    } else {
        label_set(s_err, "");
    }

    char buf[48];
    energy_fmt_kw(buf, sizeof(buf), l->valid ? l->solar_w : 0);
    if (!l->valid) {
        strlcpy(buf, "--.- kW", sizeof(buf));
    }
    label_set(s_solar_v, buf);

    energy_fmt_kw(buf, sizeof(buf), l->valid ? l->home_w : 0);
    if (!l->valid) {
        strlcpy(buf, "--.- kW", sizeof(buf));
    }
    label_set(s_home_v, buf);

    if (l->valid) {
        char kw[24];
        energy_fmt_kw(kw, sizeof(kw), l->battery_w < 0 ? -l->battery_w : l->battery_w);
        const char *dir = "";
        if (l->battery_w < -FLOW_THRESH_W) {
            dir = app_tr(STR_DIR_CHARGE);
        } else if (l->battery_w > FLOW_THRESH_W) {
            dir = app_tr(STR_DIR_DISCHARGE);
        }
        float soc = l->soc_pct;
        if (soc < 0) {
            soc = 0;
        }
        if (soc > 100) {
            soc = 100;
        }
        snprintf(buf, sizeof(buf), "%s  %.0f%%%s", kw, soc, dir);
        ui_icon_battery_set_soc(s_batt_fill, l->soc_pct);
        batt_set_charging(l->battery_w < -FLOW_THRESH_W);
    } else {
        strlcpy(buf, "--.- kW", sizeof(buf));
        ui_icon_battery_set_soc(s_batt_fill, 0);
        batt_set_charging(false);
    }
    label_set(s_pw_v, buf);

    energy_fmt_kw(buf, sizeof(buf), l->valid ? l->grid_w : 0);
    if (!l->valid) {
        strlcpy(buf, "--.- kW", sizeof(buf));
    }
    label_set(s_grid_v, buf);

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
}

void ui_live_apply_lang(void)
{
    if (s_t_solar) {
        lv_label_set_text(s_t_solar, app_tr(STR_SOLAR));
    }
    if (s_t_home) {
        lv_label_set_text(s_t_home, app_tr(STR_HOME));
    }
    if (s_t_grid) {
        lv_label_set_text(s_t_grid, app_tr(STR_GRID));
    }
    const char *when[WEATHER_SLOTS] = {app_tr(STR_MORNING), app_tr(STR_AFTERNOON), app_tr(STR_EVENING)};
    for (int d = 0; d < WEATHER_DAYS; d++) {
        if (s_fc[d].title) {
            lv_label_set_text(s_fc[d].title, app_tr((app_str_id_t)(STR_TODAY + d)));
        }
        for (int k = 0; k < WEATHER_SLOTS; k++) {
            if (s_fc[d].slot[k].when) {
                lv_label_set_text(s_fc[d].slot[k].when, when[k]);
            }
        }
    }
}
