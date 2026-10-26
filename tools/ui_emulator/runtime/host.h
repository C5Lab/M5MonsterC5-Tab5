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
#include "app_keyboard.h"
#include "app_keyboard_navigation.h"
#include "app_keyboard_rotation.h"
#include "wifi_analyzer.h"
#include "wifi_analyzer_screen.h"
#include "tile_order.h"
#include "esp_err.h"
#include "tool_order_screen.h"
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
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
/* NFC's reply handoff runs on the browser thread. */
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t wait)
{ (void)lock; (void)wait; return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t lock)
{ (void)lock; return pdTRUE; }
/* Display access is serialized on the browser main thread. */
#define bsp_display_lock(...) 1
#define bsp_display_unlock(...) ((void)0)
static void emu_unsupported(const char *name);
BaseType_t xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle);
TickType_t xTaskGetTickCount(void);
int64_t esp_timer_get_time(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
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
int nvs_open(const char *,int,nvs_handle_t *);
int nvs_set_u8(nvs_handle_t,const char *,uint8_t);
int nvs_set_i8(nvs_handle_t,const char *,int8_t);
int nvs_set_u16(nvs_handle_t,const char *,uint16_t);
int nvs_get_u8(nvs_handle_t,const char *,uint8_t *);
int nvs_get_i8(nvs_handle_t,const char *,int8_t *);
int nvs_get_u16(nvs_handle_t,const char *,uint16_t *);
static int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
static int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
esp_err_t tile_order_load(tile_order_group_t, tile_order_t *);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);

typedef struct {int line;const char *id;} app_template_t;
static lv_event_dsc_t *app_bind(lv_obj_t *,lv_event_cb_t,lv_event_code_t,void *,int);
#if !defined(EMU_NATIVE_ANALYZER_SCREEN) && !defined(EMULATOR_NATIVE_SUBGHZ) && !defined(EMULATOR_NATIVE_NFC)
#define lv_obj_add_event_cb(o,c,e,u) app_bind(o,c,e,u,__LINE__)
#endif
#ifdef EMULATOR_NATIVE_SUBGHZ
static inline lv_event_dsc_t *subghz_emulator_bind_row(lv_obj_t *row, lv_event_cb_t callback)
{
    return lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, NULL);
}
#endif

struct subghz_tab_state;
struct nfc_tab_state;
void nfc_hide_all_pages(struct nfc_tab_state *state);
void show_nfc_page(void);
static bool app_power_manager_is_screen_dimmed(void) {return false;}
#include "subghz_host.h"
