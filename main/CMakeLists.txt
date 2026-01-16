idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl m5stack_tab5 nvs_flash esp_lvgl_port driver esp_netif esp_event esp_wifi espressif__esp_hosted esp_http_server)
