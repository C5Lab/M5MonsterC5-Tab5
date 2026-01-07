/*
 * M5Stack Tab5 WiFi Scanner via UART
 * Communicates with ESP32C5 over UART to scan WiFi networks
 * Also supports native WiFi scanning via ESP32C6 (SDIO)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "bsp/m5stack_tab5.h"
#include "lvgl.h"

// ESP-Hosted includes for WiFi via ESP32C6 SDIO
#include "esp_hosted.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "wifi_scanner";

// UART Configuration for ESP32C5 communication
#define UART_NUM          UART_NUM_1
#define UART_TX_PIN       GPIO_NUM_53//38
#define UART_RX_PIN       GPIO_NUM_54//37
#define UART_BAUD_RATE    115200
#define UART_BUF_SIZE     4096
#define UART_RX_TIMEOUT   30000  // 30 seconds timeout for scan

// ESP Modem configuration (configurable pins for future external ESP32C5)
#define ESP_MODEM_UART_TX_PIN  GPIO_NUM_38
#define ESP_MODEM_UART_RX_PIN  GPIO_NUM_37

// ESP Modem scan settings
#define ESP_MODEM_MAX_NETWORKS  50

// INA226 Power Monitor Configuration (for battery voltage)
#define INA226_I2C_ADDR         0x41    // M5Tab5 uses address 0x41 (not default 0x40!)
#define INA226_REG_CONFIG       0x00    // Configuration register
#define INA226_REG_SHUNT_VOLT   0x01    // Shunt voltage register
#define INA226_REG_BUS_VOLT     0x02    // Bus voltage register
#define INA226_REG_POWER        0x03    // Power register
#define INA226_REG_CURRENT      0x04    // Current register
#define INA226_REG_CALIB        0x05    // Calibration register
#define INA226_REG_MASK_EN      0x06    // Mask/Enable register
#define INA226_REG_ALERT_LIM    0x07    // Alert limit register
#define INA226_REG_MFG_ID       0xFE    // Manufacturer ID (0x5449)
#define INA226_REG_DIE_ID       0xFF    // Die ID (0x2260)
#define INA226_BUS_VOLT_LSB     1.25f   // 1.25mV per LSB for bus voltage
#define BATTERY_UPDATE_MS       2000    // Update battery status every 2 seconds

// INA226 Calibration (from M5Tab5 official demo)
#define INA226_SHUNT_RESISTANCE 0.005f  // 5 mOhm shunt resistor
#define INA226_MAX_CURRENT      8.192f  // Max expected current in Amps
// Config register value: AVG=16 (010), VBUSCT=1100us (100), VSHCT=1100us (100), MODE=continuous (111)
// Bits: [15:12]=0, [11:9]=010 (AVG=16), [8:6]=100 (VBUS=1100us), [5:3]=100 (VSH=1100us), [2:0]=111 (continuous)
#define INA226_CONFIG_VALUE     0x4527  // AVG=16, 1100us conv times, continuous mode

// Maximum networks to display
#define MAX_NETWORKS      50
#define MAX_CLIENTS_PER_NETWORK  20
#define OBSERVER_POLL_INTERVAL_MS  20000  // 20 seconds
#define OBSERVER_LINE_BUFFER_SIZE  512

// Material Design Colors
#define COLOR_MATERIAL_BG       lv_color_make(18, 18, 18)      // #121212 - dark background
#define COLOR_MATERIAL_BLUE     lv_color_make(33, 150, 243)    // #2196F3
#define COLOR_MATERIAL_RED      lv_color_make(244, 67, 54)     // #F44336
#define COLOR_MATERIAL_PURPLE   lv_color_make(156, 39, 176)    // #9C27B0
#define COLOR_MATERIAL_GREEN    lv_color_make(76, 175, 80)     // #4CAF50
#define COLOR_MATERIAL_AMBER    lv_color_make(255, 193, 7)     // #FFC107
#define COLOR_MATERIAL_CYAN     lv_color_make(0, 188, 212)     // #00BCD4
#define COLOR_MATERIAL_TEAL     lv_color_make(0, 150, 136)     // #009688 - Kismet-style teal
#define COLOR_MATERIAL_ORANGE   lv_color_make(255, 152, 0)     // #FF9800
#define COLOR_MATERIAL_PINK     lv_color_make(233, 30, 99)     // #E91E63

// WiFi network info structure
typedef struct {
    int index;
    char ssid[33];
    char bssid[18];
    int rssi;
    char band[8];  // "2.4GHz" or "5GHz"
    char security[24];
} wifi_network_t;

// Observer network info structure (for sniffer results)
typedef struct {
    char ssid[33];
    char bssid[18];      // MAC address of AP
    int scan_index;      // 1-based index from scan_networks (for select_networks command)
    int channel;
    int rssi;            // Signal strength in dBm
    char band[8];        // "2.4GHz" or "5GHz"
    int client_count;
    char clients[MAX_CLIENTS_PER_NETWORK][18];  // MAC addresses of clients
} observer_network_t;

// Global variables
static wifi_network_t networks[MAX_NETWORKS];
static int network_count = 0;
static bool scan_in_progress = false;

// Selected network indices (0-based)
static int selected_network_indices[MAX_NETWORKS];
static int selected_network_count = 0;

// Observer global variables (large arrays in PSRAM)
static observer_network_t *observer_networks = NULL;  // Allocated in PSRAM
static int observer_network_count = 0;
static bool observer_running = false;
static TimerHandle_t observer_timer = NULL;
static TaskHandle_t observer_task_handle = NULL;

// Popup state
static bool popup_open = false;
static int popup_network_idx = -1;  // Index of network being viewed in popup
static lv_obj_t *popup_obj = NULL;
static lv_obj_t *popup_clients_container = NULL;
static TimerHandle_t popup_timer = NULL;  // 10s timer for popup polling
#define POPUP_POLL_INTERVAL_MS  10000  // 10 seconds

// Deauth popup state
static bool deauth_active = false;
static int deauth_network_idx = -1;
static int deauth_client_idx = -1;
static lv_obj_t *deauth_popup_obj = NULL;
static lv_obj_t *deauth_btn = NULL;
static lv_obj_t *deauth_btn_label = NULL;

// Scan & Attack deauth popup
static lv_obj_t *scan_deauth_overlay = NULL;  // Modal overlay
static lv_obj_t *scan_deauth_popup_obj = NULL;

// Evil Twin attack state
static lv_obj_t *evil_twin_overlay = NULL;  // Modal overlay
static lv_obj_t *evil_twin_popup_obj = NULL;
static lv_obj_t *evil_twin_network_dropdown = NULL;
static lv_obj_t *evil_twin_html_dropdown = NULL;
static lv_obj_t *evil_twin_status_label = NULL;
static lv_obj_t *evil_twin_close_btn = NULL;
static int evil_twin_html_count = 0;

// SAE Overflow attack state
static lv_obj_t *sae_popup_overlay = NULL;
static lv_obj_t *sae_popup_obj = NULL;
static char evil_twin_html_files[20][64];  // Max 20 files, 64 chars each
static volatile bool evil_twin_monitoring = false;
static TaskHandle_t evil_twin_monitor_task_handle = NULL;

// PSRAM buffers for observer (allocated once)
static char *observer_rx_buffer = NULL;
static char *observer_line_buffer = NULL;

// LVGL UI elements - pages
static lv_obj_t *tiles_container = NULL;
static lv_obj_t *scan_page = NULL;
static lv_obj_t *observer_page = NULL;
static lv_obj_t *esp_modem_page = NULL;
static lv_obj_t *global_attacks_page = NULL;

// ESP Modem global variables
static wifi_ap_record_t *esp_modem_networks = NULL;  // Allocated in PSRAM
static uint16_t esp_modem_network_count = 0;
static bool esp_modem_scan_in_progress = false;
static bool esp_modem_wifi_initialized = false;

// INA226 Power Monitor
static i2c_master_dev_handle_t ina226_dev_handle = NULL;
static bool ina226_initialized = false;

// Battery status bar
static lv_obj_t *status_bar = NULL;
static lv_obj_t *battery_voltage_label = NULL;
static lv_obj_t *charging_status_label = NULL;
static lv_timer_t *battery_update_timer = NULL;
static float current_battery_voltage = 0.0f;
static bool current_charging_status = false;

// LVGL UI elements - scan page
static lv_obj_t *scan_btn = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *network_list = NULL;
static lv_obj_t *spinner = NULL;

// LVGL UI elements - observer page
static lv_obj_t *observer_start_btn = NULL;
static lv_obj_t *observer_stop_btn = NULL;
static lv_obj_t *observer_table = NULL;
static lv_obj_t *observer_status_label = NULL;

// LVGL UI elements - ESP Modem page
static lv_obj_t *esp_modem_scan_btn = NULL;
static lv_obj_t *esp_modem_status_label = NULL;
static lv_obj_t *esp_modem_network_list = NULL;
static lv_obj_t *esp_modem_spinner = NULL;

// Forward declarations
static void show_main_tiles(void);
static void show_scan_page(void);
static void show_observer_page(void);
static void show_esp_modem_page(void);
static void main_tile_event_cb(lv_event_t *e);
static void back_btn_event_cb(lv_event_t *e);
static void network_checkbox_event_cb(lv_event_t *e);
static void attack_tile_event_cb(lv_event_t *e);
static void create_status_bar(void);
static void show_global_attacks_page(void);
static void global_attack_tile_event_cb(lv_event_t *e);
static void observer_back_btn_event_cb(lv_event_t *e);
static void esp_modem_back_btn_event_cb(lv_event_t *e);
static void esp_modem_scan_btn_click_cb(lv_event_t *e);
static esp_err_t esp_modem_wifi_init(void);
static void network_row_click_cb(lv_event_t *e);
static void client_row_click_cb(lv_event_t *e);
static void show_network_popup(int network_idx);
static void close_network_popup(void);
static void show_deauth_popup(int network_idx, int client_idx);
static void close_deauth_popup(void);
static void deauth_btn_click_cb(lv_event_t *e);
static void update_observer_table(void);
static bool parse_sniffer_network_line(const char *line, observer_network_t *net);
static bool parse_sniffer_client_line(const char *line, char *mac_out, size_t mac_size);
static void show_scan_deauth_popup(void);
static void scan_deauth_popup_close_cb(lv_event_t *e);
static void fetch_html_files_from_sd(void);
static void show_evil_twin_popup(void);
static void evil_twin_start_cb(lv_event_t *e);
static void evil_twin_close_cb(lv_event_t *e);
static void evil_twin_monitor_task(void *arg);
static void show_sae_popup(int network_idx);
static void sae_popup_close_cb(lv_event_t *e);

//==================================================================================
// INA226 Power Monitor Driver
//==================================================================================

static esp_err_t ina226_init(void)
{
    if (ina226_initialized) {
        return ESP_OK;
    }
    
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Probe for INA226 at default address
    esp_err_t ret = i2c_master_probe(i2c_bus, INA226_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "INA226 not found at address 0x%02X", INA226_I2C_ADDR);
        return ret;
    }
    
    // Add INA226 device to I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_I2C_ADDR,
        .scl_speed_hz = 100000,  // 100kHz
    };
    
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &ina226_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add INA226 device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Verify manufacturer ID (should be 0x5449 for TI)
    uint8_t reg = INA226_REG_MFG_ID;
    uint8_t data[2];
    ret = i2c_master_transmit_receive(ina226_dev_handle, &reg, 1, data, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read INA226 manufacturer ID: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(ina226_dev_handle);
        ina226_dev_handle = NULL;
        return ret;
    }
    
    uint16_t mfg_id = (data[0] << 8) | data[1];
    ESP_LOGI(TAG, "INA226 Manufacturer ID: 0x%04X (expected 0x5449)", mfg_id);
    
    if (mfg_id != 0x5449) {
        ESP_LOGE(TAG, "INA226 manufacturer ID mismatch - device not responding correctly");
        i2c_master_bus_rm_device(ina226_dev_handle);
        ina226_dev_handle = NULL;
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Configure INA226: AVG=16, 1100us conversion times, continuous mode
    // Using INA226_CONFIG_VALUE (0x4527) from M5Tab5 official demo
    uint8_t config_cmd[3] = {INA226_REG_CONFIG, (INA226_CONFIG_VALUE >> 8) & 0xFF, INA226_CONFIG_VALUE & 0xFF};
    ret = i2c_master_transmit(ina226_dev_handle, config_cmd, 3, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure INA226: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(ina226_dev_handle);
        ina226_dev_handle = NULL;
        return ret;
    }
    
    // Calibrate for current/power measurements
    // Cal = 0.00512 / (currentLSB * Rshunt)
    // With Rshunt=0.005 and maxI=8.192A: currentLSB = 8.192/32767 ≈ 0.00025
    // Cal = 0.00512 / (0.00025 * 0.005) = 4096 = 0x1000
    uint8_t calib_cmd[3] = {INA226_REG_CALIB, 0x10, 0x00};
    ret = i2c_master_transmit(ina226_dev_handle, calib_cmd, 3, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to calibrate INA226: %s (voltage readings will still work)", esp_err_to_name(ret));
        // Don't fail - voltage readings will still work
    }
    
    ina226_initialized = true;
    ESP_LOGI(TAG, "INA226 Power Monitor initialized successfully at address 0x%02X", INA226_I2C_ADDR);
    return ESP_OK;
}

static float ina226_read_bus_voltage(void)
{
    if (!ina226_initialized || ina226_dev_handle == NULL) {
        return 0.0f;
    }
    
    uint8_t reg = INA226_REG_BUS_VOLT;
    uint8_t data[2];
    
    esp_err_t ret = i2c_master_transmit_receive(ina226_dev_handle, &reg, 1, data, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read INA226 bus voltage: %s", esp_err_to_name(ret));
        return 0.0f;
    }
    
    // Bus voltage register is 16-bit, 1.25mV per LSB
    uint16_t raw_voltage = (data[0] << 8) | data[1];
    // Use same formula as official M5Tab5 demo: voltage * 0.00125 (V)
    float voltage_v = raw_voltage * 0.00125f;
    
    ESP_LOGD(TAG, "INA226 raw: 0x%04X (%u), voltage: %.3fV", raw_voltage, raw_voltage, voltage_v);
    
    return voltage_v;
}

//==================================================================================
// Battery Status Functions
//==================================================================================

static bool get_charging_status(void)
{
    // Use USB-C detection as proxy for charging status
    // When USB-C is connected and charging is enabled, device is charging
    return bsp_usb_c_detect();
}

static void update_battery_status(void)
{
    current_battery_voltage = ina226_read_bus_voltage();
    current_charging_status = get_charging_status();
}

static void battery_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    
    // Read new values
    update_battery_status();
    
    // Memory stats
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    size_t sram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t sram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_min = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    
    // Debug log with battery and memory stats
    ESP_LOGI(TAG, "Battery: %.2fV, charging: %d", current_battery_voltage, current_charging_status);
    ESP_LOGI(TAG, "Memory - PSRAM: %u KB free (min: %u KB) | SRAM: %u KB free (min: %u KB) | DMA: %u KB free (min: %u KB)",
             (unsigned)(psram_free / 1024), (unsigned)(psram_min / 1024),
             (unsigned)(sram_free / 1024), (unsigned)(sram_min / 1024),
             (unsigned)(dma_free / 1024), (unsigned)(dma_min / 1024));
    
    // Update UI labels
    if (battery_voltage_label) {
        if (current_battery_voltage > 0.1f) {
            // Use snprintf instead of lv_label_set_text_fmt for float support
            char voltage_str[16];
            snprintf(voltage_str, sizeof(voltage_str), "%.2fV", current_battery_voltage);
            lv_label_set_text(battery_voltage_label, voltage_str);
        } else {
            lv_label_set_text(battery_voltage_label, "-- V");
        }
    }
    
    if (charging_status_label) {
        if (current_charging_status) {
            // Just icon, no text - saves space
            lv_label_set_text(charging_status_label, LV_SYMBOL_CHARGE);
            lv_obj_set_style_text_color(charging_status_label, lv_color_make(76, 175, 80), 0);  // Green
        } else {
            lv_label_set_text(charging_status_label, LV_SYMBOL_BATTERY_FULL);
            lv_obj_set_style_text_color(charging_status_label, lv_color_make(255, 255, 255), 0);  // White
        }
    }
}

// UART initialization
static void uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "UART%d initialized: TX=%d, RX=%d, baud=%d", 
             UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
}

// Send command over UART
static void uart_send_command(const char *cmd)
{
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_NUM, "\r\n", 2);
    ESP_LOGI(TAG, "Sent command: %s", cmd);
}

// Parse a single network line like: "1","SSID","","C4:2B:44:12:29:21","1","WPA2","-53","2.4GHz"
static bool parse_network_line(const char *line, wifi_network_t *net)
{
    // Check if line starts with quote and number
    if (line[0] != '"') return false;
    
    char temp[256];
    strncpy(temp, line, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    // Parse CSV with quoted fields
    char *fields[8] = {NULL};
    int field_idx = 0;
    char *p = temp;
    
    while (*p && field_idx < 8) {
        if (*p == '"') {
            p++;  // Skip opening quote
            fields[field_idx] = p;
            // Find closing quote
            while (*p && *p != '"') p++;
            if (*p == '"') {
                *p = '\0';
                p++;
            }
            field_idx++;
            // Skip comma
            if (*p == ',') p++;
        } else {
            p++;
        }
    }
    
    if (field_idx < 8) return false;
    
    // fields[0] = index, fields[1] = SSID, fields[3] = BSSID, fields[5] = security, fields[6] = RSSI, fields[7] = band
    net->index = atoi(fields[0]);
    if (net->index <= 0) return false;
    
    strncpy(net->ssid, fields[1], sizeof(net->ssid) - 1);
    net->ssid[sizeof(net->ssid) - 1] = '\0';
    
    strncpy(net->bssid, fields[3], sizeof(net->bssid) - 1);
    net->bssid[sizeof(net->bssid) - 1] = '\0';
    
    strncpy(net->security, fields[5], sizeof(net->security) - 1);
    net->security[sizeof(net->security) - 1] = '\0';
    
    net->rssi = atoi(fields[6]);
    
    strncpy(net->band, fields[7], sizeof(net->band) - 1);
    net->band[sizeof(net->band) - 1] = '\0';
    
    return true;
}

// WiFi scan task
static void wifi_scan_task(void *arg)
{
    ESP_LOGI(TAG, "Starting WiFi scan task");
    
    // Clear previous results
    network_count = 0;
    memset(networks, 0, sizeof(networks));
    
    // Flush UART buffer
    uart_flush(UART_NUM);
    
    // Send scan command
    uart_send_command("scan_networks");
    
    // Buffer for receiving data
    static char rx_buffer[UART_BUF_SIZE];
    static char line_buffer[512];
    int line_pos = 0;
    bool scan_complete = false;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(UART_RX_TIMEOUT);
    
    while (!scan_complete && (xTaskGetTickCount() - start_time) < timeout_ticks) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGD(TAG, "Received %d bytes", len);
            
            // Process received data character by character
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGD(TAG, "Line: %s", line_buffer);
                        
                        // Check for scan complete marker
                        if (strstr(line_buffer, "Scan results printed") != NULL) {
                            scan_complete = true;
                            ESP_LOGI(TAG, "Scan complete marker received");
                            break;
                        }
                        
                        // Try to parse network line
                        if (line_buffer[0] == '"' && network_count < MAX_NETWORKS) {
                            wifi_network_t net;
                            if (parse_network_line(line_buffer, &net)) {
                                networks[network_count] = net;
                                network_count++;
                                ESP_LOGI(TAG, "Parsed network %d: %s (%s) %s", 
                                         net.index, net.ssid, net.bssid, net.band);
                            }
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
    }
    
    if (!scan_complete) {
        ESP_LOGW(TAG, "Scan timed out");
    }
    
    ESP_LOGI(TAG, "Scan finished. Found %d networks", network_count);
    
    // Update UI on main thread
    bsp_display_lock(0);
    
    // Hide spinner
    if (spinner) {
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update status
    if (status_label) {
        if (scan_complete) {
            lv_label_set_text_fmt(status_label, "Found %d networks", network_count);
        } else {
            lv_label_set_text(status_label, "Scan timed out");
        }
    }
    
    // Update network list
    if (network_list) {
        lv_obj_clean(network_list);
        
        for (int i = 0; i < network_count; i++) {
            wifi_network_t *net = &networks[i];
            
            // Create list item with horizontal layout (checkbox + text container)
            lv_obj_t *item = lv_obj_create(network_list);
            lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(item, 8, 0);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_radius(item, 8, 0);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(item, 12, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);  // Don't steal clicks from checkbox
            
            // Checkbox (on the left) - explicit size for better touch accuracy
            lv_obj_t *cb = lv_checkbox_create(item);
            lv_checkbox_set_text(cb, "");  // Empty text - we use separate labels
            lv_obj_set_size(cb, 50, 50);  // Explicit size for touch target
            lv_obj_set_ext_click_area(cb, 15);  // Extend touch area by 15px
            lv_obj_set_style_pad_all(cb, 4, 0);
            lv_obj_set_style_align(cb, LV_ALIGN_LEFT_MID, 0);  // Center vertically in row
            // Style the indicator - dark when unchecked, green when checked
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x3D3D3D), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x4CAF50), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_border_color(cb, lv_color_hex(0x888888), LV_PART_INDICATOR);
            lv_obj_set_style_border_width(cb, 2, LV_PART_INDICATOR);
            lv_obj_set_style_radius(cb, 4, LV_PART_INDICATOR);
            // Pass 0-based index as user data
            lv_obj_add_event_cb(cb, network_checkbox_event_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
            
            // Text container (vertical layout for SSID and info)
            lv_obj_t *text_cont = lv_obj_create(item);
            lv_obj_set_size(text_cont, lv_pct(85), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(text_cont, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(text_cont, 0, 0);
            lv_obj_set_style_pad_all(text_cont, 0, 0);
            lv_obj_set_flex_flow(text_cont, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(text_cont, 4, 0);
            lv_obj_clear_flag(text_cont, LV_OBJ_FLAG_CLICKABLE);  // Don't steal clicks
            
            // SSID (or "Hidden" if empty)
            lv_obj_t *ssid_label = lv_label_create(text_cont);
            if (strlen(net->ssid) > 0) {
                lv_label_set_text(ssid_label, net->ssid);
            } else {
                lv_label_set_text(ssid_label, "(Hidden)");
            }
            lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
            
            // BSSID and Band
            lv_obj_t *info_label = lv_label_create(text_cont);
            lv_label_set_text_fmt(info_label, "%s  |  %s  |  %d dBm", 
                                  net->bssid, net->band, net->rssi);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), 0);
        }
    }
    
    // Re-enable scan button
    if (scan_btn) {
        lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
    }
    
    scan_in_progress = false;
    
    bsp_display_unlock();
    
    // Delete this task
    vTaskDelete(NULL);
}

// Scan button click handler
static void scan_btn_click_cb(lv_event_t *e)
{
    if (scan_in_progress) {
        ESP_LOGW(TAG, "Scan already in progress");
        return;
    }
    
    scan_in_progress = true;
    
    // Clear previous selections
    selected_network_count = 0;
    memset(selected_network_indices, 0, sizeof(selected_network_indices));
    
    // Disable button during scan
    lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
    
    // Show spinner
    if (spinner) {
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update status
    if (status_label) {
        lv_label_set_text(status_label, "Scanning...");
    }
    
    // Clear previous results
    if (network_list) {
        lv_obj_clean(network_list);
    }
    
    // Start scan task
    xTaskCreate(wifi_scan_task, "wifi_scan", 8192, NULL, 5, NULL);
}

// Create a single tile button with icon, text, color
static lv_obj_t *create_tile(lv_obj_t *parent, const char *icon, const char *text, lv_color_t bg_color, lv_event_cb_t callback, const char *user_data)
{
    lv_obj_t *tile = lv_btn_create(parent);
    lv_obj_set_size(tile, 230, 140);  // Large tile size for 3 columns
    lv_obj_set_style_bg_color(tile, bg_color, LV_STATE_DEFAULT);
    // Pressed state: lighten the color
    lv_obj_set_style_bg_color(tile, lv_color_lighten(bg_color, 50), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 0, 0);  // No border for Material look
    lv_obj_set_style_radius(tile, 16, 0);  // Rounded corners
    lv_obj_set_style_shadow_width(tile, 12, 0);  // Material shadow
    lv_obj_set_style_shadow_color(tile, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_opa(tile, LV_OPA_30, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tile, 12, 0);
    
    if (icon) {
        lv_obj_t *icon_label = lv_label_create(tile);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);  // Large icon
        lv_obj_set_style_text_color(icon_label, lv_color_make(255, 255, 255), 0);  // White icon
    }
    
    if (text) {
        lv_obj_t *text_label = lv_label_create(tile);
        lv_label_set_text(text_label, text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_18, 0);  // Larger text
        lv_obj_set_style_text_color(text_label, lv_color_make(255, 255, 255), 0);  // White text
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text_label, 210);
    }
    
    if (callback && user_data) {
        lv_obj_add_event_cb(tile, callback, LV_EVENT_CLICKED, (void*)user_data);
    }
    
    return tile;
}

// Create a smaller tile button for compact layouts (e.g., attack selection row)
static lv_obj_t *create_small_tile(lv_obj_t *parent, const char *icon, const char *text, lv_color_t bg_color, lv_event_cb_t callback, const char *user_data)
{
    lv_obj_t *tile = lv_btn_create(parent);
    lv_obj_set_size(tile, 85, 55);  // Smaller tile size for single row
    lv_obj_set_style_bg_color(tile, bg_color, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(tile, lv_color_lighten(bg_color, 50), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 8, 0);  // Smaller radius
    lv_obj_set_style_shadow_width(tile, 4, 0);  // Smaller shadow
    lv_obj_set_style_shadow_color(tile, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_opa(tile, LV_OPA_30, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tile, 4, 0);
    
    if (icon) {
        lv_obj_t *icon_label = lv_label_create(tile);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);  // Smaller icon
        lv_obj_set_style_text_color(icon_label, lv_color_make(255, 255, 255), 0);
    }
    
    if (text) {
        lv_obj_t *text_label = lv_label_create(tile);
        lv_label_set_text(text_label, text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_12, 0);  // Smaller text
        lv_obj_set_style_text_color(text_label, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text_label, 75);
    }
    
    if (callback && user_data) {
        lv_obj_add_event_cb(tile, callback, LV_EVENT_CLICKED, (void*)user_data);
    }
    
    return tile;
}

// Create status bar at top of screen (reusable helper)
static void create_status_bar(void)
{
    ESP_LOGI(TAG, "Creating status bar...");
    lv_obj_t *scr = lv_scr_act();
    
    // Delete existing status bar if present
    if (status_bar) {
        lv_obj_del(status_bar);
        status_bar = NULL;
        battery_voltage_label = NULL;
        charging_status_label = NULL;
    }
    
    // Create status bar at top of screen
    status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, lv_pct(100), 40);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_make(30, 30, 30), 0);  // Slightly lighter than background
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_hor(status_bar, 16, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // App title centered
    lv_obj_t *app_title = lv_label_create(status_bar);
    lv_label_set_text(app_title, "LABORATORIUM");
    lv_obj_set_style_text_font(app_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(app_title, lv_color_make(255, 255, 255), 0);
    lv_obj_align(app_title, LV_ALIGN_CENTER, 0, 0);
    
    // Battery status container on the right - use fixed width to ensure visibility
    lv_obj_t *battery_cont = lv_obj_create(status_bar);
    lv_obj_set_size(battery_cont, 140, 36);  // Fixed width for voltage + icon
    lv_obj_set_style_bg_opa(battery_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_cont, 0, 0);
    lv_obj_set_style_pad_all(battery_cont, 0, 0);
    lv_obj_align(battery_cont, LV_ALIGN_RIGHT_MID, -8, 0);  // Small margin from edge
    lv_obj_set_flex_flow(battery_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_cont, 8, 0);
    lv_obj_clear_flag(battery_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Battery voltage label (e.g., "8.13V")
    battery_voltage_label = lv_label_create(battery_cont);
    lv_label_set_text(battery_voltage_label, "-.--V");
    lv_obj_set_style_text_font(battery_voltage_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(battery_voltage_label, lv_color_make(255, 255, 255), 0);
    
    // Charging status label (just icon, no text to save space)
    charging_status_label = lv_label_create(battery_cont);
    lv_label_set_text(charging_status_label, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(charging_status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(charging_status_label, lv_color_make(255, 255, 255), 0);
    
    // Initialize INA226 if not already done
    if (!ina226_initialized) {
        ina226_init();
    }
    
    // Create battery status update timer if not already running
    if (battery_update_timer == NULL) {
        battery_update_timer = lv_timer_create(battery_status_timer_cb, BATTERY_UPDATE_MS, NULL);
        ESP_LOGI(TAG, "Battery timer created");
    }
    
    // Update battery status immediately
    update_battery_status();
    battery_status_timer_cb(NULL);
    
    ESP_LOGI(TAG, "Status bar created: voltage_label=%p, charging_label=%p, timer=%p",
             (void*)battery_voltage_label, (void*)charging_status_label, (void*)battery_update_timer);
}

// Main tile click handler
static void main_tile_event_cb(lv_event_t *e)
{
    const char *tile_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Tile clicked: %s", tile_name);
    
    if (strcmp(tile_name, "WiFi Scan & Attack") == 0) {
        show_scan_page();
    } else if (strcmp(tile_name, "Global WiFi Attacks") == 0) {
        show_global_attacks_page();
    } else if (strcmp(tile_name, "Network Observer") == 0) {
        show_observer_page();
    } else if (strcmp(tile_name, "Internal C6") == 0) {
        show_esp_modem_page();
    } else {
        // Placeholder for other tiles - show a message
        ESP_LOGI(TAG, "Feature '%s' not implemented yet", tile_name);
    }
}

// Network checkbox event handler - toggle selection (0-based index)
static void network_checkbox_event_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_event_get_user_data(e);  // 0-based index
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    
    if (checked) {
        // Add to selected list if not already present and not full
        bool found = false;
        for (int i = 0; i < selected_network_count; i++) {
            if (selected_network_indices[i] == index) {
                found = true;
                break;
            }
        }
        if (!found && selected_network_count < MAX_NETWORKS) {
            selected_network_indices[selected_network_count++] = index;
            ESP_LOGI(TAG, "Selected network index %d (total: %d)", index, selected_network_count);
        }
    } else {
        // Remove from selected list
        for (int i = 0; i < selected_network_count; i++) {
            if (selected_network_indices[i] == index) {
                // Shift remaining elements
                for (int j = i; j < selected_network_count - 1; j++) {
                    selected_network_indices[j] = selected_network_indices[j + 1];
                }
                selected_network_count--;
                ESP_LOGI(TAG, "Deselected network index %d (total: %d)", index, selected_network_count);
                break;
            }
        }
    }
}

// Attack tile event handler for bottom icon bar
static void attack_tile_event_cb(lv_event_t *e)
{
    const char *attack_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Attack tile clicked: %s", attack_name);
    
    if (selected_network_count == 0) {
        ESP_LOGW(TAG, "No networks selected for attack");
        return;
    }
    
    // Log selected networks
    ESP_LOGI(TAG, "Selected %d network(s) for %s attack:", selected_network_count, attack_name);
    for (int i = 0; i < selected_network_count; i++) {
        int idx = selected_network_indices[i];
        if (idx >= 0 && idx < network_count) {
            ESP_LOGI(TAG, "  [%d] %s (%s)", idx, networks[idx].ssid, networks[idx].bssid);
        }
    }
    
    // Handle Deauth attack
    if (strcmp(attack_name, "Deauth") == 0) {
        // Build select_networks command with 1-based indices
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "select_networks");
        for (int i = 0; i < selected_network_count; i++) {
            int idx = selected_network_indices[i];
            if (idx >= 0 && idx < network_count) {
                char num[8];
                snprintf(num, sizeof(num), " %d", networks[idx].index);  // .index is 1-based
                strncat(cmd, num, sizeof(cmd) - strlen(cmd) - 1);
            }
        }
        
        // Send select_networks command
        uart_send_command(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Send start_deauth command
        uart_send_command("start_deauth");
        
        // Show popup with attacking networks
        show_scan_deauth_popup();
        return;
    }
    
    // Handle Evil Twin attack
    if (strcmp(attack_name, "Evil Twin") == 0) {
        show_evil_twin_popup();
        return;
    }
    
    // Handle SAE Overflow attack
    if (strcmp(attack_name, "SAE Overflow") == 0) {
        if (selected_network_count != 1) {
            ESP_LOGW(TAG, "SAE Overflow requires exactly one network, selected: %d", selected_network_count);
            // Show error in status label if available
            if (status_label) {
                lv_label_set_text(status_label, "Please select just one network");
                lv_obj_set_style_text_color(status_label, COLOR_MATERIAL_RED, 0);
            }
            return;
        }
        
        int idx = selected_network_indices[0];
        int net_1based = networks[idx].index;
        
        // Send select_networks command
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "select_networks %d", net_1based);
        uart_send_command(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Send sae_overflow command
        uart_send_command("sae_overflow");
        
        // Show popup
        show_sae_popup(idx);
        return;
    }
    
    // TODO: Implement other attack types
    // - Handshaker
    // - Sniffer
}

// Close callback for scan deauth popup - sends stop command
static void scan_deauth_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Deauth popup closed - sending stop command");
    
    // Send stop command
    uart_send_command("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    if (scan_deauth_overlay) {
        lv_obj_del(scan_deauth_overlay);
        scan_deauth_overlay = NULL;
        scan_deauth_popup_obj = NULL;
    }
}

// Show deauth popup with list of selected networks being attacked
static void show_scan_deauth_popup(void)
{
    if (scan_deauth_popup_obj != NULL) return;  // Already showing
    
    lv_obj_t *scr = lv_scr_act();
    
    // Create modal overlay (full screen, semi-transparent, blocks input behind)
    scan_deauth_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(scan_deauth_overlay);
    lv_obj_set_size(scan_deauth_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scan_deauth_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scan_deauth_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(scan_deauth_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scan_deauth_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    scan_deauth_popup_obj = lv_obj_create(scan_deauth_overlay);
    lv_obj_set_size(scan_deauth_popup_obj, 550, 450);
    lv_obj_center(scan_deauth_popup_obj);
    lv_obj_set_style_bg_color(scan_deauth_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(scan_deauth_popup_obj, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(scan_deauth_popup_obj, 2, 0);
    lv_obj_set_style_radius(scan_deauth_popup_obj, 16, 0);
    lv_obj_set_style_shadow_width(scan_deauth_popup_obj, 30, 0);
    lv_obj_set_style_shadow_color(scan_deauth_popup_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(scan_deauth_popup_obj, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(scan_deauth_popup_obj, 16, 0);
    lv_obj_set_flex_flow(scan_deauth_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scan_deauth_popup_obj, 12, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(scan_deauth_popup_obj);
    lv_label_set_text(title, "Attacking networks:");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    
    // Scrollable container for network list
    lv_obj_t *list_cont = lv_obj_create(scan_deauth_popup_obj);
    lv_obj_set_size(list_cont, lv_pct(100), 280);
    lv_obj_set_style_bg_color(list_cont, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_radius(list_cont, 8, 0);
    lv_obj_set_style_pad_all(list_cont, 12, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_cont, 8, 0);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Add each selected network to the list
    for (int i = 0; i < selected_network_count; i++) {
        int idx = selected_network_indices[i];
        if (idx >= 0 && idx < network_count) {
            wifi_network_t *net = &networks[idx];
            
            // Network item container
            lv_obj_t *item = lv_obj_create(list_cont);
            lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_radius(item, 6, 0);
            lv_obj_set_style_pad_all(item, 10, 0);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(item, 4, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            
            // SSID
            const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
            lv_obj_t *ssid_label = lv_label_create(item);
            lv_label_set_text_fmt(ssid_label, "%s %s", LV_SYMBOL_WIFI, ssid_display);
            lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
            
            // BSSID and Band
            lv_obj_t *info_label = lv_label_create(item);
            lv_label_set_text_fmt(info_label, "BSSID: %s | %s", net->bssid, net->band);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
        }
    }
    
    // STOP button
    lv_obj_t *stop_btn = lv_btn_create(scan_deauth_popup_obj);
    lv_obj_set_size(stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xCC0000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, scan_deauth_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(stop_btn);
    lv_label_set_text(btn_label, "STOP ATTACK");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_18, 0);
    lv_obj_center(btn_label);
}

// ======================= SAE Overflow Attack Functions =======================

// Close SAE popup
static void sae_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "SAE popup closed - sending stop command");
    
    // Send stop command
    uart_send_command("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    if (sae_popup_overlay) {
        lv_obj_del(sae_popup_overlay);
        sae_popup_overlay = NULL;
        sae_popup_obj = NULL;
    }
}

// Show SAE Overflow popup
static void show_sae_popup(int network_idx)
{
    if (sae_popup_obj != NULL) return;  // Already showing
    
    if (network_idx < 0 || network_idx >= network_count) return;
    
    wifi_network_t *net = &networks[network_idx];
    const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
    
    lv_obj_t *scr = lv_scr_act();
    
    // Create modal overlay (full screen, semi-transparent, blocks input behind)
    sae_popup_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(sae_popup_overlay);
    lv_obj_set_size(sae_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(sae_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sae_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(sae_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sae_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    sae_popup_obj = lv_obj_create(sae_popup_overlay);
    lv_obj_set_size(sae_popup_obj, 500, 300);
    lv_obj_center(sae_popup_obj);
    lv_obj_set_style_bg_color(sae_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(sae_popup_obj, COLOR_MATERIAL_PINK, 0);
    lv_obj_set_style_border_width(sae_popup_obj, 2, 0);
    lv_obj_set_style_radius(sae_popup_obj, 16, 0);
    lv_obj_set_style_shadow_width(sae_popup_obj, 30, 0);
    lv_obj_set_style_shadow_color(sae_popup_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(sae_popup_obj, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(sae_popup_obj, 20, 0);
    lv_obj_set_flex_flow(sae_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sae_popup_obj, 16, 0);
    lv_obj_set_flex_align(sae_popup_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Title
    lv_obj_t *title = lv_label_create(sae_popup_obj);
    lv_label_set_text(title, "SAE Overflow Active");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PINK, 0);
    
    // Network info
    lv_obj_t *network_label = lv_label_create(sae_popup_obj);
    lv_label_set_text_fmt(network_label, "on network:\n\n%s %s\n%s", 
                          LV_SYMBOL_WIFI, ssid_display, net->bssid);
    lv_obj_set_style_text_font(network_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(network_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(network_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Spacer
    lv_obj_t *spacer = lv_obj_create(sae_popup_obj);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    
    // STOP button
    lv_obj_t *stop_btn = lv_btn_create(sae_popup_obj);
    lv_obj_set_size(stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xCC0000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, sae_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
}

// ======================= Evil Twin Attack Functions =======================

// Fetch HTML files list from SD card via UART
static void fetch_html_files_from_sd(void)
{
    evil_twin_html_count = 0;
    memset(evil_twin_html_files, 0, sizeof(evil_twin_html_files));
    
    // Flush UART buffer
    uart_flush(UART_NUM);
    
    // Send list_sd command
    uart_send_command("list_sd");
    
    // Buffer for receiving data
    static char rx_buffer[2048];
    static char line_buffer[256];
    int line_pos = 0;
    bool header_found = false;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(3000);  // 3 second timeout
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && evil_twin_html_count < 20) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        // Check for header line
                        if (strstr(line_buffer, "HTML files found") != NULL) {
                            header_found = true;
                        } else if (header_found && line_pos > 2) {
                            // Parse line format: "1 PLAY.html"
                            int file_num;
                            char filename[64];
                            if (sscanf(line_buffer, "%d %63s", &file_num, filename) == 2) {
                                snprintf(evil_twin_html_files[evil_twin_html_count], 
                                         sizeof(evil_twin_html_files[0]), "%s", filename);
                                ESP_LOGI(TAG, "Found HTML file %d: %s", file_num, filename);
                                evil_twin_html_count++;
                            }
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Fetched %d HTML files from SD card", evil_twin_html_count);
}

// Close Evil Twin popup
static void evil_twin_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Evil Twin popup closed");
    
    // Stop monitoring
    evil_twin_monitoring = false;
    
    // Send stop command
    uart_send_command("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    if (evil_twin_overlay) {
        lv_obj_del(evil_twin_overlay);
        evil_twin_overlay = NULL;
        evil_twin_popup_obj = NULL;
        evil_twin_network_dropdown = NULL;
        evil_twin_html_dropdown = NULL;
        evil_twin_status_label = NULL;
        evil_twin_close_btn = NULL;
    }
}

// Evil Twin monitor task - watches UART for password capture
static void evil_twin_monitor_task(void *arg)
{
    (void)arg;
    
    static char rx_buffer[1024];
    static char line_buffer[512];
    int line_pos = 0;
    
    ESP_LOGI(TAG, "Evil Twin monitor task started");
    
    while (evil_twin_monitoring) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(200));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "Evil Twin UART: %s", line_buffer);
                        
                        // Look for client connection: "Client connected - MAC: XX:XX:XX:XX:XX:XX"
                        char *client_connected = strstr(line_buffer, "Client connected - MAC:");
                        if (client_connected && evil_twin_status_label) {
                            // Extract MAC address
                            char mac[20] = {0};
                            char *mac_start = client_connected + 24;  // Skip "Client connected - MAC: "
                            int mac_len = 0;
                            while (mac_start[mac_len] && mac_start[mac_len] != '\n' && mac_start[mac_len] != '\r' && mac_len < 17) {
                                mac[mac_len] = mac_start[mac_len];
                                mac_len++;
                            }
                            
                            // Update status with client connected message
                            char status_text[256];
                            snprintf(status_text, sizeof(status_text),
                                "Client connected!\n\n"
                                "MAC: %s\n\n"
                                "Waiting for password...", mac);
                            lv_label_set_text(evil_twin_status_label, status_text);
                            lv_obj_set_style_text_color(evil_twin_status_label, COLOR_MATERIAL_AMBER, 0);
                        }
                        
                        // Look for password capture pattern:
                        // "Wi-Fi: connected to SSID='XXX' with password='YYY'"
                        // Note: SSID and password may be quoted with single quotes
                        char *connected = strstr(line_buffer, "connected to SSID=");
                        char *pwd_start = strstr(line_buffer, "password=");
                        
                        if (connected && pwd_start) {
                            // Extract SSID (skip "connected to SSID=" and possible quote)
                            char captured_ssid[64] = {0};
                            char *ssid_start = connected + 18;  // Skip "connected to SSID="
                            if (*ssid_start == '\'') ssid_start++;  // Skip opening quote
                            char *ssid_end = strstr(ssid_start, "' with");
                            if (!ssid_end) ssid_end = strstr(ssid_start, " with");
                            if (ssid_end) {
                                int ssid_len = ssid_end - ssid_start;
                                if (ssid_len > 63) ssid_len = 63;
                                strncpy(captured_ssid, ssid_start, ssid_len);
                            }
                            
                            // Extract password (skip "password=" and possible quote)
                            char captured_pwd[128] = {0};
                            pwd_start += 9;  // Skip "password="
                            if (*pwd_start == '\'') pwd_start++;  // Skip opening quote
                            // Find end - either closing quote or end of line
                            int pwd_len = 0;
                            while (pwd_start[pwd_len] && pwd_start[pwd_len] != '\'' && pwd_start[pwd_len] != '\n' && pwd_start[pwd_len] != '\r') {
                                pwd_len++;
                            }
                            if (pwd_len > 127) pwd_len = 127;
                            strncpy(captured_pwd, pwd_start, pwd_len);
                            
                            ESP_LOGI(TAG, "PASSWORD CAPTURED! SSID: %s, Password: %s", captured_ssid, captured_pwd);
                            
                            // Update UI on main thread
                            if (evil_twin_status_label) {
                                char result_text[512];
                                snprintf(result_text, sizeof(result_text),
                                    "PASSWORD CAPTURED!\n\n"
                                    "SSID: %s\n"
                                    "Password: %s",
                                    captured_ssid, captured_pwd);
                                lv_label_set_text(evil_twin_status_label, result_text);
                                lv_obj_set_style_text_color(evil_twin_status_label, COLOR_MATERIAL_GREEN, 0);
                            }
                            
                            // Show close button
                            if (evil_twin_close_btn) {
                                lv_obj_clear_flag(evil_twin_close_btn, LV_OBJ_FLAG_HIDDEN);
                            }
                            
                            // Stop monitoring
                            evil_twin_monitoring = false;
                            break;
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "Evil Twin monitor task ended");
    evil_twin_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

// Evil Twin start button callback
static void evil_twin_start_cb(lv_event_t *e)
{
    (void)e;
    
    if (!evil_twin_network_dropdown || !evil_twin_html_dropdown) return;
    
    // Get selected network index from dropdown
    int selected_dropdown_idx = lv_dropdown_get_selected(evil_twin_network_dropdown);
    
    // Get selected HTML file index from dropdown
    int selected_html_idx = lv_dropdown_get_selected(evil_twin_html_dropdown);
    
    if (selected_dropdown_idx < 0 || selected_dropdown_idx >= selected_network_count) {
        ESP_LOGW(TAG, "Invalid network selection");
        return;
    }
    
    if (selected_html_idx < 0 || selected_html_idx >= evil_twin_html_count) {
        ESP_LOGW(TAG, "Invalid HTML file selection");
        return;
    }
    
    // Get the actual network index for evil twin (0-based in our array)
    int evil_twin_net_idx = selected_network_indices[selected_dropdown_idx];
    int evil_twin_1based = networks[evil_twin_net_idx].index;  // 1-based for UART
    
    // Build select_networks command: evil twin first, then others (no duplicates)
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "select_networks %d", evil_twin_1based);
    
    for (int i = 0; i < selected_network_count; i++) {
        int idx = selected_network_indices[i];
        int net_1based = networks[idx].index;
        if (net_1based != evil_twin_1based) {  // Skip duplicate
            char num[8];
            snprintf(num, sizeof(num), " %d", net_1based);
            strncat(cmd, num, sizeof(cmd) - strlen(cmd) - 1);
        }
    }
    
    ESP_LOGI(TAG, "Evil Twin: sending %s", cmd);
    uart_send_command(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Send select_html command (1-based index)
    char html_cmd[32];
    snprintf(html_cmd, sizeof(html_cmd), "select_html %d", selected_html_idx + 1);
    ESP_LOGI(TAG, "Evil Twin: sending %s", html_cmd);
    uart_send_command(html_cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Send start_evil_twin
    ESP_LOGI(TAG, "Evil Twin: sending start_evil_twin");
    uart_send_command("start_evil_twin");
    
    // Build status text
    wifi_network_t *et_net = &networks[evil_twin_net_idx];
    const char *et_ssid = strlen(et_net->ssid) > 0 ? et_net->ssid : "(Hidden)";
    const char *html_file = evil_twin_html_files[selected_html_idx];
    
    char status_text[512];
    int pos = snprintf(status_text, sizeof(status_text),
        "Attacking networks:\n");
    
    for (int i = 0; i < selected_network_count; i++) {
        int idx = selected_network_indices[i];
        wifi_network_t *net = &networks[idx];
        const char *ssid = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
        pos += snprintf(status_text + pos, sizeof(status_text) - pos,
            "  - %s (%s)\n", ssid, net->bssid);
    }
    
    pos += snprintf(status_text + pos, sizeof(status_text) - pos,
        "\nEvil Twin network: %s\n"
        "Portal: %s\n\n"
        "Waiting for victim to connect...",
        et_ssid, html_file);
    
    if (evil_twin_status_label) {
        lv_label_set_text(evil_twin_status_label, status_text);
    }
    
    // Start monitoring task
    evil_twin_monitoring = true;
    xTaskCreate(evil_twin_monitor_task, "et_monitor", 4096, NULL, 5, &evil_twin_monitor_task_handle);
}

// Show Evil Twin popup with dropdowns
static void show_evil_twin_popup(void)
{
    if (evil_twin_popup_obj != NULL) return;  // Already showing
    
    // First fetch HTML files from SD
    fetch_html_files_from_sd();
    
    if (evil_twin_html_count == 0) {
        ESP_LOGW(TAG, "No HTML files found on SD card");
        // Could show an error message here
        return;
    }
    
    lv_obj_t *scr = lv_scr_act();
    
    // Create modal overlay (full screen, semi-transparent, blocks input behind)
    evil_twin_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(evil_twin_overlay);
    lv_obj_set_size(evil_twin_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(evil_twin_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(evil_twin_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(evil_twin_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(evil_twin_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    evil_twin_popup_obj = lv_obj_create(evil_twin_overlay);
    lv_obj_set_size(evil_twin_popup_obj, 600, 550);
    lv_obj_center(evil_twin_popup_obj);
    lv_obj_set_style_bg_color(evil_twin_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(evil_twin_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(evil_twin_popup_obj, 2, 0);
    lv_obj_set_style_radius(evil_twin_popup_obj, 16, 0);
    lv_obj_set_style_shadow_width(evil_twin_popup_obj, 30, 0);
    lv_obj_set_style_shadow_color(evil_twin_popup_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(evil_twin_popup_obj, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(evil_twin_popup_obj, 16, 0);
    lv_obj_set_flex_flow(evil_twin_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(evil_twin_popup_obj, 12, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(evil_twin_popup_obj);
    lv_label_set_text(title, "Evil Twin Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Network dropdown container
    lv_obj_t *net_cont = lv_obj_create(evil_twin_popup_obj);
    lv_obj_set_size(net_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(net_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(net_cont, 0, 0);
    lv_obj_set_style_pad_all(net_cont, 0, 0);
    lv_obj_set_flex_flow(net_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(net_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(net_cont, 10, 0);
    lv_obj_clear_flag(net_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *net_label = lv_label_create(net_cont);
    lv_label_set_text(net_label, "Evil Twin Network:");
    lv_obj_set_style_text_font(net_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(net_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(net_label, 180);
    
    evil_twin_network_dropdown = lv_dropdown_create(net_cont);
    lv_obj_set_width(evil_twin_network_dropdown, 350);
    lv_obj_set_style_bg_color(evil_twin_network_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(evil_twin_network_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(evil_twin_network_dropdown, lv_color_hex(0x555555), 0);
    
    // Build network dropdown options from selected networks
    char network_options[1024] = "";
    for (int i = 0; i < selected_network_count; i++) {
        int idx = selected_network_indices[i];
        if (idx >= 0 && idx < network_count) {
            const char *ssid = strlen(networks[idx].ssid) > 0 ? networks[idx].ssid : "(Hidden)";
            if (i > 0) strncat(network_options, "\n", sizeof(network_options) - strlen(network_options) - 1);
            strncat(network_options, ssid, sizeof(network_options) - strlen(network_options) - 1);
        }
    }
    lv_dropdown_set_options(evil_twin_network_dropdown, network_options);
    
    // Style dropdown list (dark background when opened)
    lv_obj_t *net_list = lv_dropdown_get_list(evil_twin_network_dropdown);
    if (net_list) {
        lv_obj_set_style_bg_color(net_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(net_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(net_list, lv_color_hex(0x555555), 0);
    }
    
    // HTML dropdown container
    lv_obj_t *html_cont = lv_obj_create(evil_twin_popup_obj);
    lv_obj_set_size(html_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(html_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(html_cont, 0, 0);
    lv_obj_set_style_pad_all(html_cont, 0, 0);
    lv_obj_set_flex_flow(html_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(html_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(html_cont, 10, 0);
    lv_obj_clear_flag(html_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *html_label = lv_label_create(html_cont);
    lv_label_set_text(html_label, "Portal HTML:");
    lv_obj_set_style_text_font(html_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(html_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(html_label, 180);
    
    evil_twin_html_dropdown = lv_dropdown_create(html_cont);
    lv_obj_set_width(evil_twin_html_dropdown, 350);
    lv_obj_set_style_bg_color(evil_twin_html_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(evil_twin_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(evil_twin_html_dropdown, lv_color_hex(0x555555), 0);
    
    // Build HTML dropdown options
    char html_options[2048] = "";
    for (int i = 0; i < evil_twin_html_count; i++) {
        if (i > 0) strncat(html_options, "\n", sizeof(html_options) - strlen(html_options) - 1);
        strncat(html_options, evil_twin_html_files[i], sizeof(html_options) - strlen(html_options) - 1);
    }
    lv_dropdown_set_options(evil_twin_html_dropdown, html_options);
    
    // Style dropdown list (dark background when opened)
    lv_obj_t *html_list = lv_dropdown_get_list(evil_twin_html_dropdown);
    if (html_list) {
        lv_obj_set_style_bg_color(html_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(html_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(html_list, lv_color_hex(0x555555), 0);
    }
    
    // START ATTACK button
    lv_obj_t *start_btn = lv_btn_create(evil_twin_popup_obj);
    lv_obj_set_size(start_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(start_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0xCC7000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(start_btn, 8, 0);
    lv_obj_add_event_cb(start_btn, evil_twin_start_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START ATTACK");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_18, 0);
    lv_obj_center(start_label);
    
    // Status label (scrollable area)
    lv_obj_t *status_cont = lv_obj_create(evil_twin_popup_obj);
    lv_obj_set_size(status_cont, lv_pct(100), 200);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(status_cont, 0, 0);
    lv_obj_set_style_radius(status_cont, 8, 0);
    lv_obj_set_style_pad_all(status_cont, 12, 0);
    lv_obj_add_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    evil_twin_status_label = lv_label_create(status_cont);
    lv_label_set_text(evil_twin_status_label, "Select network and portal, then click START ATTACK");
    lv_obj_set_style_text_font(evil_twin_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(evil_twin_status_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(evil_twin_status_label, lv_pct(100));
    lv_label_set_long_mode(evil_twin_status_label, LV_LABEL_LONG_WRAP);
    
    // CLOSE button (hidden initially, shown when password captured)
    evil_twin_close_btn = lv_btn_create(evil_twin_popup_obj);
    lv_obj_set_size(evil_twin_close_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(evil_twin_close_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(evil_twin_close_btn, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
    lv_obj_set_style_radius(evil_twin_close_btn, 8, 0);
    lv_obj_add_event_cb(evil_twin_close_btn, evil_twin_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(evil_twin_close_btn, LV_OBJ_FLAG_HIDDEN);  // Hidden initially
    
    lv_obj_t *close_label = lv_label_create(evil_twin_close_btn);
    lv_label_set_text(close_label, "CLOSE");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_18, 0);
    lv_obj_center(close_label);
    
    // STOP button (always visible - sends stop command and closes popup)
    lv_obj_t *stop_btn = lv_btn_create(evil_twin_popup_obj);
    lv_obj_set_size(stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xB71C1C), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, evil_twin_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
}

// Back button click handler
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back button clicked");
    show_main_tiles();
}

// Show main tiles screen (7 tiles)
static void show_main_tiles(void)
{
    // Delete scan page if present
    if (scan_page) {
        lv_obj_del(scan_page);
        scan_page = NULL;
        // Reset scan page child pointers
        scan_btn = NULL;
        status_label = NULL;
        network_list = NULL;
        spinner = NULL;
    }
    
    // Delete observer page if present
    if (observer_page) {
        lv_obj_del(observer_page);
        observer_page = NULL;
        // Reset observer page child pointers
        observer_start_btn = NULL;
        observer_stop_btn = NULL;
        observer_table = NULL;
        observer_status_label = NULL;
    }
    
    // Delete ESP Modem page if present
    if (esp_modem_page) {
        lv_obj_del(esp_modem_page);
        esp_modem_page = NULL;
        // Reset ESP Modem page child pointers
        esp_modem_scan_btn = NULL;
        esp_modem_status_label = NULL;
        esp_modem_network_list = NULL;
        esp_modem_spinner = NULL;
    }
    
    // Delete Global Attacks page if present
    if (global_attacks_page) {
        lv_obj_del(global_attacks_page);
        global_attacks_page = NULL;
    }
    
    // Delete existing tiles container if present
    if (tiles_container) {
        lv_obj_del(tiles_container);
        tiles_container = NULL;
    }
    
    lv_obj_t *scr = lv_scr_act();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, COLOR_MATERIAL_BG, 0);
    
    // Create status bar using helper
    create_status_bar();
    
    // Create tiles container below the status bar
    tiles_container = lv_obj_create(scr);
    lv_coord_t tiles_scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(tiles_container, lv_pct(100), tiles_scr_height - 40);  // Subtract status bar height
    lv_obj_align(tiles_container, LV_ALIGN_TOP_MID, 0, 40);  // Position below status bar
    lv_obj_set_style_bg_color(tiles_container, COLOR_MATERIAL_BG, 0);
    lv_obj_set_style_border_width(tiles_container, 0, 0);
    lv_obj_set_style_radius(tiles_container, 0, 0);
    lv_obj_set_style_pad_all(tiles_container, 20, 0);
    lv_obj_set_style_pad_gap(tiles_container, 20, 0);
    lv_obj_set_flex_flow(tiles_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tiles_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create 8 main tiles with Material colors
    create_tile(tiles_container, LV_SYMBOL_WIFI, "WiFi Scan\n& Attack", COLOR_MATERIAL_BLUE, main_tile_event_cb, "WiFi Scan & Attack");
    create_tile(tiles_container, LV_SYMBOL_WARNING, "Global WiFi\nAttacks", COLOR_MATERIAL_RED, main_tile_event_cb, "Global WiFi Attacks");
    create_tile(tiles_container, LV_SYMBOL_EYE_OPEN, "WiFi Sniff\n& Karma", COLOR_MATERIAL_PURPLE, main_tile_event_cb, "WiFi Sniff & Karma");
    create_tile(tiles_container, LV_SYMBOL_SETTINGS, "WiFi\nMonitor", COLOR_MATERIAL_GREEN, main_tile_event_cb, "WiFi Monitor");
    create_tile(tiles_container, LV_SYMBOL_GPS, "Deauth\nMonitor", COLOR_MATERIAL_AMBER, main_tile_event_cb, "Deauth Monitor");
    create_tile(tiles_container, LV_SYMBOL_BLUETOOTH, "Bluetooth", COLOR_MATERIAL_CYAN, main_tile_event_cb, "Bluetooth");
    create_tile(tiles_container, LV_SYMBOL_LOOP, "Network\nObserver", COLOR_MATERIAL_TEAL, main_tile_event_cb, "Network Observer");
    create_tile(tiles_container, LV_SYMBOL_CHARGE, "Internal\nC6", lv_color_make(255, 87, 34), main_tile_event_cb, "Internal C6");  // Deep Orange
}

// Show WiFi Scanner page with Back button
static void show_scan_page(void)
{
    // Delete tiles container if present
    if (tiles_container) {
        lv_obj_del(tiles_container);
        tiles_container = NULL;
    }
    
    // Delete existing scan page if present
    if (scan_page) {
        lv_obj_del(scan_page);
        scan_page = NULL;
    }
    
    lv_obj_t *scr = lv_scr_act();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A1A), 0);
    
    // Create/update status bar
    create_status_bar();
    
    // Create scan page container below status bar
    scan_page = lv_obj_create(scr);
    lv_coord_t scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(scan_page, lv_pct(100), scr_height - 40);  // Full height minus status bar
    lv_obj_align(scan_page, LV_ALIGN_TOP_MID, 0, 40);  // Position below status bar
    lv_obj_set_style_bg_color(scan_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(scan_page, 0, 0);
    lv_obj_set_style_pad_all(scan_page, 4, 0);  // Minimal padding for maximum list space
    lv_obj_set_flex_flow(scan_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scan_page, 2, 0);  // Minimal gap
    
    // Header container
    lv_obj_t *header = lv_obj_create(scan_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Left side: Back button + Title
    lv_obj_t *left_cont = lv_obj_create(header);
    lv_obj_set_size(left_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 0, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_cont, 12, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(left_cont);
    lv_obj_set_size(back_btn, 48, 40);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(left_cont);
    lv_label_set_text(title, "Scan & Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_BLUE, 0);
    
    // Scan button container (for button + spinner)
    lv_obj_t *btn_cont = lv_obj_create(header);
    lv_obj_set_size(btn_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_cont, 12, 0);
    
    // Spinner (hidden by default)
    spinner = lv_spinner_create(btn_cont);
    lv_obj_set_size(spinner, 32, 32);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    
    // Scan button
    scan_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(scan_btn, 120, 40);
    lv_obj_set_style_bg_color(scan_btn, COLOR_MATERIAL_BLUE, 0);
    lv_obj_set_style_bg_color(scan_btn, lv_color_lighten(COLOR_MATERIAL_BLUE, 30), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x444444), LV_STATE_DISABLED);
    lv_obj_set_style_radius(scan_btn, 8, 0);
    lv_obj_add_event_cb(scan_btn, scan_btn_click_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(scan_btn);
    lv_label_set_text(btn_label, "RESCAN");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);
    
    // Status label (compact)
    status_label = lv_label_create(scan_page);
    lv_label_set_text(status_label, "Press RESCAN to search for networks");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    
    // Network list container (scrollable) - fills remaining space above attack bar
    network_list = lv_obj_create(scan_page);
    lv_obj_set_width(network_list, lv_pct(100));
    lv_obj_set_flex_grow(network_list, 1);  // Take all remaining vertical space
    lv_obj_set_style_bg_color(network_list, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(network_list, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(network_list, 1, 0);
    lv_obj_set_style_radius(network_list, 8, 0);
    lv_obj_set_style_pad_all(network_list, 6, 0);
    lv_obj_set_flex_flow(network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(network_list, 6, 0);
    lv_obj_set_scroll_dir(network_list, LV_DIR_VER);
    
    // Bottom icon bar for attack tiles
    lv_obj_t *attack_bar = lv_obj_create(scan_page);
    lv_obj_set_size(attack_bar, lv_pct(100), 70);
    lv_obj_set_style_bg_color(attack_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(attack_bar, 0, 0);
    lv_obj_set_style_pad_all(attack_bar, 5, 0);
    lv_obj_set_style_pad_gap(attack_bar, 8, 0);
    lv_obj_set_flex_flow(attack_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(attack_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(attack_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create attack tiles in the bottom bar
    create_small_tile(attack_bar, LV_SYMBOL_CHARGE, "Deauth", COLOR_MATERIAL_RED, attack_tile_event_cb, "Deauth");
    create_small_tile(attack_bar, LV_SYMBOL_WARNING, "EvilTwin", COLOR_MATERIAL_ORANGE, attack_tile_event_cb, "Evil Twin");
    create_small_tile(attack_bar, LV_SYMBOL_POWER, "SAE", COLOR_MATERIAL_PINK, attack_tile_event_cb, "SAE Overflow");
    create_small_tile(attack_bar, LV_SYMBOL_DOWNLOAD, "Handshake", COLOR_MATERIAL_AMBER, attack_tile_event_cb, "Handshaker");
    create_small_tile(attack_bar, LV_SYMBOL_EYE_OPEN, "Sniffer", COLOR_MATERIAL_PURPLE, attack_tile_event_cb, "Sniffer");
    
    // Auto-start scan when entering the page
    lv_obj_send_event(scan_btn, LV_EVENT_CLICKED, NULL);
}

// ======================= Network Observer Page =======================

// Forward declare popup poll task
static void popup_poll_task(void *arg);

// Popup timer callback - triggers poll task every 10s
static void popup_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    
    if (!popup_open || !observer_running) return;
    
    // Only start new poll if previous one finished
    if (observer_task_handle == NULL) {
        xTaskCreate(popup_poll_task, "popup_poll", 8192, NULL, 5, &observer_task_handle);
    }
}

// Update popup content with current network data
static void update_popup_content(void)
{
    if (!popup_obj || popup_network_idx < 0 || popup_network_idx >= observer_network_count) return;
    
    observer_network_t *net = &observer_networks[popup_network_idx];
    
    // Update clients container
    if (popup_clients_container) {
        lv_obj_clean(popup_clients_container);
        
        if (net->client_count == 0) {
            lv_obj_t *no_clients = lv_label_create(popup_clients_container);
            lv_label_set_text(no_clients, "No clients detected yet...");
            lv_obj_set_style_text_color(no_clients, lv_color_hex(0x666666), 0);
        } else {
            for (int j = 0; j < net->client_count && j < MAX_CLIENTS_PER_NETWORK; j++) {
                if (net->clients[j][0] != '\0') {
                    lv_obj_t *client_label = lv_label_create(popup_clients_container);
                    lv_label_set_text_fmt(client_label, "  %s", net->clients[j]);
                    lv_obj_set_style_text_font(client_label, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_color(client_label, lv_color_hex(0xAAAAAA), 0);
                }
            }
        }
    }
}

// Popup close button click handler
static void popup_close_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Popup close button clicked");
    close_network_popup();
}

// Close network popup and resume normal monitoring
static void close_network_popup(void)
{
    if (!popup_open) return;
    
    ESP_LOGI(TAG, "Closing network popup");
    
    // Stop popup timer
    if (popup_timer != NULL) {
        xTimerStop(popup_timer, 0);
    }
    
    // Send unselect_networks to monitor all networks again
    uart_send_command("unselect_networks");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Restart sniffer for all networks (without new scan)
    uart_send_command("start_sniffer_noscan");
    
    // Close popup UI
    if (popup_obj) {
        lv_obj_del(popup_obj);
        popup_obj = NULL;
        popup_clients_container = NULL;
    }
    
    popup_open = false;
    popup_network_idx = -1;
    
    // Restart main observer timer (20s)
    if (observer_timer != NULL && observer_running) {
        xTimerStart(observer_timer, 0);
        ESP_LOGI(TAG, "Resumed main observer timer (20s)");
    }
    
    // Refresh main table
    if (observer_table) {
        update_observer_table();
    }
}

// Show network popup for detailed view
static void show_network_popup(int network_idx)
{
    if (network_idx < 0 || network_idx >= observer_network_count) return;
    if (popup_open) return;  // Already showing a popup
    
    observer_network_t *net = &observer_networks[network_idx];
    ESP_LOGI(TAG, "Opening popup for network: %s (scan_index=%d)", net->ssid, net->scan_index);
    
    popup_open = true;
    popup_network_idx = network_idx;
    
    // Stop main observer timer
    if (observer_timer != NULL) {
        xTimerStop(observer_timer, 0);
        ESP_LOGI(TAG, "Stopped main observer timer");
    }
    
    // Send commands to focus on this network
    uart_send_command("stop");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Send select_networks with the scan index
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "select_networks %d", net->scan_index);
    uart_send_command(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Start sniffer for selected network only
    uart_send_command("start_sniffer");
    
    // Create popup overlay
    lv_obj_t *scr = lv_scr_act();
    popup_obj = lv_obj_create(scr);
    lv_obj_set_size(popup_obj, 600, 400);
    lv_obj_center(popup_obj);
    lv_obj_set_style_bg_color(popup_obj, lv_color_hex(0x1A2A2A), 0);
    lv_obj_set_style_border_color(popup_obj, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(popup_obj, 2, 0);
    lv_obj_set_style_radius(popup_obj, 16, 0);
    lv_obj_set_style_shadow_width(popup_obj, 30, 0);
    lv_obj_set_style_shadow_color(popup_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(popup_obj, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(popup_obj, 16, 0);
    lv_obj_set_flex_flow(popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(popup_obj, 8, 0);
    
    // Header with title and close button
    lv_obj_t *header = lv_obj_create(popup_obj);
    lv_obj_set_size(header, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "Unknown";
    lv_label_set_text_fmt(title, "Scanning only %s", ssid_display);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Close button (X)
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, popup_close_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(close_icon);
    
    // Network info section
    lv_obj_t *info_container = lv_obj_create(popup_obj);
    lv_obj_set_size(info_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_width(info_container, 0, 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_set_style_pad_all(info_container, 12, 0);
    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info_container, 4, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // SSID
    lv_obj_t *ssid_label = lv_label_create(info_container);
    lv_label_set_text_fmt(ssid_label, "SSID: %s", ssid_display);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
    
    // BSSID
    lv_obj_t *bssid_label = lv_label_create(info_container);
    lv_label_set_text_fmt(bssid_label, "BSSID: %s", net->bssid);
    lv_obj_set_style_text_font(bssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bssid_label, lv_color_hex(0xCCCCCC), 0);
    
    // Channel + Band + RSSI
    lv_obj_t *channel_label = lv_label_create(info_container);
    lv_label_set_text_fmt(channel_label, "Channel: %d  |  %s  |  %d dBm", net->channel, net->band, net->rssi);
    lv_obj_set_style_text_font(channel_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(channel_label, lv_color_hex(0xCCCCCC), 0);
    
    // Clients section header
    lv_obj_t *clients_header = lv_label_create(popup_obj);
    lv_label_set_text_fmt(clients_header, "Clients (%d):", net->client_count);
    lv_obj_set_style_text_font(clients_header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(clients_header, COLOR_MATERIAL_TEAL, 0);
    
    // Clients scrollable container
    popup_clients_container = lv_obj_create(popup_obj);
    lv_obj_set_size(popup_clients_container, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(popup_clients_container, 1);
    lv_obj_set_style_bg_color(popup_clients_container, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_width(popup_clients_container, 0, 0);
    lv_obj_set_style_radius(popup_clients_container, 8, 0);
    lv_obj_set_style_pad_all(popup_clients_container, 8, 0);
    lv_obj_set_flex_flow(popup_clients_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(popup_clients_container, 4, 0);
    lv_obj_set_scroll_dir(popup_clients_container, LV_DIR_VER);
    
    // Initial client list
    update_popup_content();
    
    // Create and start popup timer (10s polling)
    if (popup_timer == NULL) {
        popup_timer = xTimerCreate("popup_timer", 
                                   pdMS_TO_TICKS(POPUP_POLL_INTERVAL_MS),
                                   pdTRUE,  // Auto-reload
                                   NULL,
                                   popup_timer_callback);
    }
    
    if (popup_timer != NULL) {
        xTimerStart(popup_timer, 0);
        ESP_LOGI(TAG, "Started popup timer (10s polling)");
        
        // Do first poll after a short delay
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (popup_open && observer_task_handle == NULL) {
            xTaskCreate(popup_poll_task, "popup_poll", 8192, NULL, 5, &observer_task_handle);
        }
    }
}

// Helper: Check if MAC already exists in network's client list
static bool client_mac_exists(observer_network_t *net, const char *mac)
{
    for (int i = 0; i < MAX_CLIENTS_PER_NETWORK; i++) {
        if (net->clients[i][0] != '\0' && strcmp(net->clients[i], mac) == 0) {
            return true;
        }
    }
    return false;
}

// Helper: Add client MAC to network if not already present, returns true if added
static bool add_client_mac(observer_network_t *net, const char *mac)
{
    // Check if already exists
    if (client_mac_exists(net, mac)) {
        return false;
    }
    
    // Find empty slot
    for (int i = 0; i < MAX_CLIENTS_PER_NETWORK; i++) {
        if (net->clients[i][0] == '\0') {
            strncpy(net->clients[i], mac, sizeof(net->clients[i]) - 1);
            net->clients[i][sizeof(net->clients[i]) - 1] = '\0';
            net->client_count++;
            return true;
        }
    }
    return false;  // No room
}

// Popup poll task - similar to observer_poll_task but updates popup content
static void popup_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Popup poll task started for network idx %d", popup_network_idx);
    
    if (!observer_rx_buffer || !observer_line_buffer || !observer_networks) {
        ESP_LOGE(TAG, "PSRAM buffers not allocated!");
        observer_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    uart_flush(UART_NUM);
    uart_send_command("show_sniffer_results");
    
    char *rx_buffer = observer_rx_buffer;
    char *line_buffer = observer_line_buffer;
    int line_pos = 0;
    int current_network_idx = -1;
    
    // DON'T clear client data - accumulate clients over time
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        ESP_LOGD(TAG, "POPUP SNIFFER LINE: '%s'", line_buffer);
                        
                        // Check for network line (doesn't start with space)
                        if (line_buffer[0] != ' ' && line_buffer[0] != '\t') {
                            observer_network_t parsed_net = {0};
                            if (parse_sniffer_network_line(line_buffer, &parsed_net)) {
                                // Find this network in our existing list by SSID
                                current_network_idx = -1;
                                for (int n = 0; n < observer_network_count; n++) {
                                    if (strcmp(observer_networks[n].ssid, parsed_net.ssid) == 0) {
                                        current_network_idx = n;
                                        // Don't overwrite client_count - we track it via add_client_mac
                                        break;
                                    }
                                }
                            } else {
                                current_network_idx = -1;
                            }
                        }
                        // Check for client MAC line (starts with space)
                        else if ((line_buffer[0] == ' ' || line_buffer[0] == '\t') && current_network_idx >= 0) {
                            observer_network_t *net = &observer_networks[current_network_idx];
                            char mac[18];
                            if (parse_sniffer_client_line(line_buffer, mac, sizeof(mac))) {
                                // Add client if not already present (accumulate)
                                if (add_client_mac(net, mac)) {
                                    ESP_LOGI(TAG, "  -> NEW client: %s for '%s'", mac, net->ssid);
                                }
                            }
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < OBSERVER_LINE_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
        
        if (!popup_open) {
            ESP_LOGI(TAG, "Popup closed during poll");
            break;
        }
    }
    
    // Update popup UI
    if (popup_open) {
        bsp_display_lock(0);
        update_popup_content();
        bsp_display_unlock();
    }
    
    ESP_LOGI(TAG, "Popup poll task finished");
    observer_task_handle = NULL;
    vTaskDelete(NULL);
}

// Update observer table UI with current data
static void update_observer_table(void)
{
    if (!observer_table) return;
    
    lv_obj_clean(observer_table);
    
    for (int i = 0; i < observer_network_count; i++) {
        observer_network_t *net = &observer_networks[i];
        
        // Create network row (darker background, clickable) - 2 lines like WiFi Scanner
        lv_obj_t *net_row = lv_obj_create(observer_table);
        lv_obj_set_size(net_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(net_row, 8, 0);
        lv_obj_set_style_bg_color(net_row, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_bg_color(net_row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(net_row, 0, 0);
        lv_obj_set_style_radius(net_row, 8, 0);
        lv_obj_set_flex_flow(net_row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(net_row, 4, 0);
        lv_obj_clear_flag(net_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(net_row, LV_OBJ_FLAG_CLICKABLE);
        
        // Add click event with network index as user data
        lv_obj_add_event_cb(net_row, network_row_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        
        // First row: SSID (or "Hidden") + client count
        lv_obj_t *ssid_label = lv_label_create(net_row);
        if (strlen(net->ssid) > 0) {
            if (net->client_count > 0) {
                lv_label_set_text_fmt(ssid_label, "%s  (%d clients)", net->ssid, net->client_count);
            } else {
                lv_label_set_text(ssid_label, net->ssid);
            }
        } else {
            if (net->client_count > 0) {
                lv_label_set_text_fmt(ssid_label, "(Hidden)  (%d clients)", net->client_count);
            } else {
                lv_label_set_text(ssid_label, "(Hidden)");
            }
        }
        lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
        
        // Second row: BSSID | Band | RSSI
        lv_obj_t *info_label = lv_label_create(net_row);
        lv_label_set_text_fmt(info_label, "%s  |  %s  |  %d dBm", 
                              net->bssid, net->band, net->rssi);
        lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), 0);
        
        // Create client rows (indented, lighter background, clickable)
        for (int j = 0; j < MAX_CLIENTS_PER_NETWORK; j++) {
            if (net->clients[j][0] == '\0') continue;
            
            lv_obj_t *client_row = lv_obj_create(observer_table);
            lv_obj_set_size(client_row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(client_row, 6, 0);
            lv_obj_set_style_pad_left(client_row, 32, 0);  // Indent
            lv_obj_set_style_bg_color(client_row, lv_color_hex(0x1E2828), 0);
            lv_obj_set_style_bg_color(client_row, lv_color_hex(0x2E3838), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(client_row, 0, 0);
            lv_obj_set_style_radius(client_row, 4, 0);
            lv_obj_clear_flag(client_row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(client_row, LV_OBJ_FLAG_CLICKABLE);
            
            // Pack network_idx and client_idx into user_data: (network_idx << 16) | client_idx
            intptr_t packed_data = ((intptr_t)i << 16) | (intptr_t)j;
            lv_obj_add_event_cb(client_row, client_row_click_cb, LV_EVENT_CLICKED, (void*)packed_data);
            
            lv_obj_t *mac_label = lv_label_create(client_row);
            lv_label_set_text(mac_label, net->clients[j]);
            lv_obj_set_style_text_font(mac_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mac_label, COLOR_MATERIAL_TEAL, 0);
        }
    }
}

// Network row click handler
static void network_row_click_cb(lv_event_t *e)
{
    int network_idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Network row clicked: index %d", network_idx);
    
    if (network_idx >= 0 && network_idx < observer_network_count) {
        show_network_popup(network_idx);
    }
}

// Client row click handler - opens deauth popup
static void client_row_click_cb(lv_event_t *e)
{
    intptr_t packed_data = (intptr_t)lv_event_get_user_data(e);
    int network_idx = (int)(packed_data >> 16);
    int client_idx = (int)(packed_data & 0xFFFF);
    
    ESP_LOGI(TAG, "Client row clicked: network=%d, client=%d", network_idx, client_idx);
    
    if (network_idx >= 0 && network_idx < observer_network_count &&
        client_idx >= 0 && client_idx < MAX_CLIENTS_PER_NETWORK) {
        show_deauth_popup(network_idx, client_idx);
    }
}

// Show deauth popup for a specific client
static void show_deauth_popup(int network_idx, int client_idx)
{
    if (network_idx < 0 || network_idx >= observer_network_count) return;
    if (deauth_popup_obj != NULL) return;  // Already showing a popup
    
    observer_network_t *net = &observer_networks[network_idx];
    if (net->clients[client_idx][0] == '\0') return;
    
    const char *client_mac = net->clients[client_idx];
    ESP_LOGI(TAG, "Opening deauth popup for client: %s on network: %s", client_mac, net->ssid);
    
    deauth_network_idx = network_idx;
    deauth_client_idx = client_idx;
    deauth_active = false;  // Not yet deauthing
    
    // Stop main observer timer
    if (observer_timer != NULL) {
        xTimerStop(observer_timer, 0);
        ESP_LOGI(TAG, "Stopped main observer timer for deauth popup");
    }
    
    // Create popup overlay
    lv_obj_t *scr = lv_scr_act();
    deauth_popup_obj = lv_obj_create(scr);
    lv_obj_set_size(deauth_popup_obj, 550, 320);
    lv_obj_center(deauth_popup_obj);
    lv_obj_set_style_bg_color(deauth_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(deauth_popup_obj, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(deauth_popup_obj, 2, 0);
    lv_obj_set_style_radius(deauth_popup_obj, 16, 0);
    lv_obj_set_style_shadow_width(deauth_popup_obj, 30, 0);
    lv_obj_set_style_shadow_color(deauth_popup_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(deauth_popup_obj, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(deauth_popup_obj, 16, 0);
    lv_obj_set_flex_flow(deauth_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(deauth_popup_obj, 12, 0);
    
    // Header with title and close button
    lv_obj_t *header = lv_obj_create(deauth_popup_obj);
    lv_obj_set_size(header, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Deauth Station");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Close button (X)
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, deauth_btn_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);  // 1 = close button
    
    lv_obj_t *close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(close_icon);
    
    // Network info section
    lv_obj_t *info_container = lv_obj_create(deauth_popup_obj);
    lv_obj_set_size(info_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(info_container, 0, 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_set_style_pad_all(info_container, 12, 0);
    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info_container, 4, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // SSID
    const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
    lv_obj_t *ssid_label = lv_label_create(info_container);
    lv_label_set_text_fmt(ssid_label, "Network: %s", ssid_display);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
    
    // BSSID + Channel
    lv_obj_t *bssid_label = lv_label_create(info_container);
    lv_label_set_text_fmt(bssid_label, "BSSID: %s  |  CH%d", net->bssid, net->channel);
    lv_obj_set_style_text_font(bssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bssid_label, lv_color_hex(0xAAAAAA), 0);
    
    // Client MAC (highlighted)
    lv_obj_t *client_label = lv_label_create(info_container);
    lv_label_set_text_fmt(client_label, "Station: %s", client_mac);
    lv_obj_set_style_text_font(client_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(client_label, COLOR_MATERIAL_RED, 0);
    
    // Deauth button (red)
    deauth_btn = lv_btn_create(deauth_popup_obj);
    lv_obj_set_size(deauth_btn, lv_pct(100), 60);
    lv_obj_set_style_bg_color(deauth_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(deauth_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(deauth_btn, 12, 0);
    lv_obj_add_event_cb(deauth_btn, deauth_btn_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)0);  // 0 = deauth/stop button
    
    deauth_btn_label = lv_label_create(deauth_btn);
    lv_label_set_text(deauth_btn_label, "Deauth Station");
    lv_obj_set_style_text_font(deauth_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(deauth_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(deauth_btn_label);
}

// Close deauth popup and cleanup
static void close_deauth_popup(void)
{
    ESP_LOGI(TAG, "Closing deauth popup");
    
    // Send stop commands
    uart_send_command("stop");
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_send_command("start_sniffer_noscan");
    
    // Delete popup UI
    if (deauth_popup_obj != NULL) {
        lv_obj_del(deauth_popup_obj);
        deauth_popup_obj = NULL;
    }
    deauth_btn = NULL;
    deauth_btn_label = NULL;
    deauth_active = false;
    deauth_network_idx = -1;
    deauth_client_idx = -1;
    
    // Resume main observer timer
    if (observer_timer != NULL) {
        xTimerStart(observer_timer, 0);
        ESP_LOGI(TAG, "Resumed main observer timer");
    }
}

// Deauth button click handler
static void deauth_btn_click_cb(lv_event_t *e)
{
    intptr_t btn_type = (intptr_t)lv_event_get_user_data(e);
    
    if (btn_type == 1) {
        // Close button (X) clicked
        close_deauth_popup();
        return;
    }
    
    // Deauth/Stop button clicked
    if (!deauth_active) {
        // Start deauth
        if (deauth_network_idx >= 0 && deauth_network_idx < observer_network_count &&
            deauth_client_idx >= 0 && deauth_client_idx < MAX_CLIENTS_PER_NETWORK) {
            
            observer_network_t *net = &observer_networks[deauth_network_idx];
            const char *client_mac = net->clients[deauth_client_idx];
            
            ESP_LOGI(TAG, "Starting deauth: network=%d (scan_idx=%d), client=%s", 
                     deauth_network_idx, net->scan_index, client_mac);
            
            // Send UART commands
            uart_send_command("stop");
            vTaskDelay(pdMS_TO_TICKS(100));
            
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "select_networks %d", net->scan_index);
            uart_send_command(cmd);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            snprintf(cmd, sizeof(cmd), "select_stations %s", client_mac);
            uart_send_command(cmd);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            uart_send_command("start_deauth");
            
            // Change button to STOP
            deauth_active = true;
            if (deauth_btn_label != NULL) {
                lv_label_set_text(deauth_btn_label, "STOP");
            }
            ESP_LOGI(TAG, "Deauth started");
        }
    } else {
        // Stop deauth and return to sniffer
        ESP_LOGI(TAG, "Stopping deauth");
        close_deauth_popup();
    }
}

// Parse sniffer output line - returns true if network line parsed
static bool parse_sniffer_network_line(const char *line, observer_network_t *net)
{
    // Format: "SSID, CHxx: count" or "Unknown_XXXX, CHxx: count"
    // Line should NOT start with space (those are client MACs)
    if (line[0] == ' ' || line[0] == '\t') return false;
    
    // Find ", CH" marker
    const char *ch_marker = strstr(line, ", CH");
    if (!ch_marker) return false;
    
    // Extract SSID (everything before ", CH")
    int ssid_len = ch_marker - line;
    if (ssid_len >= (int)sizeof(net->ssid)) ssid_len = sizeof(net->ssid) - 1;
    strncpy(net->ssid, line, ssid_len);
    net->ssid[ssid_len] = '\0';
    
    // Parse channel and client count: "CHxx: count"
    int channel = 0, count = 0;
    if (sscanf(ch_marker, ", CH%d: %d", &channel, &count) == 2) {
        net->channel = channel;
        net->client_count = count;
        return true;
    }
    
    return false;
}

// Parse client MAC line (starts with space)
static bool parse_sniffer_client_line(const char *line, char *mac_out, size_t mac_size)
{
    // Line starts with space followed by MAC address
    if (line[0] != ' ') return false;
    
    // Skip leading whitespace
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    
    // Check if it looks like a MAC address (XX:XX:XX:XX:XX:XX)
    if (strlen(p) >= 17 && p[2] == ':' && p[5] == ':') {
        strncpy(mac_out, p, mac_size - 1);
        mac_out[mac_size - 1] = '\0';
        // Trim trailing whitespace/newline
        char *end = mac_out + strlen(mac_out) - 1;
        while (end > mac_out && (*end == ' ' || *end == '\n' || *end == '\r')) {
            *end = '\0';
            end--;
        }
        return true;
    }
    
    return false;
}

// Observer poll task - runs show_sniffer_results and parses output
static void observer_poll_task(void *arg)
{
    ESP_LOGI(TAG, "Observer poll task started");
    
    // Check if PSRAM buffers are allocated
    if (!observer_rx_buffer || !observer_line_buffer || !observer_networks) {
        ESP_LOGE(TAG, "PSRAM buffers not allocated!");
        observer_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Flush UART buffer
    uart_flush(UART_NUM);
    
    // Send show_sniffer_results command
    uart_send_command("show_sniffer_results");
    
    // Use PSRAM-allocated buffers
    char *rx_buffer = observer_rx_buffer;
    char *line_buffer = observer_line_buffer;
    int line_pos = 0;
    
    // Track current network being updated (index into observer_networks)
    int current_network_idx = -1;
    
    // DON'T clear client data - accumulate clients over time
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000);  // 5 second timeout for response
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGD(TAG, "Observer line: %s", line_buffer);
                        
                        // Log every line received for debugging
                        ESP_LOGI(TAG, "SNIFFER LINE: '%s'", line_buffer);
                        
                        // Check for network line (doesn't start with space)
                        if (line_buffer[0] != ' ' && line_buffer[0] != '\t') {
                            observer_network_t parsed_net = {0};
                            if (parse_sniffer_network_line(line_buffer, &parsed_net)) {
                                // Find this network in our existing list by SSID
                                current_network_idx = -1;
                                for (int n = 0; n < observer_network_count; n++) {
                                    if (strcmp(observer_networks[n].ssid, parsed_net.ssid) == 0) {
                                        current_network_idx = n;
                                        // Don't overwrite client_count - we track it via add_client_mac
                                        ESP_LOGI(TAG, "  -> Found network '%s' at idx %d (our count: %d)", 
                                                 parsed_net.ssid, n, observer_networks[n].client_count);
                                        break;
                                    }
                                }
                                if (current_network_idx < 0) {
                                    ESP_LOGW(TAG, "  -> Network '%s' not in scan list, skipping", parsed_net.ssid);
                                }
                            } else {
                                // Not a network line (could be command echo, prompt, etc.)
                                current_network_idx = -1;
                            }
                        }
                        // Check for client MAC line (starts with space)
                        else if ((line_buffer[0] == ' ' || line_buffer[0] == '\t') && current_network_idx >= 0) {
                            observer_network_t *net = &observer_networks[current_network_idx];
                            char mac[18];
                            if (parse_sniffer_client_line(line_buffer, mac, sizeof(mac))) {
                                // Add client if not already present (accumulate)
                                if (add_client_mac(net, mac)) {
                                    ESP_LOGI(TAG, "  -> NEW client: %s for '%s' (total: %d)", mac, net->ssid, net->client_count);
                                }
                            } else {
                                ESP_LOGW(TAG, "  -> Failed to parse as client MAC");
                            }
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < OBSERVER_LINE_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
        
        // Check if observer was stopped
        if (!observer_running) {
            ESP_LOGI(TAG, "Observer stopped during poll");
            break;
        }
    }
    
    // Log summary of parsed data
    ESP_LOGI(TAG, "=== SNIFFER UPDATE SUMMARY ===");
    ESP_LOGI(TAG, "Total networks: %d", observer_network_count);
    int networks_with_clients = 0;
    for (int i = 0; i < observer_network_count; i++) {
        if (observer_networks[i].client_count > 0) {
            networks_with_clients++;
            ESP_LOGI(TAG, "  Network %d: '%s' CH%d clients=%d", 
                     i, observer_networks[i].ssid, observer_networks[i].channel, observer_networks[i].client_count);
            for (int j = 0; j < MAX_CLIENTS_PER_NETWORK && observer_networks[i].clients[j][0] != '\0'; j++) {
                ESP_LOGI(TAG, "    Client %d: %s", j, observer_networks[i].clients[j]);
            }
        }
    }
    ESP_LOGI(TAG, "Networks with active clients: %d/%d", networks_with_clients, observer_network_count);
    ESP_LOGI(TAG, "==============================");
    
    // Update UI if observer is still running
    if (observer_running && observer_networks) {
        
        // Update UI
        bsp_display_lock(0);
        
        if (observer_status_label) {
            lv_label_set_text_fmt(observer_status_label, "Found %d networks", observer_network_count);
        }
        
        update_observer_table();
        
        bsp_display_unlock();
    }
    
    ESP_LOGI(TAG, "Observer poll task finished");
    observer_task_handle = NULL;
    vTaskDelete(NULL);
}

// Timer callback - triggers poll task
static void observer_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    
    if (!observer_running) return;
    
    // Only start new poll if previous one finished
    if (observer_task_handle == NULL) {
        xTaskCreate(observer_poll_task, "obs_poll", 8192, NULL, 5, &observer_task_handle);
    }
}

// Observer start task - runs scan_networks first, then starts sniffer
// Parse scan network line: "1","SSID","","BSSID","channel","security","rssi","band"
// Parse scan network line: "index","SSID","","BSSID","channel","security","rssi","band"
static bool parse_scan_to_observer(const char *line, observer_network_t *net)
{
    if (line[0] != '"') return false;
    
    char temp[256];
    strncpy(temp, line, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    // Parse CSV with quoted fields
    char *fields[8] = {NULL};
    int field_idx = 0;
    char *p = temp;
    
    while (*p && field_idx < 8) {
        if (*p == '"') {
            p++;
            fields[field_idx] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') {
                *p = '\0';
                p++;
            }
            field_idx++;
            if (*p == ',') p++;
        } else {
            p++;
        }
    }
    
    if (field_idx < 8) return false;
    
    // fields[0] = index, fields[1] = SSID, fields[3] = BSSID, fields[4] = channel, fields[6] = RSSI, fields[7] = band
    net->scan_index = atoi(fields[0]);  // 1-based index for select_networks command
    
    strncpy(net->ssid, fields[1], sizeof(net->ssid) - 1);
    net->ssid[sizeof(net->ssid) - 1] = '\0';
    
    strncpy(net->bssid, fields[3], sizeof(net->bssid) - 1);
    net->bssid[sizeof(net->bssid) - 1] = '\0';
    
    net->channel = atoi(fields[4]);
    net->rssi = atoi(fields[6]);
    
    strncpy(net->band, fields[7], sizeof(net->band) - 1);
    net->band[sizeof(net->band) - 1] = '\0';
    
    net->client_count = 0;  // No clients initially
    memset(net->clients, 0, sizeof(net->clients));
    
    return true;
}

static void observer_start_task(void *arg)
{
    ESP_LOGI(TAG, "Observer start task - scanning networks first");
    
    // Check if PSRAM buffers are allocated
    if (!observer_rx_buffer || !observer_line_buffer || !observer_networks) {
        ESP_LOGE(TAG, "PSRAM buffers not allocated!");
        vTaskDelete(NULL);
        return;
    }
    
    // Update UI
    bsp_display_lock(0);
    if (observer_status_label) {
        lv_label_set_text(observer_status_label, "Scanning networks...");
    }
    bsp_display_unlock();
    
    // Clear previous results
    observer_network_count = 0;
    memset(observer_networks, 0, sizeof(observer_network_t) * MAX_NETWORKS);
    
    // Flush UART buffer
    uart_flush(UART_NUM);
    
    // Step 1: Run scan_networks
    uart_send_command("scan_networks");
    
    // Wait for scan to complete - use PSRAM buffers
    char *rx_buffer = observer_rx_buffer;
    char *line_buffer = observer_line_buffer;
    int line_pos = 0;
    bool scan_complete = false;
    int scanned_count = 0;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(UART_RX_TIMEOUT);
    
    while (!scan_complete && (xTaskGetTickCount() - start_time) < timeout_ticks && observer_running) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        // Log every line during scan for debugging
                        ESP_LOGI(TAG, "SCAN LINE: '%s'", line_buffer);
                        
                        if (strstr(line_buffer, "Scan results printed") != NULL) {
                            scan_complete = true;
                            ESP_LOGI(TAG, "Network scan complete marker found");
                            break;
                        }
                        
                        // Parse network line from scan
                        if (line_buffer[0] == '"' && scanned_count < MAX_NETWORKS) {
                            observer_network_t net = {0};
                            if (parse_scan_to_observer(line_buffer, &net)) {
                                observer_networks[scanned_count] = net;
                                scanned_count++;
                                ESP_LOGI(TAG, "  -> Parsed scan network #%d: '%s' BSSID=%s CH%d %s %ddBm", 
                                         net.scan_index, net.ssid, net.bssid, net.channel, net.band, net.rssi);
                            }
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < OBSERVER_LINE_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
    }
    
    // Save count of scanned networks
    observer_network_count = scanned_count;
    ESP_LOGI(TAG, "Scan complete: %d networks added to observer list", observer_network_count);
    
    // Update UI immediately with scanned networks (all with 0 clients)
    bsp_display_lock(0);
    if (observer_status_label) {
        lv_label_set_text_fmt(observer_status_label, "Found %d networks, starting sniffer...", observer_network_count);
    }
    update_observer_table();
    bsp_display_unlock();
    
    if (!observer_running) {
        ESP_LOGI(TAG, "Observer stopped during scan");
        vTaskDelete(NULL);
        return;
    }
    
    // Step 2: Start sniffer
    ESP_LOGI(TAG, "Starting sniffer...");
    bsp_display_lock(0);
    if (observer_status_label) {
        lv_label_set_text_fmt(observer_status_label, "%d networks, waiting for clients...", observer_network_count);
    }
    bsp_display_unlock();
    
    vTaskDelay(pdMS_TO_TICKS(500));  // Short delay
    uart_flush(UART_NUM);
    uart_send_command("start_sniffer_noscan");
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for sniffer to start
    
    // Step 3: Start periodic timer for polling sniffer results
    if (observer_running) {
        ESP_LOGI(TAG, "Starting observer timer (every %d ms)", OBSERVER_POLL_INTERVAL_MS);
        
        bsp_display_lock(0);
        if (observer_status_label) {
            lv_label_set_text(observer_status_label, "Observing... (updates every 20s)");
        }
        bsp_display_unlock();
        
        // Create and start timer
        if (observer_timer == NULL) {
            observer_timer = xTimerCreate("obs_timer", 
                                          pdMS_TO_TICKS(OBSERVER_POLL_INTERVAL_MS),
                                          pdTRUE,  // Auto-reload
                                          NULL,
                                          observer_timer_callback);
        }
        
        if (observer_timer != NULL) {
            xTimerStart(observer_timer, 0);
            
            // Do first poll immediately
            xTaskCreate(observer_poll_task, "obs_poll", 8192, NULL, 5, &observer_task_handle);
        }
    }
    
    ESP_LOGI(TAG, "Observer start task finished");
    vTaskDelete(NULL);
}

// Start button click handler
static void observer_start_btn_cb(lv_event_t *e)
{
    (void)e;
    
    if (observer_running) {
        ESP_LOGW(TAG, "Observer already running");
        return;
    }
    
    ESP_LOGI(TAG, "Starting Network Observer");
    observer_running = true;
    
    // Disable start button, enable stop button
    lv_obj_add_state(observer_start_btn, LV_STATE_DISABLED);
    lv_obj_clear_state(observer_stop_btn, LV_STATE_DISABLED);
    
    // Clear table
    if (observer_table) {
        lv_obj_clean(observer_table);
    }
    
    // Start observer task
    xTaskCreate(observer_start_task, "obs_start", 8192, NULL, 5, NULL);
}

// Stop button click handler
static void observer_stop_btn_cb(lv_event_t *e)
{
    (void)e;
    
    if (!observer_running) {
        ESP_LOGW(TAG, "Observer not running");
        return;
    }
    
    ESP_LOGI(TAG, "Stopping Network Observer");
    observer_running = false;
    
    // Stop timer
    if (observer_timer != NULL) {
        xTimerStop(observer_timer, 0);
    }
    
    // Send stop command
    uart_send_command("stop");
    
    // Update UI
    lv_obj_clear_state(observer_start_btn, LV_STATE_DISABLED);
    lv_obj_add_state(observer_stop_btn, LV_STATE_DISABLED);
    
    if (observer_status_label) {
        lv_label_set_text(observer_status_label, "Stopped");
    }
}

// Observer page back button handler
static void observer_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Observer back button clicked");
    
    // Stop observer if running
    if (observer_running) {
        observer_running = false;
        if (observer_timer != NULL) {
            xTimerStop(observer_timer, 0);
        }
        uart_send_command("stop");
    }
    
    show_main_tiles();
}

// Show Network Observer page
static void show_observer_page(void)
{
    // Delete tiles container if present
    if (tiles_container) {
        lv_obj_del(tiles_container);
        tiles_container = NULL;
    }
    
    // Delete scan page if present
    if (scan_page) {
        lv_obj_del(scan_page);
        scan_page = NULL;
        scan_btn = NULL;
        status_label = NULL;
        network_list = NULL;
        spinner = NULL;
    }
    
    // Delete existing observer page if present
    if (observer_page) {
        lv_obj_del(observer_page);
        observer_page = NULL;
    }
    
    lv_obj_t *scr = lv_scr_act();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A1A1A), 0);  // Dark teal-tinted
    
    // Create/update status bar
    create_status_bar();
    
    // Create observer page container below status bar
    observer_page = lv_obj_create(scr);
    lv_coord_t obs_scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(observer_page, lv_pct(100), obs_scr_height - 40);  // Account for status bar
    lv_obj_align(observer_page, LV_ALIGN_TOP_MID, 0, 40);  // Position below status bar
    lv_obj_set_style_bg_color(observer_page, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_width(observer_page, 0, 0);
    lv_obj_set_style_pad_all(observer_page, 16, 0);
    lv_obj_set_flex_flow(observer_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(observer_page, 10, 0);
    
    // Header container - fixed height, row layout
    lv_obj_t *header = lv_obj_create(observer_page);
    lv_obj_set_size(header, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button - positioned left
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 48, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1A3333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2A4444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, observer_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title - positioned after back button
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Network Observer");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);
    lv_obj_align_to(title, back_btn, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    
    // Stop button (red) - positioned right
    observer_stop_btn = lv_btn_create(header);
    lv_obj_set_size(observer_stop_btn, 100, 40);
    lv_obj_align(observer_stop_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(observer_stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(observer_stop_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(observer_stop_btn, lv_color_hex(0x444444), LV_STATE_DISABLED);
    lv_obj_set_style_radius(observer_stop_btn, 8, 0);
    lv_obj_add_event_cb(observer_stop_btn, observer_stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(observer_stop_btn, LV_STATE_DISABLED);  // Initially disabled
    
    lv_obj_t *stop_label = lv_label_create(observer_stop_btn);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(stop_label);
    
    // Start button (green) - positioned left of stop button
    observer_start_btn = lv_btn_create(header);
    lv_obj_set_size(observer_start_btn, 100, 40);
    lv_obj_align_to(observer_start_btn, observer_stop_btn, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lv_obj_set_style_bg_color(observer_start_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(observer_start_btn, lv_color_lighten(COLOR_MATERIAL_GREEN, 30), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(observer_start_btn, lv_color_hex(0x444444), LV_STATE_DISABLED);
    lv_obj_set_style_radius(observer_start_btn, 8, 0);
    lv_obj_add_event_cb(observer_start_btn, observer_start_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(observer_start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(start_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(start_label);
    
    // Status label
    observer_status_label = lv_label_create(observer_page);
    lv_label_set_text(observer_status_label, "Press Start to begin observing");
    lv_obj_set_style_text_font(observer_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(observer_status_label, lv_color_hex(0x888888), 0);
    
    // Network table container (scrollable)
    observer_table = lv_obj_create(observer_page);
    lv_obj_set_size(observer_table, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(observer_table, 1);
    lv_obj_set_style_bg_color(observer_table, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_color(observer_table, lv_color_hex(0x1A3333), 0);
    lv_obj_set_style_border_width(observer_table, 1, 0);
    lv_obj_set_style_radius(observer_table, 12, 0);
    lv_obj_set_style_pad_all(observer_table, 8, 0);
    lv_obj_set_flex_flow(observer_table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(observer_table, 6, 0);
    lv_obj_set_scroll_dir(observer_table, LV_DIR_VER);
    
    // If we have existing data, show it
    if (observer_network_count > 0) {
        lv_label_set_text_fmt(observer_status_label, "%d networks (cached)", observer_network_count);
        update_observer_table();
    }
    
    // Update button states based on observer_running
    if (observer_running) {
        lv_obj_add_state(observer_start_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(observer_stop_btn, LV_STATE_DISABLED);
        lv_label_set_text_fmt(observer_status_label, "%d networks (monitoring...)", observer_network_count);
    }
}

// ======================= ESP Modem Page =======================

// Helper: Convert auth mode to string
static const char* esp_modem_auth_mode_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
        default:                        return "UNKNOWN";
    }
}

// Initialize WiFi via ESP-Hosted (lazy initialization)
static esp_err_t esp_modem_wifi_init(void)
{
    if (esp_modem_wifi_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi for ESP Modem via ESP-Hosted...");
    
    // Enable WiFi power on Tab5 (controls ESP32C6 power via IO expander)
    ESP_LOGI(TAG, "Enabling WiFi power...");
    bsp_set_wifi_power_enable(true);
    vTaskDelay(pdMS_TO_TICKS(500));  // Wait for ESP32C6 to power up
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize ESP-Hosted (connects to ESP32C6 via SDIO)
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-Hosted: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for transport to be ready
    ESP_LOGI(TAG, "Waiting for ESP-Hosted transport...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Give time for SDIO connection
    
    // Create default WiFi station
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi (via esp_hosted remote)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi remote: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set WiFi mode to station
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for WiFi station to be fully ready before scanning
    ESP_LOGI(TAG, "Waiting for WiFi station to be ready...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    esp_modem_wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi initialized successfully via ESP-Hosted");
    
    return ESP_OK;
}

// Update ESP Modem network list UI
static void esp_modem_update_network_list(void)
{
    if (!esp_modem_network_list) return;
    
    lv_obj_clean(esp_modem_network_list);
    
    for (int i = 0; i < esp_modem_network_count; i++) {
        wifi_ap_record_t *ap = &esp_modem_networks[i];
        
        // Create list item
        lv_obj_t *item = lv_obj_create(esp_modem_network_list);
        lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 8, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(item, 4, 0);
        
        // SSID (or "Hidden" if empty)
        lv_obj_t *ssid_label = lv_label_create(item);
        if (strlen((char*)ap->ssid) > 0) {
            lv_label_set_text(ssid_label, (char*)ap->ssid);
        } else {
            lv_label_set_text(ssid_label, "(Hidden)");
        }
        lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
        
        // BSSID, Channel, RSSI, Auth
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap->bssid[0], ap->bssid[1], ap->bssid[2],
                 ap->bssid[3], ap->bssid[4], ap->bssid[5]);
        
        lv_obj_t *info_label = lv_label_create(item);
        lv_label_set_text_fmt(info_label, "%s  |  CH%d  |  %d dBm  |  %s", 
                              bssid_str, ap->primary, ap->rssi, 
                              esp_modem_auth_mode_str(ap->authmode));
        lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), 0);
    }
}

// ESP Modem scan task
static void esp_modem_scan_task(void *arg)
{
    ESP_LOGI(TAG, "Starting ESP Modem WiFi scan task");
    
    // Initialize WiFi if not already done
    esp_err_t ret = esp_modem_wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        
        bsp_display_lock(0);
        if (esp_modem_status_label) {
            lv_label_set_text(esp_modem_status_label, "WiFi init failed!");
        }
        if (esp_modem_spinner) {
            lv_obj_add_flag(esp_modem_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        if (esp_modem_scan_btn) {
            lv_obj_clear_state(esp_modem_scan_btn, LV_STATE_DISABLED);
        }
        esp_modem_scan_in_progress = false;
        bsp_display_unlock();
        
        vTaskDelete(NULL);
        return;
    }
    
    // Clear previous results
    esp_modem_network_count = 0;
    memset(esp_modem_networks, 0, sizeof(wifi_ap_record_t) * ESP_MODEM_MAX_NETWORKS);
    
    // Start scan (blocking) via ESP-Hosted - use NULL for default config
    // Retry up to 3 times if WiFi state is not ready (common after first init)
    ESP_LOGI(TAG, "Starting WiFi scan via ESP-Hosted (default config)...");
    int max_retries = 3;
    for (int attempt = 0; attempt < max_retries; attempt++) {
        ret = esp_wifi_scan_start(NULL, true);
        if (ret == ESP_OK) {
            break;  // Scan started successfully
        }
        
        if (ret == ESP_ERR_WIFI_STATE && attempt < max_retries - 1) {
            ESP_LOGW(TAG, "WiFi not ready for scan (attempt %d/%d), waiting...", attempt + 1, max_retries);
            vTaskDelay(pdMS_TO_TICKS(1500));  // Wait 1.5s before retry
        } else {
            break;  // Other error or last attempt
        }
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed after %d attempts: %s", max_retries, esp_err_to_name(ret));
        
        bsp_display_lock(0);
        if (esp_modem_status_label) {
            lv_label_set_text_fmt(esp_modem_status_label, "Scan failed: %s", esp_err_to_name(ret));
        }
        if (esp_modem_spinner) {
            lv_obj_add_flag(esp_modem_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        if (esp_modem_scan_btn) {
            lv_obj_clear_state(esp_modem_scan_btn, LV_STATE_DISABLED);
        }
        esp_modem_scan_in_progress = false;
        bsp_display_unlock();
        
        vTaskDelete(NULL);
        return;
    }
    
    // Get scan results via ESP-Hosted
    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        ap_count = 0;
    }
    ESP_LOGI(TAG, "Scan complete. AP count from esp_wifi_scan_get_ap_num: %d", ap_count);
    
    // Only get records if we found some networks
    if (ap_count > 0) {
        // Limit to max networks
        if (ap_count > ESP_MODEM_MAX_NETWORKS) {
            ap_count = ESP_MODEM_MAX_NETWORKS;
        }
        
        // Get AP records via ESP-Hosted
        esp_modem_network_count = ap_count;
        ret = esp_wifi_scan_get_ap_records(&esp_modem_network_count, esp_modem_networks);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
            esp_modem_network_count = 0;
        }
    } else {
        ESP_LOGW(TAG, "Scan returned 0 networks - this might indicate antenna/firmware issue");
        esp_modem_network_count = 0;
    }
    
    ESP_LOGI(TAG, "Retrieved %d network records", esp_modem_network_count);
    
    // Update UI
    bsp_display_lock(0);
    
    if (esp_modem_spinner) {
        lv_obj_add_flag(esp_modem_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (esp_modem_status_label) {
        lv_label_set_text_fmt(esp_modem_status_label, "Found %d networks", esp_modem_network_count);
    }
    
    // Update network list
    esp_modem_update_network_list();
    
    if (esp_modem_scan_btn) {
        lv_obj_clear_state(esp_modem_scan_btn, LV_STATE_DISABLED);
    }
    
    esp_modem_scan_in_progress = false;
    
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "ESP Modem scan task finished");
    vTaskDelete(NULL);
}

// ESP Modem scan button click handler
static void esp_modem_scan_btn_click_cb(lv_event_t *e)
{
    (void)e;
    
    if (esp_modem_scan_in_progress) {
        ESP_LOGW(TAG, "ESP Modem scan already in progress");
        return;
    }
    
    esp_modem_scan_in_progress = true;
    
    // Disable button during scan
    lv_obj_add_state(esp_modem_scan_btn, LV_STATE_DISABLED);
    
    // Show spinner
    if (esp_modem_spinner) {
        lv_obj_clear_flag(esp_modem_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update status
    if (esp_modem_status_label) {
        lv_label_set_text(esp_modem_status_label, "Scanning...");
    }
    
    // Clear previous results
    if (esp_modem_network_list) {
        lv_obj_clean(esp_modem_network_list);
    }
    
    // Start scan task
    xTaskCreate(esp_modem_scan_task, "esp_modem_scan", 8192, NULL, 5, NULL);
}

// ESP Modem back button handler
static void esp_modem_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "ESP Modem back button clicked");
    show_main_tiles();
}

// Show ESP Modem page
static void show_esp_modem_page(void)
{
    // Delete tiles container if present
    if (tiles_container) {
        lv_obj_del(tiles_container);
        tiles_container = NULL;
    }
    
    // Delete scan page if present
    if (scan_page) {
        lv_obj_del(scan_page);
        scan_page = NULL;
        scan_btn = NULL;
        status_label = NULL;
        network_list = NULL;
        spinner = NULL;
    }
    
    // Delete observer page if present
    if (observer_page) {
        lv_obj_del(observer_page);
        observer_page = NULL;
        observer_start_btn = NULL;
        observer_stop_btn = NULL;
        observer_table = NULL;
        observer_status_label = NULL;
    }
    
    // Delete existing ESP Modem page if present
    if (esp_modem_page) {
        lv_obj_del(esp_modem_page);
        esp_modem_page = NULL;
    }
    
    lv_obj_t *scr = lv_scr_act();
    
    // Set dark background with orange tint
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1410), 0);
    
    // Create/update status bar
    create_status_bar();
    
    // Create ESP Modem page container below status bar
    esp_modem_page = lv_obj_create(scr);
    lv_coord_t modem_scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(esp_modem_page, lv_pct(100), modem_scr_height - 40);  // Account for status bar
    lv_obj_align(esp_modem_page, LV_ALIGN_TOP_MID, 0, 40);  // Position below status bar
    lv_obj_set_style_bg_color(esp_modem_page, lv_color_hex(0x1A1410), 0);
    lv_obj_set_style_border_width(esp_modem_page, 0, 0);
    lv_obj_set_style_pad_all(esp_modem_page, 16, 0);
    lv_obj_set_flex_flow(esp_modem_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(esp_modem_page, 12, 0);
    
    // Header container
    lv_obj_t *header = lv_obj_create(esp_modem_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Left side: Back button + Title
    lv_obj_t *left_cont = lv_obj_create(header);
    lv_obj_set_size(left_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 0, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_cont, 12, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(left_cont);
    lv_obj_set_size(back_btn, 48, 40);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, esp_modem_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(left_cont);
    lv_label_set_text(title, "Internal C6 WiFi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_make(255, 87, 34), 0);  // Deep Orange
    
    // Scan button container (for button + spinner)
    lv_obj_t *btn_cont = lv_obj_create(header);
    lv_obj_set_size(btn_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_cont, 12, 0);
    
    // Spinner (hidden by default)
    esp_modem_spinner = lv_spinner_create(btn_cont);
    lv_obj_set_size(esp_modem_spinner, 32, 32);
    lv_spinner_set_anim_params(esp_modem_spinner, 1000, 200);
    lv_obj_add_flag(esp_modem_spinner, LV_OBJ_FLAG_HIDDEN);
    
    // Scan button
    esp_modem_scan_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(esp_modem_scan_btn, 120, 40);
    lv_obj_set_style_bg_color(esp_modem_scan_btn, lv_color_make(255, 87, 34), 0);  // Deep Orange
    lv_obj_set_style_bg_color(esp_modem_scan_btn, lv_color_lighten(lv_color_make(255, 87, 34), 30), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(esp_modem_scan_btn, lv_color_hex(0x444444), LV_STATE_DISABLED);
    lv_obj_set_style_radius(esp_modem_scan_btn, 8, 0);
    lv_obj_add_event_cb(esp_modem_scan_btn, esp_modem_scan_btn_click_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(esp_modem_scan_btn);
    lv_label_set_text(btn_label, "SCAN");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);
    
    // Status label
    esp_modem_status_label = lv_label_create(esp_modem_page);
    lv_label_set_text(esp_modem_status_label, "Press SCAN to search for networks (via ESP32C6)");
    lv_obj_set_style_text_font(esp_modem_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(esp_modem_status_label, lv_color_hex(0x888888), 0);
    
    // Network list container (scrollable)
    esp_modem_network_list = lv_obj_create(esp_modem_page);
    lv_obj_set_size(esp_modem_network_list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(esp_modem_network_list, 1);
    lv_obj_set_style_bg_color(esp_modem_network_list, lv_color_hex(0x1A1410), 0);
    lv_obj_set_style_border_color(esp_modem_network_list, lv_color_hex(0x332820), 0);
    lv_obj_set_style_border_width(esp_modem_network_list, 1, 0);
    lv_obj_set_style_radius(esp_modem_network_list, 12, 0);
    lv_obj_set_style_pad_all(esp_modem_network_list, 8, 0);
    lv_obj_set_flex_flow(esp_modem_network_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(esp_modem_network_list, 8, 0);
    lv_obj_set_scroll_dir(esp_modem_network_list, LV_DIR_VER);
    
    // If we have existing scan results, show them
    if (esp_modem_network_count > 0) {
        lv_label_set_text_fmt(esp_modem_status_label, "Found %d networks (cached)", esp_modem_network_count);
        esp_modem_update_network_list();
    }
    
    // Auto-start scan when entering the page
    lv_obj_send_event(esp_modem_scan_btn, LV_EVENT_CLICKED, NULL);
}

// Global attack tile event handler
static void global_attack_tile_event_cb(lv_event_t *e)
{
    const char *attack_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Global attack tile clicked: %s", attack_name);
    
    // TODO: Implement actual attack logic for each type
    // - Blackout
    // - Handshakes
    // - Portal
    // - Snifferdog
    // - Wardrive
}

// Show Global WiFi Attacks page
static void show_global_attacks_page(void)
{
    // Delete tiles container if present
    if (tiles_container) {
        lv_obj_del(tiles_container);
        tiles_container = NULL;
    }
    
    // Delete scan page if present
    if (scan_page) {
        lv_obj_del(scan_page);
        scan_page = NULL;
        scan_btn = NULL;
        status_label = NULL;
        network_list = NULL;
        spinner = NULL;
    }
    
    // Delete observer page if present
    if (observer_page) {
        lv_obj_del(observer_page);
        observer_page = NULL;
        observer_start_btn = NULL;
        observer_stop_btn = NULL;
        observer_table = NULL;
        observer_status_label = NULL;
    }
    
    // Delete ESP Modem page if present
    if (esp_modem_page) {
        lv_obj_del(esp_modem_page);
        esp_modem_page = NULL;
        esp_modem_scan_btn = NULL;
        esp_modem_status_label = NULL;
        esp_modem_network_list = NULL;
        esp_modem_spinner = NULL;
    }
    
    // Delete existing global attacks page if present
    if (global_attacks_page) {
        lv_obj_del(global_attacks_page);
        global_attacks_page = NULL;
    }
    
    lv_obj_t *scr = lv_scr_act();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, COLOR_MATERIAL_BG, 0);
    
    // Create/update status bar
    create_status_bar();
    
    // Create global attacks page container below status bar
    global_attacks_page = lv_obj_create(scr);
    lv_coord_t attacks_scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(global_attacks_page, lv_pct(100), attacks_scr_height - 40);  // Account for status bar
    lv_obj_align(global_attacks_page, LV_ALIGN_TOP_MID, 0, 40);  // Position below status bar
    lv_obj_set_style_bg_color(global_attacks_page, COLOR_MATERIAL_BG, 0);
    lv_obj_set_style_border_width(global_attacks_page, 0, 0);
    lv_obj_set_style_pad_all(global_attacks_page, 16, 0);
    lv_obj_set_flex_flow(global_attacks_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(global_attacks_page, 12, 0);
    
    // Header with back button and title
    lv_obj_t *header = lv_obj_create(global_attacks_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 48, 40);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Global WiFi Attacks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    
    // Tiles container
    lv_obj_t *tiles = lv_obj_create(global_attacks_page);
    lv_obj_set_size(tiles, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_style_bg_color(tiles, COLOR_MATERIAL_BG, 0);
    lv_obj_set_style_border_width(tiles, 0, 0);
    lv_obj_set_style_pad_all(tiles, 10, 0);
    lv_obj_set_style_pad_gap(tiles, 20, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    
    // Create 5 attack tiles
    // Blackout - Red (dangerous)
    create_tile(tiles, LV_SYMBOL_POWER, "Blackout", COLOR_MATERIAL_RED, global_attack_tile_event_cb, "Blackout");
    
    // Handshaker - Amber
    create_tile(tiles, LV_SYMBOL_DOWNLOAD, "Handshaker", COLOR_MATERIAL_AMBER, global_attack_tile_event_cb, "Handshakes");
    
    // Portal - Orange
    create_tile(tiles, LV_SYMBOL_WIFI, "Portal", COLOR_MATERIAL_ORANGE, global_attack_tile_event_cb, "Portal");
    
    // SnifferDog - Purple
    create_tile(tiles, LV_SYMBOL_EYE_OPEN, "SnifferDog", COLOR_MATERIAL_PURPLE, global_attack_tile_event_cb, "Snifferdog");
    
    // Wardrive - Teal
    create_tile(tiles, LV_SYMBOL_GPS, "Wardrive", COLOR_MATERIAL_TEAL, global_attack_tile_event_cb, "Wardrive");
}

void app_main(void)
{
    ESP_LOGI(TAG, "M5Stack Tab5 WiFi Scanner");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Allocate large buffers in PSRAM for Network Observer
    ESP_LOGI(TAG, "Allocating observer buffers in PSRAM...");
    observer_networks = heap_caps_calloc(MAX_NETWORKS, sizeof(observer_network_t), MALLOC_CAP_SPIRAM);
    observer_rx_buffer = heap_caps_malloc(UART_BUF_SIZE, MALLOC_CAP_SPIRAM);
    observer_line_buffer = heap_caps_malloc(OBSERVER_LINE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    
    if (!observer_networks || !observer_rx_buffer || !observer_line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers for observer!");
    } else {
        ESP_LOGI(TAG, "Observer PSRAM buffers allocated successfully");
    }
    
    // Allocate buffer for ESP Modem WiFi scan results
    ESP_LOGI(TAG, "Allocating ESP Modem buffers in PSRAM...");
    esp_modem_networks = heap_caps_calloc(ESP_MODEM_MAX_NETWORKS, sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    if (!esp_modem_networks) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for ESP Modem!");
    } else {
        ESP_LOGI(TAG, "ESP Modem PSRAM buffer allocated successfully");
    }
    
    // Initialize I2C (required for IO expander)
    ESP_ERROR_CHECK(bsp_i2c_init());
    
    // Initialize IO expander
    bsp_io_expander_pi4ioe_init(bsp_i2c_get_handle());
    
    // Enable battery charging
    ESP_LOGI(TAG, "Enabling battery charging...");
    bsp_set_charge_en(true);
    bsp_set_charge_qc_en(true);
    
    // Initialize UART for ESP32C5 communication
    uart_init();
    
    // Initialize display
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }
    
    // Set display brightness
    bsp_display_brightness_set(80);
    
    // Create UI - show main tiles
    bsp_display_lock(0);
    show_main_tiles();
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Application started. Ready to scan.");
}
