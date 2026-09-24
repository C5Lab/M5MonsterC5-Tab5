#include "tab5_keyboard_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Model the STM32 firmware's wire format, not the host decoder's assumptions.
 * Slave_Complete_Callback reports size + 1 at 0x40 (modifier INCLUDED):
 * https://github.com/m5stack/M5Tab5-Keyboard-Internal-FW/blob/main/code/Keyboard_APP/Core/User/i2c/user_i2c_callback.c
 */
static bool plugged = true, fail_write;
static unsigned pending;
static uint8_t mode, length;
static uint8_t payload[10];
static bool read_reg(void *ctx, uint8_t reg, uint8_t *out, size_t n)
{
    (void)ctx;
    if (!plugged) return false;
    memset(out, 0, n);
    switch (reg) {
    case 0xfe: out[0] = 1; break;
    case 0xff: out[0] = 0x6d; break;
    case 0x10: out[0] = mode; break;
    case 0x40: out[0] = pending ? length : 0; break;
    case 0x50:
        if (n != length || n > sizeof(payload)) return false;
        memcpy(out, payload, n);
        pending--;
        break;
    default: assert(false);
    }
    return true;
}
static bool write_reg(void *ctx, uint8_t reg, uint8_t value)
{
    (void)ctx;
    if (!plugged || fail_write) return false;
    if (reg == 0x10) mode = value;
    if (reg == 0x02) pending = 0;
    return true;
}
static void put(const char *s, uint8_t modifiers)
{
    length = (uint8_t)(strlen(s) + 1);
    assert(length <= sizeof(payload));
    payload[0] = modifiers;
    memcpy(payload + 1, s, length - 1);
    pending = 1;
}
int main(void)
{
    tab5_keyboard_protocol_t kb = {0};
    tab5_key_t key;
    tab5_keyboard_io_t io = {read_reg, write_reg, NULL};
    plugged = false;
    assert(!tab5_keyboard_connect(&kb, &io));
    assert(!kb.connected);
    plugged = true;
    fail_write = true;
    assert(!tab5_keyboard_connect(&kb, &io));
    assert(!kb.connected); /* ACK alone must never hide the software keyboard. */
    fail_write = false;
    pending = 1;
    assert(tab5_keyboard_connect(&kb, &io));
    assert(kb.connected && mode == 2 && pending == 0);
    assert(!tab5_keyboard_poll(&kb, &io, &key));
    assert(kb.connected); /* Empty queue is not a disconnect. */
    put("A", 0);
    assert(length == 2 && payload[0] == 0 && payload[1] == 'A');
    assert(tab5_keyboard_poll(&kb, &io, &key));
    assert(key.kind == TAB5_KEY_TEXT && key.character == 'A');
    const struct { const char *name; tab5_key_kind_t kind; } cases[] = {
        {"backspace", TAB5_KEY_BACKSPACE}, {"BACKSPACE", TAB5_KEY_BACKSPACE},
        {"del", TAB5_KEY_DELETE}, {"ENTER", TAB5_KEY_ENTER},
        {"esc", TAB5_KEY_ESCAPE}, {"left", TAB5_KEY_LEFT},
        {"RIGHT", TAB5_KEY_RIGHT}, {"up", TAB5_KEY_UP},
        {"down", TAB5_KEY_DOWN}, {"tab", TAB5_KEY_TAB},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        put(cases[i].name, 0);
        assert(tab5_keyboard_poll(&kb, &io, &key));
        assert(key.kind == cases[i].kind);
    }
    put(" ", 0);
    assert(tab5_keyboard_poll(&kb, &io, &key) && key.character == ' ');
    put("x", 1);
    assert(!tab5_keyboard_poll(&kb, &io, &key)); /* Ctrl/Alt must not insert text. */
    put("x", 4);
    assert(!tab5_keyboard_poll(&kb, &io, &key));
    put("unknown", 0);
    assert(!tab5_keyboard_poll(&kb, &io, &key));
    put("", 0); /* Modifier-only packet is consumed without inserting a byte. */
    assert(!tab5_keyboard_poll(&kb, &io, &key));
    assert(pending == 0 && kb.connected);
    put("BACKSPACE", 0);
    assert(length == 10); /* Largest valid packet: modifier + 9-character name. */
    assert(tab5_keyboard_poll(&kb, &io, &key) && key.kind == TAB5_KEY_BACKSPACE);
    length = 11; pending = 1;
    assert(!tab5_keyboard_poll(&kb, &io, &key));
    assert(pending == 0);
    length = 255; pending = 1;
    assert(!tab5_keyboard_poll(&kb, &io, &key));
    assert(pending == 0); /* Invalid length never causes an oversized read. */
    plugged = false;
    tab5_keyboard_poll(&kb, &io, &key);
    assert(kb.connected); /* A transient bus error does not flicker keyboards. */
    plugged = true;
    tab5_keyboard_poll(&kb, &io, &key);
    assert(kb.connected && kb.failures == 0);
    plugged = false;
    for (int i = 0; i < 3; ++i) tab5_keyboard_poll(&kb, &io, &key);
    assert(!kb.connected);
    plugged = true;
    assert(tab5_keyboard_connect(&kb, &io));
    put("!", 0);
    assert(tab5_keyboard_poll(&kb, &io, &key) && key.character == '!');
    puts("Tab5 keyboard protocol: PASS");
}
