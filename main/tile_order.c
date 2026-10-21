#include "tile_order.h"
#include <string.h>

void tile_order_default(tile_order_group_t group, tile_order_t *order)
{
    static const uint8_t monster[] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
    static const uint8_t internal[] = {TO_AUDITOR, TO_SETTINGS, TO_ADHOC};
    memset(order, 0, sizeof(*order));
    if (group == TO_MONSTER) {
        order->count = sizeof(monster);
        memcpy(order->ids, monster, sizeof(monster));
    } else if (group == TO_INTERNAL) {
        order->count = sizeof(internal);
        memcpy(order->ids, internal, sizeof(internal));
    }
}

bool tile_order_move(tile_order_t *order, size_t from, size_t to)
{
    if (!order || order->count > TILE_ORDER_MAX || from >= order->count || to >= order->count)
        return false;
    uint8_t id = order->ids[from];
    if (from < to) memmove(order->ids + from, order->ids + from + 1, to - from);
    if (from > to) memmove(order->ids + to + 1, order->ids + to, from - to);
    order->ids[to] = id;
    return true;
}

size_t tile_order_encode(const tile_order_t *order, uint8_t *blob, size_t capacity)
{
    if (!order || !blob || order->count > TILE_ORDER_MAX || capacity < (size_t)order->count + 2)
        return 0;
    blob[0] = 1;
    blob[1] = order->count;
    memcpy(blob + 2, order->ids, order->count);
    return (size_t)order->count + 2;
}

bool tile_order_decode(tile_order_group_t group, const uint8_t *blob, size_t size,
                       tile_order_t *order)
{
    tile_order_t defaults;
    tile_order_default(group, &defaults);
    if (!order || !defaults.count || !blob || size < 2 || blob[0] != 1 ||
        blob[1] > TILE_ORDER_MAX || size != (size_t)blob[1] + 2) return false;
    memset(order, 0, sizeof(*order));
    for (size_t pass = 0; pass < 2; ++pass) {
        const uint8_t *ids = pass ? defaults.ids : blob + 2;
        size_t count = pass ? defaults.count : blob[1];
        for (size_t i = 0; i < count; ++i) {
            if (memchr(defaults.ids, ids[i], defaults.count) &&
                !memchr(order->ids, ids[i], order->count)) order->ids[order->count++] = ids[i];
        }
    }
    return true;
}
