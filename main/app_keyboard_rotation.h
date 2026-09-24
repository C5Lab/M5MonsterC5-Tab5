#pragma once

#include "lvgl.h"
#include <stdbool.h>

/* LVGL thread only. apply must persist 90 degrees before restarting, returning
 * false on save failure. The optional style callback inherits the app theme. */
void app_keyboard_rotation_init(bool (*apply)(void),
    void (*style)(lv_obj_t *overlay, lv_obj_t *card, lv_obj_t *no, lv_obj_t *yes));
/* Poll after updating keyboard connectivity. True means a dialog was revealed:
 * discard buffered keys so they cannot accidentally answer the new question. */
bool app_keyboard_rotation_update(bool connected, bool can_show, bool needs_rotation);
