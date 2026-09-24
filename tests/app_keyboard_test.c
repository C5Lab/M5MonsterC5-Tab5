#include "app_keyboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool key(tab5_key_kind_t kind, char c)
{
    tab5_key_t input = {kind, c};
    return app_keyboard_input(&input);
}
static void delete_form(lv_event_t *event)
{
    lv_obj_delete(lv_event_get_user_data(event));
}

static const uint8_t *wire_packet;
static size_t wire_size;
static bool wire_read(void *ctx, uint8_t reg, uint8_t *out, size_t size)
{
    (void)ctx;
    if (reg == 0x40 && size == 1) { out[0] = (uint8_t)wire_size; return true; }
    if (reg == 0x50 && size == wire_size) {
        memcpy(out, wire_packet, size);
        wire_size = 0;
        return true;
    }
    return false;
}
static void field_clicked(lv_event_t *event)
{
    lv_obj_t *keyboard = lv_event_get_user_data(event);
    app_keyboard_set_textarea(keyboard, lv_event_get_target(event));
    app_keyboard_set_visible(keyboard, true);
}

static void test_filter_field_from_wire(void)
{
    /* Match the Filter form's ownership: page > overlay > popup > scrollable
     * form > field, with a separate floating keyboard owned by the overlay. */
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(page, 0, 90);
    lv_obj_set_size(page, 720, 1100);
    lv_obj_t *overlay = lv_obj_create(page);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 640, 650);
    lv_obj_center(popup);
    lv_obj_t *form = lv_obj_create(popup);
    lv_obj_set_size(form, lv_pct(100), 400);
    lv_obj_set_scroll_dir(form, LV_DIR_VER);
    lv_obj_t *field = lv_textarea_create(form);
    lv_obj_set_size(field, lv_pct(100), 46);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_text(field, "");
    lv_obj_t *keyboard = app_keyboard_create(overlay);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(keyboard, lv_pct(100), 240);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    app_keyboard_set_textarea(keyboard, field);
    app_keyboard_set_visible(keyboard, false);
    lv_obj_add_event_cb(field, field_clicked, LV_EVENT_CLICKED, keyboard);
    app_keyboard_set_connected(true);
    lv_obj_send_event(field, LV_EVENT_CLICKED, NULL);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));

    /* Firmware-faithful register fixtures: 0x40 includes modifier at byte 0.
     * Pass through the real decoder AND real LVGL field routing. */
    static const struct { uint8_t size; uint8_t bytes[10]; } events[] = {
        {2, {0, 'L'}}, {2, {0, 'a'}}, {2, {0, 'b'}}, {2, {0, ' '}},
        {2, {0, '2'}}, {2, {0, '4'}},
        {10, {0, 'b', 'a', 'c', 'k', 's', 'p', 'a', 'c', 'e'}},
        {2, {0, 'G'}},
    };
    tab5_keyboard_protocol_t protocol = {.connected = true};
    const tab5_keyboard_io_t io = {wire_read, NULL, NULL};
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        wire_packet = events[i].bytes;
        wire_size = events[i].size;
        tab5_key_t decoded;
        assert(tab5_keyboard_poll(&protocol, &io, &decoded));
        assert(!app_keyboard_input(&decoded));
    }
    assert(!strcmp(lv_textarea_get_text(field), "Lab 2G"));
    lv_obj_delete(page);
    app_keyboard_set_connected(false);
}
int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(720, 1280);
    lv_obj_t *form = lv_obj_create(lv_screen_active());
    lv_obj_set_size(form, 650, 600);
    lv_obj_t *ta = lv_textarea_create(form);
    lv_obj_set_size(ta, 300, 50);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, "");
    lv_obj_t *kb = app_keyboard_create(form);
    app_keyboard_set_textarea(kb, ta);
    assert(!lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    app_keyboard_set_connected(true);
    assert(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    app_keyboard_set_visible(kb, true);
    assert(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    key(TAB5_KEY_TEXT, 'A'); key(TAB5_KEY_TEXT, 'b'); key(TAB5_KEY_TEXT, 'c');
    key(TAB5_KEY_LEFT, 0); key(TAB5_KEY_BACKSPACE, 0); key(TAB5_KEY_DELETE, 0);
    assert(!strcmp(lv_textarea_get_text(ta), "A"));
    lv_textarea_set_max_length(ta, 2);
    key(TAB5_KEY_TEXT, '1'); key(TAB5_KEY_TEXT, '2');
    assert(!strcmp(lv_textarea_get_text(ta), "A1"));
    app_keyboard_set_connected(false);
    assert(!lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN));
    key(TAB5_KEY_BACKSPACE, 0);
    assert(!strcmp(lv_textarea_get_text(ta), "A1"));
    app_keyboard_set_visible(kb, false);
    app_keyboard_set_connected(true);
    app_keyboard_set_connected(false);
    assert(lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)); /* Preserve a dismissed keyboard. */
    app_keyboard_set_connected(true);
    app_keyboard_set_visible(kb, true);
    lv_obj_t *second = lv_textarea_create(form);
    lv_obj_set_size(second, 300, 100);
    lv_obj_set_y(second, 100);
    lv_textarea_set_text(second, "");
    assert(!key(TAB5_KEY_TAB, 0)); key(TAB5_KEY_TEXT, 'x');
    assert(!strcmp(lv_textarea_get_text(second), "x"));
    assert(!key(TAB5_KEY_ENTER, 0)); /* Multiline Enter must preserve queued text. */
    assert(!strcmp(lv_textarea_get_text(second), "x\n"));
    lv_obj_delete(second); /* Must detach the target, not leave a stale pointer. */
    key(TAB5_KEY_TEXT, 'z');
    assert(lv_keyboard_get_textarea(kb) == NULL);
    app_keyboard_set_textarea(kb, ta);

    /* An unrelated confirmation dialog must block underlying text/commands. */
    lv_obj_t *confirmation = lv_obj_create(lv_layer_top());
    lv_obj_set_size(confirmation, 720, 1280);
    key(TAB5_KEY_BACKSPACE, 0);
    assert(!strcmp(lv_textarea_get_text(ta), "A1"));
    lv_obj_delete(confirmation);

    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 650, 600);
    lv_obj_t *modal_ta = lv_textarea_create(modal);
    lv_obj_set_size(modal_ta, 300, 50);
    lv_textarea_set_text(modal_ta, "");
    lv_textarea_set_one_line(modal_ta, true);
    lv_obj_t *modal_kb = app_keyboard_create(modal);
    app_keyboard_set_textarea(modal_kb, modal_ta);
    assert(lv_obj_has_flag(modal_kb, LV_OBJ_FLAG_HIDDEN));
    key(TAB5_KEY_TEXT, 'M');
    assert(!strcmp(lv_textarea_get_text(modal_ta), "M"));
    assert(!strcmp(lv_textarea_get_text(ta), "A1"));
    lv_obj_add_event_cb(modal_kb, delete_form, LV_EVENT_READY, modal);
    assert(key(TAB5_KEY_ENTER, 0)); /* Callback destroys the form; flush its keys. */
    key(TAB5_KEY_BACKSPACE, 0);
    assert(!strcmp(lv_textarea_get_text(ta), "A"));
    lv_obj_add_flag(form, LV_OBJ_FLAG_HIDDEN);
    key(TAB5_KEY_TEXT, 'Z');
    assert(!strcmp(lv_textarea_get_text(ta), "A"));
    lv_obj_remove_flag(form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, delete_form, LV_EVENT_CANCEL, form);
    key(TAB5_KEY_ESCAPE, 0);
    key(TAB5_KEY_TEXT, 'z');
    app_keyboard_set_connected(false);
    test_filter_field_from_wire();
    lv_display_delete(display);
    lv_deinit();
    puts("Tab5 keyboard LVGL integration: PASS");
}
