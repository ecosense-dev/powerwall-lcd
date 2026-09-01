#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define WEATHER_DAYS  3
#define WEATHER_SLOTS 3

typedef struct {
    bool valid;
    float temp_c;
    int wmo_code;
    char condition[24];
} weather_slot_t;

typedef struct {
    char title[16];
    weather_slot_t slot[WEATHER_SLOTS];
} weather_day_t;

typedef struct {
    bool valid;
    float temp_c;
    int wmo_code;
    char condition[24];
    weather_day_t day[WEATHER_DAYS];
} weather_t;

const char *weather_wmo_text(int code);

void weather_client_init(void);
void weather_client_get(weather_t *out);
void weather_client_invalidate(void);
void weather_client_request(void);
void weather_client_kick(void);
