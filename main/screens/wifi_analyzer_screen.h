#pragma once

#include "lvgl.h"

/* LVGL-thread entry points. External ports are 0..2; INTERNAL is unsupported. */
lv_obj_t *wa_screen_show(lv_obj_t *parent, int tab, void (*on_back)(int tab));
void wa_screen_hide(int tab);
