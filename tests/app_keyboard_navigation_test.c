#include "app_keyboard.h"
#include "app_keyboard_navigation.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static unsigned clicks[16];
static bool key(tab5_key_kind_t kind, char character)
{
    tab5_key_t event = {kind, character};
    return app_keyboard_input(&event);
}
static void clicked(lv_event_t *event)
{
    clicks[(uintptr_t)lv_event_get_user_data(event)]++;
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, unsigned id)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, 180, 100);
    lv_obj_add_event_cb(obj, clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
    return obj;
}
static void tab_clicked(lv_event_t *event)
{
    clicked(event);
    lv_obj_t *obj = lv_event_get_current_target(event);
    app_keyboard_navigation_set_tabs(lv_obj_get_parent(obj), obj);
}
static void delete_parent(lv_event_t *event)
{
    lv_obj_delete(lv_obj_get_parent(lv_event_get_current_target(event)));
}
static void test_read_only_scroll(void)
{
    memset(clicks, 0, sizeof(clicks));
    lv_obj_t *area = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(area, 0, 100);
    lv_obj_set_size(area, 600, 250);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    app_keyboard_navigation_register_scroll_area(area);
    app_keyboard_navigation_register_scroll_area(area);
    lv_obj_t *outside = button(lv_screen_active(), 450, 700, 6);
    app_keyboard_navigation_register_tile(outside);
    lv_obj_t *rows[6];
    lv_obj_t *checks[6];
    for (unsigned i = 0; i < 6; ++i) {
        lv_obj_t *row = button(area, 0, 0, i);
        rows[i] = row;
        lv_obj_set_height(row, 80 + (int)i * 12); /* Non-uniform result heights. */
        checks[i] = lv_checkbox_create(row);
        lv_obj_add_event_cb(checks[i], clicked, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    }
    app_keyboard_set_connected(false);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) == 0);
    app_keyboard_set_connected(true);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    assert(lv_obj_get_scroll_y(area) == 0);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[1], LV_STATE_USER_4));
    assert(!lv_obj_has_state(rows[0], LV_STATE_USER_4));
    key(TAB5_KEY_UP, 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    lv_obj_add_flag(rows[1], LV_OBJ_FLAG_HIDDEN);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[2], LV_STATE_USER_4));
    lv_obj_remove_flag(rows[1], LV_OBJ_FLAG_HIDDEN);
    key(TAB5_KEY_UP, 0);
    assert(lv_obj_has_state(rows[1], LV_STATE_USER_4));
    key(TAB5_KEY_DOWN, 0);
    lv_obj_add_state(rows[1], LV_STATE_DISABLED);
    key(TAB5_KEY_UP, 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    lv_obj_remove_state(rows[1], LV_STATE_DISABLED);
    key(TAB5_KEY_DOWN, 0);
    key(TAB5_KEY_UP, 0);
    lv_obj_move_to_index(rows[5], 0); /* Follow current filter/sort order. */
    key(TAB5_KEY_UP, 0);
    assert(lv_obj_has_state(rows[5], LV_STATE_USER_4));
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    lv_obj_move_to_index(rows[5], 5);
    for (unsigned i = 0; i < 30; ++i) key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[5], LV_STATE_USER_4));
    int32_t bottom = lv_obj_get_scroll_y(area);
    assert(lv_obj_get_scroll_bottom(area) == 0);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) == bottom);
    key(TAB5_KEY_UP, 0);
    int32_t before = lv_obj_get_scroll_y(area);
    assert(before <= bottom);
    assert(lv_obj_has_state(rows[4], LV_STATE_USER_4));
    key(TAB5_KEY_TEXT, ' ');
    key(TAB5_KEY_ENTER, 0);
    assert(clicks[6] == 0); /* Must not fall back to an unrelated button/tile. */

    /* Even when the list remains exposed, text editing owns the arrow keys. */
    lv_obj_t *ta = lv_textarea_create(lv_screen_active());
    lv_obj_set_pos(ta, 0, 500);
    lv_obj_set_size(ta, 400, 150);
    lv_textarea_set_text(ta, "one\ntwo");
    lv_textarea_set_cursor_pos(ta, 5);
    lv_obj_t *kb = app_keyboard_create(lv_screen_active());
    app_keyboard_set_textarea(kb, ta);
    key(TAB5_KEY_UP, 0);
    assert(lv_obj_get_scroll_y(area) == before);
    assert(lv_textarea_get_cursor_pos(ta) < 5);
    lv_obj_delete(kb);
    lv_obj_delete(ta);

    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 720, 1280);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) == before);
    lv_obj_delete(modal);
    lv_obj_add_flag(area, LV_OBJ_FLAG_HIDDEN);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) == before);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(area, LV_STATE_DISABLED);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) == before);
    lv_obj_remove_state(area, LV_STATE_DISABLED);
    for (unsigned i = 0; i < 30; ++i) key(TAB5_KEY_UP, 0);
    assert(lv_obj_get_scroll_y(area) == 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    app_keyboard_set_connected(false);
    assert(!lv_obj_has_state(rows[0], LV_STATE_USER_4));
    key(TAB5_KEY_DOWN, 0);
    assert(!lv_obj_has_state(rows[0], LV_STATE_USER_4));
    app_keyboard_set_connected(true);
    key(TAB5_KEY_DOWN, 0);
    for (unsigned i = 0; i < 6; ++i) {
        assert(clicks[i] == 0);
        assert(!lv_obj_has_state(checks[i], LV_STATE_CHECKED));
    }
    /* Rebuilding or emptying results must not leave stored row pointers. */
    lv_obj_clean(area);
    key(TAB5_KEY_TEXT, ' ');
    key(TAB5_KEY_ENTER, 0);
    assert(clicks[6] == 0); /* Refreshing rows retains the read-only context. */
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) == 0);
    for (unsigned i = 0; i < 6; ++i) rows[i] = button(area, 0, 0, i);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    for (unsigned i = 0; i < 6; ++i) key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(area) > 0);
    lv_obj_delete(outside);
    /* Generic fallback must never turn button-shaped result rows into
     * activatable controls, even after leaving browsing with Left/Right. */
    const tab5_key_kind_t horizontal[] = {TAB5_KEY_LEFT, TAB5_KEY_RIGHT};
    for (unsigned d = 0; d < 2; ++d) {
        key(TAB5_KEY_DOWN, 0);
        key(horizontal[d], 0);
        key(TAB5_KEY_TEXT, ' ');
        key(TAB5_KEY_ENTER, 0);
        for (unsigned i = 0; i < 6; ++i) assert(clicks[i] == 0);
    }
    lv_obj_delete(area);
    key(TAB5_KEY_DOWN, 0);
}
static void test_activatable_scroll(void)
{
    app_keyboard_set_connected(true);
    memset(clicks, 0, sizeof(clicks));
    lv_obj_t *area = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(area, 0, 100);
    lv_obj_set_size(area, 600, 400);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    /* Idempotent, and upgrading a plain scroll area keeps a single marker. */
    app_keyboard_navigation_register_scroll_area(area);
    app_keyboard_navigation_register_scroll_area_activatable(area);
    app_keyboard_navigation_register_scroll_area_activatable(area);
    lv_obj_t *rows[4];
    for (unsigned i = 0; i < 4; ++i) rows[i] = button(area, 0, 0, i);

    /* Space/Enter click the browsed row instead of falling through. */
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[0], LV_STATE_USER_4));
    key(TAB5_KEY_TEXT, ' ');
    assert(clicks[0] == 1);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_has_state(rows[1], LV_STATE_USER_4));
    key(TAB5_KEY_ENTER, 0);
    assert(clicks[1] == 1);
    assert(clicks[0] == 1); /* Only the browsed row is clicked. */

    /* With no browsed row, Enter/Space stay inert on the list. */
    app_keyboard_navigation_clear_selection();
    key(TAB5_KEY_TEXT, ' ');
    for (unsigned i = 0; i < 4; ++i) assert(clicks[i] == (i < 2 ? 1u : 0u));
    lv_obj_delete(area);
}
static void test_escape(void)
{
    app_keyboard_set_connected(true);
    memset(clicks, 0, sizeof(clicks));
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page, 720, 1280);
    lv_obj_t *back = button(page, 0, 0, 0);
    app_keyboard_navigation_register_escape(back);
    app_keyboard_navigation_register_escape(back); /* Idempotent registration. */
    lv_obj_add_event_cb(back, delete_parent, LV_EVENT_CLICKED, NULL);
    lv_obj_t *page_ta = lv_textarea_create(page);
    lv_obj_set_pos(page_ta, 220, 0);
    lv_obj_set_size(page_ta, 300, 60);
    lv_obj_t *page_kb = app_keyboard_create(page);
    app_keyboard_set_textarea(page_kb, page_ta);
    lv_obj_add_event_cb(page_kb, clicked, LV_EVENT_CANCEL, (void *)(uintptr_t)4);
    lv_obj_add_event_cb(page_kb, delete_parent, LV_EVENT_CANCEL, NULL);

    /* Same-screen dialog beats its parent's Back, even while editing. */
    lv_obj_t *dialog = lv_obj_create(page);
    lv_obj_set_size(dialog, 600, 600);
    lv_obj_set_pos(dialog, 30, 150);
    lv_obj_t *close = button(dialog, 0, 0, 1);
    app_keyboard_navigation_register_escape(close);
    lv_obj_add_event_cb(close, delete_parent, LV_EVENT_CLICKED, NULL);
    /* Unavailable exit in a partial dialog must consume Esc, rather than
     * activating the exposed Back or cancelling the exposed page editor. */
    lv_obj_add_state(close, LV_STATE_DISABLED);
    assert(key(TAB5_KEY_ESCAPE, 0));
    assert(clicks[0] == 0 && clicks[1] == 0 && clicks[4] == 0);
    lv_obj_remove_state(close, LV_STATE_DISABLED);
    lv_obj_add_flag(close, LV_OBJ_FLAG_HIDDEN);
    assert(key(TAB5_KEY_ESCAPE, 0));
    assert(clicks[0] == 0 && clicks[1] == 0 && clicks[4] == 0);
    lv_obj_remove_flag(close, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *ta = lv_textarea_create(dialog);
    lv_obj_set_pos(ta, 0, 150);
    lv_obj_set_size(ta, 300, 60);
    lv_textarea_set_text(ta, "draft");
    lv_obj_t *kb = app_keyboard_create(dialog);
    app_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, clicked, LV_EVENT_CANCEL, (void *)(uintptr_t)3);

    /* A top-layer modal with no exit must not activate covered controls. */
    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 720, 1280);
    assert(key(TAB5_KEY_ESCAPE, 0)); /* Covered exit consumes without clicking. */
    assert(clicks[0] == 0 && clicks[1] == 0 && clicks[3] == 0);
    /* A foreground editor without a Close button keeps its own Cancel, even
     * when a covered page has an exit. Exercise both layer and sibling order. */
    lv_obj_delete(modal);
    for (unsigned layer = 0; layer < 2; ++layer) {
        modal = lv_obj_create(layer ? lv_layer_top() : page);
        lv_obj_set_size(modal, 720, 1280);
        lv_obj_t *modal_ta = lv_textarea_create(modal);
        lv_obj_set_size(modal_ta, 300, 60);
        lv_obj_t *modal_kb = app_keyboard_create(modal);
        app_keyboard_set_textarea(modal_kb, modal_ta);
        lv_obj_add_event_cb(modal_kb, clicked, LV_EVENT_CANCEL, (void *)(uintptr_t)5);
        lv_obj_add_event_cb(modal_kb, delete_parent, LV_EVENT_CANCEL, NULL);
        assert(key(TAB5_KEY_ESCAPE, 0));
        assert(clicks[5] == layer + 1 && clicks[0] == 0 && clicks[1] == 0 && clicks[4] == 0);
    }
    modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 720, 1280);
    lv_obj_t *cancel = button(modal, 10, 10, 2);
    app_keyboard_navigation_register_escape(cancel);
    lv_obj_add_event_cb(cancel, delete_parent, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(cancel, LV_STATE_DISABLED);
    assert(key(TAB5_KEY_ESCAPE, 0));
    lv_obj_remove_state(cancel, LV_STATE_DISABLED);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_HIDDEN);
    assert(key(TAB5_KEY_ESCAPE, 0));
    lv_obj_remove_flag(cancel, LV_OBJ_FLAG_HIDDEN);
    app_keyboard_set_connected(false);
    assert(!key(TAB5_KEY_ESCAPE, 0));
    assert(clicks[2] == 0);
    app_keyboard_set_connected(true);
    assert(key(TAB5_KEY_ESCAPE, 0));
    assert(clicks[2] == 1 && clicks[0] == 0 && clicks[1] == 0);
    assert(key(TAB5_KEY_ESCAPE, 0));
    assert(clicks[1] == 1 && clicks[0] == 0 && clicks[3] == 0);
    assert(key(TAB5_KEY_ESCAPE, 0));
    assert(clicks[0] == 1);
    assert(!key(TAB5_KEY_ESCAPE, 0)); /* Root without Back is a no-op. */
}
int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(720, 1280);
    app_keyboard_set_connected(true);
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_set_pos(grid, 0, 100);
    lv_obj_set_size(grid, 720, 800);
    lv_obj_set_style_pad_all(grid, 10, 0);
    lv_obj_t *tiles[10];
    /* Reverse creation order to prove shortcuts use rendered positions. */
    for (int i = 9; i >= 0; --i) {
        tiles[i] = button(grid, (i % 3) * 225, (i / 3) * 140, (unsigned)i);
        app_keyboard_navigation_register_tile(tiles[i]);
    }
    assert(!key(TAB5_KEY_RIGHT, 0)); /* Establish selection without activation. */
    assert(lv_obj_get_style_outline_width(tiles[0], 0) == 3);
    key(TAB5_KEY_RIGHT, 0);
    key(TAB5_KEY_DOWN, 0);
    assert(key(TAB5_KEY_TEXT, ' '));
    assert(clicks[4] == 1);
    key(TAB5_KEY_LEFT, 0);
    key(TAB5_KEY_UP, 0);
    assert(key(TAB5_KEY_ENTER, 0));
    assert(clicks[0] == 1);
    for (int i = 0; i < 10; ++i) {
        unsigned before = clicks[i];
        assert(key(TAB5_KEY_TEXT, i == 9 ? '0' : (char)('1' + i)));
        assert(clicks[i] == before + 1);
    }
    lv_obj_add_state(tiles[1], LV_STATE_DISABLED);
    unsigned before = clicks[1];
    assert(!key(TAB5_KEY_TEXT, '2'));
    assert(clicks[1] == before); /* Disabled slot is not renumbered/activated. */
    lv_obj_remove_state(tiles[1], LV_STATE_DISABLED);
    lv_obj_add_flag(tiles[0], LV_OBJ_FLAG_HIDDEN);
    assert(key(TAB5_KEY_TEXT, '1'));
    assert(clicks[1] == before + 1);
    lv_obj_remove_flag(tiles[0], LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_size(bar, 720, 70);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_t *tabs[4];
    for (unsigned i = 0; i < 4; ++i) {
        tabs[i] = lv_button_create(bar);
        lv_obj_set_pos(tabs[i], (int)i * 175, 0);
        lv_obj_set_size(tabs[i], 165, 60);
        lv_obj_add_event_cb(tabs[i], tab_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)(10 + i));
    }
    app_keyboard_navigation_set_tabs(bar, tabs[0]);
    assert(key(TAB5_KEY_TAB, 0)); assert(clicks[11] == 1);
    lv_obj_add_flag(tabs[2], LV_OBJ_FLAG_HIDDEN);
    assert(key(TAB5_KEY_TAB, 0)); assert(clicks[13] == 1);
    assert(key(TAB5_KEY_TAB, 0)); assert(clicks[10] == 1);

    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 720, 1280);
    lv_obj_t *confirm = button(modal, 100, 120, 14);
    before = clicks[0];
    assert(!key(TAB5_KEY_TEXT, '1'));
    assert(clicks[0] == before);
    assert(!key(TAB5_KEY_TAB, 0));
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_style_outline_width(confirm, 0) == 3);
    lv_obj_add_event_cb(confirm, delete_parent, LV_EVENT_CLICKED, NULL);
    assert(key(TAB5_KEY_ENTER, 0));
    assert(clicks[14] == 1);

    /* Text editing owns digits, Space, arrows, and Tab while a field is active. */
    lv_obj_t *editor = lv_obj_create(lv_layer_top());
    lv_obj_set_size(editor, 720, 1280);
    lv_obj_t *ta = lv_textarea_create(editor);
    lv_obj_set_size(ta, 300, 60);
    lv_textarea_set_text(ta, "");
    lv_obj_t *kb = app_keyboard_create(editor);
    app_keyboard_set_textarea(kb, ta);
    before = clicks[0];
    assert(!key(TAB5_KEY_TEXT, '1'));
    assert(!key(TAB5_KEY_TEXT, ' '));
    assert(!strcmp(lv_textarea_get_text(ta), "1 "));
    assert(clicks[0] == before);
    before = clicks[11];
    assert(!key(TAB5_KEY_TAB, 0));
    assert(clicks[11] == before);
    lv_obj_delete(editor);

    /* An inline editor does not steal Tab from an accessible tab bar. */
    ta = lv_textarea_create(screen);
    lv_obj_set_size(ta, 300, 60);
    lv_obj_set_pos(ta, 10, 950);
    lv_textarea_set_text(ta, "");
    kb = app_keyboard_create(screen);
    app_keyboard_set_textarea(kb, ta);
    before = clicks[11];
    assert(key(TAB5_KEY_TAB, 0));
    assert(clicks[11] == before + 1);
    lv_obj_delete(kb);
    lv_obj_delete(ta);

    /* Render order is recalculated after a layout/orientation change. */
    lv_obj_set_x(tiles[0], 450);
    lv_obj_set_x(tiles[2], 0);
    before = clicks[2];
    assert(key(TAB5_KEY_TEXT, '1'));
    assert(clicks[2] == before + 1);

    /* Destroying selected widgets/tab bar and disconnecting must clear focus. */
    key(TAB5_KEY_RIGHT, 0);
    lv_obj_delete(bar);
    assert(!key(TAB5_KEY_TAB, 0));
    lv_obj_delete(grid);

    grid = lv_obj_create(screen);
    lv_obj_set_pos(grid, 0, 100);
    lv_obj_set_size(grid, 720, 180);
    lv_obj_set_style_pad_all(grid, 10, 0);
    lv_obj_t *first = button(grid, 0, 0, 0);
    lv_obj_t *below = button(grid, 0, 240, 15);
    app_keyboard_navigation_register_tile(first);
    app_keyboard_navigation_register_tile(below);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_style_outline_width(first, 0) == 3);
    assert(lv_obj_get_scroll_y(grid) == 0);
    key(TAB5_KEY_DOWN, 0);
    assert(lv_obj_get_scroll_y(grid) > 0);
    assert(lv_obj_get_style_outline_width(below, 0) == 3);
    assert(key(TAB5_KEY_ENTER, 0));
    assert(clicks[15] == 1);
    assert(key(TAB5_KEY_TEXT, '1')); /* Numbering follows the visible viewport. */
    assert(clicks[15] == 2);
    app_keyboard_set_connected(false);
    assert(!lv_obj_has_state(below, LV_STATE_USER_4));
    before = clicks[0];
    assert(!key(TAB5_KEY_TEXT, '1'));
    assert(clicks[0] == before);
    lv_obj_delete(grid);
    test_escape();
    test_read_only_scroll();
    test_activatable_scroll();
    lv_display_delete(display);
    lv_deinit();
    puts("Tab5 keyboard navigation: PASS");
}
