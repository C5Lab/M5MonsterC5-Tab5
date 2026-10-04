#pragma once
#include "lvgl.h"
#include "tile_order.h"

typedef struct {
    uint8_t id;
    const char *icon;
    const char *label;
    lv_color_t accent;
} tool_order_item_t;
typedef struct {
    lv_color_t background, card, text, accent;
    const tool_order_item_t *items;
    size_t item_count;
    void (*saved)(void);
} tool_order_screen_config_t;
/* Singleton editor on the top layer. Configuration is copied, item registry
 * must remain alive until the editor closes. All calls run on the LVGL thread. */
void tool_order_screen_show(const tool_order_screen_config_t *config);
