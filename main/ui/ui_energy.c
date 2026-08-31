#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "ui_theme.h"

static lv_obj_t *s_when;
static lv_obj_t *s_sub;
static lv_obj_t *s_title;
static lv_obj_t *s_yunit;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_ser_solar;
static lv_chart_series_t *s_ser_batt;
static lv_chart_series_t *s_ser_home;
static lv_obj_t *s_mix_home;
static lv_obj_t *s_mix_pw;
static lv_obj_t *s_mix_grid;
static lv_obj_t *s_mix_home_kwh;
static lv_obj_t *s_mix_pw_kwh;
static lv_obj_t *s_mix_grid_kwh;
static char s_xlbl[8][8];
static uint8_t s_xmaj = 2;

static void fill_area(lv_obj_draw_part_dsc_t *dsc, lv_coord_t ybot, lv_color_t color, lv_opa_t opa)
{
    if (!dsc->p1 || !dsc->p2 || !dsc->draw_ctx) {
        return;
    }
    lv_point_t pts[4] = {
        *dsc->p1,
        *dsc->p2,
        {dsc->p2->x, ybot},
        {dsc->p1->x, ybot},
    };
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = color;
    rd.bg_opa = opa;
    rd.border_width = 0;
    rd.outline_width = 0;
    rd.shadow_width = 0;
    lv_draw_polygon(dsc->draw_ctx, &rd, pts, 4);
}

static void chart_draw_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc) {
        return;
    }

    if (dsc->type == LV_CHART_DRAW_PART_LINE_AND_POINT && dsc->line_dsc) {
        lv_obj_t *obj = lv_event_get_target(e);
        lv_area_t cont;
        lv_obj_get_content_coords(obj, &cont);
        lv_coord_t ybot = cont.y2;
        const lv_chart_series_t *ser = dsc->sub_part_ptr;
        if (ser == s_ser_solar) {
            fill_area(dsc, ybot, COL_SOLAR_AREA, LV_OPA_40);
            dsc->line_dsc->color = lv_color_hex(0xD0D0D0);
            dsc->line_dsc->width = 2;
        } else if (ser == s_ser_batt) {
            fill_area(dsc, ybot, COL_PW, LV_OPA_80);
            dsc->line_dsc->opa = LV_OPA_TRANSP;
        } else if (ser == s_ser_home) {
            dsc->line_dsc->color = COL_HOME_BLUE;
            dsc->line_dsc->width = 3;
        }
        return;
    }

    if (dsc->part != LV_PART_TICKS || dsc->type != LV_CHART_DRAW_PART_TICK_LABEL || !dsc->text) {
        return;
    }
    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y || dsc->id == LV_CHART_AXIS_SECONDARY_Y) {
        float v = dsc->value / 10.0f;
        if (v < 0.05f) {
            strlcpy(dsc->text, "0", dsc->text_length);
        } else if (v >= 10.0f) {
            snprintf(dsc->text, dsc->text_length, "%.0f", v);
        } else {
            snprintf(dsc->text, dsc->text_length, "%.0f", v);
        }
    } else if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        int i = (int)dsc->value;
        if (i < 0) {
            i = 0;
        }
        if (s_xmaj > 0 && i >= s_xmaj) {
            i = s_xmaj - 1;
        }
        if (i == 0 || i == 4) {
            dsc->text[0] = '\0';
            return;
        }
        if (i < (int)(sizeof(s_xlbl) / sizeof(s_xlbl[0]))) {
            strlcpy(dsc->text, s_xlbl[i], dsc->text_length);
        }
    }
}

static void apply_axes(int y_major, int x_major)
{
    if (y_major < 2) {
        y_major = 2;
    }
    if (x_major < 2) {
        x_major = 2;
    }
    if (x_major > 8) {
        x_major = 8;
    }
    s_xmaj = (uint8_t)x_major;
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 2, 1, false, 0);
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, y_major, 1, true, 36);
    lv_chart_set_axis_tick(s_chart, LV_CHART_AXIS_PRIMARY_X, 4, 2, x_major, 1, true, 28);
}

static lv_coord_t to_chart(float watts)
{
    if (watts < 0) {
        watts = 0;
    }
    return (lv_coord_t)(watts / 100.0f);
}

static void fill_chart_day(const energy_day_t *d)
{
    int n = ENERGY_DAY_MAX_POINTS;
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, n);
    float maxv = 1.0f;
    int last = d->count > 0 && d->count < n ? d->count : n;
    for (int i = 0; i < last; i++) {
        float v = d->solar_w[i] / 1000.0f;
        if (v > maxv) {
            maxv = v;
        }
        if (d->home_w[i] / 1000.0f > maxv) {
            maxv = d->home_w[i] / 1000.0f;
        }
    }
    lv_coord_t ymax = (lv_coord_t)(maxv * 1.15f * 10);
    if (ymax < 40) {
        ymax = 40;
    }
    /* Round Y to 2/4/6/8 kW like Tesla. */
    if (ymax < 40) {
        ymax = 40;
    } else if (ymax <= 60) {
        ymax = 60;
    } else if (ymax <= 80) {
        ymax = 80;
    } else if (ymax <= 100) {
        ymax = 100;
    }
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, ymax);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_SECONDARY_Y, 0, ymax);
    for (int i = 0; i < n; i++) {
        float solar = d->solar_w[i] > 0 ? d->solar_w[i] : 0;
        float charge = d->battery_w[i] < 0 ? -d->battery_w[i] : 0;
        if (charge > solar) {
            charge = solar;
        }
        s_ser_solar->y_points[i] = to_chart(solar);
        s_ser_batt->y_points[i] = to_chart(charge);
        s_ser_home->y_points[i] = to_chart(d->home_w[i]);
    }
    strlcpy(s_xlbl[0], "0", sizeof(s_xlbl[0]));
    strlcpy(s_xlbl[1], "6", sizeof(s_xlbl[1]));
    strlcpy(s_xlbl[2], "12", sizeof(s_xlbl[2]));
    strlcpy(s_xlbl[3], "18", sizeof(s_xlbl[3]));
    strlcpy(s_xlbl[4], "24", sizeof(s_xlbl[4]));
    apply_axes(3, 5);
    lv_label_set_text(s_yunit, "kW");
    lv_chart_refresh(s_chart);
}

static void update_flow(const energy_period_t *p)
{
    float to_home = p->solar_to_home_kwh;
    float to_batt = p->solar_to_battery_kwh;
    float to_grid = p->solar_to_grid_kwh;
    float tot = to_home + to_batt + to_grid;
    if (tot < 0.01f) {
        tot = 1.0f;
    }
    int ph = (int)(to_home * 100.0f / tot + 0.5f);
    int pb = (int)(to_batt * 100.0f / tot + 0.5f);
    int pg = (int)(to_grid * 100.0f / tot + 0.5f);

    char b[48];
    snprintf(b, sizeof(b), "%d%%  Casa", ph);
    lv_label_set_text(s_mix_home, b);
    snprintf(b, sizeof(b), "%.1f kWh", to_home);
    lv_label_set_text(s_mix_home_kwh, b);

    snprintf(b, sizeof(b), "%d%%  Powerwall", pb);
    lv_label_set_text(s_mix_pw, b);
    snprintf(b, sizeof(b), "%.1f kWh", to_batt);
    lv_label_set_text(s_mix_pw_kwh, b);

    snprintf(b, sizeof(b), "%d%%  Rete", pg);
    lv_label_set_text(s_mix_grid, b);
    snprintf(b, sizeof(b), "%.1f kWh", to_grid);
    lv_label_set_text(s_mix_grid_kwh, b);
}

static void apply_state(const energy_state_t *st)
{
    lv_label_set_text(s_when, "Oggi");
    if (st->day.valid) {
        fill_chart_day(&st->day);
    }
    if (st->today.valid) {
        char kwh[32];
        snprintf(kwh, sizeof(kwh), "%.1f kWh", st->today.solar_gen_kwh);
        lv_label_set_text(s_title, kwh);
        update_flow(&st->today);
    } else {
        lv_label_set_text(s_title, "-- kWh");
        energy_period_t empty;
        memset(&empty, 0, sizeof(empty));
        update_flow(&empty);
    }
}

static lv_obj_t *dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, 18, 18);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void ui_energy_create(lv_obj_t *parent)
{
    s_when = ui_label(parent, &lv_font_montserrat_20, COL_TEXT);
    lv_label_set_text(s_when, "Oggi");
    lv_obj_align(s_when, LV_ALIGN_TOP_LEFT, 12, 6);

    s_sub = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(s_sub, "Totale prodotto");
    lv_obj_align(s_sub, LV_ALIGN_TOP_LEFT, 12, 34);

    s_title = ui_label(parent, &lv_font_montserrat_24, COL_TEXT);
    lv_label_set_text(s_title, "-- kWh");
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 12, 52);

    s_yunit = ui_label(parent, &lv_font_montserrat_12, COL_MUTED);
    lv_label_set_text(s_yunit, "kW");
    lv_obj_align(s_yunit, LV_ALIGN_TOP_RIGHT, -18, 58);

    s_chart = lv_chart_create(parent);
    lv_obj_set_size(s_chart, 776, 200);
    lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(s_chart, COL_BG, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_left(s_chart, 8, 0);
    lv_obj_set_style_pad_right(s_chart, 40, 0);
    lv_obj_set_style_pad_top(s_chart, 8, 0);
    lv_obj_set_style_pad_bottom(s_chart, 24, 0);
    lv_obj_set_style_text_color(s_chart, COL_MUTED, LV_PART_TICKS);
    lv_obj_set_style_text_font(s_chart, &lv_font_montserrat_12, LV_PART_TICKS);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x2A2A2A), LV_PART_TICKS);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, 24);
    lv_chart_set_div_line_count(s_chart, 3, 4);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(s_chart, 4, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(s_chart, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_chart, chart_draw_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    apply_axes(3, 5);

    /* Add solar first so it is drawn behind battery fill and home line. */
    s_ser_solar = lv_chart_add_series(s_chart, COL_SOLAR_AREA, LV_CHART_AXIS_SECONDARY_Y);
    s_ser_batt = lv_chart_add_series(s_chart, COL_PW, LV_CHART_AXIS_SECONDARY_Y);
    s_ser_home = lv_chart_add_series(s_chart, COL_HOME_BLUE, LV_CHART_AXIS_SECONDARY_Y);

    lv_obj_t *flusso = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_label_set_text(flusso, "Flusso di energia");
    lv_obj_align(flusso, LV_ALIGN_TOP_LEFT, 12, 288);

    lv_obj_t *used = ui_label(parent, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(used, "Utilizzata da");
    lv_obj_align(used, LV_ALIGN_TOP_LEFT, 12, 312);

    dot(parent, 12, 338, COL_HOME_BLUE);
    s_mix_home = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_pos(s_mix_home, 40, 336);
    lv_label_set_text(s_mix_home, "Casa");
    s_mix_home_kwh = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_align(s_mix_home_kwh, LV_ALIGN_TOP_RIGHT, -16, 336);

    dot(parent, 12, 366, COL_PW);
    s_mix_pw = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_pos(s_mix_pw, 40, 364);
    lv_label_set_text(s_mix_pw, "Powerwall");
    s_mix_pw_kwh = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_align(s_mix_pw_kwh, LV_ALIGN_TOP_RIGHT, -16, 364);

    dot(parent, 12, 394, COL_GRID);
    s_mix_grid = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_pos(s_mix_grid, 40, 392);
    lv_label_set_text(s_mix_grid, "Rete");
    s_mix_grid_kwh = ui_label(parent, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_align(s_mix_grid_kwh, LV_ALIGN_TOP_RIGHT, -16, 392);
}

void ui_energy_update(const energy_state_t *st)
{
    if (!s_title) {
        return;
    }
    apply_state(st);
}
