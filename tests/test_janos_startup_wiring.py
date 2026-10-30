"""Check that the tested startup policy is used by every JanOS transport."""

from pathlib import Path
import unittest


SOURCE = (Path(__file__).resolve().parents[1] / "main" / "main.c").read_text(encoding="utf-8")


def body(name: str) -> str:
    import re

    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\([^;\n]*\)\s*\{", SOURCE, re.M)
    if match is None:
        raise AssertionError(f"missing function {name}")
    opening = match.end() - 1
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[opening:end]


class JanosStartupWiringTests(unittest.TestCase):
    def test_every_port_recovers_detection_from_version_or_wps_status(self):
        detection = body("detect_boards")
        for tab in ("TAB_GROVE", "TAB_USB", "TAB_MBUS"):
            with self.subTest(tab=tab):
                self.assertIn(f"janos_recover_busy_board({tab})", detection)
        recovery = body("janos_recover_busy_board")
        self.assertLess(recovery.index("check_version_for_tab(tab)"),
                        recovery.index("check_wps_status_for_tab(tab)"))
        self.assertNotIn('"stop\\r\\n"', recovery)

    def test_boot_checks_wps_before_other_capability_probes(self):
        startup = body("boot_detect_run_probes")
        self.assertLess(startup.index("check_all_versions()"),
                        startup.index("check_all_wps_statuses()"))
        self.assertLess(startup.index("check_all_wps_statuses()"),
                        startup.index("check_all_sd_cards()"))
        self.assertIn("show_wps_active_popup()", body("boot_detection_done_async"))

    def test_failed_scan_and_observer_start_discard_partial_results(self):
        scan = body("wifi_scan_task")
        observer = body("observer_start_task")
        self.assertIn("if (!scan_complete)", scan)
        self.assertIn("ctx->network_count = 0;", scan[scan.index("if (!scan_complete)"):])
        self.assertIn("if (!scan_complete)", observer)
        self.assertIn("ctx->observer_network_count = 0;", observer[observer.index("if (!scan_complete)"):])
        self.assertLess(observer.index("if (!scan_complete)"),
                        observer.index("start_sniffer_noscan"))
        self.assertIn("Sniffer: Now monitoring", observer)
        self.assertIn("if (!sniffer_started)", observer)


if __name__ == "__main__":
    unittest.main()
