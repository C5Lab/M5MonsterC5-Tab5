#include "nfc_host.h"
#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "nfc_list";

#define NFC_LIST_TIMEOUT_MS 5000

static void on_back(lv_event_t *e)
{
    (void)e;
    show_nfc_page();
}

static void on_row_tap(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0) return;
    show_nfc_detail_page(idx);
}

static void build_list(nfc_tab_state_t *st)
{
    if (!st || !st->list_obj) return;
    lv_obj_clean(st->list_obj);

    if (st->row_count == 0) {
        lv_obj_t *l = lv_label_create(st->list_obj);
        lv_label_set_text(l, "No saved cards");
        lv_obj_set_style_text_color(l, subghz_host_ui_muted(), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        return;
    }

    for (int i = 0; i < st->row_count; i++) {
        nfc_list_entry_t *row = &st->rows[i];
        lv_obj_t *obj = lv_obj_create(st->list_obj);
        lv_obj_set_size(obj, lv_pct(100), 56);
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(obj, 8, 0);
        lv_obj_set_style_pad_gap(obj, 8, 0);
        lv_obj_set_style_bg_color(obj, subghz_host_ui_card(), 0);
        lv_obj_set_style_bg_color(obj, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_radius(obj, 8, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, on_row_tap, LV_EVENT_CLICKED, (void *)(intptr_t)row->idx);

        lv_obj_t *info = lv_label_create(obj);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(info, subghz_host_ui_text(), 0);
        lv_label_set_long_mode(info, LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(info, "%d  %s", row->idx, row->name);
    }
}

void nfc_list_on_result(nfc_tab_state_t *st)
{
    if (!st || !st->list_page) return;

    st->row_count = 0;
    int reported = -1;
    for (int i = 0; i < st->line_count; i++) {
        nfc_list_entry_t ent;
        if (nfc_parse_list_entry(st->lines[i], &ent)) {
            if (st->row_count < NFC_LIST_CAP) {
                st->rows[st->row_count] = ent;
                st->row_count++;
            }
            continue;
        }
        int n = 0;
        if (nfc_parse_card_count(st->lines[i], &n))
            reported = n;
    }
    if (reported >= 0 && reported != st->row_count)
        ESP_LOGI(TAG, "card(s)=%d parsed=%d", reported, st->row_count);

    build_list(st);
    if (!st->list_status) return;
    if (st->result_timeout && st->row_count == 0) {
        lv_label_set_text(st->list_status, "List timed out");
        lv_obj_set_style_text_color(st->list_status, subghz_host_color_red(), 0);
    } else if (st->row_count == 0) {
        lv_label_set_text(st->list_status, "No saved cards");
        lv_obj_set_style_text_color(st->list_status, subghz_host_ui_muted(), 0);
    } else {
        lv_label_set_text_fmt(st->list_status, "%d card(s)", st->row_count);
        lv_obj_set_style_text_color(st->list_status, subghz_host_ui_muted(), 0);
    }
}

void show_nfc_list_page(void)
{
    nfc_tab_state_t *st = nfc_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    st->row_count = 0;

    st->list_page = nfc_make_page(container);
    subghz_create_header(st->list_page, "NFC List", subghz_host_color_orange(), on_back);

    st->list_status = lv_label_create(st->list_page);
    lv_obj_set_style_text_font(st->list_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->list_status, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->list_status, "Loading...");

    st->list_obj = lv_obj_create(st->list_page);
    lv_obj_set_width(st->list_obj, lv_pct(100));
    lv_obj_set_flex_grow(st->list_obj, 1);
    lv_obj_set_flex_flow(st->list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(st->list_obj, 4, 0);
    lv_obj_set_style_pad_gap(st->list_obj, 6, 0);
    lv_obj_set_style_bg_opa(st->list_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->list_obj, 0, 0);
    lv_obj_set_scrollbar_mode(st->list_obj, LV_SCROLLBAR_MODE_AUTO);

    nfc_uart_start(st, NFC_OP_LIST, "nfc_list", NFC_LIST_TIMEOUT_MS);
    ESP_LOGI(TAG, "NFC List page ready");
}
