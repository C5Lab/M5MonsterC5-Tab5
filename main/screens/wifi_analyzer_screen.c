#include "wifi_analyzer_screen.h"
#include "wifi_analyzer.h"
#include "subghz_host.h"
#include "app_keyboard.h"
#include "app_keyboard_navigation.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Only the LVGL thread touches this state. The controller owns transport and
 * session lifetime; a view is a separate PSRAM copy, never a worker pointer. */
typedef struct {
    bool initialized, dirty, pending_back, editing_channels;
    int tab, chart_band, sort;
    bool descending, full_band, motion, details_open;
    unsigned zoom;
    int center_mhz, reveal;
    wa_scan_request_t request;
    wa_filter_t filter;
    wa_session_t *session;
    wa_snapshot_t *snapshot;
    wa_status_t status;
    uint32_t rendered_revision;
    char selected[18];
    lv_obj_t *root, *body, *back, *scan, *stop, *panel, *chart, *rows;
    lv_obj_t *status_label, *summary, *scope, *channel_counts, *empty;
    lv_obj_t *details, *detail_text, *detail_scroll, *selection, *editor, *textarea, *keyboard;
    lv_obj_t *acq_band, *profile, *limit, *repeat, *channels;
    lv_obj_t *filter_band, *primary, *width, *auth, *search, *rssi, *rssi_label;
    lv_obj_t *chart_selector, *chart_range, *chart_zoom, *pan_left, *pan_right, *motion_selector;
    lv_obj_t *sort_buttons[3];
    lv_obj_t *advice_panel, *advice_text, *advice_pool, *advice_own;
    lv_timer_t *timer;
    void (*on_back)(int tab);
    uint16_t indices[WA_MAX_APS], visible_count;
} wa_ui_t;

static wa_ui_t views[3];
static const int widths[] = {-1, 0, 20, 40, 80, 160, 8080};
static const uint32_t repeats[] = {0, 2000, 5000, 10000};
static const int primary_channels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112,
    116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177};
static const char *auth_options = "All security\nOPEN\nWEP\nWPA_PSK\nWPA2_PSK\nWPA_WPA2_PSK\nWPA2_ENTERPRISE\nWPA3_PSK\nWPA2_WPA3_PSK\nWAPI_PSK\nOWE\nWPA3_ENT_192\nWPA3_EXT_PSK\nWPA3_EXT_PSK_MIXED_MODE\nDPP\nWPA3_ENTERPRISE\nWPA2_WPA3_ENTERPRISE\nWPA_ENTERPRISE\nUNKNOWN";

static void refresh(wa_ui_t *u);
static void update_details(wa_ui_t *u);
static void update_channel_counts(wa_ui_t *u);
static void details_transition(wa_ui_t *u, bool open);
static void update_advice(wa_ui_t *u);

static lv_obj_t *label(lv_obj_t *parent, const char *text, bool muted)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_style_text_color(o, muted ? subghz_host_ui_muted() : subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(o, &lv_font_montserrat_14, 0);
    return o;
}

static lv_obj_t *container(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_width(o, lv_pct(100));
    lv_obj_set_height(o, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_pad_gap(o, 10, 0);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int width,
                        lv_event_cb_t cb, wa_ui_t *u)
{
    lv_obj_t *o = lv_button_create(parent);
    lv_obj_set_size(o, width, 48);
    lv_obj_set_style_bg_color(o, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_color(o, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, subghz_host_ui_border(), 0);
    lv_obj_set_style_radius(o, 8, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_t *t = label(o, text, false);
    lv_obj_center(t);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, u);
    return o;
}

static void button_text(lv_obj_t *o, const char *text)
{
    lv_label_set_text(lv_obj_get_child(o, 0), text);
}

static lv_obj_t *field(lv_obj_t *parent, const char *name)
{
    lv_obj_t *o = container(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(o, 190);
    lv_obj_set_style_pad_gap(o, 6, 0);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    label(o, name, true);
    return o;
}

static lv_obj_t *dropdown(lv_obj_t *parent, const char *name, const char *options,
                          unsigned selected, lv_event_cb_t cb, wa_ui_t *u)
{
    lv_obj_t *o = lv_dropdown_create(field(parent, name));
    lv_obj_set_size(o, lv_pct(100), 48);
    lv_dropdown_set_options(o, options);
    lv_dropdown_set_selected(o, selected);
    lv_obj_set_style_bg_color(o, subghz_host_ui_card(), 0);
    lv_obj_set_style_text_color(o, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(o, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(o, subghz_host_ui_border(), 0);
    lv_obj_t *list = lv_dropdown_get_list(o);
    lv_obj_set_style_bg_color(list, subghz_host_ui_card(), 0);
    lv_obj_set_style_text_color(list, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_16, 0);
    /* Dropdown hit testing uses font height + line spacing, not item padding. */
    lv_obj_set_style_text_line_space(list, 30, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_16, LV_PART_SELECTED);
    lv_obj_set_style_text_line_space(list, 30, LV_PART_SELECTED);
    lv_obj_add_event_cb(o, cb, LV_EVENT_VALUE_CHANGED, u);
    return o;
}

static void set_disabled(lv_obj_t *o, bool disabled)
{
    if (disabled) lv_obj_add_state(o, LV_STATE_DISABLED);
    else lv_obj_remove_state(o, LV_STATE_DISABLED);
}

static void controls(wa_ui_t *u)
{
    bool busy = wa_busy_tab(u->tab);
    lv_obj_t *settings[] = {u->acq_band, u->profile, u->limit, u->repeat, u->channels};
    for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
        set_disabled(settings[i], busy);
    set_disabled(u->scan, busy || !u->session || !u->snapshot);
    set_disabled(u->stop, !busy);
    button_text(u->back, u->pending_back ? "Stopping..." : LV_SYMBOL_LEFT " Back");
}

static void back_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (wa_busy_tab(u->tab)) {
        u->pending_back = true;
        wa_stop(u->session);
        controls(u);
        lv_label_set_text(u->status_label, "Stopping acquisition before leaving...");
        return;
    }
    wa_leave(u->tab);
    if (u->on_back) u->on_back(u->tab);
}

static void scan_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (!u->session || !u->snapshot || wa_busy_tab(u->tab)) return;
    u->pending_back = false;
    if (!wa_start(u->session, &u->request))
        lv_label_set_text(u->status_label, "Cannot start acquisition. Check connection and radio ownership.");
    controls(u);
}

static void stop_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (u->session) wa_stop(u->session);
    lv_label_set_text(u->status_label, "Stopping acquisition...");
}

static void settings_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (wa_busy_tab(u->tab)) return;
    static const wa_band_t bands[] = {WA_BAND_24, WA_BAND_5, WA_BAND_ANY};
    u->request.band = bands[lv_dropdown_get_selected(u->acq_band)];
    u->request.profile = lv_dropdown_get_selected(u->profile);
    u->request.limit = lv_dropdown_get_selected(u->limit) ? 128 : 64;
    u->request.repeat_ms = repeats[lv_dropdown_get_selected(u->repeat)];
}

static void filter_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    u->filter.band = (wa_band_t)lv_dropdown_get_selected(u->filter_band);
    u->filter.primary = primary_channels[lv_dropdown_get_selected(u->primary)];
    u->filter.width = widths[lv_dropdown_get_selected(u->width)];
    u->filter.min_rssi = lv_slider_get_value(u->rssi);
    if (lv_dropdown_get_selected(u->auth))
        lv_dropdown_get_selected_str(u->auth, u->filter.auth, sizeof(u->filter.auth));
    else u->filter.auth[0] = 0;
    lv_label_set_text_fmt(u->rssi_label, "Minimum RSSI: %d dBm", u->filter.min_rssi);
    u->dirty = true;
}

static void reset_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    memset(&u->filter, 0, sizeof(u->filter));
    u->filter.width = -1;
    u->filter.min_rssi = -100;
    lv_dropdown_set_selected(u->filter_band, 0);
    lv_dropdown_set_selected(u->primary, 0);
    lv_dropdown_set_selected(u->width, 0);
    lv_dropdown_set_selected(u->auth, 0);
    lv_slider_set_value(u->rssi, -100, LV_ANIM_OFF);
    lv_label_set_text(u->rssi_label, "Minimum RSSI: -100 dBm");
    button_text(u->search, "Search SSID / BSSID");
    u->dirty = true;
}

static void panel_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (lv_obj_has_flag(u->panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(u->panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(u->panel, LV_ANIM_OFF);
    } else lv_obj_add_flag(u->panel, LV_OBJ_FLAG_HIDDEN);
}

static void editor_close(wa_ui_t *u, bool save)
{
    if (!u->editor) return;
    if (save) {
        const char *text = lv_textarea_get_text(u->textarea);
        if (u->editing_channels && !wa_busy_tab(u->tab)) {
            snprintf(u->request.channels, sizeof(u->request.channels), "%s", text);
            button_text(u->channels, text[0] ? "Edit channel plan" : "Driver channel plan");
        } else if (!u->editing_channels) {
            snprintf(u->filter.search, sizeof(u->filter.search), "%s", text);
            button_text(u->search, text[0] ? "Edit search" : "Search SSID / BSSID");
            u->dirty = true;
        }
    }
    lv_obj_delete(u->editor);
    u->editor = u->textarea = u->keyboard = NULL;
}

static void editor_done(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    editor_close(u, true);
}

static void editor_cancel(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    editor_close(u, false);
}

static void edit_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (u->editor) return;
    u->editing_channels = lv_event_get_target(e) == u->channels;
    if (u->editing_channels && wa_busy_tab(u->tab)) return;
    u->editor = lv_obj_create(u->root);
    lv_obj_add_flag(u->editor, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(u->editor, lv_pct(100), lv_pct(100));
    lv_obj_align(u->editor, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(u->editor, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(u->editor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(u->editor, 0, 0);
    lv_obj_set_style_pad_all(u->editor, 16, 0);
    lv_obj_set_flex_flow(u->editor, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(u->editor, 12, 0);
    label(u->editor, u->editing_channels ? "Requested channels (CSV; empty uses driver plan)" : "Find an SSID or BSSID", false);
    u->textarea = lv_textarea_create(u->editor);
    lv_obj_set_size(u->textarea, lv_pct(100), 56);
    lv_textarea_set_one_line(u->textarea, true);
    lv_textarea_set_max_length(u->textarea, u->editing_channels ? 159 : 96);
    if (u->editing_channels) lv_textarea_set_accepted_chars(u->textarea, "0123456789,");
    lv_textarea_set_text(u->textarea, u->editing_channels ? u->request.channels : u->filter.search);
    lv_obj_set_style_bg_color(u->textarea, subghz_host_ui_card(), 0);
    lv_obj_set_style_text_color(u->textarea, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(u->textarea, &lv_font_montserrat_18, 0);
    app_keyboard_style_cursor(u->textarea, subghz_host_color_cyan());
    lv_obj_t *actions = container(u->editor, LV_FLEX_FLOW_ROW);
    button(actions, "Apply", 120, editor_done, u);
    app_keyboard_navigation_register_escape(button(actions, "Cancel", 120, editor_cancel, u));
    u->keyboard = app_keyboard_create(u->editor);
    lv_obj_set_width(u->keyboard, lv_pct(100));
    lv_obj_set_flex_grow(u->keyboard, 1);
    app_keyboard_set_textarea(u->keyboard, u->textarea);
    lv_obj_add_event_cb(u->keyboard, editor_done, LV_EVENT_READY, u);
    lv_obj_add_event_cb(u->keyboard, editor_cancel, LV_EVENT_CANCEL, u);
}

static void chart_anim_exec(void *variable, int32_t value)
{
    wa_ui_t *u = variable;
    if (!u->chart || !lv_obj_is_visible(u->chart)) {
        u->reveal = 1024;
        lv_anim_delete(u, chart_anim_exec);
        return;
    }
    u->reveal = (int)value;
    lv_obj_invalidate(u->chart);
}

static void chart_reveal(wa_ui_t *u)
{
    lv_anim_delete(u, chart_anim_exec);
    u->reveal = 1024;
    if (!u->chart) return;
    if (!u->motion || !u->snapshot || !u->snapshot->valid || !lv_obj_is_visible(u->chart)) {
        lv_obj_invalidate(u->chart);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, u);
    lv_anim_set_exec_cb(&animation, chart_anim_exec);
    lv_anim_set_values(&animation, 0, 1024);
    lv_anim_set_duration(&animation, 480);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void chart_navigation(wa_ui_t *u)
{
    wa_segment_t base = wa_plot_range(u->snapshot, &u->filter,
                                     (wa_band_t)u->chart_band, u->full_band);
    wa_segment_t window = wa_plot_zoom(base, u->zoom, u->center_mhz);
    if (u->pan_left) set_disabled(u->pan_left, window.low_mhz <= base.low_mhz);
    if (u->pan_right) set_disabled(u->pan_right, window.high_mhz >= base.high_mhz);
    if (u->chart) lv_obj_invalidate(u->chart);
}

static void zoom_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    wa_segment_t base = wa_plot_range(u->snapshot, &u->filter,
                                     (wa_band_t)u->chart_band, u->full_band);
    wa_segment_t window = wa_plot_zoom(base, u->zoom, u->center_mhz);
    u->center_mhz = (window.low_mhz + window.high_mhz) / 2;
    u->zoom = 1U << lv_dropdown_get_selected(u->chart_zoom);
    chart_navigation(u);
}

static void pan_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    wa_segment_t base = wa_plot_range(u->snapshot, &u->filter,
                                     (wa_band_t)u->chart_band, u->full_band);
    wa_segment_t window = wa_plot_zoom(base, u->zoom, u->center_mhz);
    int direction = lv_event_get_target(e) == u->pan_left ? -1 : 1;
    int center = (window.low_mhz + window.high_mhz) / 2;
    center += direction * (window.high_mhz - window.low_mhz) / 2;
    window = wa_plot_zoom(base, u->zoom, center);
    u->center_mhz = (window.low_mhz + window.high_mhz) / 2;
    chart_navigation(u);
}

static void fit_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    u->zoom = 1; u->center_mhz = 0;
    lv_dropdown_set_selected(u->chart_zoom, 0);
    chart_navigation(u);
}

static void motion_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    u->motion = lv_dropdown_get_selected(u->motion_selector) == 0;
    chart_reveal(u);
    details_transition(u, u->details_open);
}

static void chart_band_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    u->chart_band = lv_dropdown_get_selected(u->chart_selector) ? WA_BAND_5 : WA_BAND_24;
    u->zoom = 1; u->center_mhz = 0;
    lv_dropdown_set_selected(u->chart_zoom, 0);
    u->dirty = true;
    chart_reveal(u);
}

static void chart_range_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    u->full_band = lv_dropdown_get_selected(u->chart_range) != 0;
    u->zoom = 1; u->center_mhz = 0;
    lv_dropdown_set_selected(u->chart_zoom, 0);
    chart_navigation(u);
}

static void sort_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    int sort = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (u->sort == sort) u->descending = !u->descending;
    else { u->sort = sort; u->descending = sort == 0; }
    u->dirty = true;
}

/* Animate a clipped wrapper, keeping its inner card and the AP list fixed.
 * No automatic outer scroll: selecting an AP must not move the plot away. */
static void details_anim_exec(void *variable, int32_t value)
{
    wa_ui_t *u = variable;
    if (!u->details) return;
    lv_obj_set_height(u->details, value);
    if (!value && !u->details_open) lv_obj_add_flag(u->details, LV_OBJ_FLAG_HIDDEN);
}

static void details_transition(wa_ui_t *u, bool open)
{
    if (!u->details) return;
    lv_anim_delete(u, details_anim_exec);
    int start = lv_obj_has_flag(u->details, LV_OBJ_FLAG_HIDDEN) ? 0 : lv_obj_get_height(u->details);
    u->details_open = open;
    int end = open ? 248 : 0;
    if (open) lv_obj_remove_flag(u->details, LV_OBJ_FLAG_HIDDEN);
    if (!u->motion || start == end || lv_obj_has_flag(u->root, LV_OBJ_FLAG_HIDDEN)) {
        details_anim_exec(u, end);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, u);
    lv_anim_set_exec_cb(&animation, details_anim_exec);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void highlight_selection(wa_ui_t *u)
{
    for (unsigned i = 0; i < u->visible_count; ++i) {
        const wa_ap_t *ap = &u->snapshot->aps[u->indices[i]];
        lv_obj_t *row = lv_obj_get_child(u->rows, i);
        bool selected = !strcmp(ap->bssid, u->selected);
        lv_obj_set_style_border_color(row, selected ? lv_color_hex(wa_ap_color_rgb(ap)) : subghz_host_ui_border(), 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
    }
    lv_obj_invalidate(u->chart);
}

static void close_details(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    details_transition(u, false);
}

static void show_details_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (!u->selected[0]) return;
    details_transition(u, true);
    /* Explicit navigation may scroll the page, after resolving final layout. */
    lv_anim_delete(u, details_anim_exec);
    details_anim_exec(u, 248);
    lv_obj_update_layout(u->details);
    lv_obj_scroll_to_view(u->details, u->motion ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void select_ap(wa_ui_t *u, unsigned index, bool from_chart)
{
    if (!u->snapshot || !u->snapshot->valid || index >= u->snapshot->count) return;
    const wa_ap_t *ap = &u->snapshot->aps[index];
    bool changed = strcmp(u->selected, ap->bssid) != 0;
    snprintf(u->selected, sizeof(u->selected), "%s", ap->bssid);
    if (!from_chart) {
        u->chart_band = ap->band;
        u->center_mhz = wa_frequency(ap->primary);
        lv_dropdown_set_selected(u->chart_selector, ap->band == WA_BAND_5);
        lv_obj_t *range_field = lv_obj_get_parent(u->chart_range);
        if (ap->band == WA_BAND_5) lv_obj_remove_flag(range_field, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(range_field, LV_OBJ_FLAG_HIDDEN);
        update_channel_counts(u);
    }
    highlight_selection(u);
    update_details(u);
    if (changed) lv_obj_scroll_to_y(u->detail_scroll, 0, LV_ANIM_OFF);
    details_transition(u, true);
    chart_navigation(u);
    update_advice(u);
}

static void update_advice(wa_ui_t *u)
{
    if (!u->advice_panel || lv_obj_has_flag(u->advice_panel, LV_OBJ_FLAG_HIDDEN)) return;
    if (!u->snapshot || !u->snapshot->valid) {
        lv_label_set_text(u->advice_text, "Run a full-band scan first, then choose Propose channel.");
        return;
    }
    if (u->status.stale || u->status.age_ms > 300000) {
        lv_label_set_text(u->advice_text, "Snapshot is stale or older than 5 minutes. Run a fresh full-band scan before choosing a channel.");
        return;
    }
    const char *exclude = NULL;
    if (lv_dropdown_get_selected(u->advice_own)) {
        for (unsigned i = 0; i < u->snapshot->count; ++i)
            if (!strcmp(u->snapshot->aps[i].bssid, u->selected) &&
                u->snapshot->aps[i].band == u->chart_band) exclude = u->selected;
        if (!exclude) {
            lv_label_set_text(u->advice_text, "Select your own AP in the current chart band, or choose Include all APs.");
            return;
        }
    }
    wa_channel_advice_t advice;
    wa_propose_channels(u->snapshot, (wa_band_t)u->chart_band,
                        lv_dropdown_get_selected(u->advice_pool) != 0, exclude, &advice);
    if (advice.state != WA_ADVICE_OK) {
        lv_label_set_text(u->advice_text, advice.state == WA_ADVICE_TRUNCATED ?
            "Snapshot is truncated. Scan the full band with 128 records; a complete snapshot is required." :
            "Insufficient coverage to compare this band. This advisor needs channels 1-13 for 2.4 GHz or the full 5 GHz plan through 177. "
            "If you set Channels CSV, clear it and scan this band again. If country/driver limits exclude channels, "
            "advice is unavailable with this coverage; repeating the same scan will not resolve it.");
        return;
    }
    unsigned ties = 0;
    while (ties < advice.count && advice.choices[ties].score == advice.choices[0].score) ++ties;
    char text[1600];
    int n = snprintf(text, sizeof(text), "%s GHz | 20 MHz | %u APs considered | top 3 of %u candidates\n%s\n",
        u->chart_band == WA_BAND_24 ? "2.4" : "5", advice.observed, advice.count,
        !advice.observed ? "No APs observed: no channel preference can be inferred." :
        ties > 1 ? "Several channels tie for the lowest observed overlap." : "Suggested starting channel: first in the ranking below.");
    for (unsigned i = 0; i < advice.count && i < 3 && n > 0 && (size_t)n < sizeof(text); ++i) {
        const wa_channel_choice_t *c = &advice.choices[i];
        char strongest[24] = "none";
        if (c->overlapping) snprintf(strongest, sizeof(strongest), "%d dBm", c->strongest_rssi);
        n += snprintf(text + n, sizeof(text) - (size_t)n,
            "%u. Ch %d%s | score %lu | %u known overlaps | strongest %s\n",
            i + 1, c->channel, c->dfs ? " (DFS)" : "", (unsigned long)c->score, c->overlapping, strongest);
    }
    if (n > 0 && (size_t)n < sizeof(text))
        snprintf(text + n, sizeof(text) - (size_t)n,
            "Best-score ties: %u | Unknown-width APs: %u%s\n%s%s\n"
            "Lower score = less observed overlap, not airtime or a speed guarantee. "
            "Full snapshot used; display filters ignored. Unknown widths add equal uncertainty to every candidate. "
            "Scan coverage may be driver-filtered. Use only channels offered by your router/country; DFS may require radar checks.",
            ties, advice.unknown, advice.unknown ? " (provisional ranking)" : "",
            exclude ? "Excluded own BSSID: " : "Own AP: included with other observed APs.", exclude ? exclude : "");
    lv_label_set_text(u->advice_text, text);
}

static void propose_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    lv_obj_remove_flag(u->advice_panel, LV_OBJ_FLAG_HIDDEN);
    update_advice(u);
    lv_obj_update_layout(u->advice_panel);
    lv_obj_scroll_to_view(u->advice_panel, u->motion ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void advice_close_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    lv_obj_add_flag(u->advice_panel, LV_OBJ_FLAG_HIDDEN);
}

static void advice_options_event(lv_event_t *e)
{
    update_advice(lv_event_get_user_data(e));
}

static void row_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    unsigned index = (unsigned)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    select_ap(u, index, false);
}

static int compare(const wa_ui_t *u, unsigned a, unsigned b)
{
    const wa_ap_t *x = &u->snapshot->aps[a], *y = &u->snapshot->aps[b];
    int v = u->sort == 0 ? x->rssi - y->rssi :
            u->sort == 1 ? wa_frequency(x->primary) - wa_frequency(y->primary) :
            strcmp(x->ssid_display, y->ssid_display);
    if (!v) v = strcmp(x->bssid, y->bssid);
    return u->descending ? -v : v;
}

static int plot_x(int mhz, const lv_area_t *a, const wa_segment_t *range)
{
    return a->x1 + (mhz - range->low_mhz) * (a->x2 - a->x1) /
                   (range->high_mhz - range->low_mhz);
}

static int plot_y(int dbm, const lv_area_t *a)
{
    if (dbm < -100) dbm = -100;
    if (dbm > -20) dbm = -20;
    return a->y2 - (dbm + 100) * (a->y2 - a->y1) / 80;
}

static bool chart_area(wa_ui_t *u, lv_area_t *a)
{
    lv_obj_get_content_coords(u->chart, a);
    a->x1 += 44; a->x2 -= 14; a->y1 += 20; a->y2 -= 42;
    return a->x2 > a->x1 && a->y2 > a->y1;
}

static int chart_hit_distance(const wa_ap_t *ap, const lv_area_t *a,
                              const wa_segment_t *range, const lv_point_t *point, int reveal)
{
    int y = plot_y(ap->rssi, a);
    y = a->y2 - (a->y2 - y) * reveal / 1024;
    if (point->y < y - 14 || point->y > a->y2) return INT_MAX;
    wa_segment_t segments[2];
    size_t count = wa_ap_segments(ap, segments);
    if (!count) {
        int x = plot_x(wa_frequency(ap->primary), a, range);
        if (x < a->x1 || x > a->x2 || abs(point->x - x) > 12) return INT_MAX;
        return abs(point->x - x) + (abs(point->y - y) < 14 ? abs(point->y - y) : 14);
    }
    int best = INT_MAX;
    for (size_t i = 0; i < count; ++i) {
        int left = plot_x(segments[i].low_mhz, a, range);
        int right = plot_x(segments[i].high_mhz, a, range);
        if (left < a->x1) left = a->x1;
        if (right > a->x2) right = a->x2;
        if (right < left) continue;
        int dx = point->x < left ? left - point->x : point->x > right ? point->x - right : 0;
        if (dx > 12) continue;
        int distance = abs(point->y - y) + dx * 3;
        if (distance < best) best = distance;
    }
    return best;
}

static void chart_tap(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    lv_indev_t *input = lv_indev_active();
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return;
    if (!u->snapshot || !u->snapshot->valid || !u->reveal) return;
    if (u->dirty) refresh(u);
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t a;
    if (!chart_area(u, &a) || point.x < a.x1 || point.x > a.x2 ||
        point.y < a.y1 || point.y > a.y2) return;
    wa_segment_t range = wa_plot_range(u->snapshot, &u->filter,
                                       (wa_band_t)u->chart_band, u->full_band);
    range = wa_plot_zoom(range, u->zoom, u->center_mhz);
    int distances[WA_MAX_APS], best = INT_MAX, current = -1;
    for (unsigned i = 0; i < u->visible_count; ++i) {
        const wa_ap_t *ap = &u->snapshot->aps[u->indices[i]];
        distances[i] = INT_MAX;
        if (ap->band != u->chart_band) continue;
        distances[i] = chart_hit_distance(ap, &a, &range, &point, u->reveal);
        if (distances[i] < best) best = distances[i];
        if (!strcmp(ap->bssid, u->selected)) current = (int)i;
    }
    if (best == INT_MAX) return;
    /* Nearly coincident traces cycle in the displayed list order. A new tap
     * elsewhere picks the nearest trace instead of favoring an old selection. */
    if (current >= 0 && distances[current] > best + 4) current = -1;
    for (unsigned step = 1; step <= u->visible_count; ++step) {
        unsigned i = (unsigned)(current + (int)step) % u->visible_count;
        if (distances[i] > best + 4) continue;
        select_ap(u, u->indices[i], true);
        /* Non-recursive: reveal the row inside the list, keep chart in view. */
        lv_obj_scroll_to_view(lv_obj_get_child(u->rows, i), u->motion ? LV_ANIM_ON : LV_ANIM_OFF);
        return;
    }
}

static void draw_text(lv_layer_t *layer, int x, int y, int w, const char *text, lv_color_t color)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = text;
    d.text_local = 1; /* Axis strings are stack-owned, draw tasks run later. */
    d.font = &lv_font_montserrat_14;
    d.color = color;
    lv_area_t a = {x, y, x + w, y + 20};
    lv_draw_label(layer, &d, &a);
}

static void draw_line(lv_layer_t *layer, int x1, int y1, int x2, int y2,
                      lv_color_t color, int width, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
    d.color = color; d.width = width; d.opa = opa;
    lv_draw_line(layer, &d);
}

static void chart_draw(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    if (!chart_area(u, &a)) return;
    wa_segment_t range = wa_plot_range(u->snapshot, &u->filter,
                                       (wa_band_t)u->chart_band, u->full_band);
    range = wa_plot_zoom(range, u->zoom, u->center_mhz);
    lv_color_t grid = subghz_host_ui_border(), muted = subghz_host_ui_muted();
    for (int dbm = -100; dbm <= -20; dbm += 20) {
        int y = plot_y(dbm, &a);
        char text[12]; snprintf(text, sizeof(text), "%d", dbm);
        draw_text(layer, a.x1 - 42, y - 8, 40, text, muted);
        draw_line(layer, a.x1, y, a.x2, y, grid, 1, LV_OPA_70);
    }
    draw_text(layer, a.x1 - 42, a.y1 - 20, 50, "dBm", muted);
    static const int ticks24[] = {1, 3, 5, 7, 9, 11, 13, 14};
    static const int ticks24_zoom[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    static const int ticks5[] = {36, 40, 44, 48, 52, 56, 60, 64,
        100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
        149, 153, 157, 161, 165, 169, 173, 177};
    const int *ticks = u->chart_band == WA_BAND_24 ? ticks24 : ticks5;
    unsigned count = u->chart_band == WA_BAND_24 ? sizeof(ticks24)/sizeof(ticks24[0]) : sizeof(ticks5)/sizeof(ticks5[0]);
    if (u->chart_band == WA_BAND_24 && u->zoom > 1) {
        ticks = ticks24_zoom; count = sizeof(ticks24_zoom)/sizeof(ticks24_zoom[0]);
    }
    int last_tick = a.x1 - 48;
    for (unsigned i = 0; i < count; ++i) {
        int mhz = wa_frequency(ticks[i]);
        if (mhz < range.low_mhz || mhz > range.high_mhz) continue;
        int x = plot_x(mhz, &a, &range);
        if ((u->chart_band == WA_BAND_5 || u->zoom > 1) && x - last_tick < 48) continue;
        last_tick = x;
        char text[8]; snprintf(text, sizeof(text), "%d", ticks[i]);
        draw_line(layer, x, a.y1, x, a.y2, grid, 1, LV_OPA_40);
        draw_text(layer, x - 12, a.y2 + 6, 34, text, muted);
    }
    char axis[96];
    snprintf(axis, sizeof(axis), "Channel / frequency (%d-%d MHz)%s | %ux",
             range.low_mhz, range.high_mhz,
             u->chart_band == WA_BAND_5 ? (u->full_band ? " | Full band" : " | Auto") : "", u->zoom);
    draw_text(layer, a.x1, a.y2 + 25, a.x2 - a.x1, axis, muted);
    if (!u->snapshot || !u->snapshot->valid || !u->reveal) return;
    /* Draw selected last so a dense scan cannot obscure its outline. */
    for (int pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < u->visible_count; ++i) {
            const wa_ap_t *ap = &u->snapshot->aps[u->indices[i]];
            bool selected = !strcmp(ap->bssid, u->selected);
            if (ap->band != u->chart_band || selected != (pass == 1)) continue;
            lv_color_t color = lv_color_hex(wa_ap_color_rgb(ap));
            int y = plot_y(ap->rssi, &a), x = plot_x(wa_frequency(ap->primary), &a, &range);
            y = a.y2 - (a.y2 - y) * u->reveal / 1024;
            bool primary_visible = x >= a.x1 && x <= a.x2;
            /* Full geometry is mandatory. Unknown width/centers stay markers. */
            wa_segment_t segments[2];
            size_t segment_count = wa_ap_segments(ap, segments);
            if (!segment_count) {
                if (!primary_visible) continue;
                draw_line(layer, x, y, x, a.y2, color, selected ? 4 : 2, LV_OPA_COVER);
                draw_line(layer, x - 4 < a.x1 ? a.x1 : x - 4, y,
                          x + 4 > a.x2 ? a.x2 : x + 4, y, color, 2, LV_OPA_COVER);
                continue;
            }
            for (size_t s = 0; s < segment_count; ++s) {
                lv_area_t footprint = {plot_x(segments[s].low_mhz, &a, &range), y,
                    plot_x(segments[s].high_mhz, &a, &range), a.y2};
                if (footprint.x1 < a.x1) footprint.x1 = a.x1;
                if (footprint.x2 > a.x2) footprint.x2 = a.x2;
                if (footprint.x2 < footprint.x1) continue;
                lv_draw_rect_dsc_t d;
                lv_draw_rect_dsc_init(&d);
                d.radius = 5;
                d.bg_color = color; d.bg_opa = selected ? LV_OPA_30 : LV_OPA_10;
                d.border_color = color; d.border_width = selected ? 3 : 1;
                d.border_opa = selected ? LV_OPA_COVER : LV_OPA_70;
                lv_draw_rect(layer, &d, &footprint);
            }
            if (primary_visible)
                draw_line(layer, x, y - 3 < a.y1 ? a.y1 : y - 3, x,
                          y + 5 > a.y2 ? a.y2 : y + 5, color, selected ? 3 : 2, LV_OPA_COVER);
        }
    }
}

static void update_details(wa_ui_t *u)
{
    if (!u->snapshot || !u->selected[0]) return;
    const wa_ap_t *ap = NULL;
    for (unsigned i = 0; i < u->snapshot->count; ++i)
        if (!strcmp(u->snapshot->aps[i].bssid, u->selected)) { ap = &u->snapshot->aps[i]; break; }
    if (!ap) {
        lv_label_set_text(u->detail_text, "Selected AP is absent from the latest snapshot.");
        button_text(u->selection, "Details below: selected AP absent from latest scan");
        return;
    }
    char selected_text[200];
    snprintf(selected_text, sizeof(selected_text), "Details below: %s | %s | %d dBm",
             ap->ssid_len ? ap->ssid_display : "Hidden SSID", ap->bssid, ap->rssi);
    button_text(u->selection, selected_text);
    set_disabled(u->selection, false);
    char width[24], center1[16], center2[16], phy[64] = "", hex[65];
    if (!ap->width) snprintf(width, sizeof(width), "Unknown");
    else if (ap->width == 8080) snprintf(width, sizeof(width), "80+80 MHz");
    else snprintf(width, sizeof(width), "%d MHz", ap->width);
    if (ap->center1_mhz) snprintf(center1, sizeof(center1), "%d MHz", ap->center1_mhz);
    else snprintf(center1, sizeof(center1), "Unknown");
    if (ap->center2_mhz) snprintf(center2, sizeof(center2), "%d MHz", ap->center2_mhz);
    else snprintf(center2, sizeof(center2), "Unknown");
    static const char *flags[] = {"11a", "11b", "11g", "11n", "11ac", "11ax", "LR"};
    for (unsigned i = 0; i < 7; ++i) if (ap->phy_bits & (1U << i)) {
        if (phy[0]) strncat(phy, " / ", sizeof(phy) - strlen(phy) - 1);
        strncat(phy, flags[i], sizeof(phy) - strlen(phy) - 1);
    }
    for (unsigned i = 0; i < ap->ssid_len; ++i) snprintf(hex + 2 * i, 3, "%02X", ap->ssid[i]);
    hex[2 * ap->ssid_len] = 0;
    lv_label_set_text_fmt(u->detail_text,
        "%s\n%s   |   %d dBm   |   %s GHz / channel %d (%d MHz)\n"
        "Security: %s   |   PHY: %s\nWidth: %s   |   Centers: %s / %s   |   Secondary: %s\n"
        "SSID bytes: %s\nWidth metadata: SDK derived, unverified on target.",
        ap->ssid_len ? ap->ssid_display : "Hidden SSID", ap->bssid, ap->rssi,
        ap->band == WA_BAND_24 ? "2.4" : "5", ap->primary, wa_frequency(ap->primary),
        ap->auth, phy[0] ? phy : "Unknown", width, center1, center2,
        ap->secondary == -1 ? "below" : ap->secondary == 1 ? "above" : ap->secondary == 0 ? "none" : "unknown",
        hex[0] ? hex : "(empty)");
}

static void update_channel_counts(wa_ui_t *u)
{
    /* Counts belong to primary channels and the selected plot band, not airtime. */
    char counts[640];
    int used = snprintf(counts, sizeof(counts), "Observed APs by primary channel: ");
    bool any = false;
    for (unsigned p = 1; p < sizeof(primary_channels)/sizeof(primary_channels[0]); ++p) {
        int count = 0, primary = primary_channels[p];
        for (unsigned i = 0; i < u->visible_count; ++i) {
            const wa_ap_t *ap = &u->snapshot->aps[u->indices[i]];
            if (ap->primary == primary && ap->band == u->chart_band) ++count;
        }
        if (count && used > 0 && (size_t)used < sizeof(counts)) {
            used += snprintf(counts + used, sizeof(counts) - used, "%sch %d: %d", any ? "   |   " : "", primary, count);
            any = true;
        }
    }
    if (!any && used > 0 && (size_t)used < sizeof(counts)) snprintf(counts + used, sizeof(counts) - used, "none in this view");
    lv_label_set_text(u->channel_counts, counts);
    if (!u->snapshot || !u->snapshot->valid)
        lv_label_set_text(u->channel_counts, "Observed APs by primary channel: awaiting a completed scan");
}

static void refresh(wa_ui_t *u)
{
    u->dirty = false;
    if (u->chart_range) {
        lv_obj_t *range_field = lv_obj_get_parent(u->chart_range);
        if (u->chart_band == WA_BAND_5) lv_obj_remove_flag(range_field, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(range_field, LV_OBJ_FLAG_HIDDEN);
    }
    int32_t previous_scroll = lv_obj_get_scroll_y(u->rows);
    const char *sort_names[] = {"RSSI", "Channel", "Name"};
    for (int i = 0; i < 3; ++i) {
        char text[32];
        snprintf(text, sizeof(text), "%s%s", sort_names[i], u->sort == i ?
            (u->descending ? " " LV_SYMBOL_DOWN : " " LV_SYMBOL_UP) : "");
        button_text(u->sort_buttons[i], text);
    }
    u->visible_count = 0;
    if (u->snapshot && u->snapshot->valid) {
        for (unsigned i = 0; i < u->snapshot->count; ++i)
            if (wa_ap_matches(&u->snapshot->aps[i], &u->filter)) u->indices[u->visible_count++] = i;
        for (unsigned i = 1; i < u->visible_count; ++i) {
            uint16_t value = u->indices[i];
            unsigned j = i;
            while (j && compare(u, value, u->indices[j - 1]) < 0) {
                u->indices[j] = u->indices[j - 1]; --j;
            }
            u->indices[j] = value;
        }
    }
    lv_obj_clean(u->rows);
    if (!u->snapshot || !u->snapshot->valid) {
        label(u->rows, "No completed snapshot yet. Choose Scan to observe nearby APs.", true);
        lv_label_set_text(u->summary, "Observed APs: --");
        lv_label_set_text(u->scope, "Requested channel scope appears after a completed scan.");
    } else {
        const wa_snapshot_t *s = u->snapshot;
        lv_label_set_text_fmt(u->summary, "Observed APs: %u filtered / %u returned / %u found%s",
            u->visible_count, s->count, s->found, s->truncated ? "  |  TRUNCATED" : "");
        char scope[320];
        int n = snprintf(scope, sizeof(scope), "Requested channels (driver may filter): ");
        for (unsigned i = 0; i < s->channel_count && n > 0 && (size_t)n < sizeof(scope); ++i)
            n += snprintf(scope + n, sizeof(scope) - n, "%s%u", i ? "," : "", s->channels[i]);
        lv_label_set_text(u->scope, scope);
        if (!u->visible_count) label(u->rows, s->count ? "No APs match these filters. Reset filters to see the snapshot." : "Scan complete: no APs returned.", true);
        for (unsigned i = 0; i < u->visible_count; ++i) {
            const wa_ap_t *ap = &s->aps[u->indices[i]];
            lv_obj_t *row = button(u->rows, "", lv_pct(100), row_event, u);
            /* LVGL buttons otherwise scroll all ancestors on pointer focus.
             * Keyboard list navigation already reveals rows non-recursively. */
            lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_set_height(row, 84);
            lv_obj_set_user_data(row, (void *)(uintptr_t)u->indices[i]);
            lv_obj_set_style_pad_all(row, 10, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row, 12, 0);
            lv_obj_clean(row);
            bool selected = !strcmp(ap->bssid, u->selected);
            lv_obj_set_style_border_color(row, selected ? lv_color_hex(wa_ap_color_rgb(ap)) : subghz_host_ui_border(), 0);
            lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
            lv_obj_t *dot = lv_obj_create(row);
            lv_obj_remove_style_all(dot);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(wa_ap_color_rgb(ap)), 0);
            lv_obj_t *name = container(row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_width(name, 0);
            lv_obj_set_flex_grow(name, 1);
            lv_obj_set_style_pad_gap(name, 4, 0);
            lv_obj_set_flex_align(name, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t *ssid = label(name, ap->ssid_len ? ap->ssid_display : "Hidden SSID", false);
            lv_obj_set_width(ssid, lv_pct(100));
            lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(ssid, &lv_font_montserrat_16, 0);
            label(name, ap->bssid, true);
            char channel[90], signal[20], width[24];
            if (!ap->width) snprintf(width, sizeof(width), "Width unknown");
            else if (ap->width == 8080) snprintf(width, sizeof(width), "80+80 MHz");
            else snprintf(width, sizeof(width), "%d MHz", ap->width);
            snprintf(channel, sizeof(channel), "%s GHz  ch %d  |  %s\n%s", ap->band == WA_BAND_24 ? "2.4" : "5", ap->primary, width, ap->auth);
            lv_obj_t *meta = label(row, channel, true);
            lv_obj_set_width(meta, 220);
            lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
            snprintf(signal, sizeof(signal), "%d dBm", ap->rssi);
            lv_obj_t *rssi = label(row, signal, false);
            lv_obj_set_width(rssi, 76);
        }
    }
    update_channel_counts(u);
    update_details(u);
    lv_obj_update_layout(u->rows);
    lv_obj_scroll_to_y(u->rows, previous_scroll, LV_ANIM_OFF);
    chart_navigation(u);
    update_advice(u);
}

static void tick(lv_timer_t *timer)
{
    wa_ui_t *u = lv_timer_get_user_data(timer);
    if (!u->root) return;
    if (u->pending_back && !wa_busy_tab(u->tab)) {
        u->pending_back = false;
        wa_leave(u->tab);
        if (u->on_back) u->on_back(u->tab);
        return; /* Host callback may delete this page and timer. */
    }
    if (lv_obj_has_flag(u->root, LV_OBJ_FLAG_HIDDEN)) return;
    bool old_stale = u->status.stale;
    bool old_expired = u->status.age_ms > 300000;
    bool old_valid = u->snapshot && u->snapshot->valid;
    uint32_t old_scan = old_valid ? u->snapshot->scan : 0;
    char old_boot[17] = {0};
    if (old_valid) memcpy(old_boot, u->snapshot->boot, sizeof(old_boot));
    if (u->session && u->snapshot && wa_copy_view(u->session, u->snapshot, &u->status)) {
        if (u->snapshot->valid && (!old_valid || old_scan != u->snapshot->scan ||
                                  strcmp(old_boot, u->snapshot->boot))) chart_reveal(u);
        if (u->rendered_revision != u->status.revision) {
            u->rendered_revision = u->status.revision;
            u->dirty = true;
        }
        char text[280];
        if (u->snapshot->valid)
            snprintf(text, sizeof(text), "%s%s%s  |  Last snapshot %llu s ago%s", u->status.running ? "Running: " : "",
                u->status.message, u->status.stale ? "  |  STALE" : "",
                (unsigned long long)(u->status.age_ms / 1000), u->status.needs_recovery ? "  |  Device reboot required" : "");
        else snprintf(text, sizeof(text), "%s%s%s", u->status.running ? "Running: " : "",
            u->status.message[0] ? u->status.message : "Ready. Scan probes WFA/1 support and acquires a snapshot.",
            u->status.needs_recovery ? "  |  Device reboot required" : "");
        if (!u->pending_back && strcmp(lv_label_get_text(u->status_label), text)) lv_label_set_text(u->status_label, text);
        lv_obj_set_style_text_color(u->status_label,
            u->status.needs_recovery || u->status.stale ? subghz_host_color_orange() : subghz_host_ui_muted(), 0);
    }
    controls(u);
    if (u->dirty) refresh(u);
    else if (old_stale != u->status.stale || old_expired != (u->status.age_ms > 300000)) update_advice(u);
}

static void delete_event(lv_event_t *e)
{
    wa_ui_t *u = lv_event_get_user_data(e);
    if (lv_event_get_target(e) != u->root) return;
    lv_anim_delete(u, chart_anim_exec);
    u->reveal = 1024;
    wa_leave(u->tab);
    lv_anim_delete(u, details_anim_exec);
    if (u->timer) lv_timer_delete(u->timer);
    if (u->snapshot) heap_caps_free(u->snapshot);
    /* Preserve preferences and BSSID selection, never free the worker/session. */
    u->root = u->body = u->back = u->scan = u->stop = u->panel = u->chart = u->rows = NULL;
    u->status_label = u->summary = u->scope = u->channel_counts = u->empty = NULL;
    u->details = u->detail_text = u->detail_scroll = u->selection = NULL;
    u->editor = u->textarea = u->keyboard = NULL;
    u->details_open = false;
    u->acq_band = u->profile = u->limit = u->repeat = u->channels = NULL;
    u->filter_band = u->primary = u->width = u->auth = u->search = u->rssi = u->rssi_label = NULL;
    u->chart_selector = u->chart_range = NULL;
    u->chart_zoom = u->pan_left = u->pan_right = u->motion_selector = NULL;
    u->advice_panel = u->advice_text = u->advice_pool = u->advice_own = NULL;
    u->timer = NULL;
    u->snapshot = NULL;
    memset(u->sort_buttons, 0, sizeof(u->sort_buttons));
    u->pending_back = false;
}

static void make_panel(wa_ui_t *u)
{
    u->panel = container(u->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(u->panel, subghz_host_ui_panel(), 0);
    lv_obj_set_style_bg_opa(u->panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(u->panel, 12, 0);
    lv_obj_set_style_pad_all(u->panel, 14, 0);
    lv_obj_set_flex_align(u->panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    label(u->panel, "Acquisition settings - used by the next Scan", false);
    lv_obj_t *acquisition = container(u->panel, LV_FLEX_FLOW_ROW_WRAP);
    u->acq_band = dropdown(acquisition, "Scan band", "2.4 GHz\n5 GHz\nBoth bands", u->request.band == WA_BAND_24 ? 0 : u->request.band == WA_BAND_5 ? 1 : 2, settings_event, u);
    u->profile = dropdown(acquisition, "Profile", "Quick\nDetailed\nPassive", u->request.profile, settings_event, u);
    u->limit = dropdown(acquisition, "AP record limit", "64 records\n128 records", u->request.limit == 128, settings_event, u);
    unsigned repeat = 0;
    for (unsigned i = 0; i < 4; ++i) if (repeats[i] == u->request.repeat_ms) repeat = i;
    u->repeat = dropdown(acquisition, "Repeat after completion", "Off\n2 seconds\n5 seconds\n10 seconds", repeat, settings_event, u);
    u->channels = button(field(acquisition, "Channels CSV"), u->request.channels[0] ? "Edit channel plan" : "Driver channel plan", 190, edit_event, u);
    label(u->panel, "Display filters - apply to the current snapshot", false);
    lv_obj_t *filters = container(u->panel, LV_FLEX_FLOW_ROW_WRAP);
    u->filter_band = dropdown(filters, "Band", "All bands\n2.4 GHz\n5 GHz", u->filter.band, filter_event, u);
    char options[200] = "All channels";
    unsigned primary = 0;
    for (unsigned i = 1; i < sizeof(primary_channels)/sizeof(primary_channels[0]); ++i) {
        char option[8]; snprintf(option, sizeof(option), "\n%d", primary_channels[i]);
        strncat(options, option, sizeof(options) - strlen(options) - 1);
        if (primary_channels[i] == u->filter.primary) primary = i;
    }
    u->primary = dropdown(filters, "Primary channel", options, primary, filter_event, u);
    unsigned width = 0;
    for (unsigned i = 0; i < 7; ++i) if (widths[i] == u->filter.width) width = i;
    u->width = dropdown(filters, "Channel width", "All widths\nUnknown\n20 MHz\n40 MHz\n80 MHz\n160 MHz\n80+80 MHz", width, filter_event, u);
    u->auth = dropdown(filters, "Security", auth_options, 0, filter_event, u);
    /* Restore an exact auth label, including future canonical labels if listed. */
    char auth[33];
    for (unsigned i = 1; i < lv_dropdown_get_option_count(u->auth); ++i) {
        lv_dropdown_set_selected(u->auth, i);
        lv_dropdown_get_selected_str(u->auth, auth, sizeof(auth));
        if (!strcmp(auth, u->filter.auth)) break;
        lv_dropdown_set_selected(u->auth, 0);
    }
    u->search = button(field(filters, "SSID / BSSID"), u->filter.search[0] ? "Edit search" : "Search SSID / BSSID", 190, edit_event, u);
    lv_obj_t *rssi_box = container(u->panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(rssi_box, lv_pct(100));
    lv_obj_set_flex_align(rssi_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    u->rssi_label = label(rssi_box, "", true);
    lv_label_set_text_fmt(u->rssi_label, "Minimum RSSI: %d dBm", u->filter.min_rssi);
    u->rssi = lv_slider_create(rssi_box);
    lv_obj_set_size(u->rssi, lv_pct(94), 20);
    lv_obj_set_style_margin_all(u->rssi, 14, 0);
    lv_obj_set_ext_click_area(u->rssi, 14);
    lv_slider_set_range(u->rssi, -100, -20);
    lv_slider_set_value(u->rssi, u->filter.min_rssi, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(u->rssi, subghz_host_color_cyan(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(u->rssi, subghz_host_color_cyan(), LV_PART_KNOB);
    lv_obj_add_event_cb(u->rssi, filter_event, LV_EVENT_VALUE_CHANGED, u);
    button(u->panel, "Reset filters", 160, reset_event, u);
    u->motion_selector = dropdown(u->panel, "Chart animation", "Grow\nOff", !u->motion, motion_event, u);
    lv_obj_add_flag(u->panel, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *wa_screen_show(lv_obj_t *parent, int tab, void (*on_back)(int tab))
{
    if (!parent || tab < 0 || tab >= 3) return NULL;
    wa_ui_t *u = &views[tab];
    if (!u->initialized) {
        u->initialized = true; u->tab = tab; u->chart_band = WA_BAND_24;
        u->zoom = 1; u->reveal = 1024; u->motion = true;
        u->request.band = WA_BAND_ANY; u->request.limit = 64;
        u->filter.min_rssi = -100; u->filter.width = -1; u->descending = true;
    }
    u->on_back = on_back;
    u->pending_back = false;
    if (!u->session) u->session = wa_session_get(tab);
    if (!u->snapshot)
        u->snapshot = heap_caps_calloc(1, sizeof(*u->snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (u->root) {
        lv_obj_remove_flag(u->root, LV_OBJ_FLAG_HIDDEN);
        u->dirty = true;
        lv_timer_ready(u->timer);
        chart_reveal(u);
        return u->root;
    }
    u->root = lv_obj_create(parent);
    lv_obj_set_size(u->root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(u->root, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(u->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(u->root, 0, 0);
    lv_obj_set_style_pad_all(u->root, 16, 0);
    lv_obj_set_style_pad_gap(u->root, 12, 0);
    lv_obj_set_style_text_font(u->root, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_flow(u->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(u->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(u->root, delete_event, LV_EVENT_DELETE, u);
    lv_obj_t *header = container(u->root, LV_FLEX_FLOW_ROW_WRAP);
    u->back = button(header, LV_SYMBOL_LEFT " Back", 112, back_event, u);
    app_keyboard_navigation_register_escape(u->back);
    lv_obj_t *title = label(header, "Wi-Fi Analyzer", false);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(title, 1);
    u->scan = button(header, "Scan", 84, scan_event, u);
    lv_obj_set_style_border_color(u->scan, subghz_host_color_cyan(), 0);
    u->stop = button(header, "Stop", 84, stop_event, u);
    button(header, "Filters / Setup", 140, panel_event, u);
    u->status_label = label(u->root, "Ready. Scan to discover nearby access points.", true);
    lv_obj_set_width(u->status_label, lv_pct(100));
    /* Keep Back/Stop reachable while the chart, filters and list scroll. */
    u->body = container(u->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(u->body, 0);
    lv_obj_set_flex_grow(u->body, 1);
    lv_obj_set_flex_align(u->body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(u->body, 12, 0);
    lv_obj_add_flag(u->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(u->body, LV_DIR_VER);
    make_panel(u);
    u->summary = label(u->body, "Observed APs: --", false);
    lv_obj_set_width(u->summary, lv_pct(100));
    lv_obj_set_style_text_font(u->summary, &lv_font_montserrat_16, 0);
    lv_obj_t *chart_header = container(u->body, LV_FLEX_FLOW_ROW_WRAP);
    u->chart_selector = dropdown(chart_header, "Channel / RSSI view", "2.4 GHz\n5 GHz", u->chart_band == WA_BAND_5, chart_band_event, u);
    u->chart_range = dropdown(chart_header, "5 GHz range", "Auto\nFull band", u->full_band, chart_range_event, u);
    lv_obj_t *legend = label(chart_header, "Footprint = reported width; tick = primary channel\nUnknown geometry = marker only. SDK width unverified.", true);
    lv_obj_set_width(legend, lv_pct(60));
    lv_obj_t *navigation = container(u->body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(navigation, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    u->chart_zoom = dropdown(navigation, "Zoom", "1x\n2x\n4x", u->zoom == 4 ? 2 : u->zoom == 2 ? 1 : 0, zoom_event, u);
    u->pan_left = button(navigation, LV_SYMBOL_LEFT, 64, pan_event, u);
    u->pan_right = button(navigation, LV_SYMBOL_RIGHT, 64, pan_event, u);
    button(navigation, "Fit", 80, fit_event, u);
    button(navigation, "Propose channel", 170, propose_event, u);
    u->chart = lv_obj_create(u->body);
    lv_obj_set_size(u->chart, lv_pct(100), 264);
    lv_obj_set_style_bg_color(u->chart, subghz_host_ui_panel(), 0);
    lv_obj_set_style_border_width(u->chart, 0, 0);
    lv_obj_set_style_radius(u->chart, 12, 0);
    lv_obj_set_style_pad_all(u->chart, 8, 0);
    lv_obj_remove_flag(u->chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(u->chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(u->chart, chart_draw, LV_EVENT_DRAW_MAIN, u);
    lv_obj_add_event_cb(u->chart, chart_tap, LV_EVENT_SHORT_CLICKED, u);
    u->channel_counts = label(u->body, "Observed APs by primary channel: --", true);
    lv_obj_set_width(u->channel_counts, lv_pct(100));
    u->advice_panel = container(u->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(u->advice_panel, subghz_host_ui_panel(), 0);
    lv_obj_set_style_bg_opa(u->advice_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(u->advice_panel, 12, 0);
    lv_obj_set_style_pad_all(u->advice_panel, 12, 0);
    lv_obj_set_flex_align(u->advice_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *advice_header = container(u->advice_panel, LV_FLEX_FLOW_ROW);
    lv_obj_t *advice_title = label(advice_header, "Channel proposal / 20 MHz", false);
    lv_obj_set_flex_grow(advice_title, 1);
    button(advice_header, "Close", 90, advice_close_event, u);
    lv_obj_t *advice_options = container(u->advice_panel, LV_FLEX_FLOW_ROW_WRAP);
    u->advice_pool = dropdown(advice_options, "5 GHz candidates", "36-48\n36-165 (incl. DFS)", 0, advice_options_event, u);
    u->advice_own = dropdown(advice_options, "Own access point", "Include all APs\nExclude selected AP", 0, advice_options_event, u);
    u->advice_text = label(u->advice_panel, "", false);
    lv_obj_set_width(u->advice_text, lv_pct(100));
    lv_label_set_long_mode(u->advice_text, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(u->advice_panel, LV_OBJ_FLAG_HIDDEN);
    u->selection = button(u->body, "Tap a network trace or row to show details below", lv_pct(100), show_details_event, u);
    lv_obj_t *selection_text = lv_obj_get_child(u->selection, 0);
    lv_obj_set_width(selection_text, lv_pct(100));
    lv_label_set_long_mode(selection_text, LV_LABEL_LONG_DOT);
    set_disabled(u->selection, !u->selected[0]);
    lv_obj_t *sort = container(u->body, LV_FLEX_FLOW_ROW_WRAP);
    label(sort, "Access points  |  Sort:", true);
    const char *names[] = {"RSSI", "Channel", "Name"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *b = button(sort, names[i], 100, sort_event, u);
        u->sort_buttons[i] = b;
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
    u->rows = container(u->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(u->rows, 320);
    lv_obj_add_flag(u->rows, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(u->rows, LV_DIR_VER);
    lv_obj_set_flex_align(u->rows, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    app_keyboard_navigation_register_scroll_area_activatable(u->rows);
    /* The wrapper expands only below the list. The fixed inner card keeps
     * text/header geometry stable throughout the reveal animation. */
    u->details = container(u->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(u->details, 0);
    lv_obj_remove_flag(u->details, LV_OBJ_FLAG_OVERFLOW_VISIBLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *card = container(u->details, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(card, 248);
    lv_obj_set_style_bg_color(card, subghz_host_ui_panel(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *detail_header = container(card, LV_FLEX_FLOW_ROW);
    lv_obj_t *detail_title = label(detail_header, "Network details", false);
    lv_obj_set_flex_grow(detail_title, 1);
    button(detail_header, "Close", 90, close_details, u);
    u->detail_scroll = container(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_height(u->detail_scroll, 0);
    lv_obj_set_flex_grow(u->detail_scroll, 1);
    lv_obj_add_flag(u->detail_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(u->detail_scroll, LV_DIR_VER);
    lv_obj_set_flex_align(u->detail_scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    app_keyboard_navigation_register_scroll_area_activatable(u->detail_scroll);
    u->detail_text = label(u->detail_scroll, "", false);
    lv_obj_set_width(u->detail_text, lv_pct(100));
    lv_label_set_long_mode(u->detail_text, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(u->details, LV_OBJ_FLAG_HIDDEN);
    u->scope = label(u->body, "", true);
    lv_obj_set_width(u->scope, lv_pct(100));
    u->dirty = true;
    u->rendered_revision = UINT32_MAX;
    refresh(u);
    controls(u);
    if (!u->session || !u->snapshot)
        lv_label_set_text(u->status_label, "no_psram: analyzer storage unavailable. Close and reopen when memory is available.");
    u->timer = lv_timer_create(tick, 250, u);
    return u->root;
}

void wa_screen_hide(int tab)
{
    if (tab < 0 || tab >= 3) return;
    wa_ui_t *u = &views[tab];
    lv_anim_delete(u, chart_anim_exec);
    lv_anim_delete(u, details_anim_exec);
    details_anim_exec(u, u->details_open ? 248 : 0);
    u->reveal = 1024;
    wa_leave(tab);
    u->pending_back = false;
    if (u->editor) editor_close(u, false);
    if (u->root) lv_obj_add_flag(u->root, LV_OBJ_FLAG_HIDDEN);
}
