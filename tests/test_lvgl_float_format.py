"""Exercise production label formatting against LVGL's actual no-float formatter.

Run with Python; uses cc locally or through WSL on Windows. No firmware build.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def span(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


class LabelFormattingTests(unittest.TestCase):
    def test_decimal_labels_preserve_values_and_following_arguments(self):
        source = (ROOT / 'main/main.c').read_text(encoding='utf-8')
        wardrive = span(source, 'static void update_wardrive_count_label(tab_context_t *ctx)\n{',
                        'static void update_wardrive_trace_button')
        gps = span(source, '                lv_label_set_text_fmt(ctx->wardrive_gps_debug_coord_label,',
                   '                lv_obj_set_style_text_color(ctx->wardrive_gps_debug_coord_label,')
        # Include any C-library decimal conversion inserted before the GPS label.
        gps_begin = source.rfind('            if (have_coords) {', 0, source.index(gps))
        gps = source[gps_begin + len('            if (have_coords) {'):source.index(gps) + len(gps)]
        activity = span(source, '    double active_seconds = dossier.last_time_us',
                        '    lv_obj_set_width(traffic,')
        index_rate = span(source, '        if (bps >= 1.0 && file_size > offset) {',
                          '        } else {').split('{', 1)[1]
        summary_rate = span(source, '        if (pps >= 1.0 && total_packets > processed_packets) {',
                            '        } else {').split('{', 1)[1]
        capture = span(source, '    lv_obj_t *summary_label = lv_label_create(summary);',
                       '    lv_label_set_recolor(summary_label, true);')
        timeline = span(source, '            char relative_text[40];', '            lv_obj_set_width(label, lv_pct(100));')
        formatter = (ROOT / 'managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c').read_text()
        # Only dependency includes are substituted; the formatter implementation is unchanged.
        formatter = re.sub(r'^#include .*$', '', formatter, flags=re.MULTILINE)
        code = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#define LV_UNUSED(x) (void)(x)
#define LV_STDLIB_BUILTIN 0
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_USE_FLOAT 0
#define lv_strnlen strnlen
''' + formatter + r'''
static char output[1024];
typedef void lv_obj_t;
static lv_obj_t *lv_label_create(lv_obj_t *parent) { return parent; }
static void lv_label_set_text_fmt(void *label, const char *fmt, ...) {
    (void)label;
    va_list args; va_start(args, fmt);
    lv_vsnprintf(output, sizeof(output), fmt, args);
    va_end(args);
}
typedef struct {
    void *wardrive_net_count_label, *wardrive_gps_debug_coord_label;
    int wardrive_wifi_count, wardrive_bt_count, wardrive_relogs;
    int wardrive_best_ch, wardrive_sat_count;
    double wardrive_distance_m;
} tab_context_t;
// Deterministic boundaries for unrelated ETA, link-name and widget creation.
static void compromised_format_duration(uint64_t seconds, char *buffer, size_t length) {
    snprintf(buffer, length, "%llus", (unsigned long long)seconds);
}
static const char *pcap_reader_link_type_name(unsigned link_type) {
    (void)link_type; return "Ethernet";
}
#define PCAP_TIMESTAMP_NANOSECONDS 1
static void expect(const char *expected) {
    if (strcmp(output, expected)) {
        fprintf(stderr, "Expected: %s\nActual:   %s\n", expected, output);
        exit(1);
    }
}
''' + wardrive + r'''
int main(void) {
    tab_context_t context = {(void *)1, (void *)1, 12, 3, 0, 0, 8, 1234.0};
    tab_context_t *ctx = &context;
    update_wardrive_count_label(ctx);
    expect("WiFi: 12  BT: 3  SAT: 8  1.23 km");
    ctx->wardrive_relogs = 2; ctx->wardrive_best_ch = 6;
    update_wardrive_count_label(ctx);
    expect("WiFi: 12  BT: 3  relog: 2  ch: 6  SAT: 8  1.23 km");
    ctx->wardrive_distance_m = 0;
    update_wardrive_count_label(ctx);
    expect("WiFi: 12  BT: 3  relog: 2  ch: 6  SAT: 8  0.00 km");
    double latitude = -33.8688194, longitude = 151.2092954;
''' + gps + r'''
    expect("Coordinates: -33.868819, 151.209295");
    void *traffic = (void *)1;
    struct {
        uint64_t first_time_us, last_time_us, sent_bytes, received_bytes;
        unsigned flow_count, remote_peer_count, service_count;
    } dossier = {1000000, 2234567, 123456789012ULL, 987654321098ULL, 3, 4, 5};
''' + activity + r'''
    expect("Observed activity\n3 flow(s) | 4 peer(s) | 5 service(s) | 1.235 s span\nTX 123456789012 B | RX 987654321098 B");
    struct { unsigned flow_count; int flow_limited; } flows = {3, 1};
    struct {
        void *status_label;
        struct {
            unsigned link_type, version_major, version_minor;
            int big_endian, timestamp_resolution;
            unsigned snaplen;
        } capture_info;
        struct {
            uint64_t packet_count;
            unsigned indexed_packets;
            int index_limited, truncated_tail;
        } scan_summary;
        typeof(flows) *flow_analysis;
        int cache_hit, cache_available;
    } state_value = {(void *)1, {1, 2, 4, 0, 0, 65535},
                     {9, 7, 1, 1}, &flows, 1, 1};
    typeof(state_value) *state = &state_value;
    double bps = 8192.0, pps = 1234.4;
    size_t file_size = 16384, offset = 4096;
    size_t processed_packets = 5, total_packets = 2469;
    uint64_t packet_count = 3;
    int percent = 25;
    {
''' + index_rate + r'''
    }
    expect("Indexing packets... 25%\n3 packet(s)\n8 KB/s  -  1s left");
    {
''' + summary_rate + r'''
    }
    expect("Building Zeek-style summary... 25%\n5/2469 indexed packets\n1234 pkt/s  -  1s left");
    void *summary = (void *)1;
    double duration = 12.3456;
    char file_size_text[] = "4.5 KB";
''' + capture + r'''
    expect("#52B6FF Ethernet#  |  PCAP 2.4 little-endian us\n"
           "#5EDCA3 9 packets#  |  indexed 7  |  flows 3  |  12.346 s  |  4.5 KB  |  snaplen 65535"
           "  |  INDEX LIMITED  |  TRUNCATED TAIL  |  FLOWS LIMITED\n"
           "#5EDCA3 CACHE HIT - analysis loaded from SD#");
    {
        void *label = (void *)1;
        double relative = 12.3456;
        struct { int type, severity; char actor[24], detail[32]; } event_value = {0, 0, "192.0.2.1", "First observed flow"};
        __typeof__(event_value) *event = &event_value;
#define pcap_investigation_event_type_t int
#define pcap_health_level_t int
#define pcap_investigation_event_name(x) "FLOW"
#define pcap_flow_health_name(x) "WATCH"
''' + timeline + r'''
        expect("+12.346s | FLOW | WATCH | 192.0.2.1\nFirst observed flow");
    }
    puts("PASS: production decimal label call sites including PCAP timeline");
}
'''
        prefix = ['wsl', '--exec'] if os.name == 'nt' else []

        def host_path(path):
            if os.name == 'nt':
                return subprocess.check_output(prefix + ['wslpath', '-a', str(path)], text=True).strip()
            return str(path)

        with tempfile.TemporaryDirectory(prefix='tab5-label-format-') as folder:
            test = Path(folder) / 'labels.c'
            executable = Path(folder) / 'labels'
            test.write_text(code, encoding='utf-8')
            subprocess.run(prefix + ['cc', '-std=gnu11', '-Wall', '-Wextra',
                                    '-fsanitize=address,undefined', host_path(test),
                                    '-o', host_path(executable)], check=True)
            result = subprocess.run(prefix + [host_path(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
