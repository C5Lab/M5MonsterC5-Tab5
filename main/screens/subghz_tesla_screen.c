#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "subghz_tesla";

static void on_back(lv_event_t *e);

static void on_open_port(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();

    subghz_host_uart_send("subghz_freq 315.00");
    subghz_host_uart_send("subghz_tx tesla");

    if (st && st->tesla_status_lbl) {
        lv_label_set_text(st->tesla_status_lbl, "Signal sent!");
        lv_obj_set_style_text_color(st->tesla_status_lbl, subghz_host_color_green(), 0);
    }
    ESP_LOGI(TAG, "Tesla charge port signal sent (315 MHz)");
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (!st) return;
    if (st->tesla_page) { lv_obj_delete(st->tesla_page); st->tesla_page = NULL; }
    st->tesla_status_lbl = NULL;
    show_subghz_page();
}

void show_subghz_tesla_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->tesla_page) {
        lv_obj_clear_flag(st->tesla_page, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    st->tesla_page = lv_obj_create(container);
    lv_obj_set_size(st->tesla_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->tesla_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->tesla_page, 0, 0);
    lv_obj_set_style_pad_all(st->tesla_page, 10, 0);
    lv_obj_set_flex_flow(st->tesla_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->tesla_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(st->tesla_page, 22, 0);
    lv_obj_clear_flag(st->tesla_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->tesla_page, "Tesla", subghz_host_color_purple(), on_back);

    /* Icon */
    lv_obj_t *icon = lv_label_create(st->tesla_page);
    lv_label_set_text(icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, subghz_host_color_purple(), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(st->tesla_page);
    lv_label_set_text(title, "Charge Port Opener");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);

    /* Freq info */
    lv_obj_t *freq = lv_label_create(st->tesla_page);
    lv_label_set_text(freq, "315.00 MHz OOK");
    lv_obj_set_style_text_font(freq, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(freq, subghz_host_ui_muted(), 0);

    /* Big button */
    lv_obj_t *btn = lv_btn_create(st->tesla_page);
    lv_obj_set_size(btn, 380, 100);
    lv_obj_set_style_bg_color(btn, subghz_host_color_purple(), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x7B1FA2), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_add_event_cb(btn, on_open_port, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_POWER " Open Port");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_center(bl);

    /* Status */
    st->tesla_status_lbl = lv_label_create(st->tesla_page);
    lv_obj_set_style_text_font(st->tesla_status_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(st->tesla_status_lbl, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->tesla_status_lbl, "Ready");

    ESP_LOGI(TAG, "Tesla page ready");
}
