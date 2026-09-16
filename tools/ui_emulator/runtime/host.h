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
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include "lvgl.h"
#include "pcap_reader.h"
#include "pcap_summary.h"
#include "pcap_flow.h"
#include "pcap_analysis_store.h"
#include "pcap_investigation.h"
#include "pcap_extract.h"
#include "cgw_parser.h"
#include <emscripten/emscripten.h>
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
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1
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
/* Display access is serialized on the browser main thread. */
#define bsp_display_lock(...) 1
#define bsp_display_unlock(...) ((void)0)
static void emu_unsupported(const char *name);
static BaseType_t xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle);
static TickType_t xTaskGetTickCount(void);
static int64_t esp_timer_get_time(void);
static void vTaskDelay(TickType_t ticks);
static void vTaskDelete(TaskHandle_t task);
/* Software timers exist so retained popup code keeps its original shape. The
   browser owns the clock, so these hold the callback and never fire by themselves. */
static TimerHandle_t xTimerCreate(const char *name,TickType_t period,BaseType_t reload,void *id,void (*cb)(TimerHandle_t));
static BaseType_t xTimerStop(TimerHandle_t timer,TickType_t wait);
static void vTimerSetTimerID(TimerHandle_t timer,void *id);
static int uart_flush(uart_port_t port);
#define uart_flush_input(port) ((void)(port),ESP_OK)
static int uart_write_bytes(uart_port_t port,const void *bytes,size_t count);
static void esp_restart(void);
#define NVS_READONLY 0
#define NVS_READWRITE 1
static int nvs_open(const char *,int,nvs_handle_t *);
static int nvs_set_u8(nvs_handle_t,const char *,uint8_t);
static int nvs_commit(nvs_handle_t);
static void nvs_close(nvs_handle_t);

typedef struct {int line;const char *id;} app_template_t;
static lv_event_dsc_t *app_bind(lv_obj_t *,lv_event_cb_t,lv_event_code_t,void *,int);
#define lv_obj_add_event_cb(o,c,e,u) app_bind(o,c,e,u,__LINE__)

struct subghz_tab_state;
static void subghz_hide_all_pages(struct subghz_tab_state *state) {if(state)emu_unsupported("SubGHz is deferred");}
static void show_subghz_page(void) {emu_unsupported("SubGHz is deferred");}
