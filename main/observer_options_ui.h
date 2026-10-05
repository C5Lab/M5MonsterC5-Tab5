#pragma once
#include "observer_options.h"
#include "lvgl.h"
#include "nvs.h"
#include "app_keyboard_navigation.h"

/* Included once by main.c: one preference set shared by every transport. */
static observer_view_prefs_t observer_view_prefs;
static bool observer_options_loaded;
static observer_view_prefs_t observer_options_draft;
static lv_obj_t *observer_options_modal;
static lv_obj_t *observer_options_overlay;
static lv_obj_t *observer_options_lists[2];
static lv_obj_t *observer_options_status;
static void (*observer_options_on_apply)(void);

/* Explicit Wardrive Setup palette: native LVGL may use the light default
 * theme, so every new Observer surface/state declares its own colors. */
static void observer_style_button(lv_obj_t *button, uint32_t background) {
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x333333), LV_STATE_DISABLED);
    lv_obj_set_style_recolor_opa(button, LV_OPA_TRANSP, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(button, lv_color_lighten(lv_color_hex(background), 20), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(0x888888), LV_STATE_DISABLED);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_outline_color(button, lv_color_hex(0x009688), LV_STATE_FOCUS_KEY);
}
static void observer_style_dark_tabs(lv_obj_t *tabs) {
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_text_color(tabs, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(tabs, &lv_font_montserrat_14, 0);
    lv_obj_t *content = lv_tabview_get_content(tabs);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabs);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0A1A1A), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    for (uint32_t i = 0; i < lv_tabview_get_tab_count(tabs); ++i) {
        lv_obj_t *button = lv_tabview_get_tab_button(tabs, i);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x1A1A2A), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x009688), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x455A64), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x009688), LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x1A1A2A), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x009688), LV_STATE_CHECKED | LV_STATE_FOCUS_KEY);
        lv_obj_set_style_text_color(button, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(button, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
        lv_obj_set_style_text_font(button, &lv_font_montserrat_14, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_outline_color(button, lv_color_hex(0x009688), LV_STATE_FOCUS_KEY);
    }
}

static void observer_options_load(void) {
    if (observer_options_loaded) return;
    observer_prefs_defaults(&observer_view_prefs);
    nvs_handle_t handle;
    if (nvs_open("observer", NVS_READONLY, &handle) == ESP_OK) {
        observer_view_prefs_t stored;
        size_t size = sizeof(stored);
        if (nvs_get_blob(handle, "view_prefs", &stored, &size) == ESP_OK)
            observer_prefs_decode(&stored, size, &observer_view_prefs);
        nvs_close(handle);
    }
    observer_options_loaded = true;
}
static lv_obj_t *observer_options_button(lv_obj_t *parent, const char *text) {
    lv_obj_t *button = lv_button_create(parent);
    observer_style_button(button, 0x455A64);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_height(button, 44);
    return button;
}
static void observer_options_render(void);
/* User data packs list, row and direction without heap-owned event payloads. */
static void observer_options_field_event(lv_event_t *event) {
    uintptr_t key = (uintptr_t)lv_event_get_user_data(event);
    size_t list = (key >> 8) & 1, row = (key >> 2) & 31;
    unsigned action = key & 3;
    uint8_t *order = list ? observer_options_draft.client_order : observer_options_draft.ap_order;
    uint8_t *visible = list ? observer_options_draft.client_visible : observer_options_draft.ap_visible;
    size_t count = list ? OBS_CLIENT_FIELD_COUNT : OBS_FIELD_COUNT;
    if (action == 0) {
        visible[order[row]] = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED) ? 1 : 0;
    } else if (observer_prefs_move(order, count, row, action == 1 ? -1 : 1)) {
        observer_options_render();
    }
}
static void observer_options_render(void) {
    static const char *ap_names[] = {"Security", "PMF", "WPS", "Channel / band",
        "AP RSSI", "Last seen", "BSSID", "Uptime", "Vendor", "Pairwise ciphers",
        "Group cipher", "AKM", "SSID discovery source"};
    static const char *client_names[] = {"Vendor", "RSSI", "Last seen"};
    for (size_t list = 0; list < 2; ++list) {
        lv_obj_t *parent = observer_options_lists[list];
        int32_t scroll_y = lv_obj_get_scroll_y(parent);
        lv_obj_clean(parent);
        size_t count = list ? OBS_CLIENT_FIELD_COUNT : OBS_FIELD_COUNT;
        uint8_t *order = list ? observer_options_draft.client_order : observer_options_draft.ap_order;
        uint8_t *visible = list ? observer_options_draft.client_visible : observer_options_draft.ap_visible;
        for (size_t i = 0; i < count; ++i) {
            lv_obj_t *row = lv_obj_create(parent);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(row, 6, 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_text_color(row, lv_color_hex(0xFFFFFF), 0);
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_t *check = lv_checkbox_create(row);
            lv_checkbox_set_text(check, list ? client_names[order[i]] : ap_names[order[i]]);
            lv_obj_set_style_text_color(check, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(check, &lv_font_montserrat_14, 0);
            lv_obj_set_style_outline_color(check, lv_color_hex(0x009688), LV_STATE_FOCUS_KEY);
            lv_obj_set_style_bg_color(check, lv_color_hex(0x0A1A1A), LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(check, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_border_color(check, lv_color_hex(0x455A64), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(check, lv_color_hex(0x009688), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_border_color(check, lv_color_hex(0x009688), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_style_text_color(check, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_CHECKED);
            lv_obj_set_flex_grow(check, 1);
            if (visible[order[i]]) lv_obj_add_state(check, LV_STATE_CHECKED);
            uintptr_t key = (list << 8) | (i << 2);
            lv_obj_add_event_cb(check, observer_options_field_event, LV_EVENT_VALUE_CHANGED, (void *)key);
            for (unsigned action = 1; action <= 2; ++action) {
                lv_obj_t *button = observer_options_button(row, action == 1 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
                lv_obj_set_width(button, 44);
                if ((action == 1 && i == 0) || (action == 2 && i + 1 == count))
                    lv_obj_add_state(button, LV_STATE_DISABLED);
                lv_obj_add_event_cb(button, observer_options_field_event, LV_EVENT_CLICKED, (void *)(key | action));
            }
        }
        lv_obj_update_layout(parent);
        lv_obj_scroll_to_y(parent, scroll_y, LV_ANIM_OFF);
    }
}
static void observer_options_deleted(lv_event_t *event) {
    (void)event;
    observer_options_modal = NULL;
    observer_options_overlay = NULL;
    observer_options_on_apply = NULL;
}
static void observer_options_close(void) {
    if (observer_options_overlay) lv_obj_delete(observer_options_overlay);
}
static void observer_options_action(lv_event_t *event) {
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    if (action == 1) {
        observer_prefs_defaults(&observer_options_draft);
        observer_options_render();
        return;
    }
    void (*callback)(void) = observer_options_on_apply;
    if (action == 2 && !observer_prefs_equal(&observer_options_draft, &observer_view_prefs)) {
        nvs_handle_t handle;
        esp_err_t result = nvs_open("observer", NVS_READWRITE, &handle);
        if (result == ESP_OK) {
            result = nvs_set_blob(handle, "view_prefs", &observer_options_draft, sizeof(observer_options_draft));
            if (result == ESP_OK) result = nvs_commit(handle);
            nvs_close(handle);
        }
        if (result != ESP_OK) {
            lv_label_set_text(observer_options_status, "Could not save preferences. Try Apply again.");
            return;
        }
        observer_view_prefs = observer_options_draft;
    }
    observer_options_close();
    if (action == 2 && callback) callback();
}
static void observer_options_open(lv_obj_t *parent, void (*on_apply)(void)) {
    observer_options_load();
    if (observer_options_modal) return;
    observer_options_draft = observer_view_prefs;
    observer_options_on_apply = on_apply;
    observer_options_overlay = lv_obj_create(parent);
    lv_obj_set_size(observer_options_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(observer_options_overlay);
    lv_obj_set_style_pad_all(observer_options_overlay, 0, 0);
    lv_obj_set_style_border_width(observer_options_overlay, 0, 0);
    lv_obj_set_style_radius(observer_options_overlay, 0, 0);
    lv_obj_set_style_bg_color(observer_options_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(observer_options_overlay, LV_OPA_60, 0);
    lv_obj_remove_flag(observer_options_overlay, LV_OBJ_FLAG_SCROLLABLE);
    observer_options_modal = lv_obj_create(observer_options_overlay);
    lv_obj_set_size(observer_options_modal, LV_PCT(96), LV_PCT(96));
    lv_obj_center(observer_options_modal);
    lv_obj_set_style_bg_color(observer_options_modal, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_bg_opa(observer_options_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(observer_options_modal, lv_color_hex(0x009688), 0);
    lv_obj_set_style_border_width(observer_options_modal, 3, 0);
    lv_obj_set_style_radius(observer_options_modal, 16, 0);
    lv_obj_set_style_pad_all(observer_options_modal, 12, 0);
    lv_obj_set_style_text_color(observer_options_modal, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(observer_options_modal, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_flow(observer_options_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(observer_options_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(observer_options_modal, observer_options_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = lv_label_create(observer_options_modal);
    lv_label_set_text(title, "Observer Options");
    lv_obj_set_style_text_color(title, lv_color_hex(0x009688), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_t *tabs = lv_tabview_create(observer_options_modal);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_height(tabs, 0);
    lv_obj_set_flex_grow(tabs, 1);
    lv_tabview_set_tab_bar_size(tabs, 44);
    observer_options_lists[0] = lv_tabview_add_tab(tabs, "Networks");
    observer_options_lists[1] = lv_tabview_add_tab(tabs, "Clients");
    observer_style_dark_tabs(tabs);
    for (size_t i = 0; i < 2; ++i) {
        lv_obj_set_flex_flow(observer_options_lists[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(observer_options_lists[i], 4, 0);
        lv_obj_set_style_bg_opa(observer_options_lists[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(observer_options_lists[i], 0, 0);
        lv_obj_set_style_text_color(observer_options_lists[i], lv_color_hex(0xFFFFFF), 0);
    }
    observer_options_status = lv_label_create(observer_options_modal);
    lv_obj_set_width(observer_options_status, LV_PCT(100));
    lv_label_set_text(observer_options_status, "Changes apply to all Observer transports.");
    lv_obj_set_style_text_color(observer_options_status, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(observer_options_status, &lv_font_montserrat_12, 0);
    lv_obj_t *bar = lv_obj_create(observer_options_modal);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    const char *names[] = {"Cancel", "Restore defaults", "Apply"};
    for (uintptr_t i = 0; i < 3; ++i) {
        lv_obj_t *button = observer_options_button(bar, names[i]);
        observer_style_button(button, i == 0 ? 0x444444 : i == 1 ? 0x2196F3 : 0x4CAF50);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x555555), LV_STATE_DISABLED);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_add_event_cb(button, observer_options_action, LV_EVENT_CLICKED, (void *)i);
        if (i == 0) app_keyboard_navigation_register_escape(button);
    }
    observer_options_render();
}
