#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCAN_FILTER_QUERY_MAX 32

typedef enum {
    SCAN_SORT_DEFAULT = 0,
    SCAN_SORT_NAME,
    SCAN_SORT_SIGNAL,
    SCAN_SORT_CHANNEL,
} scan_sort_key_t;

typedef enum {
    SCAN_VISIBILITY_ALL = 0,
    SCAN_VISIBILITY_NAMED,
    SCAN_VISIBILITY_HIDDEN,
} scan_visibility_t;

enum {
    SCAN_SECURITY_ALL     = 0,
    SCAN_SECURITY_OPEN    = 1U << 0,
    SCAN_SECURITY_WPA     = 1U << 1,
    SCAN_SECURITY_WPA2    = 1U << 2,
    SCAN_SECURITY_WPA3    = 1U << 3,
    SCAN_SECURITY_WEP     = 1U << 4,
    SCAN_SECURITY_UNKNOWN = 1U << 5,
};

typedef struct {
    scan_sort_key_t sort_key;
    bool reverse;
    uint32_t security_mask;
    scan_visibility_t visibility;
    char ssid_query[SCAN_FILTER_QUERY_MAX + 1];
} scan_filter_options_t;

typedef struct {
    const char *ssid;
    const char *security;
    int rssi;
    int channel;
    int source_index;
} scan_filter_record_t;

uint32_t scan_filter_security_mask(const char *security);

size_t scan_filter_build_order(const scan_filter_record_t *records,
                               size_t record_count,
                               const scan_filter_options_t *options,
                               int *out_source_indices,
                               size_t out_capacity);

size_t scan_filter_format_status(char *out,
                                 size_t out_size,
                                 int found,
                                 size_t shown,
                                 int selected,
                                 int hidden_selected,
                                 bool filters_active,
                                 bool timed_out);

size_t scan_filter_format_summary(char *out,
                                  size_t out_size,
                                  const scan_filter_options_t *options);
