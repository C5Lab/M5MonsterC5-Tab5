#include "app_keyboard_navigation.h"
#include <limits.h>
#include <stdlib.h>

/* All operations are synchronous on LVGL. Keep traversal storage off its task
 * stack; activation callbacks run only after the candidate list is consumed. */
#define MAX_CANDIDATES 128
static lv_obj_t *candidates[MAX_CANDIDATES];
static unsigned candidate_count;
static lv_obj_t *selected;
/* Browsing is deliberately separate from the pointer used by activate(). */
static lv_obj_t *browse_area;
static lv_obj_t *browse_row;
static lv_obj_t *tab_bar;
static lv_obj_t *active_tab;
static lv_style_t focus_style;
static bool style_ready;

static void selected_deleted(lv_event_t *event)
{
    if (lv_event_get_current_target(event) == selected) selected = NULL;
}

static void browse_row_deleted(lv_event_t *event)
{
    if (lv_event_get_current_target(event) == browse_row) browse_row = NULL;
    /* Keep the area as the read-only input context while results rebuild. */
}

static void clear_browse_row(void)
{
    if (!browse_row) return;
    lv_obj_t *old = browse_row;
    browse_row = NULL;
    lv_obj_remove_event_cb(old, browse_row_deleted);
    lv_obj_remove_state(old, LV_STATE_USER_4);
    lv_obj_remove_style(old, &focus_style, LV_PART_MAIN | LV_STATE_USER_4);
}

void app_keyboard_navigation_clear_selection(void)
{
    browse_area = NULL;
    clear_browse_row();
    if (!selected) return;
    lv_obj_t *old = selected;
    selected = NULL;
    lv_obj_remove_event_cb(old, selected_deleted);
    lv_obj_remove_state(old, LV_STATE_USER_4);
    lv_obj_remove_style(old, &focus_style, LV_PART_MAIN | LV_STATE_USER_4);
}

static void ensure_focus_style(void)
{
    if (!style_ready) {
        lv_style_init(&focus_style);
        lv_style_set_outline_width(&focus_style, 3);
        /* LVGL expands by width + pad: 3 - 7 = -4, so the entire frame
         * sits 4 px inside the tile. This avoids scroll-container clipping
         * at the top edge in landscape without changing the tile layout. */
        lv_style_set_outline_pad(&focus_style, -7);
        lv_style_set_outline_color(&focus_style, lv_color_hex(0xE63A9D));
        lv_style_set_outline_opa(&focus_style, LV_OPA_COVER);
        style_ready = true;
    }
}

static void select_object(lv_obj_t *obj)
{
    if (selected == obj) return;
    app_keyboard_navigation_clear_selection();
    if (!obj) return;
    ensure_focus_style();
    selected = obj;
    lv_obj_add_event_cb(obj, selected_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_add_style(obj, &focus_style, LV_PART_MAIN | LV_STATE_USER_4);
    lv_obj_add_state(obj, LV_STATE_USER_4);
}

static void browse_object(lv_obj_t *area, lv_obj_t *row)
{
    if (browse_area == area && browse_row == row) return;
    app_keyboard_navigation_clear_selection();
    browse_area = area;
    browse_row = row;
    if (!row) return;
    ensure_focus_style();
    lv_obj_add_event_cb(row, browse_row_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_add_style(row, &focus_style, LV_PART_MAIN | LV_STATE_USER_4);
    lv_obj_add_state(row, LV_STATE_USER_4);
}

/* An event descriptor marks a tile without claiming its user_data or a flag
 * used by other widgets. LVGL owns and deletes the descriptor with the tile. */
static void tile_marker(lv_event_t *event) { (void)event; }
static void escape_marker(lv_event_t *event) { (void)event; }
static void scroll_marker(lv_event_t *event)
{
    if (lv_event_get_current_target(event) == browse_area)
        app_keyboard_navigation_clear_selection();
}

static bool is_scroll_area(lv_obj_t *obj)
{
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == scroll_marker) return true;
    return false;
}

/* Opt-in companion to scroll_marker: rows in an area carrying this descriptor
 * accept Enter/Space, which clicks the browsed row (e.g. toggles its checkbox).
 * A plain scroll area stays read-only. */
static void activate_marker(lv_event_t *event) { (void)event; }

static bool is_activatable_area(lv_obj_t *obj)
{
    if (!obj) return false;
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == activate_marker) return true;
    return false;
}

void app_keyboard_navigation_register_scroll_area(lv_obj_t *area)
{
    if (!area) return;
    for (uint32_t i = 0; i < lv_obj_get_event_count(area); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(area, i)) == scroll_marker) return;
    lv_obj_add_event_cb(area, scroll_marker, LV_EVENT_DELETE, NULL);
}

void app_keyboard_navigation_register_scroll_area_activatable(lv_obj_t *area)
{
    if (!area) return;
    app_keyboard_navigation_register_scroll_area(area);
    if (is_activatable_area(area)) return;
    lv_obj_add_event_cb(area, activate_marker, LV_EVENT_DELETE, NULL);
}

void app_keyboard_navigation_register_escape(lv_obj_t *button)
{
    if (!button) return;
    for (uint32_t i = 0; i < lv_obj_get_event_count(button); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(button, i)) == escape_marker) return;
    lv_obj_add_event_cb(button, escape_marker, LV_EVENT_DELETE, NULL);
}

static bool is_tile(lv_obj_t *obj)
{
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == tile_marker) return true;
    return false;
}

void app_keyboard_navigation_register_tile(lv_obj_t *tile)
{
    if (tile && !is_tile(tile)) lv_obj_add_event_cb(tile, tile_marker, LV_EVENT_DELETE, NULL);
}

static void tabs_deleted(lv_event_t *event)
{
    (void)event;
    tab_bar = NULL;
    active_tab = NULL;
    app_keyboard_navigation_clear_selection();
}

void app_keyboard_navigation_set_tabs(lv_obj_t *bar, lv_obj_t *active)
{
    if (bar != tab_bar) {
        if (tab_bar) lv_obj_remove_event_cb(tab_bar, tabs_deleted);
        tab_bar = bar;
        if (bar) lv_obj_add_event_cb(bar, tabs_deleted, LV_EVENT_DELETE, NULL);
    }
    if (active_tab != active) app_keyboard_navigation_clear_selection();
    active_tab = active;
}

static bool shown(lv_obj_t *obj, bool enabled)
{
    if (!obj) return false;
    for (lv_obj_t *p = obj; p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) return false;
        if (enabled && lv_obj_has_state(p, LV_STATE_DISABLED)) return false;
    }
    lv_obj_t *root = lv_obj_get_screen(obj);
    return root == lv_screen_active() || root == lv_layer_top() || root == lv_layer_sys();
}

static bool reachable(lv_obj_t *obj)
{
    if (!shown(obj, false)) return false;
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_point_t point = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    lv_obj_t *hit = lv_indev_search_obj(lv_layer_sys(), &point);
    if (!hit) hit = lv_indev_search_obj(lv_layer_top(), &point);
    if (!hit) hit = lv_indev_search_obj(lv_screen_active(), &point);
    for (; hit; hit = lv_obj_get_parent(hit)) if (hit == obj) return true;
    return false;
}

static lv_obj_t *find_scroll_area(lv_obj_t *root)
{
    if (!shown(root, true)) return NULL;
    for (uint32_t i = lv_obj_get_child_count(root); i > 0; --i) {
        lv_obj_t *area = find_scroll_area(lv_obj_get_child(root, i - 1));
        if (area) return area;
    }
    if (!lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE) ||
        !(lv_obj_get_scroll_dir(root) & LV_DIR_VER)) return NULL;
    return is_scroll_area(root) && reachable(root) ? root : NULL;
}

static bool row_in_view(lv_obj_t *area, lv_obj_t *row)
{
    lv_area_t viewport, bounds;
    lv_obj_get_content_coords(area, &viewport);
    lv_obj_get_coords(row, &bounds);
    return bounds.y2 >= viewport.y1 && bounds.y1 <= viewport.y2 &&
           bounds.x2 >= viewport.x1 && bounds.x1 <= viewport.x2;
}

static bool browsable_row(lv_obj_t *row)
{
    return shown(row, true) && lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE);
}

static bool browse_vertical(tab5_key_kind_t direction)
{
    lv_obj_t *area = find_scroll_area(lv_layer_sys());
    if (!area) area = find_scroll_area(lv_layer_top());
    if (!area) area = find_scroll_area(lv_screen_active());
    if (!area) return false;
    lv_obj_t *next = NULL;
    int32_t count = (int32_t)lv_obj_get_child_count(area);
    if (browse_area == area && browse_row) {
        /* Filter/sort changes reorder direct children of the column layout. */
        int32_t step = direction == TAB5_KEY_DOWN ? 1 : -1;
        for (int32_t i = lv_obj_get_index(browse_row) + step; i >= 0 && i < count; i += step) {
            lv_obj_t *row = lv_obj_get_child(area, i);
            if (browsable_row(row)) { next = row; break; }
        }
        if (!next) next = browse_row; /* Stop at the boundary; no wrapping. */
    } else {
        lv_obj_t *first = NULL;
        for (int32_t i = 0; i < count; ++i) {
            lv_obj_t *row = lv_obj_get_child(area, i);
            if (!browsable_row(row)) continue;
            if (!first) first = row;
            if (row_in_view(area, row)) { next = row; break; }
        }
        if (!next) next = first;
    }
    browse_object(area, next);
    /* No row CLICKED, VALUE_CHANGED, FOCUSED or checkbox changes. Scrolling
     * callbacks may rebuild the list, so do not access next after this call. */
    if (next) lv_obj_scroll_to_view(next, LV_ANIM_OFF);
    return true;
}

static bool is_escape(lv_obj_t *obj)
{
    for (uint32_t i = 0; i < lv_obj_get_event_count(obj); ++i)
        if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(obj, i)) == escape_marker) return true;
    return false;
}

static lv_obj_t *find_escape(lv_obj_t *root)
{
    /* A hidden/disabled exit in a visible dialog still owns Esc. Do not fall
     * through to an exposed Back (or text editor) on the page underneath.
     * Entire hidden pages/dialogs, however, must not participate. */
    if (!shown(root, false))
        return is_escape(root) && shown(lv_obj_get_parent(root), false) ? root : NULL;
    /* LVGL paints later siblings above earlier ones. Search front to back so
     * a dialog closes before the page behind it, including nested dialogs. */
    for (uint32_t i = lv_obj_get_child_count(root); i > 0; --i) {
        lv_obj_t *button = find_escape(lv_obj_get_child(root, i - 1));
        if (button) return button;
    }
    return is_escape(root) ? root : NULL;
}

static bool contains(lv_obj_t *parent, lv_obj_t *obj)
{
    for (; obj; obj = lv_obj_get_parent(obj)) if (obj == parent) return true;
    return false;
}

static bool editor_above_exit(lv_obj_t *editor, lv_obj_t *button)
{
    /* An explicit exit belonging to this form always takes priority. */
    if (!editor || contains(editor, button) || contains(button, editor)) return false;
    lv_obj_t *editor_root = lv_obj_get_screen(editor);
    lv_obj_t *button_root = lv_obj_get_screen(button);
    if (editor_root != button_root) {
        int editor_layer = editor_root == lv_layer_sys() ? 2 : editor_root == lv_layer_top() ? 1 : 0;
        int button_layer = button_root == lv_layer_sys() ? 2 : button_root == lv_layer_top() ? 1 : 0;
        return editor_layer > button_layer;
    }
    /* Compare sibling branches at their common ancestor, not field/button
     * creation order inside one form. Later branches are painted in front. */
    for (lv_obj_t *branch = editor; lv_obj_get_parent(branch); branch = lv_obj_get_parent(branch)) {
        lv_obj_t *parent = lv_obj_get_parent(branch);
        if (!contains(parent, button)) continue;
        for (uint32_t i = lv_obj_get_child_count(parent); i > 0; --i) {
            lv_obj_t *child = lv_obj_get_child(parent, i - 1);
            if (child == branch) return true;
            if (contains(child, button)) return false;
        }
    }
    return false;
}

bool app_keyboard_navigation_escape(lv_obj_t *editor)
{
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
    lv_obj_update_layout(lv_layer_sys());
    app_keyboard_navigation_clear_selection();
    lv_obj_t *button = find_escape(lv_layer_sys());
    if (!button) button = find_escape(lv_layer_top());
    if (!button) button = find_escape(lv_screen_active());
    if (!button || editor_above_exit(editor, button)) return false;
    if (!shown(button, true) || !lv_obj_has_flag(button, LV_OBJ_FLAG_CLICKABLE) ||
        !reachable(button)) return true; /* Consume blocked Esc; no fallback. */
    /* Exactly one existing action; it may delete the page or open a dialog. */
    lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    return true;
}

static void collect(lv_obj_t *root, bool tiles_only)
{
    /* Read-only list contents never participate in generic activation, even
     * when rows happen to use button widgets or tile markers. */
    if (root == tab_bar || is_scroll_area(root) || !shown(root, false)) return;
    bool tile = is_tile(root);
    if ((tile || (!tiles_only && lv_obj_check_type(root, &lv_button_class))) &&
        lv_obj_has_flag(root, LV_OBJ_FLAG_CLICKABLE) && reachable(root) &&
        candidate_count < MAX_CANDIDATES) candidates[candidate_count++] = root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        collect(lv_obj_get_child(root, i), tiles_only);
}

static int visual_order(const void *a, const void *b)
{
    lv_area_t aa, bb;
    lv_obj_get_coords(*(lv_obj_t *const *)a, &aa);
    lv_obj_get_coords(*(lv_obj_t *const *)b, &bb);
    if (aa.y1 != bb.y1) return aa.y1 < bb.y1 ? -1 : 1;
    if (aa.x1 != bb.x1) return aa.x1 < bb.x1 ? -1 : 1;
    return 0;
}

static void collect_visible(bool tiles_only)
{
    candidate_count = 0;
    collect(lv_screen_active(), tiles_only);
    collect(lv_layer_top(), tiles_only);
    collect(lv_layer_sys(), tiles_only);
    qsort(candidates, candidate_count, sizeof(*candidates), visual_order);
}

static bool activate(lv_obj_t *obj)
{
    if (!shown(obj, true) || !reachable(obj)) return false;
    select_object(obj);
    /* Use the same application callback as a touch click. It may delete any
     * widget, so do not read obj, selection, or candidates after dispatch. */
    lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
    return true;
}

static bool next_tab(void)
{
    if (!shown(tab_bar, true)) return false;
    uint32_t count = lv_obj_get_child_count(tab_bar);
    int32_t start = -1;
    for (uint32_t i = 0; i < count; ++i)
        if (lv_obj_get_child(tab_bar, i) == active_tab) start = (int32_t)i;
    for (uint32_t step = 1; step <= count; ++step) {
        lv_obj_t *obj = lv_obj_get_child(tab_bar, (start + (int32_t)step) % (int32_t)count);
        if (obj != active_tab && lv_obj_check_type(obj, &lv_button_class) &&
            shown(obj, true) && reachable(obj)) {
            app_keyboard_navigation_clear_selection();
            lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL);
            return true;
        }
    }
    return false;
}

static int64_t directional_distance(lv_obj_t *from, lv_obj_t *to, tab5_key_kind_t direction)
{
    lv_area_t a, b;
    lv_obj_get_coords(from, &a);
    lv_obj_get_coords(to, &b);
    int64_t dx = (int64_t)b.x1 + b.x2 - a.x1 - a.x2;
    int64_t dy = (int64_t)b.y1 + b.y2 - a.y1 - a.y2;
    int64_t forward, cross;
    if (direction == TAB5_KEY_LEFT || direction == TAB5_KEY_RIGHT) {
        forward = direction == TAB5_KEY_RIGHT ? dx : -dx;
        cross = dy;
    } else {
        forward = direction == TAB5_KEY_DOWN ? dy : -dy;
        cross = dx;
    }
    return forward > 0 ? forward * forward + 4 * cross * cross : INT64_MAX;
}

bool app_keyboard_navigation_input(const tab5_key_t *key)
{
    if (!key) return false;
    if (key->kind == TAB5_KEY_ESCAPE) return app_keyboard_navigation_escape(NULL);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
    lv_obj_update_layout(lv_layer_sys());
    if (selected && (!shown(selected, true) || !reachable(selected)))
        app_keyboard_navigation_clear_selection();
    if (browse_area && (!shown(browse_area, true) || !reachable(browse_area)))
        app_keyboard_navigation_clear_selection();
    if (browse_row && (!browsable_row(browse_row) ||
        lv_obj_get_parent(browse_row) != browse_area || !row_in_view(browse_area, browse_row)))
        clear_browse_row();
    if (key->kind == TAB5_KEY_TAB) return next_tab();
    if ((key->kind == TAB5_KEY_UP || key->kind == TAB5_KEY_DOWN) &&
        browse_vertical(key->kind)) return false;
    bool number = key->kind == TAB5_KEY_TEXT && key->character >= '0' && key->character <= '9';
    bool accept = key->kind == TAB5_KEY_ENTER || (key->kind == TAB5_KEY_TEXT && key->character == ' ');
    if (accept && browse_area) {
        /* Read-only lists ignore Enter/Space. A list registered as activatable
         * clicks the browsed row instead, using the same event a touch produces
         * so the row toggles its own checkbox and updates the selection. The
         * click may reorder/hide rows in place, so do not touch browse_row
         * afterwards. */
        if (browse_row && is_activatable_area(browse_area) &&
            browsable_row(browse_row) && reachable(browse_row)) {
            lv_obj_send_event(browse_row, LV_EVENT_CLICKED, NULL);
            return true; /* State changed; discard buffered keys. */
        }
        return false; /* Never activate a browsed result in a read-only list. */
    }
    bool arrow = key->kind == TAB5_KEY_LEFT || key->kind == TAB5_KEY_RIGHT ||
                 key->kind == TAB5_KEY_UP || key->kind == TAB5_KEY_DOWN;
    if (!number && !accept && !arrow) return false;
    if (arrow && browse_area) app_keyboard_navigation_clear_selection();

    collect_visible(true);
    if (number) {
        unsigned index = key->character == '0' ? 9u : (unsigned)(key->character - '1');
        return index < candidate_count ? activate(candidates[index]) : false;
    }
    if (!candidate_count) collect_visible(false);
    if (!selected) {
        for (unsigned i = 0; i < candidate_count; ++i) {
            if (shown(candidates[i], true)) { select_object(candidates[i]); break; }
        }
        /* The first arrow establishes a visible starting point. */
        if (arrow) return false;
    }
    if (!selected) return false;
    if (accept) return activate(selected);

    lv_obj_t *next = NULL;
    int64_t best = INT64_MAX;
    for (unsigned i = 0; i < candidate_count; ++i) {
        if (!shown(candidates[i], true)) continue;
        int64_t distance = directional_distance(selected, candidates[i], key->kind);
        if (distance < best) { best = distance; next = candidates[i]; }
    }
    /* Continue through a scrolling tile grid even beyond the viewport, while
     * restricting off-screen choices to siblings of the unobstructed selection. */
    if (is_tile(selected)) {
        lv_obj_t *parent = lv_obj_get_parent(selected);
        for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
            lv_obj_t *obj = lv_obj_get_child(parent, i);
            if (!is_tile(obj) || !shown(obj, true)) continue;
            int64_t distance = directional_distance(selected, obj, key->kind);
            if (distance < best) { best = distance; next = obj; }
        }
    }
    if (next) {
        lv_obj_scroll_to_view(next, LV_ANIM_OFF);
        lv_obj_update_layout(next);
        if (reachable(next)) select_object(next);
    }
    return false;
}
