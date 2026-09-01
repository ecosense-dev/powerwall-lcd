#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Waveshare ESP32-S3-Touch-LCD-7: UART1 on the RS485 MCU pins.
 * A7670E TTL: ESP TX GPIO15 -> modem RX, ESP RX GPIO16 <- modem TX.
 * PWRKEY optional on Sensor AD GPIO6. Console stays on UART0 (USB-UART). */
#define APP_LTE_TX_GPIO     15
#define APP_LTE_RX_GPIO     16
#define APP_LTE_PWRKEY_GPIO 6

esp_err_t app_lte_init(void);
esp_err_t app_lte_connect(void);
/* Non-blocking: starts LTE only if APN is set. No-op without APN/modem. */
void app_lte_request(void);
esp_err_t app_lte_disconnect(void);
bool app_lte_is_connected(void);
const char *app_lte_ip(void);
const char *app_lte_last_error(void);
