idf_component_register(
    SRCS
        "app_main.c"
        "app_config.c"
        "app_wifi.c"
        "tesla_client.c"
        "energy_model.c"
        "app_console.c"
        "app_httpd.c"
        "app_dns.c"
        "weather_client.c"
        "app_tz.c"
        "app_net.c"
        "app_lte.c"
        "app_ota.c"
        "app_i18n.c"
        "ui/ui.c"
        "ui/ui_live.c"
        "ui/ui_icons.c"
        "ui/ui_energy.c"
        "ui/ui_settings.c"
        "bsp/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS
        "."
        "bsp"
        "ui"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        esp_http_server
        json
        mbedtls
        driver
        esp_lcd
        esp_timer
        console
        vfs
        lwip
        esp-tls
        app_update
        esp_app_format
)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
elseif("lvgl__lvgl" IN_LIST build_components)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
endif()

if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
