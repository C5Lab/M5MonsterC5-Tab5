#include "sta_mac_setup_model.h"

#include <string.h>

const char sta_mac_setup_status_command[] = "sta_mac status";

sta_mac_setup_port_t sta_mac_setup_single_detected_port(bool grove, bool usb,
                                                        bool mbus)
{
    unsigned count = (unsigned)grove + (unsigned)usb + (unsigned)mbus;
    if (count != 1) return STA_MAC_SETUP_PORT_NONE;
    if (grove) return STA_MAC_SETUP_PORT_GROVE;
    if (usb) return STA_MAC_SETUP_PORT_USB;
    return STA_MAC_SETUP_PORT_MBUS;
}

static int hex_value(char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

static char upper_hex(int digit)
{
    return (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
}

bool sta_mac_setup_normalize_fixed_mac(const char *input, char *output,
                                       size_t output_size)
{
    if (!output || output_size == 0) return false;
    output[0] = '\0';
    if (!input || output_size < STA_MAC_SETUP_MAC_BUFFER_SIZE) return false;
    if (strlen(input) != STA_MAC_SETUP_MAC_TEXT_LENGTH) return false;

    char normalized[STA_MAC_SETUP_MAC_BUFFER_SIZE];
    for (size_t i = 0; i < STA_MAC_SETUP_MAC_TEXT_LENGTH; i++) {
        if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
            if (input[i] != ':') return false;
            normalized[i] = ':';
        } else {
            int digit = hex_value(input[i]);
            if (digit < 0) return false;
            normalized[i] = upper_hex(digit);
        }
    }
    if (input[STA_MAC_SETUP_MAC_TEXT_LENGTH] != '\0') return false;

    int first_octet = (hex_value(input[0]) << 4) | hex_value(input[1]);
    if ((first_octet & 0x03) != 0x02) return false;

    normalized[STA_MAC_SETUP_MAC_TEXT_LENGTH] = '\0';
    memcpy(output, normalized, sizeof(normalized));
    return true;
}

bool sta_mac_setup_build_set_command(sta_mac_setup_mode_t mode,
                                     const char *fixed_mac, char *output,
                                     size_t output_size)
{
    if (!output || output_size == 0) return false;
    output[0] = '\0';

    const char *command = NULL;
    char fixed_command[STA_MAC_SETUP_SET_COMMAND_BUFFER_SIZE];
    if (mode == STA_MAC_SETUP_FACTORY) {
        command = "sta_mac set factory";
    } else if (mode == STA_MAC_SETUP_RANDOM) {
        command = "sta_mac set random";
    } else if (mode == STA_MAC_SETUP_FIXED) {
        char normalized[STA_MAC_SETUP_MAC_BUFFER_SIZE];
        if (!sta_mac_setup_normalize_fixed_mac(fixed_mac, normalized,
                                                sizeof(normalized))) return false;
        static const char prefix[] = "sta_mac set fixed ";
        memcpy(fixed_command, prefix, sizeof(prefix) - 1);
        memcpy(fixed_command + sizeof(prefix) - 1, normalized,
               sizeof(normalized));
        command = fixed_command;
    } else {
        return false;
    }

    size_t command_size = strlen(command) + 1;
    if (output_size < command_size) return false;
    memcpy(output, command, command_size);
    return true;
}
