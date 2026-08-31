#include "ui_icons.h"

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
