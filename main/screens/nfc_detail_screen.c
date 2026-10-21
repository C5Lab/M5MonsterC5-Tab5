#include "nfc_host.h"
#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "nfc_detail";

#define NFC_CMD_TIMEOUT_MS 5000

static void set_labels(nfc_tab_state_t *st, const char *status, const char *detail, lv_color_t color)
{
    if (!st) return;
    if (st->detail_status) {
        lv_label_set_text(st->detail_status, status ? status : "");
        lv_obj_set_style_text_color(st->detail_status, color, 0);
    }
    if (st->detail_lbl)
        lv_label_set_text(st->detail_lbl, detail ? detail : "");
}

static void apply_loaded(nfc_tab_state_t *st)
{
    char status[64];
    char detail[192];
    const nfc_ui_card_t *c = &st->card;

    if (c->load_failed || c->not_detected || c->not_initialized ||
        (!c->have_loaded && !c->type[0] && !c->uid[0])) {
        set_labels(st, "Load failed", "", subghz_host_color_red());
        st->detail_loaded = false;
        nfc_set_btn_enabled(st->detail_emu_btn, false);
        return;
    }

    if (c->have_loaded)
        nfc_path_basename(c->loaded_path, st->detail_name, sizeof(st->detail_name), true);

    nfc_format_card_detail(c, status, sizeof(status), detail, sizeof(detail));
    set_labels(st, status, detail, subghz_host_color_green());
    st->detail_loaded = true;
    nfc_set_btn_enabled(st->detail_emu_btn, true);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (st && (st->text_input_open || st->confirm_popup)) return;
    show_nfc_list_page();
}

static void on_emulate(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || !st->detail_loaded || st->text_input_open || st->confirm_popup) return;
    st->hint_have_data = st->card.have_data;
    snprintf(st->hint_type, sizeof(st->hint_type), "%s", st->card.type);
    show_nfc_emulate_page();
}

static void on_rename_confirm(const char *text, void *user_data)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)user_data;
    if (!st) return;
    st->text_input_open = false;
    if (!st->detail_page || st->detail_idx < 0) return;

    if (!text || !text[0] || !nfc_name_is_valid(text)) {
        if (st->detail_status) {
            lv_label_set_text(st->detail_status, "Rename: invalid name");
            lv_obj_set_style_text_color(st->detail_status, subghz_host_color_red(), 0);
        }
        return;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "nfc_rename %d %s", st->detail_idx, text);
    if (st->detail_status) {
        lv_label_set_text(st->detail_status, "Renaming...");
        lv_obj_set_style_text_color(st->detail_status, subghz_host_color_blue(), 0);
    }
    nfc_uart_start(st, NFC_OP_RENAME, cmd, NFC_CMD_TIMEOUT_MS);
}

static void on_rename_cancel(void *user_data)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)user_data;
    if (st) st->text_input_open = false;
}

static void on_rename(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || st->detail_idx < 0 || st->text_input_open || st->confirm_popup) return;

    st->text_input_open = true;
    subghz_show_text_input_popup("Rename card", st->detail_name, 48,
                                 subghz_host_color_blue(),
                                 on_rename_confirm, on_rename_cancel, st);
}

static void close_confirm(nfc_tab_state_t *st)
{
    if (!st || !st->confirm_popup) return;
    lv_obj_delete(st->confirm_popup);
    st->confirm_popup = NULL;
}

static void on_delete_confirmed(lv_event_t *e)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)lv_event_get_user_data(e);
    if (!st) st = nfc_host_state();
    if (!st) return;
    close_confirm(st);
    if (st->detail_idx < 0 || !st->detail_page) return;

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "nfc_delete %d", st->detail_idx);
    if (st->detail_status) {
        lv_label_set_text(st->detail_status, "Deleting...");
        lv_obj_set_style_text_color(st->detail_status, subghz_host_color_red(), 0);
    }
    nfc_uart_start(st, NFC_OP_DELETE, cmd, NFC_CMD_TIMEOUT_MS);
}

static void on_delete_cancel(lv_event_t *e)
{
    nfc_tab_state_t *st = (nfc_tab_state_t *)lv_event_get_user_data(e);
    if (!st) st = nfc_host_state();
    close_confirm(st);
}

static void show_delete_confirm(nfc_tab_state_t *st)
{
    close_confirm(st);
    st->confirm_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(st->confirm_popup, 420, 180);
    lv_obj_center(st->confirm_popup);
    subghz_style_popup_card(st->confirm_popup, 12, subghz_host_color_red());
    lv_obj_set_flex_flow(st->confirm_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st->confirm_popup, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(st->confirm_popup, 16, 0);
    lv_obj_set_style_pad_gap(st->confirm_popup, 12, 0);
    lv_obj_clear_flag(st->confirm_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(st->confirm_popup);
    if (st->detail_name[0])
        lv_label_set_text_fmt(title, "Delete %s?", st->detail_name);
    else
        lv_label_set_text_fmt(title, "Delete #%d?", st->detail_idx);
    lv_obj_set_style_text_color(title, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    lv_obj_t *brow = lv_obj_create(st->confirm_popup);
    lv_obj_set_size(brow, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_set_style_pad_all(brow, 0, 0);
    lv_obj_set_style_pad_gap(brow, 12, 0);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *yb = lv_btn_create(brow);
    lv_obj_set_size(yb, 140, 48);
    lv_obj_set_style_bg_color(yb, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(yb, 8, 0);
    lv_obj_add_event_cb(yb, on_delete_confirmed, LV_EVENT_CLICKED, st);
    lv_obj_t *yl = lv_label_create(yb);
    lv_label_set_text(yl, "Delete");
    lv_obj_set_style_text_color(yl, lv_color_white(), 0);
    lv_obj_set_style_text_font(yl, &lv_font_montserrat_16, 0);
    lv_obj_center(yl);

    lv_obj_t *nb = lv_btn_create(brow);
    lv_obj_set_size(nb, 140, 48);
    lv_obj_set_style_bg_color(nb, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(nb, 8, 0);
    lv_obj_add_event_cb(nb, on_delete_cancel, LV_EVENT_CLICKED, st);
    lv_obj_t *nl = lv_label_create(nb);
    lv_label_set_text(nl, "Cancel");
    lv_obj_set_style_text_color(nl, lv_color_white(), 0);
    lv_obj_set_style_text_font(nl, &lv_font_montserrat_16, 0);
    lv_obj_center(nl);
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    nfc_tab_state_t *st = nfc_host_state();
    if (!st || st->detail_idx < 0 || st->text_input_open) return;
    show_delete_confirm(st);
}

void nfc_detail_on_load(nfc_tab_state_t *st)
{
    if (!st || !st->detail_page) return;
    if (st->result_timeout)
        st->result_card.load_failed = true;
    st->card = st->result_card;
    apply_loaded(st);
}

void nfc_detail_on_rename(nfc_tab_state_t *st)
{
    if (!st || !st->detail_page) return;

    bool failed = st->result_timeout ||
                  nfc_lines_contain(st, "[NFC] rename failed") ||
                  nfc_lines_contain(st, "[NFC] not found");
    if (!failed) {
        for (int i = 0; i < st->line_count; i++) {
            if (strncmp(st->lines[i], "[NFC] renamed: ", 15) != 0) continue;
            const char *arrow = strstr(st->lines[i] + 15, " -> ");
            if (!arrow) continue;
            nfc_path_basename(arrow + 4, st->detail_name, sizeof(st->detail_name), true);
            snprintf(st->card.loaded_path, sizeof(st->card.loaded_path), "%s", arrow + 4);
            st->card.have_loaded = true;
        }
    }

    if (failed) {
        if (st->detail_status) {
            lv_label_set_text(st->detail_status,
                              st->result_timeout ? "Rename timed out" : "Rename failed");
            lv_obj_set_style_text_color(st->detail_status, subghz_host_color_red(), 0);
        }
    } else {
        char status[64];
        char detail[192];
        nfc_format_card_detail(&st->card, status, sizeof(status), detail, sizeof(detail));
        if (st->detail_name[0])
            snprintf(status, sizeof(status), "%s", st->card.type[0] ? st->card.type : st->detail_name);
        set_labels(st, status, detail, subghz_host_color_green());
    }
    nfc_set_btn_enabled(st->detail_emu_btn, st->detail_loaded);
}

void nfc_detail_on_delete(nfc_tab_state_t *st)
{
    if (!st) return;
    show_nfc_list_page();
}

void show_nfc_detail_page(int idx)
{
    nfc_tab_state_t *st = nfc_host_state();
    lv_obj_t *container = subghz_host_current_container();
    if (!st || !container) return;

    subghz_host_hide_all_pages();
    st->detail_idx = idx;
    st->detail_loaded = false;
    st->text_input_open = false;
    nfc_card_reset(&st->card);
    snprintf(st->detail_name, sizeof(st->detail_name), "#%d", idx);

    st->detail_page = nfc_make_page(container);
    subghz_create_header(st->detail_page, "NFC Card", subghz_host_color_cyan(), on_back);

    st->detail_status = lv_label_create(st->detail_page);
    lv_obj_set_width(st->detail_status, lv_pct(100));
    lv_obj_set_style_text_align(st->detail_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->detail_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(st->detail_status, subghz_host_ui_muted(), 0);
    lv_label_set_text(st->detail_status, "Loading...");

    st->detail_lbl = lv_label_create(st->detail_page);
    lv_obj_set_width(st->detail_lbl, lv_pct(100));
    lv_obj_set_flex_grow(st->detail_lbl, 1);
    lv_obj_set_style_text_align(st->detail_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(st->detail_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st->detail_lbl, subghz_host_ui_muted(), 0);
    lv_label_set_long_mode(st->detail_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(st->detail_lbl, "");

    lv_obj_t *brow = lv_obj_create(st->detail_page);
    lv_obj_set_size(brow, lv_pct(100), 64);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_set_style_pad_all(brow, 0, 0);
    lv_obj_set_style_pad_gap(brow, 10, 0);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    st->detail_emu_btn = lv_btn_create(brow);
    lv_obj_set_size(st->detail_emu_btn, 150, 56);
    lv_obj_set_style_bg_color(st->detail_emu_btn, subghz_host_color_orange(), 0);
    lv_obj_set_style_radius(st->detail_emu_btn, 8, 0);
    lv_obj_add_event_cb(st->detail_emu_btn, on_emulate, LV_EVENT_CLICKED, NULL);
    lv_obj_t *el = lv_label_create(st->detail_emu_btn);
    lv_label_set_text(el, "Emulate");
    lv_obj_set_style_text_color(el, lv_color_white(), 0);
    lv_obj_set_style_text_font(el, &lv_font_montserrat_16, 0);
    lv_obj_center(el);
    nfc_set_btn_enabled(st->detail_emu_btn, false);

    lv_obj_t *rb = lv_btn_create(brow);
    lv_obj_set_size(rb, 150, 56);
    lv_obj_set_style_bg_color(rb, subghz_host_color_blue(), 0);
    lv_obj_set_style_radius(rb, 8, 0);
    lv_obj_add_event_cb(rb, on_rename, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rb);
    lv_label_set_text(rl, "Rename");
    lv_obj_set_style_text_color(rl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_16, 0);
    lv_obj_center(rl);

    lv_obj_t *db = lv_btn_create(brow);
    lv_obj_set_size(db, 150, 56);
    lv_obj_set_style_bg_color(db, subghz_host_color_red(), 0);
    lv_obj_set_style_radius(db, 8, 0);
    lv_obj_add_event_cb(db, on_delete, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(db);
    lv_label_set_text(dl, "Delete");
    lv_obj_set_style_text_color(dl, lv_color_white(), 0);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_16, 0);
    lv_obj_center(dl);

    char cmd[48];
    snprintf(cmd, sizeof(cmd), "nfc_load %d", idx);
    nfc_uart_start(st, NFC_OP_LOAD, cmd, NFC_CMD_TIMEOUT_MS);
    ESP_LOGI(TAG, "NFC detail idx=%d", idx);
}
