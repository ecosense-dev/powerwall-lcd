#pragma once

#include <stdbool.h>
#include <stddef.h>

void app_tz_init(void);
bool app_tz_apply_iana(const char *iana);
void app_tz_apply_offset(int utc_offset_sec);
bool app_tz_time_ok(void);
void app_tz_format(char *time_buf, size_t time_len, char *date_buf, size_t date_len);
