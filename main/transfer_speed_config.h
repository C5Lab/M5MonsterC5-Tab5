#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JANOS_FT_HARDWARE_DEFAULT 460800U
#define JANOS_FT_USB_DEFAULT 921600U

typedef enum {
    JANOS_FT_LINK_HARDWARE_UART = 0,
    JANOS_FT_LINK_USB_CH34X,
} janos_ft_link_t;

size_t janos_ft_baud_choice_count(janos_ft_link_t link);
uint32_t janos_ft_baud_choice_at(janos_ft_link_t link, size_t index);
uint32_t janos_ft_baud_default(janos_ft_link_t link);
uint16_t janos_ft_baud_index(janos_ft_link_t link, uint32_t rate);
uint32_t janos_ft_baud_sanitize(janos_ft_link_t link, uint32_t rate);
uint32_t janos_ft_baud_selected(janos_ft_link_t link,
                                uint32_t hardware_rate, uint32_t usb_rate);

#ifdef __cplusplus
}
#endif
