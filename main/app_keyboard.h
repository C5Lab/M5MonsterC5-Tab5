#pragma once

#include "lvgl.h"
#include "tab5_keyboard_protocol.h"

/* All calls run on the LVGL thread / with the display lock held. */
lv_obj_t *app_keyboard_create(lv_obj_t *parent);
void app_keyboard_set_textarea(lv_obj_t *keyboard, lv_obj_t *textarea);
void app_keyboard_set_visible(lv_obj_t *keyboard, bool visible);
void app_keyboard_set_connected(bool connected);
/* Textarea/spinbox caret styling; normal LVGL touch focus controls visibility. */
void app_keyboard_style_cursor(lv_obj_t *textarea, lv_color_t color);
/* True after an action/tab change/form close or blocked Esc; discard buffered keys. */
bool app_keyboard_input(const tab5_key_t *key);
