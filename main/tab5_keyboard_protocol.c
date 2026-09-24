#include "tab5_keyboard_protocol.h"
#include <string.h>

/* A164 Character-mode protocol. Version/address are FE/FF, as confirmed by
 * M5Stack's driver and STM32 firmware (the PDF labels their row as F0).
 * https://github.com/m5stack/M5Tab5-Keyboard-UserDemo */
bool tab5_keyboard_connect(tab5_keyboard_protocol_t *kb, const tab5_keyboard_io_t *io)
{
    uint8_t version, address, mode;
    kb->connected = false;
    kb->failures = 0;
    if (!io->read(io->ctx, 0xfe, &version, 1) || version == 0xff ||
        !io->read(io->ctx, 0xff, &address, 1) || address != 0x6d ||
        !io->write(io->ctx, 0x10, 2) ||
        !io->read(io->ctx, 0x10, &mode, 1) || mode != 2 ||
        !io->write(io->ctx, 0x00, 0) || /* Polling: no interrupt pin ownership. */
        !io->write(io->ctx, 0x02, 0)) return false;
    kb->connected = true;
    return true;
}

static bool failed(tab5_keyboard_protocol_t *kb)
{
    if (++kb->failures >= 3) kb->connected = false;
    return false;
}

bool tab5_keyboard_poll(tab5_keyboard_protocol_t *kb, const tab5_keyboard_io_t *io,
                       tab5_key_t *key)
{
    if (!kb->connected) return false;
    uint8_t packet_size;
    if (!io->read(io->ctx, 0x40, &packet_size, 1)) return failed(kb);
    if (packet_size > 10) {
        if (!io->write(io->ctx, 0x02, 0)) return failed(kb);
        kb->failures = 0;
        return false;
    }
    if (!packet_size) {
        kb->failures = 0;
        return false;
    }
    uint8_t payload[10];
    /* STM32 Slave_Complete_Callback returns the complete packet size at 0x40:
     * modifier + character/name bytes. Do not add the modifier a second time.
     * In particular, a letter is 2 bytes and "backspace" is 10 bytes. */
    if (!io->read(io->ctx, 0x50, payload, packet_size)) return failed(kb);
    kb->failures = 0;
    uint8_t length = packet_size - 1;
    if (payload[0] != 0) return false; /* No Ctrl/Alt shortcuts are assigned. */
    if (length == 1 && payload[1] >= 32 && payload[1] <= 126) {
        *key = (tab5_key_t){TAB5_KEY_TEXT, (char)payload[1]};
        return true;
    }
    char name[10];
    for (unsigned i = 0; i < length; ++i) {
        uint8_t c = payload[i + 1];
        name[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    name[length] = '\0';
    static const struct { const char *name; tab5_key_kind_t kind; } keys[] = {
        {"backspace", TAB5_KEY_BACKSPACE}, {"del", TAB5_KEY_DELETE},
        {"enter", TAB5_KEY_ENTER}, {"esc", TAB5_KEY_ESCAPE},
        {"left", TAB5_KEY_LEFT}, {"right", TAB5_KEY_RIGHT},
        {"up", TAB5_KEY_UP}, {"down", TAB5_KEY_DOWN}, {"tab", TAB5_KEY_TAB},
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (!strcmp(name, keys[i].name)) {
            *key = (tab5_key_t){keys[i].kind, 0};
            return true;
        }
    }
    return false;
}
