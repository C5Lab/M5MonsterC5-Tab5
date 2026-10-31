"""Browser acceptance for Deauth Detector history column headings.

Run after rebuilding the browser emulator. The page under test is the native
Tab5 C/LVGL screen; the single JanOS event is supplied over its emulator wire.
"""
import os
from pathlib import Path
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


HEADINGS = ("CHANNEL / EVENT", "NETWORK / SSID", "BSSID", "RSSI (dBm)")
EVENT = ("[DEAUTH_V2] CH: 1 | BSSID: 02:20:77:00:00:01 | "
         "TX: AA:BB:CC:DD:EE:01 | DA: FF:FF:FF:FF:FF:FF | "
         "RSSI: -42 | REASON: 7 | SSID: Lab (east)")


class DetectorHeaderBrowser(unittest.TestCase):
    def test_headings_identify_columns_above_history_in_both_orientations(self):
        with browser_session() as (browser, url):
            for rotation, size in ((1, (1280, 720)), (0, (720, 1280))):
                with self.subTest(rotation=rotation):
                    page = browser.new_page(viewport={"width": 1440, "height": 1400})
                    try:
                        page.goto(f"{url}/?rotation={rotation}")
                        page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                        click_text(page, "Deauth\nDetector")
                        page.evaluate("""() => {
                            globalThis.headerTestWire = [];
                            emulator.device.deauthDetectorLine = () =>
                                headerTestWire.shift() || '';
                        }""")
                        start = next(item for item in objects(page)
                                     if "deauth_start_btn" in item["binding"])
                        click_object(page, start)
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text === 'Detector running');
                        }""", timeout=30000)
                        page.evaluate("line => headerTestWire.push(line)", EVENT)
                        page.wait_for_function("""() => {
                            for (let i = 0; i < 15; i++)
                                emulator.module._emu_tick(100);
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text === 'Lab (east)');
                        }""", timeout=30000)

                        inspected = objects(page)
                        labels = {heading: [item for item in inspected
                                            if item["text"] == heading]
                                  for heading in HEADINGS}
                        for heading, matches in labels.items():
                            self.assertEqual(len(matches), 1,
                                             f"Expected one visible {heading} heading")
                        headings = [labels[name][0] for name in HEADINGS]
                        first_row = next(item for item in inspected
                                         if item["text"] == "Lab (east)")
                        for heading in headings:
                            self.assertGreaterEqual(heading["x"], 0)
                            self.assertGreaterEqual(heading["y"], 0)
                            self.assertLessEqual(heading["x"] + heading["width"], size[0])
                            self.assertLessEqual(heading["y"] + heading["height"], size[1])
                            self.assertLessEqual(heading["y"] + heading["height"],
                                                 first_row["y"])
                        for index, left in enumerate(headings):
                            for right in headings[index + 1:]:
                                overlap_x = (left["x"] < right["x"] + right["width"]
                                             and right["x"] < left["x"] + left["width"])
                                overlap_y = (left["y"] < right["y"] + right["height"]
                                             and right["y"] < left["y"] + left["height"])
                                self.assertFalse(overlap_x and overlap_y,
                                                 f"Overlapping headings: {left} and {right}")
                        if screenshot_dir := os.environ.get("DEAUTH_HEADER_SCREENSHOT_DIR"):
                            destination = Path(screenshot_dir) / f"deauth-header-rotation-{rotation}.png"
                            page.locator("#display").screenshot(path=str(destination))
                    finally:
                        page.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
