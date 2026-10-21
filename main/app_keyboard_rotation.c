#include "app_keyboard_rotation.h"
#include "app_keyboard_navigation.h"

static lv_obj_t *overlay;
static lv_obj_t *message;
static bool offered;
static bool (*apply_rotation)(void);
static void (*style_prompt)(lv_obj_t *, lv_obj_t *, lv_obj_t *, lv_obj_t *);

static void deleted(lv_event_t *e)
{
    (void)e;
    overlay = NULL;
    message = NULL;
}

static void close_prompt(void)
{
    if (overlay) {
        app_keyboard_navigation_clear_selection();
        lv_obj_delete(overlay);
    }
}

static void decline(lv_event_t *e)
{
    (void)e;
    close_prompt();
}

static void accept(lv_event_t *e)
{
    (void)e;
    if (apply_rotation && apply_rotation()) {
        close_prompt();
    } else if (message) {
        lv_label_set_text(message, "Could not save orientation.\nTry Yes again, or choose No to cancel.");
    }
}

static lv_obj_t *button(lv_obj_t *row, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, 120, 48);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void show_prompt(void)
{
    app_keyboard_navigation_clear_selection();
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, deleted, LV_EVENT_DELETE, NULL);

    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, 440, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_row(card, 18, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Keyboard detected");
    message = lv_label_create(card);
    lv_obj_set_width(message, LV_PCT(100));
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(message, "Change screen orientation to 90?\nYes will restart the device.");

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* The first Enter/Space chooses No unless the user navigates to Yes. */
    lv_obj_t *no = button(row, "No", decline);
    lv_obj_t *yes = button(row, "Yes", accept);
    app_keyboard_navigation_register_escape(no);
    if (style_prompt) style_prompt(overlay, card, no, yes);
}

void app_keyboard_rotation_init(bool (*apply)(void),
    void (*style)(lv_obj_t *, lv_obj_t *, lv_obj_t *, lv_obj_t *))
{
    close_prompt();
    offered = false;
    apply_rotation = apply;
    style_prompt = style;
}

bool app_keyboard_rotation_update(bool connected, bool can_show, bool needs_rotation)
{
    if (!connected) {
        close_prompt();
        offered = false;
        return false;
    }
    if (overlay) {
        if (!needs_rotation) close_prompt();
        else if (!can_show) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        else if (lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            return true;
        }
    } else if (!offered && can_show) {
        offered = true;
        if (needs_rotation) {
            show_prompt();
            return true;
        }
    }
    return false;
}
