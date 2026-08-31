#pragma once

#include "esp_err.h"

#include <stddef.h>

esp_err_t tesla_client_fetch_live(void);
esp_err_t tesla_client_fetch_history(void);
esp_err_t tesla_client_test(char *msg, size_t msg_len);
