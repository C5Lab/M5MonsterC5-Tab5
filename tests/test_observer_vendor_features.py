"""Observer vendor regression tests; run with host Python + gcc (including WSL)."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from test_observer_incremental_ui_contract import function_body

SOURCE = (Path(__file__).resolve().parents[1] / "main/main.c").read_text(encoding="utf-8")


class VendorIntegration(unittest.TestCase):
    def test_start_synchronizes_both_settings_before_scan(self):
        body = function_body(SOURCE, "static void observer_start_task")
        self.assertLess(body.index("observer_apply_vendor_settings(ctx)"), body.index('"scan_networks'))

    def test_log_setting_persisted_and_dependent_on_resolution(self):
        body = function_body(SOURCE, "static void observer_apply_vendor_settings")
        for cmd in ("vendor set on", "vendor set off", "vendor log on", "vendor log off"):
            self.assertIn(cmd, body)
        self.assertIn("observer_log_unknown_vendors", body)
        self.assertIn("home_vendors_present", body)
        self.assertIn('"obs_vendor_log"', SOURCE)
        self.assertIn("Log unknown vendors", SOURCE)

    def test_ap_vendor_reaches_stored_network_in_both_polls(self):
        for task in ("popup_poll_task", "observer_poll_task"):
            body = function_body(SOURCE, "static void " + task)
            self.assertIn("observer_receive_ap(ctx, line_buffer, vendor_query", body)
        receiver = (Path(__file__).resolve().parents[1] / "main/observer_receive.inc").read_text()
        self.assertIn("if (vendor) snprintf(net->vendor", receiver)

    def test_vendor_updates_refresh_existing_rows_without_rebuilding(self):
        body = function_body(SOURCE, "static void observer_sync_changed_tiles")
        self.assertIn("observer_update_client_vendor_labels", body)
        self.assertNotIn("lv_obj_clean", body)

    def test_controls_live_only_in_scan_setup(self):
        observer = function_body(SOURCE, "static void show_observer_page")
        setup = function_body(SOURCE, "static void show_scan_time_popup")
        self.assertNotIn("vendor_controls", observer)
        self.assertNotIn("lv_checkbox_create", observer)
        self.assertIn("scan_setup_vendor_log_switch", setup)
        self.assertIn("Log unknown vendors", setup)

    def test_setup_preferences_are_shared_with_observer_without_blocking_reads(self):
        for name in ("scan_setup_vendor_switch_cb", "scan_setup_vendor_log_switch_cb"):
            body = function_body(SOURCE, "static void " + name)
            self.assertIn("observer_apply_vendor_settings", body)
            self.assertIn("observer_update_other_vendor_contexts", body)
            self.assertNotIn("vendor_read_from_target", body)
            self.assertNotIn("home_collect_uart_response", body)

    def test_command_terminator_is_written_with_the_command(self):
        body = function_body(SOURCE, "static void observer_send_command_to_transport")
        self.assertEqual(body.count("transport_write_bytes_tab("), 1)
        self.assertIn('"%s\\r\\n"', body)


@unittest.skipUnless(shutil.which("gcc"), "host gcc required for executed C parser tests")
class VendorParser(unittest.TestCase):
    def test_wire_cases(self):
        functions = []
        for result_type, name in (("bool", "observer_extract_vendor_bracket"),
                     ("bool", "parse_sniffer_network_line"),
                     ("bool", "parse_sniffer_client_line"), ("bool", "add_client_mac"),
                     ("int", "parse_csv_mixed_fields"), ("bool", "observer_scan_field_is_mac"),
                     ("bool", "parse_network_line"), ("void", "format_network_info")):
            prefix = "static " + result_type + " " + name
            body = function_body(SOURCE, prefix)
            start = SOURCE.index(body, SOURCE.index(prefix))
            # Locate the definition signature, skipping forward declarations.
            signature = re.search(prefix + r"\([^;{}]*\)\s*$", SOURCE[:start], re.S)
            self.assertIsNotNone(signature, name)
            functions.append(signature.group() + body)
        harness = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#define MAX_CLIENTS_PER_NETWORK 4
typedef struct { char ssid[33]; int channel, client_count; char vendor[48];
char clients[4][18], client_vendors[4][48]; } observer_network_t;
typedef struct { int index; char ssid[33], bssid[18]; int channel, rssi;
 char band[8], security[24], vendor[48]; } wifi_network_t;
static bool wide;
static bool ui_wide_layout(void) { return wide; }
static void trim_ascii_whitespace(char *s) {
 size_t n = strlen(s); while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
 size_t i=0; while (isspace((unsigned char)s[i])) ++i; memmove(s,s+i,strlen(s+i)+1);
}
''' + "\n".join(functions) + r'''
int main(void) {
 observer_network_t n={0}; char mac[18], v[48];
 assert(parse_sniffer_network_line("Home [SSID], CH6: 2 [Acme Corp]", &n, true));
 assert(!strcmp(n.vendor,"Acme Corp"));
 assert(parse_sniffer_network_line("Home [SSID], CH6: 2 [Unknown]", &n, true));
 assert(!n.vendor[0]);
 assert(parse_sniffer_network_line("Home [SSID], CH6: 2", &n, true));
 assert(!n.vendor[0]);
 strcpy(n.vendor,"Keep");
 assert(parse_sniffer_network_line("Home [SSID], CH6: 2", &n, false));
 assert(!strcmp(n.vendor,"Keep"));
 assert(parse_sniffer_client_line(" AA:BB:CC:DD:EE:FF [Acme Corp]",mac,sizeof(mac),v,sizeof(v),true));
 assert(!strcmp(mac,"AA:BB:CC:DD:EE:FF") && !strcmp(v,"Acme Corp"));
 n.client_count=0;
 assert(add_client_mac(&n,mac,v));
 assert(!add_client_mac(&n,mac,"Updated"));
 assert(n.client_count==1 && !strcmp(n.client_vendors[0],"Updated"));
 assert(!add_client_mac(&n,mac,""));
 assert(!n.client_vendors[0][0]);
 assert(parse_sniffer_client_line(" AA:BB:CC:DD:EE:FF [Unknown]",mac,sizeof(mac),v,sizeof(v),true));
 assert(!v[0]);
 assert(parse_sniffer_client_line(" AA:BB:CC:DD:EE:FF",mac,sizeof(mac),v,sizeof(v),false));
 assert(!v[0]);
 strcpy(n.client_vendors[0],"Keep");
 assert(!add_client_mac(&n,mac,NULL));
 assert(!strcmp(n.client_vendors[0],"Keep"));
 assert(!observer_extract_vendor_bracket("Home [SSID], CH6: 2",v,sizeof(v)) || !v[0]);
 assert(!observer_extract_vendor_bracket("[Acme] junk",v,sizeof(v)));
 wifi_network_t scan={0};
 assert(parse_network_line("\"1\",\"Virtual\",\"\",\"02:11:22:33:44:55\",\"6\",\"WPA2\",\"-50\",\"2.4GHz\"", &scan));
 assert(!scan.vendor[0]);
 assert(parse_network_line("\"1\",\"Real\",\"Acme Corp\",\"00:11:22:33:44:55\",\"6\",\"WPA2\",\"-50\",\"2.4GHz\"", &scan));
 assert(!strcmp(scan.vendor,"Acme Corp"));
 /* Older CSV can retain an empty slot before the vendor and split its comma.
    This reproduces the shifted BSSID/channel/RSSI/vendor seen on hardware. */
 assert(parse_network_line("\"1\",\"TP-Link_0E90\",\"\",TP-LINK TECHNOLOGIES CO., LTD.,\"B0:BE:76:70:0E:90\",\"4\",\"WPA2\",\"-62\",\"2.4GHz\"", &scan));
 assert(!strcmp(scan.bssid,"B0:BE:76:70:0E:90"));
 assert(scan.channel==4 && scan.rssi==-62);
 assert(!strcmp(scan.security,"WPA2") && !strcmp(scan.band,"2.4GHz"));
 assert(!strcmp(scan.vendor,"TP-LINK TECHNOLOGIES CO., LTD."));
 for (int rotation=0; rotation<2; ++rotation) {
   char text[512]; wide=rotation;
   format_network_info(text,sizeof(text),scan.bssid,scan.channel,scan.band,
                       scan.security,scan.rssi,"MFP Off","1d",scan.vendor);
   assert(strstr(text,"#5599FF B0:BE:76:70:0E:90#"));
   assert(strstr(text,"CH4#") && strstr(text,"-62 dBm#"));
   assert(strstr(text,"Vendor: TP-LINK TECHNOLOGIES CO., LTD."));
   assert(!strstr(text,"Vendor: -62"));
 }
 assert(parse_network_line("\"1\",\"TP-Link_0E90\",\"\",\"TP-LINK TECHNOLOGIES CO., LTD.\",\"B0:BE:76:70:0E:90\",\"4\",\"WPA2\",\"-62\",\"2.4GHz\"", &scan));
 assert(scan.channel==4 && scan.rssi==-62 && !strcmp(scan.bssid,"B0:BE:76:70:0E:90"));
 assert(!strcmp(scan.vendor,"TP-LINK TECHNOLOGIES CO., LTD."));
 assert(parse_network_line("\"1\",\"Virtual\",\"\",\"02:11:22:33:44:55\",\"1\",\"WPA2\",\"-77\",\"2.4GHz\"", &scan));
 assert(scan.channel==1 && scan.rssi==-77 && !scan.vendor[0]);
 assert(!strcmp(scan.bssid,"02:11:22:33:44:55"));
 assert(!parse_network_line("\"1\",\"Broken\",\"Acme\",\"not a MAC\",\"1\",\"WPA2\",\"-77\",\"2.4GHz\"", &scan));
 puts("vendor parser cases passed"); return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            cfile, binary = Path(d) / "vendor.c", Path(d) / "vendor"
            cfile.write_text(harness)
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", str(cfile), "-o", str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
