#include <assert.h>
#include <stdio.h>
#include "../tools/ui_emulator/runtime/coordinates.h"

int main(void) {
    /* Hand-derived raw panel positions for a logical point (100, 200). */
    const int expected[4][2] = {{100,200},{200,1179},{619,1079},{519,100}};
    for (int r = 0; r < 4; r++) {
        int x = 100, y = 200;
        emu_logical_to_raw(r, &x, &y);
        assert(x == expected[r][0] && y == expected[r][1]);
        int w = r % 2 ? 1280 : 720, h = r % 2 ? 720 : 1280;
        for (int ly = 0; ly < h; ly++) for (int lx = 0; lx < w; lx++) {
            x = lx; y = ly;
            emu_logical_to_raw(r, &x, &y);
            assert(x >= 0 && x < 720 && y >= 0 && y < 1280);
            /* LVGL's raw-to-logical pointer processing, from lv_indev.c. */
            if (r == 2 || r == 3) { x = 719 - x; y = 1279 - y; }
            if (r == 1 || r == 3) { int tmp = y; y = x; x = 1279 - tmp; }
            assert(x == lx && y == ly);
        }
    }
    puts("PASS: all 3,686,400 logical pixels round-trip through LVGL rotation");
}
