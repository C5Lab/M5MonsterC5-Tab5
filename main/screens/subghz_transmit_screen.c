#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_signal_list.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "subghz_tx";

static void on_back(lv_event_t *e);

static void close_tx_popup(subghz_tab_state_t *st)
{
    if (st->tx_popup) {
        lv_obj_delete(st->tx_popup);
        st->tx_popup = NULL;
    }
}

static void on_tx_confirm(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    if (!st) return;
    int idx = st->pending_tx_idx;
    close_tx_popup(st);

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_tx %d", idx);
    subghz_host_uart_send(cmd);

    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "Transmitted signal #%d", idx);
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_color_green(), 0);
    }
    ESP_LOGI(TAG, "Transmit signal idx=%d", idx);
}

static void on_tx_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    close_tx_popup(st);
}

static subghz_stored_sig_t *find_sig_by_idx(subghz_tab_state_t *st, int idx)
{
    subghz_stored_sig_t *arr = (subghz_stored_sig_t *)st->sigs;
    if (!arr) return NULL;
    for (int i = 0; i < st->sigs_count; i++)
        if (arr[i].idx == idx) return &arr[i];
    return NULL;
}

static void on_signal_tap(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (st->tx_popup) close_tx_popup(st);

    st->pending_tx_idx = idx;
    subghz_stored_sig_t *sig = find_sig_by_idx(st, idx);

    /* Overlay */
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->tx_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 460, 240);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_green());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 16, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *info = lv_label_create(popup);
    if (sig) {
        lv_label_set_text_fmt(info, "#%d  %s  %d.%02d MHz\n%s  %s",
                              sig->idx, sig->type,
                              (int)sig->freq, ((int)(sig->freq * 100.0f + 0.5f)) % 100,
                              sig->mf[0] ? sig->mf : "--", sig->serial);
    } else {
        lv_label_set_text_fmt(info, "Signal #%d", idx);
    }
    lv_obj_set_style_text_color(info, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_row = lv_obj_create(popup);
    lv_obj_set_size(btn_row, lv_pct(100), 60);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tx_btn = lv_btn_create(btn_row);
    lv_obj_set_size(tx_btn, 180, 50);
    lv_obj_set_style_bg_color(tx_btn, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(tx_btn, 8, 0);
    lv_obj_add_event_cb(tx_btn, on_tx_confirm, LV_EVENT_CLICKED, st);
    lv_obj_t *tl = lv_label_create(tx_btn);
    lv_label_set_text(tl, LV_SYMBOL_PLAY " Transmit");
    lv_obj_set_style_text_color(tl, lv_color_white(), 0);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_18, 0);
    lv_obj_center(tl);

    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 160, 50);
    lv_obj_set_style_bg_color(cancel_btn, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, on_tx_cancel, LV_EVENT_CLICKED, st);
    lv_obj_t *cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);
}

static void build_list(subghz_tab_state_t *st)
{
    if (!st->sig_list_obj) return;
    lv_obj_clean(st->sig_list_obj);

    if (st->sigs_count == 0) {
        lv_obj_t *l = lv_label_create(st->sig_list_obj);
        lv_label_set_text(l, "No stored signals");
        lv_obj_set_style_text_color(l, subghz_host_ui_muted(), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        return;
    }

    subghz_stored_sig_t *sigs = (subghz_stored_sig_t *)st->sigs;
    for (int i = 0; i < st->sigs_count; i++) {
        subghz_stored_sig_t *sig = &sigs[i];

        lv_obj_t *row = lv_obj_create(st->sig_list_obj);
        lv_obj_set_size(row, lv_pct(100), 56);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_gap(row, 12, 0);
        lv_obj_set_style_bg_color(row, subghz_host_ui_card(), 0);
        lv_obj_set_style_bg_color(row, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, on_signal_tap, LV_EVENT_CLICKED, (void *)(intptr_t)sig->idx);

        lv_obj_t *l;

        l = lv_label_create(row);
        lv_obj_set_width(l, 60);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(l, subghz_host_color_green(), 0);
        lv_label_set_text_fmt(l, "%d", sig->idx);

        l = lv_label_create(row);
        lv_obj_set_width(l, 160);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(l, subghz_host_ui_text(), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_label_set_text(l, sig->type);

        l = lv_label_create(row);
        lv_obj_set_width(l, 130);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(l, subghz_host_ui_muted(), 0);
        lv_label_set_text_fmt(l, "%d.%02d", (int)sig->freq, ((int)(sig->freq * 100.0f + 0.5f)) % 100);

        l = lv_label_create(row);
        lv_obj_set_flex_grow(l, 1);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(l, subghz_host_color_cyan(), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_label_set_text(l, sig->mf[0] ? sig->mf : sig->serial);

        l = lv_label_create(row);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(l, subghz_host_color_green(), 0);
        lv_label_set_text(l, LV_SYMBOL_PLAY);
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    if (st->tx_popup) { lv_obj_delete(st->tx_popup); st->tx_popup = NULL; }
    if (st->transmit_page) { lv_obj_delete(st->transmit_page); st->transmit_page = NULL; }
    st->sig_list_obj = NULL;
    st->status_lbl = NULL;
    show_subghz_page();
}

void show_subghz_transmit_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();

    /* Always rebuild — the signal list may have changed since last visit */
    if (st->transmit_page) { lv_obj_delete(st->transmit_page); st->transmit_page = NULL; }

    st->transmit_page = lv_obj_create(container);
    lv_obj_set_size(st->transmit_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->transmit_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->transmit_page, 0, 0);
    lv_obj_set_style_pad_all(st->transmit_page, 10, 0);
    lv_obj_set_flex_flow(st->transmit_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->transmit_page, 8, 0);
    lv_obj_clear_flag(st->transmit_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->transmit_page, "Transmit", subghz_host_color_green(), on_back);

    st->status_lbl = lv_label_create(st->transmit_page);
    lv_obj_set_style_text_font(st->status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->status_lbl, "Loading signals...");

    st->sig_list_obj = lv_obj_create(st->transmit_page);
    lv_obj_set_size(st->sig_list_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(st->sig_list_obj, 1);
    lv_obj_set_flex_flow(st->sig_list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(st->sig_list_obj, 4, 0);
    lv_obj_set_style_pad_gap(st->sig_list_obj, 4, 0);
    lv_obj_set_style_bg_color(st->sig_list_obj, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(st->sig_list_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->sig_list_obj, 0, 0);
    lv_obj_set_scrollbar_mode(st->sig_list_obj, LV_SCROLLBAR_MODE_AUTO);

    /* Synchronously fetch the signal list */
    subghz_host_uart_send("subghz_list");
    subghz_collect_signal_list(st, 5000, false);

    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "%d signals available", st->sigs_count);
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }
    build_list(st);

    ESP_LOGI(TAG, "SubGHz Transmit page ready (%d signals)", st->sigs_count);
}
