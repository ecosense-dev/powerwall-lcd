#include "ui.h"

#include "app_config.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "ui_theme.h"

static const char *TAG = "ui";

static lv_obj_t *s_tv;
static lv_obj_t *s_wizard;
static lv_obj_t *s_kb;
static lv_obj_t *s_kb_target;

void ui_lock(void)
{
    esp_lv_adapter_lock(-1);
}

void ui_unlock(void)
{
    esp_lv_adapter_unlock();
}

static void kb_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        s_kb_target = NULL;
    }
}

static void ta_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        s_kb_target = ta;
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (s_kb_target == ta) {
            lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
            s_kb_target = NULL;
        }
    }
}

void ui_attach_textarea(lv_obj_t *ta)
{
    lv_obj_add_event_cb(ta, ta_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, ta_event, LV_EVENT_DEFOCUSED, NULL);
}

static void style_tabview(lv_obj_t *tv)
{
    lv_obj_set_style_bg_color(tv, COL_BG, 0);
    lv_obj_t *btns = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(btns, COL_ELEV, 0);
    lv_obj_set_style_bg_opa(btns, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btns, COL_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btns, COL_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btns, COL_PW, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(btns, LV_BORDER_SIDE_TOP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(btns, 3, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(btns, &lv_font_montserrat_16, 0);
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    ui_style_screen(scr);
    lv_obj_set_style_text_color(scr, COL_TEXT, 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_16, 0);

    s_tv = lv_tabview_create(scr, LV_DIR_BOTTOM, 56);
    lv_obj_set_size(s_tv, LV_PCT(100), LV_PCT(100));
    style_tabview(s_tv);

    lv_obj_t *tab_live = lv_tabview_add_tab(s_tv, "Live");
    lv_obj_t *tab_en = lv_tabview_add_tab(s_tv, "Energia");
    lv_obj_t *tab_set = lv_tabview_add_tab(s_tv, "Impostazioni");
    ui_style_screen(tab_live);
    ui_style_screen(tab_en);
    ui_style_screen(tab_set);
    lv_obj_set_style_pad_all(tab_live, 0, 0);
    lv_obj_set_style_pad_all(tab_en, 8, 0);
    lv_obj_set_style_pad_all(tab_set, 8, 0);
    lv_obj_set_scrollbar_mode(tab_set, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(tab_set, LV_OBJ_FLAG_SCROLLABLE);

    ui_live_create(tab_live);
    ui_energy_create(tab_en);
    ui_settings_create(tab_set, false);

    s_wizard = lv_obj_create(scr);
    lv_obj_set_size(s_wizard, LV_PCT(100), LV_PCT(100));
    ui_style_screen(s_wizard);
    lv_obj_set_style_pad_all(s_wizard, 16, 0);
    lv_obj_add_flag(s_wizard, LV_OBJ_FLAG_SCROLLABLE);
    ui_settings_create(s_wizard, true);
    lv_obj_add_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);

    s_kb = lv_keyboard_create(scr);
    lv_obj_add_event_cb(s_kb, kb_event, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(s_kb, COL_ELEV, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    if (app_config_needs_wizard()) {
        ui_show_wizard(true);
    }

    ESP_LOGI(TAG, "UI ready");
}

void ui_show_wizard(bool show)
{
    if (!s_wizard) {
        return;
    }
    if (show) {
        lv_obj_clear_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_wizard);
    } else {
        lv_obj_add_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_refresh(void)
{
    /* Callers already hold the LVGL lock; keep the snapshot off task stacks. */
    static energy_state_t st;
    energy_model_get(&st);
    ui_live_update(&st);
    ui_energy_update(&st);
    ui_settings_on_wifi();
}
