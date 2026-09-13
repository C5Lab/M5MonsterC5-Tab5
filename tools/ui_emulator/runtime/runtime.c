#include "runtime.h"
#include "coordinates.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static lv_display_t *display;
static uint16_t framebuffer[720 * 1280];
static uint16_t draw_buffer[1280 * 64];
static lv_indev_t *pointer;
static int pointer_x, pointer_y;
static bool pointer_down;
static double tick_fraction;
static lv_obj_t *numeric_edit;
static char numeric_draft[5];

static void numeric_deleted(lv_event_t *event) {
    (void)event;
    numeric_edit = NULL;
}

static void finish_numeric(bool commit) {
    if (!numeric_edit) return;
    lv_obj_t *obj = numeric_edit;
    numeric_edit = NULL;
    lv_obj_remove_event_cb(obj, numeric_deleted);
    int value = commit && numeric_draft[0] ? atoi(numeric_draft) : lv_spinbox_get_value(obj);
    lv_spinbox_set_value(obj, value);
}

void emu_unavailable(const char *name) {
    EM_ASM({
        globalThis.dispatchEvent(new CustomEvent('emulator-unavailable', {detail:UTF8ToString($0)}));
    }, name);
}

static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
    const int width = area->x2 - area->x1 + 1;
    const unsigned stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(framebuffer + y * emu_width() + area->x1,
               pixels + (y - area->y1) * stride, width * sizeof(uint16_t));
    }
    lv_display_flush_ready(d);
}

static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = pointer_x;
    data->point.y = pointer_y;
    int x = data->point.x, y = data->point.y;
    emu_logical_to_raw(lv_display_get_rotation(display), &x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int emu_init(int rotation) {
    if (display || rotation < 0 || rotation > 3) return 0;
    lv_init();
    display = lv_display_create(720, 1280);
    if (!display) return 0;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(display, (lv_display_rotation_t)rotation);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_pointer);
    /* read_pointer inverts LVGL's physical-to-logical rotation. */
    lv_indev_set_display(pointer, display);
    app_init();
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(display);
    return 1;
}

void emu_tick(double elapsed_ms) {
    if (!display || !isfinite(elapsed_ms) || elapsed_ms < 0) return;
    /* Avoid jumping deadlines after tab suspension; each callback stays bounded. */
    double elapsed = elapsed_ms > 100 ? 100 : elapsed_ms;
    tick_fraction += elapsed;
    uint32_t whole = (uint32_t)tick_fraction;
    tick_fraction -= whole;
    lv_tick_inc(whole);
    app_tick(elapsed);
    lv_timer_handler();
}

void emu_pointer(int x, int y, int pressed) {
    if (!display) return;
    if (pressed && !pointer_down) finish_numeric(true);
    pointer_x = x < 0 ? 0 : x >= emu_width() ? emu_width() - 1 : x;
    pointer_y = y < 0 ? 0 : y >= emu_height() ? emu_height() - 1 : y;
    pointer_down = pressed != 0;
    /* Read on every DOM event, so quick press/release is not lost between frames. */
    lv_indev_read(pointer);
}

void emu_pointer_cancel(void) {
    if (!display) return;
    pointer_down = false;
    lv_indev_reset(pointer, NULL);
    lv_indev_read(pointer);
}

static lv_obj_t *find_textarea(lv_obj_t *root) {
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_has_class(root, &lv_textarea_class) && lv_obj_has_state(root, LV_STATE_FOCUSED)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *found = find_textarea(lv_obj_get_child(root, i));
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t *focused_textarea(void) {
    lv_obj_t *found = find_textarea(lv_layer_top());
    return found ? found : find_textarea(lv_screen_active());
}

void emu_key(int key) {
    if (!display) return;
    lv_obj_t *focused = focused_textarea();
    if (!focused) return;
    if (lv_obj_check_type(focused, &lv_spinbox_class)) {
        if (key == 13 || key == 27) { finish_numeric(key == 13); return; }
        if (key == 8) {
            if (!numeric_edit) {
                numeric_edit = focused;
                snprintf(numeric_draft, sizeof(numeric_draft), "%ld", (long)lv_spinbox_get_value(focused));
                lv_obj_add_event_cb(focused, numeric_deleted, LV_EVENT_DELETE, NULL);
            }
            size_t length = strlen(numeric_draft);
            if (length) numeric_draft[length - 1] = 0;
            lv_textarea_set_text(focused, numeric_draft);
        }
        return;
    }
    if (key == 8) lv_textarea_delete_char(focused);
    else if (key >= 32 && key < 127) lv_textarea_add_char(focused, (uint32_t)key);
}

void emu_text(const char *text) {
    if (!display || !text) return;
    lv_obj_t *focused = focused_textarea();
    if (!focused) return;
    if (lv_obj_check_type(focused, &lv_spinbox_class)) {
        /* Browser numeric entry is an explicit adapter. Preserve the firmware
           Spinbox range and commit its value before a production Save click. */
        size_t length = strlen(text), prior = numeric_edit ? strlen(numeric_draft) : 0;
        if (!length || prior + length > 4) return;
        for (size_t i = 0; i < length; i++) if (text[i] < '0' || text[i] > '9') return;
        if (!numeric_edit) {
            numeric_edit = focused;
            numeric_draft[0] = 0;
            lv_obj_add_event_cb(focused, numeric_deleted, LV_EVENT_DELETE, NULL);
        }
        memcpy(numeric_draft + prior, text, length + 1);
        lv_textarea_set_text(focused, numeric_draft);
    } else lv_textarea_add_text(focused, text);
}

void emu_wheel(int x, int y, int delta) {
    if (!display) return;
    lv_point_t point = {x, y};
    lv_obj_t *obj = lv_indev_search_obj(lv_layer_top(), &point);
    if (!obj) obj = lv_indev_search_obj(lv_screen_active(), &point);
    while (obj) {
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) &&
            ((delta > 0 && lv_obj_get_scroll_bottom(obj) > 0) ||
             (delta < 0 && lv_obj_get_scroll_top(obj) > 0))) {
            lv_obj_scroll_by(obj, 0, -delta, LV_ANIM_OFF);
            return;
        }
        obj = lv_obj_get_parent(obj);
    }
}

static void inspect_object(lv_obj_t *obj, uintptr_t parent) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const char *text = lv_obj_check_type(obj, &lv_label_class) ? lv_label_get_text(obj) :
        lv_obj_check_type(obj, &lv_textarea_class) ? lv_textarea_get_text(obj) : "";
    EM_ASM({ globalThis.emulatorObjects.push({id:$0,parent:$1,x:$2,y:$3,width:$4,height:$5,
        text:UTF8ToString($6),clickable:!!$7,state:$8,scrollY:$9}); },
        (uintptr_t)obj, parent, area.x1, area.y1, lv_area_get_width(&area), lv_area_get_height(&area),
        text, lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE), lv_obj_get_state(obj), lv_obj_get_scroll_y(obj));
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) inspect_object(lv_obj_get_child(obj, i), (uintptr_t)obj);
}

void emu_inspect(void) {
    if (!display) return;
    lv_obj_update_layout(lv_screen_active());
    EM_ASM({ globalThis.emulatorObjects = []; });
    inspect_object(lv_screen_active(), 0);
    inspect_object(lv_layer_top(), 0);
}

int emu_width(void) { return display ? lv_display_get_horizontal_resolution(display) : 0; }
int emu_height(void) { return display ? lv_display_get_vertical_resolution(display) : 0; }
uintptr_t emu_framebuffer(void) { return (uintptr_t)framebuffer; }
