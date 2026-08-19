// Host test for cgw_parser: feeds the exact block shapes JanOS emits.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cgw_parser.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

static void feed(cgw_snapshot_t *s, const char **lines, int n, bool *done)
{
    *done = false;
    cgw_snapshot_reset(s);
    for (int i = 0; i < n; i++) {
        if (cgw_parse_line(lines[i], s)) *done = true;
    }
}

int main(void)
{
    cgw_snapshot_t s;
    bool done;

    // --- 1. Full active block from section 8.3, with interleaved ESP-IDF logs
    //        and a human status line that must all be ignored. ---
    const char *active[] = {
        "I (12345) JANOS: Capture Gateway: active SSID='JanOS Lab' security=open clients=2 channel=6 upstream=up NAPT=on",
        "I (12346) JANOS: Capture Gateway: AP=10.42.0.1 STA=192.168.1.27 DNS=10.42.0.1 (proxy=on upstream_dns=192.168.1.1)",
        "[CGW] status active=1 upstream=1 napt=1 clients=2 channel=6",
        "[CGW] ssid=JanOS Lab security=open upstream_ssid=Office WiFi",
        "[CGW] ap_ip=10.42.0.1 sta_ip=192.168.1.27 dns=10.42.0.1 dns_proxy=on upstream_dns=192.168.1.1",
        "[CGW] capture=active file=/sdcard/lab/pcaps/iot-lab_20260812_143000.pcap packets=18742 frames=18742 drops=0 file_bytes=9238164",
        "[CGW] recorder drop_alloc=0 drop_queue=0 drop_write=0 queue_depth=3 queue_capacity=1024 queue_high_water=41",
        "[CGW] rate_limit_kbps=4096 rate_effective_kbps=2048 adaptive=on throttle_events=3 pause_events=0 rate_queue_depth=27 rate_queue_capacity=1024 rate_queue_drops=0 rate_queue_high_water=545",
        "[CGW] pcap_scope=softap_10_42 filter_drops=11",
        "[CGW] client_isolation=off",
        "[CGW_CLIENT] mac=02:11:22:33:44:55 ip=10.42.0.2",
        "[CGW_CLIENT] mac=02:AA:BB:CC:DD:EE ip=10.42.0.3",
        "[CGW] END",
    };
    feed(&s, active, sizeof(active) / sizeof(active[0]), &done);

    CHECK(done && s.complete, "block did not complete");
    CHECK(s.active && s.upstream && s.napt, "active/upstream/napt wrong");
    CHECK(s.reported_clients == 2, "clients=%u", s.reported_clients);
    CHECK(s.channel == 6, "channel=%u", s.channel);
    // SSIDs with spaces must survive the anchored split.
    CHECK(strcmp(s.ssid, "JanOS Lab") == 0, "ssid='%s'", s.ssid);
    CHECK(strcmp(s.security, "open") == 0, "security='%s'", s.security);
    CHECK(strcmp(s.upstream_ssid, "Office WiFi") == 0, "upstream_ssid='%s'", s.upstream_ssid);
    CHECK(strcmp(s.ap_ip, "10.42.0.1") == 0, "ap_ip='%s'", s.ap_ip);
    CHECK(strcmp(s.sta_ip, "192.168.1.27") == 0, "sta_ip='%s'", s.sta_ip);
    CHECK(strcmp(s.upstream_dns, "192.168.1.1") == 0, "upstream_dns='%s'", s.upstream_dns);
    CHECK(s.dns_proxy, "dns_proxy not set");
    CHECK(s.capture_active, "capture_active not set");
    CHECK(s.packets == 18742, "packets=%u", (unsigned)s.packets);
    CHECK(s.file_bytes == 9238164ULL, "file_bytes=%llu", (unsigned long long)s.file_bytes);
    CHECK(strcmp(cgw_file_basename(&s), "iot-lab_20260812_143000.pcap") == 0,
          "basename='%s'", cgw_file_basename(&s));
    // "drops=" must not be captured from "rate_queue_drops=" on another line.
    CHECK(s.drops == 0, "drops=%u", (unsigned)s.drops);
    CHECK(s.queue_high_water == 41, "queue_high_water=%u", (unsigned)s.queue_high_water);
    CHECK(s.rate_limit_kbps == 4096, "rate_limit=%u", (unsigned)s.rate_limit_kbps);
    CHECK(s.rate_effective_kbps == 2048, "rate_eff=%u", (unsigned)s.rate_effective_kbps);
    CHECK(s.rate_queue_high_water == 545, "rate_qhw=%u", (unsigned)s.rate_queue_high_water);
    CHECK(s.rate_queue_drops == 0, "rate_queue_drops=%u", (unsigned)s.rate_queue_drops);
    CHECK(s.throttle_events == 3, "throttle=%u", (unsigned)s.throttle_events);
    CHECK(s.filter_drops == 11, "filter_drops=%u", (unsigned)s.filter_drops);
    CHECK(strcmp(s.client_isolation, "off") == 0, "isolation='%s'", s.client_isolation);
    CHECK(s.client_count == 2, "client_count=%d", s.client_count);
    CHECK(strcmp(s.clients[0].mac, "02:11:22:33:44:55") == 0, "c0 mac='%s'", s.clients[0].mac);
    CHECK(strcmp(s.clients[1].ip, "10.42.0.3") == 0, "c1 ip='%s'", s.clients[1].ip);
    // Throttling alone is not damage.
    CHECK(!cgw_recorder_degraded(&s), "false recorder degraded");
    CHECK(!cgw_shaper_degraded(&s), "false shaper degraded");

    // --- 2. Idle block ---
    const char *idle[] = { "[CGW] status active=0", "[CGW] END" };
    feed(&s, idle, 2, &done);
    CHECK(done && s.complete, "idle block did not complete");
    CHECK(!s.active, "idle reported active");
    CHECK(s.client_count == 0, "idle has clients");

    // --- 3. Degraded: recorder loss vs shaper loss are independent ---
    const char *deg[] = {
        "[CGW] status active=1 upstream=0 napt=1 clients=1 channel=11",
        "[CGW] capture=active file=/sdcard/lab/pcaps/sniff_7.pcap packets=100 frames=100 drops=5 file_bytes=24",
        "[CGW] recorder drop_alloc=2 drop_queue=3 drop_write=0 queue_depth=1024 queue_capacity=1024 queue_high_water=1024",
        "[CGW] rate_limit_kbps=4096 rate_effective_kbps=0 adaptive=on throttle_events=9 pause_events=2 rate_queue_depth=0 rate_queue_capacity=1024 rate_queue_drops=17 rate_queue_high_water=1024",
        "[CGW] END",
    };
    feed(&s, deg, 5, &done);
    CHECK(done, "degraded block did not complete");
    CHECK(!s.upstream, "upstream should be 0");
    CHECK(s.drops == 5 && s.drop_alloc == 2 && s.drop_queue == 3, "split drops wrong");
    CHECK(cgw_recorder_degraded(&s), "recorder degraded not detected");
    CHECK(cgw_shaper_degraded(&s), "shaper degraded not detected");
    CHECK(s.rate_effective_kbps == 0, "paused rate wrong");

    // --- 4. Fragment safety: a partial response must not look complete ---
    const char *partial[] = {
        "[CGW] status active=1 upstream=1 napt=1 clients=1 channel=6",
        "[CGW] capture=active file=/sdcard/lab/pcaps/x.pcap packets=5 frames=5 drops=0 file_bytes=24",
    };
    feed(&s, partial, 2, &done);
    CHECK(!done && !s.complete, "partial response reported complete");

    // --- 5. A log line that merely mentions the prefix must not be parsed ---
    cgw_snapshot_t s2;
    cgw_snapshot_reset(&s2);
    cgw_parse_line("I (999) JANOS: parser expects [CGW] status active=1 someday", &s2);
    CHECK(!s2.active, "matched [CGW] inside a log message");
    CHECK(!cgw_parse_line("I (999) X: [CGW] END of story", &s2), "matched bogus END");

    // --- 6. Duplicate client rows upsert instead of appending ---
    cgw_snapshot_reset(&s2);
    cgw_parse_line("[CGW_CLIENT] mac=02:11:22:33:44:55 ip=0.0.0.0", &s2);
    cgw_parse_line("[CGW_CLIENT] mac=02:11:22:33:44:55 ip=10.42.0.2", &s2);
    CHECK(s2.client_count == 1, "duplicate MAC appended (count=%d)", s2.client_count);
    CHECK(strcmp(s2.clients[0].ip, "10.42.0.2") == 0, "upsert kept stale ip '%s'", s2.clients[0].ip);

    // --- 7. [PCAP_FINAL] and the legacy marker ---
    cgw_final_t f;
    CHECK(cgw_parse_final_line(
        "[PCAP_FINAL] file=/sdcard/lab/pcaps/test.pcap frames=174340 drops=0 drop_alloc=0 drop_queue=0 drop_write=0 rate_queue_drops=0 throttle_events=7 pause_events=1", &f),
        "PCAP_FINAL not parsed");
    CHECK(f.valid && f.frames == 174340 && f.drops == 0, "final counters wrong");
    CHECK(f.throttle_events == 7 && f.pause_events == 1, "final events wrong");
    CHECK(strcmp(f.file, "/sdcard/lab/pcaps/test.pcap") == 0, "final file='%s'", f.file);

    CHECK(cgw_parse_final_line("I (5555) JANOS: PCAP saved: /sdcard/lab/pcaps/old.pcap", &f),
          "legacy marker not parsed");
    CHECK(strcmp(f.file, "/sdcard/lab/pcaps/old.pcap") == 0, "legacy file='%s'", f.file);
    CHECK(!cgw_parse_final_line("[CGW] status active=1", &f), "final matched a status line");

    // --- 8. CRLF terminators must be stripped ---
    cgw_snapshot_reset(&s2);
    CHECK(cgw_parse_line("[CGW] END\r", &s2), "CR-terminated END not matched");

    // --- 9. Truncation is flagged, not silently dropped ---
    cgw_snapshot_reset(&s2);
    for (int i = 0; i < CGW_MAX_CLIENTS + 3; i++) {
        char row[64];
        snprintf(row, sizeof(row), "[CGW_CLIENT] mac=02:00:00:00:00:%02X ip=10.42.0.%d", i, i + 2);
        cgw_parse_line(row, &s2);
    }
    CHECK(s2.client_count == CGW_MAX_CLIENTS, "count=%d", s2.client_count);
    CHECK(s2.clients_truncated, "truncation not flagged");

    if (failures == 0) {
        printf("all cgw_parser tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
