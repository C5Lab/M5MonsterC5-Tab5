#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Start after LVGL init, while holding the display lock. The activity callback
 * runs on LVGL and returns false to consume a wake key or block a locked screen. */
/* state_cb is polled on LVGL (also while no keys are pressed). Return true when
 * revealing UI to discard input buffered before it was visible. */
esp_err_t tab5_keyboard_init(bool (*activity_cb)(void), bool (*state_cb)(bool connected));
