/* SubGHz Settings — per-device frequency correction.
 *
 * Issues `subghz_get_freq_correction` on entry; a short-lived reader task
 * waits for the [SUBGHZ_FREQ_CORRECTION] response and pushes the value to
 * the LVGL rollers via the per-tab state struct. Tapping "Set" sends
 * `subghz_set_freq_correction <+0.dd>`. */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "subghz_settings";

#define UART_RX_BUF_LEN 512
#define LINE_BUF_LEN    512

static volatile bool s_settings_page_alive = false;

static const char *s_sign_opts  = "+\n-";
static const char *s_int_opts   = "0\n1\n2\n3\n4\n5";
static const char *s_digit_opts = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9";

static void on_back(lv_event_t *e);
static void on_set(lv_event_t *e);
static void process_line(subghz_tab_state_t *st, const char *line);
static void settings_reader_task(void *arg);
static void ui_tick_cb(lv_timer_t *t);

static void decompose(float v, int *sign_idx, int d[3])
{
    int centi = (int)lroundf(fabsf(v) * 100.0f);
    if (centi > 500) centi = 500;
    *sign_idx = (v < 0.0f) ? 1 : 0;
    d[0] = (centi / 100) % 10;
    d[1] = (centi / 10) % 10;
    d[2] = centi % 10;
}

static float compose_from_rollers(subghz_tab_state_t *st)
{
    int sign_idx = (int)lv_roller_get_selected(st->settings_rollers[0]);
    int d0 = (int)lv_roller_get_selected(st->settings_rollers[1]);
    int d1 = (int)lv_roller_get_selected(st->settings_rollers[2]);
    int d2 = (int)lv_roller_get_selected(st->settings_rollers[3]);
    float v = d0 + d1 * 0.1f + d2 * 0.01f;
    if (v > 5.0f) v = 5.0f;
    return sign_idx ? -v : v;
}

static void apply_value_to_rollers(subghz_tab_state_t *st, float v)
{
    int sign_idx;
    int d[3];
    decompose(v, &sign_idx, d);
    if (st->settings_rollers[0]) lv_roller_set_selected(st->settings_rollers[0], sign_idx, LV_ANIM_OFF);
    if (st->settings_rollers[1]) lv_roller_set_selected(st->settings_rollers[1], d[0], LV_ANIM_OFF);
    if (st->settings_rollers[2]) lv_roller_set_selected(st->settings_rollers[2], d[1], LV_ANIM_OFF);
    if (st->settings_rollers[3]) lv_roller_set_selected(st->settings_rollers[3], d[2], LV_ANIM_OFF);
}

static void process_line(subghz_tab_state_t *st, const char *line)
{
    if (!st || !line) return;
    const char *tag = strstr(line, "[SUBGHZ_FREQ_CORRECTION]");
    if (!tag) return;
    const char *p = tag + strlen("[SUBGHZ_FREQ_CORRECTION]");
    while (*p == ' ') p++;
    float v = strtof(p, NULL);
    if (v >  5.0f) v =  5.0f;
    if (v < -5.0f) v = -5.0f;
    st->settings_pending_correction = v;
    st->settings_correction_loaded = true;
}

static void settings_reader_task(void *arg)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)arg;
    int tab_id = st->settings_task_tab_id;
    static char rx_buf[UART_RX_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;
    ESP_LOGI(TAG, "Settings reader task started for tab %d", tab_id);
    while (s_settings_page_alive) {
        int len = subghz_host_uart_read_bytes(tab_id, rx_buf, sizeof(rx_buf) - 1,
                                              pdMS_TO_TICKS(100));
        if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        rx_buf[len] = '\0';
        for (int i = 0; i < len; i++) {
            char c = rx_buf[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    process_line(st, line_buf);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }
    ESP_LOGI(TAG, "Settings reader task ended");
    st->settings_task = NULL;
    vTaskDelete(NULL);
}

static void ui_tick_cb(lv_timer_t *t)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)lv_timer_get_user_data(t);
    if (!st) return;
    if (st->settings_correction_loaded) {
        st->settings_correction_loaded = false;
        apply_value_to_rollers(st, st->settings_pending_correction);
        if (st->settings_status_lbl) {
            char msg[40];
            snprintf(msg, sizeof(msg), "Loaded %+.2f MHz", st->settings_pending_correction);
            lv_label_set_text(st->settings_status_lbl, msg);
            lv_obj_set_style_text_color(st->settings_status_lbl, subghz_host_color_cyan(), 0);
        }
        if (st->settings_set_btn)
            lv_obj_clear_state(st->settings_set_btn, LV_STATE_DISABLED);
    }
}

static void on_set(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    float v = compose_from_rollers(st);
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "subghz_set_freq_correction %+.2f", v);
    subghz_host_uart_send(cmd);
    if (st->settings_status_lbl) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Saved %+.2f MHz", v);
        lv_label_set_text(st->settings_status_lbl, msg);
        lv_obj_set_style_text_color(st->settings_status_lbl, subghz_host_color_green(), 0);
    }
    ESP_LOGI(TAG, "Set freq correction %+.2f MHz", v);
}

void subghz_settings_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    s_settings_page_alive = false;
    for (int i = 0; i < 25 && st->settings_task; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (st->settings_ui_timer) { lv_timer_delete(st->settings_ui_timer); st->settings_ui_timer = NULL; }
    if (st->settings_page) { lv_obj_delete(st->settings_page); st->settings_page = NULL; }
    for (int i = 0; i < 4; i++) st->settings_rollers[i] = NULL;
    st->settings_status_lbl = NULL;
    st->settings_set_btn = NULL;
    st->settings_correction_loaded = false;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_settings_cleanup(st);
    show_subghz_page();
}

static void style_roller(lv_obj_t *r, lv_coord_t width)
{
    lv_obj_set_width(r, width);
    lv_obj_set_height(r, 150);
    lv_obj_set_style_bg_color(r, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(r, subghz_host_color_cyan(), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, subghz_host_ui_panel(), LV_PART_SELECTED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 8, 0);
}

void show_subghz_settings_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->settings_page) { lv_obj_delete(st->settings_page); st->settings_page = NULL; }
    for (int i = 0; i < 4; i++) st->settings_rollers[i] = NULL;
    st->settings_correction_loaded = false;
    st->settings_pending_correction = 0.0f;

    st->settings_page = lv_obj_create(container);
    lv_obj_set_size(st->settings_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->settings_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->settings_page, 0, 0);
    lv_obj_set_style_pad_all(st->settings_page, 10, 0);
    lv_obj_set_flex_flow(st->settings_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->settings_page, 8, 0);
    lv_obj_clear_flag(st->settings_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->settings_page, "SubGHz Settings",
                         subghz_host_ui_muted(), on_back);

    lv_obj_t *card = lv_obj_create(st->settings_page);
    lv_obj_set_size(card, lv_pct(80), 360);
    lv_obj_set_style_bg_color(card, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Frequency Correction (MHz)");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, lv_pct(100), 160);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row, 6, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    st->settings_rollers[0] = lv_roller_create(row);
    lv_roller_set_options(st->settings_rollers[0], s_sign_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(st->settings_rollers[0], 3);
    style_roller(st->settings_rollers[0], 60);

    st->settings_rollers[1] = lv_roller_create(row);
    lv_roller_set_options(st->settings_rollers[1], s_int_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(st->settings_rollers[1], 3);
    style_roller(st->settings_rollers[1], 70);

    lv_obj_t *dot = lv_label_create(row);
    lv_label_set_text(dot, ".");
    lv_obj_set_style_text_font(dot, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(dot, subghz_host_ui_text(), 0);

    st->settings_rollers[2] = lv_roller_create(row);
    lv_roller_set_options(st->settings_rollers[2], s_digit_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(st->settings_rollers[2], 3);
    style_roller(st->settings_rollers[2], 70);

    st->settings_rollers[3] = lv_roller_create(row);
    lv_roller_set_options(st->settings_rollers[3], s_digit_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(st->settings_rollers[3], 3);
    style_roller(st->settings_rollers[3], 70);

    apply_value_to_rollers(st, 0.0f);

    st->settings_set_btn = lv_btn_create(card);
    lv_obj_set_size(st->settings_set_btn, 200, 50);
    lv_obj_set_style_bg_color(st->settings_set_btn, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(st->settings_set_btn, 8, 0);
    lv_obj_add_event_cb(st->settings_set_btn, on_set, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(st->settings_set_btn, LV_STATE_DISABLED);
    lv_obj_t *sl = lv_label_create(st->settings_set_btn);
    lv_label_set_text(sl, "Set");
    lv_obj_set_style_text_color(sl, lv_color_white(), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_18, 0);
    lv_obj_center(sl);

    st->settings_status_lbl = lv_label_create(card);
    lv_label_set_text(st->settings_status_lbl, "Loading...");
    lv_obj_set_style_text_color(st->settings_status_lbl, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(st->settings_status_lbl, &lv_font_montserrat_18, 0);

    /* Spawn short-lived reader + query */
    st->settings_task_tab_id = subghz_host_current_tab();
    s_settings_page_alive = true;
    subghz_host_uart_flush_input(subghz_host_current_tab());
    if (!st->settings_task)
        xTaskCreate(settings_reader_task, "sg_set", 4096, st, 5, &st->settings_task);
    subghz_host_uart_send("subghz_get_freq_correction");
    if (!st->settings_ui_timer)
        st->settings_ui_timer = lv_timer_create(ui_tick_cb, 100, st);

    ESP_LOGI(TAG, "SubGHz settings page ready");
}
