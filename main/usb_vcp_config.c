#include "usb_vcp_config.h"

bool usb_vcp_is_ch34x(uint16_t vid, uint16_t pid)
{
    if (vid != USB_VCP_CH34X_VID) return false;
    return pid == USB_VCP_CH340_PID || pid == USB_VCP_CH340_ALT_PID ||
           pid == USB_VCP_CH341_PID;
}

bool usb_vcp_gps_poll_allowed(bool known_gps, bool nmea_seen,
                              bool monster_detected, bool rx_exclusive,
                              bool transport_owned)
{
    return (known_gps || nmea_seen) && !monster_detected && !rx_exclusive &&
           !transport_owned;
}

bool usb_vcp_ch34x_baud_register(uint32_t baud_rate, uint8_t chip_version,
                                 uint16_t *value)
{
    if (baud_rate == 0 || !value) return false;

    uint8_t factor;
    uint8_t divisor;
    uint32_t clock;
    if (baud_rate == 921600U) {
        factor = 0xF3U;
        divisor = 7U;
    } else if (baud_rate == 307200U) {
        factor = 0xD9U;
        divisor = 7U;
    } else {
        if (baud_rate > 6000000U / 255U) {
            divisor = 3U;
            clock = 6000000U;
        } else if (baud_rate > 750000U / 255U) {
            divisor = 2U;
            clock = 750000U;
        } else if (baud_rate > 93750U / 255U) {
            divisor = 1U;
            clock = 93750U;
        } else {
            divisor = 0U;
            clock = 11719U;
        }

        uint32_t quotient = clock / baud_rate;
        if (quotient == 0 || quotient == 0xFFU) return false;
        uint32_t slower = clock / (quotient + 1U);
        if ((clock / quotient) - baud_rate > baud_rate - slower) quotient++;
        factor = (uint8_t)(256U - quotient);
    }

    /* Bit 7 asks CH340A to forward a short final USB packet immediately, but
     * the common 0x27 revision implements that bit inverted. */
    uint16_t short_packet = chip_version > 0x27U ? 0x0080U : 0U;
    *value = (uint16_t)(((uint16_t)factor << 8) | divisor | short_packet);
    return true;
}

bool usb_vcp_ch34x_baud_request(uint32_t baud_rate, uint8_t chip_version,
                                usb_vcp_control_request_t *request)
{
    uint16_t baud_register = 0;
    if (!request ||
        !usb_vcp_ch34x_baud_register(baud_rate, chip_version,
                                     &baud_register)) {
        return false;
    }
    *request = (usb_vcp_control_request_t){
        .request = 0x9AU,
        .value = 0x1312U,
        .index = baud_register,
    };
    return true;
}

uint16_t usb_vcp_ch34x_lcr_8n1(void)
{
    /* RX enable | TX enable | eight data bits, no parity, one stop bit. */
    return 0x00C3U;
}

size_t usb_vcp_ch34x_init_plan(uint32_t baud_rate, uint8_t chip_version,
                               usb_vcp_control_request_t *steps,
                               size_t capacity)
{
    const size_t required = chip_version >= 0x30U ? 4U : 3U;
    usb_vcp_control_request_t baud_request;
    if (!steps || capacity < required ||
        !usb_vcp_ch34x_baud_request(baud_rate, chip_version,
                                    &baud_request)) {
        return 0;
    }

    size_t count = 0;
    steps[count++] = (usb_vcp_control_request_t){
        .request = 0xA1U, .value = 0, .index = 0,
    };
    steps[count++] = baud_request;
    if (chip_version >= 0x30U) {
        steps[count++] = (usb_vcp_control_request_t){
            .request = 0x9AU, .value = 0x2518U,
            .index = usb_vcp_ch34x_lcr_8n1(),
        };
    }
    steps[count++] = (usb_vcp_control_request_t){
        .request = 0xA4U, .value = 0xFFFFU, .index = 0,
    };
    return count;
}
