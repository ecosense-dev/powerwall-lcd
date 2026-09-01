#include "app_lte.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_net.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "app_lte";

#define LTE_GOT_IP_BIT  BIT0
#define LTE_LOST_IP_BIT BIT1

static SemaphoreHandle_t s_mu;
static EventGroupHandle_t s_eg;
static esp_modem_dce_t *s_dce;
static esp_netif_t *s_netif;
static bool s_connected;
static bool s_busy;
static int64_t s_retry_after_us;
static char s_ip[16];
static char s_last_err[80];

static void set_err(const char *msg)
{
    strlcpy(s_last_err, msg ? msg : "", sizeof(s_last_err));
}

static void backoff(int sec)
{
    s_retry_after_us = esp_timer_get_time() + (int64_t)sec * 1000000;
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_last_err[0] = '\0';
        ESP_LOGI(TAG, "PPP IP %s", s_ip);

        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
        esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns);
        dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
        esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);

        esp_netif_set_default_netif(event->esp_netif);
        if (s_eg) {
            xEventGroupClearBits(s_eg, LTE_LOST_IP_BIT);
            xEventGroupSetBits(s_eg, LTE_GOT_IP_BIT);
        }
        app_net_notify_got_ip(s_ip);
    } else if (id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGW(TAG, "PPP lost IP");
        s_connected = false;
        s_ip[0] = '\0';
        set_err("4G disconnesso");
        if (s_eg) {
            xEventGroupClearBits(s_eg, LTE_GOT_IP_BIT);
            xEventGroupSetBits(s_eg, LTE_LOST_IP_BIT);
        }
    }
}

static esp_err_t ensure_dce(const char *apn)
{
    if (s_dce) {
        return ESP_OK;
    }

    if (!s_netif) {
        esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
        s_netif = esp_netif_new(&netif_ppp_config);
        if (!s_netif) {
            set_err("PPP netif fallito");
            return ESP_FAIL;
        }
    }

    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.port_num = UART_NUM_1;
    dte_config.uart_config.tx_io_num = APP_LTE_TX_GPIO;
    dte_config.uart_config.rx_io_num = APP_LTE_RX_GPIO;
    dte_config.uart_config.rts_io_num = UART_PIN_NO_CHANGE;
    dte_config.uart_config.cts_io_num = UART_PIN_NO_CHANGE;
    dte_config.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE;
    dte_config.uart_config.baud_rate = 115200;
    dte_config.uart_config.rx_buffer_size = 4096;
    dte_config.uart_config.tx_buffer_size = 512;
    dte_config.uart_config.event_queue_size = 30;
    dte_config.task_stack_size = 4096;
    dte_config.dte_buffer_size = 1024;

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(apn);
    /* A7670E AT set is close to SIM7600; avoid CMUX (A7670 exit is buggy). */
    s_dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, s_netif);
    if (!s_dce) {
        set_err("modem UART init fallito");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "A7670E UART1 TX=%d RX=%d APN=%s", APP_LTE_TX_GPIO, APP_LTE_RX_GPIO, apn);
    return ESP_OK;
}

esp_err_t app_lte_init(void)
{
    if (!s_mu) {
        s_mu = xSemaphoreCreateMutex();
    }
    if (!s_eg) {
        s_eg = xEventGroupCreate();
    }
    static bool handlers;
    if (!handlers) {
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
        handlers = true;
    }
    return ESP_OK;
}

esp_err_t app_lte_connect(void)
{
    if (!app_config_has_lte()) {
        set_err("APN non impostato");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_connected) {
        return ESP_OK;
    }

    app_config_t cfg;
    app_config_get(&cfg);

    xSemaphoreTake(s_mu, portMAX_DELAY);

    if (s_connected) {
        xSemaphoreGive(s_mu);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "connessione 4G APN=%s", cfg.lte_apn);

    esp_err_t err = ensure_dce(cfg.lte_apn);
    if (err != ESP_OK) {
        backoff(180);
        xSemaphoreGive(s_mu);
        return err;
    }

    if (cfg.lte_user[0] || cfg.lte_pass[0]) {
        err = esp_netif_ppp_set_auth(s_netif, NETIF_PPP_AUTHTYPE_PAP, cfg.lte_user, cfg.lte_pass);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "PPP auth: %s", esp_err_to_name(err));
        }
    }

    err = esp_modem_sync(s_dce);
    if (err != ESP_OK) {
        set_err("modem 4G non risponde (cavo/SIM)");
        ESP_LOGW(TAG, "AT sync fallito, skip 4G");
        backoff(180);
        xSemaphoreGive(s_mu);
        return err;
    }

    err = esp_modem_set_apn(s_dce, cfg.lte_apn);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_apn: %s", esp_err_to_name(err));
    }

    bool pin_ok = true;
    if (esp_modem_read_pin(s_dce, &pin_ok) == ESP_OK && !pin_ok) {
        if (!cfg.lte_pin[0]) {
            set_err("SIM richiede PIN");
            backoff(180);
            xSemaphoreGive(s_mu);
            return ESP_FAIL;
        }
        if (esp_modem_set_pin(s_dce, cfg.lte_pin) != ESP_OK) {
            set_err("PIN SIM errato");
            backoff(180);
            xSemaphoreGive(s_mu);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    int rssi = 0, ber = 0;
    if (esp_modem_get_signal_quality(s_dce, &rssi, &ber) == ESP_OK) {
        ESP_LOGI(TAG, "segnale rssi=%d ber=%d", rssi, ber);
    }

    xEventGroupClearBits(s_eg, LTE_GOT_IP_BIT | LTE_LOST_IP_BIT);
    err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK) {
        set_err("PPP data mode fallito (cavo/SIM/APN?)");
        ESP_LOGE(TAG, "set_mode DATA: %s", esp_err_to_name(err));
        backoff(120);
        xSemaphoreGive(s_mu);
        return err;
    }
    xSemaphoreGive(s_mu);

    EventBits_t bits = xEventGroupWaitBits(s_eg, LTE_GOT_IP_BIT | LTE_LOST_IP_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(25000));
    if (!(bits & LTE_GOT_IP_BIT)) {
        set_err("4G: niente IP (rete/APN)");
        backoff(120);
        xSemaphoreTake(s_mu, portMAX_DELAY);
        if (s_dce) {
            esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
        }
        xSemaphoreGive(s_mu);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void lte_job(void *arg)
{
    (void)arg;
    app_lte_connect();
    s_busy = false;
    vTaskDelete(NULL);
}

void app_lte_request(void)
{
    if (!app_config_has_lte() || s_connected || s_busy) {
        return;
    }
    if (esp_timer_get_time() < s_retry_after_us) {
        return;
    }
    s_busy = true;
    if (xTaskCreate(lte_job, "lte", 12288, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        ESP_LOGW(TAG, "task 4G non creato");
    }
}

esp_err_t app_lte_disconnect(void)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    if (s_dce && (s_connected || s_ip[0])) {
        ESP_LOGI(TAG, "stop PPP, torno in command mode");
        esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
    }
    s_connected = false;
    s_ip[0] = '\0';
    xSemaphoreGive(s_mu);
    return ESP_OK;
}

bool app_lte_is_connected(void)
{
    return s_connected;
}

const char *app_lte_ip(void)
{
    return s_ip;
}

const char *app_lte_last_error(void)
{
    return s_last_err;
}
