#include "usb_vcp_config.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    assert(usb_vcp_is_ch34x(0x1A86, 0x7522));
    assert(usb_vcp_is_ch34x(0x1A86, 0x7523));
    assert(usb_vcp_is_ch34x(0x1A86, 0x5523));
    assert(!usb_vcp_is_ch34x(0x10C4, 0xEA60));
    assert(!usb_vcp_is_ch34x(0x1A86, 0x1234));

    uint16_t baud_register = 0;
    assert(usb_vcp_ch34x_baud_register(115200, 0x30, &baud_register));
    assert(baud_register == 0xCC83);

    /* CH340 version 0x27 has the short-packet bit inverted. Leaving it clear
     * prevents a trailing sub-32-byte binary fragment from being buffered. */
    assert(usb_vcp_ch34x_baud_register(115200, 0x27, &baud_register));
    assert(baud_register == 0xCC03);

    assert(usb_vcp_ch34x_baud_register(921600, 0x30, &baud_register));
    assert(baud_register == 0xF387);

    assert(usb_vcp_ch34x_baud_register(460800, 0x35, &baud_register));
    assert(baud_register == 0xF383);

    assert(usb_vcp_ch34x_baud_register(2000000, 0x30, &baud_register));
    assert(baud_register == 0xFD83);

    assert(usb_vcp_ch34x_baud_register(1500000, 0x30, &baud_register));
    assert(baud_register == 0xFC83);

    assert(usb_vcp_ch34x_baud_register(3000000, 0x30, &baud_register));
    assert(baud_register == 0xFE83);

    /* 4 MBaud is not representable by this divisor ladder and rounds to the
     * same register as 3 MBaud, so the USB settings UI intentionally omits it. */
    assert(usb_vcp_ch34x_baud_register(4000000, 0x30, &baud_register));
    assert(baud_register == 0xFE83);

    usb_vcp_control_request_t baud_request = {0};
    assert(usb_vcp_ch34x_baud_request(921600, 0x35, &baud_request));
    assert(baud_request.request == 0x9A);
    assert(baud_request.value == 0x1312);
    assert(baud_request.index == 0xF387);
    assert(!usb_vcp_ch34x_baud_request(0, 0x35, &baud_request));
    assert(!usb_vcp_ch34x_baud_request(921600, 0x35, 0));

    assert(!usb_vcp_ch34x_baud_register(0, 0x30, &baud_register));
    assert(!usb_vcp_ch34x_baud_register(115200, 0x30, 0));
    assert(usb_vcp_ch34x_lcr_8n1() == 0x00C3);

    usb_vcp_control_request_t steps[USB_VCP_CH34X_INIT_MAX_STEPS];
    size_t step_count = usb_vcp_ch34x_init_plan(
        115200, 0x35, steps, USB_VCP_CH34X_INIT_MAX_STEPS);
    assert(step_count == 4);
    assert(steps[0].request == 0xA1 && steps[0].value == 0 &&
           steps[0].index == 0);
    assert(steps[1].request == 0x9A && steps[1].value == 0x1312 &&
           steps[1].index == 0xCC83);
    assert(steps[2].request == 0x9A && steps[2].value == 0x2518 &&
           steps[2].index == 0x00C3);
    assert(steps[3].request == 0xA4 && steps[3].value == 0xFFFF &&
           steps[3].index == 0);

    /* Pre-0x30 chips use legacy line-control registers, so the combined LCR
     * write is omitted while SERIAL_INIT and MODEM_CTRL remain mandatory. */
    step_count = usb_vcp_ch34x_init_plan(
        115200, 0x27, steps, USB_VCP_CH34X_INIT_MAX_STEPS);
    assert(step_count == 3);
    assert(steps[0].request == 0xA1);
    assert(steps[1].request == 0x9A && steps[1].value == 0x1312 &&
           steps[1].index == 0xCC03);
    assert(steps[2].request == 0xA4);
    assert(usb_vcp_ch34x_init_plan(115200, 0x35, steps, 3) == 0);
    assert(usb_vcp_ch34x_init_plan(115200, 0x35, NULL,
                                           USB_VCP_CH34X_INIT_MAX_STEPS) == 0);

    /* Periodic dashboard refreshes must never consume a Monster's CRACK/1
     * stream. Only an identified GPS with an otherwise idle RX path may poll. */
    assert(usb_vcp_gps_poll_allowed(true, false, false, false, false));
    assert(usb_vcp_gps_poll_allowed(false, true, false, false, false));
    assert(!usb_vcp_gps_poll_allowed(false, false, false, false, false));
    assert(!usb_vcp_gps_poll_allowed(true, false, true, false, false));
    assert(!usb_vcp_gps_poll_allowed(true, false, false, true, false));
    assert(!usb_vcp_gps_poll_allowed(true, false, false, false, true));
    return 0;
}
