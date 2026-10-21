#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WA_MAX_APS 128
#define WA_MAX_CHANNELS 42
#define WA_MAX_LINE_BYTES 1024
#define WA_JSON_TOKENS 512

typedef enum { WA_BAND_ANY = 0, WA_BAND_24 = 1, WA_BAND_5 = 2 } wa_band_t;
typedef struct {
    char bssid[18];
    uint8_t ssid[32], ssid_len;
    char ssid_display[97], auth[33];
    int primary, rssi;
    wa_band_t band;
    int width; /* 0 unknown; 20, 40, 80, 160, 8080 */
    int secondary; /* -1 below, 0 none, 1 above, 2 unknown */
    int center1_mhz, center2_mhz; /* 0 unknown */
    uint8_t phy_bits; /* 11a/b/g/n/ac/ax/lr, in that order */
} wa_ap_t;

typedef struct {
    bool valid, truncated;
    char boot[17];
    uint32_t scan;
    uint16_t limit;
    wa_band_t band;
    uint8_t channels[WA_MAX_CHANNELS], channel_count;
    char profile[10];
    uint64_t started_ms, duration_ms;
    uint16_t count, found;
    wa_ap_t aps[WA_MAX_APS];
} wa_snapshot_t;

typedef struct {
    wa_band_t band;
    int min_rssi, primary, width; /* primary 0 all; width -1 all */
    char auth[33], search[97]; /* empty means all; ASCII case insensitive search */
} wa_filter_t;
typedef struct { int low_mhz, high_mhz; } wa_segment_t;

/* Private parser storage is public only to permit caller-owned allocation. */
typedef struct { uint16_t start, end, next; uint8_t type; } wa_json_token_t;
typedef struct {
    wa_snapshot_t *working, *committed;
    bool pending, caps_ready, stale;
    uint16_t max_records, default_records;
    char control_state[24], error[96], last_terminal_status[12];
    char control_type[16], control_code[40], control_reason[96];
    uint32_t commit_count, terminal_count, error_count, begin_count, control_count;
    char line[WA_MAX_LINE_BYTES + 1], boot[17];
    size_t line_len;
    bool dropping;
    uint32_t last_scan;
    wa_json_token_t tokens[WA_JSON_TOKENS];
    uint16_t token_count, parse_pos;
} wa_reader_t;

/* Caller owns three distinct buffers; no allocation and no internal locking.
 * Feed/inspect on one owner task, or protect the entire reader and snapshots.
 * Only a valid successful end copies working into committed. */
void wa_reader_init(wa_reader_t *reader, wa_snapshot_t *working, wa_snapshot_t *committed);
void wa_reader_feed(wa_reader_t *reader, const void *bytes, size_t len);
void wa_reader_abort(wa_reader_t *reader, const char *reason);
/* Retains the last good snapshot as stale; permits a new boot identity. */
void wa_reader_reset_connection(wa_reader_t *reader);
int wa_frequency(int channel);
bool wa_ap_matches(const wa_ap_t *ap, const wa_filter_t *filter);
uint32_t wa_ap_color_rgb(const wa_ap_t *ap);
/* Validated RF extent; 0 for unknown width, 2 for disjoint 80+80 segments. */
size_t wa_ap_segments(const wa_ap_t *ap, wa_segment_t segments[2]);
/* 2.4 GHz stays fixed; 5 GHz Auto fits filtered APs, including both 80+80
 * segments and unknown-width primary markers. Empty views use the full band. */
wa_segment_t wa_plot_range(const wa_snapshot_t *snapshot, const wa_filter_t *filter,
                           wa_band_t band, bool full_band);
/* Zoom 1/2/4 within the base range; center 0 means the base midpoint. */
wa_segment_t wa_plot_zoom(wa_segment_t base, unsigned factor, int center_mhz);

#define WA_ADVICE_MAX_CHOICES 25
typedef enum {
    WA_ADVICE_OK, WA_ADVICE_NO_SNAPSHOT, WA_ADVICE_INVALID_BAND,
    WA_ADVICE_TRUNCATED, WA_ADVICE_INCOMPLETE_SCOPE
} wa_advice_state_t;
typedef struct {
    int channel, strongest_rssi; /* -128 means no known overlapping AP. */
    uint32_t score; /* Relative heuristic units, NOT airtime or RF power. */
    uint16_t overlapping;
    bool dfs;
} wa_channel_choice_t;
typedef struct {
    wa_advice_state_t state;
    unsigned count, observed, unknown;
    wa_channel_choice_t choices[WA_ADVICE_MAX_CHOICES];
} wa_channel_advice_t;
/* Rank 20 MHz candidates from the entire committed snapshot, ignoring display
 * filters. Default 5 GHz pool: 36-48; extended: 36-165, including DFS. The
 * caller must explain router/country availability is not known by this model.
 * Unknown geometry adds the same conservative penalty to every candidate.
 * No allocation, scan command or change to measurements/router configuration. */
void wa_propose_channels(const wa_snapshot_t *snapshot, wa_band_t band,
                         bool extended_5ghz, const char *exclude_bssid,
                         wa_channel_advice_t *out);
