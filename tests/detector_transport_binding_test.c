#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int tab_id_t;
typedef int uart_port_t;
typedef struct {
    int deauth_detector_bound_tab;
    int deauth_detector_bound_port;
    bool connected;
} tab_context_t;
static int current_tab;
static char wire[3][128];
static int last_port[3];
static bool short_newline_write;
static bool deauth_detector_link_ready(const tab_context_t *ctx) { return ctx->connected; }
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
                           .deauth_detector_bound_port = 1, .connected = true};
    tab_context_t mbus = {.deauth_detector_bound_tab = 2,
                          .deauth_detector_bound_port = 2, .connected = true};
    current_tab = 2;
    assert(deauth_detector_send_bound(&grove, "scan_networks"));
    assert(!strcmp(wire[0], "scan_networks\r\n") && last_port[0] == 1);
    assert(wire[2][0] == '\0');
    assert(deauth_detector_send_bound(&mbus, "deauth_detector_v2 --only 1 3 trace"));
    assert(!strcmp(wire[2], "deauth_detector_v2 --only 1 3 trace\r\n") && last_port[2] == 2);
    current_tab = 1;
    assert(deauth_detector_send_bound(&grove, "stop"));
    assert(!strcmp(wire[0], "scan_networks\r\nstop\r\n") && last_port[0] == 1);
    assert(wire[1][0] == '\0' && current_tab == 1);
    mbus.connected = false;
    assert(!deauth_detector_send_bound(&mbus, "stop"));
    assert(!strcmp(wire[2], "deauth_detector_v2 --only 1 3 trace\r\n"));
    mbus.connected = true;
    size_t prior_mbus_length = strlen(wire[2]);
    short_newline_write = true;
    assert(!deauth_detector_send_bound(&mbus, "deauth_detector_v2"));
    assert(!short_newline_write);
    assert(!strcmp(wire[2] + prior_mbus_length, "deauth_detector_v2\r"));
    assert(last_port[2] == 2 && wire[1][0] == '\0' && current_tab == 1);
    puts("detector bound tab/port transport assertions passed");
    return 0;
}
