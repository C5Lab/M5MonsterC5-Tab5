/* Listen Settings — single RSSI floor dropdown (persisted to NVS subghz_rf). */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_rf_settings.h"
#include "esp_log.h"

static const char *TAG = "subghz_listen_set";

static void on_rssi_changed(lv_event_t *e)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    cfg.listen_rssi_dbm = subghz_rf_listen_rssi_from_index(
        (int)lv_dropdown_get_selected(lv_event_get_target(e)));
    subghz_rf_settings_save(&cfg);
}

void subghz_listen_settings_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->listen_settings_page) {
        lv_obj_delete(st->listen_settings_page);
        st->listen_settings_page = NULL;
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (st) subghz_listen_settings_cleanup(st);
    show_subghz_listen_page();
}

void show_subghz_listen_settings_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->listen_settings_page) {
        lv_obj_delete(st->listen_settings_page);
        st->listen_settings_page = NULL;
    }

    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);

    st->listen_settings_page = lv_obj_create(container);
    lv_obj_set_size(st->listen_settings_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->listen_settings_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->listen_settings_page, 0, 0);
    lv_obj_set_style_pad_all(st->listen_settings_page, 10, 0);
    lv_obj_set_flex_flow(st->listen_settings_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->listen_settings_page, 8, 0);
    lv_obj_clear_flag(st->listen_settings_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->listen_settings_page, "Listen Settings",
                         subghz_host_color_cyan(), on_back);

    lv_obj_t *card = lv_obj_create(st->listen_settings_page);
    lv_obj_set_size(card, lv_pct(80), 240);
    lv_obj_set_style_bg_color(card, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "RSSI floor (dBm)");
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *dd = lv_dropdown_create(card);
    lv_dropdown_set_options(dd, "-85\n-80\n-75\n-70\n-65\n-60");
    lv_dropdown_set_selected(dd, (uint32_t)subghz_rf_listen_rssi_index(cfg.listen_rssi_dbm));
    lv_obj_set_width(dd, 240);
    lv_obj_set_height(dd, 60);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_22, 0);
    lv_obj_set_style_bg_color(dd, subghz_host_ui_panel(), 0);
    lv_obj_set_style_text_color(dd, subghz_host_ui_text(), 0);
    lv_obj_add_event_cb(dd, on_rssi_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, subghz_host_ui_card(), 0);
        lv_obj_set_style_text_color(list, subghz_host_ui_text(), 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    }

    lv_obj_t *desc = lv_label_create(card);
    lv_label_set_text(desc, "Signals weaker than this are not reported in Listen.");
    lv_obj_set_style_text_color(desc, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, lv_pct(100));

    ESP_LOGI(TAG, "Listen settings page ready");
}
