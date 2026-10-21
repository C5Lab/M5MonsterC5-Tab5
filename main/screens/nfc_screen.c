#include "nfc_host.h"
#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nfc";

#define NFC_INIT_TIMEOUT_MS 3000

static void destroy_page(lv_obj_t **page)
{
    if (page && *page) {
        lv_obj_delete(*page);
        *page = NULL;
    }
}

void nfc_set_btn_enabled(lv_obj_t *btn, bool en)
{
    if (!btn) return;
    if (en)
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
    else
        lv_obj_add_state(btn, LV_STATE_DISABLED);
}

lv_obj_t *nfc_make_page(lv_obj_t *container)
{
    lv_obj_t *page = lv_obj_create(container);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 10, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static lv_obj_t *nfc_create_tile(lv_obj_t *parent, const char *icon, const char *text,
                                 lv_color_t accent, lv_event_cb_t cb)
{
    bool large = lv_disp_get_ver_res(NULL) >= 1000 || lv_disp_get_hor_res(NULL) >= 960;
    lv_coord_t tile_w = large ? 220 : 208;
    lv_coord_t tile_h = large ? 154 : 146;
    const lv_font_t *icon_font = large ? &lv_font_montserrat_36 : &lv_font_montserrat_34;
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

void nfc_hide_all_pages(nfc_tab_state_t *st)
{
    if (!st) return;

    bool need_stop = st->emulate_running || st->op == NFC_OP_EMULATE;
    nfc_uart_cancel(st);
    if (need_stop) {
        st->emulate_running = false;
        subghz_host_uart_send_for_tab(st->tab_id, "stop");
    }

    if (st->confirm_popup) {
        lv_obj_delete(st->confirm_popup);
        st->confirm_popup = NULL;
    }
    st->text_input_open = false;

    destroy_page(&st->hub_page);
    destroy_page(&st->read_page);
    destroy_page(&st->list_page);
    destroy_page(&st->detail_page);
    destroy_page(&st->emulate_page);

    st->hub_status = NULL;
    st->hub_read_tile = NULL;
    st->hub_list_tile = NULL;
    st->read_status = NULL;
    st->read_detail = NULL;
    st->read_btn = NULL;
    st->save_btn = NULL;
    st->list_status = NULL;
    st->list_obj = NULL;
    st->detail_status = NULL;
    st->detail_lbl = NULL;
    st->detail_emu_btn = NULL;
    st->emu_status = NULL;
    st->emu_detail = NULL;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_host_show_main_tiles();
}

static void on_read(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || st->busy || !st->hub_page) return;
    if (st->hub_read_tile && lv_obj_has_state(st->hub_read_tile, LV_STATE_DISABLED)) return;
    ESP_LOGI(TAG, "Read");
    show_nfc_read_page();
}

static void on_list(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || st->busy || !st->hub_page) return;
    ESP_LOGI(TAG, "List");
    show_nfc_list_page();
}

static void apply_init(nfc_tab_state_t *st, bool detected)
{
    nfc_set_btn_enabled(st->hub_read_tile, detected);
    nfc_set_btn_enabled(st->hub_list_tile, true);
    if (!st->hub_status) return;
    if (detected) {
        lv_label_set_text(st->hub_status, "NFC ready");
        lv_obj_set_style_text_color(st->hub_status, subghz_host_color_green(), 0);
    } else {
        lv_label_set_text(st->hub_status, "NFC not detected");
        lv_obj_set_style_text_color(st->hub_status, subghz_host_color_red(), 0);
    }
}

void nfc_hub_on_init(nfc_tab_state_t *st)
{
    if (!st || !st->hub_page) return;
    bool detected = !st->result_timeout &&
                    st->result_card.detected &&
                    !st->result_card.not_detected;
    apply_init(st, detected);
}

void show_nfc_page(void)
{
    nfc_tab_state_t *st = nfc_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) {
        ESP_LOGE(TAG, "show_nfc_page: no state or container");
        return;
    }

    subghz_host_hide_all_pages();

    st->hub_page = nfc_make_page(container);
    subghz_create_header(st->hub_page, "NFC", subghz_host_color_cyan(), on_back);

    st->hub_status = lv_label_create(st->hub_page);
    lv_obj_set_width(st->hub_status, lv_pct(100));
    lv_obj_set_style_text_align(st->hub_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->hub_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->hub_status, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->hub_status, "Probing...");

    lv_obj_t *tiles = lv_obj_create(st->hub_page);
    lv_obj_set_size(tiles, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tiles, 1);
    lv_obj_set_style_bg_opa(tiles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tiles, 0, 0);
    lv_obj_set_style_pad_all(tiles, 2, 0);
    lv_obj_set_style_pad_gap(tiles, 10, 0);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    subghz_grid_scrollable(tiles);

    st->hub_read_tile = nfc_create_tile(tiles, LV_SYMBOL_DOWNLOAD, "Read",
                                        subghz_host_color_cyan(), on_read);
    st->hub_list_tile = nfc_create_tile(tiles, LV_SYMBOL_LIST, "List",
                                        subghz_host_color_orange(), on_list);
    nfc_set_btn_enabled(st->hub_read_tile, false);
    nfc_set_btn_enabled(st->hub_list_tile, false);

    nfc_uart_start(st, NFC_OP_INIT, "init_nfc", NFC_INIT_TIMEOUT_MS);
    ESP_LOGI(TAG, "NFC hub ready");
}
