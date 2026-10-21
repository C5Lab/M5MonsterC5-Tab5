#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TAB5_KEY_TEXT, TAB5_KEY_BACKSPACE, TAB5_KEY_DELETE, TAB5_KEY_ENTER,
    TAB5_KEY_ESCAPE, TAB5_KEY_LEFT, TAB5_KEY_RIGHT, TAB5_KEY_UP,
    TAB5_KEY_DOWN, TAB5_KEY_TAB,
} tab5_key_kind_t;

typedef struct {
    tab5_key_kind_t kind;
    char character;
} tab5_key_t;

typedef struct {
    bool (*read)(void *ctx, uint8_t reg, uint8_t *data, size_t size);
    bool (*write)(void *ctx, uint8_t reg, uint8_t value);
    void *ctx;
} tab5_keyboard_io_t;

typedef struct {
    bool connected;
    uint8_t failures;
} tab5_keyboard_protocol_t;

bool tab5_keyboard_connect(tab5_keyboard_protocol_t *kb, const tab5_keyboard_io_t *io);
/* One FIFO event per call. False means no usable key (also check connected). */
bool tab5_keyboard_poll(tab5_keyboard_protocol_t *kb, const tab5_keyboard_io_t *io,
                       tab5_key_t *key);
