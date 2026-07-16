idf_component_register(SRCS
                    "main.c"
                    "lv_font_lato_regular_ttf.c"
                    "screens/psram_dynarr.c"
                    "screens/subghz_parser.c"
                    "screens/subghz_signal_list.c"
                    "screens/subghz_rf_settings.c"
                    "screens/subghz_text_input_popup.c"
                    "screens/subghz_screen.c"
                    "screens/subghz_listen_screen.c"
                    "screens/subghz_listen_settings_screen.c"
                    "screens/subghz_manage_screen.c"
                    "screens/subghz_jammer_screen.c"
                    "screens/subghz_tesla_screen.c"
                    "screens/subghz_hunter_screen.c"
                    "screens/subghz_hunter_settings_screen.c"
                    "screens/subghz_scanner_screen.c"
                    "screens/subghz_scanner_settings_screen.c"
                    "screens/subghz_weather_screen.c"
                    "screens/subghz_settings_screen.c"
                    INCLUDE_DIRS "." "screens"
                    REQUIRES lvgl m5stack_tab5 nvs_flash esp_lvgl_port driver esp_netif esp_event esp_wifi espressif__esp_hosted esp_http_server fatfs)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
