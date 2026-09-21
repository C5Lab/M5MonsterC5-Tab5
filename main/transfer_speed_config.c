#include "transfer_speed_config.h"

static const uint32_t hardware_choices[] = {
    115200U, 230400U, 460800U, 921600U, 1000000U,
    1500000U, 2000000U, 3000000U, 4000000U,
};

/* CH34x's 6 MHz divisor ladder represents 3 MBaud exactly. A requested
 * 4 MBaud rounds to that same divisor, so it must not appear as a distinct
 * USB option. */
static const uint32_t usb_choices[] = {
    115200U, 230400U, 460800U, 921600U,
    1000000U, 1500000U, 2000000U, 3000000U,
};

static const uint32_t *choices_for(janos_ft_link_t link, size_t *count)
{
    if (link == JANOS_FT_LINK_USB_CH34X) {
        *count = sizeof(usb_choices) / sizeof(usb_choices[0]);
        return usb_choices;
    }
    *count = sizeof(hardware_choices) / sizeof(hardware_choices[0]);
    return hardware_choices;
}

size_t janos_ft_baud_choice_count(janos_ft_link_t link)
{
    size_t count = 0;
    (void)choices_for(link, &count);
    return count;
}

uint32_t janos_ft_baud_choice_at(janos_ft_link_t link, size_t index)
{
    size_t count = 0;
    const uint32_t *choices = choices_for(link, &count);
    return index < count ? choices[index] : 0U;
}

uint32_t janos_ft_baud_default(janos_ft_link_t link)
{
    return link == JANOS_FT_LINK_USB_CH34X
               ? JANOS_FT_USB_DEFAULT
               : JANOS_FT_HARDWARE_DEFAULT;
}

uint16_t janos_ft_baud_index(janos_ft_link_t link, uint32_t rate)
{
    size_t count = 0;
    const uint32_t *choices = choices_for(link, &count);
    uint32_t fallback_rate = janos_ft_baud_default(link);
    uint16_t fallback = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (choices[i] == rate) return i;
        if (choices[i] == fallback_rate) fallback = i;
    }
    return fallback;
}

uint32_t janos_ft_baud_sanitize(janos_ft_link_t link, uint32_t rate)
{
    uint16_t index = janos_ft_baud_index(link, rate);
    uint32_t selected = janos_ft_baud_choice_at(link, index);
    return selected == rate ? rate : janos_ft_baud_default(link);
}

uint32_t janos_ft_baud_selected(janos_ft_link_t link,
                                uint32_t hardware_rate, uint32_t usb_rate)
{
    uint32_t selected = link == JANOS_FT_LINK_USB_CH34X
                            ? usb_rate
                            : hardware_rate;
    return janos_ft_baud_sanitize(link, selected);
}
