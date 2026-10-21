#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TILE_ORDER_MAX 13
#define TILE_ORDER_BLOB_SIZE (TILE_ORDER_MAX + 2)
typedef enum { TO_MONSTER, TO_INTERNAL, TO_GROUP_COUNT } tile_order_group_t;
/* Persisted IDs: never renumber or reuse these values. */
typedef enum {
    TO_WIFI_SCAN = 1, TO_GLOBAL_WIFI = 2, TO_COMPROMISED = 3,
    TO_DEAUTH = 4, TO_BLUETOOTH = 5, TO_OBSERVER = 6, TO_KARMA = 7,
    TO_WARDRIVE = 8, TO_ANTISURV = 9, TO_MESH = 10, TO_SUBGHZ = 11,
    TO_NFC = 12, TO_ANALYZER = 13,
    TO_AUDITOR = 32, TO_SETTINGS = 33, TO_ADHOC = 34
} tile_order_id_t;
typedef struct { uint8_t count; uint8_t ids[TILE_ORDER_MAX]; } tile_order_t;
void tile_order_default(tile_order_group_t group, tile_order_t *order);
bool tile_order_move(tile_order_t *order, size_t from, size_t to);
size_t tile_order_encode(const tile_order_t *order, uint8_t *blob, size_t capacity);
bool tile_order_decode(tile_order_group_t group, const uint8_t *blob, size_t size,
                       tile_order_t *order);
