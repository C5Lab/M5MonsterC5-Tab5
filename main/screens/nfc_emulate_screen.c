#include "nfc_host.h"
#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "nfc_emulate";

#define NFC_EMULATE_TIMEOUT_MS 5000

static void set_labels(nfc_tab_state_t *st, const char *status, const char *detail, lv_color_t color)
{
    if (!st) return;
    if (st->emu_status) {
        lv_label_set_text(st->emu_status, status ? status : "");
        lv_obj_set_style_text_color(st->emu_status, color, 0);
    }
    if (st->emu_detail)
        lv_label_set_text(st->emu_detail, detail ? detail : "");
}

static void fill_capability(nfc_tab_state_t *st, const nfc_ui_card_t *card, const char *summary)
{
    const char *type = card->type[0] ? card->type :
                       (st->hint_type[0] ? st->hint_type : summary);
    bool classic = nfc_type_is_classic(type);
    char detail[160];

    if (card->emulate_full_ul) {
        snprintf(detail, sizeof(detail), "%s\nNTAG/Ultralight pages",
                 summary && summary[0] ? summary : "");
    } else {
        snprintf(detail, sizeof(detail), "%s\nUID/ATQA/SAK only%s",
                 summary && summary[0] ? summary : "",
                 classic ? "\nClassic Crypto1 not emulated" : "");
    }
    set_labels(st, "Emulating", detail, subghz_host_color_green());
}

static void on_back(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    int idx = st ? st->detail_idx : -1;
    if (idx >= 0)
        show_nfc_detail_page(idx);
    else
        show_nfc_list_page();
}

void nfc_emulate_on_result(nfc_tab_state_t *st)
{
    if (!st || !st->emulate_page) return;

    const nfc_ui_card_t *card = &st->result_card;
    const char *summary = card->have_emulating ? card->emulating_summary : "";

    if (!st->result_timeout && card->have_emulating) {
        st->emulate_running = true;
        fill_capability(st, card, summary);
    } else if (card->emulate_failed || card->no_card_loaded || card->load_failed) {
        st->emulate_running = false;
        set_labels(st, "Emulate failed", "Only NFC-A UID emulation", subghz_host_color_red());
    } else {
        st->emulate_running = false;
        set_labels(st, st->result_timeout ? "Start timed out" : "Start failed",
                   "", subghz_host_color_red());
    }
}

void show_nfc_emulate_page(void)
{
    nfc_tab_state_t *st = nfc_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    int idx = st->detail_idx;
    subghz_host_hide_all_pages();
    st->detail_idx = idx;
    st->emulate_running = false;

    st->emulate_page = nfc_make_page(container);
    subghz_create_header(st->emulate_page, "NFC Emulate", subghz_host_color_orange(), on_back);

    lv_obj_t *icon = lv_label_create(st->emulate_page);
    lv_label_set_text(icon, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, subghz_host_color_orange(), 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(icon, lv_pct(100));

    st->emu_status = lv_label_create(st->emulate_page);
    lv_obj_set_width(st->emu_status, lv_pct(100));
    lv_obj_set_style_text_align(st->emu_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->emu_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st->emu_status, subghz_host_color_cyan(), 0);
    lv_label_set_text(st->emu_status, "Starting...");

    st->emu_detail = lv_label_create(st->emulate_page);
    lv_obj_set_width(st->emu_detail, lv_pct(100));
    lv_obj_set_flex_grow(st->emu_detail, 1);
    lv_obj_set_style_text_align(st->emu_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->emu_detail, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->emu_detail, subghz_host_ui_muted(), 0);
    lv_label_set_long_mode(st->emu_detail, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(st->emu_detail, "Card #%d", idx);

    lv_obj_t *hint = lv_label_create(st->emulate_page);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, subghz_host_ui_muted(), 0);
    lv_label_set_text(hint, "Back sends stop");

    nfc_uart_start(st, NFC_OP_EMULATE, "start_nfc_emulate", NFC_EMULATE_TIMEOUT_MS);
    ESP_LOGI(TAG, "NFC Emulate idx=%d", idx);
}
