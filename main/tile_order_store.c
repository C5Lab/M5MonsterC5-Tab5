#include "tile_order_store.h"
#include "nvs.h"
#include <string.h>

static const char *key(tile_order_group_t group)
{
    return group == TO_MONSTER ? "monster" : "internal";
}

esp_err_t tile_order_load(tile_order_group_t group, tile_order_t *order)
{
    if (group >= TO_GROUP_COUNT || !order) return ESP_ERR_INVALID_ARG;
    tile_order_default(group, order);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tool_order", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t blob[TILE_ORDER_BLOB_SIZE];
    size_t size = sizeof(blob);
    err = nvs_get_blob(handle, key(group), blob, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    return tile_order_decode(group, blob, size, order) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t tile_order_save(tile_order_group_t group, const tile_order_t *order, bool reset)
{
    if (group >= TO_GROUP_COUNT || !order) return ESP_ERR_INVALID_ARG;
    uint8_t blob[TILE_ORDER_BLOB_SIZE];
    size_t size = tile_order_encode(order, blob, sizeof(blob));
    tile_order_t normalized;
    if (!size || !tile_order_decode(group, blob, size, &normalized) ||
        normalized.count != order->count || memcmp(normalized.ids, order->ids, order->count))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tool_order", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    if (reset) {
        err = nvs_erase_key(handle, key(group));
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else err = nvs_set_blob(handle, key(group), blob, size);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
