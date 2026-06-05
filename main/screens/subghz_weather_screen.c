/* Weather sensor listener — 2x4 tile grid keyed by (proto, id, ch).
 *
 * Sends `subghz_weather`, parses [SUBGHZ_WEATHER_START] / [SUBGHZ_WEATHER]
 * into an LRU-evicted slot table (8 max). Updates tiles with proto, temp,
 * humidity, battery, age. */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "subghz_weather";

#define MAX_SENSORS  8
#define UART_RX_BUF_LEN 512
#define LINE_BUF_LEN    512

struct subghz_weather_sensor {
    bool          used;
    char          proto[24];
    unsigned long id;
    char          ch[8];
    char          temp[16];
    char          hum[8];
    char          batt[8];
    TickType_t    last_seen;
    /* UI handles for this slot */
    lv_obj_t     *tile;
    lv_obj_t     *empty_lbl;
    lv_obj_t     *proto_lbl;
    lv_obj_t     *age_lbl;
    lv_obj_t     *temp_lbl;
    lv_obj_t     *hum_lbl;
    lv_obj_t     *batt_lbl;
};

static volatile bool s_weather_page_alive = false;
static portMUX_TYPE  s_weather_lock = portMUX_INITIALIZER_UNLOCKED;

static void on_back(lv_event_t *e);
static void process_line(subghz_tab_state_t *st, const char *line);
static void weather_reader_task(void *arg);
static void refresh_tiles(subghz_tab_state_t *st);
static void refresh_ages(subghz_tab_state_t *st);
static void update_status_label(subghz_tab_state_t *st, int sensor_count);
static void ui_tick_cb(lv_timer_t *t);

static lv_color_t batt_color(const char *batt)
{
    if (strcmp(batt, "ok")  == 0) return subghz_host_color_green();
    if (strcmp(batt, "low") == 0) return subghz_host_color_orange();
    return subghz_host_ui_muted();
}

static int find_sensor_slot_unlocked(subghz_tab_state_t *st,
                                     const char *proto, unsigned long id,
                                     const char *ch)
{
    for (int i = 0; i < MAX_SENSORS; i++) {
        subghz_weather_sensor_t *s = &st->weather_sensors[i];
        if (s->used && s->id == id &&
            strncmp(s->proto, proto, sizeof(s->proto)) == 0 &&
            strncmp(s->ch, ch, sizeof(s->ch)) == 0) {
            return i;
        }
    }
    return -1;
}

static int pick_lru_slot_unlocked(subghz_tab_state_t *st)
{
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!st->weather_sensors[i].used) return i;
    }
    int lru = 0;
    for (int i = 1; i < MAX_SENSORS; i++) {
        if ((int32_t)(st->weather_sensors[i].last_seen -
                      st->weather_sensors[lru].last_seen) < 0) {
            lru = i;
        }
    }
    return lru;
}

static void weather_upsert(subghz_tab_state_t *st,
                           const char *proto, unsigned long id, const char *ch,
                           const char *temp, const char *hum, const char *batt)
{
    portENTER_CRITICAL(&s_weather_lock);
    int slot = find_sensor_slot_unlocked(st, proto, id, ch);
    if (slot < 0) slot = pick_lru_slot_unlocked(st);

    subghz_weather_sensor_t *s = &st->weather_sensors[slot];
    bool was_used = s->used;
    s->used = true;
    s->id = id;
    snprintf(s->proto, sizeof(s->proto), "%s", proto);
    snprintf(s->ch,    sizeof(s->ch),    "%s", ch);
    snprintf(s->temp,  sizeof(s->temp),  "%s", temp);
    snprintf(s->hum,   sizeof(s->hum),   "%s", hum);
    snprintf(s->batt,  sizeof(s->batt),  "%s", batt);
    s->last_seen = xTaskGetTickCount();
    if (!was_used) st->weather_count++;
    portEXIT_CRITICAL(&s_weather_lock);

    st->weather_pulse = true;
    st->weather_dirty = true;
}

static void process_line(subghz_tab_state_t *st, const char *line)
{
    if (!st || !line) return;
    if (strstr(line, "[SUBGHZ_WEATHER_START]")) {
        float f = 0.0f;
        if (sscanf(line, "[SUBGHZ_WEATHER_START] freq=%f", &f) == 1)
            st->weather_freq = f;
        st->weather_dirty = true;
        return;
    }
    const char *p = strstr(line, "[SUBGHZ_WEATHER] proto=");
    if (!p) return;

    char proto[24] = {0};
    char ch[8]     = {0};
    char temp[16]  = {0};
    char hum[8]    = {0};
    char batt[8]   = {0};
    unsigned long id = 0;
    if (sscanf(p,
               "[SUBGHZ_WEATHER] proto=%23s id=0x%lX ch=%7s temp=%15s hum=%7s batt=%7s",
               proto, &id, ch, temp, hum, batt) == 6) {
        weather_upsert(st, proto, id, ch, temp, hum, batt);
    }
}

static void weather_reader_task(void *arg)
{
    subghz_tab_state_t *st = (subghz_tab_state_t *)arg;
    int tab_id = st->weather_task_tab_id;
    static char rx_buf[UART_RX_BUF_LEN];
    static char line_buf[LINE_BUF_LEN];
    int line_pos = 0;

    ESP_LOGI(TAG, "Weather reader task started for tab %d", tab_id);
    while (s_weather_page_alive) {
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
    ESP_LOGI(TAG, "Weather reader task ended");
    st->weather_task = NULL;
    vTaskDelete(NULL);
}

static void apply_sensor_to_tile(subghz_weather_sensor_t *s)
{
    if (!s->tile) return;
    if (!s->used) {
        lv_obj_set_style_bg_color(s->tile, subghz_host_ui_panel(), LV_STATE_DEFAULT);
        if (s->proto_lbl) lv_obj_add_flag(s->proto_lbl, LV_OBJ_FLAG_HIDDEN);
        if (s->age_lbl)   lv_obj_add_flag(s->age_lbl,   LV_OBJ_FLAG_HIDDEN);
        if (s->temp_lbl)  lv_obj_add_flag(s->temp_lbl,  LV_OBJ_FLAG_HIDDEN);
        if (s->hum_lbl)   lv_obj_add_flag(s->hum_lbl,   LV_OBJ_FLAG_HIDDEN);
        if (s->batt_lbl)  lv_obj_add_flag(s->batt_lbl,  LV_OBJ_FLAG_HIDDEN);
        if (s->empty_lbl) lv_obj_clear_flag(s->empty_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_style_bg_color(s->tile, subghz_host_ui_card(), LV_STATE_DEFAULT);
    if (s->empty_lbl) lv_obj_add_flag(s->empty_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s->proto_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s->age_lbl,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s->temp_lbl,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s->hum_lbl,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s->batt_lbl,  LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(s->proto_lbl, s->proto);
    if (strcmp(s->temp, "-") == 0) lv_label_set_text(s->temp_lbl, "-- C");
    else lv_label_set_text_fmt(s->temp_lbl, "%s C", s->temp);
    if (strcmp(s->hum, "-") == 0) lv_label_set_text(s->hum_lbl, "--");
    else lv_label_set_text_fmt(s->hum_lbl, "%s%%", s->hum);
    lv_label_set_text(s->batt_lbl, s->batt);
    lv_obj_set_style_text_color(s->batt_lbl, batt_color(s->batt), 0);
}

static void refresh_tiles(subghz_tab_state_t *st)
{
    if (!st || !st->weather_sensors) return;
    int count = 0;
    /* Snapshot under lock then apply */
    subghz_weather_sensor_t snap[MAX_SENSORS];
    portENTER_CRITICAL(&s_weather_lock);
    memcpy(snap, st->weather_sensors, sizeof(snap));
    portEXIT_CRITICAL(&s_weather_lock);

    for (int i = 0; i < MAX_SENSORS; i++) {
        /* Copy *back* the UI handles, since snap[] doesn't really own them. */
        snap[i].tile      = st->weather_sensors[i].tile;
        snap[i].empty_lbl = st->weather_sensors[i].empty_lbl;
        snap[i].proto_lbl = st->weather_sensors[i].proto_lbl;
        snap[i].age_lbl   = st->weather_sensors[i].age_lbl;
        snap[i].temp_lbl  = st->weather_sensors[i].temp_lbl;
        snap[i].hum_lbl   = st->weather_sensors[i].hum_lbl;
        snap[i].batt_lbl  = st->weather_sensors[i].batt_lbl;
        if (snap[i].used) count++;
        apply_sensor_to_tile(&snap[i]);
    }
    update_status_label(st, count);
}

static void format_age(uint32_t secs, char *buf, size_t sz)
{
    if (secs < 60)         snprintf(buf, sz, "%us", (unsigned)secs);
    else if (secs < 3600)  snprintf(buf, sz, "%um", (unsigned)(secs / 60));
    else if (secs < 86400) snprintf(buf, sz, "%uh", (unsigned)(secs / 3600));
    else                   snprintf(buf, sz, "%ud", (unsigned)(secs / 86400));
}

static void refresh_ages(subghz_tab_state_t *st)
{
    if (!st || !st->weather_sensors) return;
    TickType_t now = xTaskGetTickCount();
    struct { bool used; TickType_t last; lv_obj_t *age_lbl; } snap[MAX_SENSORS];
    portENTER_CRITICAL(&s_weather_lock);
    for (int i = 0; i < MAX_SENSORS; i++) {
        snap[i].used = st->weather_sensors[i].used;
        snap[i].last = st->weather_sensors[i].last_seen;
        snap[i].age_lbl = st->weather_sensors[i].age_lbl;
    }
    portEXIT_CRITICAL(&s_weather_lock);

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!snap[i].age_lbl || !snap[i].used) continue;
        uint32_t ms = (uint32_t)((now - snap[i].last) * portTICK_PERIOD_MS);
        char buf[12];
        format_age(ms / 1000U, buf, sizeof(buf));
        lv_label_set_text(snap[i].age_lbl, buf);
    }
}

static void update_status_label(subghz_tab_state_t *st, int sensor_count)
{
    if (!st || !st->weather_status_lbl) return;
    if (st->weather_freq > 0.0f) {
        lv_label_set_text_fmt(st->weather_status_lbl,
                              "%.2f MHz  -  %d sensor%s",
                              st->weather_freq, sensor_count,
                              sensor_count == 1 ? "" : "s");
    } else {
        lv_label_set_text_fmt(st->weather_status_lbl,
                              "Listening...  -  %d sensor%s",
                              sensor_count, sensor_count == 1 ? "" : "s");
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
    bool pulse = st->weather_pulse;
    bool dirty = st->weather_dirty;
    st->weather_pulse = false;
    st->weather_dirty = false;
    if (pulse && st->weather_pulse_dot) {
        static bool bright = true;
        bright = !bright;
        lv_obj_set_style_bg_opa(st->weather_pulse_dot, bright ? LV_OPA_COVER : LV_OPA_20, 0);
    }
    if (dirty) refresh_tiles(st);
    refresh_ages(st);
}

static void build_tile(subghz_tab_state_t *st, lv_obj_t *parent, int idx)
{
    subghz_weather_sensor_t *s = &st->weather_sensors[idx];

    s->tile = lv_obj_create(parent);
    lv_obj_set_size(s->tile, 240, 130);
    lv_obj_set_style_bg_color(s->tile, subghz_host_ui_panel(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s->tile, 0, 0);
    lv_obj_set_style_radius(s->tile, 10, 0);
    lv_obj_set_style_shadow_width(s->tile, 0, 0);
    lv_obj_set_style_pad_all(s->tile, 6, 0);
    lv_obj_clear_flag(s->tile, LV_OBJ_FLAG_SCROLLABLE);

    s->empty_lbl = lv_label_create(s->tile);
    lv_label_set_text(s->empty_lbl, "--");
    lv_obj_set_style_text_font(s->empty_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s->empty_lbl, subghz_host_ui_muted(), 0);
    lv_obj_center(s->empty_lbl);

    s->proto_lbl = lv_label_create(s->tile);
    lv_label_set_text(s->proto_lbl, "");
    lv_obj_set_style_text_font(s->proto_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s->proto_lbl, subghz_host_color_cyan(), 0);
    lv_obj_set_pos(s->proto_lbl, 4, 4);
    lv_obj_set_width(s->proto_lbl, 160);
    lv_label_set_long_mode(s->proto_lbl, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(s->proto_lbl, LV_OBJ_FLAG_HIDDEN);

    s->age_lbl = lv_label_create(s->tile);
    lv_label_set_text(s->age_lbl, "");
    lv_obj_set_style_text_font(s->age_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s->age_lbl, subghz_host_ui_muted(), 0);
    lv_obj_align(s->age_lbl, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_flag(s->age_lbl, LV_OBJ_FLAG_HIDDEN);

    s->temp_lbl = lv_label_create(s->tile);
    lv_label_set_text(s->temp_lbl, "");
    lv_obj_set_style_text_font(s->temp_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s->temp_lbl, subghz_host_ui_text(), 0);
    lv_obj_align(s->temp_lbl, LV_ALIGN_BOTTOM_LEFT, 4, -6);
    lv_obj_add_flag(s->temp_lbl, LV_OBJ_FLAG_HIDDEN);

    s->hum_lbl = lv_label_create(s->tile);
    lv_label_set_text(s->hum_lbl, "");
    lv_obj_set_style_text_font(s->hum_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s->hum_lbl, subghz_host_ui_muted(), 0);
    lv_obj_align(s->hum_lbl, LV_ALIGN_BOTTOM_MID, 24, -8);
    lv_obj_add_flag(s->hum_lbl, LV_OBJ_FLAG_HIDDEN);

    s->batt_lbl = lv_label_create(s->tile);
    lv_label_set_text(s->batt_lbl, "");
    lv_obj_set_style_text_font(s->batt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s->batt_lbl, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_add_flag(s->batt_lbl, LV_OBJ_FLAG_HIDDEN);
}

void subghz_weather_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->weather_running) {
        st->weather_running = false;
        subghz_host_uart_send("subghz_stop");
    }
    s_weather_page_alive = false;
    for (int i = 0; i < 25 && st->weather_task; i++) vTaskDelay(pdMS_TO_TICKS(20));
    if (st->weather_ui_timer) { lv_timer_delete(st->weather_ui_timer); st->weather_ui_timer = NULL; }
    if (st->weather_sensors) { free(st->weather_sensors); st->weather_sensors = NULL; }
    if (st->weather_page) { lv_obj_delete(st->weather_page); st->weather_page = NULL; }
    st->weather_count = 0;
    st->weather_pulse_dot = NULL;
    st->weather_status_lbl = NULL;
    st->weather_tiles_grid = NULL;
    st->weather_freq = 0.0f;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    subghz_weather_cleanup(st);
    show_subghz_page();
}

void show_subghz_weather_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->weather_page) { lv_obj_delete(st->weather_page); st->weather_page = NULL; }

    if (!st->weather_sensors)
        st->weather_sensors = (subghz_weather_sensor_t *)calloc(MAX_SENSORS,
            sizeof(subghz_weather_sensor_t));
    else
        memset(st->weather_sensors, 0, sizeof(subghz_weather_sensor_t) * MAX_SENSORS);
    st->weather_count = 0;
    st->weather_freq = 0.0f;
    st->weather_dirty = true;
    st->weather_pulse = false;

    st->weather_page = lv_obj_create(container);
    lv_obj_set_size(st->weather_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->weather_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->weather_page, 0, 0);
    lv_obj_set_style_pad_all(st->weather_page, 10, 0);
    lv_obj_set_flex_flow(st->weather_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->weather_page, 8, 0);
    lv_obj_clear_flag(st->weather_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = subghz_create_header(st->weather_page, "Weather",
                                            subghz_host_color_blue(), on_back);
    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 1);
    st->weather_pulse_dot = lv_obj_create(header);
    lv_obj_remove_style_all(st->weather_pulse_dot);
    lv_obj_set_size(st->weather_pulse_dot, 18, 18);
    lv_obj_set_style_bg_color(st->weather_pulse_dot, subghz_host_color_green(), 0);
    lv_obj_set_style_bg_opa(st->weather_pulse_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(st->weather_pulse_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_margin_right(st->weather_pulse_dot, 8, 0);
    subghz_add_radio_badge(header, st);

    st->weather_status_lbl = lv_label_create(st->weather_page);
    lv_obj_set_style_text_font(st->weather_status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(st->weather_status_lbl, subghz_host_color_cyan(), 0);
    lv_label_set_text(st->weather_status_lbl, "Listening...  -  0 sensors");

    st->weather_tiles_grid = lv_obj_create(st->weather_page);
    lv_obj_set_size(st->weather_tiles_grid, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(st->weather_tiles_grid, 1);
    lv_obj_set_style_bg_opa(st->weather_tiles_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(st->weather_tiles_grid, 0, 0);
    lv_obj_set_style_pad_all(st->weather_tiles_grid, 6, 0);
    lv_obj_set_style_pad_row(st->weather_tiles_grid, 8, 0);
    lv_obj_set_style_pad_column(st->weather_tiles_grid, 8, 0);
    lv_obj_set_flex_flow(st->weather_tiles_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(st->weather_tiles_grid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(st->weather_tiles_grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAX_SENSORS; i++) build_tile(st, st->weather_tiles_grid, i);
    refresh_tiles(st);

    st->weather_task_tab_id = subghz_host_current_tab();
    s_weather_page_alive = true;
    if (!st->weather_task)
        xTaskCreate(weather_reader_task, "sg_weather", 4096, st, 5, &st->weather_task);
    if (!st->weather_ui_timer)
        st->weather_ui_timer = lv_timer_create(ui_tick_cb, 250, st);

    /* Start the firmware-side weather decoder */
    subghz_host_uart_flush_input(subghz_host_current_tab());
    st->weather_running = true;
    subghz_host_uart_send("subghz_weather");

    ESP_LOGI(TAG, "Weather page ready");
}
