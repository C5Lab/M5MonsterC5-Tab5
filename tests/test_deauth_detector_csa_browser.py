"""Native LVGL acceptance using a literal, synthetic CSA wire record."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object


class CsaDetectorBrowserTests(unittest.TestCase):
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
                self.assertIn("beacon | Count: 3 | -61 dBm | Mode: 1 | Class: -", labels)
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
