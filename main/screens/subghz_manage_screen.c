/* SD Signals (Manage) screen.
 *
 * Lists signals stored on SD (subghz_list sd) and exposes per-row
 * Rename / Delete / Transmit actions via a touch popup.
 *
 * Background reader task parses [SUBGHZ_RENAME], [SUBGHZ_DELETE],
 * [SUBGHZ_TX] and their error variants and surfaces them on a status
 * label. After any mutation, we re-issue subghz_list sd (positional
 * indices change after delete/rename). */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_signal_list.h"
#include "subghz_parser.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "subghz_manage";

#define BUILD_ROWS_PER_TICK 6
#define UART_RX_BUF_LEN     512
#define LINE_BUF_LEN        512

static volatile bool s_manage_page_alive = false;

/* Reader/collector cooperation: the reader task and the synchronous bulk
 * collector (subghz_collect_signal_list) both read from the same per-tab
 * UART. Without coordination the reader steals most `[SUBGHZ_LIST]` lines
 * from the collector, so the SD list ends up almost empty. The collector
 * sets s_manage_reader_paused=true and busy-waits for s_manage_reader_idle
 * to flip true (meaning the reader has parked outside subghz_host_uart_read_bytes),
 * runs the bulk collect alone, then releases the reader. */
static volatile bool s_manage_reader_paused = false;
static volatile bool s_manage_reader_idle   = true;

static void on_back(lv_event_t *e);
static void build_list_step(lv_timer_t *t);
static void build_one_row(subghz_tab_state_t *st, int i);
static void stop_build_timer(subghz_tab_state_t *st);
static void refresh_list_from_sd(subghz_tab_state_t *st);
static void rebuild_row_list(subghz_tab_state_t *st);
static void set_status_msg(subghz_tab_state_t *st, const char *msg, lv_color_t color);
static void apply_pending_status(subghz_tab_state_t *st);
static void show_action_popup(subghz_tab_state_t *st, int idx);
static void close_action_popup(subghz_tab_state_t *st);
static void show_delete_confirm(subghz_tab_state_t *st, int idx, const char *name);
static void close_delete_popup(subghz_tab_state_t *st);
static void show_tx_count_popup(subghz_tab_state_t *st, int idx);
static void close_tx_count_popup(subghz_tab_state_t *st);
static void on_tx_count_confirm(lv_event_t *e);
static void on_tx_count_cancel(lv_event_t *e);
static void ui_tick_cb(lv_timer_t *t);
static void process_event_line(subghz_tab_state_t *st, const char *line);

/* ------------------------------------------------------------------- */
/* Cross-task status message helpers (same pattern as Listen screen)   */
/* ------------------------------------------------------------------- */

static void status_clear_timer_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;
    st->manage_status_clear_timer = NULL;
    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "%d signal%s on SD",
                              st->sigs_count, st->sigs_count == 1 ? "" : "s");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }
}

static void set_status_msg(subghz_tab_state_t *st, const char *msg, lv_color_t color)
{
    if (!st || !msg) return;
    snprintf(st->manage_status_text, sizeof(st->manage_status_text), "%s", msg);
    st->manage_status_color_argb = ((int)color.red   << 16) |
                                   ((int)color.green << 8)  |
                                    (int)color.blue;
    st->manage_status_pending = true;
}

static void apply_pending_status(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->manage_status_pending && st->status_lbl) {
        uint32_t packed = (uint32_t)st->manage_status_color_argb;
        lv_color_t color = lv_color_make((uint8_t)((packed >> 16) & 0xFF),
                                         (uint8_t)((packed >> 8)  & 0xFF),
                                         (uint8_t)( packed        & 0xFF));
        lv_label_set_text(st->status_lbl, st->manage_status_text);
        lv_obj_set_style_text_color(st->status_lbl, color, 0);
        st->manage_status_pending = false;
        if (st->manage_status_clear_timer) {
            lv_timer_delete(st->manage_status_clear_timer);
            st->manage_status_clear_timer = NULL;
        }
        st->manage_status_clear_timer = lv_timer_create(status_clear_timer_cb, 3000, st);
        lv_timer_set_repeat_count(st->manage_status_clear_timer, 1);
    }
    if (st->manage_relist_pending) {
        st->manage_relist_pending = false;
        refresh_list_from_sd(st);
    }
}

/* ------------------------------------------------------------------- */
/* Reader task (parses RENAME/DELETE/TX events into status text)       */
/* ------------------------------------------------------------------- */

static void process_event_line(subghz_tab_state_t *st, const char *line)
{
    if (!st) return;

    if (strstr(line, "[SUBGHZ_RENAME] ")) {
        int idx = 0;
        char name[64] = {0};
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        p = strstr(line, "name=");
        if (p) {
            p += 5;
            const char *end = strchr(p, ' ');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, p, len);
            name[len] = '\0';
        }
        char msg[96];
        if (name[0]) snprintf(msg, sizeof(msg), "Renamed #%d -> %s", idx, name);
        else         snprintf(msg, sizeof(msg), "Renamed #%d", idx);
        set_status_msg(st, msg, subghz_host_color_green());
        st->manage_relist_pending = true;
        return;
    }
    if (strstr(line, "[SUBGHZ_RENAME_ERR] ")) {
        const char *r = strstr(line, "reason=");
        char msg[96];
        snprintf(msg, sizeof(msg), "Rename failed: %s", r ? r + 7 : "error");
        set_status_msg(st, msg, subghz_host_color_red());
        return;
    }
    if (strstr(line, "[SUBGHZ_DELETE] ")) {
        int idx = 0;
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        char msg[64];
        snprintf(msg, sizeof(msg), "Deleted #%d", idx);
        set_status_msg(st, msg, subghz_host_color_orange());
        st->manage_relist_pending = true;
        return;
    }
    if (strstr(line, "[SUBGHZ_DELETE_ERR] ")) {
        const char *r = strstr(line, "reason=");
        char msg[96];
        snprintf(msg, sizeof(msg), "Delete failed: %s", r ? r + 7 : "error");
        set_status_msg(st, msg, subghz_host_color_red());
        return;
    }
    if (strstr(line, "[SUBGHZ_TX] ")) {
        int idx = 0;
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        if (st->manage_tx_active && st->manage_tx_waiting) {
            /* Ack for a repeat send: count it and let ui_tick fire the next. */
            st->manage_tx_done++;
            st->manage_tx_waiting = false;
            if (st->manage_tx_done >= st->manage_tx_total) {
                st->manage_tx_active = false;
                char msg[64];
                snprintf(msg, sizeof(msg), "Transmitted #%d x%d",
                         st->manage_tx_idx, st->manage_tx_done);
                set_status_msg(st, msg, subghz_host_color_green());
            }
            /* While more remain, ui_tick keeps the "Transmitting X/Y" status. */
            return;
        }
        char msg[64];
        if (idx > 0) snprintf(msg, sizeof(msg), "Transmitted #%d", idx);
        else         snprintf(msg, sizeof(msg), "Transmitted");
        set_status_msg(st, msg, subghz_host_color_green());
        return;
    }
}

static void manage_reader_task(void *arg)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)arg;
    int tab_id = st->manage_task_tab_id;

    ESP_LOGI(TAG, "SD Signals reader task started for tab %d", tab_id);

    static char rx_buf[UART_RX_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;

    while (s_manage_page_alive) {
        if (s_manage_reader_paused) {
            /* Drop any partial line we accumulated before yielding so we don't
             * splice across the collector's run. */
            line_pos = 0;
            s_manage_reader_idle = true;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        s_manage_reader_idle = false;
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1,
                                              pdMS_TO_TICKS(100));
        s_manage_reader_idle = true;
        if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        rx_buf[len] = '\0';
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    process_event_line(st, line_buf);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }
    ESP_LOGI(TAG, "SD Signals reader task ended");
    st->manage_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------- */
/* Row build (virtualization is unnecessary for SD list, build linear) */
/* ------------------------------------------------------------------- */

static void stop_build_timer(subghz_tab_state_t *st)
{
    if (st->build_timer) {
        lv_timer_delete(st->build_timer);
        st->build_timer = NULL;
    }
}

static void on_row_click(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    lv_obj_t *row = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx > 0) show_action_popup(st, idx);
}

static void build_one_row(subghz_tab_state_t *st, int i)
{
    subghz_stored_sig_t *sigs = (subghz_stored_sig_t *)st->sigs;
    if (!sigs || i < 0 || i >= st->sigs_count) return;
    subghz_stored_sig_t *sig = &sigs[i];

    lv_obj_t *row = lv_obj_create(st->sig_list_obj);
    lv_obj_set_size(row, lv_pct(100), 60);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_style_bg_color(row, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_color(row, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row, (void *)(intptr_t)sig->idx);
    lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, NULL);

    /* Index badge */
    lv_obj_t *idx_lbl = lv_label_create(row);
    lv_label_set_text_fmt(idx_lbl, "#%d", sig->idx);
    lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(idx_lbl, subghz_host_color_cyan(), 0);
    lv_obj_set_width(idx_lbl, 60);

    /* Name + sub-info column */
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, lv_pct(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_gap(col, 2, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    /* Let taps on the name/info column bubble up to the row so the whole
     * row opens the action popup, not just the badge/chevron/gaps. */
    lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *name_lbl = lv_label_create(col);
    lv_obj_set_width(name_lbl, lv_pct(100));
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(name_lbl, sig->name[0] ? sig->name : "(unnamed)");
    lv_obj_set_style_text_color(name_lbl, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18, 0);

    lv_obj_t *sub_lbl = lv_label_create(col);
    lv_obj_set_width(sub_lbl, lv_pct(100));
    lv_label_set_long_mode(sub_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(sub_lbl, "%s  %.2f MHz  %s",
                          sig->type[0] ? sig->type : "--",
                          sig->freq,
                          sig->mf[0] ? sig->mf : sig->serial);
    lv_obj_set_style_text_color(sub_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *chev = lv_label_create(row);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_18, 0);
}

static void build_list_step(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) { stop_build_timer(st); return; }

    int end = st->build_idx + BUILD_ROWS_PER_TICK;
    if (end > st->sigs_count) end = st->sigs_count;
    for (int i = st->build_idx; i < end; i++) build_one_row(st, i);
    st->build_idx = end;
    if (st->build_idx >= st->sigs_count) stop_build_timer(st);
}

static void rebuild_row_list(subghz_tab_state_t *st)
{
    if (!st || !st->sig_list_obj) return;
    stop_build_timer(st);
    lv_obj_clean(st->sig_list_obj);

    if (st->sigs_count == 0) {
        lv_obj_t *empty = lv_label_create(st->sig_list_obj);
        lv_label_set_text(empty, "No signals saved on SD");
        lv_obj_set_style_text_color(empty, subghz_host_ui_muted(), 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_18, 0);
        lv_obj_center(empty);
        return;
    }
    st->build_idx = 0;
    st->build_timer = lv_timer_create(build_list_step, 16, st);
}

static void refresh_list_from_sd(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->status_lbl) {
        lv_label_set_text(st->status_lbl, "Loading SD signals...");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }

    /* Park the reader task so it doesn't steal `[SUBGHZ_LIST]` lines from the
     * synchronous collector. Wait up to ~250 ms for it to park outside the
     * UART read call before flushing input and issuing the command. */
    bool had_reader = (st->manage_task != NULL);
    if (had_reader) {
        s_manage_reader_paused = true;
        TickType_t wait_start = xTaskGetTickCount();
        while (!s_manage_reader_idle &&
               (xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(250)) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    subghz_collect_signal_list(st, "sd", 4000, true);

    if (had_reader) {
        s_manage_reader_paused = false;
    }

    if (st->status_lbl) {
        lv_label_set_text_fmt(st->status_lbl, "%d signal%s on SD",
                              st->sigs_count, st->sigs_count == 1 ? "" : "s");
        lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    }
    rebuild_row_list(st);
}

/* ------------------------------------------------------------------- */
/* Action popup (Rename / Delete / Transmit / Cancel)                  */
/* ------------------------------------------------------------------- */

static void close_action_popup(subghz_tab_state_t *st)
{
    if (st && st->manage_action_popup) {
        lv_obj_delete(st->manage_action_popup);
        st->manage_action_popup = NULL;
    }
}

static void close_delete_popup(subghz_tab_state_t *st)
{
    if (st && st->manage_delete_popup) {
        lv_obj_delete(st->manage_delete_popup);
        st->manage_delete_popup = NULL;
    }
}

static bool find_sig_by_idx(subghz_tab_state_t *st, int idx, subghz_stored_sig_t *out)
{
    if (!st || !out) return false;
    subghz_stored_sig_t *arr = (subghz_stored_sig_t *)st->sigs;
    for (int i = 0; i < st->sigs_count; i++) {
        if (arr[i].idx == idx) { *out = arr[i]; return true; }
    }
    return false;
}

static void on_rename_text_confirm(const char *text, void *user_data)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)user_data;
    if (!st || !text || !text[0]) return;
    /* Build "subghz_rename <idx> <new_name>" -- spaces in names are not
     * supported by the CLI today, so we strip them. */
    char clean[64];
    size_t j = 0;
    for (size_t i = 0; text[i] && j < sizeof(clean) - 1; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t') c = '_';
        clean[j++] = c;
    }
    clean[j] = '\0';
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "subghz_rename %d %s",
             st->manage_pending_action_idx, clean);
    subghz_host_uart_send(cmd);
    set_status_msg(st, "Renaming...", subghz_host_color_blue());
}

static void on_rename_text_cancel(void *user_data)
{
    (void)user_data;
}

static void on_action_rename(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->manage_pending_action_idx;
    char initial[64];
    snprintf(initial, sizeof(initial), "%s", st->manage_pending_action_name);
    close_action_popup(st);
    if (idx <= 0) return;
    subghz_show_text_input_popup("Rename signal", initial, 32,
                                 subghz_host_color_cyan(),
                                 on_rename_text_confirm,
                                 on_rename_text_cancel,
                                 st);
    (void)e;
}

static void on_delete_confirmed(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->manage_pending_action_idx;
    close_delete_popup(st);
    if (idx <= 0) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_delete %d", idx);
    subghz_host_uart_send(cmd);
    char msg[64];
    snprintf(msg, sizeof(msg), "Deleting #%d...", idx);
    set_status_msg(st, msg, subghz_host_color_blue());
    (void)e;
}

static void on_delete_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (st) close_delete_popup(st);
    (void)e;
}

static void show_delete_confirm(subghz_tab_state_t *st, int idx, const char *name)
{
    close_delete_popup(st);
    st->manage_pending_action_idx = idx;

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->manage_delete_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 540, 240);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_red());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 18, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text_fmt(title, "Delete #%d?", idx);
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *body = lv_label_create(popup);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(body, "%s", name && name[0] ? name : "(unnamed)");
    lv_obj_set_style_text_color(body, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *brow = lv_obj_create(popup);
    lv_obj_set_size(brow, lv_pct(100), 60);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *del = lv_btn_create(brow);
    lv_obj_set_size(del, 200, 50);
    lv_obj_set_style_bg_color(del, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(del, 8, 0);
    lv_obj_add_event_cb(del, on_delete_confirmed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(del);
    lv_label_set_text(dl, "Delete");
    lv_obj_set_style_text_color(dl, lv_color_white(), 0);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_18, 0);
    lv_obj_center(dl);

    lv_obj_t *can = lv_btn_create(brow);
    lv_obj_set_size(can, 200, 50);
    lv_obj_set_style_bg_color(can, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(can, 8, 0);
    lv_obj_add_event_cb(can, on_delete_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(can);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);
}

static void on_action_delete(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->manage_pending_action_idx;
    char name[64];
    snprintf(name, sizeof(name), "%s", st->manage_pending_action_name);
    close_action_popup(st);
    if (idx <= 0) return;
    show_delete_confirm(st, idx, name);
    (void)e;
}

static void on_action_transmit(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->manage_pending_action_idx;
    close_action_popup(st);
    if (idx <= 0) return;
    /* Ask how many times to replay the signal. */
    show_tx_count_popup(st, idx);
    (void)e;
}

static void on_action_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (st) close_action_popup(st);
    (void)e;
}

/* ------------------------------------------------------------------- */
/* Repeated transmit: "how many times?" roller popup + event-driven loop */
/* ------------------------------------------------------------------- */

static const char *s_tx_digit_opts = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

static void close_tx_count_popup(subghz_tab_state_t *st)
{
    if (st && st->manage_tx_count_popup) {
        lv_obj_delete(st->manage_tx_count_popup);
        st->manage_tx_count_popup = NULL;
    }
    if (st) {
        for (int i = 0; i < 3; i++) st->manage_tx_rollers[i] = NULL;
    }
}

static void on_tx_count_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (st) close_tx_count_popup(st);
    (void)e;
}

static void on_tx_count_confirm(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->manage_pending_action_idx;
    if (idx <= 0) { close_tx_count_popup(st); return; }

    int h = st->manage_tx_rollers[0] ? (int)lv_roller_get_selected(st->manage_tx_rollers[0]) : 0;
    int t = st->manage_tx_rollers[1] ? (int)lv_roller_get_selected(st->manage_tx_rollers[1]) : 0;
    int o = st->manage_tx_rollers[2] ? (int)lv_roller_get_selected(st->manage_tx_rollers[2]) : 0;
    int count = h * 100 + t * 10 + o;
    if (count < 1) count = 1;

    close_tx_count_popup(st);

    /* Start the repeat. ui_tick_cb issues the actual sends so all UART
     * writes stay on the LVGL thread, matching the rest of this screen. */
    st->manage_tx_idx     = idx;
    st->manage_tx_total   = count;
    st->manage_tx_done    = 0;
    st->manage_tx_waiting = false;
    st->manage_tx_active  = true;
    (void)e;
}

static void style_tx_roller(lv_obj_t *r)
{
    lv_obj_set_width(r, 70);
    lv_obj_set_style_bg_color(r, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(r, subghz_host_color_cyan(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, subghz_host_ui_bg(), LV_PART_SELECTED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 8, 0);
}

static void show_tx_count_popup(subghz_tab_state_t *st, int idx)
{
    if (!st || idx <= 0) return;
    close_tx_count_popup(st);

    st->manage_pending_action_idx = idx;

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->manage_tx_count_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 540, 360);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_orange());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 18, 0);
    lv_obj_set_style_pad_gap(popup, 16, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text_fmt(title, "Transmit #%d - how many times?", idx);
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *roller_row = lv_obj_create(popup);
    lv_obj_set_size(roller_row, lv_pct(100), 150);
    lv_obj_set_flex_flow(roller_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(roller_row, 0, 0);
    lv_obj_set_style_pad_gap(roller_row, 8, 0);
    lv_obj_set_style_bg_opa(roller_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(roller_row, 0, 0);
    lv_obj_clear_flag(roller_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 3; i++) {
        st->manage_tx_rollers[i] = lv_roller_create(roller_row);
        lv_roller_set_options(st->manage_tx_rollers[i], s_tx_digit_opts,
                              LV_ROLLER_MODE_INFINITE);
        lv_roller_set_visible_row_count(st->manage_tx_rollers[i], 3);
        style_tx_roller(st->manage_tx_rollers[i]);
    }
    /* default to 1 (ones digit) */
    lv_roller_set_selected(st->manage_tx_rollers[2], 1, LV_ANIM_OFF);

    lv_obj_t *brow = lv_obj_create(popup);
    lv_obj_set_size(brow, lv_pct(100), 56);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tx = lv_btn_create(brow);
    lv_obj_set_size(tx, 220, 50);
    lv_obj_set_style_bg_color(tx, subghz_host_color_orange(), 0);
    lv_obj_set_style_radius(tx, 8, 0);
    lv_obj_add_event_cb(tx, on_tx_count_confirm, LV_EVENT_CLICKED, NULL);
    lv_obj_t *txl = lv_label_create(tx);
    lv_label_set_text(txl, "Transmit");
    lv_obj_set_style_text_color(txl, lv_color_white(), 0);
    lv_obj_set_style_text_font(txl, &lv_font_montserrat_18, 0);
    lv_obj_center(txl);

    lv_obj_t *cn = lv_btn_create(brow);
    lv_obj_set_size(cn, 220, 50);
    lv_obj_set_style_bg_color(cn, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(cn, 8, 0);
    lv_obj_add_event_cb(cn, on_tx_count_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cnl = lv_label_create(cn);
    lv_label_set_text(cnl, "Cancel");
    lv_obj_set_style_text_color(cnl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cnl, &lv_font_montserrat_18, 0);
    lv_obj_center(cnl);
}

static void show_action_popup(subghz_tab_state_t *st, int idx)
{
    if (!st || idx <= 0) return;
    close_action_popup(st);
    st->manage_pending_action_idx = idx;
    subghz_stored_sig_t s;
    if (find_sig_by_idx(st, idx, &s))
        snprintf(st->manage_pending_action_name,
                 sizeof(st->manage_pending_action_name), "%s", s.name);
    else
        st->manage_pending_action_name[0] = '\0';

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->manage_action_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 580, 320);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_orange());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 16, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text_fmt(title, "Signal #%d", idx);
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *name_lbl = lv_label_create(popup);
    lv_obj_set_width(name_lbl, lv_pct(100));
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(name_lbl, "name: %s",
                          st->manage_pending_action_name[0]
                            ? st->manage_pending_action_name : "(unnamed)");
    lv_obj_set_style_text_color(name_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *brow = lv_obj_create(popup);
    lv_obj_set_size(brow, lv_pct(100), 120);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    struct {
        const char    *label;
        lv_color_t     bg;
        lv_event_cb_t  cb;
    } btns[] = {
        { "Transmit", subghz_host_color_orange(), on_action_transmit },
        { "Rename",   subghz_host_color_cyan(),   on_action_rename   },
        { "Delete",   subghz_host_color_red(),    on_action_delete   },
        { "Cancel",   subghz_host_ui_muted(),     on_action_cancel   },
    };
    /* First row: Transmit + Rename, second row: Delete + Cancel */
    lv_obj_t *row1 = lv_obj_create(brow);
    lv_obj_set_size(row1, lv_pct(100), 50);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row2 = lv_obj_create(brow);
    lv_obj_set_size(row2, lv_pct(100), 50);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *parent = (i < 2) ? row1 : row2;
        lv_obj_t *b = lv_btn_create(parent);
        lv_obj_set_size(b, 200, 46);
        lv_obj_set_style_bg_color(b, btns[i].bg, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_add_event_cb(b, btns[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, btns[i].label);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
        lv_obj_center(l);
    }
}

/* ------------------------------------------------------------------- */
/* UI tick — drains pending status updates / relist requests           */
/* ------------------------------------------------------------------- */

static void ui_tick_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;
    apply_pending_status(st);

    /* Drive the repeated-transmit loop. Sends happen here (LVGL thread); the
     * reader task only flips manage_tx_waiting when a [SUBGHZ_TX] ack lands. */
    if (st->manage_tx_active) {
        if (st->manage_tx_waiting) {
            /* Watchdog: if no ack within 4 s, stop waiting so the loop can
             * advance instead of hanging on a dropped [SUBGHZ_TX]. */
            if ((int32_t)(lv_tick_get() - st->manage_tx_deadline_ms) >= 0)
                st->manage_tx_waiting = false;
        } else if (st->manage_tx_done < st->manage_tx_total) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "subghz_tx %d sd", st->manage_tx_idx);
            subghz_host_uart_send(cmd);
            st->manage_tx_waiting     = true;
            st->manage_tx_deadline_ms = lv_tick_get() + 4000;
            char msg[64];
            snprintf(msg, sizeof(msg), "Transmitting %d/%d (#%d)...",
                     st->manage_tx_done + 1, st->manage_tx_total, st->manage_tx_idx);
            set_status_msg(st, msg, subghz_host_color_orange());
        }
    }
}

/* ------------------------------------------------------------------- */
/* Cleanup + back                                                       */
/* ------------------------------------------------------------------- */

void subghz_manage_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    s_manage_page_alive = false;
    for (int i = 0; i < 25 && st->manage_task; i++) vTaskDelay(pdMS_TO_TICKS(20));

    stop_build_timer(st);
    if (st->manage_status_clear_timer) {
        lv_timer_delete(st->manage_status_clear_timer);
        st->manage_status_clear_timer = NULL;
    }
    if (st->manage_ui_timer) {
        lv_timer_delete(st->manage_ui_timer);
        st->manage_ui_timer = NULL;
    }
    /* Stop any in-flight repeated transmit and tear down its popup. */
    st->manage_tx_active  = false;
    st->manage_tx_waiting = false;
    close_action_popup(st);
    close_delete_popup(st);
    close_tx_count_popup(st);
    subghz_signal_list_free(st);
    if (st->manage_page) { lv_obj_delete(st->manage_page); st->manage_page = NULL; }
    st->sig_list_obj = NULL;
    st->status_lbl = NULL;
    st->manage_status_pending = false;
    st->manage_relist_pending = false;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_manage_cleanup(st);
    show_subghz_page();
}

/* ------------------------------------------------------------------- */
/* Public entry                                                         */
/* ------------------------------------------------------------------- */

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

    subghz_create_header(st->manage_page, "SD Signals",
                         subghz_host_color_orange(), on_back);

    /* Status line under header */
    st->status_lbl = lv_label_create(st->manage_page);
    lv_obj_set_width(st->status_lbl, lv_pct(100));
    lv_label_set_text(st->status_lbl, "Loading SD signals...");
    lv_obj_set_style_text_color(st->status_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(st->status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(st->status_lbl, LV_TEXT_ALIGN_LEFT, 0);

    /* Scrollable list of rows */
    st->sig_list_obj = lv_obj_create(st->manage_page);
    lv_obj_set_size(st->sig_list_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(st->sig_list_obj, 1);
    lv_obj_set_style_pad_all(st->sig_list_obj, 4, 0);
    lv_obj_set_style_pad_row(st->sig_list_obj, 6, 0);
    lv_obj_set_style_bg_color(st->sig_list_obj, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(st->sig_list_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->sig_list_obj, 0, 0);
    lv_obj_set_flex_flow(st->sig_list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(st->sig_list_obj, LV_SCROLLBAR_MODE_AUTO);

    /* Spawn reader task for the page lifetime */
    st->manage_task_tab_id = subghz_host_current_tab();
    s_manage_page_alive = true;
    if (!st->manage_task)
        xTaskCreate(manage_reader_task, "sg_manage", 4096, st, 5, &st->manage_task);

    /* UI ticker for status + relist application */
    st->manage_status_clear_timer = NULL;
    if (st->manage_ui_timer) { lv_timer_delete(st->manage_ui_timer); }
    st->manage_ui_timer = lv_timer_create(ui_tick_cb, 200, st);

    refresh_list_from_sd(st);

    ESP_LOGI(TAG, "SD Signals page ready");
}
