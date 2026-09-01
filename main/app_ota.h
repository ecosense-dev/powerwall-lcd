#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/* Streams a .bin (raw or multipart form) into the next OTA slot, then reboots. */
esp_err_t app_ota_httpd_post(httpd_req_t *req);
const char *app_ota_version(void);
