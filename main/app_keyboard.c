#include "app_keyboard.h"
#include "app_keyboard_navigation.h"
#include <stdlib.h>

typedef struct keyboard_binding {
    lv_obj_t *keyboard;
    lv_obj_t *textarea;
    bool requested_visible;
    struct keyboard_binding *next;
} keyboard_binding_t;

static keyboard_binding_t *bindings;
static bool physical_connected;

void app_keyboard_style_cursor(lv_obj_t *textarea, lv_color_t color)
{
    /* The light LVGL theme otherwise supplies a dark caret on our dark fields.
     * Hide the caret when unfocused; let LVGL own focus and the blink timer. */
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    /* Spinboxes inherit a contrasting digit color for their theme's block
     * cursor. Our transparent caret must keep the digit's normal text color. */
    lv_obj_set_style_text_color(textarea, color, LV_PART_CURSOR);
    lv_obj_set_style_border_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_style_selector_t focused = LV_PART_CURSOR | LV_STATE_FOCUSED;
    lv_obj_set_style_border_color(textarea, color, focused);
    lv_obj_set_style_border_width(textarea, 2, focused);
    lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT, focused);
    lv_obj_set_style_border_opa(textarea, LV_OPA_COVER, focused);
    lv_obj_set_style_anim_duration(textarea, 400, focused);
}

static keyboard_binding_t *find_binding(lv_obj_t *keyboard)
{
    for (keyboard_binding_t *b = bindings; b; b = b->next)
        if (b->keyboard == keyboard) return b;
    return NULL;
}

static void textarea_deleted(lv_event_t *e)
{
    keyboard_binding_t *b = lv_event_get_user_data(e);
    b->textarea = NULL;
    lv_keyboard_set_textarea(b->keyboard, NULL);
}

static void keyboard_deleted(lv_event_t *e)
{
    keyboard_binding_t *b = lv_event_get_user_data(e);
    if (b->textarea)
        lv_obj_remove_event_cb_with_user_data(b->textarea, textarea_deleted, b);
    keyboard_binding_t **p = &bindings;
    while (*p && *p != b) p = &(*p)->next;
    if (*p) *p = b->next;
    free(b);
}

lv_obj_t *app_keyboard_create(lv_obj_t *parent)
{
    lv_obj_t *keyboard = lv_keyboard_create(parent);
    if (!keyboard) return NULL;
    keyboard_binding_t *b = calloc(1, sizeof(*b));
    if (!b) return keyboard; /* Allocation failure leaves touch input usable. */
    *b = (keyboard_binding_t){keyboard, NULL, true, bindings};
    bindings = b;
    lv_obj_add_event_cb(keyboard, keyboard_deleted, LV_EVENT_DELETE, b);
    app_keyboard_set_visible(keyboard, true);
    return keyboard;
}

void app_keyboard_set_textarea(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    keyboard_binding_t *b = find_binding(keyboard);
    if (b && b->textarea != textarea) {
        if (b->textarea)
            lv_obj_remove_event_cb_with_user_data(b->textarea, textarea_deleted, b);
        b->textarea = textarea;
        if (textarea) lv_obj_add_event_cb(textarea, textarea_deleted, LV_EVENT_DELETE, b);
    }
    lv_keyboard_set_textarea(keyboard, textarea);
}

void app_keyboard_set_visible(lv_obj_t *keyboard, bool visible)
{
    keyboard_binding_t *b = find_binding(keyboard);
    if (b) b->requested_visible = visible;
    if (!visible || (b && physical_connected)) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void app_keyboard_set_connected(bool connected)
{
    physical_connected = connected;
    if (!connected) app_keyboard_navigation_clear_selection();
    for (keyboard_binding_t *b = bindings; b; b = b->next)
        app_keyboard_set_visible(b->keyboard, b->requested_visible);
}

static bool available(lv_obj_t *obj)
{
    if (!obj || lv_obj_has_state(obj, LV_STATE_DISABLED)) return false;
    for (lv_obj_t *p = obj; p; p = lv_obj_get_parent(p))
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(p, LV_STATE_DISABLED)) return false;
    lv_obj_t *root = lv_obj_get_screen(obj);
    return root == lv_screen_active() || root == lv_layer_top();
}

static bool unobstructed(lv_obj_t *textarea)
{
    lv_obj_update_layout(textarea);
    lv_obj_update_layout(lv_layer_top());
    lv_obj_update_layout(lv_layer_sys());
    lv_area_t area;
    lv_obj_get_coords(textarea, &area);
    lv_point_t point = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    lv_obj_t *hit = lv_indev_search_obj(lv_layer_sys(), &point);
    if (!hit) hit = lv_indev_search_obj(lv_layer_top(), &point);
    if (!hit) hit = lv_indev_search_obj(lv_screen_active(), &point);
    for (; hit; hit = lv_obj_get_parent(hit))
        if (hit == textarea) return true;
    return false;
}

/* Search within the same form, in widget order; never tab into another screen. */
static void find_next_textarea(lv_obj_t *root, lv_obj_t *current,
                               lv_obj_t **first, lv_obj_t **next, bool *passed)
{
    if (!available(root)) return;
    if (lv_obj_check_type(root, &lv_textarea_class)) {
        if (!*first) *first = root;
        if (*passed && !*next) *next = root;
        if (root == current) *passed = true;
    }
    uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < count; ++i)
        find_next_textarea(lv_obj_get_child(root, i), current, first, next, passed);
}

bool app_keyboard_input(const tab5_key_t *key)
{
    if (!physical_connected || !key) return false;
    /* Tab is the global tab-bar shortcut whenever that bar is accessible.
     * A modal that covers it retains local field-to-field Tab navigation. */
    if (key->kind == TAB5_KEY_TAB && app_keyboard_navigation_input(key)) return true;
    keyboard_binding_t *b;
    for (b = bindings; b; b = b->next) {
        if (b->requested_visible && available(b->textarea) &&
            available(lv_obj_get_parent(b->keyboard)) && unobstructed(b->textarea)) break;
    }
    /* Esc gives explicit exits priority within a form. A foreground editor
     * without one retains Cancel; a blocked dialog never cancels a page below. */
    if (key->kind == TAB5_KEY_ESCAPE &&
        app_keyboard_navigation_escape(b ? lv_obj_get_parent(b->keyboard) : NULL)) return true;
    if (!b) return key->kind == TAB5_KEY_ESCAPE ? false : app_keyboard_navigation_input(key);
    app_keyboard_navigation_clear_selection();
    lv_obj_t *keyboard = b->keyboard;
    lv_obj_t *ta = b->textarea;
    switch (key->kind) {
    case TAB5_KEY_TEXT: lv_textarea_add_char(ta, (uint8_t)key->character); break;
    case TAB5_KEY_BACKSPACE: lv_textarea_delete_char(ta); break;
    case TAB5_KEY_DELETE: lv_textarea_delete_char_forward(ta); break;
    case TAB5_KEY_LEFT: lv_textarea_cursor_left(ta); break;
    case TAB5_KEY_RIGHT: lv_textarea_cursor_right(ta); break;
    case TAB5_KEY_UP: lv_textarea_cursor_up(ta); break;
    case TAB5_KEY_DOWN: lv_textarea_cursor_down(ta); break;
    case TAB5_KEY_ENTER:
        if (!lv_textarea_get_one_line(ta)) {
            lv_textarea_add_char(ta, '\n');
            break;
        }
        /* fall through: single-line Enter is the on-screen OK button. */
        /* fallthrough */
    case TAB5_KEY_ESCAPE: {
        lv_event_code_t event = key->kind == TAB5_KEY_ENTER ? LV_EVENT_READY : LV_EVENT_CANCEL;
        /* A callback may delete the complete form, including this binding. */
        if (lv_obj_send_event(keyboard, event, NULL) != LV_RESULT_OK) return true;
        if (lv_obj_is_valid(ta)) lv_obj_send_event(ta, event, NULL);
        return true;
    }
    case TAB5_KEY_TAB: {
        lv_obj_t *first = NULL, *next = NULL;
        bool passed = false;
        find_next_textarea(lv_obj_get_parent(keyboard), ta, &first, &next, &passed);
        if (!next) next = first;
        if (next && next != ta) {
            lv_obj_remove_state(ta, LV_STATE_FOCUSED);
            app_keyboard_set_textarea(keyboard, next);
            lv_obj_add_state(next, LV_STATE_FOCUSED);
            lv_obj_scroll_to_view(next, LV_ANIM_OFF);
            lv_obj_send_event(next, LV_EVENT_FOCUSED, NULL);
        }
        break;
    }
    }
    return !lv_obj_is_valid(keyboard) || !lv_obj_is_valid(ta);
}
