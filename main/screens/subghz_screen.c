#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "subghz";

/* ---------- Tile factory (Tab5-flavoured) ---------------------------- */

typedef void (*subghz_tile_cb_t)(lv_event_t *e);

static lv_obj_t *subghz_create_tile(lv_obj_t *parent, const char *icon,
                                    const char *text, lv_color_t accent,
                                    subghz_tile_cb_t cb)
{
    bool large = lv_disp_get_ver_res(NULL) >= 1000 || lv_disp_get_hor_res(NULL) >= 960;
    lv_coord_t tile_w = large ? 220 : 208;
    lv_coord_t tile_h = large ? 154 : 146;

    const lv_font_t *icon_font  = large ? &lv_font_montserrat_36 : &lv_font_montserrat_34;
    const lv_font_t *label_font = large ? &lv_font_montserrat_18 : &lv_font_montserrat_16;

    lv_obj_t *tile = lv_btn_create(parent);
    lv_obj_set_size(tile, tile_w, tile_h);
    lv_obj_set_style_bg_color(tile, subghz_host_ui_card(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(tile, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 16, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tile, large ? 10 : 12, 0);

    /* Accent stripe */
    lv_obj_t *accent_bar = lv_obj_create(tile);
    lv_obj_remove_style_all(accent_bar);
    lv_obj_set_size(accent_bar, large ? 64 : 58, 3);
    lv_obj_align(accent_bar, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_set_style_bg_color(accent_bar, accent, 0);
    lv_obj_add_flag(accent_bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(accent_bar, LV_OBJ_FLAG_CLICKABLE);

    if (icon) {
        lv_obj_t *icon_label = lv_label_create(tile);
        lv_label_set_text(icon_label, icon);
        lv_obj_set_style_text_font(icon_label, icon_font, 0);
        lv_obj_set_style_text_color(icon_label, accent, 0);
    }

    if (text) {
        lv_obj_t *text_label = lv_label_create(tile);
        lv_label_set_text(text_label, text);
        lv_obj_set_style_text_font(text_label, label_font, 0);
        lv_obj_set_style_text_color(text_label, subghz_host_ui_text(), 0);
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text_label, tile_w - 30);
    }

    if (cb) lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);
    return tile;
}

/* ---------- Header (back + title) ----------------------------------- */

lv_obj_t *subghz_create_header(lv_obj_t *parent, const char *title,
                               lv_color_t title_color, lv_event_cb_t on_back)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    if (on_back) lv_obj_add_event_cb(back_btn, on_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    lv_obj_t *t = lv_label_create(header);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, title_color, 0);

    return header;
}

lv_obj_t *subghz_add_header_action(lv_obj_t *header, const char *symbol,
                                   lv_event_cb_t cb, void *user_data)
{
    if (!header) return NULL;
    lv_obj_t *btn = lv_btn_create(header);
    lv_obj_set_size(btn, 60, 56);
    lv_obj_set_style_bg_color(btn, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_color(btn, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_ext_click_area(btn, 18);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol ? symbol : LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_center(lbl);

    return btn;
}

/* ---------- Style helper ------------------------------------------- */

void subghz_style_popup_card(lv_obj_t *popup, lv_coord_t radius, lv_color_t accent)
{
    if (!popup) return;
    lv_obj_set_style_bg_color(popup, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_border_color(popup, accent, 0);
    lv_obj_set_style_border_opa(popup, LV_OPA_70, 0);
    lv_obj_set_style_radius(popup, radius, 0);
    lv_obj_set_style_shadow_width(popup, 18, 0);
    lv_obj_set_style_shadow_color(popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(popup, LV_OPA_30, 0);
}

/* ---------- Event callbacks ----------------------------------------- */

void subghz_on_back_to_menu(lv_event_t *e)
{
    (void)e;
    show_subghz_page();
}

static void on_back_to_main(lv_event_t *e)
{
    (void)e;
    subghz_host_show_main_tiles();
}

static void on_scanner(lv_event_t *e)  { (void)e; ESP_LOGI(TAG, "Quick Scan"); show_subghz_scanner_page(); }
static void on_hunter(lv_event_t *e)   { (void)e; ESP_LOGI(TAG, "Hunter");     show_subghz_hunter_page(); }
static void on_listen(lv_event_t *e)   { (void)e; ESP_LOGI(TAG, "Listen");     show_subghz_listen_page(); }
static void on_manage(lv_event_t *e)   { (void)e; ESP_LOGI(TAG, "SD Signals"); show_subghz_manage_page(); }
static void on_weather(lv_event_t *e)  { (void)e; ESP_LOGI(TAG, "Weather");    show_subghz_weather_page(); }
static void on_jammer(lv_event_t *e)   { (void)e; ESP_LOGI(TAG, "Jammer");     show_subghz_jammer_page(); }
static void on_tesla(lv_event_t *e)    { (void)e; ESP_LOGI(TAG, "Tesla");      show_subghz_tesla_page(); }
static void on_settings(lv_event_t *e) { (void)e; ESP_LOGI(TAG, "Settings");   show_subghz_settings_page(); }

/* ---------- Public entry ------------------------------------------- */

void subghz_hide_all_pages(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->page) lv_obj_add_flag(st->page, LV_OBJ_FLAG_HIDDEN);
    if (st->listen_page) lv_obj_add_flag(st->listen_page, LV_OBJ_FLAG_HIDDEN);
    if (st->manage_page) lv_obj_add_flag(st->manage_page, LV_OBJ_FLAG_HIDDEN);
    if (st->jammer_page) lv_obj_add_flag(st->jammer_page, LV_OBJ_FLAG_HIDDEN);
    if (st->tesla_page) lv_obj_add_flag(st->tesla_page, LV_OBJ_FLAG_HIDDEN);
    if (st->hunter_page) lv_obj_add_flag(st->hunter_page, LV_OBJ_FLAG_HIDDEN);
    if (st->scanner_page) lv_obj_add_flag(st->scanner_page, LV_OBJ_FLAG_HIDDEN);
    if (st->weather_page) lv_obj_add_flag(st->weather_page, LV_OBJ_FLAG_HIDDEN);
    if (st->settings_page) lv_obj_add_flag(st->settings_page, LV_OBJ_FLAG_HIDDEN);
    if (st->listen_settings_page) lv_obj_add_flag(st->listen_settings_page, LV_OBJ_FLAG_HIDDEN);
    if (st->hunter_settings_page) lv_obj_add_flag(st->hunter_settings_page, LV_OBJ_FLAG_HIDDEN);
    if (st->scanner_settings_page) lv_obj_add_flag(st->scanner_settings_page, LV_OBJ_FLAG_HIDDEN);
}

void show_subghz_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) {
        ESP_LOGE(TAG, "show_subghz_page: no state or container");
        return;
    }

    /* Hide other (non-subghz) pages and other subghz sub-pages */
    subghz_host_hide_all_pages();

    if (st->page) {
        lv_obj_clear_flag(st->page, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Showing existing SubGHz menu page");
        return;
    }

    ESP_LOGI(TAG, "Creating SubGHz menu page");

    st->page = lv_obj_create(container);
    lv_obj_set_size(st->page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->page, 0, 0);
    lv_obj_set_style_pad_all(st->page, 10, 0);
    lv_obj_set_flex_flow(st->page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->page, 10, 0);

    subghz_create_header(st->page, "Sub-GHz", subghz_host_color_pink(), on_back_to_main);

    /* Tile grid */
    lv_obj_t *tiles = lv_obj_create(st->page);
    lv_obj_set_size(tiles, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tiles, 0, 0);
    lv_obj_set_style_pad_all(tiles, 2, 0);
    lv_obj_set_style_pad_gap(tiles, 10, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_tile(tiles, LV_SYMBOL_REFRESH,  "Quick Scan", subghz_host_color_cyan(),   on_scanner);
    subghz_create_tile(tiles, LV_SYMBOL_GPS,      "Hunter",     subghz_host_color_pink(),   on_hunter);
    subghz_create_tile(tiles, LV_SYMBOL_EYE_OPEN, "Listen",     subghz_host_color_cyan(),   on_listen);
    subghz_create_tile(tiles, LV_SYMBOL_LIST,     "SD Signals", subghz_host_color_orange(), on_manage);
    subghz_create_tile(tiles, LV_SYMBOL_TINT,     "Weather",    subghz_host_color_blue(),   on_weather);
    subghz_create_tile(tiles, LV_SYMBOL_WARNING,  "Jammer",     subghz_host_color_red(),    on_jammer);
    subghz_create_tile(tiles, LV_SYMBOL_POWER,    "Tesla",      subghz_host_color_purple(), on_tesla);
    subghz_create_tile(tiles, LV_SYMBOL_SETTINGS, "Settings",   subghz_host_ui_muted(),     on_settings);
}
