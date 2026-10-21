#include "transfer_speed_config.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    static const uint32_t expected_hardware[] = {
        115200U, 230400U, 460800U, 921600U, 1000000U,
        1500000U, 2000000U, 3000000U, 4000000U,
    };
    static const uint32_t expected_usb[] = {
        115200U, 230400U, 460800U, 921600U,
        1000000U, 1500000U, 2000000U, 3000000U,
    };

    assert(janos_ft_baud_default(JANOS_FT_LINK_HARDWARE_UART) == 460800U);
    assert(janos_ft_baud_default(JANOS_FT_LINK_USB_CH34X) == 921600U);

    assert(janos_ft_baud_choice_count(JANOS_FT_LINK_HARDWARE_UART) ==
           sizeof(expected_hardware) / sizeof(expected_hardware[0]));
    assert(janos_ft_baud_choice_count(JANOS_FT_LINK_USB_CH34X) ==
           sizeof(expected_usb) / sizeof(expected_usb[0]));

    for (size_t i = 0; i < sizeof(expected_hardware) / sizeof(expected_hardware[0]); ++i) {
        assert(janos_ft_baud_choice_at(JANOS_FT_LINK_HARDWARE_UART, i) ==
               expected_hardware[i]);
    }
    for (size_t i = 0; i < sizeof(expected_usb) / sizeof(expected_usb[0]); ++i) {
        assert(janos_ft_baud_choice_at(JANOS_FT_LINK_USB_CH34X, i) ==
               expected_usb[i]);
    }

    assert(janos_ft_baud_choice_at(JANOS_FT_LINK_USB_CH34X, 99U) == 0U);
    assert(janos_ft_baud_index(JANOS_FT_LINK_HARDWARE_UART, 2000000U) == 6U);
    assert(janos_ft_baud_index(JANOS_FT_LINK_USB_CH34X, 1500000U) == 5U);
    assert(janos_ft_baud_index(JANOS_FT_LINK_USB_CH34X, 4000000U) == 3U);
    assert(janos_ft_baud_sanitize(JANOS_FT_LINK_USB_CH34X, 2000000U) ==
           2000000U);
    assert(janos_ft_baud_sanitize(JANOS_FT_LINK_USB_CH34X, 4000000U) ==
           921600U);
    assert(janos_ft_baud_selected(JANOS_FT_LINK_HARDWARE_UART,
                                  2000000U, 1500000U) == 2000000U);
    assert(janos_ft_baud_selected(JANOS_FT_LINK_USB_CH34X,
                                  2000000U, 1500000U) == 1500000U);
    assert(janos_ft_baud_selected(JANOS_FT_LINK_USB_CH34X,
                                  2000000U, 4000000U) == 921600U);

    return 0;
}
