#include "ui.h"

#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "app_i18n.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "ui_theme.h"
#include "weather_client.h"

static const char *TAG = "ui";

#define SETTINGS_TAB_IDX 2
#define PIN_MAX_LEN      4

static lv_obj_t *s_tv;
static lv_obj_t *s_wizard;
static lv_obj_t *s_kb;
static lv_obj_t *s_kb_target;
static lv_obj_t *s_pin_gate;
static lv_obj_t *s_pin_dots;
static lv_obj_t *s_pin_err;
static lv_obj_t *s_pin_title;
static lv_obj_t *s_pin_sub;
static const char *s_tab_map[4];
static char s_pin[PIN_MAX_LEN + 1];
static bool s_settings_unlocked;

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
        lv_keyboard_set_mode(s_kb, lv_obj_get_user_data(ta) ? LV_KEYBOARD_MODE_NUMBER
                                                            : LV_KEYBOARD_MODE_TEXT_LOWER);
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

void ui_attach_textarea_num(lv_obj_t *ta)
{
    lv_obj_set_user_data(ta, (void *)1);
    ui_attach_textarea(ta);
}

static void pin_hide_kb(void)
{
    if (s_kb) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        s_kb_target = NULL;
    }
}

static void pin_update_dots(void)
{
    char dots[8];
    size_t n = strlen(s_pin);
    for (size_t i = 0; i < PIN_MAX_LEN; i++) {
        dots[i] = i < n ? '*' : '-';
    }
    dots[PIN_MAX_LEN] = '\0';
    lv_label_set_text(s_pin_dots, dots);
}

static void pin_reset(void)
{
    s_pin[0] = '\0';
    if (s_pin_dots) {
        pin_update_dots();
    }
    if (s_pin_err) {
        lv_label_set_text(s_pin_err, "");
    }
}

static void pin_show_gate(bool show)
{
    if (!s_pin_gate) {
        return;
    }
    if (show) {
        pin_hide_kb();
        pin_reset();
        lv_obj_clear_flag(s_pin_gate, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_pin_gate);
    } else {
        lv_obj_add_flag(s_pin_gate, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool wizard_visible(void)
{
    return s_wizard && !lv_obj_has_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
}

static void pin_sync_to_tab(void)
{
    if (!s_tv || wizard_visible()) {
        pin_show_gate(false);
        return;
    }
    uint16_t tab = lv_tabview_get_tab_act(s_tv);
    if (tab == SETTINGS_TAB_IDX) {
        if (!s_settings_unlocked) {
            pin_show_gate(true);
        } else {
            pin_show_gate(false);
        }
    } else {
        s_settings_unlocked = false;
        pin_show_gate(false);
    }
}

static void pin_try(void)
{
    if (strcmp(s_pin, APP_SETTINGS_PIN) == 0) {
        s_settings_unlocked = true;
        pin_show_gate(false);
        return;
    }
    pin_reset();
    lv_label_set_text(s_pin_err, app_tr(STR_PIN_WRONG));
    lv_obj_set_style_text_color(s_pin_err, COL_DANGER, 0);
}

static void pin_digit_cb(lv_event_t *e)
{
    char d = (char)(intptr_t)lv_event_get_user_data(e);
    size_t n = strlen(s_pin);
    if (n >= PIN_MAX_LEN) {
        return;
    }
    s_pin[n] = d;
    s_pin[n + 1] = '\0';
    pin_update_dots();
    lv_label_set_text(s_pin_err, "");
    if (n + 1 == PIN_MAX_LEN) {
        pin_try();
    }
}

static void pin_clear_cb(lv_event_t *e)
{
    (void)e;
    pin_reset();
}

static void pin_ok_cb(lv_event_t *e)
{
    (void)e;
    if (strlen(s_pin) == PIN_MAX_LEN) {
        pin_try();
    } else {
        lv_label_set_text(s_pin_err, app_tr(STR_PIN_DIGITS));
        lv_obj_set_style_text_color(s_pin_err, COL_MUTED, 0);
    }
}

static lv_obj_t *make_pin_key(lv_obj_t *parent, const char *txt, lv_coord_t x, lv_coord_t y,
                              lv_event_cb_t cb, void *user, lv_color_t bg, lv_color_t fg)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 88, 52);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = ui_label(btn, &lv_font_montserrat_24, fg);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return btn;
}

static void pin_create(lv_obj_t *scr)
{
    lv_coord_t h = lv_disp_get_ver_res(NULL) - 56;
    s_pin_gate = lv_obj_create(scr);
    lv_obj_set_size(s_pin_gate, LV_PCT(100), h);
    lv_obj_set_pos(s_pin_gate, 0, 0);
    ui_style_screen(s_pin_gate);
    lv_obj_set_style_bg_opa(s_pin_gate, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_pin_gate, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pin_gate, LV_OBJ_FLAG_CLICKABLE);

    s_pin_title = ui_label(s_pin_gate, &lv_font_montserrat_24, COL_TEXT);
    lv_label_set_text(s_pin_title, app_tr(STR_PIN_TITLE));
    lv_obj_align(s_pin_title, LV_ALIGN_TOP_MID, 0, 16);

    s_pin_sub = ui_label(s_pin_gate, &lv_font_montserrat_14, COL_MUTED);
    lv_label_set_text(s_pin_sub, app_tr(STR_PIN_ENTER));
    lv_obj_align(s_pin_sub, LV_ALIGN_TOP_MID, 0, 50);

    s_pin_dots = ui_label(s_pin_gate, &lv_font_montserrat_28, COL_TEXT);
    lv_label_set_text(s_pin_dots, "----");
    lv_obj_align(s_pin_dots, LV_ALIGN_TOP_MID, 0, 84);

    s_pin_err = ui_label(s_pin_gate, &lv_font_montserrat_14, COL_DANGER);
    lv_label_set_text(s_pin_err, "");
    lv_obj_align(s_pin_err, LV_ALIGN_TOP_MID, 0, 124);

    const lv_coord_t ox = 268;
    const lv_coord_t oy = 156;
    const lv_coord_t dx = 100;
    const lv_coord_t dy = 64;
    static const char *keys[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 9; i++) {
        make_pin_key(s_pin_gate, keys[i], ox + (i % 3) * dx, oy + (i / 3) * dy, pin_digit_cb,
                     (void *)(intptr_t)keys[i][0], COL_ELEV, COL_TEXT);
    }
    make_pin_key(s_pin_gate, "C", ox, oy + 3 * dy, pin_clear_cb, NULL, COL_CARD, COL_MUTED);
    make_pin_key(s_pin_gate, "0", ox + dx, oy + 3 * dy, pin_digit_cb, (void *)(intptr_t)'0', COL_ELEV,
                 COL_TEXT);
    make_pin_key(s_pin_gate, "OK", ox + 2 * dx, oy + 3 * dy, pin_ok_cb, NULL, COL_PW, COL_BG);
    lv_obj_add_flag(s_pin_gate, LV_OBJ_FLAG_HIDDEN);
}

static void on_tab_changed(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *content = lv_tabview_get_content(tv);
    uint16_t tab = lv_tabview_get_tab_act(tv);
    lv_obj_t *page = content ? lv_obj_get_child(content, tab) : NULL;
    if (page) {
        lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
    }
    pin_hide_kb();
    pin_sync_to_tab();
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
    lv_obj_set_style_anim_time(lv_tabview_get_content(s_tv), 0, 0);
    style_tabview(s_tv);

    lv_obj_t *tab_live = lv_tabview_add_tab(s_tv, app_tr(STR_TAB_LIVE));
    lv_obj_t *tab_en = lv_tabview_add_tab(s_tv, app_tr(STR_TAB_ENERGY));
    lv_obj_t *tab_set = lv_tabview_add_tab(s_tv, app_tr(STR_TAB_SETTINGS));
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
    lv_obj_add_event_cb(s_tv, on_tab_changed, LV_EVENT_VALUE_CHANGED, NULL);

    pin_create(scr);

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

void ui_apply_lang(void)
{
    energy_model_apply_lang();
    weather_client_apply_lang();
    if (s_tv) {
        s_tab_map[0] = app_tr(STR_TAB_LIVE);
        s_tab_map[1] = app_tr(STR_TAB_ENERGY);
        s_tab_map[2] = app_tr(STR_TAB_SETTINGS);
        s_tab_map[3] = "";
        lv_btnmatrix_set_map(lv_tabview_get_tab_btns(s_tv), s_tab_map);
    }
    if (s_pin_title) {
        lv_label_set_text(s_pin_title, app_tr(STR_PIN_TITLE));
    }
    if (s_pin_sub) {
        lv_label_set_text(s_pin_sub, app_tr(STR_PIN_ENTER));
    }
    ui_live_apply_lang();
    ui_energy_apply_lang();
    ui_settings_apply_lang();
}

void ui_show_wizard(bool show)
{
    if (!s_wizard) {
        return;
    }
    if (show) {
        pin_show_gate(false);
        lv_obj_clear_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_wizard);
    } else {
        lv_obj_add_flag(s_wizard, LV_OBJ_FLAG_HIDDEN);
        pin_sync_to_tab();
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
