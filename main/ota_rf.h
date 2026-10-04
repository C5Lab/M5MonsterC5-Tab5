#pragma once

// Pure, UI-free Monster OTA / JanOS RF logic for the Tab5 (ESP32-P4) remote.
//
// The Tab5 never flashes the C5 itself: it sends text commands over UART and
// parses the C5's log lines (see docs/Monster_OTA.md and
// docs/ota-rf-implementation-plan.md). Everything in this module is host
// testable with no LVGL or ESP-IDF dependency, so tests/ota_rf_test.c and the
// Python contract tests exercise the real parser and decision logic instead of
// a copy.
//
// Three facts are kept strictly independent, because none of them proves the
// others (see the implementation plan, requirement 1):
//   1. firmware variant   - RF vs classic firmware currently running;
//   2. command support     - does the running updater understand the `rf`
//                            argument on ota_list / ota_check;
//   3. partition layout     - is the flash layout the RF layout (table @0x10000)
//                            that RF images require.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The RF release repository (elpadrino26/janosrf-web-flasher) and the classic
// partition-table offsets, matching projectZero/ESP32C5/main/main.c.
#define OTA_RF_TABLE_OFFSET_RF       0x10000u
#define OTA_RF_TABLE_OFFSET_CLASSIC  0x8000u

// ---------------------------------------------------------------------------
// ota_info report
// ---------------------------------------------------------------------------

// RF partition-layout compatibility, as reported by JanOS `ota_info`.
// UNKNOWN means no "OTA RF layout:" line was seen - either an older updater
// that predates RF support, or ota_info has not been read yet. It must never
// be treated as compatible (requirement 5).
typedef enum {
    OTA_RF_LAYOUT_UNKNOWN = 0,
    OTA_RF_LAYOUT_COMPATIBLE,
    OTA_RF_LAYOUT_INCOMPATIBLE,
} ota_rf_layout_t;

typedef struct {
    bool present;
    char label[16];   // "ota_0" / "ota_1"
    int  state;        // esp_ota_img_states_t numeric value, or -1 if absent
    bool have_ver;
    char ver[64];
} ota_info_slot_t;

// Parsed view of the multi-line `ota_info` output.
typedef struct {
    bool has_default_source;   // "OTA default source:" seen
    bool has_rf_source;        // "OTA RF source:" seen (new-updater capability)
    bool have_offset;          // "OTA partition table offset:" parsed
    uint32_t table_offset;
    ota_rf_layout_t layout;    // from "OTA RF layout:" (UNKNOWN if line absent)

    char boot[16];
    char running[16];
    char next[16];
    int  running_state;        // numeric state of the running slot, or -1

    ota_info_slot_t slots[2];  // index 0 = ota_0, 1 = ota_1
} ota_info_report_t;

void ota_info_report_reset(ota_info_report_t *r);

// Feed one already-framed line (no trailing CR/LF). Returns true if the line
// was recognised as part of the ota_info output and consumed.
bool ota_info_report_feed_line(ota_info_report_t *r, const char *line);

// New-updater capability: does the running firmware's updater understand the
// `rf` argument? Proven only by ota_info emitting the RF source line - never by
// sending `ota_list rf` (an older parser may silently ignore the extra
// argument, requirement 4).
bool ota_info_supports_rf_commands(const ota_info_report_t *r);

// ---------------------------------------------------------------------------
// version / tag handling (mirrors JanOS ota_parse_version / ota_is_newer_version)
// ---------------------------------------------------------------------------

// Accepts an optional leading 'v'/'V'. Rejects leading zeros and trailing junk,
// like the firmware. Pre-release (-) / build (+) suffixes are permitted and
// ignored for the numeric triple. Returns false if not a valid version.
bool ota_rf_parse_version(const char *tag, int *major, int *minor, int *patch);

typedef enum {
    OTA_CMP_UNPARSEABLE = -2,
    OTA_CMP_OLDER       = -1,   // candidate is older than current
    OTA_CMP_SAME        =  0,
    OTA_CMP_NEWER       =  1,   // candidate is newer than current
} ota_ver_cmp_t;

// Compares `candidate` against `current` on the numeric triple only. If either
// side is null/unparseable, returns OTA_CMP_UNPARSEABLE.
ota_ver_cmp_t ota_rf_compare_versions(const char *current, const char *candidate);

// ---------------------------------------------------------------------------
// release list line ("OTA[n]: <tag> (main|dev) <date> <title>")
// ---------------------------------------------------------------------------

typedef struct {
    int  index;
    char tag[48];      // original tag string, preserved verbatim for install
    char channel[16];
    char date[16];
    char title[128];
    bool ok;
} ota_release_t;

bool ota_release_parse_line(const char *line, ota_release_t *out);

// ---------------------------------------------------------------------------
// command builders (write the command WITHOUT a trailing CRLF; the caller adds
// \r\n). RF listing and install must always carry the `rf` argument and never
// fall back to the classic repo (requirement 3).
// ---------------------------------------------------------------------------

bool ota_rf_build_list_cmd(char *out, size_t out_sz);                 // "ota_list rf"
bool ota_classic_build_list_cmd(char *out, size_t out_sz);            // "ota_list"

// "ota_check rf" (explicit_latest=false) or "ota_check rf latest" (true).
// Both install only when the release is newer; the firmware enforces that.
bool ota_rf_build_check_latest_cmd(char *out, size_t out_sz, bool explicit_latest);

// "ota_check rf <tag>" with the tag copied verbatim (preserving a leading 'v').
// An explicit tag permits reinstall/downgrade on the firmware side.
bool ota_rf_build_check_tag_cmd(char *out, size_t out_sz, const char *tag);

// Classic: "ota_check" or "ota_check <tag>".
bool ota_classic_build_check_cmd(char *out, size_t out_sz, const char *tag_or_null);

/* Literal release identifiers only; never interpret a tag as CLI arguments. */
bool ota_release_tag_valid(const char *tag);
/* Shared gate for every installation, including single-source RF commands.
 * Manual variant selection and Force never override contradictory layout data.
 * An empty precheck is blocked. Legacy single-source RF requires a recognized
 * ota_info response and positive Sub-GHz identification. */
bool ota_install_route_allowed(const ota_info_report_t *report, bool use_rf, bool subghz_seen);
/* JanOS version from device detection is separate from ESP-IDF APP metadata. */
const char *ota_effective_running_version(const char *detected, const ota_info_report_t *report);

// ---------------------------------------------------------------------------
// install decision
// ---------------------------------------------------------------------------

typedef enum {
    OTA_DECIDE_ALLOW_LATEST,          // proceed with latest (firmware skips if not newer)
    OTA_DECIDE_ALLOW_TAG,             // proceed with an explicit newer tag
    OTA_DECIDE_CONFIRM_REINSTALL,     // explicit tag equals the running version
    OTA_DECIDE_CONFIRM_DOWNGRADE,     // explicit tag is older than the running version
    OTA_DECIDE_BLOCK_INCOMPATIBLE,    // RF layout incompatible - restore RF layout over USB
    OTA_DECIDE_BLOCK_UNKNOWN_LAYOUT,  // RF layout not reported - do not assume compatibility
    OTA_DECIDE_BLOCK_NO_CAPABILITY,   // updater does not understand the rf commands
    OTA_DECIDE_BLOCK_BAD_TAG,         // explicit tag is not a valid version
} ota_decision_t;

// Decide whether an RF install may proceed.
//   r                   - the latest parsed ota_info (may be freshly reset)
//   updater_supports_rf - result of ota_info_supports_rf_commands(r)
//   current             - running JanOS version (may be null/unknown)
//   tag                 - explicit tag, or null/empty for "latest"
//   explicit_latest     - the user asked for "latest" explicitly (informational)
//   manual_override     - the user explicitly chose the RF variant. Some RF
//                         firmware does not advertise RF support in ota_info
//                         (no RF source/layout line), so automatic detection
//                         cannot see it. A manual override tolerates an
//                         unproven capability and an UNKNOWN layout (the C5
//                         firmware still performs its own verification), but a
//                         positively INCOMPATIBLE layout stays a hard block -
//                         a classic 0x8000 table physically cannot take an RF
//                         image.
// Reinstall/downgrade are only ever signalled as CONFIRM_*, never started
// automatically (requirement 6).
ota_decision_t ota_rf_decide_install(const ota_info_report_t *r,
                                     bool updater_supports_rf,
                                     const char *current,
                                     const char *tag,
                                     bool explicit_latest,
                                     bool manual_override);

// Automatic RF routing. `subghz_seen` - RF firmware positively identified via a
// live [SUBGHZ_STATUS] reply - is sufficient on its own to route RF: the subghz
// command exists only in RF firmware, which can only have booted from the RF
// layout, so ota_info omitting the RF lines does not matter. Absent that, the
// app routes RF only when the updater proves rf-command support AND the layout
// is compatible. A missing subghz reply never forces the classic path
// (requirement 1/2).
bool ota_rf_auto_should_use_rf(const ota_info_report_t *r, bool subghz_seen);

// ---------------------------------------------------------------------------
// async line reassembler (fragmentation, echo, prompt chars, foreign logs)
// ---------------------------------------------------------------------------

typedef struct {
    char buf[512];
    int  len;
    bool overflow;   // set if a single line exceeded the buffer; the line is dropped
} ota_line_asm_t;

void ota_line_asm_reset(ota_line_asm_t *a);

// Feed raw bytes. For every completed line (terminated by CR and/or LF) the
// sink is called once with a NUL-terminated line (CR/LF stripped). Empty lines
// are dropped. A line longer than the buffer is discarded up to the next
// terminator rather than truncated into a false record.
void ota_line_asm_feed(ota_line_asm_t *a, const char *data, int n,
                       void (*sink)(void *user, const char *line), void *user);

// ---------------------------------------------------------------------------
// per-device capability cache + single-operation guard
// ---------------------------------------------------------------------------

typedef enum {
    OTA_VARIANT_UNKNOWN = 0,   // not positively identified
    OTA_VARIANT_RF,            // [SUBGHZ_STATUS] observed on this device
} ota_fw_variant_t;

typedef struct {
    int  bound_tab;             // tab this state describes, or -1
    bool op_in_progress;        // blocks a second concurrent OTA operation
    bool updater_supports_rf;   // cached from the last ota_info
    ota_rf_layout_t layout;     // cached from the last ota_info
    ota_fw_variant_t variant;   // cached (RF once subghz seen)
} ota_rf_state_t;

// Clear everything, including the device binding.
void ota_rf_state_reset(ota_rf_state_t *s);

// Bind to a device tab. If the tab differs from the currently bound one, all
// cached capability/layout/variant data is cleared so stale detection from a
// previous module can never leak across a device swap (requirement 7). Does not
// change an in-progress flag.
void ota_rf_state_bind_device(ota_rf_state_t *s, int tab);

// Try to claim the single-operation slot. Returns false if one is already in
// progress (parallel updates are refused).
bool ota_rf_state_begin_op(ota_rf_state_t *s);
void ota_rf_state_end_op(ota_rf_state_t *s);

#ifdef __cplusplus
}
#endif
