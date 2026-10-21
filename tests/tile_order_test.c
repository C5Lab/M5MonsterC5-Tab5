#include "tile_order.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    tile_order_t order;
    tile_order_default(TO_MONSTER, &order);
    assert(order.count == 13 && order.ids[0] == TO_WIFI_SCAN);
    assert(tile_order_move(&order, 7, 1));
    assert(order.ids[1] == TO_WARDRIVE && order.ids[2] == TO_GLOBAL_WIFI);
    assert(!tile_order_move(&order, 13, 0));
    uint8_t blob[TILE_ORDER_BLOB_SIZE];
    size_t size = tile_order_encode(&order, blob, sizeof(blob));
    tile_order_t decoded;
    assert(tile_order_decode(TO_MONSTER, blob, size, &decoded));
    assert(memcmp(order.ids, decoded.ids, order.count) == 0);
    /* Firmware upgrades: preserve user prefix, discard unknown/duplicate IDs,
       append tools added since the saved layout. Optional hardware IDs survive. */
    uint8_t old[] = {1, 5, TO_NFC, TO_WARDRIVE, TO_NFC, 255, TO_SUBGHZ};
    assert(tile_order_decode(TO_MONSTER, old, sizeof(old), &decoded));
    assert(decoded.count == 13 && decoded.ids[0] == TO_NFC);
    assert(decoded.ids[1] == TO_WARDRIVE && decoded.ids[2] == TO_SUBGHZ);
    assert(decoded.ids[3] == TO_WIFI_SCAN);
    old[0] = 2;
    assert(!tile_order_decode(TO_MONSTER, old, sizeof(old), &decoded));
    old[0] = 1;
    assert(!tile_order_decode(TO_MONSTER, old, sizeof(old) - 1, &decoded));
    assert(!tile_order_decode(TO_MONSTER, NULL, 0, &decoded));
    tile_order_default(TO_INTERNAL, &order);
    assert(order.count == 3 && order.ids[1] == TO_SETTINGS);
    assert(tile_order_move(&order, 2, 0));
    assert(order.ids[0] == TO_ADHOC && order.ids[1] == TO_AUDITOR);
    size = tile_order_encode(&order, blob, sizeof(blob));
    assert(tile_order_decode(TO_INTERNAL, blob, size, &decoded));
    assert(decoded.ids[0] == TO_ADHOC);
    assert(tile_order_encode(&order, blob, 2) == 0);
    puts("tile_order: all tests passed");
    return 0;
}
