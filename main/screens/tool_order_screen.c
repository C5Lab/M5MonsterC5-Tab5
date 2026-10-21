#include "tool_order_screen.h"
#include "tile_order_store.h"
#include "app_keyboard_navigation.h"
#include <stdio.h>
#include <string.h>

static struct {
    tool_order_screen_config_t config;
    lv_obj_t *page, *grid, *hint, *ghost, *selected_label;
    lv_obj_t *tiles[TILE_ORDER_MAX];
    lv_timer_t *timer;
    lv_indev_t *pointer;
    tile_order_t draft[TO_GROUP_COUNT];
    bool reset[TO_GROUP_COUNT], dirty[TO_GROUP_COUNT];
    tile_order_group_t group;
    lv_display_rotation_t rotation;
    uint8_t selected, dragging;
    lv_coord_t width, height;
} editor;

static const tool_order_item_t *item(uint8_t id)
{
    for (size_t i = 0; i < editor.config.item_count; ++i)
        if (editor.config.items[i].id == id) return &editor.config.items[i];
    return NULL;
}

static size_t position(uint8_t id)
{
    tile_order_t *order = &editor.draft[editor.group];
    for (size_t i = 0; i < order->count; ++i) if (order->ids[i] == id) return i;
    return order->count;
}

static void stop_drag(void)
{
    editor.dragging = 0;
    editor.pointer = NULL;
    if (editor.ghost) { lv_obj_delete(editor.ghost); editor.ghost = NULL; }
    if (editor.grid) lv_obj_add_flag(editor.grid, LV_OBJ_FLAG_SCROLLABLE);
}

static void selection(void)
{
    tile_order_t *order = &editor.draft[editor.group];
    for (size_t i = 0; i < order->count; ++i) {
        lv_obj_t *tile = editor.tiles[i];
        bool selected = order->ids[i] == editor.selected;
        lv_obj_set_style_outline_width(tile, selected ? 3 : 0, 0);
        lv_obj_set_style_outline_color(tile, editor.config.accent, 0);
    }
    const tool_order_item_t *selected = item(editor.selected);
    lv_label_set_text_fmt(editor.selected_label, "Selected: %s", selected ? selected->label : "none");
}

static void move_to(size_t target)
{
    tile_order_t *order = &editor.draft[editor.group];
    size_t from = position(editor.selected);
    if (from == target || !tile_order_move(order, from, target)) return;
    lv_obj_t *tile = editor.tiles[from];
    if (from < target) memmove(editor.tiles + from, editor.tiles + from + 1,
                              (target - from) * sizeof(tile));
    else memmove(editor.tiles + target + 1, editor.tiles + target,
                 (from - target) * sizeof(tile));
    editor.tiles[target] = tile;
    lv_obj_move_to_index(tile, (int32_t)target);
    editor.dirty[editor.group] = true;
    editor.reset[editor.group] = false;
    lv_obj_update_layout(editor.grid);
    selection();
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    /* Half turns keep the same dimensions and do not emit SIZE_CHANGED. */
    lv_display_rotation_t rotation = lv_display_get_rotation(lv_obj_get_display(editor.page));
    if (rotation != editor.rotation) {
        stop_drag();
        editor.rotation = rotation;
    }
    if (!editor.dragging || !editor.pointer) return;
    if (lv_indev_get_state(editor.pointer) != LV_INDEV_STATE_PRESSED) { stop_drag(); return; }
    lv_point_t point;
    lv_indev_get_point(editor.pointer, &point);
    lv_obj_set_pos(editor.ghost, point.x - 80, point.y - 30);
    lv_area_t bounds;
    lv_obj_get_coords(editor.grid, &bounds);
    if (point.x < bounds.x1 || point.x > bounds.x2) return;
    if (point.y < bounds.y1 + 40) lv_obj_scroll_by_bounded(editor.grid, 0, 12, LV_ANIM_OFF);
    else if (point.y > bounds.y2 - 40) lv_obj_scroll_by_bounded(editor.grid, 0, -12, LV_ANIM_OFF);
    lv_obj_update_layout(editor.grid);
    /* Only occupied target rectangles reorder. Gaps keep the last preview,
     * avoiding oscillation at row boundaries and while autoscrolling. */
    tile_order_t *order = &editor.draft[editor.group];
    if (point.y < bounds.y1 || point.y > bounds.y2) return;
    for (size_t i = 0; i < order->count; ++i) {
        lv_area_t area;
        lv_obj_get_coords(editor.tiles[i], &area);
        if (point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2) {
            move_to(i);
            break;
        }
    }
}

static void tile_event(lv_event_t *event)
{
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED) { editor.selected = id; selection(); }
    else if (code == LV_EVENT_LONG_PRESSED) {
        lv_indev_t *pointer = lv_indev_active();
        if (!pointer || lv_indev_get_type(pointer) != LV_INDEV_TYPE_POINTER) return;
        stop_drag();
        editor.selected = editor.dragging = id;
        editor.pointer = pointer;
        selection();
        lv_obj_remove_flag(editor.grid, LV_OBJ_FLAG_SCROLLABLE);
        editor.ghost = lv_obj_create(editor.page);
        lv_obj_set_size(editor.ghost, 160, 60);
        lv_obj_add_flag(editor.ghost, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_remove_flag(editor.ghost, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(editor.ghost, editor.config.card, 0);
        lv_obj_set_style_border_color(editor.ghost, editor.config.accent, 0);
        lv_obj_set_style_border_width(editor.ghost, 2, 0);
        lv_obj_t *label = lv_label_create(editor.ghost);
        lv_label_set_text(label, item(id)->label);
        lv_obj_set_style_text_color(label, editor.config.text, 0);
        lv_obj_center(label);
        tick(NULL);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) stop_drag();
}

static void render_grid(void)
{
    stop_drag();
    app_keyboard_navigation_clear_selection();
    lv_obj_clean(editor.grid);
    tile_order_t *order = &editor.draft[editor.group];
    lv_coord_t width = lv_obj_get_content_width(editor.page);
    unsigned columns = width >= 960 ? 5 : width >= 650 ? 3 : 2;
    lv_coord_t tile_width = (width - 16 - (columns - 1) * 10) / columns;
    for (size_t i = 0; i < order->count; ++i) {
        const tool_order_item_t *descriptor = item(order->ids[i]);
        lv_obj_t *tile = editor.tiles[i] = lv_button_create(editor.grid);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_size(tile, tile_width, 112);
        lv_obj_set_style_bg_color(tile, editor.config.card, 0);
        lv_obj_set_style_radius(tile, 16, 0);
        lv_obj_set_style_pad_all(tile, 8, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(tile, tile_event, LV_EVENT_ALL, (void *)(uintptr_t)order->ids[i]);
        app_keyboard_navigation_register_tile(tile);
        lv_obj_t *icon = lv_label_create(tile);
        lv_label_set_text(icon, descriptor ? descriptor->icon : LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_color(icon, descriptor ? descriptor->accent : editor.config.accent, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        lv_obj_t *label = lv_label_create(tile);
        lv_label_set_text(label, descriptor ? descriptor->label : "Tool");
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, editor.config.text, 0);
    }
    selection();
}

static void group_changed(lv_event_t *event)
{
    editor.group = (tile_order_group_t)(uintptr_t)lv_event_get_user_data(event);
    editor.selected = editor.draft[editor.group].ids[0];
    render_grid();
}

static void move_button(lv_event_t *event)
{
    intptr_t direction = (intptr_t)lv_event_get_user_data(event);
    size_t from = position(editor.selected);
    if ((direction < 0 && from == 0) || (direction > 0 && from + 1 >= editor.draft[editor.group].count)) return;
    move_to((size_t)((intptr_t)from + direction));
    lv_obj_scroll_to_view(editor.tiles[position(editor.selected)], LV_ANIM_OFF);
}

static void reset_button(lv_event_t *event)
{
    (void)event;
    tile_order_default(editor.group, &editor.draft[editor.group]);
    editor.reset[editor.group] = editor.dirty[editor.group] = true;
    render_grid();
    lv_label_set_text(editor.hint, "Default order restored. Choose Save to apply.");
}

static void deleted(lv_event_t *event)
{
    (void)event;
    if (editor.timer) lv_timer_delete(editor.timer);
    memset(&editor, 0, sizeof(editor));
}

static void cancel(lv_event_t *event)
{
    (void)event;
    stop_drag();
    app_keyboard_navigation_clear_selection();
    lv_obj_delete(editor.page);
}

static void save(lv_event_t *event)
{
    (void)event;
    stop_drag();
    bool applied = false;
    for (tile_order_group_t group = TO_MONSTER; group < TO_GROUP_COUNT; ++group) {
        if (!editor.dirty[group]) continue;
        esp_err_t err = tile_order_save(group, &editor.draft[group], editor.reset[group]);
        if (err != ESP_OK) {
            lv_label_set_text_fmt(editor.hint, "Could not save %s: %s. Retry Save.",
                                  group == TO_MONSTER ? "Monster" : "INTERNAL", esp_err_to_name(err));
            /* Earlier successful group commits are independent, not rolled back. */
            if (applied && editor.config.saved) editor.config.saved();
            return;
        }
        editor.dirty[group] = false;
        applied = true;
    }
    void (*saved)(void) = editor.config.saved;
    cancel(NULL);
    if (applied && saved) saved();
}

static lv_obj_t *button(lv_obj_t *row, const char *text, lv_event_cb_t callback, intptr_t data)
{
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_height(btn, 44);
    lv_obj_set_style_bg_color(btn, editor.config.card, 0);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, (void *)data);
    /* Include editor actions in the same keyboard traversal as the grid. */
    app_keyboard_navigation_register_tile(btn);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, editor.config.text, 0);
    lv_obj_center(label);
    return btn;
}

static void resized(lv_event_t *event)
{
    (void)event;
    if (!editor.grid) return;
    lv_coord_t width = lv_obj_get_width(editor.page), height = lv_obj_get_height(editor.page);
    if (width == editor.width && height == editor.height) return;
    editor.width = width;
    editor.height = height;
    render_grid();
}

void tool_order_screen_show(const tool_order_screen_config_t *config)
{
    if (editor.page) return;
    memset(&editor, 0, sizeof(editor));
    editor.config = *config;
    bool load_error = false;
    for (tile_order_group_t group = TO_MONSTER; group < TO_GROUP_COUNT; ++group) {
        if (tile_order_load(group, &editor.draft[group]) != ESP_OK) {
            load_error = true;
            editor.dirty[group] = true; /* Explicit Save repairs the fallback. */
        }
    }
    editor.selected = editor.draft[TO_MONSTER].ids[0];
    app_keyboard_navigation_clear_selection();
    editor.page = lv_obj_create(lv_layer_top());
    lv_obj_set_size(editor.page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(editor.page, config->background, 0);
    lv_obj_set_style_bg_opa(editor.page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(editor.page, 0, 0);
    lv_obj_set_style_radius(editor.page, 0, 0);
    lv_obj_set_style_pad_all(editor.page, 16, 0);
    lv_obj_set_style_pad_row(editor.page, 10, 0);
    lv_obj_set_flex_flow(editor.page, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(editor.page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(editor.page, deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = lv_label_create(editor.page);
    lv_label_set_text(title, "Tool Order");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, config->text, 0);
    lv_obj_t *groups = lv_obj_create(editor.page);
    lv_obj_remove_style_all(groups);
    lv_obj_set_size(groups, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(groups, 10, 0);
    lv_obj_set_flex_flow(groups, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(groups, LV_OBJ_FLAG_SCROLLABLE);
    button(groups, "Monster", group_changed, TO_MONSTER);
    button(groups, "INTERNAL", group_changed, TO_INTERNAL);
    editor.hint = lv_label_create(editor.page);
    lv_obj_set_width(editor.hint, LV_PCT(100));
    lv_obj_set_style_text_color(editor.hint, config->text, 0);
    lv_label_set_text(editor.hint, load_error ? "Saved order could not be read. Showing defaults; Save replaces it."
                      : "Hold and drag a tool. Select a tool to use Move earlier / later. NFC and Sub-GHz appear on supported hardware.");
    editor.grid = lv_obj_create(editor.page);
    lv_obj_set_size(editor.grid, LV_PCT(100), 100);
    lv_obj_set_flex_grow(editor.grid, 1);
    lv_obj_set_style_bg_opa(editor.grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(editor.grid, 0, 0);
    lv_obj_set_style_pad_all(editor.grid, 8, 0);
    lv_obj_set_style_pad_gap(editor.grid, 10, 0);
    lv_obj_set_flex_flow(editor.grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(editor.grid, LV_DIR_VER);
    editor.selected_label = lv_label_create(editor.page);
    lv_obj_set_style_text_color(editor.selected_label, config->text, 0);
    lv_obj_t *row = lv_obj_create(editor.page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    button(row, "Move earlier", move_button, -1);
    button(row, "Move later", move_button, 1);
    button(row, "Reset to Default", reset_button, 0);
    lv_obj_t *cancel_btn = button(row, "Cancel", cancel, 0);
    app_keyboard_navigation_register_escape(cancel_btn);
    button(row, "Save", save, 0);
    lv_obj_update_layout(editor.page);
    editor.width = lv_obj_get_width(editor.page);
    editor.height = lv_obj_get_height(editor.page);
    editor.rotation = lv_display_get_rotation(lv_obj_get_display(editor.page));
    render_grid();
    lv_obj_add_event_cb(editor.page, resized, LV_EVENT_SIZE_CHANGED, NULL);
    editor.timer = lv_timer_create(tick, 30, NULL);
}
