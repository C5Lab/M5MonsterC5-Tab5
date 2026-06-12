
idf_component_register(
    SRCS "esp_lcd_st7121.c" "m5stack_tab5.c" "esp_lcd_st7123.c"
    INCLUDE_DIRS "include"
    INCLUDE_DIRS "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES 
        driver 
        esp_lcd
        esp_codec_dev
        esp_lvgl_port
	esp_lcd_ili9881c
    esp_lcd_touch_st7123
    PRIV_REQUIRES usb spiffs fatfs
)
