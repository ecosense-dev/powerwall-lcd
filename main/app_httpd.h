#pragma once

#include "esp_err.h"

esp_err_t app_httpd_start(void);
esp_err_t app_httpd_restart(void);
void app_httpd_stop(void);
