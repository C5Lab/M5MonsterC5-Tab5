#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Persisted IDs: append new fields and bump the schema; never renumber. */
typedef enum {
    OBS_FIELD_SECURITY, OBS_FIELD_PMF, OBS_FIELD_WPS, OBS_FIELD_CHANNEL,
    OBS_FIELD_RSSI, OBS_FIELD_AGE, OBS_FIELD_BSSID, OBS_FIELD_UPTIME,
    OBS_FIELD_VENDOR, OBS_FIELD_PAIRWISE, OBS_FIELD_GROUP, OBS_FIELD_AKM,
    OBS_FIELD_SOURCE, OBS_FIELD_COUNT
} observer_field_t;
typedef enum {
    OBS_CLIENT_FIELD_VENDOR, OBS_CLIENT_FIELD_RSSI, OBS_CLIENT_FIELD_AGE,
    OBS_CLIENT_FIELD_COUNT
} observer_client_field_t;
#define OBS_CLIENT_VENDOR OBS_CLIENT_FIELD_VENDOR
#define OBS_CLIENT_RSSI OBS_CLIENT_FIELD_RSSI
#define OBS_CLIENT_AGE OBS_CLIENT_FIELD_AGE
#define OBS_PREFS_VERSION 1
typedef struct {
    uint8_t version;
    uint8_t ap_order[OBS_FIELD_COUNT];
    uint8_t ap_visible[OBS_FIELD_COUNT];
    uint8_t client_order[OBS_CLIENT_FIELD_COUNT];
    uint8_t client_visible[OBS_CLIENT_FIELD_COUNT];
} observer_view_prefs_t;

static inline void observer_prefs_defaults(observer_view_prefs_t *p) {
    static const uint8_t order[] = {OBS_FIELD_SECURITY, OBS_FIELD_PMF,
        OBS_FIELD_WPS, OBS_FIELD_CHANNEL, OBS_FIELD_RSSI, OBS_FIELD_BSSID,
        OBS_FIELD_UPTIME, OBS_FIELD_VENDOR, OBS_FIELD_AGE, OBS_FIELD_PAIRWISE,
        OBS_FIELD_GROUP, OBS_FIELD_AKM, OBS_FIELD_SOURCE};
    memset(p, 0, sizeof(*p));
    p->version = OBS_PREFS_VERSION;
    memcpy(p->ap_order, order, sizeof(order));
    for (size_t i = 0; i < 8; ++i) p->ap_visible[order[i]] = 1;
    for (size_t i = 0; i < OBS_CLIENT_FIELD_COUNT; ++i) {
        p->client_order[i] = (uint8_t)i;
        p->client_visible[i] = 1;
    }
}
static inline bool observer_prefs_list_valid(const uint8_t *order,
        const uint8_t *visible, size_t count) {
    uint32_t seen = 0;
    for (size_t i = 0; i < count; ++i) {
        if (order[i] >= count || visible[i] > 1) return false;
        uint32_t bit = (uint32_t)1 << order[i];
        if (seen & bit) return false;
        seen |= bit;
    }
    return true;
}
static inline bool observer_prefs_valid(const observer_view_prefs_t *p) {
    return p && p->version == OBS_PREFS_VERSION &&
        observer_prefs_list_valid(p->ap_order, p->ap_visible, OBS_FIELD_COUNT) &&
        observer_prefs_list_valid(p->client_order, p->client_visible, OBS_CLIENT_FIELD_COUNT);
}
static inline bool observer_prefs_equal(const observer_view_prefs_t *a,
        const observer_view_prefs_t *b) { return memcmp(a, b, sizeof(*a)) == 0; }
/* Invalid stored data restores a complete coherent default configuration. */
static inline bool observer_prefs_decode(const void *blob, size_t size,
        observer_view_prefs_t *out) {
    observer_view_prefs_t candidate;
    if (blob && size == sizeof(candidate)) {
        memcpy(&candidate, blob, size);
        if (observer_prefs_valid(&candidate)) { *out = candidate; return true; }
    }
    observer_prefs_defaults(out);
    return false;
}
static inline bool observer_prefs_move(uint8_t *order, size_t count,
        size_t position, int direction) {
    if (position >= count || (direction != -1 && direction != 1) ||
        (direction == -1 && position == 0) ||
        (direction == 1 && position + 1 >= count)) return false;
    size_t next = direction < 0 ? position - 1 : position + 1;
    uint8_t tmp = order[position]; order[position] = order[next]; order[next] = tmp;
    return true;
}
