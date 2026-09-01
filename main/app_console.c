#include "app_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_lte.h"
#include "app_net.h"
#include "app_wifi.h"
#include "esp_console.h"
#include "esp_log.h"
#include "tesla_client.h"
#include "weather_client.h"

static const char *TAG = "console";

static int cmd_show(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    app_config_t c;
    app_config_get(&c);
    printf("ssid=%s\n", c.wifi_ssid);
    printf("host=%s\n", c.api_host);
    printf("site_id=%s\n", c.site_id);
    printf("site_name=%s\n", c.site_name);
    printf("gps=%s,%s%s\n", c.gps_lat, c.gps_lon, app_config_has_gps() ? "" : " (non valido)");
    printf("poll_s=%u\n", c.poll_s);
    printf("wx_poll_min=%u\n", c.wx_poll_min);
    printf("token=%s\n", c.api_token[0] ? "(impostato)" : "(vuoto)");
    printf("apn=%s pin=%s\n", c.lte_apn[0] ? c.lte_apn : "(vuoto)", c.lte_pin[0] ? "(impostato)" : "(vuoto)");
    printf("net=%s ip=%s wifi=%s lte=%s\n", app_net_kind_name(), app_net_ip(),
           app_wifi_is_connected() ? "up" : "down", app_lte_is_connected() ? "up" : "down");
    return 0;
}

static int cmd_token(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: token <valore>\n");
        return 1;
    }
    app_config_set_token(argv[1]);
    app_config_save();
    printf("token salvato (%u caratteri)\n", (unsigned)strlen(argv[1]));
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: wifi <ssid> [password]\n");
        return 1;
    }
    const char *pass = argc > 2 ? argv[2] : "";
    app_config_set_wifi(argv[1], pass);
    app_config_save();
    printf("WiFi salvato, connessione...\n");
    esp_err_t err = app_wifi_connect(argv[1], pass);
    printf("%s\n", err == ESP_OK ? "connesso" : "connessione fallita");
    return err == ESP_OK ? 0 : 1;
}

static int cmd_host(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: host <url>\n");
        return 1;
    }
    app_config_set_host(argv[1]);
    app_config_save();
    printf("host salvato\n");
    return 0;
}

static int cmd_site(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: site <energy_site_id>\n");
        return 1;
    }
    app_config_set_site_id(argv[1]);
    app_config_save();
    printf("site_id salvato\n");
    return 0;
}

static int cmd_gps(int argc, char **argv)
{
    if (argc == 1) {
        app_config_t c;
        app_config_get(&c);
        float lat = 0, lon = 0;
        if (app_config_parse_gps(&lat, &lon)) {
            printf("gps=%.5f,%.5f\n", lat, lon);
        } else {
            printf("gps non impostato (uso: gps <lat> <lon>)\n");
        }
        return 0;
    }
    if (argc < 3) {
        printf("uso: gps <latitudine> <longitudine>\n");
        return 1;
    }
    app_config_set_gps(argv[1], argv[2]);
    if (!app_config_has_gps()) {
        printf("coordinate non valide (lat -90..90, lon -180..180)\n");
        return 1;
    }
    app_config_save();
    weather_client_kick();
    printf("gps salvato\n");
    return 0;
}

static int cmd_apn(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: apn <nome> [pin_sim]\n");
        return 1;
    }
    app_config_t c;
    app_config_get(&c);
    app_config_set_lte(argv[1], argc > 2 ? argv[2] : c.lte_pin, c.lte_user, c.lte_pass);
    app_config_save();
    printf("APN salvato, connessione 4G...\n");
    esp_err_t err = app_lte_connect();
    printf("%s %s\n", err == ESP_OK ? "4G ok" : "4G fallito", app_lte_last_error());
    return err == ESP_OK ? 0 : 1;
}

static int cmd_poll(int argc, char **argv)
{
    app_config_t c;
    app_config_get(&c);
    if (argc < 2) {
        printf("poll_s=%u  (uso: poll <secondi 5-300>)\n", c.poll_s);
        return 0;
    }
    app_config_set_polls((uint16_t)atoi(argv[1]), c.wx_poll_min);
    app_config_save();
    app_config_get(&c);
    printf("poll_s=%u\n", c.poll_s);
    return 0;
}

static int cmd_wxpoll(int argc, char **argv)
{
    app_config_t c;
    app_config_get(&c);
    if (argc < 2) {
        printf("wx_poll_min=%u  (uso: wxpoll <minuti 1-120>)\n", c.wx_poll_min);
        return 0;
    }
    app_config_set_polls(c.poll_s, (uint16_t)atoi(argv[1]));
    app_config_save();
    app_config_get(&c);
    printf("wx_poll_min=%u\n", c.wx_poll_min);
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char msg[96];
    tesla_client_test(msg, sizeof(msg));
    printf("%s\n", msg);
    return 0;
}

esp_err_t app_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "pw>";
    repl_config.max_cmdline_length = 1024;

    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        {.command = "show", .help = "Mostra configurazione (token mascherato)", .hint = NULL, .func = &cmd_show},
        {.command = "token", .help = "Salva il token API in NVS", .hint = "<valore>", .func = &cmd_token},
        {.command = "wifi", .help = "Salva SSID/password e connetti", .hint = "<ssid> [password]", .func = &cmd_wifi},
        {.command = "host", .help = "Imposta host API", .hint = "<url>", .func = &cmd_host},
        {.command = "site", .help = "Imposta energy_site_id", .hint = "<id>", .func = &cmd_site},
        {.command = "gps", .help = "Imposta coordinate GPS per il meteo", .hint = "<lat> <lon>", .func = &cmd_gps},
        {.command = "apn", .help = "Salva APN 4G e tenta la connessione LTE", .hint = "<apn> [pin]", .func = &cmd_apn},
        {.command = "poll", .help = "Intervallo poll TeslaMate in secondi", .hint = "[secondi]", .func = &cmd_poll},
        {.command = "wxpoll", .help = "Intervallo aggiornamento meteo in minuti", .hint = "[minuti]", .func = &cmd_wxpoll},
        {.command = "test", .help = "Chiama GET /api/1/products", .hint = NULL, .func = &cmd_test},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }

    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart, &repl_config, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UART REPL non disponibile (%s) — usa http://IP/", esp_err_to_name(err));
        return err;
    }
    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_console_start_repl: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "console ready (token / wifi / show / test)");
    return ESP_OK;
}
