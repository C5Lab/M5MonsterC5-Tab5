// Host unit test for main/ota_rf.c - the pure Monster OTA / JanOS RF logic.
//
//   gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g
//       -I main -o /tmp/ota_rf_test tests/ota_rf_test.c main/ota_rf.c
//   /tmp/ota_rf_test
//
// It exercises the real parser and decision code the firmware links, not a copy.

#include "ota_rf.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                      \
    } while (0)

// --- helper: feed a whole multi-line ota_info blob into a report -----------
static void feed_info(ota_info_report_t *r, const char *const *lines, int n)
{
    ota_info_report_reset(r);
    for (int i = 0; i < n; i++) ota_info_report_feed_line(r, lines[i]);
}

// ===========================================================================
// version parsing / comparison
// ===========================================================================
static void test_versions(void)
{
    int a, b, c;
    CHECK(ota_rf_parse_version("1.7.5", &a, &b, &c) && a == 1 && b == 7 && c == 5);
    CHECK(ota_rf_parse_version("v1.7.5", &a, &b, &c) && a == 1 && b == 7 && c == 5);
    CHECK(ota_rf_parse_version("V2.0.0", &a, &b, &c) && a == 2 && b == 0 && c == 0);
    CHECK(ota_rf_parse_version("1.7.5-rc.1", &a, &b, &c));
    CHECK(!ota_rf_parse_version("1.7", &a, &b, &c));       // too few
    CHECK(!ota_rf_parse_version("1.07.5", &a, &b, &c));    // leading zero
    CHECK(!ota_rf_parse_version("1.7.5x", &a, &b, &c));    // trailing junk
    CHECK(!ota_rf_parse_version("dev", &a, &b, &c));       // not a version
    CHECK(!ota_rf_parse_version("", &a, &b, &c));
    CHECK(!ota_rf_parse_version(NULL, &a, &b, &c));

    // newer / same / older with and without the v prefix
    CHECK(ota_rf_compare_versions("1.7.5", "1.7.6") == OTA_CMP_NEWER);
    CHECK(ota_rf_compare_versions("1.7.5", "v1.7.6") == OTA_CMP_NEWER);
    CHECK(ota_rf_compare_versions("1.7.5", "1.7.5") == OTA_CMP_SAME);
    CHECK(ota_rf_compare_versions("v1.7.5", "1.7.5") == OTA_CMP_SAME);
    CHECK(ota_rf_compare_versions("1.7.5", "1.7.4") == OTA_CMP_OLDER);
    CHECK(ota_rf_compare_versions("1.7.5", "1.8.0") == OTA_CMP_NEWER);
    CHECK(ota_rf_compare_versions("2.0.0", "1.9.9") == OTA_CMP_OLDER);
    // a pre-release ranks below the release of the same triple
    CHECK(ota_rf_compare_versions("1.7.5", "1.7.5-rc.1") == OTA_CMP_OLDER);
    CHECK(ota_rf_compare_versions("1.7.5-rc.1", "1.7.5") == OTA_CMP_NEWER);
    CHECK(ota_rf_compare_versions("unknown", "1.7.5") == OTA_CMP_UNPARSEABLE);
    CHECK(ota_rf_compare_versions(NULL, "1.7.5") == OTA_CMP_UNPARSEABLE);
}

// ===========================================================================
// ota_info report parsing and capability detection
// ===========================================================================
static void test_info_classic_new_updater(void)
{
    // A CLASSIC build carrying the new updater: it prints the RF source line
    // (capability) but its layout is the classic 0x8000 table (incompatible).
    const char *lines[] = {
        "OTA default source: classic (C5Lab/projectZero), channel=main",
        "OTA RF source: elpadrino26/janosrf-web-flasher (one-shot: ota_check rf <tag>)",
        "OTA partition table offset: 0x8000",
        "OTA RF layout: incompatible",
        "OTA boot: ota_0",
        "OTA running: ota_0 state=1",
        "OTA next: ota_1",
        "APP[0]: ota_0 state=1 ver=1.7.5",
        "APP[1]: ota_1 state=0 ver=1.7.4",
    };
    ota_info_report_t r;
    feed_info(&r, lines, (int)(sizeof(lines) / sizeof(lines[0])));

    CHECK(r.has_default_source);
    CHECK(r.has_rf_source);
    CHECK(ota_info_supports_rf_commands(&r));        // capability YES
    CHECK(r.have_offset && r.table_offset == 0x8000);
    CHECK(r.layout == OTA_RF_LAYOUT_INCOMPATIBLE);   // but not RF-compatible
    CHECK(strcmp(r.boot, "ota_0") == 0);
    CHECK(strcmp(r.running, "ota_0") == 0 && r.running_state == 1);
    CHECK(strcmp(r.next, "ota_1") == 0);
    CHECK(r.slots[0].present && r.slots[0].have_ver && strcmp(r.slots[0].ver, "1.7.5") == 0);
    CHECK(r.slots[1].present && strcmp(r.slots[1].ver, "1.7.4") == 0);

    // Presence of the RF source line must NOT be read as RF hardware, and the
    // classic table must never be treated as RF-compatible.
    CHECK(!ota_rf_auto_should_use_rf(&r, /*subghz_seen=*/false));
}

static void test_info_rf_compatible(void)
{
    const char *lines[] = {
        "OTA default source: classic (C5Lab/projectZero), channel=main",
        "OTA RF source: elpadrino26/janosrf-web-flasher (one-shot: ota_check rf <tag>)",
        "OTA partition table offset: 0x10000",
        "OTA RF layout: compatible (release files still require verification)",
        "OTA running: ota_0 state=2",
    };
    ota_info_report_t r;
    feed_info(&r, lines, (int)(sizeof(lines) / sizeof(lines[0])));
    CHECK(ota_info_supports_rf_commands(&r));
    CHECK(r.have_offset && r.table_offset == 0x10000);
    CHECK(r.layout == OTA_RF_LAYOUT_COMPATIBLE);
    CHECK(ota_rf_auto_should_use_rf(&r, false));  // compatible + capable => RF path
}

static void test_info_old_updater_no_rf_lines(void)
{
    // An older updater emits no RF source / RF layout lines at all.
    const char *lines[] = {
        "OTA boot: ota_0",
        "OTA running: ota_0 state=1",
        "OTA next: ota_1",
        "APP[0]: ota_0 state=1 ver=1.6.8",
        "APP[1]: ota_1 missing",
    };
    ota_info_report_t r;
    feed_info(&r, lines, (int)(sizeof(lines) / sizeof(lines[0])));
    CHECK(!r.has_rf_source);
    CHECK(!ota_info_supports_rf_commands(&r));       // capability unproven
    CHECK(r.layout == OTA_RF_LAYOUT_UNKNOWN);         // never assume compatible
    CHECK(!r.slots[1].present);                       // "missing" is not present
    CHECK(!ota_rf_auto_should_use_rf(&r, false));
    // But a live SUB-GHz reply proves RF firmware, so it routes RF even when
    // ota_info omits the RF lines (the reported field case).
    CHECK(ota_rf_auto_should_use_rf(&r, true));
}

// ===========================================================================
// release list parsing
// ===========================================================================
static void test_release_parse(void)
{
    ota_release_t rel;
    CHECK(ota_release_parse_line("OTA[0]: v1.7.5 (main) 2026-01-02 Big release", &rel));
    CHECK(rel.index == 0 && strcmp(rel.tag, "v1.7.5") == 0 &&
          strcmp(rel.channel, "main") == 0 && strcmp(rel.date, "2026-01-02") == 0 &&
          strcmp(rel.title, "Big release") == 0);

    CHECK(ota_release_parse_line("OTA[3]: 1.7.4 (dev) 2025-12-01 ", &rel));
    CHECK(rel.index == 3 && strcmp(rel.tag, "1.7.4") == 0 &&
          strcmp(rel.channel, "dev") == 0);

    // A tag with no title still parses.
    CHECK(ota_release_parse_line("OTA[1]: 2.0.0 (main) 2026-02-02", &rel));
    CHECK(rel.index == 1 && strcmp(rel.tag, "2.0.0") == 0);

    // Non-release lines are rejected.
    CHECK(!ota_release_parse_line("OTA: not connected or no IP", &rel));
    CHECK(!ota_release_parse_line("random log line", &rel));
    CHECK(!ota_release_parse_line("", &rel));
}

// ===========================================================================
// command builders (RF routing, no classic fallback)
// ===========================================================================
static void test_command_builders(void)
{
    char cmd[64];
    CHECK(ota_rf_build_list_cmd(cmd, sizeof(cmd)) && strcmp(cmd, "ota_list rf") == 0);
    CHECK(ota_classic_build_list_cmd(cmd, sizeof(cmd)) && strcmp(cmd, "ota_list") == 0);

    CHECK(ota_rf_build_check_latest_cmd(cmd, sizeof(cmd), false) &&
          strcmp(cmd, "ota_check rf") == 0);
    CHECK(ota_rf_build_check_latest_cmd(cmd, sizeof(cmd), true) &&
          strcmp(cmd, "ota_check rf latest") == 0);

    // The tag is preserved verbatim, including a leading v.
    CHECK(ota_rf_build_check_tag_cmd(cmd, sizeof(cmd), "v1.7.5") &&
          strcmp(cmd, "ota_check rf v1.7.5") == 0);
    CHECK(ota_rf_build_check_tag_cmd(cmd, sizeof(cmd), "1.7.4") &&
          strcmp(cmd, "ota_check rf 1.7.4") == 0);
    CHECK(!ota_rf_build_check_tag_cmd(cmd, sizeof(cmd), ""));   // no empty tag

    CHECK(ota_classic_build_check_cmd(cmd, sizeof(cmd), NULL) &&
          strcmp(cmd, "ota_check") == 0);
    CHECK(ota_classic_build_check_cmd(cmd, sizeof(cmd), "latest") &&
          strcmp(cmd, "ota_check latest") == 0);

    // Overflow is refused, never truncated into a half command.
    char tiny[8];
    CHECK(!ota_rf_build_check_tag_cmd(tiny, sizeof(tiny), "1.2.3-verylong"));
}

// ===========================================================================
// install decision matrix
// ===========================================================================
static ota_info_report_t make_report(ota_rf_layout_t layout, bool has_rf_source)
{
    ota_info_report_t r;
    ota_info_report_reset(&r);
    r.has_rf_source = has_rf_source;
    r.layout = layout;
    return r;
}

static void test_decisions(void)
{
    // ---- automatic routing (manual_override = false) ----
    // No capability -> block, regardless of layout.
    ota_info_report_t r = make_report(OTA_RF_LAYOUT_COMPATIBLE, false);
    CHECK(ota_rf_decide_install(&r, false, "1.7.5", NULL, false, false) == OTA_DECIDE_BLOCK_NO_CAPABILITY);

    // Incompatible layout -> block (restore over USB), even with capability.
    r = make_report(OTA_RF_LAYOUT_INCOMPATIBLE, true);
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, false) == OTA_DECIDE_BLOCK_INCOMPATIBLE);

    // Unknown layout -> block, never assume compatible.
    r = make_report(OTA_RF_LAYOUT_UNKNOWN, true);
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, false) == OTA_DECIDE_BLOCK_UNKNOWN_LAYOUT);

    // Compatible + capability, latest -> allow (firmware skips if not newer).
    r = make_report(OTA_RF_LAYOUT_COMPATIBLE, true);
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, false) == OTA_DECIDE_ALLOW_LATEST);
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", NULL, true, false) == OTA_DECIDE_ALLOW_LATEST);

    // Explicit newer tag -> allow.
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", "1.7.6", false, false) == OTA_DECIDE_ALLOW_TAG);
    // Same tag -> reinstall confirm (never auto).
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", "1.7.5", false, false) == OTA_DECIDE_CONFIRM_REINSTALL);
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", "v1.7.5", false, false) == OTA_DECIDE_CONFIRM_REINSTALL);
    // Older tag -> downgrade confirm (never auto).
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", "1.7.4", false, false) == OTA_DECIDE_CONFIRM_DOWNGRADE);
    // Bad tag -> block.
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", "dev", false, false) == OTA_DECIDE_BLOCK_BAD_TAG);
    // Unknown current version, explicit valid tag -> confirm rather than silently install.
    CHECK(ota_rf_decide_install(&r, true, "unknown", "1.7.4", false, false) == OTA_DECIDE_CONFIRM_REINSTALL);

    // ---- manual override (user explicitly chose the RF variant) ----
    // Real RF firmware can omit the RF lines from ota_info, so capability is
    // unproven and the layout is UNKNOWN. A manual override proceeds anyway;
    // the C5 firmware still verifies. (This is the reported field case.)
    r = make_report(OTA_RF_LAYOUT_UNKNOWN, false);
    CHECK(ota_rf_decide_install(&r, false, "1.7.5", NULL, false, true) == OTA_DECIDE_ALLOW_LATEST);
    // Force reinstall of the same version via an explicit tag -> confirm.
    CHECK(ota_rf_decide_install(&r, false, "1.7.5", "1.7.5", false, true) == OTA_DECIDE_CONFIRM_REINSTALL);
    // A bad tag is still rejected under manual override.
    CHECK(ota_rf_decide_install(&r, false, "1.7.5", "dev", false, true) == OTA_DECIDE_BLOCK_BAD_TAG);
    // Incompatible layout stays a hard block even under manual override.
    r = make_report(OTA_RF_LAYOUT_INCOMPATIBLE, true);
    CHECK(ota_rf_decide_install(&r, true, "1.7.5", NULL, false, true) == OTA_DECIDE_BLOCK_INCOMPATIBLE);
}

// ===========================================================================
// async line reassembler
// ===========================================================================
typedef struct {
    char lines[32][512];
    int  count;
} sink_ctx_t;

static void collect(void *user, const char *line)
{
    sink_ctx_t *c = (sink_ctx_t *)user;
    if (c->count < 32) {
        snprintf(c->lines[c->count], sizeof(c->lines[c->count]), "%s", line);
        c->count++;
    }
}

static void feed_str(ota_line_asm_t *a, sink_ctx_t *c, const char *s)
{
    ota_line_asm_feed(a, s, (int)strlen(s), collect, c);
}

static void test_line_asm(void)
{
    ota_line_asm_t a;
    sink_ctx_t c = {0};
    ota_line_asm_reset(&a);

    // Fragmentation: a line split across three feeds emerges once, whole.
    feed_str(&a, &c, "OTA: prog");
    feed_str(&a, &c, "ress 42");
    feed_str(&a, &c, "%\r\n");
    CHECK(c.count == 1 && strcmp(c.lines[0], "OTA: progress 42%") == 0);

    // CRLF, bare CR and bare LF all terminate; empty lines are dropped.
    c.count = 0;
    ota_line_asm_reset(&a);
    feed_str(&a, &c, "a\r\nb\rc\n\r\n\nd\n");
    CHECK(c.count == 4);
    CHECK(strcmp(c.lines[0], "a") == 0 && strcmp(c.lines[1], "b") == 0 &&
          strcmp(c.lines[2], "c") == 0 && strcmp(c.lines[3], "d") == 0);

    // Interleaved foreign log lines pass through as their own lines; the caller
    // (parser) decides which to keep.
    c.count = 0;
    ota_line_asm_reset(&a);
    feed_str(&a, &c, "I (1234) wifi: some noise\r\nOTA[0]: 1.7.5 (main) 2026-01-01 x\r\n");
    CHECK(c.count == 2);
    CHECK(strcmp(c.lines[1], "OTA[0]: 1.7.5 (main) 2026-01-01 x") == 0);

    // Echo of the sent command comes back as an ordinary line.
    c.count = 0;
    ota_line_asm_reset(&a);
    feed_str(&a, &c, "ota_check rf\r\n");
    CHECK(c.count == 1 && strcmp(c.lines[0], "ota_check rf") == 0);

    // An over-long line is dropped, not truncated, and does not corrupt the
    // following good line.
    c.count = 0;
    ota_line_asm_reset(&a);
    char big[700];
    memset(big, 'X', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    feed_str(&a, &c, big);
    feed_str(&a, &c, "\r\nOTA: ok\r\n");
    CHECK(c.count == 1 && strcmp(c.lines[0], "OTA: ok") == 0);
}

// ===========================================================================
// per-device state: device change reset + concurrency guard
// ===========================================================================
static void test_state(void)
{
    ota_rf_state_t s;
    ota_rf_state_reset(&s);
    CHECK(s.bound_tab == -1 && !s.op_in_progress);

    // Bind device 0, cache detection.
    ota_rf_state_bind_device(&s, 0);
    s.updater_supports_rf = true;
    s.layout = OTA_RF_LAYOUT_COMPATIBLE;
    s.variant = OTA_VARIANT_RF;

    // Re-binding the same device keeps the cache.
    ota_rf_state_bind_device(&s, 0);
    CHECK(s.updater_supports_rf && s.layout == OTA_RF_LAYOUT_COMPATIBLE &&
          s.variant == OTA_VARIANT_RF);

    // Switching devices clears cached detection so it cannot leak.
    ota_rf_state_bind_device(&s, 1);
    CHECK(!s.updater_supports_rf && s.layout == OTA_RF_LAYOUT_UNKNOWN &&
          s.variant == OTA_VARIANT_UNKNOWN && s.bound_tab == 1);

    // Concurrency: only one operation at a time.
    CHECK(ota_rf_state_begin_op(&s));
    CHECK(!ota_rf_state_begin_op(&s));  // refused while in progress
    ota_rf_state_end_op(&s);
    CHECK(ota_rf_state_begin_op(&s));   // available again
    ota_rf_state_end_op(&s);
}

int main(void)
{
    test_versions();
    test_info_classic_new_updater();
    test_info_rf_compatible();
    test_info_old_updater_no_rf_lines();
    test_release_parse();
    test_command_builders();
    test_decisions();
    test_line_asm();
    test_state();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
