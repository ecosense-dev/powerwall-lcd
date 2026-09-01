#pragma once

#include "lvgl.h"

lv_obj_t *ui_icon_sun(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy);
lv_obj_t *ui_icon_house(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy);
lv_obj_t *ui_icon_pylon(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy);
lv_obj_t *ui_icon_battery(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy, lv_obj_t **fill_out);
void ui_icon_battery_set_soc(lv_obj_t *fill, float soc_pct);

lv_obj_t *ui_wx_icon_create(lv_obj_t *parent, lv_coord_t size);
void ui_wx_icon_set(lv_obj_t *icon, int wmo_code);
