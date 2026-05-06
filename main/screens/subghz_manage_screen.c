#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_signal_list.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "subghz_manage";

#define BUILD_ROWS_PER_TICK 6

static void on_back(lv_event_t *e);
static void build_list(subghz_tab_state_t *st);
static void build_list_step(lv_timer_t *t);
static void build_one_row(subghz_tab_state_t *st, int i);

static void stop_build_timer(subghz_tab_state_t *st)
{
    if (st->build_timer) {
        lv_timer_delete(st->build_timer);
        st->build_timer = NULL;
    }
}

static void on_delete_tap(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_delete %d", idx);
    subghz_host_uart_send(cmd);

    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "Deleted signal #%d", idx);
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_color_red(), 0);
    }
    ESP_LOGI(TAG, "Delete signal idx=%d", idx);

    /* Refresh list */
    subghz_host_uart_send("subghz_list");
    subghz_collect_signal_list(st, 5000, true);
    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "%d signals (unique)", st->sigs_count);
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }
    build_list(st);
}

static void build_one_row(subghz_tab_state_t *st, int i)
{
    subghz_stored_sig_t *sigs = (subghz_stored_sig_t *)st->sigs;
    subghz_stored_sig_t *sig = &sigs[i];

    lv_obj_t *row = lv_obj_create(st->sig_list_obj);
    lv_obj_set_size(row, lv_pct(100), 50);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_style_bg_color(row, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *info = lv_label_create(row);
    lv_obj_set_flex_grow(info, 1);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(info, subghz_host_ui_text(), 0);
    lv_label_set_long_mode(info, LV_LABEL_LONG_CLIP);
    lv_label_set_text_fmt(info, "%d  %s  %d.%02d MHz  %s",
                          sig->idx, sig->type,
                          (int)sig->freq, ((int)(sig->freq * 100.0f + 0.5f)) % 100,
                          sig->mf[0] ? sig->mf : sig->serial);

    lv_obj_t *del_btn = lv_btn_create(row);
    lv_obj_set_size(del_btn, 56, 40);
    lv_obj_set_style_bg_color(del_btn, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(del_btn, 6, 0);
    lv_obj_add_event_cb(del_btn, on_delete_tap, LV_EVENT_CLICKED, (void *)(intptr_t)sig->idx);

    lv_obj_t *icon = lv_label_create(del_btn);
    lv_label_set_text(icon, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_center(icon);
}

static void build_list_step(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st || !st->sig_list_obj) {
        if (st) stop_build_timer(st);
        return;
    }
    int target = st->build_idx + BUILD_ROWS_PER_TICK;
    if (target > st->sigs_count) target = st->sigs_count;
    for (; st->build_idx < target; st->build_idx++) {
        build_one_row(st, st->build_idx);
    }
    if (st->build_idx >= st->sigs_count) stop_build_timer(st);
}

static void build_list(subghz_tab_state_t *st)
{
    stop_build_timer(st);
    if (!st->sig_list_obj) return;
    lv_obj_clean(st->sig_list_obj);
    st->build_idx = 0;

    if (st->sigs_count == 0) {
        lv_obj_t *l = lv_label_create(st->sig_list_obj);
        lv_label_set_text(l, "No stored signals");
        lv_obj_set_style_text_color(l, subghz_host_ui_muted(), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        return;
    }

    /* Chunk row creation to keep LVGL responsive on long lists */
    st->build_timer = lv_timer_create(build_list_step, 30, st);
}

/* ---------- Clear All popup ---------------------------------------- */

static void close_clear_popup(subghz_tab_state_t *st)
{
    if (st->clear_popup) {
        lv_obj_delete(st->clear_popup);
        st->clear_popup = NULL;
    }
}

static void on_clear_confirmed(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    if (!st) return;
    close_clear_popup(st);
    subghz_host_uart_send("subghz_clear");

    if (st->status_lbl) {
        lv_label_set_text(st->status_lbl, "All signals cleared");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_color_red(), 0);
    }
    st->sigs_count = 0;
    build_list(st);
    ESP_LOGI(TAG, "Clear all signals");
}

static void on_clear_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    close_clear_popup(st);
}

static void on_clear_all(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st || st->clear_popup) return;

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->clear_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 460, 200);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_red());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 16, 0);
    lv_obj_set_style_pad_gap(popup, 14, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(popup);
    lv_label_set_text(l, "Delete ALL signals?");
    lv_obj_set_style_text_color(l, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_22, 0);

    lv_obj_t *brow = lv_obj_create(popup);
    lv_obj_set_size(brow, lv_pct(100), 60);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);

    lv_obj_t *yes = lv_btn_create(brow);
    lv_obj_set_size(yes, 160, 50);
    lv_obj_set_style_bg_color(yes, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(yes, 8, 0);
    lv_obj_add_event_cb(yes, on_clear_confirmed, LV_EVENT_CLICKED, st);
    l = lv_label_create(yes);
    lv_label_set_text(l, "Yes");
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_obj_center(l);

    lv_obj_t *no = lv_btn_create(brow);
    lv_obj_set_size(no, 160, 50);
    lv_obj_set_style_bg_color(no, subghz_host_ui_card(), 0);
    lv_obj_set_style_radius(no, 8, 0);
    lv_obj_add_event_cb(no, on_clear_cancel, LV_EVENT_CLICKED, st);
    l = lv_label_create(no);
    lv_label_set_text(l, "Cancel");
    lv_obj_set_style_text_color(l, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_obj_center(l);
}

/* ---------- Export / Import ---------------------------------------- */

static void on_export_all(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    subghz_host_uart_send("subghz_export all");
    if (st && st->status_lbl) {
        lv_label_set_text(st->status_lbl, "Export sent");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_color_green(), 0);
    }
    ESP_LOGI(TAG, "Export all");
}

static void on_import(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;

    if (st->status_lbl) {
        lv_label_set_text(st->status_lbl, "Importing from SD...");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_color_blue(), 0);
    }
    ESP_LOGI(TAG, "Import all from SD");

    subghz_host_uart_send("subghz_import all");

    int tab_id = subghz_host_current_tab();
    int imported = subghz_wait_for_marker(tab_id, SUBGHZ_IMPORT_END_MARKER, "[SUBGHZ_IMPORT] ", 8000);

    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "Imported %d signal%s",
                              imported, imported == 1 ? "" : "s");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_color_green(), 0);
    }

    /* Refresh list */
    subghz_host_uart_send("subghz_list");
    subghz_collect_signal_list(st, 8000, true);
    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "%d signals (unique)", st->sigs_count);
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }
    build_list(st);
}

/* ---------- Cleanup / back ----------------------------------------- */

void subghz_manage_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    stop_build_timer(st);
    if (st->clear_popup) { lv_obj_delete(st->clear_popup); st->clear_popup = NULL; }
    if (st->manage_page) { lv_obj_delete(st->manage_page); st->manage_page = NULL; }
    st->sig_list_obj = NULL;
    st->status_lbl = NULL;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_manage_cleanup(st);
    show_subghz_page();
}

/* ---------- Public entry ------------------------------------------- */

void show_subghz_manage_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->manage_page) { lv_obj_delete(st->manage_page); st->manage_page = NULL; }

    st->manage_page = lv_obj_create(container);
    lv_obj_set_size(st->manage_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->manage_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->manage_page, 0, 0);
    lv_obj_set_style_pad_all(st->manage_page, 10, 0);
    lv_obj_set_flex_flow(st->manage_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->manage_page, 8, 0);
    lv_obj_clear_flag(st->manage_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->manage_page, "Manage Signals", subghz_host_color_orange(), on_back);

    /* Action bar */
    lv_obj_t *abar = lv_obj_create(st->manage_page);
    lv_obj_set_size(abar, lv_pct(100), 60);
    lv_obj_set_flex_flow(abar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(abar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(abar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(abar, 0, 0);
    lv_obj_set_style_pad_all(abar, 4, 0);
    lv_obj_clear_flag(abar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn, *lbl;

    btn = lv_btn_create(abar);
    lv_obj_set_size(btn, 160, 50);
    lv_obj_set_style_bg_color(btn, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, on_export_all, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Export");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    btn = lv_btn_create(abar);
    lv_obj_set_size(btn, 160, 50);
    lv_obj_set_style_bg_color(btn, subghz_host_color_blue(), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, on_import, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Import");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    btn = lv_btn_create(abar);
    lv_obj_set_size(btn, 160, 50);
    lv_obj_set_style_bg_color(btn, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, on_clear_all, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Clear All");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    /* Status label */
    st->status_lbl = lv_label_create(st->manage_page);
    lv_obj_set_style_text_font(st->status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->status_lbl, "Loading...");

    /* Signal list */
    st->sig_list_obj = lv_obj_create(st->manage_page);
    lv_obj_set_size(st->sig_list_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(st->sig_list_obj, 1);
    lv_obj_set_flex_flow(st->sig_list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(st->sig_list_obj, 4, 0);
    lv_obj_set_style_pad_gap(st->sig_list_obj, 4, 0);
    lv_obj_set_style_bg_color(st->sig_list_obj, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(st->sig_list_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->sig_list_obj, 0, 0);
    lv_obj_set_scrollbar_mode(st->sig_list_obj, LV_SCROLLBAR_MODE_AUTO);

    /* Fetch the signal list */
    subghz_host_uart_send("subghz_list");
    subghz_collect_signal_list(st, 5000, true);

    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "%d signals (unique)", st->sigs_count);
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }
    build_list(st);

    ESP_LOGI(TAG, "SubGHz Manage page ready (%d signals)", st->sigs_count);
}
