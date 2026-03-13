idf_component_register(SRCS
                    "main.c"
                    "lv_font_lato_regular_ttf.c"
                    "../assets/intro/generated/intro_splash_img.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl m5stack_tab5 nvs_flash esp_lvgl_port driver esp_netif esp_event esp_wifi espressif__esp_hosted esp_http_server fatfs)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
