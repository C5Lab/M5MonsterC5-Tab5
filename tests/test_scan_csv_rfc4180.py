"""Host execution of the production scan CSV parser.

JanOS `scan_networks` / `show_scan_results` lines are RFC 4180:
`"index","SSID","vendor","BSSID","channel","auth","RSSI","band"`,
with a quote inside a field written as `""`. SSID and vendor may also
contain commas. Run with host Python and gcc.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_observer_incremental_ui_contract import function_body

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/main.c").read_text(encoding="utf-8")

EXTRACT = (
    ("void", "trim_ascii_whitespace"),
    ("int", "parse_csv_mixed_fields"),
    ("bool", "observer_scan_field_is_mac"),
    ("bool", "parse_network_line"),
    ("bool", "observer_parse_scan_identity"),
    ("void", "format_network_info"),
    ("void", "beacon_spam_escape_quoted_arg"),
    ("bool", "build_wifi_connect_command"),
)


def production_functions():
    chunks = []
    for result_type, name in EXTRACT:
        prefix = "static " + result_type + " " + name
        body = function_body(SOURCE, prefix)
        start = SOURCE.index(body, SOURCE.index(prefix))
        signature = re.search(prefix + r"\([^;{}]*\)\s*$", SOURCE[:start], re.S)
        if signature is None:
            raise AssertionError("missing signature: " + name)
        chunks.append(signature.group() + body)
    return "\n".join(chunks)


HARNESS = r'''
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef enum {
    WIFI_CONNECT_AUTH_OPEN = 0,
    WIFI_CONNECT_AUTH_PASSWORD = 1,
    WIFI_CONNECT_AUTH_SAVED = 2,
} wifi_connect_auth_mode_t;
typedef struct {
    int index;
    char ssid[33];
    char bssid[18];
    int rssi;
    int channel;
    char band[8];
    char security[24];
    char vendor[48];
} wifi_network_t;
static bool ui_wide_layout(void) { return false; }
''' + production_functions() + r'''

static void expect_line(const char *line, int index, const char *ssid, const char *vendor,
                        const char *bssid, int channel, const char *security, int rssi,
                        const char *band) {
    wifi_network_t net;
    memset(&net, 0x5A, sizeof(net));
    if (!parse_network_line(line, &net) || net.index != index || strcmp(net.ssid, ssid) ||
        strcmp(net.vendor, vendor) || strcmp(net.bssid, bssid) || net.channel != channel ||
        strcmp(net.security, security) || net.rssi != rssi || strcmp(net.band, band)) {
        fprintf(stderr,
                "parse mismatch\n line=%s\n got idx=%d ssid=[%s] vendor=[%s] bssid=[%s] ch=%d sec=[%s] rssi=%d band=[%s]\n",
                line, net.index, net.ssid, net.vendor, net.bssid, net.channel, net.security, net.rssi, net.band);
        exit(1);
    }
    assert(memchr(net.ssid, '\0', sizeof(net.ssid)));
    assert(memchr(net.vendor, '\0', sizeof(net.vendor)));
    assert(strlen(net.ssid) < sizeof(net.ssid));
    assert(strlen(net.vendor) < sizeof(net.vendor));

    int got_index = 0;
    char got_ssid[33], got_bssid[18];
    memset(got_ssid, 0x5A, sizeof(got_ssid));
    memset(got_bssid, 0x5A, sizeof(got_bssid));
    assert(observer_parse_scan_identity(line, &got_index, got_ssid, sizeof(got_ssid),
                                       got_bssid, sizeof(got_bssid)));
    assert(got_index == index);
    assert(!strcmp(got_ssid, ssid));
    assert(!strcmp(got_bssid, bssid));

    char info[256];
    format_network_info(info, sizeof(info), net.bssid, net.channel, net.band, net.security,
                        net.rssi, NULL, NULL, net.vendor);
    char vendor_label[80];
    snprintf(vendor_label, sizeof(vendor_label), "Vendor: %s", vendor[0] ? vendor : "-");
    assert(strstr(info, vendor_label));
    assert(strstr(info, bssid));
    assert(strstr(info, band));
    assert(strstr(info, security));
}

/* Build one quoted CSV field, doubling quotes. Returns bytes written, or -1. */
static int csv_field(char *dst, size_t dst_sz, const char *value) {
    size_t n = 0;
    if (n + 1 >= dst_sz) return -1;
    dst[n++] = '"';
    for (size_t i = 0; value[i]; i++) {
        if (value[i] == '"') {
            if (n + 2 >= dst_sz) return -1;
            dst[n++] = '"';
            dst[n++] = '"';
        } else {
            if (n + 1 >= dst_sz) return -1;
            dst[n++] = value[i];
        }
    }
    if (n + 1 >= dst_sz) return -1;
    dst[n++] = '"';
    dst[n] = '\0';
    return (int)n;
}

int main(void) {
    const char *quoted =
        "\"1\",\"moja \"\"specjalna\"\" siec\",\"Acme \"\"X\"\" Inc\",\"AA:BB:CC:DD:EE:FF\",\"6\",\"WPA2\",\"-50\",\"2.4GHz\"";
    expect_line(quoted, 1, "moja \"specjalna\" siec", "Acme \"X\" Inc",
                "AA:BB:CC:DD:EE:FF", 6, "WPA2", -50, "2.4GHz");

    const char *commas =
        "\"2\",\"moja, \"\"specjalna\"\" siec\",\"Acme, \"\"X\"\" Inc\",\"AA:BB:CC:DD:EE:F0\",\"11\",\"WPA3\",\"-40\",\"5GHz\"";
    expect_line(commas, 2, "moja, \"specjalna\" siec", "Acme, \"X\" Inc",
                "AA:BB:CC:DD:EE:F0", 11, "WPA3", -40, "5GHz");

    const char *plain =
        "\"3\",\"Cafe\",\"Acme Inc\",\"AA:BB:CC:DD:EE:01\",\"1\",\"OPEN\",\"-70\",\"2.4GHz\"";
    expect_line(plain, 3, "Cafe", "Acme Inc", "AA:BB:CC:DD:EE:01", 1, "OPEN", -70, "2.4GHz");

    const char *empty_vendor =
        "\"4\",\"Cafe\",\"\",\"AA:BB:CC:DD:EE:02\",\"6\",\"WPA2\",\"-55\",\"2.4GHz\"";
    expect_line(empty_vendor, 4, "Cafe", "", "AA:BB:CC:DD:EE:02", 6, "WPA2", -55, "2.4GHz");

    /* Older unquoted vendor split must keep BSSID/channel/RSSI anchored on the MAC. */
    const char *split =
        "\"1\",\"TP-Link_0E90\",\"\",TP-LINK TECHNOLOGIES CO., LTD.,\"B0:BE:76:70:0E:90\",\"4\",\"WPA2\",\"-62\",\"2.4GHz\"";
    expect_line(split, 1, "TP-Link_0E90", "TP-LINK TECHNOLOGIES CO., LTD.",
                "B0:BE:76:70:0E:90", 4, "WPA2", -62, "2.4GHz");

    wifi_network_t net = {0};
    assert(parse_network_line(quoted, &net));
    char escaped[67];
    beacon_spam_escape_quoted_arg(net.ssid, escaped, sizeof(escaped));
    assert(!strcmp(escaped, "moja \\\"specjalna\\\" siec"));
    char cmd[160];
    assert(build_wifi_connect_command(cmd, sizeof(cmd), net.ssid, "p\"x", WIFI_CONNECT_AUTH_PASSWORD));
    assert(!strcmp(cmd, "wifi_connect \"moja \\\"specjalna\\\" siec\" \"p\\\"x\""));
    assert(build_wifi_connect_command(cmd, sizeof(cmd), net.ssid, NULL, WIFI_CONNECT_AUTH_OPEN));
    assert(!strcmp(cmd, "wifi_connect \"moja \\\"specjalna\\\" siec\""));
    char tiny[8];
    assert(!build_wifi_connect_command(tiny, sizeof(tiny), net.ssid, NULL, WIFI_CONNECT_AUTH_OPEN));
    char clip[4];
    beacon_spam_escape_quoted_arg(net.ssid, clip, sizeof(clip));
    assert(clip[sizeof(clip) - 1] == '\0');
    assert(strlen(clip) < sizeof(clip));

    /* 32-byte SSID and a vendor longer than the 48-byte field stay inside their buffers. */
    char ssid[33];
    memset(ssid, 'A', 32);
    ssid[5] = '"';
    ssid[18] = '"';
    ssid[32] = '\0';
    char vendor[80];
    memset(vendor, 'B', sizeof(vendor) - 1);
    vendor[10] = '"';
    vendor[40] = ',';
    vendor[sizeof(vendor) - 1] = '\0';
    char line[512];
    int used = snprintf(line, sizeof(line), "\"9\",");
    int wrote = csv_field(line + used, sizeof(line) - (size_t)used, ssid);
    assert(wrote > 0);
    used += wrote;
    used += snprintf(line + used, sizeof(line) - (size_t)used, ",");
    wrote = csv_field(line + used, sizeof(line) - (size_t)used, vendor);
    assert(wrote > 0);
    used += wrote;
    snprintf(line + used, sizeof(line) - (size_t)used,
             ",\"AA:BB:CC:DD:EE:09\",\"36\",\"WPA2\",\"-33\",\"5GHz\"");
    wifi_network_t bounded = {0};
    assert(parse_network_line(line, &bounded));
    assert(bounded.index == 9);
    assert(strlen(bounded.ssid) == 32);
    assert(bounded.ssid[5] == '"' && bounded.ssid[18] == '"');
    assert(!strcmp(bounded.bssid, "AA:BB:CC:DD:EE:09"));
    assert(bounded.channel == 36 && bounded.rssi == -33);
    assert(!strcmp(bounded.security, "WPA2"));
    assert(!strcmp(bounded.band, "5GHz"));
    assert(strlen(bounded.vendor) == sizeof(bounded.vendor) - 1);
    assert(bounded.vendor[10] == '"');
    assert(memchr(bounded.vendor, '\0', sizeof(bounded.vendor)));

    assert(!parse_network_line("Scan results printed.", &net));
    assert(!parse_network_line("\"1\",\"only-ssid\"", &net));
    puts("rfc4180 scan csv passed");
    return 0;
}
'''


class ScanCsvRfc4180(unittest.TestCase):
    def test_scan_screens_share_the_production_parser(self):
        for name in (
            "static void wifi_scan_task",
            "static bool wardrive_wigle_ensure_wifi_connected",
            "static void ota_scan_task",
            "static void home_mgmt_scan_task",
            "static void wpasec_upload_task",
            "static bool parse_scan_to_observer",
        ):
            body = function_body(SOURCE, name)
            self.assertIn("parse_network_line(", body, name)

    def test_home_add_escapes_a_scanned_ssid_before_quoting_it(self):
        body = function_body(SOURCE, "static void home_mgmt_add_cb")
        self.assertLess(body.index("beacon_spam_escape_quoted_arg(ssid"), body.index('home_add \\"'))

    @unittest.skipUnless(shutil.which("gcc"), "host gcc required")
    def test_firmware_example_round_trip(self):
        harness = HARNESS
        with tempfile.TemporaryDirectory() as directory:
            cfile = Path(directory) / "scan_csv.c"
            binary = Path(directory) / "scan_csv"
            cfile.write_text(harness, encoding="utf-8")
            compiled = subprocess.run(
                ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-g", "-fsanitize=address,undefined", str(cfile), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn("rfc4180 scan csv passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
