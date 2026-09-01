#include "ui_icons.h"

#include <stdint.h>

#include "ui_theme.h"

static lv_obj_t *host(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x - w / 2, y - h / 2);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

static lv_obj_t *block(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                       lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *make_line(lv_obj_t *parent, lv_point_t *pts, uint16_t n, lv_color_t color, lv_coord_t w)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_line_set_points(line, pts, n);
    lv_obj_set_style_line_width(line, w, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

lv_obj_t *ui_icon_sun(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy)
{
    lv_obj_t *box = host(parent, cx, cy, 72, 72);
    static const lv_coord_t rx[8] = {33, 50, 56, 50, 33, 16, 10, 16};
    static const lv_coord_t ry[8] = {10, 16, 33, 50, 56, 50, 33, 16};
    for (int i = 0; i < 8; i++) {
        block(box, rx[i], ry[i], 6, 6, COL_SOLAR, 3);
    }
    block(box, 22, 22, 28, 28, COL_SOLAR, LV_RADIUS_CIRCLE);
    return box;
}

lv_obj_t *ui_icon_house(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy)
{
    lv_obj_t *box = host(parent, cx, cy, 72, 72);
    static lv_point_t roof[] = {{8, 32}, {36, 8}, {64, 32}};
    make_line(box, roof, 3, COL_HOME, 4);
    block(box, 16, 32, 40, 30, COL_HOME, 3);
    block(box, 30, 42, 12, 20, COL_BG, 2);
    block(box, 20, 38, 10, 8, COL_CARD, 2);
    block(box, 42, 38, 10, 8, COL_CARD, 2);
    return box;
}

lv_obj_t *ui_icon_pylon(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy)
{
    lv_obj_t *box = host(parent, cx, cy, 72, 72);
    static lv_point_t left[] = {{18, 68}, {34, 10}};
    static lv_point_t right[] = {{54, 68}, {38, 10}};
    static lv_point_t top[] = {{28, 8}, {44, 8}};
    static lv_point_t peak[] = {{36, 4}, {36, 12}};
    static lv_point_t b1[] = {{22, 56}, {50, 56}};
    static lv_point_t b2[] = {{24, 42}, {48, 42}};
    static lv_point_t b3[] = {{28, 28}, {44, 28}};
    static lv_point_t x1[] = {{22, 56}, {48, 42}};
    static lv_point_t x2[] = {{50, 56}, {24, 42}};
    make_line(box, left, 2, COL_GRID, 4);
    make_line(box, right, 2, COL_GRID, 4);
    make_line(box, top, 2, COL_GRID, 3);
    make_line(box, peak, 2, COL_GRID, 3);
    make_line(box, b1, 2, COL_GRID, 3);
    make_line(box, b2, 2, COL_GRID, 3);
    make_line(box, b3, 2, COL_GRID, 3);
    make_line(box, x1, 2, COL_GRID, 2);
    make_line(box, x2, 2, COL_GRID, 2);
    return box;
}

lv_obj_t *ui_icon_battery(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy, lv_obj_t **fill_out)
{
    lv_obj_t *box = host(parent, cx, cy, 72, 72);
    block(box, 28, 6, 16, 8, COL_PW, 3);
    lv_obj_t *body = lv_obj_create(box);
    lv_obj_set_size(body, 40, 54);
    lv_obj_set_pos(body, 16, 12);
    lv_obj_set_style_bg_color(body, COL_ELEV, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(body, COL_PW, 0);
    lv_obj_set_style_border_width(body, 3, 0);
    lv_obj_set_style_radius(body, 8, 0);
    lv_obj_set_style_pad_all(body, 4, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fill = lv_obj_create(body);
    lv_obj_set_width(fill, 28);
    lv_obj_set_height(fill, 4);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(fill, COL_PW, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    if (fill_out) {
        *fill_out = fill;
    }
    return box;
}

void ui_icon_battery_set_soc(lv_obj_t *fill, float soc_pct)
{
    if (!fill) {
        return;
    }
    if (soc_pct < 0) {
        soc_pct = 0;
    }
    if (soc_pct > 100) {
        soc_pct = 100;
    }
    lv_coord_t h = 4 + (lv_coord_t)((42 * soc_pct) / 100.0f);
    lv_obj_set_height(fill, h);
    lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_color_t c = COL_PW;
    if (soc_pct < 15) {
        c = COL_DANGER;
    } else if (soc_pct < 40) {
        c = COL_SOLAR;
    }
    lv_obj_set_style_bg_color(fill, c, 0);
}

static lv_coord_t sc(lv_coord_t size, int v)
{
    return (lv_coord_t)((size * v + 16) / 32);
}

typedef enum {
    UI_WX_NONE = 0,
    UI_WX_SUN,
    UI_WX_PARTLY,
    UI_WX_CLOUD,
    UI_WX_FOG,
    UI_WX_RAIN,
    UI_WX_SNOW,
    UI_WX_STORM,
} ui_wx_kind_t;

static ui_wx_kind_t kind_from_wmo(int code)
{
    if (code == 0 || code == 1) {
        return UI_WX_SUN;
    }
    if (code == 2) {
        return UI_WX_PARTLY;
    }
    if (code == 3) {
        return UI_WX_CLOUD;
    }
    if (code == 45 || code == 48) {
        return UI_WX_FOG;
    }
    if (code >= 71 && code <= 77) {
        return UI_WX_SNOW;
    }
    if (code == 85 || code == 86) {
        return UI_WX_SNOW;
    }
    if (code >= 95) {
        return UI_WX_STORM;
    }
    if (code >= 51) {
        return UI_WX_RAIN;
    }
    return UI_WX_CLOUD;
}

static void draw_sun(lv_obj_t *box, lv_coord_t size, lv_coord_t ox, lv_coord_t oy, lv_coord_t sun_s)
{
    lv_coord_t ray = sc(sun_s, 4);
    if (ray < 2) {
        ray = 2;
    }
    static const int rx[8] = {14, 24, 28, 24, 14, 4, 0, 4};
    static const int ry[8] = {0, 4, 14, 24, 28, 24, 14, 4};
    for (int i = 0; i < 8; i++) {
        block(box, ox + sc(sun_s, rx[i]), oy + sc(sun_s, ry[i]), ray, ray, COL_SOLAR, ray / 2);
    }
    lv_coord_t d = sc(sun_s, 16);
    block(box, ox + sc(sun_s, 8), oy + sc(sun_s, 8), d, d, COL_SOLAR, LV_RADIUS_CIRCLE);
    (void)size;
}

static void draw_cloud(lv_obj_t *box, lv_coord_t size, lv_coord_t ox, lv_coord_t oy, lv_color_t color)
{
    lv_coord_t a = sc(size, 12);
    lv_coord_t b = sc(size, 14);
    lv_coord_t c = sc(size, 10);
    block(box, ox + sc(size, 4), oy + sc(size, 12), a, a, color, LV_RADIUS_CIRCLE);
    block(box, ox + sc(size, 10), oy + sc(size, 8), b, b, color, LV_RADIUS_CIRCLE);
    block(box, ox + sc(size, 18), oy + sc(size, 12), a, a, color, LV_RADIUS_CIRCLE);
    block(box, ox + sc(size, 6), oy + sc(size, 16), sc(size, 20), c, color, 4);
}

static void draw_kind(lv_obj_t *box, ui_wx_kind_t kind, lv_coord_t size)
{
    switch (kind) {
    case UI_WX_SUN:
        draw_sun(box, size, 0, 0, size);
        break;
    case UI_WX_PARTLY:
        draw_sun(box, size, sc(size, 10), 0, sc(size, 20));
        draw_cloud(box, size, 0, sc(size, 8), COL_CLOUD);
        break;
    case UI_WX_CLOUD:
        draw_cloud(box, size, 0, sc(size, 4), COL_CLOUD);
        break;
    case UI_WX_FOG:
        block(box, sc(size, 4), sc(size, 8), sc(size, 24), sc(size, 4), COL_FOG, 2);
        block(box, sc(size, 6), sc(size, 15), sc(size, 20), sc(size, 4), COL_FOG, 2);
        block(box, sc(size, 4), sc(size, 22), sc(size, 24), sc(size, 4), COL_FOG, 2);
        break;
    case UI_WX_RAIN:
        draw_cloud(box, size, 0, 0, COL_CLOUD);
        block(box, sc(size, 8), sc(size, 22), sc(size, 3), sc(size, 7), COL_HOME_BLUE, 2);
        block(box, sc(size, 15), sc(size, 24), sc(size, 3), sc(size, 7), COL_HOME_BLUE, 2);
        block(box, sc(size, 22), sc(size, 22), sc(size, 3), sc(size, 7), COL_HOME_BLUE, 2);
        break;
    case UI_WX_SNOW:
        draw_cloud(box, size, 0, 0, COL_CLOUD);
        block(box, sc(size, 8), sc(size, 24), sc(size, 4), sc(size, 4), COL_TEXT, LV_RADIUS_CIRCLE);
        block(box, sc(size, 15), sc(size, 26), sc(size, 4), sc(size, 4), COL_TEXT, LV_RADIUS_CIRCLE);
        block(box, sc(size, 22), sc(size, 24), sc(size, 4), sc(size, 4), COL_TEXT, LV_RADIUS_CIRCLE);
        break;
    case UI_WX_STORM:
        draw_cloud(box, size, 0, 0, COL_CLOUD);
        block(box, sc(size, 14), sc(size, 16), sc(size, 6), sc(size, 8), COL_SOLAR, 1);
        block(box, sc(size, 12), sc(size, 22), sc(size, 8), sc(size, 8), COL_SOLAR, 1);
        break;
    default:
        break;
    }
}

lv_obj_t *ui_wx_icon_create(lv_obj_t *parent, lv_coord_t size)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(box, (void *)(uintptr_t)UI_WX_NONE);
    return box;
}

void ui_wx_icon_set(lv_obj_t *icon, int wmo_code)
{
    if (!icon) {
        return;
    }
    ui_wx_kind_t kind = kind_from_wmo(wmo_code);
    ui_wx_kind_t prev = (ui_wx_kind_t)(uintptr_t)lv_obj_get_user_data(icon);
    if (kind == prev && lv_obj_get_child_cnt(icon) > 0) {
        return;
    }
    lv_obj_clean(icon);
    lv_obj_set_user_data(icon, (void *)(uintptr_t)kind);
    draw_kind(icon, kind, lv_obj_get_width(icon));
}
