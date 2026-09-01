#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENERGY_DAY_MAX_POINTS   96
#define ENERGY_PERIOD_MAX_POINTS 31
#define ENERGY_READ_LOG         15

typedef struct {
    float solar_w;
    float home_w;
    float battery_w;
    float grid_w;
    float soc_pct;
    float energy_left_wh;
    bool grid_active;
    bool storm;
    bool valid;
    char status[32];
    char site_name[64];
    char updated[24];
} energy_live_t;

typedef struct {
    char time[10];
    bool ok;
    float solar_w;
    float home_w;
    float battery_w;
    float grid_w;
    float soc_pct;
    char note[88];
} energy_read_log_t;

typedef struct {
    int count;
    float solar_w[ENERGY_DAY_MAX_POINTS];
    float home_w[ENERGY_DAY_MAX_POINTS];
    float battery_w[ENERGY_DAY_MAX_POINTS];
    float grid_w[ENERGY_DAY_MAX_POINTS];
    bool valid;
} energy_day_t;

typedef struct {
    int count;
    char labels[ENERGY_PERIOD_MAX_POINTS][8];
    float solar_kwh[ENERGY_PERIOD_MAX_POINTS];
    float to_home_kwh[ENERGY_PERIOD_MAX_POINTS];
    float to_battery_kwh[ENERGY_PERIOD_MAX_POINTS];
    float to_grid_kwh[ENERGY_PERIOD_MAX_POINTS];
    float from_solar_kwh[ENERGY_PERIOD_MAX_POINTS];
    float from_battery_kwh[ENERGY_PERIOD_MAX_POINTS];
    float from_grid_kwh[ENERGY_PERIOD_MAX_POINTS];
    float solar_gen_kwh;
    float solar_to_home_kwh;
    float solar_to_battery_kwh;
    float solar_to_grid_kwh;
    float home_kwh;
    float home_from_solar_kwh;
    float home_from_battery_kwh;
    float home_from_grid_kwh;
    bool valid;
} energy_period_t;

typedef struct {
    energy_live_t live;
    energy_day_t day;
    energy_period_t today;
    energy_period_t week;
    energy_period_t month;
    char last_error[128];
    bool fetching;
    energy_read_log_t reads[ENERGY_READ_LOG];
    uint8_t read_n;
} energy_state_t;

void energy_model_init(void);
void energy_model_get(energy_state_t *out);
void energy_model_set_live(const energy_live_t *live);
void energy_model_set_day(const energy_day_t *day);
void energy_model_set_today(const energy_period_t *today);
void energy_model_set_week(const energy_period_t *week);
void energy_model_set_month(const energy_period_t *month);
void energy_model_set_error(const char *err);
void energy_model_set_fetching(bool fetching);
void energy_model_set_status(const char *status);
void energy_model_log_event(const char *note);
void energy_model_format_reads(char *buf, size_t n);
void energy_model_derive_status(energy_live_t *live);
void energy_model_apply_lang(void);
bool energy_model_live_valid(void);
void energy_fmt_kw(char *buf, size_t n, float watts);
void energy_fmt_kwh(char *buf, size_t n, float kwh);
