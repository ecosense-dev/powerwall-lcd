#include "app_tz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "tz";
#define NVS_NS "pw"
#define DEFAULT_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"

#include "tz_db.inc"

static char s_iana[48];
static char s_posix[64];

static const char *lookup_posix(const char *iana)
{
    if (!iana || !iana[0]) {
        return NULL;
    }
    const char *p = s_tz_db;
    while (*p) {
        const char *name = p;
        p += strlen(p) + 1;
        const char *posix = p;
        p += strlen(p) + 1;
        if (strcmp(name, iana) == 0) {
            return posix;
        }
    }
    return NULL;
}

static void apply_posix(const char *posix)
{
    if (!posix || !posix[0]) {
        return;
    }
    if (strcmp(s_posix, posix) == 0) {
        return;
    }
    strlcpy(s_posix, posix, sizeof(s_posix));
    setenv("TZ", s_posix, 1);
    tzset();
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "tz_iana", s_iana);
    nvs_set_str(h, "tz_posix", s_posix);
    nvs_commit(h);
    nvs_close(h);
}

void app_tz_init(void)
{
    s_iana[0] = '\0';
    s_posix[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_iana);
        if (nvs_get_str(h, "tz_iana", s_iana, &n) != ESP_OK) {
            s_iana[0] = '\0';
        }
        n = sizeof(s_posix);
        if (nvs_get_str(h, "tz_posix", s_posix, &n) != ESP_OK) {
            s_posix[0] = '\0';
        }
        nvs_close(h);
    }

    if (!s_posix[0]) {
        strlcpy(s_posix, DEFAULT_POSIX, sizeof(s_posix));
        if (!s_iana[0]) {
            strlcpy(s_iana, "Europe/Rome", sizeof(s_iana));
        }
    }
    setenv("TZ", s_posix, 1);
    tzset();
    ESP_LOGI(TAG, "TZ %s (%s)", s_iana[0] ? s_iana : "?", s_posix);
}

bool app_tz_apply_iana(const char *iana)
{
    if (!iana || !iana[0]) {
        return false;
    }
    if (strcmp(s_iana, iana) == 0 && s_posix[0]) {
        apply_posix(s_posix);
        return true;
    }
    const char *posix = lookup_posix(iana);
    if (!posix) {
        ESP_LOGW(TAG, "fuso sconosciuto: %s", iana);
        return false;
    }
    strlcpy(s_iana, iana, sizeof(s_iana));
    apply_posix(posix);
    nvs_save();
    ESP_LOGI(TAG, "TZ da GPS: %s -> %s", s_iana, s_posix);
    return true;
}

void app_tz_apply_offset(int utc_offset_sec)
{
    char posix[32];
    int sign = utc_offset_sec >= 0 ? 1 : -1;
    int tot = utc_offset_sec >= 0 ? utc_offset_sec : -utc_offset_sec;
    int h = tot / 3600;
    int m = (tot % 3600) / 60;
    /* POSIX sign is inverted vs ISO offset. */
    if (m) {
        snprintf(posix, sizeof(posix), "UTC%c%d:%02d", sign > 0 ? '-' : '+', h, m);
    } else if (h == 0) {
        strlcpy(posix, "UTC0", sizeof(posix));
    } else {
        snprintf(posix, sizeof(posix), "UTC%c%d", sign > 0 ? '-' : '+', h);
    }
    s_iana[0] = '\0';
    apply_posix(posix);
    ESP_LOGI(TAG, "TZ da offset %ds -> %s", utc_offset_sec, posix);
}

bool app_tz_time_ok(void)
{
    return time(NULL) > 1700000000;
}

void app_tz_format(char *time_buf, size_t time_len, char *date_buf, size_t date_len)
{
    static const char *wd[] = {"dom", "lun", "mar", "mer", "gio", "ven", "sab"};
    static const char *mo[] = {"gen", "feb", "mar", "apr", "mag", "giu",
                               "lug", "ago", "set", "ott", "nov", "dic"};
    if (time_buf && time_len) {
        time_buf[0] = '\0';
    }
    if (date_buf && date_len) {
        date_buf[0] = '\0';
    }
    if (!app_tz_time_ok()) {
        if (time_buf && time_len) {
            strlcpy(time_buf, "--:--", time_len);
        }
        if (date_buf && date_len) {
            strlcpy(date_buf, "attesa NTP", date_len);
        }
        return;
    }
    time_t t = time(NULL);
    struct tm tm;
    if (!localtime_r(&t, &tm)) {
        return;
    }
    if (time_buf && time_len) {
        strftime(time_buf, time_len, "%H:%M", &tm);
    }
    if (date_buf && date_len) {
        int w = tm.tm_wday;
        int m = tm.tm_mon;
        if (w < 0 || w > 6) {
            w = 0;
        }
        if (m < 0 || m > 11) {
            m = 0;
        }
        snprintf(date_buf, date_len, "%s %d %s", wd[w], tm.tm_mday, mo[m]);
    }
}
