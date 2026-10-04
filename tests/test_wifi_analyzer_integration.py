"""Compilation-free guards for the additive Tab5 analyzer integration.

These tests inspect source boundaries, not executed C/FreeRTOS/LVGL behavior.
"""
from pathlib import Path
import hashlib
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

NONCODE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.S)


def code_only(source):
    return NONCODE.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), source)


def function_body(source, name):
    masked = code_only(source)
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", masked, re.S)
    if match is None:
        raise AssertionError("Missing function: " + name)
    opening = match.end() - 1
    depth = 0
    for position in range(opening, len(masked)):
        depth += (masked[position] == "{") - (masked[position] == "}")
        if not depth:
            return source[opening + 1:position]
    raise AssertionError("Unclosed function: " + name)


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


class AnalyzerIntegrationTests(unittest.TestCase):
    def test_analyzer_is_last_external_tool_tile(self):
        page = function_body(read("main/main.c"), "create_uart_tiles_in_container")
        tile_calls = re.findall(r'create_tile\(.*?;', page, re.S)
        self.assertIn('"WiFi Analyzer"', tile_calls[-1])

    def test_advisor_uses_full_snapshot_and_fails_closed_on_partial_data(self):
        model = function_body(read("main/wifi_analyzer_model.c"), "wa_propose_channels")
        self.assertIn("snapshot->truncated", model)
        self.assertIn("advice_scope_complete", model)
        self.assertIn("wa_ap_segments", model)
        self.assertNotIn("wa_ap_matches", model)
        self.assertIn("exclude_bssid", model)
        self.assertIn("snapshot->count", model)
        ui = read("main/screens/wifi_analyzer_screen.c")
        advice = function_body(ui, "update_advice")
        self.assertIn("u->status.stale", advice)
        self.assertIn("u->status.age_ms", advice)
        self.assertIn("wa_propose_channels", advice)
        self.assertIn("country/driver limits", advice)
        self.assertIn("repeating the same scan will not resolve it", advice)
        for name in ("update_advice", "propose_event", "advice_options_event"):
            self.assertNotRegex(function_body(ui, name), r"wa_(?:start|stop|leave)\s*\(")

    def test_160_geometry_rejects_misaligned_full_channel_center(self):
        geometry = function_body(read("main/wifi_analyzer_model.c"), "geometry")
        self.assertIn("a->width==160", geometry)
        for center in (5250, 5570, 5815):
            self.assertIn(f"a->center1_mhz!={center}", geometry)

    def test_additive_modules_exist(self):
        for name in ("wifi_analyzer.h", "wifi_analyzer.c", "wifi_analyzer_model.h",
                     "wifi_analyzer_model.c", "screens/wifi_analyzer_screen.h",
                     "screens/wifi_analyzer_screen.c"):
            with self.subTest(name=name):
                self.assertTrue((ROOT / "main" / name).is_file(), name)

    def test_legacy_parser_formatter_and_selection_are_unchanged(self):
        # Baseline HEAD at task entry. Freeze the presentation/selection seams,
        # not unrelated functions that intentionally acquire ownership guards.
        # Parser baseline includes the MAC-anchored vendor fix; executed wire
        # regression cases are in test_observer_vendor_features.py.
        expected = {
            "parse_network_line": "8caff54e07a5be19aab8e08550d0a8ae2a61d27ef7836e71915dd733fac5a504",
            "format_network_info": "60ae4556fe35b701fa49e92ed19556525158d4f83f7e768873c9b98085e2e9f1",
            "get_scan_view": "69eea3b91f2783ad7b3332ade93a95453b557a0537be3c3e9d67570a66039b40",
        }
        source = read("main/main.c")
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256(function_body(source, name).encode()).hexdigest(), digest, name)

    def test_large_session_buffers_use_psram_without_fallback(self):
        source = read("main/wifi_analyzer.c")
        body = code_only(function_body(source, "wa_session_get"))
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", body)
        self.assertIn("heap_caps_calloc", body)
        self.assertNotRegex(body, r"(?<!heap_caps_)\b(?:malloc|calloc|realloc)\s*\(")
        self.assertIn("wa_snapshot_t working, committed", source)

    def test_model_is_bounded_and_has_no_transport_or_gui_dependency(self):
        source = code_only(read("main/wifi_analyzer_model.c"))
        for forbidden in ("malloc", "calloc", "realloc", "uart_read_bytes", "lv_label_set_text"):
            self.assertNotRegex(source, r"\b" + forbidden + r"\s*\(")
        header = read("main/wifi_analyzer_model.h")
        self.assertRegex(header, r"#define WA_MAX_APS 128\b")
        self.assertRegex(header, r"#define WA_MAX_LINE_BYTES 1024\b")
        self.assertIn("wa_snapshot_t *working, *committed", header)

    def test_worker_owns_stop_and_waits_for_idle_before_release(self):
        source = read("main/wifi_analyzer.c")
        stop = code_only(function_body(source, "wa_stop"))
        self.assertIn("atomic_store", stop)
        self.assertNotIn("s_host.write", stop)
        recovery = function_body(source, "recover_idle")
        self.assertIn("wifi_analyzer stop", recovery)
        self.assertIn("query_idle", recovery)
        self.assertIn("while (s_host.connected", recovery)
        task = code_only(function_body(source, "analyzer_task"))
        self.assertLess(task.rindex("recover_idle(session)"), task.index("s_host.release(session->tab)"))
        self.assertLess(task.index("s_host.release(session->tab)"), task.index("atomic_store(&session->running, false)"))
        self.assertNotIn("vTaskDelete(session", source)

    def test_request_settings_cannot_inject_console_commands(self):
        source = read("main/wifi_analyzer.c")
        validation = function_body(source, "validate_request")
        for guard in ("request->limit > WA_MAX_APS", "request->profile > 2", "wa_frequency", "channels[i] == channel"):
            self.assertIn(guard, validation)
        self.assertIn("*p < '0' || *p > '9'", validation)
        self.assertIn("*p++ != ','", validation)
        start = function_body(source, "wa_start")
        self.assertLess(start.index("validate_request(request)"), start.index("xTaskCreate("))

    def test_pre_begin_rejection_requires_idle_before_transport_release(self):
        task = function_body(read("main/wifi_analyzer.c"), "analyzer_task")
        rejection = task.split("rejected = true;", 1)[1].split("break;", 1)[0]
        self.assertIn("query_idle(session, 3000)", rejection)
        self.assertIn("recover_idle(session)", rejection)
        self.assertLess(rejection.index("query_idle"), rejection.index("remote_may_scan = false"))

    def test_repeat_budget_accounts_for_scan_and_wire_duration(self):
        source = read("main/wifi_analyzer.c")
        budget = function_body(source, "scan_budget")
        self.assertIn("request->limit + 2", budget)
        self.assertIn("1027 * 10000", budget)
        self.assertIn("channels * (dwell + 150)", budget)
        self.assertIn("session->request.repeat_ms", function_body(source, "analyzer_task"))
        self.assertIn("wa_stop(session_for(tab))", function_body(source, "wa_leave"))

    def test_all_modules_are_linked_and_tile_is_additive(self):
        cmake = read("main/CMakeLists.txt")
        for name in ('"wifi_analyzer.c"', '"wifi_analyzer_model.c"', '"screens/wifi_analyzer_screen.c"'):
            self.assertIn(name, cmake)
        source = read("main/main.c")
        self.assertIn('"WiFi Analyzer"', source)
        self.assertIn('"WiFi Scan & Attack"', source)
        self.assertIn("wa_init(&analyzer_hooks)", source)

    def test_no_analyzer_module_reuses_legacy_selection(self):
        for name in ("main/wifi_analyzer.c", "main/wifi_analyzer_model.c", "main/screens/wifi_analyzer_screen.c"):
            source = code_only(read(name))
            for symbol in ("parse_network_line", "selected_indices", "wifi_scan_task", "creds_fetch"):
                self.assertNotRegex(source, r"\b" + symbol + r"\b", name)

    def test_host_claim_uses_console_mutex_and_shared_owner(self):
        source = read("main/main.c")
        claim = function_body(source, "wa_host_claim")
        self.assertIn("xSemaphoreTake", claim)
        self.assertIn("crack_transport_owner", claim)
        self.assertLess(claim.index("xSemaphoreTake"), claim.index("crack_transport_owner"))
        release = function_body(source, "wa_host_release")
        self.assertIn("xSemaphoreGive", release)
        self.assertIn("crack_transport_owner", release)
        self.assertIn("wa_busy_tab", function_body(source, "home_meta_refresh_allowed"))

    def test_ui_and_worker_communicate_through_snapshot_copy(self):
        source = code_only(read("main/screens/wifi_analyzer_screen.c"))
        for required in ("wa_copy_view", "wa_ap_matches", "wa_ap_color_rgb", "wa_frequency", "wa_stop"):
            self.assertRegex(source, r"\b" + required + r"\s*\(")
        for forbidden in ("uart_read_bytes", "transport_read_bytes_tab", "vTaskDelete"):
            self.assertNotRegex(source, r"\b" + forbidden + r"\s*\(")


if __name__ == "__main__":
    unittest.main()
