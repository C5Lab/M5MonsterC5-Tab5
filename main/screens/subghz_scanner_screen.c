/* Quick-Scan screen — 2x3 MRU grid of detected frequencies.
 *
 * Sends `subghz_scanner ...` built from NVS RF settings, parses
 * [SUBGHZ_SCAN_HIT] / [SUBGHZ_SCAN_PASS] into a 6-slot LRU.
 * Tapping a tile stops the scanner, sets the radio to that frequency
 * and hands off to Listen via show_subghz_listen_page_at(freq, true). */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_rf_settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "subghz_scanner";

#define MAX_FREQ_TILES   6
#define UART_RX_BUF_LEN  512
#define LINE_BUF_LEN     512

/* Concrete definition of scanner_freq_view_t (forward-declared in host header) */
struct scanner_freq_view {
    lv_obj_t *tile;
    lv_obj_t *freq_lbl;
    lv_obj_t *hint_lbl;
};

static volatile bool s_scanner_page_alive = false;
static portMUX_TYPE  s_scanner_lock = portMUX_INITIALIZER_UNLOCKED;

static void on_back(lv_event_t *e);
static void on_settings(lv_event_t *e);
static void on_tile_clicked(lv_event_t *e);
static void scanner_start_uart(void);
static void scanner_reader_task(void *arg);
static void process_line(subghz_tab_state_t *st, const char *line);
static void mru_insert_freq(subghz_tab_state_t *st, float freq);
static void refresh_tiles(subghz_tab_state_t *st);
static void ui_tick_cb(lv_timer_t *t);
static lv_obj_t *make_freq_tile(subghz_tab_state_t *st, lv_obj_t *parent, int idx);

static void stop_scanner(subghz_tab_state_t *st)
{
    if (!st || !st->scanner_running) return;
    st->scanner_running = false;
    subghz_host_uart_send("subghz_stop");
}

static void scanner_start_uart(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_rf_settings_t cfg;
    char cmd[96];
    subghz_rf_settings_load(&cfg);
    subghz_rf_build_scanner_cmd(&cfg, cmd, sizeof(cmd));
    if (cmd[0] == '\0')
        snprintf(cmd, sizeof(cmd), "subghz_scanner dwell=80 edges=4 -80 fast");

    subghz_host_uart_flush_input(subghz_host_current_tab());
    st->scanner_running = true;
    subghz_host_uart_send(cmd);
    ESP_LOGI(TAG, "Scanner UART: %s", cmd);
}

static void mru_insert_freq(subghz_tab_state_t *st, float freq)
{
    portENTER_CRITICAL(&s_scanner_lock);
    int existing = -1;
    for (int i = 0; i < MAX_FREQ_TILES; i++) {
        if (i < st->scanner_freq_count && fabsf(st->scanner_freqs[i] - freq) < 0.005f) {
            existing = i;
            break;
        }
    }
    int from = (existing >= 0) ? existing : (MAX_FREQ_TILES - 1);
    for (int i = from; i > 0; i--) {
        st->scanner_freqs[i] = st->scanner_freqs[i - 1];
    }
    st->scanner_freqs[0] = freq;
    if (st->scanner_freq_count < MAX_FREQ_TILES)
        st->scanner_freq_count = (existing < 0)
            ? (st->scanner_freq_count + 1 > MAX_FREQ_TILES
                ? MAX_FREQ_TILES : st->scanner_freq_count + 1)
            : st->scanner_freq_count;
    portEXIT_CRITICAL(&s_scanner_lock);
    st->scanner_dirty = true;
}

static void process_line(subghz_tab_state_t *st, const char *line)
{
    if (!st || !line) return;
    if (strstr(line, "[SUBGHZ_SCAN_PASS]")) {
        st->scanner_pulse = true;
        return;
    }
    const char *hit = strstr(line, "[SUBGHZ_SCAN_HIT] freq=");
    if (hit) {
        float freq = 0.0f;
        unsigned edges = 0;
        int rssi = 0;
        if (sscanf(hit, "[SUBGHZ_SCAN_HIT] freq=%f edges=%u rssi=%d",
                   &freq, &edges, &rssi) >= 1 && freq > 0.0f) {
            float rounded = roundf(freq * 100.0f) / 100.0f;
            mru_insert_freq(st, rounded);
        }
        return;
    }
}

static void scanner_reader_task(void *arg)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)arg;
    int tab_id = st->scanner_task_tab_id;
    static char rx_buf[UART_RX_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;
    ESP_LOGI(TAG, "Scanner reader task started for tab %d", tab_id);
    while (s_scanner_page_alive) {
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
                    process_line(st, line_buf);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }
    ESP_LOGI(TAG, "Scanner reader task ended");
    st->scanner_task = NULL;
    vTaskDelete(NULL);
}

static void refresh_tiles(subghz_tab_state_t *st)
{
    if (!st || !st->scanner_tiles) return;
    float snap[MAX_FREQ_TILES];
    int snap_n;
    portENTER_CRITICAL(&s_scanner_lock);
    memcpy(snap, st->scanner_freqs, sizeof(snap));
    snap_n = st->scanner_freq_count;
    portEXIT_CRITICAL(&s_scanner_lock);

    for (int i = 0; i < MAX_FREQ_TILES; i++) {
        struct scanner_freq_view *v = &st->scanner_tiles[i];
        if (!v->tile) continue;
        if (i < snap_n && snap[i] > 0.0f) {
            lv_label_set_text_fmt(v->freq_lbl, "%.2f MHz", snap[i]);
            lv_obj_set_style_text_color(v->freq_lbl, subghz_host_color_pink(), 0);
            if (v->hint_lbl) {
                lv_label_set_text(v->hint_lbl, "Tap to Listen");
                lv_obj_set_style_text_color(v->hint_lbl, subghz_host_ui_muted(), 0);
            }
            lv_obj_add_flag(v->tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(v->tile, subghz_host_ui_card(), LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(v->tile, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
        } else {
            lv_label_set_text(v->freq_lbl, "--");
            lv_obj_set_style_text_color(v->freq_lbl, subghz_host_ui_muted(), 0);
            if (v->hint_lbl) {
                lv_label_set_text(v->hint_lbl, "(empty)");
                lv_obj_set_style_text_color(v->hint_lbl, subghz_host_ui_muted(), 0);
            }
            lv_obj_clear_flag(v->tile, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(v->tile, subghz_host_ui_panel(), LV_STATE_DEFAULT);
        }
    }
}

static void ui_tick_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;
    if (st->radio_status_dirty) {
        st->radio_status_dirty = false;
        subghz_refresh_radio_badge(st);
    }
    bool pulse = st->scanner_pulse;
    bool dirty = st->scanner_dirty;
    if (!pulse && !dirty) return;
    st->scanner_pulse = false;
    st->scanner_dirty = false;
    if (pulse && st->scanner_pulse_dot) {
        static bool bright = true;
        bright = !bright;
        lv_obj_set_style_bg_opa(st->scanner_pulse_dot, bright ? LV_OPA_COVER : LV_OPA_20, 0);
    }
    if (dirty) refresh_tiles(st);
}

static void on_tile_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= MAX_FREQ_TILES) return;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    float freq;
    portENTER_CRITICAL(&s_scanner_lock);
    freq = (idx < st->scanner_freq_count) ? st->scanner_freqs[idx] : 0.0f;
    portEXIT_CRITICAL(&s_scanner_lock);
    if (freq <= 0.0f) return;

    stop_scanner(st);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_freq %.2f", freq);
    subghz_host_uart_send(cmd);

    /* Hand off to Listen at that freq + autostart. Listen rebuilds its
     * own page and steals UART control via its own reader task. */
    ESP_LOGI(TAG, "Tile clicked: %.2f MHz -> Listen", freq);
    /* Best-effort: cleanup our state first so reader doesn't fight Listen */
    /* The actual cleanup is done by listen page taking over via host_hide_all_pages. */
    subghz_scanner_cleanup(st);
    show_subghz_listen_page_at(freq, true);
}

static lv_obj_t *make_freq_tile(subghz_tab_state_t *st, lv_obj_t *parent, int idx)
{
    lv_obj_t *tile = lv_btn_create(parent);
    lv_obj_set_size(tile, 320, 130);
    lv_obj_set_style_bg_color(tile, subghz_host_ui_panel(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(tile, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 6, 0);
    lv_obj_set_style_pad_row(tile, 4, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *freq_lbl = lv_label_create(tile);
    lv_label_set_text(freq_lbl, "--");
    lv_obj_set_style_text_font(freq_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(freq_lbl, subghz_host_ui_muted(), 0);

    lv_obj_t *hint_lbl = lv_label_create(tile);
    lv_label_set_text(hint_lbl, "(empty)");
    lv_obj_set_style_text_font(hint_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint_lbl, subghz_host_ui_muted(), 0);

    lv_obj_add_event_cb(tile, on_tile_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)idx);

    st->scanner_tiles[idx].tile     = tile;
    st->scanner_tiles[idx].freq_lbl = freq_lbl;
    st->scanner_tiles[idx].hint_lbl = hint_lbl;
    return tile;
}

void subghz_scanner_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    stop_scanner(st);
    s_scanner_page_alive = false;
    for (int i = 0; i < 25 && st->scanner_task; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (st->scanner_ui_timer) { lv_timer_delete(st->scanner_ui_timer); st->scanner_ui_timer = NULL; }
    if (st->scanner_tiles) { free(st->scanner_tiles); st->scanner_tiles = NULL; }
    if (st->scanner_page) { lv_obj_delete(st->scanner_page); st->scanner_page = NULL; }
    st->scanner_pulse_dot = NULL;
    st->scanner_pass_lbl  = NULL;
    st->scanner_freq_count = 0;
    memset(st->scanner_freqs, 0, sizeof(st->scanner_freqs));
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_scanner_cleanup(st);
    show_subghz_page();
}

static void on_settings(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_scanner_cleanup(st);
    show_subghz_scanner_settings_page();
}

void show_subghz_scanner_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->scanner_page) { lv_obj_delete(st->scanner_page); st->scanner_page = NULL; }

    memset(st->scanner_freqs, 0, sizeof(st->scanner_freqs));
    st->scanner_freq_count = 0;
    st->scanner_pulse = false;
    st->scanner_dirty = true;
    st->scanner_running = false;

    if (!st->scanner_tiles) {
        st->scanner_tiles = (struct scanner_freq_view *)calloc(MAX_FREQ_TILES,
            sizeof(struct scanner_freq_view));
    } else {
        memset(st->scanner_tiles, 0, sizeof(struct scanner_freq_view) * MAX_FREQ_TILES);
    }

    st->scanner_page = lv_obj_create(container);
    lv_obj_set_size(st->scanner_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->scanner_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->scanner_page, 0, 0);
    lv_obj_set_style_pad_all(st->scanner_page, 10, 0);
    lv_obj_set_flex_flow(st->scanner_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->scanner_page, 8, 0);
    lv_obj_clear_flag(st->scanner_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = subghz_create_header(st->scanner_page, "Quick Scan",
                                            subghz_host_color_cyan(), on_back);
    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 1);

    st->scanner_pass_lbl = lv_label_create(header);
    lv_label_set_text(st->scanner_pass_lbl, "Scanning");
    lv_obj_set_style_text_font(st->scanner_pass_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(st->scanner_pass_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_margin_right(st->scanner_pass_lbl, 6, 0);

    st->scanner_pulse_dot = lv_obj_create(header);
    lv_obj_remove_style_all(st->scanner_pulse_dot);
    lv_obj_set_size(st->scanner_pulse_dot, 18, 18);
    lv_obj_set_style_bg_color(st->scanner_pulse_dot, subghz_host_color_red(), 0);
    lv_obj_set_style_bg_opa(st->scanner_pulse_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(st->scanner_pulse_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_margin_right(st->scanner_pulse_dot, 8, 0);

    subghz_add_header_action(header, LV_SYMBOL_SETTINGS, on_settings, NULL);
    subghz_add_radio_badge(header, st);

    lv_obj_t *grid = lv_obj_create(st->scanner_page);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAX_FREQ_TILES; i++) make_freq_tile(st, grid, i);
    refresh_tiles(st);

    st->scanner_task_tab_id = subghz_host_current_tab();
    s_scanner_page_alive = true;
    if (!st->scanner_task)
        xTaskCreate(scanner_reader_task, "sg_scan", 4096, st, 5, &st->scanner_task);
    if (!st->scanner_ui_timer)
        st->scanner_ui_timer = lv_timer_create(ui_tick_cb, 120, st);

    scanner_start_uart();

    ESP_LOGI(TAG, "Scanner page ready");
}
