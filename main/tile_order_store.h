#pragma once
#include "tile_order.h"
#include "esp_err.h"
/* An error still returns defaults; callers must not auto-write the fallback. */
esp_err_t tile_order_load(tile_order_group_t group, tile_order_t *order);
esp_err_t tile_order_save(tile_order_group_t group, const tile_order_t *order, bool reset);
