"""Contract: consuming a JanOS OTA release-list / fetch response over UART.

The Tab5 never does HTTP itself - the C5 fetches from GitHub and streams back
log lines. This pins how the Tab5 turns that asynchronous, fragmented byte
stream into release rows and how it handles the non-happy cases the firmware can
emit instead of a list:
  - a normal `ota_list rf` reply split across reads, with echo and foreign log
    lines interleaved, yields exactly the OTA[n] releases with tags preserved;
  - an empty list (only a notice, no OTA[n] lines) yields zero releases;
  - fetch/connectivity errors and "Unrecognized command" produce no releases and
    are never mistaken for entries;
  - a truncated final line that never terminates is not emitted as a row.

Exercises the real reassembler and parser in main/ota_rf.c.

    python3 tests/test_ota_http.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _ota_rf_harness import HARNESS_PRELUDE, assert_ok, run_harness  # noqa: E402


HARNESS = HARNESS_PRELUDE + r"""
// Consume a byte stream the way the monitor task does: reassemble lines, then
// classify each line as a release row or a notice.
typedef struct {
    ota_release_t rels[16];
    int rel_count;
    int notice_count;
} listing_t;

static void on_line(void *user, const char *line) {
    listing_t *L = (listing_t *)user;
    ota_release_t rel;
    if (ota_release_parse_line(line, &rel)) {
        if (L->rel_count < 16) L->rels[L->rel_count++] = rel;
    } else if (strncmp(line, "OTA", 3) == 0) {
        L->notice_count++;   // e.g. "OTA: not connected ..."
    }
}

// Feed a whole response as several arbitrary fragments to exercise framing.
static void feed_fragments(ota_line_asm_t *a, listing_t *L,
                           const char *const *frags, int n) {
    for (int i = 0; i < n; i++)
        ota_line_asm_feed(a, frags[i], (int)strlen(frags[i]), on_line, L);
}

int main(void) {
    // --- normal fragmented list, echo + foreign logs interleaved ----------
    {
        ota_line_asm_t a; ota_line_asm_reset(&a);
        listing_t L = {0};
        const char *frags[] = {
            "ota_list rf\r\n",                                   // command echo
            "OTA: list source=rf repo=elpadrino26/janosrf",       // notice, no newline yet
            "-web-flasher\r\n",                                    // ... completes across reads
            "I (900) wifi: rx\r\nOTA[0]: v1.7.5 (main) 2026-01-",  // foreign log + split row
            "02 Latest\r\nOTA[1]: 1.7.4 (main) 2025-12-01 Prev\r\n",
        };
        feed_fragments(&a, &L, frags, (int)(sizeof(frags)/sizeof(frags[0])));
        EXPECT(L.rel_count == 2);
        EXPECT(L.rels[0].index == 0 && strcmp(L.rels[0].tag, "v1.7.5") == 0);
        EXPECT(strcmp(L.rels[0].channel, "main") == 0);
        EXPECT(L.rels[1].index == 1 && strcmp(L.rels[1].tag, "1.7.4") == 0);
        EXPECT(L.notice_count >= 1);   // the "OTA: list source=..." notice
    }

    // --- empty list: a notice but no OTA[n] rows --------------------------
    {
        ota_line_asm_t a; ota_line_asm_reset(&a);
        listing_t L = {0};
        const char *frags[] = { "OTA: list source=rf repo=elpadrino26/janosrf-web-flasher\r\n" };
        feed_fragments(&a, &L, frags, 1);
        EXPECT(L.rel_count == 0);
        EXPECT(L.notice_count == 1);
    }

    // --- fetch error / not connected: no rows -----------------------------
    {
        ota_line_asm_t a; ota_line_asm_reset(&a);
        listing_t L = {0};
        const char *frags[] = {
            "OTA: not connected or no IP. Use 'wifi_connect' first.\r\n",
            "OTA: list failed: ESP_ERR_HTTP_CONNECT\r\n",
        };
        feed_fragments(&a, &L, frags, 2);
        EXPECT(L.rel_count == 0);
        EXPECT(L.notice_count == 2);
    }

    // --- old parser: "Unrecognized command" is not a release --------------
    {
        ota_line_asm_t a; ota_line_asm_reset(&a);
        listing_t L = {0};
        const char *frags[] = { "Unrecognized command: ota_list\r\n" };
        feed_fragments(&a, &L, frags, 1);
        EXPECT(L.rel_count == 0);
        EXPECT(L.notice_count == 0);
    }

    // --- truncated final line never terminates: not emitted ---------------
    {
        ota_line_asm_t a; ota_line_asm_reset(&a);
        listing_t L = {0};
        const char *frags[] = {
            "OTA[0]: 1.7.5 (main) 2026-01-02 Good\r\n",
            "OTA[1]: 1.7.4 (main) 2025-12-0",   // stream cut here, no newline
        };
        feed_fragments(&a, &L, frags, 2);
        EXPECT(L.rel_count == 1);               // only the completed row
        EXPECT(strcmp(L.rels[0].tag, "1.7.5") == 0);
    }

    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
"""


def test_ota_http():
    result = run_harness(HARNESS)
    assert_ok(result)
    print(result.stdout.strip())


if __name__ == "__main__":
    test_ota_http()
    print("test_ota_http: OK")
