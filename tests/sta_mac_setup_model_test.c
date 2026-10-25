/* Host contract for the Tab5 STA MAC command builder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sta_mac_setup_model.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_exact_commands(void)
{
    char output[36];
    CHECK(strcmp(sta_mac_setup_status_command, "sta_mac status") == 0);
    CHECK(sta_mac_setup_build_set_command(STA_MAC_SETUP_FACTORY, NULL,
                                          output, sizeof(output)));
    CHECK(strcmp(output, "sta_mac set factory") == 0);
    CHECK(sta_mac_setup_build_set_command(STA_MAC_SETUP_RANDOM, NULL,
                                          output, sizeof(output)));
    CHECK(strcmp(output, "sta_mac set random") == 0);
    CHECK(sta_mac_setup_build_set_command(STA_MAC_SETUP_FIXED,
                                          "02:11:22:33:44:55", output,
                                          sizeof(output)));
    CHECK(strcmp(output, "sta_mac set fixed 02:11:22:33:44:55") == 0);
}

static void test_fixed_mac_normalization(void)
{
    char output[18];
    char command[36];
    CHECK(sta_mac_setup_normalize_fixed_mac("02:ab:cd:ef:09:aA",
                                            output, sizeof(output)));
    CHECK(strcmp(output, "02:AB:CD:EF:09:AA") == 0);
    CHECK(sta_mac_setup_normalize_fixed_mac("06:aa:bb:cc:dd:ee",
                                            output, sizeof(output)));
    CHECK(strcmp(output, "06:AA:BB:CC:DD:EE") == 0);
    CHECK(sta_mac_setup_build_set_command(STA_MAC_SETUP_FIXED,
                                          "0a:ab:cd:ef:09:aa", command,
                                          sizeof(command)));
    CHECK(strcmp(command, "sta_mac set fixed 0A:AB:CD:EF:09:AA") == 0);
}

static void test_invalid_fixed_mac_is_rejected(void)
{
    static const char *invalid[] = {
        "", "02:11:22:33:44", "02:11:22:33:44:555",
        "02:11:22:33:44:5", "02-11-22-33-44-55",
        "02:11:22:33:44-55",
        "02:11:22:33:44:GG", " 02:11:22:33:44:55",
        "02:11:22:33:44:55 ", "00:11:22:33:44:55",
        "03:11:22:33:44:55", "ff:11:22:33:44:55",
    };
    char output[36];
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        strcpy(output, "stale command");
        CHECK(!sta_mac_setup_normalize_fixed_mac(invalid[i], output,
                                                  sizeof(output)));
        CHECK(output[0] == '\0');
        strcpy(output, "stale command");
        CHECK(!sta_mac_setup_build_set_command(STA_MAC_SETUP_FIXED,
                                                invalid[i], output,
                                                sizeof(output)));
        CHECK(output[0] == '\0');
    }
    strcpy(output, "stale command");
    CHECK(!sta_mac_setup_build_set_command(STA_MAC_SETUP_FIXED, NULL,
                                            output, sizeof(output)));
    CHECK(output[0] == '\0');
}

static void test_bounded_output_and_invalid_mode(void)
{
    char exact[36];
    char short_output[35];
    char mac[18];
    strcpy(short_output, "stale command");
    CHECK(!sta_mac_setup_build_set_command(STA_MAC_SETUP_FIXED,
                                            "02:11:22:33:44:55",
                                            short_output,
                                            sizeof(short_output)));
    CHECK(short_output[0] == '\0');
    CHECK(sta_mac_setup_build_set_command(STA_MAC_SETUP_FIXED,
                                          "02:11:22:33:44:55", exact,
                                          sizeof(exact)));
    CHECK(strcmp(exact, "sta_mac set fixed 02:11:22:33:44:55") == 0);
    strcpy(mac, "stale");
    CHECK(!sta_mac_setup_normalize_fixed_mac("02:11:22:33:44:55",
                                             mac, 17));
    CHECK(mac[0] == '\0');
    strcpy(exact, "stale command");
    CHECK(!sta_mac_setup_build_set_command((sta_mac_setup_mode_t)99, NULL,
                                            exact, sizeof(exact)));
    CHECK(exact[0] == '\0');
    CHECK(!sta_mac_setup_build_set_command(STA_MAC_SETUP_FACTORY, NULL,
                                            NULL, 0));
}

static void test_automatic_port_selection(void)
{
    CHECK(sta_mac_setup_single_detected_port(false, false, false) == 0);
    CHECK(sta_mac_setup_single_detected_port(true, false, false) == 1);
    CHECK(sta_mac_setup_single_detected_port(false, true, false) == 2);
    CHECK(sta_mac_setup_single_detected_port(false, false, true) == 3);
    CHECK(sta_mac_setup_single_detected_port(true, true, false) == 0);
    CHECK(sta_mac_setup_single_detected_port(true, false, true) == 0);
    CHECK(sta_mac_setup_single_detected_port(false, true, true) == 0);
    CHECK(sta_mac_setup_single_detected_port(true, true, true) == 0);
}

static void test_short_heap_strings_do_not_overread(void)
{
    static const char *short_values[] = { "", "0", "02", "02:", "02:11" };
    char output[STA_MAC_SETUP_MAC_BUFFER_SIZE];
    for (size_t i = 0; i < sizeof(short_values) / sizeof(short_values[0]); i++) {
        size_t bytes = strlen(short_values[i]) + 1;
        char *input = malloc(bytes);
        CHECK(input != NULL);
        if (!input) continue;
        memcpy(input, short_values[i], bytes);
        strcpy(output, "stale");
        CHECK(!sta_mac_setup_normalize_fixed_mac(input, output,
                                                  sizeof(output)));
        CHECK(output[0] == '\0');
        free(input);
    }
}

int main(void)
{
    test_exact_commands();
    test_fixed_mac_normalization();
    test_invalid_fixed_mac_is_rejected();
    test_bounded_output_and_invalid_mode();
    test_automatic_port_selection();
    test_short_heap_strings_do_not_overread();
    if (failures) {
        fprintf(stderr, "%d STA MAC setup test failures\n", failures);
        return 1;
    }
    puts("STA MAC setup model tests passed");
    return 0;
}
