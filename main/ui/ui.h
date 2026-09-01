#pragma once

#include <stdbool.h>

#include "energy_model.h"
#include "lvgl.h"

void ui_init(void);
void ui_refresh(void);
void ui_show_wizard(bool show);
void ui_lock(void);
void ui_unlock(void);

void ui_live_create(lv_obj_t *parent);
void ui_live_update(const energy_state_t *st);
void ui_live_apply_lang(void);

void ui_energy_create(lv_obj_t *parent);
void ui_energy_update(const energy_state_t *st);
void ui_energy_apply_lang(void);

void ui_settings_create(lv_obj_t *parent, bool wizard);
void ui_settings_set_scan_busy(bool busy);
void ui_settings_show_message(const char *msg, bool error);
void ui_settings_reload(void);
void ui_settings_on_wifi(void);
void ui_settings_apply_lang(void);
void ui_apply_lang(void);

void ui_attach_textarea(lv_obj_t *ta);
void ui_attach_textarea_num(lv_obj_t *ta);
