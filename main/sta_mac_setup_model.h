#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STA_MAC_SETUP_MAC_TEXT_LENGTH 17U
#define STA_MAC_SETUP_MAC_BUFFER_SIZE 18U
#define STA_MAC_SETUP_SET_COMMAND_BUFFER_SIZE 36U

typedef enum {
    STA_MAC_SETUP_FACTORY = 0,
    STA_MAC_SETUP_RANDOM,
    STA_MAC_SETUP_FIXED,
} sta_mac_setup_mode_t;

typedef enum {
    STA_MAC_SETUP_PORT_NONE = 0,
    STA_MAC_SETUP_PORT_GROVE = 1,
    STA_MAC_SETUP_PORT_USB = 2,
    STA_MAC_SETUP_PORT_MBUS = 3,
} sta_mac_setup_port_t;

/* Select automatically only when exactly one recognized console is available. */
sta_mac_setup_port_t sta_mac_setup_single_detected_port(bool grove, bool usb,
                                                        bool mbus);

extern const char sta_mac_setup_status_command[];

/* Output is cleared on failure when a nonempty output buffer is supplied. */
bool sta_mac_setup_normalize_fixed_mac(const char *input, char *output,
                                       size_t output_size);

/* Builds only the supported JanOS set forms; output is cleared on failure. */
bool sta_mac_setup_build_set_command(sta_mac_setup_mode_t mode,
                                     const char *fixed_mac, char *output,
                                     size_t output_size);

#ifdef __cplusplus
}
#endif
