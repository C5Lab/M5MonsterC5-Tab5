#pragma once
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "lvgl.h"
#include "pcap_reader.h"
#include "pcap_summary.h"
#include "pcap_flow.h"
#include "pcap_analysis_store.h"
#include "pcap_investigation.h"
#include "pcap_extract.h"
#include "cgw_parser.h"
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TimerHandle_t;
typedef void *EventGroupHandle_t;
typedef int uart_port_t;
typedef int esp_err_t;
typedef int gpio_num_t;
typedef void *esp_netif_t;
typedef void *i2c_master_dev_handle_t;
typedef void *esp_codec_dev_handle_t;
typedef void *httpd_handle_t;
typedef int portMUX_TYPE;
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define nvs_open(...) 0x1102
#define nvs_get_i8(...) 0x1102
#define nvs_get_u8(...) 0x1102
#define nvs_get_u16(...) 0x1102
#define nvs_set_i8(...) 0
#define nvs_set_u8(...) 0
#define nvs_set_u16(...) 0
#define nvs_commit(...) 0
#define nvs_close(...) ((void)0)
typedef void *usbh_cdc_handle_t;
typedef int wifi_auth_mode_t;
typedef struct { char ssid[33]; uint8_t bssid[6]; int rssi, primary, authmode; } wifi_ap_record_t;
typedef void (*TaskFunction_t)(void *);
#define IRAM_ATTR
#define DRAM_ATTR
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1
static unsigned xTaskGetTickCount(void) { static unsigned tick=1234000; return tick+=100; }
#define xTaskGetCurrentTaskHandle() NULL
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#define BSP_LCD_H_RES 720
#define BSP_LCD_V_RES 1280
#define heap_caps_malloc(n,c) malloc(n)
#define heap_caps_calloc(n,s,c) calloc(n,s)
#define heap_caps_realloc(p,n,c) realloc(p,n)
#define heap_caps_free(p) free(p)
#define xTaskCreate(...) pdPASS
#define xTaskCreatePinnedToCore(...) pdPASS
#define vTaskDelete(...) ((void)0)
#define vTaskDelay(...) ((void)0)
#define xSemaphoreTake(...) 1
#define xSemaphoreGive(...) 1
#define bsp_display_lock(...) 1
#define bsp_display_unlock(...) ((void)0)
#define taskENTER_CRITICAL(...) ((void)0)
#define taskEXIT_CRITICAL(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define xTimerStop(...) 1
#define xTimerCreate(...) NULL
#define vTimerSetTimerID(...) ((void)0)
#define uart_flush(...) 0
#define uart_flush_input(...) 0
#define usbh_cdc_flush_rx_buffer(...) 0
static int64_t esp_timer_get_time(void) { static int64_t now=1234000000LL; return now+=1000000; }
#include "subghz_host.h"
