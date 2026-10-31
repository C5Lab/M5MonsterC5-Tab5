"""Native LVGL acceptance using a literal, synthetic CSA wire record."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object


class CsaDetectorBrowserTests(unittest.TestCase):
    def test_mixed_history_cards_do_not_overlap_in_either_orientation(self):
        events = (
            "[DEAUTH_V2] CH: 11 | BSSID: AC:22:05:A2:30:F5 | "
            "TX: 70:EE:50:0F:0F:90 | DA: AC:22:05:A2:30:F5 | "
            "RSSI: -87 | REASON: 7 | SSID: <Unknown>",
            "[CSA] RX: 11 | TO: 1 | BSSID: 84:A1:D1:C9:83:E0 | "
            "TX: 84:A1:D1:C9:83:E0 | RSSI: -87 | VIA: probe | "
            "COUNT: 3 | MODE: 0 | CLASS: 4 | SSID: Orange_Swiatlowod_83E0",
            "[DEAUTH] CH: 2 | AP: Legacy AP (with parens) "
            "(D8:CF:61:5D:41:10) | RSSI: -40",
            "[CSA] RX: 4 | TO: 9 | BSSID: B0:95:75:42:6A:89 | "
            "TX: B0:95:75:42:6A:89 | RSSI: -46 | VIA: beacon | "
            "COUNT: 0 | MODE: 1 | CLASS: 81 | SSID: Robusta_Monika",
        )
        summaries = (
            "CSA | Orange_Swiatlowod_83E0 | RX CH11 -> TO CH1",
            "CSA | Robusta_Monika | RX CH4 -> TO CH9",
        )

        with browser_session() as (browser, url):
            for rotation in (1, 0):
                with self.subTest(rotation=rotation):
                    page = browser.new_page(viewport={"width": 1440, "height": 1400})
                    try:
                        page.goto(f"{url}/?rotation={rotation}")
                        page.wait_for_function(
                            "globalThis.emulator?.measurements.frames > 2")
                        page.evaluate("""() => {
                            globalThis.mixedDetectorWire = [];
                            emulator.device.deauthDetectorLine = () =>
                                mixedDetectorWire.shift() || '';
                        }""")
                        click_text(page, "Deauth\nDetector")
                        start = next(item for item in objects(page)
                                     if "deauth_start_btn" in item["binding"])
                        click_object(page, start)
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text === 'Detector running');
                        }""", timeout=30000)

                        for event in events:
                            page.evaluate("line => mixedDetectorWire.push(line)", event)
                            page.evaluate("""() => {
                                for (let i = 0; i < 15; i++)
                                    emulator.module._emu_tick(100);
                            }""")

                        inspected = objects(page)
                        by_id = {item["id"]: item for item in inspected}
                        for summary_text in summaries:
                            summary = next(item for item in inspected
                                           if item["text"] == summary_text)
                            row = by_id[summary["parent"]]
                            labels = sorted(
                                (item for item in inspected
                                 if item["parent"] == row["id"] and item["text"]),
                                key=lambda item: item["y"])
                            self.assertEqual(len(labels), 4, labels)
                            for upper, lower in zip(labels, labels[1:]):
                                self.assertGreaterEqual(
                                    lower["y"], upper["y"] + upper["height"] + 4,
                                    f"Overlapping CSA labels in row {row}: {labels}")
                            self.assertGreaterEqual(labels[0]["y"], row["y"] + 8)
                            self.assertLessEqual(
                                labels[-1]["y"] + labels[-1]["height"],
                                row["y"] + row["height"] - 8)
                    finally:
                        page.close()

    def test_csa_row_and_stop_use_retained_detector_ui(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1100})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                page.evaluate("""() => {
                    emulator.device.deauthDetectorLine = () =>
                      '[CSA] RX: 6 | TO: 11 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -61 | VIA: beacon | COUNT: 3 | MODE: 1 | CLASS: - | SSID: Lab (east)';
                }""")
                click_text(page, "Deauth\nDetector")

                def click_binding(needle):
                    matches = [item for item in objects(page) if needle in item["binding"]]
                    self.assertEqual(len(matches), 1)
                    click_object(page, matches[0])

                def advance():
                    page.evaluate("() => { for(let i=0; i<40; i++) emulator.module._emu_tick(100); }")

                click_binding("deauth_start_btn")
                advance()
                labels = [item["text"] for item in objects(page)]
                self.assertIn("CSA | Lab (east) | RX CH6 -> TO CH11", labels)
                self.assertIn("BSSID: 00:11:22:33:44:55 | Claimed TX: AA:BB:CC:DD:EE:FF", labels)
                self.assertIn("beacon | JanOS COUNT: 3 | -61 dBm | Mode: 1 | Class: -", labels)
                before = labels.count("CSA | Lab (east) | RX CH6 -> TO CH11")
                self.assertGreater(before, 0)
                click_binding("deauth_stop_btn")
                frozen = [item["text"] for item in objects(page)].count("CSA | Lab (east) | RX CH6 -> TO CH11")
                advance()
                labels = [item["text"] for item in objects(page)]
                self.assertEqual(labels.count("CSA | Lab (east) | RX CH6 -> TO CH11"), frozen)
                self.assertEqual(errors, [])
            finally:
                page.close()


if __name__ == "__main__":
    unittest.main()
