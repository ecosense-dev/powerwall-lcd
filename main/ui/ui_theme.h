#pragma once

#include "lvgl.h"

#define COL_BG        lv_color_hex(0x000000)
#define COL_CARD      lv_color_hex(0x121212)
#define COL_ELEV      lv_color_hex(0x1C1C1E)
#define COL_TEXT      lv_color_hex(0xF5F5F5)
#define COL_MUTED     lv_color_hex(0x9A9A9A)
#define COL_SOLAR     lv_color_hex(0xF5C542)
#define COL_PW        lv_color_hex(0x2EE56B)
#define COL_GRID      lv_color_hex(0xA8A8A8)
#define COL_HOME      lv_color_hex(0xFFFFFF)
#define COL_HOME_BLUE lv_color_hex(0x4C9BFF)
#define COL_SOLAR_AREA lv_color_hex(0x8E8E8E)
#define COL_STATUS    lv_color_hex(0x5B9DFF)
#define COL_DANGER    lv_color_hex(0xFF5A5A)
#define COL_LINE      lv_color_hex(0x2A2A2A)

static inline void ui_style_screen(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, COL_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static inline lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}
