#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "subghz_jam";

/* Frequency presets (MHz). Values stored in static so we can pass pointers via user_data. */
static float s_p315  = 315.00f;
static float s_p433  = 433.92f;
static float s_p868  = 868.00f;
static float s_p915  = 915.00f;

static void on_back(lv_event_t *e);

static void close_freq_popup(subghz_tab_state_t *st)
{
    if (st->jammer_freq_popup) {
        lv_obj_delete(st->jammer_freq_popup);
        st->jammer_freq_popup = NULL;
    }
}

static void update_jammer_freq_label(subghz_tab_state_t *st)
{
    if (!st->jammer_freq_lbl) return;
    int whole = (int)st->freq_mhz;
    int frac  = ((int)(st->freq_mhz * 100.0f + 0.5f)) % 100;
    lv_label_set_text_fmt(st->jammer_freq_lbl, "%d.%02d MHz", whole, frac);
}

static void on_freq_preset(lv_event_t *e)
{
    subghz_tab_state_t *st = subghz_host_state();
    float *freq = (float *)lv_event_get_user_data(e);
    if (!st || !freq) return;
    st->freq_mhz = *freq;
    update_jammer_freq_label(st);
    close_freq_popup(st);
}

static void on_freq_popup_close(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (st) close_freq_popup(st);
}

static void on_freq_tap(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st || st->jamming) return;
    if (st->jammer_freq_popup) { close_freq_popup(st); return; }

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    st->jammer_freq_popup = overlay;

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, 460, 280);
    lv_obj_center(popup);
    subghz_style_popup_card(popup, 12, subghz_host_color_red());
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(popup, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(popup, 14, 0);
    lv_obj_set_style_pad_gap(popup, 12, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, "Frequency (MHz)");
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    static const struct { const char *txt; float *val; } presets[] = {
        {"315",    &s_p315},
        {"433.92", &s_p433},
        {"868",    &s_p868},
        {"915",    &s_p915},
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_btn_create(popup);
        lv_obj_set_size(btn, 180, 60);
        lv_obj_set_style_bg_color(btn, subghz_host_ui_card(), 0);
        lv_obj_set_style_bg_color(btn, subghz_host_ui_card_pressed(), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_add_event_cb(btn, on_freq_preset, LV_EVENT_CLICKED, presets[i].val);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, presets[i].txt);
        lv_obj_set_style_text_color(lbl, subghz_host_ui_text(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
        lv_obj_center(lbl);
    }

    lv_obj_t *close_btn = lv_btn_create(popup);
    lv_obj_set_size(close_btn, 380, 50);
    lv_obj_set_style_bg_color(close_btn, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, on_freq_popup_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "Close");
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);
}

static void stop_jamming(subghz_tab_state_t *st)
{
    if (!st->jamming) return;
    st->jamming = false;
    subghz_host_uart_send("subghz_stop");

    if (st->jammer_status_lbl) {
        lv_label_set_text(st->jammer_status_lbl, "Idle");
        lv_obj_set_style_text_color(st->jammer_status_lbl, subghz_host_ui_muted(), 0);
    }
    if (st->jammer_big_btn)
        lv_obj_set_style_bg_color(st->jammer_big_btn, subghz_host_color_red(), 0);
    if (st->jammer_big_btn_lbl)
        lv_label_set_text(st->jammer_big_btn_lbl, LV_SYMBOL_WARNING " START JAM");

    ESP_LOGI(TAG, "Jammer stopped");
}

static void on_big_btn(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;

    if (st->jamming) { stop_jamming(st); return; }

    st->jamming = true;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "subghz_freq %.2f", st->freq_mhz);
    subghz_host_uart_send(cmd);
    subghz_host_uart_send("subghz_jam");

    if (st->jammer_status_lbl) {
        lv_label_set_text_fmt(st->jammer_status_lbl, "Jamming %d.%02d MHz...",
                              (int)st->freq_mhz,
                              ((int)(st->freq_mhz * 100.0f + 0.5f)) % 100);
        lv_obj_set_style_text_color(st->jammer_status_lbl, subghz_host_color_red(), 0);
    }
    if (st->jammer_big_btn)
        lv_obj_set_style_bg_color(st->jammer_big_btn, lv_color_hex(0x8B0000), 0);
    if (st->jammer_big_btn_lbl)
        lv_label_set_text(st->jammer_big_btn_lbl, LV_SYMBOL_STOP " STOP");

    ESP_LOGI(TAG, "Jammer started on %.2f MHz", st->freq_mhz);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    stop_jamming(st);
    if (st->jammer_freq_popup) { lv_obj_delete(st->jammer_freq_popup); st->jammer_freq_popup = NULL; }
    if (st->jammer_page) { lv_obj_delete(st->jammer_page); st->jammer_page = NULL; }
    st->jammer_status_lbl = NULL;
    st->jammer_freq_lbl = NULL;
    st->jammer_big_btn = NULL;
    st->jammer_big_btn_lbl = NULL;
    show_subghz_page();
}

void show_subghz_jammer_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->jammer_page) {
        lv_obj_clear_flag(st->jammer_page, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (st->freq_mhz < 1.0f) st->freq_mhz = 433.92f;

    st->jammer_page = lv_obj_create(container);
    lv_obj_set_size(st->jammer_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->jammer_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->jammer_page, 0, 0);
    lv_obj_set_style_pad_all(st->jammer_page, 10, 0);
    lv_obj_set_flex_flow(st->jammer_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->jammer_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(st->jammer_page, 24, 0);
    lv_obj_clear_flag(st->jammer_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->jammer_page, "Jammer", subghz_host_color_red(), on_back);

    /* Frequency label (tappable) */
    st->jammer_freq_lbl = lv_label_create(st->jammer_page);
    lv_obj_set_style_text_font(st->jammer_freq_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(st->jammer_freq_lbl, subghz_host_color_red(), 0);
    update_jammer_freq_label(st);
    lv_obj_add_flag(st->jammer_freq_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(st->jammer_freq_lbl, on_freq_tap, LV_EVENT_CLICKED, NULL);

    /* Big start/stop button */
    st->jammer_big_btn = lv_btn_create(st->jammer_page);
    lv_obj_set_size(st->jammer_big_btn, 380, 100);
    lv_obj_set_style_bg_color(st->jammer_big_btn, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(st->jammer_big_btn, 16, 0);
    lv_obj_add_event_cb(st->jammer_big_btn, on_big_btn, LV_EVENT_CLICKED, NULL);

    st->jammer_big_btn_lbl = lv_label_create(st->jammer_big_btn);
    lv_label_set_text(st->jammer_big_btn_lbl, LV_SYMBOL_WARNING " START JAM");
    lv_obj_set_style_text_font(st->jammer_big_btn_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(st->jammer_big_btn_lbl, lv_color_white(), 0);
    lv_obj_center(st->jammer_big_btn_lbl);

    /* Status */
    st->jammer_status_lbl = lv_label_create(st->jammer_page);
    lv_obj_set_style_text_font(st->jammer_status_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(st->jammer_status_lbl, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->jammer_status_lbl, "Idle");

    ESP_LOGI(TAG, "SubGHz Jammer page ready");
}
