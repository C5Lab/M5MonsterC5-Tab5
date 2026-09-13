#pragma once

/* The Canvas exposes logical pixels; LVGL expects raw portrait-panel input. */
static inline void emu_logical_to_raw(int rotation, int *x, int *y) {
    int logical_x = *x, logical_y = *y;
    switch (rotation) {
        case 1: *x = logical_y; *y = 1279 - logical_x; break;
        case 2: *x = 719 - logical_x; *y = 1279 - logical_y; break;
        case 3: *x = 719 - logical_y; *y = logical_x; break;
        default: break;
    }
}
