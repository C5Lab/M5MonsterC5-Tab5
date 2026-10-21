#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "test error"; }
