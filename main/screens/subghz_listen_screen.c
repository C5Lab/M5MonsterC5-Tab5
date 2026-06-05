#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_parser.h"
#include "subghz_rf_settings.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/m5stack_tab5.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "subghz_listen";

static void show_action_popup(subghz_tab_state_t *st, int idx);
static void close_action_popup(subghz_tab_state_t *st);
static void show_leave_popup(subghz_tab_state_t *st, size_t count);
static void close_leave_popup(subghz_tab_state_t *st);
static void perform_back(subghz_tab_state_t *st);
static void set_status_msg(subghz_tab_state_t *st, const char *msg, lv_color_t color);
static void apply_pending_status(subghz_tab_state_t *st);
static void update_listen_rssi_gauge(subghz_tab_state_t *st);
static void stop_listening(subghz_tab_state_t *st);

/* ---- Layout (Tab5: large) ----- */
#define WATERFALL_W       800
#define WATERFALL_H       70
#define WATERFALL_TICK_MS 120
#define SIGNAL_CHUNK_CAPACITY 64
#define SIGNAL_ROW_HEIGHT     34
#define SIGNAL_ROW_POOL_SIZE  16
#define COL_IDX_W   60
#define COL_TYPE_W  140
#define COL_FREQ_W  120
#define COL_MF_W    240
#define COL_SER_W   180

#define WF_BG_COLOR    0x0A1628
#define WF_GRID_COLOR  0x152540

#define UART_BUF_LEN 256
#define LINE_BUF_LEN 512

#define LISTEN_RSSI_MIN_DBM  (-120)
#define LISTEN_RSSI_MAX_DBM  (-40)
#define LISTEN_RSSI_ARC_MAX  80

/* Peak-hold: pin the gauge at a caught signal's level for a while, then
 * ease it back down so a burst stays visible instead of flashing for one
 * frame. Tuned for the 120 ms ui tick (WATERFALL_TICK_MS). */
#define LISTEN_RSSI_HOLD_TICKS  12   /* ~1.5 s hold */
#define LISTEN_RSSI_DECAY_DBM   3    /* dB eased off per tick after hold */
#define LISTEN_RSSI_CAPTURE_DBM (-45) /* floor forced when a signal decodes */

/* Private types (forward-declared in subghz_host.h) */
typedef struct subghz_signal {
    int   idx;
    char  type[32];
    float freq;
    char  serial[32];
    int   btn;
    int   cnt;
    char  mf[32];
    char  name[64];
    bool  is_raw;
} subghz_signal_t;

typedef struct subghz_signal_chunk {
    struct subghz_signal_chunk *next;
    size_t used;
    subghz_signal_t items[SIGNAL_CHUNK_CAPACITY];
} subghz_signal_chunk_t;

typedef struct signal_row_view {
    lv_obj_t *row;
    lv_obj_t *idx;
    lv_obj_t *type;
    lv_obj_t *freq;
    lv_obj_t *mf;
    lv_obj_t *serial;
} signal_row_view_t;

/* History critical-section spinlock */
static portMUX_TYPE s_signal_lock = portMUX_INITIALIZER_UNLOCKED;

static const char *s_digit_opts = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

/* ---- Signal-history helpers --------------------------------------- */

static size_t signal_count_snapshot(subghz_tab_state_t *st)
{
    size_t count;
    portENTER_CRITICAL(&s_signal_lock);
    count = st->signal_count;
    portEXIT_CRITICAL(&s_signal_lock);
    return count;
}

static void fill_signal(subghz_signal_t *dst, const subghz_signal_info_t *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->idx    = src->idx;
    dst->freq   = src->freq;
    dst->btn    = src->btn;
    dst->cnt    = src->cnt;
    dst->is_raw = src->is_raw;
    snprintf(dst->type,   sizeof(dst->type),   "%s", src->type[0]   ? src->type   : "--");
    snprintf(dst->serial, sizeof(dst->serial), "%s", src->serial[0] ? src->serial : "--");
    snprintf(dst->mf,     sizeof(dst->mf),     "%s", src->mf[0]     ? src->mf     : "--");
    snprintf(dst->name,   sizeof(dst->name),   "%s", src->name);
}

/* Find a captured signal by idx; copies into `out` if found. */
static bool find_signal_by_idx(subghz_tab_state_t *st, int idx, subghz_signal_t *out)
{
    if (!st || !out || idx <= 0) return false;
    bool found = false;
    portENTER_CRITICAL(&s_signal_lock);
    for (subghz_signal_chunk_t *chunk = st->signal_head; chunk && !found; chunk = chunk->next) {
        for (size_t i = 0; i < chunk->used; i++) {
            if (chunk->items[i].idx == idx) {
                *out = chunk->items[i];
                found = true;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_signal_lock);
    return found;
}

static subghz_signal_chunk_t *alloc_signal_chunk(void)
{
    subghz_signal_chunk_t *chunk = heap_caps_malloc(sizeof(*chunk),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!chunk) return NULL;
    memset(chunk, 0, sizeof(*chunk));
    return chunk;
}

static bool append_signal_history(subghz_tab_state_t *st, const subghz_signal_t *sig)
{
    subghz_signal_chunk_t *new_chunk = NULL;

    if (!st || !sig) return false;

    if (!st->signal_tail || st->signal_tail->used >= SIGNAL_CHUNK_CAPACITY) {
        new_chunk = alloc_signal_chunk();
        if (!new_chunk) {
            if (!st->psram_exhausted)
                ESP_LOGE(TAG, "PSRAM exhausted while storing captured signals");
            st->psram_exhausted = true;
            st->history_dirty = true;
            return false;
        }
    }

    portENTER_CRITICAL(&s_signal_lock);
    if (!st->signal_head) {
        st->signal_head = new_chunk;
        st->signal_tail = new_chunk;
        new_chunk = NULL;
    } else if (new_chunk) {
        st->signal_tail->next = new_chunk;
        st->signal_tail = new_chunk;
        new_chunk = NULL;
    }

    subghz_signal_t *inserted = &st->signal_tail->items[st->signal_tail->used++];
    *inserted = *sig;
    st->last_signal = inserted;
    st->signal_count++;
    portEXIT_CRITICAL(&s_signal_lock);

    if (new_chunk) free(new_chunk);

    st->history_dirty = true;
    return true;
}

static bool merge_duplicate_signal(subghz_tab_state_t *st, const subghz_signal_info_t *src)
{
    bool merged = false;
    if (!src || !src->is_duplicate) return false;

    portENTER_CRITICAL(&s_signal_lock);
    if (st->last_signal && !st->last_signal->is_raw) {
        if ((src->idx > 0 && st->last_signal->idx == src->idx) ||
            (strcmp(st->last_signal->type, src->type) == 0 &&
             strcmp(st->last_signal->serial, src->serial) == 0 &&
             st->last_signal->btn == src->btn)) {
            if (src->cnt > 0) st->last_signal->cnt = src->cnt;
            merged = true;
        }
    }
    portEXIT_CRITICAL(&s_signal_lock);

    if (merged) st->history_dirty = true;
    return merged;
}

static void clear_signal_history(subghz_tab_state_t *st)
{
    subghz_signal_chunk_t *head;

    portENTER_CRITICAL(&s_signal_lock);
    head = st->signal_head;
    st->signal_head = NULL;
    st->signal_tail = NULL;
    st->last_signal = NULL;
    st->signal_count = 0;
    portEXIT_CRITICAL(&s_signal_lock);

    while (head) {
        subghz_signal_chunk_t *next = head->next;
        free(head);
        head = next;
    }
}

static void copy_signal_window(subghz_tab_state_t *st, size_t first_index,
                               subghz_signal_t *out, size_t max_items,
                               size_t *out_count, size_t *out_total)
{
    size_t copied = 0;
    size_t base = 0;
    subghz_signal_chunk_t *chunk;
    size_t offset;

    portENTER_CRITICAL(&s_signal_lock);
    if (out_total) *out_total = st->signal_count;

    chunk = st->signal_head;
    while (chunk && first_index >= base + chunk->used) {
        base += chunk->used;
        chunk = chunk->next;
    }

    offset = first_index - base;
    while (chunk && copied < max_items) {
        while (offset < chunk->used && copied < max_items) {
            out[copied++] = chunk->items[offset++];
        }
        chunk = chunk->next;
        offset = 0;
    }
    portEXIT_CRITICAL(&s_signal_lock);

    if (out_count) *out_count = copied;
}

/* ---- Waterfall canvas (RGB565) ------------------------------------ */

static inline uint16_t wf_color(uint32_t hex)
{
    return lv_color_to_u16(lv_color_hex(hex));
}

static void waterfall_fill_bg(uint16_t *buf)
{
    uint16_t bg   = wf_color(WF_BG_COLOR);
    uint16_t grid = wf_color(WF_GRID_COLOR);
    for (int y = 0; y < WATERFALL_H; y++) {
        uint16_t c = (y % 10 == 0) ? grid : bg;
        for (int x = 0; x < WATERFALL_W; x++)
            buf[y * WATERFALL_W + x] = c;
    }
}

static void waterfall_push_activity(subghz_tab_state_t *st, bool active)
{
    if (!st->canvas || !st->canvas_buf) return;
    uint16_t *buf = (uint16_t *)st->canvas_buf;

    /* Shift left one column */
    for (int y = 0; y < WATERFALL_H; y++) {
        for (int x = 0; x < WATERFALL_W - 1; x++)
            buf[y * WATERFALL_W + x] = buf[y * WATERFALL_W + x + 1];
    }

    uint16_t bg   = wf_color(WF_BG_COLOR);
    uint16_t grid = wf_color(WF_GRID_COLOR);
    uint16_t bar  = wf_color(0xFF3030);

    for (int y = 0; y < WATERFALL_H; y++) {
        if (active)
            buf[y * WATERFALL_W + WATERFALL_W - 1] = bar;
        else
            buf[y * WATERFALL_W + WATERFALL_W - 1] = (y % 10 == 0) ? grid : bg;
    }

    lv_obj_invalidate(st->canvas);
}

/* ---- Signal list (lazy virtual rendering) ------------------------- */

static void update_signal_count_label(subghz_tab_state_t *st, size_t count)
{
    if (!st->count_lbl) return;

    if (st->psram_exhausted) {
        lv_label_set_text_fmt(st->count_lbl, "Sig: %lu MEM", (unsigned long)count);
        lv_obj_set_style_text_color(st->count_lbl, subghz_host_color_red(), 0);
    } else {
        lv_label_set_text_fmt(st->count_lbl, "Sig: %lu", (unsigned long)count);
        lv_obj_set_style_text_color(st->count_lbl, subghz_host_color_cyan(), 0);
    }
}

static void on_signal_row_clicked(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    lv_obj_t *row = lv_event_get_current_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx > 0) show_action_popup(st, idx);
}

static void configure_signal_row(subghz_tab_state_t *st, signal_row_view_t *view)
{
    if (!view || !st->sig_list) return;

    view->row = lv_obj_create(st->sig_list);
    lv_obj_set_size(view->row, lv_pct(100), SIGNAL_ROW_HEIGHT - 2);
    lv_obj_set_style_pad_all(view->row, 4, 0);
    lv_obj_set_style_pad_gap(view->row, 6, 0);
    lv_obj_set_style_bg_color(view->row, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_color(view->row, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(view->row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->row, 0, 0);
    lv_obj_set_style_radius(view->row, 4, 0);
    lv_obj_set_flex_flow(view->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(view->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(view->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view->row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(view->row, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(view->row, on_signal_row_clicked, LV_EVENT_CLICKED, NULL);

    view->idx = lv_label_create(view->row);
    lv_obj_set_width(view->idx, COL_IDX_W);
    lv_obj_set_style_text_font(view->idx, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(view->idx, subghz_host_color_cyan(), 0);

    view->type = lv_label_create(view->row);
    lv_obj_set_width(view->type, COL_TYPE_W);
    lv_obj_set_style_text_font(view->type, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(view->type, LV_LABEL_LONG_CLIP);

    view->freq = lv_label_create(view->row);
    lv_obj_set_width(view->freq, COL_FREQ_W);
    lv_obj_set_style_text_font(view->freq, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(view->freq, subghz_host_ui_muted(), 0);

    view->mf = lv_label_create(view->row);
    lv_obj_set_width(view->mf, COL_MF_W);
    lv_obj_set_style_text_font(view->mf, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(view->mf, subghz_host_ui_text(), 0);
    lv_label_set_long_mode(view->mf, LV_LABEL_LONG_CLIP);

    view->serial = lv_label_create(view->row);
    lv_obj_set_width(view->serial, COL_SER_W);
    lv_obj_set_style_text_font(view->serial, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(view->serial, subghz_host_ui_muted(), 0);
    lv_label_set_long_mode(view->serial, LV_LABEL_LONG_CLIP);
}

static void refresh_signal_list_view(subghz_tab_state_t *st)
{
    if (!st || !st->sig_list || !st->sig_spacer || !st->empty_lbl || !st->row_pool) return;

    subghz_signal_t window[SIGNAL_ROW_POOL_SIZE];
    size_t copied = 0;
    size_t total = 0;

    lv_coord_t scroll_y = lv_obj_get_scroll_y(st->sig_list);
    if (scroll_y < 0) scroll_y = 0;

    size_t first_index = (size_t)scroll_y / SIGNAL_ROW_HEIGHT;
    copy_signal_window(st, first_index, window, SIGNAL_ROW_POOL_SIZE, &copied, &total);

    update_signal_count_label(st, total);

    if (total == 0) {
        lv_obj_clear_flag(st->empty_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(st->sig_spacer, 1);
        for (int i = 0; i < SIGNAL_ROW_POOL_SIZE; i++) {
            if (st->row_pool[i].row) {
                lv_obj_set_user_data(st->row_pool[i].row, (void *)(intptr_t)-1);
                lv_obj_add_flag(st->row_pool[i].row, LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    lv_obj_add_flag(st->empty_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(st->sig_spacer, (lv_coord_t)(total * SIGNAL_ROW_HEIGHT));

    for (int i = 0; i < SIGNAL_ROW_POOL_SIZE; i++) {
        signal_row_view_t *view = &st->row_pool[i];
        if (!view->row) continue;

        if ((size_t)i >= copied) {
            lv_obj_set_user_data(view->row, (void *)(intptr_t)-1);
            lv_obj_add_flag(view->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        size_t signal_index = first_index + (size_t)i;
        const subghz_signal_t *sig = &window[i];

        lv_obj_set_pos(view->row, 0, (lv_coord_t)(signal_index * SIGNAL_ROW_HEIGHT));
        lv_obj_set_user_data(view->row, (void *)(intptr_t)sig->idx);
        lv_label_set_text_fmt(view->idx, "%d", sig->idx);
        lv_label_set_text(view->type, sig->type);
        lv_obj_set_style_text_color(view->type,
                                    sig->is_raw ? subghz_host_color_orange() : subghz_host_color_green(), 0);
        lv_label_set_text_fmt(view->freq, "%d.%02d",
                              (int)sig->freq,
                              ((int)(sig->freq * 100.0f + 0.5f)) % 100);
        lv_label_set_text(view->mf, sig->mf[0] ? sig->mf : "--");
        lv_label_set_text(view->serial, sig->serial[0] ? sig->serial : "--");
        lv_obj_clear_flag(view->row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- RSSI gauge + decode log helpers ----------------------------- */

static lv_color_t listen_rssi_color(int rssi_dbm)
{
    if (rssi_dbm > -50) return subghz_host_color_green();
    if (rssi_dbm > -70) return subghz_host_color_orange();
    return subghz_host_color_red();
}

static int listen_rssi_to_arc(int rssi_dbm)
{
    int span = LISTEN_RSSI_MAX_DBM - LISTEN_RSSI_MIN_DBM;
    int val = (rssi_dbm - LISTEN_RSSI_MIN_DBM) * LISTEN_RSSI_ARC_MAX / span;
    if (val < 0) val = 0;
    if (val > LISTEN_RSSI_ARC_MAX) val = LISTEN_RSSI_ARC_MAX;
    return val;
}

/* Raise the held peak (and reset the hold window) when a stronger value
 * arrives. Called from the reader task on RSSI / signal capture. */
static void listen_bump_peak(subghz_tab_state_t *st, int value)
{
    if (!st) return;
    if (value > st->listen_rssi_peak_dbm) {
        st->listen_rssi_peak_dbm = value;
    }
    st->listen_rssi_hold_ticks = LISTEN_RSSI_HOLD_TICKS;
    st->listen_rssi_dirty = true;
}

/* Advance the peak-hold envelope one ui tick: hold while ticks remain,
 * then decay toward the latest raw RSSI. */
static void listen_advance_peak(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->listen_rssi_hold_ticks > 0) {
        st->listen_rssi_hold_ticks--;
        return;
    }
    int raw = st->listen_rssi_dbm;
    if (st->listen_rssi_peak_dbm > raw) {
        st->listen_rssi_peak_dbm -= LISTEN_RSSI_DECAY_DBM;
        if (st->listen_rssi_peak_dbm < raw)
            st->listen_rssi_peak_dbm = raw;
    } else {
        st->listen_rssi_peak_dbm = raw;
    }
}

static void update_listen_rssi_gauge(subghz_tab_state_t *st)
{
    if (!st) return;

    if (!st->listen_running) {
        if (st->listen_rssi_arc) {
            lv_arc_set_value(st->listen_rssi_arc, 0);
            lv_obj_set_style_arc_color(st->listen_rssi_arc, subghz_host_ui_muted(),
                                       LV_PART_INDICATOR);
        }
        if (st->listen_rssi_lbl) {
            lv_label_set_text(st->listen_rssi_lbl, "--");
            lv_obj_set_style_text_color(st->listen_rssi_lbl, subghz_host_ui_muted(), 0);
        }
        return;
    }

    int rssi = st->listen_rssi_peak_dbm;
    lv_color_t col = listen_rssi_color(rssi);

    if (st->listen_rssi_arc) {
        lv_arc_set_value(st->listen_rssi_arc, listen_rssi_to_arc(rssi));
        lv_obj_set_style_arc_color(st->listen_rssi_arc, col, LV_PART_INDICATOR);
    }
    if (st->listen_rssi_lbl) {
        lv_label_set_text_fmt(st->listen_rssi_lbl, "%d", rssi);
        lv_obj_set_style_text_color(st->listen_rssi_lbl, col, 0);
    }
}

static bool listen_line_should_log(const char *line)
{
    if (!line) return false;

    const char *tag = subghz_normalize_line(line);
    if (tag) {
        if (strncmp(tag, "[SUBGHZ_RSSI]", 13) == 0) return true;
        if (strncmp(tag, "[KL_DEC]", 8) == 0) return true;
        if (strncmp(tag, "[REPLAY_SEG]", 12) == 0) return true;
        if (strncmp(tag, "[REPLAY]", 8) == 0) return true;
        if (strncmp(tag, "[KAT_RX]", 8) == 0) return true;
        if (strncmp(tag, "[SUBGHZ_RX]", 11) == 0 &&
            (tag[11] == ' ' || tag[11] == '\0'))
            return true;
    }

    if (strstr(line, "SubGHz: key saved")) return true;
    return false;
}

static void listen_log_line(const char *line)
{
    if (line && line[0]) ESP_LOGI(TAG, "%s", line);
}

/* ---- UART monitor task ------------------------------------------- */

/* Lift name= value out of an event line into a stack buffer. */
static void extract_event_name(const char *line, char *out, size_t out_sz)
{
    out[0] = '\0';
    const char *p = strstr(line, "name=");
    if (!p) return;
    p += 5;
    const char *end = strchr(p, ' ');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static void handle_event_line(subghz_tab_state_t *st, const char *line)
{
    /* These short events are handled regardless of listen_running so the UI
     * updates correctly when the user triggers Save/Transmit from a row
     * popup after pausing capture. */
    if (strstr(line, "[SUBGHZ_SAVE] ")) {
        int idx = 0;
        char name[64];
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        extract_event_name(line, name, sizeof(name));
        char msg[96];
        if (name[0]) snprintf(msg, sizeof(msg), "#%d saved to SD: %s", idx, name);
        else         snprintf(msg, sizeof(msg), "#%d saved to SD", idx);
        set_status_msg(st, msg, subghz_host_color_green());
        return;
    }
    if (strstr(line, "[SUBGHZ_SAVE_ERR] ")) {
        int idx = 0;
        char reason[32] = {0};
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        p = strstr(line, "reason=");
        if (p) {
            p += 7;
            const char *end = strchr(p, ' ');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len >= sizeof(reason)) len = sizeof(reason) - 1;
            memcpy(reason, p, len);
            reason[len] = '\0';
        }
        char msg[96];
        snprintf(msg, sizeof(msg), "Save #%d failed: %s", idx, reason[0] ? reason : "error");
        set_status_msg(st, msg, subghz_host_color_red());
        return;
    }
    if (strstr(line, "[SUBGHZ_TX] ")) {
        int idx = 0;
        const char *p = strstr(line, "idx=");
        if (p) idx = atoi(p + 4);
        char msg[64];
        if (idx > 0) snprintf(msg, sizeof(msg), "Transmitted #%d", idx);
        else         snprintf(msg, sizeof(msg), "Transmitted");
        set_status_msg(st, msg, subghz_host_color_green());
        return;
    }
}

static void process_subghz_line(subghz_tab_state_t *st, const char *line)
{
    handle_event_line(st, line);

    subghz_note_radio_line(st, line);

    /* Radio missing / failed to start: surface it and ask the UI tick to
     * drop back to idle (LVGL must be touched on the UI thread). */
    if (strstr(line, "CC1101 NOT DETECTED") ||
        strstr(line, "SubGHz receive start failed:")) {
        if (st->listen_running) {
            st->listen_radio_fail_pending = true;
            set_status_msg(st, "CC1101 not detected", subghz_host_color_red());
        }
        return;
    }

    if (!st->listen_running) return;

    if (listen_line_should_log(line))
        listen_log_line(line);

    int rssi = 0;
    if (subghz_parse_rssi_line(line, &rssi)) {
        st->listen_rssi_dbm = rssi;
        listen_bump_peak(st, rssi);
        return;
    }

    subghz_signal_info_t parsed;
    if (!subghz_parse_signal_line(line, &parsed)) return;

    if (parsed.kind == SUBGHZ_SIGNAL_KIND_LIST) return;

    if (parsed.kind == SUBGHZ_SIGNAL_KIND_RX ||
        parsed.kind == SUBGHZ_SIGNAL_KIND_RX_DUP ||
        parsed.kind == SUBGHZ_SIGNAL_KIND_RAW) {
        st->activity_pending = true;
        /* An actual decode means a real signal was caught: pin the gauge
         * high and reset the hold window so it stays lit a moment. */
        listen_bump_peak(st, LISTEN_RSSI_CAPTURE_DBM);
    }

    if (!st->raw_mode && parsed.kind == SUBGHZ_SIGNAL_KIND_RAW) return;
    if (st->raw_mode && parsed.kind == SUBGHZ_SIGNAL_KIND_RX_DUP) return;

    if (merge_duplicate_signal(st, &parsed)) return;

    subghz_signal_t sig;
    fill_signal(&sig, &parsed);
    append_signal_history(st, &sig);
}

/* Set to false in listen_cleanup() to terminate the reader task without
 * stopping the firmware capture (st->listen_running covers that). */
static volatile bool s_listen_page_alive = false;

static void subghz_listen_task_fn(void *arg)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)arg;
    int tab_id = st->listen_task_tab_id;

    ESP_LOGI(TAG, "Listen monitor task started for tab %d", tab_id);

    static char rx_buf[UART_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;

    while (s_listen_page_alive) {
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            rx_buf[len] = '\0';
            for (int i = 0; i < len; i++) {
                char c = rx_buf[i];
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';
                        process_subghz_line(st, line_buf);
                        line_pos = 0;
                    }
                } else if (line_pos < (int)sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    ESP_LOGI(TAG, "Listen monitor task ended");
    st->listen_task = NULL;
    vTaskDelete(NULL);
}

/* ---- UI tick (waterfall + list refresh) ------------------------- */

static void ui_tick_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;

    if (st->radio_status_dirty) {
        st->radio_status_dirty = false;
        subghz_refresh_radio_badge(st);
    }

    if (st->listen_radio_fail_pending) {
        st->listen_radio_fail_pending = false;
        if (st->listen_running) stop_listening(st);
    }

    if (st->listen_running) {
        /* Run the peak-hold envelope every tick so the gauge eases down
         * smoothly regardless of how often RSSI lines arrive. */
        listen_advance_peak(st);
        st->listen_rssi_dirty = false;
        update_listen_rssi_gauge(st);
    } else if (st->listen_rssi_dirty) {
        st->listen_rssi_dirty = false;
        update_listen_rssi_gauge(st);
    }

    bool activity = st->activity_pending;
    st->activity_pending = false;
    bool history_dirty = st->history_dirty;
    st->history_dirty = false;

    if (st->listen_running)
        waterfall_push_activity(st, activity);

    if (history_dirty && st->follow_latest && st->sig_list) {
        size_t total = signal_count_snapshot(st);
        lv_coord_t target_y = (lv_coord_t)(total * SIGNAL_ROW_HEIGHT) - lv_obj_get_height(st->sig_list);
        if (target_y < 0) target_y = 0;
        lv_obj_scroll_to_y(st->sig_list, target_y, LV_ANIM_OFF);
    }
    if (history_dirty || st->psram_exhausted)
        refresh_signal_list_view(st);
    apply_pending_status(st);
}

static void on_signal_list_scroll(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    if (!st || !st->sig_list) return;

    size_t total = signal_count_snapshot(st);
    lv_coord_t scroll_y = lv_obj_get_scroll_y(st->sig_list);
    if (scroll_y < 0) scroll_y = 0;

    lv_coord_t max_scroll = (lv_coord_t)(total * SIGNAL_ROW_HEIGHT) - lv_obj_get_height(st->sig_list);
    if (max_scroll < 0) max_scroll = 0;

    st->follow_latest = (max_scroll - scroll_y) <= SIGNAL_ROW_HEIGHT;
    refresh_signal_list_view(st);
}

/* ---- Frequency popup (5 digit rollers) ------------------------- */

static void close_freq_popup(subghz_tab_state_t *st)
{
    if (st->listen_freq_popup) {
        lv_obj_delete(st->listen_freq_popup);
        st->listen_freq_popup = NULL;
    }
}

static void update_freq_label(subghz_tab_state_t *st)
{
    if (st->listen_freq_lbl) {
        int whole = (int)st->freq_mhz;
        int frac  = ((int)(st->freq_mhz * 100.0f + 0.5f)) % 100;
        lv_label_set_text_fmt(st->listen_freq_lbl, "%d.%02d MHz", whole, frac);
    }
}

static void on_freq_set(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    int d[5];
    for (int i = 0; i < 5; i++)
        d[i] = (int)lv_roller_get_selected(st->rollers[i]);

    st->freq_mhz = d[0] * 100.0f + d[1] * 10.0f + d[2] * 1.0f
                 + d[3] * 0.1f + d[4] * 0.01f;
    update_freq_label(st);
    close_freq_popup(st);
}

static void on_freq_cancel(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    close_freq_popup(st);
}

static void freq_decompose(float freq, int digits[5])
{
    int val = (int)(freq * 100.0f + 0.5f);
    digits[0] = (val / 10000) % 10;
    digits[1] = (val / 1000) % 10;
    digits[2] = (val / 100) % 10;
    digits[3] = (val / 10) % 10;
    digits[4] = val % 10;
}

static void on_freq_tap(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    if (!st) return;
    if (st->listen_running) return;
    if (st->listen_freq_popup) { close_freq_popup(st); return; }

    int digits[5];
    freq_decompose(st->freq_mhz, digits);

    /* Overlay (semi-transparent) */
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->listen_freq_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 480, 280);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_pink());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 14, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, "Frequency (MHz)");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    lv_obj_t *roller_row = lv_obj_create(popup);
    lv_obj_set_size(roller_row, lv_pct(100), 140);
    lv_obj_set_flex_flow(roller_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(roller_row, 0, 0);
    lv_obj_set_style_pad_gap(roller_row, 4, 0);
    lv_obj_set_style_bg_opa(roller_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(roller_row, 0, 0);
    lv_obj_clear_flag(roller_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        if (i == 3) {
            lv_obj_t *dot = lv_label_create(roller_row);
            lv_label_set_text(dot, ".");
            lv_obj_set_style_text_font(dot, &lv_font_montserrat_36, 0);
            lv_obj_set_style_text_color(dot, subghz_host_ui_text(), 0);
        }
        st->rollers[i] = lv_roller_create(roller_row);
        lv_roller_set_options(st->rollers[i], s_digit_opts, LV_ROLLER_MODE_INFINITE);
        lv_roller_set_visible_row_count(st->rollers[i], 3);
        lv_obj_set_width(st->rollers[i], 64);
        lv_obj_set_style_bg_color(st->rollers[i], subghz_host_ui_card(), 0);
        lv_obj_set_style_bg_opa(st->rollers[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(st->rollers[i], subghz_host_ui_text(), 0);
        lv_obj_set_style_text_font(st->rollers[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(st->rollers[i], subghz_host_color_cyan(), LV_PART_SELECTED);
        lv_obj_set_style_bg_color(st->rollers[i], subghz_host_ui_panel(), LV_PART_SELECTED);
        lv_obj_set_style_border_width(st->rollers[i], 0, 0);
        lv_obj_set_style_radius(st->rollers[i], 8, 0);
        lv_roller_set_selected(st->rollers[i], digits[i], LV_ANIM_OFF);
    }

    lv_obj_t *btn_row = lv_obj_create(popup);
    lv_obj_set_size(btn_row, lv_pct(100), 60);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *set_btn = lv_btn_create(btn_row);
    lv_obj_set_size(set_btn, 160, 50);
    lv_obj_set_style_bg_color(set_btn, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(set_btn, 8, 0);
    lv_obj_add_event_cb(set_btn, on_freq_set, LV_EVENT_CLICKED, st);
    lv_obj_t *sl = lv_label_create(set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_18, 0);
    lv_obj_center(sl);

    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 160, 50);
    lv_obj_set_style_bg_color(cancel_btn, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, on_freq_cancel, LV_EVENT_CLICKED, st);
    lv_obj_t *cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);
}

/* ---- Start/Stop / RAW toggle ------------------------------------ */

static void update_start_stop_btn(subghz_tab_state_t *st)
{
    if (!st->btn_start_stop) return;
    lv_obj_t *lbl = lv_obj_get_child(st->btn_start_stop, 0);
    if (st->listen_running) {
        lv_obj_set_style_bg_color(st->btn_start_stop, subghz_host_color_red(), 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_STOP " Stop");
    } else {
        lv_obj_set_style_bg_color(st->btn_start_stop, subghz_host_color_green(), 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLAY " Start");
    }
}

static void reset_capture_session(subghz_tab_state_t *st)
{
    st->follow_latest = true;
    st->activity_pending = false;
    st->history_dirty = true;
    st->psram_exhausted = false;
    st->listen_rssi_dbm = -100;
    st->listen_rssi_peak_dbm = -100;
    st->listen_rssi_hold_ticks = 0;
    st->listen_rssi_dirty = true;
    st->listen_radio_fail_pending = false;
    clear_signal_history(st);

    if (st->sig_list) lv_obj_scroll_to_y(st->sig_list, 0, LV_ANIM_OFF);
    if (st->canvas_buf) {
        waterfall_fill_bg((uint16_t *)st->canvas_buf);
        if (st->canvas) lv_obj_invalidate(st->canvas);
    }
    refresh_signal_list_view(st);
}

static void stop_listening(subghz_tab_state_t *st)
{
    if (!st->listen_running) return;
    st->listen_running = false;
    subghz_host_uart_send("subghz_stop");
    /* Keep monitor task installed so Save/TX responses still update the
     * status label; listen_cleanup() tears it down on screen exit. */
    st->activity_pending = false;
    st->listen_rssi_dirty = true;
    update_start_stop_btn(st);
    if (st->btn_raw) lv_obj_clear_state(st->btn_raw, LV_STATE_DISABLED);
    ESP_LOGI(TAG, "SubGHz listen stopped");
}

static void start_listening(subghz_tab_state_t *st)
{
    if (st->listen_running) return;

    reset_capture_session(st);
    st->listen_running = true;

    update_start_stop_btn(st);
    if (st->btn_raw) lv_obj_add_state(st->btn_raw, LV_STATE_DISABLED);

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "subghz_freq %.2f", st->freq_mhz);
    subghz_host_uart_send(cmd);

    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    if (st->raw_mode)
        snprintf(cmd, sizeof(cmd), "subghz_rx raw rssi=%d", (int)cfg.listen_rssi_dbm);
    else
        snprintf(cmd, sizeof(cmd), "subghz_rx rssi=%d", (int)cfg.listen_rssi_dbm);
    subghz_host_uart_send(cmd);

    ESP_LOGI(TAG, "SubGHz listen started (%.2f MHz, raw=%d, rssi=%d)",
             st->freq_mhz, (int)st->raw_mode, (int)cfg.listen_rssi_dbm);
}

static void on_start_stop(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    if (!st) return;
    if (st->listen_running) stop_listening(st);
    else                    start_listening(st);
}

static void update_mode_switch_labels(subghz_tab_state_t *st)
{
    if (!st->btn_raw) return;
    lv_obj_t *parent = lv_obj_get_parent(st->btn_raw);
    if (!parent) return;
    lv_obj_t *dec_lbl = lv_obj_get_child(parent, 0);
    lv_obj_t *raw_lbl = lv_obj_get_child(parent, 2);
    if (dec_lbl) {
        lv_obj_set_style_text_color(dec_lbl,
            st->raw_mode ? subghz_host_ui_muted() : subghz_host_color_cyan(), 0);
    }
    if (raw_lbl) {
        lv_obj_set_style_text_color(raw_lbl,
            st->raw_mode ? subghz_host_color_orange() : subghz_host_ui_muted(), 0);
    }
}

static void on_raw_toggle(lv_event_t *e)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_event_get_user_data(e);
    if (!st || !st->btn_raw) return;
    st->raw_mode = lv_obj_has_state(st->btn_raw, LV_STATE_CHECKED);
    update_mode_switch_labels(st);
}

/* ---- Cleanup ----------------------------------------------------- */

void subghz_listen_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    stop_listening(st);

    /* Tell reader task to exit and wait for it (best effort, ~500 ms) */
    s_listen_page_alive = false;
    for (int i = 0; i < 25 && st->listen_task; i++) vTaskDelay(pdMS_TO_TICKS(20));

    if (st->ui_timer) { lv_timer_delete(st->ui_timer); st->ui_timer = NULL; }
    if (st->listen_status_clear_timer) {
        lv_timer_delete(st->listen_status_clear_timer);
        st->listen_status_clear_timer = NULL;
    }
    st->listen_status_pending = false;
    if (st->canvas_buf) { heap_caps_free(st->canvas_buf); st->canvas_buf = NULL; }
    st->canvas = NULL;
    if (st->row_pool) { free(st->row_pool); st->row_pool = NULL; }
    clear_signal_history(st);

    close_action_popup(st);
    close_leave_popup(st);
    if (st->listen_freq_popup) { lv_obj_delete(st->listen_freq_popup); st->listen_freq_popup = NULL; }
    if (st->listen_page) { lv_obj_delete(st->listen_page); st->listen_page = NULL; }

    st->sig_list = NULL;
    st->sig_spacer = NULL;
    st->empty_lbl = NULL;
    st->count_lbl = NULL;
    st->listen_freq_lbl = NULL;
    st->listen_rssi_arc = NULL;
    st->listen_rssi_lbl = NULL;
    st->btn_start_stop = NULL;
    st->btn_raw = NULL;
    for (int i = 0; i < 5; i++) st->rollers[i] = NULL;
}

static void perform_back(subghz_tab_state_t *st)
{
    subghz_listen_cleanup(st);
    show_subghz_page();
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;

    /* Confirm leaving if there are unsaved captures. */
    size_t total = signal_count_snapshot(st);
    if (total > 0 && !st->listen_leave_popup) {
        show_leave_popup(st, total);
        return;
    }
    perform_back(st);
}

static void on_settings(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_listen_cleanup(st);
    show_subghz_listen_settings_page();
}

/* ---- Status messaging (cross-task) ----------------------------- */

static void set_status_msg(subghz_tab_state_t *st, const char *msg, lv_color_t color)
{
    if (!st || !msg) return;
    snprintf(st->listen_status_text, sizeof(st->listen_status_text), "%s", msg);
    /* Pack RGB into an int for thread-safe handoff to the UI tick. */
    st->listen_status_color_argb = ((int)color.red   << 16) |
                                   ((int)color.green << 8)  |
                                    (int)color.blue;
    st->listen_status_pending = true;
}

static void status_clear_timer_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (st) {
        st->listen_status_clear_timer = NULL;
        update_signal_count_label(st, signal_count_snapshot(st));
    }
}

static void apply_pending_status(subghz_tab_state_t *st)
{
    if (!st || !st->listen_status_pending || !st->count_lbl) return;
    uint32_t packed = (uint32_t)st->listen_status_color_argb;
    lv_color_t color = lv_color_make((uint8_t)((packed >> 16) & 0xFF),
                                     (uint8_t)((packed >> 8)  & 0xFF),
                                     (uint8_t)( packed        & 0xFF));
    lv_label_set_text(st->count_lbl, st->listen_status_text);
    lv_obj_set_style_text_color(st->count_lbl, color, 0);
    st->listen_status_pending = false;
    if (st->listen_status_clear_timer) {
        lv_timer_delete(st->listen_status_clear_timer);
        st->listen_status_clear_timer = NULL;
    }
    st->listen_status_clear_timer = lv_timer_create(status_clear_timer_cb, 3000, st);
    lv_timer_set_repeat_count(st->listen_status_clear_timer, 1);
}

/* ---- Action popup (row tap -> Save / Transmit / Cancel) -------- */

static void close_action_popup(subghz_tab_state_t *st)
{
    if (st && st->listen_action_popup) {
        lv_obj_delete(st->listen_action_popup);
        st->listen_action_popup = NULL;
    }
}

static void on_action_save(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->listen_pending_action_idx;
    close_action_popup(st);
    if (idx <= 0) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_save %d", idx);
    subghz_host_uart_send(cmd);
    char msg[64];
    snprintf(msg, sizeof(msg), "Saving #%d to SD...", idx);
    set_status_msg(st, msg, subghz_host_color_blue());
    (void)e;
}

static void on_action_transmit(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    int idx = st->listen_pending_action_idx;
    close_action_popup(st);
    if (idx <= 0) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_tx %d mem", idx);
    subghz_host_uart_send(cmd);
    char msg[64];
    snprintf(msg, sizeof(msg), "Transmitting #%d...", idx);
    set_status_msg(st, msg, subghz_host_color_blue());
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
    if (!st || idx <= 0) return;
    close_action_popup(st);

    subghz_signal_t sig;
    bool found = find_signal_by_idx(st, idx, &sig);
    st->listen_pending_action_idx = idx;

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->listen_action_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 540, 280);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_cyan());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 16, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text_fmt(title, "Signal #%d (%s)", idx,
                          found && sig.type[0] ? sig.type : "--");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *name_lbl = lv_label_create(popup);
    lv_obj_set_width(name_lbl, lv_pct(100));
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(name_lbl, "name: %s",
                          (found && sig.name[0]) ? sig.name : "(unset)");
    lv_obj_set_style_text_color(name_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(name_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *brow = lv_obj_create(popup);
    lv_obj_set_size(brow, lv_pct(100), 60);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_set_style_pad_all(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    struct {
        const char    *label;
        lv_color_t     bg;
        lv_event_cb_t  cb;
    } btns[] = {
        { "Save to SD", subghz_host_color_green(),  on_action_save     },
        { "Transmit",   subghz_host_color_orange(), on_action_transmit },
        { "Cancel",     subghz_host_ui_muted(),     on_action_cancel   },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(brow);
        lv_obj_set_size(b, 160, 50);
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

/* ---- Leave popup (unsaved captures warning) -------------------- */

static void close_leave_popup(subghz_tab_state_t *st)
{
    if (st && st->listen_leave_popup) {
        lv_obj_delete(st->listen_leave_popup);
        st->listen_leave_popup = NULL;
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

static void show_leave_popup(subghz_tab_state_t *st, size_t count)
{
    close_leave_popup(st);

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->listen_leave_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 560, 260);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_red());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 18, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, "Leave Listen?");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *body = lv_label_create(popup);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(body,
        "%lu unsaved capture%s will be cleared from this view. "
        "Save them to SD first?",
        (unsigned long)count, count == 1 ? "" : "s");
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

    lv_obj_t *leave_btn = lv_btn_create(brow);
    lv_obj_set_size(leave_btn, 200, 50);
    lv_obj_set_style_bg_color(leave_btn, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(leave_btn, 8, 0);
    lv_obj_add_event_cb(leave_btn, on_leave_confirm, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ll = lv_label_create(leave_btn);
    lv_label_set_text(ll, "Leave");
    lv_obj_set_style_text_color(ll, lv_color_white(), 0);
    lv_obj_set_style_text_font(ll, &lv_font_montserrat_18, 0);
    lv_obj_center(ll);

    lv_obj_t *stay_btn = lv_btn_create(brow);
    lv_obj_set_size(stay_btn, 200, 50);
    lv_obj_set_style_bg_color(stay_btn, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(stay_btn, 8, 0);
    lv_obj_add_event_cb(stay_btn, on_leave_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl2 = lv_label_create(stay_btn);
    lv_label_set_text(sl2, "Stay");
    lv_obj_set_style_text_color(sl2, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl2, &lv_font_montserrat_18, 0);
    lv_obj_center(sl2);
}

/* ---- Public entry ------------------------------------------------ */

void show_subghz_listen_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();

    /* Always rebuild — we own the per-tab reader task. */
    if (st->listen_page) { lv_obj_delete(st->listen_page); st->listen_page = NULL; }

    /* Ensure freq default */
    if (st->freq_mhz < 1.0f) st->freq_mhz = 433.92f;

    /* Reset transient state */
    st->signal_head = NULL;
    st->signal_tail = NULL;
    st->last_signal = NULL;
    st->signal_count = 0;
    st->listen_running = false;
    st->follow_latest = true;
    st->history_dirty = true;
    st->activity_pending = false;
    st->psram_exhausted = false;
    st->listen_status_pending = false;
    st->listen_rssi_dbm = -100;
    st->listen_rssi_peak_dbm = -100;
    st->listen_rssi_hold_ticks = 0;
    st->listen_rssi_dirty = true;
    st->listen_rssi_arc = NULL;
    st->listen_rssi_lbl = NULL;

    /* Allocate row pool */
    if (!st->row_pool) {
        st->row_pool = calloc(SIGNAL_ROW_POOL_SIZE, sizeof(signal_row_view_t));
    }

    /* Build page */
    st->listen_page = lv_obj_create(container);
    lv_obj_set_size(st->listen_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->listen_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->listen_page, 0, 0);
    lv_obj_set_style_pad_all(st->listen_page, 10, 0);
    lv_obj_set_flex_flow(st->listen_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->listen_page, 8, 0);
    lv_obj_clear_flag(st->listen_page, LV_OBJ_FLAG_SCROLLABLE);

    /* Header with back + title + freq button + settings gear */
    lv_obj_t *header = subghz_create_header(st->listen_page, "Listen",
                                            subghz_host_color_cyan(), on_back);

    /* Spacer + freq button + settings gear on right */
    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 1);

    lv_obj_t *rssi_box = lv_obj_create(header);
    lv_obj_set_size(rssi_box, 56, 48);
    lv_obj_set_style_bg_opa(rssi_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rssi_box, 0, 0);
    lv_obj_set_style_pad_all(rssi_box, 0, 0);
    lv_obj_set_style_margin_right(rssi_box, 8, 0);
    lv_obj_clear_flag(rssi_box, LV_OBJ_FLAG_SCROLLABLE);

    st->listen_rssi_arc = lv_arc_create(rssi_box);
    lv_obj_set_size(st->listen_rssi_arc, 48, 48);
    lv_obj_align(st->listen_rssi_arc, LV_ALIGN_TOP_MID, 0, 0);
    lv_arc_set_rotation(st->listen_rssi_arc, 135);
    lv_arc_set_bg_angles(st->listen_rssi_arc, 0, 270);
    lv_arc_set_range(st->listen_rssi_arc, 0, LISTEN_RSSI_ARC_MAX);
    lv_arc_set_value(st->listen_rssi_arc, 0);
    lv_obj_remove_style(st->listen_rssi_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(st->listen_rssi_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(st->listen_rssi_arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(st->listen_rssi_arc, subghz_host_ui_panel(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(st->listen_rssi_arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(st->listen_rssi_arc, subghz_host_ui_muted(), LV_PART_INDICATOR);

    st->listen_rssi_lbl = lv_label_create(rssi_box);
    lv_obj_align(st->listen_rssi_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(st->listen_rssi_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_text(st->listen_rssi_lbl, "--");
    lv_obj_set_style_text_color(st->listen_rssi_lbl, subghz_host_ui_muted(), 0);

    lv_obj_t *freq_btn = lv_btn_create(header);
    lv_obj_set_size(freq_btn, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_bg_color(freq_btn, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_color(freq_btn, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(freq_btn, 8, 0);
    lv_obj_set_style_pad_hor(freq_btn, 16, 0);
    lv_obj_add_event_cb(freq_btn, on_freq_tap, LV_EVENT_CLICKED, st);
    lv_obj_set_style_margin_right(freq_btn, 8, 0);

    subghz_add_header_action(header, LV_SYMBOL_SETTINGS, on_settings, NULL);
    subghz_add_radio_badge(header, st);

    st->listen_freq_lbl = lv_label_create(freq_btn);
    update_freq_label(st);
    lv_obj_set_style_text_color(st->listen_freq_lbl, subghz_host_color_pink(), 0);
    lv_obj_set_style_text_font(st->listen_freq_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(st->listen_freq_lbl);

    /* Control bar */
    lv_obj_t *ctrl = lv_obj_create(st->listen_page);
    lv_obj_set_size(ctrl, lv_pct(100), 60);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl, 0, 0);
    lv_obj_set_style_pad_all(ctrl, 4, 0);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);

    st->btn_start_stop = lv_btn_create(ctrl);
    lv_obj_set_size(st->btn_start_stop, 160, 50);
    lv_obj_set_style_bg_color(st->btn_start_stop, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(st->btn_start_stop, 8, 0);
    lv_obj_add_event_cb(st->btn_start_stop, on_start_stop, LV_EVENT_CLICKED, st);
    lv_obj_t *sl = lv_label_create(st->btn_start_stop);
    lv_label_set_text(sl, LV_SYMBOL_PLAY " Start");
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_18, 0);
    lv_obj_center(sl);

    lv_obj_t *mode_box = lv_obj_create(ctrl);
    lv_obj_set_size(mode_box, 240, 50);
    lv_obj_set_flex_flow(mode_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mode_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(mode_box, 0, 0);
    lv_obj_set_style_pad_gap(mode_box, 8, 0);
    lv_obj_set_style_bg_opa(mode_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mode_box, 0, 0);
    lv_obj_clear_flag(mode_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dec_lbl = lv_label_create(mode_box);
    lv_label_set_text(dec_lbl, "Decode");
    lv_obj_set_style_text_font(dec_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(dec_lbl,
        st->raw_mode ? subghz_host_ui_muted() : subghz_host_color_cyan(), 0);

    st->btn_raw = lv_switch_create(mode_box);
    lv_obj_set_size(st->btn_raw, 60, 32);
    lv_obj_set_style_bg_color(st->btn_raw, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_color(st->btn_raw, subghz_host_color_orange(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (st->raw_mode) lv_obj_add_state(st->btn_raw, LV_STATE_CHECKED);
    else              lv_obj_clear_state(st->btn_raw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(st->btn_raw, on_raw_toggle, LV_EVENT_VALUE_CHANGED, st);

    lv_obj_t *raw_lbl = lv_label_create(mode_box);
    lv_label_set_text(raw_lbl, "Raw");
    lv_obj_set_style_text_font(raw_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(raw_lbl,
        st->raw_mode ? subghz_host_color_orange() : subghz_host_ui_muted(), 0);

    st->count_lbl = lv_label_create(ctrl);
    lv_obj_set_style_text_font(st->count_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(st->count_lbl, subghz_host_color_cyan(), 0);
    lv_label_set_text(st->count_lbl, "Sig: 0");

    /* Waterfall canvas (RGB565) */
    size_t buf_sz = (size_t)WATERFALL_W * WATERFALL_H * sizeof(uint16_t);
    st->canvas_buf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!st->canvas_buf) st->canvas_buf = malloc(buf_sz);

    if (st->canvas_buf) {
        waterfall_fill_bg((uint16_t *)st->canvas_buf);
        st->canvas = lv_canvas_create(st->listen_page);
        lv_canvas_set_buffer(st->canvas, st->canvas_buf, WATERFALL_W, WATERFALL_H,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_size(st->canvas, WATERFALL_W, WATERFALL_H);
    }

    /* Header row for signal table */
    lv_obj_t *hdr = lv_obj_create(st->listen_page);
    lv_obj_set_size(hdr, lv_pct(100), 28);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(hdr, 4, 0);
    lv_obj_set_style_pad_gap(hdr, 6, 0);
    lv_obj_set_style_bg_color(hdr, subghz_host_ui_panel(), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    static const struct { const char *t; int w; } cols[] = {
        {"#", COL_IDX_W}, {"Type", COL_TYPE_W}, {"Freq", COL_FREQ_W},
        {"Signal", COL_MF_W}, {"Serial", COL_SER_W},
    };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *l = lv_label_create(hdr);
        lv_obj_set_width(l, cols[i].w);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, subghz_host_ui_muted(), 0);
        lv_label_set_text(l, cols[i].t);
    }

    /* Signal list */
    st->sig_list = lv_obj_create(st->listen_page);
    lv_obj_set_size(st->sig_list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(st->sig_list, 1);
    lv_obj_set_style_pad_all(st->sig_list, 0, 0);
    lv_obj_set_style_bg_color(st->sig_list, subghz_host_ui_bg(), 0);
    lv_obj_set_style_bg_opa(st->sig_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(st->sig_list, 0, 0);
    lv_obj_set_scrollbar_mode(st->sig_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(st->sig_list, on_signal_list_scroll, LV_EVENT_SCROLL, st);

    st->sig_spacer = lv_obj_create(st->sig_list);
    lv_obj_set_pos(st->sig_spacer, 0, 0);
    lv_obj_set_size(st->sig_spacer, 1, 1);
    lv_obj_set_style_bg_opa(st->sig_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->sig_spacer, 0, 0);
    lv_obj_clear_flag(st->sig_spacer, LV_OBJ_FLAG_SCROLLABLE);

    st->empty_lbl = lv_label_create(st->sig_list);
    lv_obj_set_pos(st->empty_lbl, 12, 12);
    lv_obj_set_style_text_color(st->empty_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(st->empty_lbl, &lv_font_montserrat_18, 0);
    lv_label_set_text(st->empty_lbl, "No signals captured");

    if (st->row_pool) {
        for (int i = 0; i < SIGNAL_ROW_POOL_SIZE; i++)
            configure_signal_row(st, &st->row_pool[i]);
    }

    refresh_signal_list_view(st);
    update_listen_rssi_gauge(st);

    if (!st->ui_timer)
        st->ui_timer = lv_timer_create(ui_tick_cb, WATERFALL_TICK_MS, st);

    /* Spawn background reader task for the page lifetime */
    st->listen_task_tab_id = subghz_host_current_tab();
    s_listen_page_alive = true;
    if (!st->listen_task)
        xTaskCreate(subghz_listen_task_fn, "sg_listen", 4096, st, 5, &st->listen_task);

    if (st->listen_pending_autostart) {
        st->listen_pending_autostart = false;
        start_listening(st);
    }

    ESP_LOGI(TAG, "SubGHz Listen page ready");
}

void show_subghz_listen_page_at(float mhz, bool autostart)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (st) {
        if (mhz >= 1.0f && mhz <= 1000.0f) st->freq_mhz = mhz;
        st->listen_pending_autostart = autostart;
    }
    show_subghz_listen_page();
}
