#include "scan_filter.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int ascii_tolower(int c)
{
    return tolower((unsigned char)c);
}

static bool contains_ascii_case_insensitive(const char *text, const char *needle)
{
    if (!needle || needle[0] == '\0') return true;
    if (!text) return false;

    size_t needle_len = strlen(needle);
    for (const char *start = text; *start != '\0'; start++) {
        size_t i = 0;
        while (i < needle_len && start[i] != '\0' &&
               ascii_tolower(start[i]) == ascii_tolower(needle[i])) {
            i++;
        }
        if (i == needle_len) return true;
    }
    return false;
}

static bool is_ascii_token_char(int c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static bool contains_ascii_case_insensitive_token(const char *text, const char *token)
{
    if (!text || !token || token[0] == '\0') return false;

    size_t token_len = strlen(token);
    for (const char *start = text; *start != '\0'; start++) {
        size_t i = 0;
        while (i < token_len && start[i] != '\0' &&
               ascii_tolower(start[i]) == ascii_tolower(token[i])) {
            i++;
        }
        if (i == token_len &&
            (start == text || !is_ascii_token_char(start[-1])) &&
            !is_ascii_token_char(start[token_len])) {
            return true;
        }
    }
    return false;
}

static int compare_ascii_case_insensitive(const char *left, const char *right)
{
    if (!left) left = "";
    if (!right) right = "";
    while (*left != '\0' && *right != '\0') {
        int a = ascii_tolower(*left);
        int b = ascii_tolower(*right);
        if (a != b) return a < b ? -1 : 1;
        left++;
        right++;
    }
    if (*left == *right) return 0;
    return *left == '\0' ? -1 : 1;
}

uint32_t scan_filter_security_mask(const char *security)
{
    if (!security || security[0] == '\0') return SCAN_SECURITY_UNKNOWN;

    uint32_t mask = 0;
    if (contains_ascii_case_insensitive_token(security, "WPA3")) mask |= SCAN_SECURITY_WPA3;
    if (contains_ascii_case_insensitive_token(security, "WPA2")) mask |= SCAN_SECURITY_WPA2;
    if (contains_ascii_case_insensitive_token(security, "WEP")) mask |= SCAN_SECURITY_WEP;
    if (contains_ascii_case_insensitive_token(security, "OPEN") ||
        contains_ascii_case_insensitive_token(security, "NONE")) {
        mask |= SCAN_SECURITY_OPEN;
    }
    if (contains_ascii_case_insensitive_token(security, "WPA")) {
        mask |= SCAN_SECURITY_WPA;
    }
    return mask != 0 ? mask : SCAN_SECURITY_UNKNOWN;
}

static bool record_matches(const scan_filter_record_t *record,
                           const scan_filter_options_t *options)
{
    const char *ssid = record->ssid ? record->ssid : "";
    bool hidden = ssid[0] == '\0';
    if (options->visibility == SCAN_VISIBILITY_NAMED && hidden) return false;
    if (options->visibility == SCAN_VISIBILITY_HIDDEN && !hidden) return false;
    if (!contains_ascii_case_insensitive(ssid, options->ssid_query)) return false;

    if (options->security_mask != SCAN_SECURITY_ALL) {
        uint32_t record_mask = scan_filter_security_mask(record->security);
        if ((record_mask & options->security_mask) == 0) return false;
    }
    return true;
}

static int compare_records(const scan_filter_record_t *left,
                           const scan_filter_record_t *right,
                           scan_sort_key_t key)
{
    switch (key) {
        case SCAN_SORT_NAME:
            return compare_ascii_case_insensitive(left->ssid, right->ssid);
        case SCAN_SORT_SIGNAL:
            if (left->rssi == right->rssi) return 0;
            return left->rssi > right->rssi ? -1 : 1;
        case SCAN_SORT_CHANNEL:
            if (left->channel == right->channel) return 0;
            return left->channel < right->channel ? -1 : 1;
        case SCAN_SORT_DEFAULT:
        default:
            if (left->source_index == right->source_index) return 0;
            return left->source_index < right->source_index ? -1 : 1;
    }
}

size_t scan_filter_build_order(const scan_filter_record_t *records,
                               size_t record_count,
                               const scan_filter_options_t *options,
                               int *out_source_indices,
                               size_t out_capacity)
{
    if (!records || !options || !out_source_indices || out_capacity == 0) return 0;

    size_t count = 0;
    for (size_t i = 0; i < record_count; i++) {
        if (!record_matches(&records[i], options)) continue;

        size_t insert_at = 0;
        while (insert_at < count) {
            int existing_source = out_source_indices[insert_at];
            const scan_filter_record_t *existing = NULL;
            for (size_t j = 0; j < record_count; j++) {
                if (records[j].source_index == existing_source) {
                    existing = &records[j];
                    break;
                }
            }
            if (!existing) break;
            int comparison = compare_records(existing, &records[i], options->sort_key);
            if (options->reverse) comparison = -comparison;
            if (comparison > 0) break;
            insert_at++;
        }

        if (insert_at >= out_capacity) continue;
        size_t new_count = count < out_capacity ? count + 1 : count;
        for (size_t j = new_count - 1; j > insert_at; j--) {
            out_source_indices[j] = out_source_indices[j - 1];
        }
        out_source_indices[insert_at] = records[i].source_index;
        count = new_count;
    }
    return count;
}

size_t scan_filter_format_status(char *out,
                                 size_t out_size,
                                 int found,
                                 size_t shown,
                                 int selected,
                                 int hidden_selected,
                                 bool filters_active,
                                 bool timed_out)
{
    if (!out || out_size == 0) return 0;

    int written;
    if (timed_out && found <= 0) {
        written = snprintf(out, out_size, "Scan timed out");
    } else if (timed_out && selected > 0 && hidden_selected > 0) {
        written = snprintf(out, out_size,
                           "Scan timed out | Found %d partial results | Shown %u | Selected %d (%d hidden by filters)",
                           found, (unsigned)shown, selected, hidden_selected);
    } else if (timed_out && selected > 0) {
        written = snprintf(out, out_size,
                           "Scan timed out | Found %d partial results | Shown %u | Selected %d",
                           found, (unsigned)shown, selected);
    } else if (timed_out && shown == 0) {
        written = snprintf(out, out_size,
                           "Scan timed out | Found %d partial results | No matching networks",
                           found);
    } else if (timed_out && filters_active) {
        written = snprintf(out, out_size,
                           "Scan timed out | Found %d partial results | Shown %u",
                           found, (unsigned)shown);
    } else if (timed_out) {
        written = snprintf(out, out_size,
                           "Scan timed out | Found %d partial results", found);
    } else if (selected > 0 && hidden_selected > 0) {
        written = snprintf(out, out_size,
                           "Found %d | Shown %u | Selected %d (%d hidden by filters)",
                           found, (unsigned)shown, selected, hidden_selected);
    } else if (selected > 0) {
        written = snprintf(out, out_size,
                           "Found %d | Shown %u | Selected %d",
                           found, (unsigned)shown, selected);
    } else if (shown == 0 && found > 0) {
        written = snprintf(out, out_size,
                           "Found %d | No matching networks", found);
    } else if (filters_active) {
        written = snprintf(out, out_size, "Found %d | Shown %u",
                           found, (unsigned)shown);
    } else {
        written = snprintf(out, out_size, "Found %d networks", found);
    }

    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

static void summary_append(char *out, size_t out_size, size_t *used,
                           const char *format, ...)
{
    if (!out || !used || *used >= out_size) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out + *used, out_size - *used, format, args);
    va_end(args);
    if (written < 0) return;
    size_t remaining = out_size - *used;
    *used += (size_t)written < remaining ? (size_t)written : remaining - 1;
}

size_t scan_filter_format_summary(char *out,
                                  size_t out_size,
                                  const scan_filter_options_t *options)
{
    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    if (!options) return 0;

    bool filters_active = options->ssid_query[0] != '\0' ||
                          options->security_mask != SCAN_SECURITY_ALL ||
                          options->visibility != SCAN_VISIBILITY_ALL;
    size_t used = 0;
    if (!filters_active && options->sort_key == SCAN_SORT_DEFAULT && !options->reverse) {
        summary_append(out, out_size, &used, "FILTERS: None | SORT: Default");
        return used;
    }

    switch (options->sort_key) {
        case SCAN_SORT_NAME:
            summary_append(out, out_size, &used, "SORT: Name %s",
                           options->reverse ? "Z-A" : "A-Z");
            break;
        case SCAN_SORT_SIGNAL:
            summary_append(out, out_size, &used, "SORT: Signal %s",
                           options->reverse ? "weakest" : "strongest");
            break;
        case SCAN_SORT_CHANNEL:
            summary_append(out, out_size, &used, "SORT: Channel %s",
                           options->reverse ? "high-low" : "low-high");
            break;
        case SCAN_SORT_DEFAULT:
        default:
            summary_append(out, out_size, &used, "SORT: Default%s",
                           options->reverse ? " reverse" : "");
            break;
    }

    if (options->ssid_query[0] != '\0') {
        summary_append(out, out_size, &used, " | SSID: \"%s\"", options->ssid_query);
    }

    if (options->security_mask != SCAN_SECURITY_ALL) {
        static const uint32_t bits[] = {
            SCAN_SECURITY_OPEN, SCAN_SECURITY_WPA, SCAN_SECURITY_WPA2,
            SCAN_SECURITY_WPA3, SCAN_SECURITY_WEP, SCAN_SECURITY_UNKNOWN,
        };
        static const char *names[] = {
            "Open", "WPA", "WPA2", "WPA3", "WEP", "Unknown",
        };
        summary_append(out, out_size, &used, " | SECURITY: ");
        bool first = true;
        for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
            if ((options->security_mask & bits[i]) == 0) continue;
            summary_append(out, out_size, &used, "%s%s", first ? "" : ",", names[i]);
            first = false;
        }
    }

    if (options->visibility != SCAN_VISIBILITY_ALL) {
        summary_append(out, out_size, &used, " | VIEW: %s",
                       options->visibility == SCAN_VISIBILITY_HIDDEN ? "Hidden" : "Named");
    }
    return used;
}
