#include "energy_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static energy_state_t s_state;
static SemaphoreHandle_t s_mu;

static void fill_stamp(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    if (t > 1700000000 && localtime_r(&t, &tm)) {
        strftime(buf, n, "%H:%M:%S", &tm);
        return;
    }
    snprintf(buf, n, "%lus", (unsigned long)(esp_timer_get_time() / 1000000ULL));
}

static void push_log(bool ok, const energy_live_t *live, const char *note)
{
    memmove(&s_state.reads[1], &s_state.reads[0], sizeof(s_state.reads[0]) * (ENERGY_READ_LOG - 1));
    energy_read_log_t *e = &s_state.reads[0];
    memset(e, 0, sizeof(*e));
    fill_stamp(e->time, sizeof(e->time));
    e->ok = ok;
    if (live) {
        e->solar_w = live->solar_w;
        e->home_w = live->home_w;
        e->battery_w = live->battery_w;
        e->grid_w = live->grid_w;
        e->soc_pct = live->soc_pct;
    }
    strlcpy(e->note, note ? note : "", sizeof(e->note));
    if (s_state.read_n < ENERGY_READ_LOG) {
        s_state.read_n++;
    }
}

void energy_model_init(void)
{
    s_mu = xSemaphoreCreateMutex();
    memset(&s_state, 0, sizeof(s_state));
    strlcpy(s_state.live.status, "In attesa", sizeof(s_state.live.status));
    energy_model_log_event("avvio, in attesa del poll");
}

void energy_model_get(energy_state_t *out)
{
    if (!out) {
        return;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mu);
}

void energy_model_set_live(const energy_live_t *live)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_state.live = *live;
    s_state.last_error[0] = '\0';
    push_log(true, live, live->status);
    xSemaphoreGive(s_mu);
}

void energy_model_set_day(const energy_day_t *day)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_state.day = *day;
    xSemaphoreGive(s_mu);
}

void energy_model_set_today(const energy_period_t *today)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_state.today = *today;
    xSemaphoreGive(s_mu);
}

void energy_model_set_week(const energy_period_t *week)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_state.week = *week;
    xSemaphoreGive(s_mu);
}

void energy_model_set_month(const energy_period_t *month)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_state.month = *month;
    xSemaphoreGive(s_mu);
}

void energy_model_set_error(const char *err)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    strlcpy(s_state.last_error, err ? err : "", sizeof(s_state.last_error));
    push_log(false, NULL, s_state.last_error);
    xSemaphoreGive(s_mu);
}

void energy_model_set_fetching(bool fetching)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_state.fetching = fetching;
    xSemaphoreGive(s_mu);
}

void energy_model_set_status(const char *status)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    strlcpy(s_state.live.status, status ? status : "", sizeof(s_state.live.status));
    xSemaphoreGive(s_mu);
}

void energy_model_log_event(const char *note)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    push_log(false, NULL, note);
    xSemaphoreGive(s_mu);
}

void energy_model_format_reads(char *buf, size_t n)
{
    if (!buf || n == 0) {
        return;
    }
    buf[0] = '\0';
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_state.read_n == 0) {
        xSemaphoreGive(s_mu);
        strlcpy(buf, "Nessuna lettura ancora.", n);
        return;
    }
    for (uint8_t i = 0; i < s_state.read_n && i < ENERGY_READ_LOG; i++) {
        const energy_read_log_t *e = &s_state.reads[i];
        char line[160];
        if (e->ok) {
            snprintf(line, sizeof(line), "%s  ok  sol %.1fkW  casa %.1fkW  batt %.1fkW  rete %.1fkW  %.0f%%\n",
                     e->time, e->solar_w / 1000.0f, e->home_w / 1000.0f, e->battery_w / 1000.0f,
                     e->grid_w / 1000.0f, e->soc_pct);
        } else {
            snprintf(line, sizeof(line), "%s  %s\n", e->time, e->note[0] ? e->note : "errore");
        }
        strlcat(buf, line, n);
    }
    xSemaphoreGive(s_mu);
}

void energy_model_derive_status(energy_live_t *live)
{
    if (!live) {
        return;
    }
    if (live->battery_w > 150.0f) {
        strlcpy(live->status, "In scarica", sizeof(live->status));
    } else if (live->battery_w < -150.0f) {
        strlcpy(live->status, "In carica", sizeof(live->status));
    } else if (live->solar_w > 200.0f && live->grid_w < 100.0f) {
        strlcpy(live->status, "Autoconsumo", sizeof(live->status));
    } else if (live->grid_w > 150.0f) {
        strlcpy(live->status, "Dalla rete", sizeof(live->status));
    } else {
        strlcpy(live->status, "Inattivo", sizeof(live->status));
    }
}

void energy_fmt_kw(char *buf, size_t n, float watts)
{
    float kw = watts / 1000.0f;
    if (!isfinite(kw)) {
        snprintf(buf, n, "--.- kW");
        return;
    }
    if (fabsf(kw) < 0.05f) {
        snprintf(buf, n, "0.0 kW");
        return;
    }
    snprintf(buf, n, "%.1f kW", kw);
}

void energy_fmt_kwh(char *buf, size_t n, float kwh)
{
    if (!isfinite(kwh)) {
        snprintf(buf, n, "--.- kWh");
        return;
    }
    snprintf(buf, n, "%.1f kWh", kwh);
}
