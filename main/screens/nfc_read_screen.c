#include "nfc_host.h"
#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "nfc_read";

#define NFC_READ_TIMEOUT_MS 20000
#define NFC_SAVE_TIMEOUT_MS 5000

static void set_labels(nfc_tab_state_t *st, const char *status, const char *detail, lv_color_t color)
{
    if (!st) return;
    if (st->read_status) {
        lv_label_set_text(st->read_status, status ? status : "");
        lv_obj_set_style_text_color(st->read_status, color, 0);
    }
    if (st->read_detail)
        lv_label_set_text(st->read_detail, detail ? detail : "");
}

static void apply_card(nfc_tab_state_t *st)
{
    char status[64];
    char detail[160];

    if (st->card.no_card) {
        set_labels(st, "No card detected", "Try again", subghz_host_color_orange());
        st->have_card = false;
    } else if (st->card.not_detected || st->card.not_initialized) {
        set_labels(st, "NFC not ready", "Check wiring / power", subghz_host_color_red());
        st->have_card = false;
    } else if (!st->card.type[0] && !st->card.uid[0]) {
        set_labels(st, "No result", "", subghz_host_ui_muted());
        st->have_card = false;
    } else {
        nfc_format_card_detail(&st->card, status, sizeof(status), detail, sizeof(detail));
        set_labels(st, status, detail, subghz_host_color_green());
        st->have_card = true;
    }
    nfc_set_btn_enabled(st->save_btn, st->have_card);
    nfc_set_btn_enabled(st->read_btn, true);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (st && st->text_input_open) return;
    show_nfc_page();
}

static void on_read(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || st->busy || st->text_input_open || !st->read_page) return;

    nfc_card_reset(&st->card);
    st->have_card = false;
    nfc_set_btn_enabled(st->save_btn, false);
    nfc_set_btn_enabled(st->read_btn, false);
    set_labels(st, "Present a card...", "", subghz_host_color_cyan());
    nfc_uart_start(st, NFC_OP_READ, "nfc_read", NFC_READ_TIMEOUT_MS);
    ESP_LOGI(TAG, "nfc_read started");
}

static void on_save_confirm(const char *text, void *user_data)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)user_data;
    if (!st) return;
    st->text_input_open = false;
    if (!st->read_page) return;

    if (!text || !text[0]) {
        set_labels(st, "Save: empty name", "", subghz_host_color_red());
        return;
    }
    if (!nfc_name_is_valid(text)) {
        set_labels(st, "Save: invalid name", "", subghz_host_color_red());
        return;
    }

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "nfc_save %s", text);
    set_labels(st, "Saving...", text, subghz_host_color_blue());
    nfc_uart_start(st, NFC_OP_SAVE, cmd, NFC_SAVE_TIMEOUT_MS);
    ESP_LOGI(TAG, "nfc_save %s", text);
}

static void on_save_cancel(void *user_data)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)user_data;
    if (st) st->text_input_open = false;
}

static void on_save(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || st->busy || st->text_input_open || !st->have_card || !st->read_page) return;

    st->text_input_open = true;
    subghz_show_text_input_popup("Save card", "", 48, subghz_host_color_blue(),
                                 on_save_confirm, on_save_cancel, st);
}

void nfc_read_on_live(nfc_tab_state_t *st, const char *status, const char *detail)
{
    if (!st || !st->read_page || st->op != NFC_OP_READ) return;
    set_labels(st, status, detail, subghz_host_color_cyan());
}

void nfc_read_on_result(nfc_tab_state_t *st)
{
    if (!st || !st->read_page) return;
    if (st->result_timeout && !st->result_card.type[0] && !st->result_card.uid[0])
        st->result_card.no_card = true;
    st->card = st->result_card;
    apply_card(st);
}

void nfc_read_on_save(nfc_tab_state_t *st)
{
    if (!st || !st->read_page) return;

    if (st->result_card.have_saved) {
        set_labels(st, "Saved", st->result_card.saved_path, subghz_host_color_green());
    } else if (st->result_card.nothing_to_save || nfc_lines_contain(st, "[NFC] nothing to save")) {
        set_labels(st, "Nothing to save", "", subghz_host_color_orange());
        st->have_card = false;
    } else {
        set_labels(st, st->result_timeout ? "Save timed out" : "Save failed",
                   "", subghz_host_color_red());
    }
    nfc_set_btn_enabled(st->save_btn, st->have_card);
    nfc_set_btn_enabled(st->read_btn, true);
}

void show_nfc_read_page(void)
{
    nfc_tab_state_t *st = nfc_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    st->have_card = false;
    st->text_input_open = false;
    nfc_card_reset(&st->card);

    st->read_page = nfc_make_page(container);
    subghz_create_header(st->read_page, "NFC Read", subghz_host_color_cyan(), on_back);

    st->read_status = lv_label_create(st->read_page);
    lv_obj_set_width(st->read_status, lv_pct(100));
    lv_obj_set_style_text_align(st->read_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->read_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st->read_status, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->read_status, "Ready");

    st->read_detail = lv_label_create(st->read_page);
    lv_obj_set_width(st->read_detail, lv_pct(100));
    lv_obj_set_flex_grow(st->read_detail, 1);
    lv_obj_set_style_text_align(st->read_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->read_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->read_detail, subghz_host_ui_muted(), 0);
    lv_label_set_long_mode(st->read_detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(st->read_detail, "");

    lv_obj_t *brow = lv_obj_create(st->read_page);
    lv_obj_set_size(brow, lv_pct(100), 64);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_set_style_pad_all(brow, 0, 0);
    lv_obj_set_style_pad_gap(brow, 12, 0);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    st->read_btn = lv_btn_create(brow);
    lv_obj_set_size(st->read_btn, 180, 56);
    lv_obj_set_style_bg_color(st->read_btn, subghz_host_color_cyan(), 0);
    lv_obj_set_style_radius(st->read_btn, 8, 0);
    lv_obj_add_event_cb(st->read_btn, on_read, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(st->read_btn);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Read");
    lv_obj_set_style_text_color(rl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_18, 0);
    lv_obj_center(rl);

    st->save_btn = lv_btn_create(brow);
    lv_obj_set_size(st->save_btn, 180, 56);
    lv_obj_set_style_bg_color(st->save_btn, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(st->save_btn, 8, 0);
    lv_obj_add_event_cb(st->save_btn, on_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(st->save_btn);
    lv_label_set_text(sl, LV_SYMBOL_SAVE " Save");
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_18, 0);
    lv_obj_center(sl);
    nfc_set_btn_enabled(st->save_btn, false);

    ESP_LOGI(TAG, "NFC Read page ready");
}
