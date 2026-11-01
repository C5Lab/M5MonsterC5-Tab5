#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
typedef enum { TAB_GROVE = 0, TAB_USB = 1, TAB_MBUS = 2 } tab_id_t;
typedef int uart_port_t;
typedef struct {
    int deauth_detector_bound_tab;
    int deauth_detector_bound_port;
    uint32_t deauth_detector_bound_link_generation;
    bool deauth_detector_link_lost;
} tab_context_t;
static int current_tab;
static char wire[3][128];
static int last_port[3];
static bool short_newline_write;
static bool grove_detected = true;
static bool mbus_detected = true;
static bool uart2_initialized = true;
static bool usb_cdc_connected = true;
static bool usb_transport_ready = true;
static void *usb_cdc_handle = (void *)1;
static atomic_uint sta_mac_usb_generation = ATOMIC_VAR_INIT(7);
static uart_port_t uart_port_for_tab(tab_id_t tab) { return 10 + (int)tab; }
#define tab_transport_name(tab) "host"
#define TAG "host"
#define ESP_LOGI(...) ((void)0)
static int transport_write_bytes_tab(tab_id_t tab, uart_port_t port,
                                     const char *bytes, size_t length)
{
    assert(tab >= 0 && tab < 3);
    if (short_newline_write && length == 2 && !memcmp(bytes, "\r\n", 2)) {
        length = 1;
        short_newline_write = false;
    }
    size_t used = strlen(wire[tab]);
    assert(used + length < sizeof(wire[tab]));
    memcpy(wire[tab] + used, bytes, length);
    wire[tab][used + length] = '\0';
    last_port[tab] = port;
    return (int)length;
}
/* SEND */
int main(void)
{
    tab_context_t grove = {.deauth_detector_bound_tab = 0,
                           .deauth_detector_bound_port = 10};
    tab_context_t usb = {.deauth_detector_bound_tab = 1,
                         .deauth_detector_bound_port = 11,
                         .deauth_detector_bound_link_generation = 7};
    tab_context_t mbus = {.deauth_detector_bound_tab = 2,
                          .deauth_detector_bound_port = 12};
    current_tab = 2;
    assert(deauth_detector_send_bound(&grove, "scan_networks"));
    assert(deauth_detector_send_bound(&grove, "deauth_detector_v2 --only 1 trace defence"));
    assert(!strcmp(wire[0], "scan_networks\r\ndeauth_detector_v2 --only 1 trace defence\r\n") &&
           last_port[0] == 10);
    assert(wire[2][0] == '\0');
    current_tab = 0;
    assert(deauth_detector_send_bound(&usb, "deauth_detector_v2 --only 2 trace defence"));
    assert(deauth_detector_send_bound(&usb, "stop"));
    assert(!strcmp(wire[1],
        "deauth_detector_v2 --only 2 trace defence\r\nstop\r\n") &&
        last_port[1] == 11);
    atomic_store(&sta_mac_usb_generation, 8);
    assert(!deauth_detector_send_bound(&usb, "stop"));
    assert(!strcmp(wire[1],
        "deauth_detector_v2 --only 2 trace defence\r\nstop\r\n"));

    assert(deauth_detector_send_bound(&mbus,
                                      "deauth_detector_v2 --only 1 3 trace defence"));
    assert(!strcmp(wire[2],
                   "deauth_detector_v2 --only 1 3 trace defence\r\n") &&
           last_port[2] == 12);
    current_tab = 1;
    assert(deauth_detector_send_bound(&grove, "stop"));
    assert(!strcmp(wire[0],
        "scan_networks\r\ndeauth_detector_v2 --only 1 trace defence\r\nstop\r\n") &&
        last_port[0] == 10 && current_tab == 1);
    mbus_detected = false;
    assert(!deauth_detector_send_bound(&mbus, "stop"));
    assert(!strcmp(wire[2],
                   "deauth_detector_v2 --only 1 3 trace defence\r\n"));
    mbus_detected = true;
    size_t prior_mbus_length = strlen(wire[2]);
    short_newline_write = true;
    assert(!deauth_detector_send_bound(&mbus, "deauth_detector_v2"));
    assert(!short_newline_write);
    assert(!strcmp(wire[2] + prior_mbus_length, "deauth_detector_v2\r"));
    assert(last_port[2] == 12 && current_tab == 1);
    grove.deauth_detector_link_lost = true;
    assert(!deauth_detector_send_bound(&grove, "stop"));
    usb.deauth_detector_bound_port = 99;
    assert(!deauth_detector_send_bound(&usb, "stop"));
    puts("detector bound Grove/USB/MBus tab/port/generation assertions passed");
    return 0;
}
