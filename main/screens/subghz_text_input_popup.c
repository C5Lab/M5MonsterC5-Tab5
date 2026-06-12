#include "subghz_host.h"
#include "subghz_internal.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "subghz_text_input";

typedef struct {
    lv_obj_t                          *overlay;
    lv_obj_t                          *card;
    lv_obj_t                          *textarea;
    lv_obj_t                          *keyboard;
    subghz_text_input_confirm_cb_t     on_confirm;
    subghz_text_input_cancel_cb_t      on_cancel;
    void                              *user_data;
} subghz_text_input_ctx_t;

static void destroy_ctx(subghz_text_input_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->overlay) lv_obj_delete(ctx->overlay);
    free(ctx);
}

static void on_confirm_clicked(lv_event_t *e)
{
    subghz_text_input_ctx_t *ctx = (subghz_text_input_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    const char *text = lv_textarea_get_text(ctx->textarea);
    char *copy = NULL;
    if (text) {
        size_t len = strlen(text);
        copy = (char *)malloc(len + 1);
        if (copy) memcpy(copy, text, len + 1);
    }
    subghz_text_input_confirm_cb_t cb = ctx->on_confirm;
    void *ud = ctx->user_data;
    destroy_ctx(ctx);
    if (cb) cb(copy ? copy : "", ud);
    if (copy) free(copy);
}

static void on_cancel_clicked(lv_event_t *e)
{
    subghz_text_input_ctx_t *ctx = (subghz_text_input_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    subghz_text_input_cancel_cb_t cb = ctx->on_cancel;
    void *ud = ctx->user_data;
    destroy_ctx(ctx);
    if (cb) cb(ud);
}

static void on_kb_event(lv_event_t *e)
{
    subghz_text_input_ctx_t *ctx = (subghz_text_input_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *text = lv_textarea_get_text(ctx->textarea);
        char *copy = NULL;
        if (text) {
            size_t len = strlen(text);
            copy = (char *)malloc(len + 1);
            if (copy) memcpy(copy, text, len + 1);
        }
        subghz_text_input_confirm_cb_t cb = ctx->on_confirm;
        void *ud = ctx->user_data;
        destroy_ctx(ctx);
        if (cb) cb(copy ? copy : "", ud);
        if (copy) free(copy);
    } else if (code == LV_EVENT_CANCEL) {
        subghz_text_input_cancel_cb_t cb = ctx->on_cancel;
        void *ud = ctx->user_data;
        destroy_ctx(ctx);
        if (cb) cb(ud);
    }
}

void subghz_show_text_input_popup(const char *title,
                                  const char *initial,
                                  uint32_t max_len,
                                  lv_color_t accent,
                                  subghz_text_input_confirm_cb_t on_confirm,
                                  subghz_text_input_cancel_cb_t on_cancel,
                                  void *user_data)
{
    subghz_text_input_ctx_t *ctx = (subghz_text_input_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        ESP_LOGE(TAG, "Failed to allocate text input ctx");
        if (on_cancel) on_cancel(user_data);
        return;
    }
    ctx->on_confirm = on_confirm;
    ctx->on_cancel  = on_cancel;
    ctx->user_data  = user_data;

    /* Full-screen overlay */
    ctx->overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(ctx->overlay);
    lv_obj_set_size(ctx->overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->overlay, LV_OPA_60, 0);
    lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);

    /* Card at the top half (keyboard occupies bottom 260 px) */
    ctx->card = lv_obj_create(ctx->overlay);
    lv_obj_set_size(ctx->card, 720, 240);
    lv_obj_align(ctx->card, LV_ALIGN_TOP_MID, 0, 36);
    subghz_style_popup_card(ctx->card, 12, accent);
    lv_obj_set_flex_flow(ctx->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ctx->card, 16, 0);
    lv_obj_set_style_pad_gap(ctx->card, 12, 0);
    lv_obj_clear_flag(ctx->card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(ctx->card);
    lv_label_set_text(title_lbl, title ? title : "Input");
    lv_obj_set_style_text_color(title_lbl, subghz_host_ui_text(), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_22, 0);

    ctx->textarea = lv_textarea_create(ctx->card);
    lv_obj_set_size(ctx->textarea, lv_pct(100), 64);
    lv_textarea_set_one_line(ctx->textarea, true);
    lv_textarea_set_max_length(ctx->textarea, max_len > 0 ? max_len : 48);
    lv_textarea_set_text(ctx->textarea, initial ? initial : "");
    lv_obj_set_style_text_font(ctx->textarea, &lv_font_montserrat_22, 0);
    lv_obj_set_style_bg_color(ctx->textarea, subghz_host_ui_card(), 0);
    lv_obj_set_style_text_color(ctx->textarea, subghz_host_ui_text(), 0);
    lv_obj_set_style_border_width(ctx->textarea, 1, 0);
    lv_obj_set_style_border_color(ctx->textarea, accent, 0);

    lv_obj_t *brow = lv_obj_create(ctx->card);
    lv_obj_set_size(brow, lv_pct(100), 56);
    lv_obj_set_flex_flow(brow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brow, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brow, 0, 0);
    lv_obj_set_style_pad_all(brow, 0, 0);
    lv_obj_clear_flag(brow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ok = lv_btn_create(brow);
    lv_obj_set_size(ok, 180, 48);
    lv_obj_set_style_bg_color(ok, subghz_host_color_green(), 0);
    lv_obj_set_style_radius(ok, 8, 0);
    lv_obj_add_event_cb(ok, on_confirm_clicked, LV_EVENT_CLICKED, ctx);
    lv_obj_t *okl = lv_label_create(ok);
    lv_label_set_text(okl, "Save");
    lv_obj_set_style_text_color(okl, lv_color_white(), 0);
    lv_obj_set_style_text_font(okl, &lv_font_montserrat_18, 0);
    lv_obj_center(okl);

    lv_obj_t *cancel = lv_btn_create(brow);
    lv_obj_set_size(cancel, 180, 48);
    lv_obj_set_style_bg_color(cancel, subghz_host_ui_muted(), 0);
    lv_obj_set_style_radius(cancel, 8, 0);
    lv_obj_add_event_cb(cancel, on_cancel_clicked, LV_EVENT_CLICKED, ctx);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);

    /* Bottom keyboard */
    ctx->keyboard = lv_keyboard_create(ctx->overlay);
    lv_obj_set_size(ctx->keyboard, lv_pct(100), 320);
    lv_obj_align(ctx->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ctx->keyboard, ctx->textarea);
    lv_obj_add_event_cb(ctx->keyboard, on_kb_event, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(ctx->keyboard, on_kb_event, LV_EVENT_CANCEL, ctx);
}
