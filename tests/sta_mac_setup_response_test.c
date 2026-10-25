#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "sta_mac_setup_response.h"

static int failures;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    failures++; \
} } while (0)

static const char *const factory_status =
    "{\"schema_version\":1,\"record_type\":\"sta_mac_status\","
    "\"configured_mode\":\"factory\",\"boot_mode\":\"factory\","
    "\"configured_mac\":null,\"effective_boot_mac\":\"24:6F:28:12:34:56\","
    "\"active_sta_mac\":null,\"record_health\":\"missing\","
    "\"identity_ready\":true,\"pending_reboot\":false}";

static void test_status(void)
{
    sta_mac_setup_status_t s = {0};
    CHECK(sta_mac_setup_parse_status_line(factory_status, &s));
    CHECK(strcmp(s.saved_policy, "factory") == 0);
    CHECK(strcmp(s.boot_policy, "factory") == 0);
    CHECK(strcmp(s.effective_mac, "24:6F:28:12:34:56") == 0);
    CHECK(s.current_mac_is_null);
    CHECK(strcmp(s.nvs_state, "missing") == 0);
    CHECK(s.identity_ready && !s.restart_required);

    const char *fixed_pending =
        " { \"pending_reboot\": true, \"active_sta_mac\":\"0A:AA:BB:CC:DD:EE\","
        "\"record_health\":\"valid\",\"configured_mac\":\"02:11:22:33:44:55\","
        "\"configured_mode\":\"fixed\",\"effective_boot_mac\":\"02:10:20:30:40:50\","
        "\"boot_mode\":\"random\",\"identity_ready\":true,"
        "\"record_type\":\"sta_mac_status\",\"schema_version\":1 } ";
    CHECK(sta_mac_setup_parse_status_line(fixed_pending, &s));
    CHECK(strcmp(s.saved_policy, "fixed 02:11:22:33:44:55") == 0);
    CHECK(strcmp(s.boot_policy, "random") == 0);
    CHECK(strcmp(s.current_mac, "0A:AA:BB:CC:DD:EE") == 0);
    CHECK(!s.current_mac_is_null && s.restart_required);

    const char *read_error =
        "{\"schema_version\":1,\"record_type\":\"sta_mac_status\","
        "\"configured_mode\":null,\"boot_mode\":\"factory\","
        "\"configured_mac\":null,\"effective_boot_mac\":\"24:6F:28:12:34:56\","
        "\"active_sta_mac\":null,\"record_health\":\"read_error\","
        "\"identity_ready\":false,\"pending_reboot\":false}";
    CHECK(sta_mac_setup_parse_status_line(read_error, &s));
    CHECK(strcmp(s.saved_policy, "unknown") == 0);
    CHECK(strcmp(s.nvs_state, "read_error") == 0);
    CHECK(!s.identity_ready);
}

static void test_invalid_status_keeps_previous_data(void)
{
    const char *bad[] = {
        "sta_mac status", "log: {\"record_type\":\"sta_mac_status\"}",
        "{\"schema_version\":2,\"record_type\":\"sta_mac_status\"}",
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\"}",
        "{\"schema_version\":1,\"record_type\":\"sta_mac_status\",\"configured_mode\":\"factory\"}",
        "{\"schema_version\":1,\"record_type\":\"sta_mac_status\",\"configured_mode\":\"fixed\",\"boot_mode\":\"factory\",\"configured_mac\":null,\"effective_boot_mac\":\"24:6F:28:12:34:56\",\"active_sta_mac\":null,\"record_health\":\"valid\",\"identity_ready\":true,\"pending_reboot\":false}",
        "{\"schema_version\":1,\"record_type\":\"sta_mac_status\",\"configured_mode\":\"factory\",\"boot_mode\":\"factory\",\"configured_mac\":null,\"effective_boot_mac\":\"24:6F:28:12:34:56\",\"active_sta_mac\":null,\"record_health\":\"missing\",\"identity_ready\":true,\"pending_reboot\":false,\"pending_reboot\":true}",
        "{\"schema_version\":1,\"record_type\":\"sta_mac_status\",\"configured_mode\":\"factory\",\"boot_mode\":\"factory\",\"configured_mac\":null,\"effective_boot_mac\":\"24:6F:28:12:34:56\",\"active_sta_mac\":null,\"record_health\":\"missing\",\"identity_ready\":true,\"pending_reboot\":false} trailing",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        sta_mac_setup_status_t s;
        memset(&s, 0x5a, sizeof(s));
        sta_mac_setup_status_t before = s;
        CHECK(!sta_mac_setup_parse_status_line(bad[i], &s));
        CHECK(memcmp(&s, &before, sizeof(s)) == 0);
    }
}

static void test_set_results(void)
{
    bool ok = false;
    char error[64] = {0};
    CHECK(sta_mac_setup_parse_set_line(
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\","
        "\"result\":\"set\",\"configured_mode\":\"fixed\","
        "\"configured_mac\":\"02:11:22:33:44:55\",\"applies_after_reboot\":true}",
        "sta_mac set fixed 02:11:22:33:44:55", &ok, error, sizeof(error)));
    CHECK(ok && error[0] == '\0');

    CHECK(sta_mac_setup_parse_set_line(
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\","
        "\"result\":\"store_error\",\"error\":\"commit_failed\"}",
        "sta_mac set fixed 02:11:22:33:44:55", &ok, error, sizeof(error)));
    CHECK(!ok && strcmp(error, "commit_failed") == 0);

    CHECK(sta_mac_setup_parse_set_line(
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\","
        "\"result\":\"invalid_mac\"}",
        "sta_mac set fixed 02:11:22:33:44:55", &ok, error, sizeof(error)));
    CHECK(!ok && strcmp(error, "invalid_mac") == 0);

    CHECK(sta_mac_setup_parse_set_line(
        "[WFACTL1] {\"v\":1,\"type\":\"error\",\"command\":\"console\","
        "\"code\":\"busy\",\"reason\":\"radio_lease\"}",
        "sta_mac set factory", &ok, error, sizeof(error)));
    CHECK(!ok && strcmp(error, "radio_lease") == 0);

    ok = true;
    strcpy(error, "unchanged");
    CHECK(!sta_mac_setup_parse_set_line("sta_mac set random", "sta_mac set random",
                                         &ok, error, sizeof(error)));
    CHECK(ok && strcmp(error, "unchanged") == 0);
    CHECK(!sta_mac_setup_parse_set_line(factory_status, "sta_mac set random",
                                         &ok, error, sizeof(error)));
    CHECK(ok && strcmp(error, "unchanged") == 0);
    CHECK(!sta_mac_setup_parse_set_line(
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\","
        "\"result\":\"set\",\"applies_after_reboot\":false}",
        "sta_mac set factory", &ok, error, sizeof(error)));
    CHECK(ok && strcmp(error, "unchanged") == 0);

    CHECK(!sta_mac_setup_parse_set_line(
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\","
        "\"result\":\"set\",\"configured_mode\":\"random\","
        "\"configured_mac\":null,\"applies_after_reboot\":true}",
        "sta_mac set factory", &ok, error, sizeof(error)));
    CHECK(ok && strcmp(error, "unchanged") == 0);

    CHECK(!sta_mac_setup_parse_set_line(
        "{\"schema_version\":1,\"record_type\":\"sta_mac_result\","
        "\"result\":\"set\",\"configured_mode\":\"fixed\","
        "\"configured_mac\":\"02:11:22:33:44:55\",\"applies_after_reboot\":true}",
        "sta_mac set fixed 02:11:22:33:44:56", &ok, error, sizeof(error)));
    CHECK(ok && strcmp(error, "unchanged") == 0);
}

int main(void)
{
    test_status();
    test_invalid_status_keeps_previous_data();
    test_set_results();
    if (failures) return 1;
    puts("STA MAC response tests passed");
    return 0;
}
