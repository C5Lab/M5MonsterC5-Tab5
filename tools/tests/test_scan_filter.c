#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "scan_filter.h"

static const scan_filter_record_t records[] = {
    {.ssid = "Lab-Guest", .security = "OPEN", .rssi = -72, .channel = 11, .source_index = 0},
    {.ssid = "alpha", .security = "WPA2", .rssi = -35, .channel = 6, .source_index = 1},
    {.ssid = "Beta", .security = "WPA3", .rssi = -61, .channel = 1, .source_index = 2},
    {.ssid = "", .security = "WPA2/WPA3", .rssi = -48, .channel = 36, .source_index = 3},
    {.ssid = "lab-iot", .security = "WEP", .rssi = -80, .channel = 3, .source_index = 4},
    {.ssid = "Mystery", .security = "UNKNOWN", .rssi = -55, .channel = 9, .source_index = 5},
};

static scan_filter_options_t defaults(void)
{
    scan_filter_options_t options = {
        .sort_key = SCAN_SORT_DEFAULT,
        .reverse = false,
        .security_mask = SCAN_SECURITY_ALL,
        .visibility = SCAN_VISIBILITY_ALL,
    };
    options.ssid_query[0] = '\0';
    return options;
}

static void expect_order(const scan_filter_options_t *options,
                         const int *expected, size_t expected_count)
{
    int order[16] = {0};
    size_t count = scan_filter_build_order(
        records, sizeof(records) / sizeof(records[0]), options,
        order, sizeof(order) / sizeof(order[0]));
    assert(count == expected_count);
    for (size_t i = 0; i < expected_count; i++) {
        assert(order[i] == expected[i]);
    }
}

static void test_default_keeps_source_order(void)
{
    scan_filter_options_t options = defaults();
    const int expected[] = {0, 1, 2, 3, 4, 5};
    expect_order(&options, expected, 6);
}

static void test_ssid_query_is_ascii_case_insensitive(void)
{
    scan_filter_options_t options = defaults();
    strcpy(options.ssid_query, "LAB");
    const int expected[] = {0, 4};
    expect_order(&options, expected, 2);
}

static void test_signal_defaults_to_strongest_first(void)
{
    scan_filter_options_t options = defaults();
    options.sort_key = SCAN_SORT_SIGNAL;
    const int expected[] = {1, 3, 5, 2, 0, 4};
    expect_order(&options, expected, 6);
}

static void test_name_and_channel_support_reverse_order(void)
{
    scan_filter_options_t options = defaults();
    options.sort_key = SCAN_SORT_NAME;
    const int name_ascending[] = {3, 1, 2, 0, 4, 5};
    expect_order(&options, name_ascending, 6);

    options.sort_key = SCAN_SORT_CHANNEL;
    options.reverse = true;
    const int channel_descending[] = {3, 0, 5, 1, 4, 2};
    expect_order(&options, channel_descending, 6);
}

static void test_security_visibility_and_text_combine(void)
{
    scan_filter_options_t options = defaults();
    options.security_mask = SCAN_SECURITY_WPA2 | SCAN_SECURITY_WPA3;
    options.visibility = SCAN_VISIBILITY_NAMED;
    options.sort_key = SCAN_SORT_NAME;
    const int named_secure[] = {1, 2};
    expect_order(&options, named_secure, 2);

    options.visibility = SCAN_VISIBILITY_HIDDEN;
    const int hidden_secure[] = {3};
    expect_order(&options, hidden_secure, 1);
}

static void test_security_classifier_handles_mixed_and_unknown_values(void)
{
    assert(scan_filter_security_mask("WPA2/WPA3") ==
           (SCAN_SECURITY_WPA2 | SCAN_SECURITY_WPA3));
    assert(scan_filter_security_mask("Open") == SCAN_SECURITY_OPEN);
    assert(scan_filter_security_mask("WEP") == SCAN_SECURITY_WEP);
    assert(scan_filter_security_mask("WPA/WPA2 Mixed") ==
           (SCAN_SECURITY_WPA | SCAN_SECURITY_WPA2));
    assert(scan_filter_security_mask("something-new") == SCAN_SECURITY_UNKNOWN);
}

static void test_limited_output_keeps_best_global_matches(void)
{
    scan_filter_options_t options = defaults();
    options.sort_key = SCAN_SORT_SIGNAL;
    int order[2] = {-1, -1};
    size_t count = scan_filter_build_order(
        records, sizeof(records) / sizeof(records[0]), &options, order, 2);
    assert(count == 2);
    assert(order[0] == 1);
    assert(order[1] == 3);
}

static void test_status_keeps_selection_and_timeout_context(void)
{
    char status[160];

    scan_filter_format_status(status, sizeof(status), 3, 0, 2, 2, true, false);
    assert(strcmp(status,
                  "Found 3 | Shown 0 | Selected 2 (2 hidden by filters)") == 0);

    scan_filter_format_status(status, sizeof(status), 0, 0, 0, 0, false, true);
    assert(strcmp(status, "Scan timed out") == 0);

    scan_filter_format_status(status, sizeof(status), 2, 1, 0, 0, true, true);
    assert(strcmp(status, "Scan timed out | Found 2 partial results | Shown 1") == 0);
}

static void test_filter_summary_exposes_active_criteria(void)
{
    char summary[256];
    scan_filter_options_t options = defaults();

    scan_filter_format_summary(summary, sizeof(summary), &options);
    assert(strcmp(summary, "FILTERS: None | SORT: Default") == 0);

    options.sort_key = SCAN_SORT_SIGNAL;
    options.reverse = true;
    options.security_mask = SCAN_SECURITY_OPEN | SCAN_SECURITY_WPA2 |
                            SCAN_SECURITY_WPA3 | SCAN_SECURITY_UNKNOWN;
    options.visibility = SCAN_VISIBILITY_HIDDEN;
    strcpy(options.ssid_query, "lab");
    scan_filter_format_summary(summary, sizeof(summary), &options);
    assert(strcmp(summary,
                  "SORT: Signal weakest | SSID: \"lab\" | SECURITY: Open,WPA2,WPA3,Unknown | VIEW: Hidden") == 0);
}

int main(void)
{
    test_default_keeps_source_order();
    test_ssid_query_is_ascii_case_insensitive();
    test_signal_defaults_to_strongest_first();
    test_name_and_channel_support_reverse_order();
    test_security_visibility_and_text_combine();
    test_security_classifier_handles_mixed_and_unknown_values();
    test_limited_output_keeps_best_global_matches();
    test_status_keeps_selection_and_timeout_context();
    test_filter_summary_exposes_active_criteria();
    puts("scan_filter tests passed");
    return 0;
}
