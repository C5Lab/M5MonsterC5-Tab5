#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <emscripten/emscripten.h>
#include "lvgl.h"

void app_init(void);
void app_tick(double elapsed_ms);
void emu_unavailable(const char *name);
EMSCRIPTEN_KEEPALIVE int emu_init(int rotation);
EMSCRIPTEN_KEEPALIVE void emu_tick(double elapsed_ms);
EMSCRIPTEN_KEEPALIVE void emu_pointer(int x, int y, int pressed);
EMSCRIPTEN_KEEPALIVE void emu_pointer_cancel(void);
EMSCRIPTEN_KEEPALIVE void emu_key(int key);
EMSCRIPTEN_KEEPALIVE void emu_text(const char *text);
EMSCRIPTEN_KEEPALIVE void emu_wheel(int x, int y, int delta);
EMSCRIPTEN_KEEPALIVE void emu_inspect(void);
EMSCRIPTEN_KEEPALIVE int emu_width(void);
EMSCRIPTEN_KEEPALIVE int emu_height(void);
EMSCRIPTEN_KEEPALIVE uintptr_t emu_framebuffer(void);
