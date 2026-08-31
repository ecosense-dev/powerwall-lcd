#pragma once

#include "lvgl.h"

lv_obj_t *ui_icon_sun(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy);
lv_obj_t *ui_icon_house(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy);
lv_obj_t *ui_icon_pylon(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy);
lv_obj_t *ui_icon_battery(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy, lv_obj_t **fill_out);
void ui_icon_battery_set_soc(lv_obj_t *fill, float soc_pct);
