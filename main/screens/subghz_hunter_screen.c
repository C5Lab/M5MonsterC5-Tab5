/* Hunter screen — Frequency Analyzer + auto-capture.
 *
 * Sends `subghz_freq_analyzer ... hunt ...` built from NVS RF settings,
 * runs a per-tab background reader task that parses [SUBGHZ_FA*],
 * [SUBGHZ_RX], [SUBGHZ_RX_DUP], [SUBGHZ_RAW], [SUBGHZ_SAVE], [SUBGHZ_TX]
 * into a status spinner + capture list with row-tap Save/Transmit.
 *
 * Fresh entry (show_subghz_hunter_page) wipes captures and starts hunt;
 * resume entry (show_subghz_hunter_page_resume) preserves captures and
 * also restarts hunt. */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_parser.h"
#include "subghz_rf_settings.h"
#include "psram_dynarr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static const char *TAG = "subghz_hunter";

#define UART_RX_BUF_LEN  512
#define LINE_BUF_LEN     512
#define ROW_HEIGHT       42
#define MAX_VIRT_HEIGHT  28000
#define HUNTER_HARD_CAP  2048

/* One captured signal in Hunter's per-tab history. Stored in PSRAM dynarr. */
typedef struct {
    int   idx;
    char  type[32];
    float freq;
    char  serial[32];
    int   btn;
    int   cnt;
    char  mf[32];
    bool  is_raw;
    bool  is_duplicate;
} hunter_sig_t;

/* PSRAM dynarr backing — owned by the page (freed in cleanup). One per tab. */
typedef struct {
    hunter_sig_t *items;
    int           cap;
    int           count;
} hunter_history_t;

/* signal_row_view_t — declared in subghz_host.h as forward */
struct signal_row_view {
    lv_obj_t *row;
    lv_obj_t *idx;
    lv_obj_t *info;
};

static volatile bool s_hunter_page_alive = false;
static portMUX_TYPE  s_hunter_lock = portMUX_INITIALIZER_UNLOCKED;
static hunter_history_t s_hist; /* singleton across tabs - simple */

static void on_back(lv_event_t *e);
static void on_settings(lv_event_t *e);
static void on_stop(lv_event_t *e);
static void hunter_start_uart(subghz_tab_state_t *st);
static void process_event_line(subghz_tab_state_t *st, const char *line);
static void hunter_reader_task(void *arg);
static void ui_tick_cb(lv_timer_t *t);
static void show_action_popup(subghz_tab_state_t *st, int idx);
static void close_action_popup(subghz_tab_state_t *st);
static void show_leave_popup(subghz_tab_state_t *st, int count);
static void close_leave_popup(subghz_tab_state_t *st);
static void perform_back(subghz_tab_state_t *st);
static void rebuild_rows(subghz_tab_state_t *st);
static void set_status(subghz_tab_state_t *st, int kind, const char *fmt, ...);
static void apply_status(subghz_tab_state_t *st);
static void clear_history(subghz_tab_state_t *st);

/* --------------------------------------------------------------------- */
/* Status helpers                                                         */
/* --------------------------------------------------------------------- */

static void set_status(subghz_tab_state_t *st, int kind, const char *fmt, ...)
{
    if (!st) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->hunter_status_text, sizeof(st->hunter_status_text), fmt, ap);
    va_end(ap);
    st->hunter_status_kind = kind;
    st->hunter_status_dirty = true;
}

static lv_color_t status_color(int kind)
{
    switch (kind) {
    case 1:  return subghz_host_color_orange();   /* capture */
    case 2:  return subghz_host_color_green();    /* ok */
    case 3:  return subghz_host_color_red();      /* err */
    case 5:  return subghz_host_color_cyan();     /* dup */
    case 6:  return subghz_host_color_pink();     /* scan */
    default: return subghz_host_ui_muted();       /* idle/stopped/timeout */
    }
}

static void apply_status(subghz_tab_state_t *st)
{
    if (!st || !st->hunter_status_dirty || !st->hunter_status_lbl) return;
    st->hunter_status_dirty = false;
    lv_label_set_text(st->hunter_status_lbl, st->hunter_status_text);
    lv_obj_set_style_text_color(st->hunter_status_lbl,
                                status_color(st->hunter_status_kind), 0);
}

/* --------------------------------------------------------------------- */
/* History store (per-tab via st->signal_count repurposed?  NO - use a   */
/* dedicated dynarr in PSRAM, allocated lazily.)                         */
/* --------------------------------------------------------------------- */

static int history_count(subghz_tab_state_t *st)
{
    (void)st;
    int c;
    portENTER_CRITICAL(&s_hunter_lock);
    c = s_hist.count;
    portEXIT_CRITICAL(&s_hunter_lock);
    return c;
}

static bool history_find_by_idx(int idx, hunter_sig_t *out)
{
    bool found = false;
    portENTER_CRITICAL(&s_hunter_lock);
    for (int i = 0; i < s_hist.count; i++) {
        if (s_hist.items[i].idx == idx) {
            *out = s_hist.items[i];
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_hunter_lock);
    return found;
}

static bool merge_duplicate(const subghz_signal_info_t *src)
{
    bool merged = false;
    portENTER_CRITICAL(&s_hunter_lock);
    if (s_hist.count > 0) {
        hunter_sig_t *last = &s_hist.items[s_hist.count - 1];
        if (src->kind != SUBGHZ_SIGNAL_KIND_RAW && last->idx == src->idx) {
            if (src->cnt > last->cnt) last->cnt = src->cnt;
            merged = true;
        }
    }
    portEXIT_CRITICAL(&s_hunter_lock);
    return merged;
}

static void append_signal(const subghz_signal_info_t *src)
{
    portENTER_CRITICAL(&s_hunter_lock);
    /* Use psram_dynarr ensure outside of critical (allocation) - so split */
    portEXIT_CRITICAL(&s_hunter_lock);
    if (!psram_dynarr_ensure((void **)&s_hist.items, &s_hist.cap,
                             s_hist.count + 1, sizeof(hunter_sig_t),
                             HUNTER_HARD_CAP)) {
        ESP_LOGW(TAG, "PSRAM cap reached at %d", s_hist.count);
        return;
    }
    portENTER_CRITICAL(&s_hunter_lock);
    hunter_sig_t *dst = &s_hist.items[s_hist.count];
    memset(dst, 0, sizeof(*dst));
    dst->idx  = src->idx;
    dst->freq = src->freq;
    dst->btn  = src->btn;
    dst->cnt  = src->cnt;
    dst->is_raw = src->is_raw;
    dst->is_duplicate = src->is_duplicate;
    snprintf(dst->type,   sizeof(dst->type),   "%s", src->type[0]   ? src->type   : "--");
    snprintf(dst->serial, sizeof(dst->serial), "%s", src->serial[0] ? src->serial : "--");
    snprintf(dst->mf,     sizeof(dst->mf),     "%s", src->mf[0]     ? src->mf     : "--");
    s_hist.count++;
    portEXIT_CRITICAL(&s_hunter_lock);
}

static void clear_history(subghz_tab_state_t *st)
{
    (void)st;
    portENTER_CRITICAL(&s_hunter_lock);
    s_hist.count = 0;
    portEXIT_CRITICAL(&s_hunter_lock);
    psram_dynarr_free((void **)&s_hist.items, &s_hist.cap);
}

/* --------------------------------------------------------------------- */
/* Event parsing                                                          */
/* --------------------------------------------------------------------- */

static void parse_fa_status_line(subghz_tab_state_t *st, const char *line)
{
    if (strstr(line, "[SUBGHZ_FA] hunt capture")) {
        set_status(st, 1, "Capturing...");
        return;
    }
    if (strstr(line, "[SUBGHZ_FA] hunt timeout")) {
        set_status(st, 0, "Timeout");
        return;
    }
    if (strstr(line, "[SUBGHZ_FA] hunt duplicate")) {
        set_status(st, 5, "Duplicate");
        return;
    }
    if (strstr(line, "[SUBGHZ_FA] hunt error")) {
        set_status(st, 3, "Capture error");
        return;
    }
    if (strstr(line, "[SUBGHZ_FA] silent")) {
        set_status(st, 0, "Idle (no signal)");
        return;
    }
    if (strstr(line, "[SUBGHZ_FA] freq=")) {
        float freq = 0.0f;
        int   rssi = 0;
        char  stage[8] = {0};
        const char *p = strstr(line, "[SUBGHZ_FA]");
        if (p && sscanf(p, "[SUBGHZ_FA] freq=%f rssi=%d stage=%7s",
                        &freq, &rssi, stage) >= 2) {
            set_status(st, 6, "Scan %.2f @ %d dBm", freq, rssi);
        }
        return;
    }
    if (strstr(line, "[SUBGHZ_FA_START]")) {
        set_status(st, 6, "Hunting...");
        return;
    }
}

static void process_event_line(subghz_tab_state_t *st, const char *line)
{
    if (!st || !line) return;

    if (strstr(line, "[SUBGHZ_SAVE] ")) {
        int idx = 0;
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx > 0) set_status(st, 2, "#%d saved to SD", idx);
        else         set_status(st, 2, "Saved to SD");
        return;
    }
    if (strstr(line, "[SUBGHZ_SAVE_ERR] ")) {
        const char *p = strstr(line, "reason=");
        set_status(st, 3, "Save failed: %s", p ? p + 7 : "error");
        return;
    }
    if (strstr(line, "[SUBGHZ_TX] ")) {
        int idx = 0;
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        if (idx > 0) set_status(st, 2, "Transmitted #%d", idx);
        else         set_status(st, 2, "Transmitted");
        return;
    }

    if (!st->hunter_running) return;

    if (strstr(line, "[SUBGHZ_FA")) { parse_fa_status_line(st, line); return; }

    int rssi_unused;
    if (subghz_parse_rssi_line(line, &rssi_unused)) return;

    subghz_signal_info_t parsed;
    if (!subghz_parse_signal_line(line, &parsed)) return;
    if (parsed.kind == SUBGHZ_SIGNAL_KIND_LIST) return;

    if (merge_duplicate(&parsed)) return;
    append_signal(&parsed);
    st->hunter_status_kind = 1;
    /* Update count via dirty flag — UI tick will repaint */
    st->hunter_status_dirty = true;
}

static void hunter_reader_task(void *arg)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)arg;
    int tab_id = st->hunter_task_tab_id;

    static char rx_buf[UART_RX_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;

    ESP_LOGI(TAG, "Hunter reader task started for tab %d", tab_id);
    while (s_hunter_page_alive) {
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1,
                                              pdMS_TO_TICKS(100));
        if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        rx_buf[len] = '\0';
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    subghz_note_radio_line(st, line_buf);
                    process_event_line(st, line_buf);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }
    ESP_LOGI(TAG, "Hunter reader task ended");
    st->hunter_task = NULL;
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------------- */
/* UART control                                                           */
/* --------------------------------------------------------------------- */

static void hunter_start_uart(subghz_tab_state_t *st)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    char cmd[96];
    subghz_rf_build_hunter_cmd(&cfg, cmd, sizeof(cmd));
    if (cmd[0] == '\0')
        snprintf(cmd, sizeof(cmd), "subghz_freq_analyzer -70 hunt timeout=2000");

    subghz_host_uart_flush_input(subghz_host_current_tab());
    st->hunter_running = true;
    subghz_host_uart_send(cmd);
    set_status(st, 6, "Hunting...");
    ESP_LOGI(TAG, "Hunter UART: %s", cmd);
}

static void stop_hunting(subghz_tab_state_t *st)
{
    if (!st || !st->hunter_running) return;
    st->hunter_running = false;
    subghz_host_uart_send("subghz_stop");
    set_status(st, 0, "Stopped");
    if (st->hunter_spinner) lv_obj_add_flag(st->hunter_spinner, LV_OBJ_FLAG_HIDDEN);
    if (st->hunter_btn_stop) {
        lv_obj_add_state(st->hunter_btn_stop, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(st->hunter_btn_stop, subghz_host_ui_muted(), 0);
    }
    ESP_LOGI(TAG, "Hunter stopped");
}

static void on_stop(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (st) stop_hunting(st);
}

/* --------------------------------------------------------------------- */
/* Row list                                                                */
/* --------------------------------------------------------------------- */

static void on_row_click(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    lv_obj_t *row = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx > 0) show_action_popup(st, idx);
}

static void rebuild_rows(subghz_tab_state_t *st)
{
    if (!st || !st->hunter_sig_list) return;
    lv_obj_clean(st->hunter_sig_list);
    /* lv_obj_clean() just freed every child of hunter_sig_list, including
     * any previously-stored empty label. Null the cached pointer so we don't
     * dereference freed memory below. */
    st->hunter_empty_lbl = NULL;

    int n = history_count(st);
    if (n == 0) {
        st->hunter_empty_lbl = lv_label_create(st->hunter_sig_list);
        lv_label_set_text(st->hunter_empty_lbl, "No signals captured yet");
        lv_obj_set_style_text_color(st->hunter_empty_lbl, subghz_host_ui_muted(), 0);
        lv_obj_set_style_text_font(st->hunter_empty_lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(st->hunter_empty_lbl);
    } else {
        /* Snapshot the array under lock first to avoid creating LVGL
         * objects while holding portMUX. */
        hunter_sig_t *snap = (hunter_sig_t *)heap_caps_malloc(
            sizeof(hunter_sig_t) * (size_t)n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!snap) snap = (hunter_sig_t *)malloc(sizeof(hunter_sig_t) * (size_t)n);
        int snap_n = 0;
        if (snap) {
            portENTER_CRITICAL(&s_hunter_lock);
            int total = s_hist.count < n ? s_hist.count : n;
            memcpy(snap, s_hist.items, sizeof(hunter_sig_t) * (size_t)total);
            snap_n = total;
            portEXIT_CRITICAL(&s_hunter_lock);
        }
        for (int i = 0; i < snap_n; i++) {
            hunter_sig_t s = snap[i];
            lv_obj_t *row = lv_obj_create(st->hunter_sig_list);
            lv_obj_set_size(row, lv_pct(100), ROW_HEIGHT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(row, 6, 0);
            lv_obj_set_style_pad_gap(row, 10, 0);
            lv_obj_set_style_bg_color(row, subghz_host_ui_card(), 0);
            lv_obj_set_style_bg_color(row, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(row, (void *)(intptr_t)s.idx);
            lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, NULL);

            lv_obj_t *idx_lbl = lv_label_create(row);
            lv_label_set_text_fmt(idx_lbl, "#%d", s.idx);
            lv_obj_set_width(idx_lbl, 60);
            lv_obj_set_style_text_color(idx_lbl, subghz_host_color_cyan(), 0);
            lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_18, 0);

            lv_obj_t *info = lv_label_create(row);
            lv_obj_set_flex_grow(info, 1);
            lv_label_set_long_mode(info, LV_LABEL_LONG_DOT);
            lv_label_set_text_fmt(info, "%s  %.2f MHz  %s",
                                  s.type, s.freq,
                                  s.mf[0] && strcmp(s.mf, "--") ? s.mf : s.serial);
            lv_obj_set_style_text_color(info, subghz_host_ui_text(), 0);
            lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
        }
        if (snap) free(snap);
    }
    if (st->hunter_capt_count_lbl)
        lv_label_set_text_fmt(st->hunter_capt_count_lbl, "Captures: %d", n);
}

static void ui_tick_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;
    if (st->radio_status_dirty) {
        st->radio_status_dirty = false;
        subghz_refresh_radio_badge(st);
    }
    static int s_last_count = -1;
    int n = history_count(st);
    if (n != s_last_count) {
        s_last_count = n;
        rebuild_rows(st);
    }
    apply_status(st);
}

/* --------------------------------------------------------------------- */
/* Action popup (Save / Transmit / Cancel)                                */
/* --------------------------------------------------------------------- */

static void close_action_popup(subghz_tab_state_t *st)
{
    if (st && st->hunter_action_popup) {
        lv_obj_delete(st->hunter_action_popup);
        st->hunter_action_popup = NULL;
    }
}

static void on_action_save(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->hunter_pending_action_idx;
    close_action_popup(st);
    if (idx <= 0) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_save %d", idx);
    subghz_host_uart_send(cmd);
    set_status(st, 1, "Saving #%d...", idx);
    (void)e;
}

static void on_action_tx(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->hunter_pending_action_idx;
    close_action_popup(st);
    if (idx <= 0) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_tx %d mem", idx);
    subghz_host_uart_send(cmd);
    set_status(st, 1, "Transmitting #%d...", idx);
    (void)e;
}

static void on_action_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (st) close_action_popup(st);
    (void)e;
}

static void show_action_popup(subghz_tab_state_t *st, int idx)
{
    close_action_popup(st);
    st->hunter_pending_action_idx = idx;
    hunter_sig_t s;
    bool found = history_find_by_idx(idx, &s);

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->hunter_action_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 540, 260);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_pink());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 18, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text_fmt(title, "Capture #%d (%s)", idx,
                          found && s.type[0] ? s.type : "--");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *brow = lv_obj_create(popup);
    lv_obj_set_size(brow, lv_pct(100), 60);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    struct { const char *l; lv_color_t c; lv_event_cb_t cb; } bs[] = {
        { "Save", subghz_host_color_green(),  on_action_save },
        { "Transmit", subghz_host_color_orange(), on_action_tx },
        { "Cancel", subghz_host_ui_muted(),   on_action_cancel },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(brow);
        lv_obj_set_size(b, 150, 50);
        lv_obj_set_style_bg_color(b, bs[i].c, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_add_event_cb(b, bs[i].cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lab = lv_label_create(b);
        lv_label_set_text(lab, bs[i].l);
        lv_obj_set_style_text_color(lab, lv_color_white(), 0);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_18, 0);
        lv_obj_center(lab);
    }
}

/* --------------------------------------------------------------------- */
/* Leave popup                                                             */
/* --------------------------------------------------------------------- */

static void close_leave_popup(subghz_tab_state_t *st)
{
    if (st && st->hunter_leave_popup) {
        lv_obj_delete(st->hunter_leave_popup);
        st->hunter_leave_popup = NULL;
    }
}

static void on_leave_confirm(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    close_leave_popup(st);
    perform_back(st);
    (void)e;
}

static void on_leave_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (st) close_leave_popup(st);
    (void)e;
}

static void show_leave_popup(subghz_tab_state_t *st, int count)
{
    close_leave_popup(st);
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->hunter_leave_popup = overlay;

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
    lv_label_set_text(title, "Leave Hunter?");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_t *body = lv_label_create(popup);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(body,
        "%d unsaved capture%s will be cleared. Save to SD first?",
        count, count == 1 ? "" : "s");
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

    lv_obj_t *leave = lv_btn_create(brow);
    lv_obj_set_size(leave, 200, 50);
    lv_obj_set_style_bg_color(leave, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(leave, 8, 0);
    lv_obj_add_event_cb(leave, on_leave_confirm, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ll = lv_label_create(leave);
    lv_label_set_text(ll, "Leave");
    lv_obj_set_style_text_color(ll, lv_color_white(), 0);
    lv_obj_set_style_text_font(ll, &lv_font_montserrat_18, 0);
    lv_obj_center(ll);

    lv_obj_t *stay = lv_btn_create(brow);
    lv_obj_set_size(stay, 200, 50);
    lv_obj_set_style_bg_color(stay, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(stay, 8, 0);
    lv_obj_add_event_cb(stay, on_leave_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(stay);
    lv_label_set_text(sl, "Stay");
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_18, 0);
    lv_obj_center(sl);
}

/* --------------------------------------------------------------------- */
/* Cleanup + back                                                          */
/* --------------------------------------------------------------------- */

void subghz_hunter_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    stop_hunting(st);
    s_hunter_page_alive = false;
    for (int i = 0; i < 25 && st->hunter_task; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (st->hunter_ui_timer) { lv_timer_delete(st->hunter_ui_timer); st->hunter_ui_timer = NULL; }
    close_action_popup(st);
    close_leave_popup(st);
    if (st->hunter_page) { lv_obj_delete(st->hunter_page); st->hunter_page = NULL; }
    st->hunter_spinner = NULL;
    st->hunter_status_lbl = NULL;
    st->hunter_btn_stop = NULL;
    st->hunter_capt_count_lbl = NULL;
    st->hunter_sig_list = NULL;
    st->hunter_sig_spacer = NULL;
    st->hunter_empty_lbl = NULL;
}

static void perform_back(subghz_tab_state_t *st)
{
    subghz_hunter_cleanup(st);
    clear_history(st);
    show_subghz_page();
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int count = history_count(st);
    if (count > 0 && !st->hunter_leave_popup) {
        show_leave_popup(st, count);
        return;
    }
    perform_back(st);
}

static void on_settings(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_hunter_cleanup(st);
    show_subghz_hunter_settings_page();
}

/* --------------------------------------------------------------------- */
/* Public entries                                                          */
/* --------------------------------------------------------------------- */

static void build_page_and_start(bool resume)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->hunter_page) { lv_obj_delete(st->hunter_page); st->hunter_page = NULL; }

    if (!resume) clear_history(st);

    st->hunter_page = lv_obj_create(container);
    lv_obj_set_size(st->hunter_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->hunter_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->hunter_page, 0, 0);
    lv_obj_set_style_pad_all(st->hunter_page, 10, 0);
    lv_obj_set_flex_flow(st->hunter_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->hunter_page, 8, 0);
    lv_obj_clear_flag(st->hunter_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = subghz_create_header(st->hunter_page, "Hunter",
                                            subghz_host_color_pink(), on_back);
    /* Spacer + settings */
    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 1);
    subghz_add_header_action(header, LV_SYMBOL_SETTINGS, on_settings, NULL);
    subghz_add_radio_badge(header, st);

    /* Spinner + status row */
    lv_obj_t *row = lv_obj_create(st->hunter_page);
    lv_obj_set_size(row, lv_pct(100), 60);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_style_bg_color(row, subghz_host_ui_panel(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    st->hunter_spinner = lv_spinner_create(row);
    lv_obj_set_size(st->hunter_spinner, 40, 40);
    lv_obj_set_style_arc_color(st->hunter_spinner, subghz_host_color_pink(), LV_PART_INDICATOR);

    st->hunter_status_lbl = lv_label_create(row);
    lv_obj_set_flex_grow(st->hunter_status_lbl, 1);
    lv_label_set_long_mode(st->hunter_status_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text(st->hunter_status_lbl, "Hunting...");
    lv_obj_set_style_text_color(st->hunter_status_lbl, subghz_host_color_pink(), 0);
    lv_obj_set_style_text_font(st->hunter_status_lbl, &lv_font_montserrat_18, 0);

    st->hunter_capt_count_lbl = lv_label_create(row);
    lv_label_set_text(st->hunter_capt_count_lbl, "Captures: 0");
    lv_obj_set_style_text_color(st->hunter_capt_count_lbl, subghz_host_color_cyan(), 0);
    lv_obj_set_style_text_font(st->hunter_capt_count_lbl, &lv_font_montserrat_18, 0);

    st->hunter_btn_stop = lv_btn_create(row);
    lv_obj_set_size(st->hunter_btn_stop, 100, 50);
    lv_obj_set_style_bg_color(st->hunter_btn_stop, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(st->hunter_btn_stop, 8, 0);
    lv_obj_add_event_cb(st->hunter_btn_stop, on_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(st->hunter_btn_stop);
    lv_label_set_text(sl, "Stop");
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_18, 0);
    lv_obj_center(sl);

    /* Signal list */
    st->hunter_sig_list = lv_obj_create(st->hunter_page);
    lv_obj_set_size(st->hunter_sig_list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(st->hunter_sig_list, 1);
    lv_obj_set_flex_flow(st->hunter_sig_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(st->hunter_sig_list, 4, 0);
    lv_obj_set_style_pad_row(st->hunter_sig_list, 4, 0);
    lv_obj_set_style_bg_color(st->hunter_sig_list, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(st->hunter_sig_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->hunter_sig_list, 0, 0);
    lv_obj_set_scrollbar_mode(st->hunter_sig_list, LV_SCROLLBAR_MODE_AUTO);

    rebuild_rows(st);

    /* Start reader task + tick */
    st->hunter_task_tab_id = subghz_host_current_tab();
    s_hunter_page_alive = true;
    if (!st->hunter_task)
        xTaskCreate(hunter_reader_task, "sg_hunter", 4096, st, 5, &st->hunter_task);
    if (!st->hunter_ui_timer)
        st->hunter_ui_timer = lv_timer_create(ui_tick_cb, 200, st);

    hunter_start_uart(st);

    ESP_LOGI(TAG, "Hunter page ready (resume=%d)", (int)resume);
}

void show_subghz_hunter_page(void)        { build_page_and_start(false); }
void show_subghz_hunter_page_resume(void) { build_page_and_start(true);  }
