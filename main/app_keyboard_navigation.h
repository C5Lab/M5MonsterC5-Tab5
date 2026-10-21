#pragma once

#include "lvgl.h"
#include "tab5_keyboard_protocol.h"

/* LVGL thread only. Tile numbers follow current rendered row/column order. */
void app_keyboard_navigation_register_tile(lv_obj_t *tile);
/* Up/Down browse direct rows and reveal them without clicking/checking them.
 * Enter/Space do nothing in this read-only context. Use a vertical column. */
void app_keyboard_navigation_register_scroll_area(lv_obj_t *area);
/* Same browsing, but Enter/Space click the browsed row (e.g. a row that toggles
 * its own selection checkbox). The click may reorder/hide rows in place. */
void app_keyboard_navigation_register_scroll_area_activatable(lv_obj_t *area);
/* Explicit existing Back/Close/Cancel control; never infer actions from labels. */
void app_keyboard_navigation_register_escape(lv_obj_t *button);
void app_keyboard_navigation_set_tabs(lv_obj_t *bar, lv_obj_t *active);
void app_keyboard_navigation_clear_selection(void);
/* editor is the active keyboard's parent, or NULL. A foreground editor with
 * no registered exit may use its own Cancel fallback. */
bool app_keyboard_navigation_escape(lv_obj_t *editor);
/* True after activation/tab change or blocked Esc: discard buffered keys. */
bool app_keyboard_navigation_input(const tab5_key_t *key);
