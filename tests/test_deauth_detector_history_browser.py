"""Browser acceptance for the retained Tab5 detector history UI.

Run after rebuilding the browser emulator. JanOS lines are synthetic; all
grouping, time labels and controls are rendered by the retained C/LVGL page.
"""
import csv
import io
import re
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


EVENT = ("[DEAUTH_V2] CH: 1 | BSSID: 02:20:77:00:00:01 | "
         "TX: AA:BB:CC:DD:EE:01 | DA: FF:FF:FF:FF:FF:FF | "
         "RSSI: -42 | REASON: 7 | SSID: Lab (east)")


class DetectorHistoryBrowser(unittest.TestCase):
    @staticmethod
    def labels(page):
        return [item["text"] for item in objects(page) if item["text"]]

    @staticmethod
    def advance(page, ticks=15):
        page.evaluate("ticks => { for (let i = 0; i < ticks; i++) "
                      "emulator.module._emu_tick(100); }", ticks)

    def click_binding(self, page, name):
        matches = [item for item in objects(page) if name in item["binding"]]
        self.assertEqual(len(matches), 1, name)
        click_object(page, matches[0])

    def click_clear(self, page):
        matches = [item for item in objects(page)
                   if re.match(r"^Clear(?:\s|$)", item["text"], re.I)]
        self.assertEqual(len(matches), 1, "Expected one visible Clear control")
        click_object(page, matches[0])

    def open_with_synthetic_wire(self, page):
        page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
        click_text(page, "Deauth\nDetector")
        page.evaluate("""() => {
            globalThis.detectorHistoryWire = [];
            emulator.device.deauthDetectorLine = () =>
                detectorHistoryWire.shift() || '';
        }""")

    def start(self, page):
        self.click_binding(page, "deauth_start_btn")
        page.wait_for_function("""() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(item => item.text === 'Detector running');
        }""", timeout=30000)

    def stop(self, page):
        self.click_binding(page, "deauth_stop_btn")
        page.wait_for_function("""() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(item => item.text === 'Stopped');
        }""", timeout=30000)

    def emit(self, page, count):
        for _ in range(count):
            page.evaluate("line => detectorHistoryWire.push(line)", EVENT)
            self.advance(page)

    def test_repeated_deauth_is_grouped_with_seen_and_detection_times(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_with_synthetic_wire(page)
                self.start(page)
                self.emit(page, 3)
                labels = self.labels(page)
                self.assertEqual(labels.count("Lab (east)"), 1, labels)
                self.assertEqual(sum(bool(re.search(r"\bSeen:\s*3\b", s))
                                     for s in labels), 1, labels)
                first = next((s for s in labels if "First:" in s), "")
                last = next((s for s in labels if "Last:" in s), "")
                self.assertRegex(first, r"First:\s*\S")
                self.assertRegex(last, r"Last:\s*\S")
                self.stop(page)
                self.advance(page, 30)
                self.assertEqual(self.labels(page).count("Lab (east)"), 1)
                self.click_clear(page)
                self.assertNotIn("Lab (east)", self.labels(page))
                self.assertTrue(any("CSV" in label and
                                    ("Save" in label or "Export" in label)
                                    for label in self.labels(page)),
                                "Expected a visible CSV Save/Export control")
            finally:
                page.close()

    def test_stopped_history_is_cleared_on_next_confirmed_start(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_with_synthetic_wire(page)
                self.start(page)
                self.emit(page, 1)
                self.stop(page)
                self.assertIn("Lab (east)", self.labels(page))
                self.start(page)
                self.assertNotIn("Lab (east)", self.labels(page))
            finally:
                page.close()

    def test_save_csv_uses_tab5_local_sd_and_reports_missing_card(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_with_synthetic_wire(page)
                self.start(page)
                self.emit(page, 2)
                self.stop(page)
                page.evaluate("""() =>
                    emulator.module.FS.rename('/sdcard', '/__tab5_sd_missing')
                """)
                self.assertTrue(page.evaluate("""() =>
                    emulator.device.device.systemStatus('grove').sdPresent
                """), "JanOS module SD should remain separate from Tab5 SD")
                click_text(page, "Save CSV")
                page.wait_for_function("""() => {
                    emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Save failed: Tab5 SD card is not mounted');
                }""", timeout=30000)
                page.evaluate("""() =>
                    emulator.module.FS.rename('/__tab5_sd_missing', '/sdcard')
                """)
                click_text(page, "Save CSV")
                page.wait_for_function("""() => {
                    emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.startsWith('Saved snapshot of 1 groups to Tab5 SD: '));
                }""", timeout=30000)
                saved = page.evaluate("""() => {
                    const fs = emulator.module.FS;
                    const dir = '/sdcard/lab/detector';
                    const names = fs.readdir(dir).filter(name => name.endsWith('.csv'));
                    const path = `${dir}/${names[0]}`;
                    return {names, path, csv: new TextDecoder().decode(fs.readFile(path))};
                }""")
                self.assertEqual(len(saved["names"]), 1, saved)
                self.assertRegex(saved["names"][0], r"^detector_.*\.csv$")
                rows = list(csv.DictReader(io.StringIO(saved["csv"])))
                self.assertEqual(len(rows), 1, rows)
                self.assertEqual(rows[0]["type"], "DEAUTH")
                self.assertEqual(rows[0]["ssid"], "Lab (east)")
                self.assertEqual(rows[0]["bssid"], "02:20:77:00:00:01")
                self.assertEqual(rows[0]["rx_channel"], "1")
                self.assertEqual(rows[0]["observations"], "2")
                self.assertTrue(any(saved["path"] in label for label in self.labels(page)))
            finally:
                page.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
