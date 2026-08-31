#include <stdio.h>

#include "app_config.h"
#include "app_console.h"
#include "app_httpd.h"
#include "app_wifi.h"
#include "energy_model.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "tesla_client.h"
#include "ui.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "app";
static TaskHandle_t s_energy;

static void ui_refresh_locked(void)
{
    ui_lock();
    ui_refresh();
    ui_unlock();
}

static void ip_ready_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));
    /* Bind 0.0.0.0; do not restart here — overlapping stop/start leaves a zombie :80. */
    app_httpd_start();
    ui_refresh_locked();
    ui_lock();
    char msg[96];
    snprintf(msg, sizeof(msg), "IP %s  —  http://%s", app_wifi_ip(), app_wifi_ip());
    ui_settings_show_message(msg, false);
    ui_show_wizard(false);
    ui_unlock();
    vTaskDelete(NULL);
}

static void on_got_ip(const char *ip)
{
    ESP_LOGI(TAG, "IP %s — http://%s/", ip, ip);
    static volatile bool busy;
    if (busy) {
        return;
    }
    busy = true;
    if (s_energy) {
        xTaskNotifyGive(s_energy);
    }
    if (xTaskCreate(ip_ready_task, "ip_ready", 8192, NULL, 5, NULL) != pdPASS) {
        busy = false;
    }
}

static void wifi_supervisor(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));

    while (1) {
        if (!app_wifi_is_connected()) {
            if (app_config_has_wifi()) {
                ESP_LOGI(TAG, "tentativo STA...");
                esp_err_t err = app_wifi_connect_saved();
                ESP_LOGI(TAG, "wifi: %s %s", esp_err_to_name(err), app_wifi_last_error());
            }
            if (!app_wifi_is_connected()) {
                app_wifi_start_ap();
                app_httpd_start();
                ui_refresh_locked();
                ui_lock();
                char msg[120];
                snprintf(msg, sizeof(msg), "AP %s  pass %s  http://%s", APP_WIFI_AP_SSID, APP_WIFI_AP_PASS,
                         APP_WIFI_AP_IP);
                ui_settings_show_message(msg, false);
                ui_unlock();
            }
        } else {
            ui_refresh_locked();
            ui_lock();
            ui_show_wizard(false);
            ui_unlock();
            vTaskDelay(pdMS_TO_TICKS(25000));
            if (app_wifi_is_connected() && app_wifi_ap_is_up()) {
                ESP_LOGI(TAG, "STA ok, spengo AP di configurazione");
                /* Stop httpd while APSTA is still up so the ctrl socket can shut it down. */
                app_httpd_stop();
                app_wifi_stop_ap();
                vTaskDelay(pdMS_TO_TICKS(400));
                app_httpd_start();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(app_wifi_is_connected() ? 30000 : 20000));
    }
}

static void energy_task(void *arg)
{
    (void)arg;
    int hist_div = 0;
    ESP_LOGI(TAG, "energy task avviato");
    while (1) {
        bool wifi = app_wifi_is_connected();
        bool token = app_config_has_token();
        ESP_LOGI(TAG, "poll tick wifi=%d token=%d", (int)wifi, (int)token);

        if (wifi && token) {
            energy_model_log_event("chiamo live_status...");
            energy_model_set_status("Lettura in corso...");
            energy_model_set_fetching(true);
            ui_refresh_locked();
            tesla_client_fetch_live();
            energy_model_set_fetching(false);
            ui_refresh_locked();
            if (hist_div == 0) {
                ESP_LOGI(TAG, "poll history");
                tesla_client_fetch_history();
                ui_refresh_locked();
            }
            hist_div = (hist_div + 1) % 15;
        } else {
            if (!wifi) {
                energy_model_log_event("in attesa del WiFi");
            } else {
                energy_model_log_event("token mancante");
            }
            ui_refresh_locked();
        }

        uint32_t ms = 4000;
        if (wifi && token) {
            app_config_t cfg;
            app_config_get(&cfg);
            energy_state_t live_st;
            energy_model_get(&live_st);
            if (live_st.live.valid && cfg.poll_s >= 5) {
                ms = cfg.poll_s * 1000;
            } else if (live_st.live.valid) {
                ms = 20000;
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Powerwall dashboard POC");
    ESP_ERROR_CHECK(app_config_init());
    energy_model_init();
    ESP_ERROR_CHECK(app_wifi_init());

    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(tear_mode, rotation, &panel_handle, &touch_handle));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle, NULL, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, rotation);
    disp_config.profile.use_psram = true;

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
    if (disp == NULL) {
        ESP_LOGE(TAG, "display register failed");
        return;
    }

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        if (touch == NULL) {
            ESP_LOGW(TAG, "touch register failed");
        }
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ui_lock();
    ui_init();
    ui_unlock();

    app_wifi_set_ip_callback(on_got_ip);
    /* Console before httpd: REPL task needs internal RAM; httpd must not abort boot. */
    app_console_start();
    app_httpd_start();
    xTaskCreate(wifi_supervisor, "wifi_sup", 10240, NULL, 5, NULL);
    BaseType_t ok = xTaskCreateWithCaps(energy_task, "energy", 32768, NULL, 4, &s_energy,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "energy stack PSRAM fallito, riprovo in RAM");
        ok = xTaskCreate(energy_task, "energy", 12288, NULL, 4, &s_energy);
    }
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "energy task NON creato");
        s_energy = NULL;
    }
}
