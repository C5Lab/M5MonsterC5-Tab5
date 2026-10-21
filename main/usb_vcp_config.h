#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_VCP_CH34X_VID 0x1A86U
#define USB_VCP_CH340_PID 0x7522U
#define USB_VCP_CH340_ALT_PID 0x7523U
#define USB_VCP_CH341_PID 0x5523U
#define USB_VCP_CH34X_INIT_MAX_STEPS 4U

typedef struct {
    uint8_t request;
    uint16_t value;
    uint16_t index;
} usb_vcp_control_request_t;

bool usb_vcp_is_ch34x(uint16_t vid, uint16_t pid);
bool usb_vcp_gps_poll_allowed(bool known_gps, bool nmea_seen,
                              bool monster_detected, bool rx_exclusive,
                              bool transport_owned);
bool usb_vcp_ch34x_baud_register(uint32_t baud_rate, uint8_t chip_version,
                                 uint16_t *value);
bool usb_vcp_ch34x_baud_request(uint32_t baud_rate, uint8_t chip_version,
                                usb_vcp_control_request_t *request);
uint16_t usb_vcp_ch34x_lcr_8n1(void);
size_t usb_vcp_ch34x_init_plan(uint32_t baud_rate, uint8_t chip_version,
                               usb_vcp_control_request_t *steps,
                               size_t capacity);
