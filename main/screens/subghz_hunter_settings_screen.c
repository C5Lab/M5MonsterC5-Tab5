/* Hunter Settings — RF parameters for the Frequency Analyzer.
 * Persists every change to NVS (subghz_rf namespace) immediately. */

#include "subghz_host.h"
#include "subghz_internal.h"
#include "subghz_rf_settings.h"
#include "esp_log.h"

static const char *TAG = "subghz_hunter_set";

static lv_obj_t *s_single_block;

static lv_obj_t *create_setting_block(lv_obj_t *parent)
{
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    lv_obj_set_style_pad_bottom(block, 10, 0);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(block, 4, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    return block;
}

static lv_obj_t *create_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void add_setting_desc(lv_obj_t *block, const char *text)
{
    lv_obj_t *desc = lv_label_create(block);
    lv_label_set_text(desc, text);
    lv_obj_set_style_text_color(desc, subghz_host_ui_muted(), 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, lv_pct(100));
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    return lbl;
}

static void style_dropdown(lv_obj_t *dd)
{
    lv_obj_set_width(dd, 200);
    lv_obj_set_height(dd, 50);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(dd, subghz_host_ui_card(), 0);
    lv_obj_set_style_text_color(dd, subghz_host_ui_text(), 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, subghz_host_ui_card(), 0);
        lv_obj_set_style_text_color(list, subghz_host_ui_text(), 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_18, 0);
    }
}

static void update_single_visibility(const subghz_rf_settings_t *cfg)
{
    if (!s_single_block) return;
    if (cfg->hunter_raw) lv_obj_add_flag(s_single_block, LV_OBJ_FLAG_HIDDEN);
    else                 lv_obj_clear_flag(s_single_block, LV_OBJ_FLAG_HIDDEN);
}

static void on_trigger_changed(lv_event_t *e)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    cfg.hunter_trigger_dbm = subghz_rf_hunter_trigger_from_index(
        (int)lv_dropdown_get_selected(lv_event_get_target(e)));
    subghz_rf_settings_save(&cfg);
}

static void on_timeout_changed(lv_event_t *e)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    cfg.hunter_timeout_ms = subghz_rf_hunter_timeout_from_index(
        (int)lv_dropdown_get_selected(lv_event_get_target(e)));
    subghz_rf_settings_save(&cfg);
}

static void on_mode_changed(lv_event_t *e)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    cfg.hunter_raw = (lv_dropdown_get_selected(lv_event_get_target(e)) == 1);
    if (cfg.hunter_raw) cfg.hunter_single = false;
    subghz_rf_settings_save(&cfg);
    update_single_visibility(&cfg);
}

static void on_single_changed(lv_event_t *e)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    cfg.hunter_single = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    subghz_rf_settings_save(&cfg);
}

static void on_fast_changed(lv_event_t *e)
{
    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);
    cfg.hunter_fast = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    subghz_rf_settings_save(&cfg);
}

void subghz_hunter_settings_cleanup(subghz_tab_state_t *st)
{
    if (!st) return;
    if (st->hunter_settings_page) {
        lv_obj_delete(st->hunter_settings_page);
        st->hunter_settings_page = NULL;
    }
    s_single_block = NULL;
}

static void on_back(lv_event_t *e)
{
    (void)e;
    subghz_tab_state_t *st = subghz_host_state();
    if (st) subghz_hunter_settings_cleanup(st);
    show_subghz_hunter_page_resume();
}

void show_subghz_hunter_settings_page(void)
{
    subghz_tab_state_t *st = subghz_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    if (st->hunter_settings_page) {
        lv_obj_delete(st->hunter_settings_page);
        st->hunter_settings_page = NULL;
    }

    subghz_rf_settings_t cfg;
    subghz_rf_settings_load(&cfg);

    st->hunter_settings_page = lv_obj_create(container);
    lv_obj_set_size(st->hunter_settings_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(st->hunter_settings_page, subghz_host_ui_bg(), 0);
    lv_obj_set_style_border_width(st->hunter_settings_page, 0, 0);
    lv_obj_set_style_pad_all(st->hunter_settings_page, 10, 0);
    lv_obj_set_flex_flow(st->hunter_settings_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st->hunter_settings_page, 8, 0);
    lv_obj_clear_flag(st->hunter_settings_page, LV_OBJ_FLAG_SCROLLABLE);

    subghz_create_header(st->hunter_settings_page, "Hunter Settings",
                         subghz_host_color_pink(), on_back);

    lv_obj_t *cont = lv_obj_create(st->hunter_settings_page);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(cont, 1);
    lv_obj_set_style_bg_color(cont, subghz_host_ui_card(), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cont, 12, 0);
    lv_obj_set_style_pad_all(cont, 16, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 10, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);

    /* Trigger */
    lv_obj_t *blk = create_setting_block(cont);
    lv_obj_t *row = create_row(blk);
    make_label(row, "Trigger (dBm)");
    lv_obj_t *dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, "-85\n-80\n-75\n-70\n-65\n-60");
    lv_dropdown_set_selected(dd, (uint32_t)subghz_rf_hunter_trigger_index(cfg.hunter_trigger_dbm));
    style_dropdown(dd);
    lv_obj_add_event_cb(dd, on_trigger_changed, LV_EVENT_VALUE_CHANGED, NULL);
    add_setting_desc(blk, "RSSI level a signal must exceed before it is reported.");

    /* Timeout */
    blk = create_setting_block(cont);
    row = create_row(blk);
    make_label(row, "Capture timeout");
    dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, "1 s\n2 s\n3 s\n5 s");
    lv_dropdown_set_selected(dd, (uint32_t)subghz_rf_hunter_timeout_index(cfg.hunter_timeout_ms));
    style_dropdown(dd);
    lv_obj_add_event_cb(dd, on_timeout_changed, LV_EVENT_VALUE_CHANGED, NULL);
    add_setting_desc(blk, "How long to wait for a burst after locking onto a frequency.");

    /* Mode */
    blk = create_setting_block(cont);
    row = create_row(blk);
    make_label(row, "Mode");
    dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, "Decode\nRaw");
    lv_dropdown_set_selected(dd, cfg.hunter_raw ? 1 : 0);
    style_dropdown(dd);
    lv_obj_add_event_cb(dd, on_mode_changed, LV_EVENT_VALUE_CHANGED, NULL);
    add_setting_desc(blk, "Decode saves protocol keys; Raw saves timing edges only.");

    /* Single burst */
    s_single_block = create_setting_block(cont);
    row = create_row(s_single_block);
    make_label(row, "Single burst");
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 80, 40);
    if (cfg.hunter_single) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, subghz_host_ui_muted(), 0);
    lv_obj_set_style_bg_color(sw, subghz_host_color_pink(), LV_STATE_CHECKED | LV_PART_INDICATOR);
    lv_obj_add_event_cb(sw, on_single_changed, LV_EVENT_VALUE_CHANGED, NULL);
    add_setting_desc(s_single_block, "Capture after one remote press instead of two.");
    update_single_visibility(&cfg);

    /* Fast scan */
    blk = create_setting_block(cont);
    row = create_row(blk);
    make_label(row, "Fast scan");
    sw = lv_switch_create(row);
    lv_obj_set_size(sw, 80, 40);
    if (cfg.hunter_fast) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, subghz_host_ui_muted(), 0);
    lv_obj_set_style_bg_color(sw, subghz_host_color_pink(), LV_STATE_CHECKED | LV_PART_INDICATOR);
    lv_obj_add_event_cb(sw, on_fast_changed, LV_EVENT_VALUE_CHANGED, NULL);
    add_setting_desc(blk, "Faster frequency steps with shorter settle and sticky re-lock.");

    ESP_LOGI(TAG, "Hunter settings ready");
}
