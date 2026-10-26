#pragma once
/* ESP-IDF error values needed by retained portable screens and the virtual NVS. */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102

static inline const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    default: return "ESP_FAIL";
    }
}
