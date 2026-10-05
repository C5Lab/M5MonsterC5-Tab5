"""Native extended Observer preferences and detail tabs in both orientations."""
import unittest
import re
from emulator_browser import objects, click_text, click_object
import test_emulator_portal_demo as helpers


class ObserverExtendedBrowser(unittest.TestCase):
    setUp = helpers.PortalDemoBrowser.setUp
    load = helpers.PortalDemoBrowser.load
    ready = helpers.PortalDemoBrowser.ready
    text = helpers.PortalDemoBrowser.text

    def start(self, rotation):
        self.load(rotation)
        click_text(self.page, "Network\nObserver")
        click_text(self.page, "Start")
        self.assertEqual(sum(o["binding"].endswith("/observe") for o in objects(self.page)), 12)

    def control(self, suffix):
        return next(o for o in objects(self.page) if o["binding"].endswith(suffix))

    def option(self, name):
        click_object(self.page, self.control(f"emu.observer.options.{name}/Grove"))

    def stored(self):
        return self.page.evaluate("localStorage.getItem('tab5.nvs.observer.view_prefs')")

    def metadata(self):
        return [re.sub(r"#[0-9A-Fa-f]{6} |#", "", o["text"]) for o in objects(self.page)
                if "CH" in o["text"] and "WPS" in o["text"] and "  |  " in o["text"]]

    def test_metadata_renders_scan_colors_in_both_orientations(self):
        for rotation in (0, 1):
            with self.subTest(rotation=rotation):
                self.start(rotation)
                self.page.wait_for_timeout(250)
                rows = [o for o in objects(self.page) if "CH" in o["text"] and "WPS" in o["text"] and "  |  " in o["text"]]
                self.assertTrue(all("#5599FF CH" in o["text"] for o in rows))
                counts = self.page.evaluate("""rows => {
                    const c = document.querySelector('#display');
                    const ctx = c.getContext('2d');
                    const counts = {blue:0, green:0, amber:0, cyan:0};
                    for (const row of rows) {
                        const x=Math.max(0,row.x), y=Math.max(0,row.y);
                        const w=Math.min(row.width,c.width-x), h=Math.min(row.height,c.height-y);
                        if(w<=0 || h<=0) continue;
                        const p=ctx.getImageData(x,y,w,h).data;
                        for(let i=0;i<p.length;i+=4) {
                            const r=p[i],g=p[i+1],b=p[i+2];
                            if(b>200 && b>r+70 && b>g+50) counts.blue++;
                            if(g>140 && r<130 && b<130) counts.green++;
                            if(r>180 && g>80 && g<200 && b<90) counts.amber++;
                            if(r<80 && g>120 && b>120) counts.cyan++;
                        }
                    }
                    return counts;
                }""", rows)
                for color, count in counts.items():
                    self.assertGreater(count, 5, (rotation, color, counts))

    def test_options_uses_wardrive_dark_palette_in_both_orientations(self):
        for rotation in (0, 1):
            with self.subTest(rotation=rotation):
                self.start(rotation)
                click_text(self.page, "Options")
                current = objects(self.page)
                by_id = {o["id"]: o for o in current}
                title = next(o for o in current if o["text"] == "Observer Options")
                modal = by_id[title["parent"]]
                # Check rendered pixels as well as computed colors. RGB565
                # expands this Wardrive background to (24, 24, 41).
                self.page.wait_for_timeout(100)
                pixel = self.page.evaluate("([x,y])=>Array.from(document.querySelector('#display').getContext('2d').getImageData(x,y,1,1).data).slice(0,3)",
                    [modal["x"] + modal["width"] // 2, modal["y"] + 8])
                self.assertEqual(pixel, [24, 24, 41])
                self.assertEqual(modal["bg"] & 0xFFFFFF, 0x1A1A2A)
                self.assertEqual(modal["border"] & 0xFFFFFF, 0x009688)
                self.assertEqual(modal["borderWidth"], 3)
                self.assertEqual(title["fg"] & 0xFFFFFF, 0x009688)
                for text, expected in (("Networks", 0x009688), ("Clients", 0x1A1A2A)):
                    tab = by_id[next(o for o in current if o["text"] == text)["parent"]]
                    self.assertEqual(tab["bg"] & 0xFFFFFF, expected)
                    self.assertEqual(tab["fg"] & 0xFFFFFF, 0xFFFFFF)
                for action, expected in (("cancel", 0x444444), ("defaults", 0x2196F3), ("apply", 0x4CAF50)):
                    button = self.control(f"emu.observer.options.{action}/Grove")
                    self.assertEqual(button["bg"] & 0xFFFFFF, expected)
                check = self.control("emu.observer.options.networks.0.visible/Grove")
                self.assertEqual(check["fg"] & 0xFFFFFF, 0xFFFFFF)
                self.assertEqual(check["indicatorBg"] & 0xFFFFFF, 0x009688)
                row = by_id[check["parent"]]
                self.assertEqual(row["bgOpa"], 0)
                self.assertEqual(by_id[row["parent"]]["bgOpa"], 0)
                self.option("networks.0.visible")
                self.page.wait_for_timeout(250)
                check = self.control("emu.observer.options.networks.0.visible/Grove")
                self.assertEqual(check["indicatorBg"] & 0xFFFFFF, 0x0A1A1A)
                for suffix in ("networks.0.up", "networks.0.down"):
                    button = self.control(f"emu.observer.options.{suffix}/Grove")
                    self.assertEqual(button["bg"] & 0xFFFFFF, 0x333333 if suffix.endswith("up") else 0x455A64)
                self.option("cancel")
                click_object(self.page, self.control("/observe"))
                current = objects(self.page)
                by_id = {o["id"]: o for o in current}
                for text, expected in (("Overview", 0x009688), ("Security", 0x1A1A2A)):
                    tab = by_id[next(o for o in current if o["text"] == text)["parent"]]
                    self.assertEqual(tab["bg"] & 0xFFFFFF, expected)
                    self.assertEqual(tab["fg"] & 0xFFFFFF, 0xFFFFFF)
                technical = by_id[next(o for o in current if o["text"] == "Technical details")["parent"]]
                self.assertEqual(technical["bg"] & 0xFFFFFF, 0x455A64)

    def assert_header_controls_at_right(self, started):
        current = objects(self.page)
        by_id = {o["id"]: o for o in current}
        labels = ["Options", "Start", "Stop"]
        if started:
            labels.insert(1, "\uf0c7 Export")
        controls = [by_id[next(o for o in current if o["text"] == text)["parent"]]
                    for text in labels]
        actions = by_id[controls[-1]["parent"]]
        header = by_id[actions["parent"]]
        self.assertEqual(len({o["parent"] for o in controls}), 1)
        self.assertLessEqual(abs(controls[-1]["x"] + controls[-1]["width"] -
                                 header["x"] - header["width"]), 2)
        for left, right in zip(controls, controls[1:]):
            self.assertEqual(left["y"], right["y"])
            self.assertLessEqual(left["x"] + left["width"], right["x"])
        title = next(o for o in current if o["text"] == "Network Observer")
        if self.page.evaluate("emulator.module._emu_width() > emulator.module._emu_height()"):
            self.assertGreaterEqual(controls[0]["x"], title["x"] + title["width"])
        else:
            self.assertGreaterEqual(controls[0]["y"], title["y"] + title["height"])

    def test_header_controls_remain_at_right_before_and_after_start(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                click_text(self.page, "Network\nObserver")
                self.assert_header_controls_at_right(False)
                click_text(self.page, "Start")
                self.assert_header_controls_at_right(True)
                self.assertEqual(self.errors, [])
                self.assertEqual(self.page.evaluate("unavailable"), [])

    def test_visibility_order_cancel_defaults_and_reload(self):
        p = self.page
        for rotation in (0, 1):
            with self.subTest(rotation=rotation):
                self.start(rotation)
                self.assertEqual(len(self.metadata()), 12)
                before = self.stored()
                click_text(p, "Options")
                p.locator("#display").focus()
                p.keyboard.press("Escape")
                self.assertNotIn("Observer Options", self.text())
                click_text(p, "Options")
                self.option("networks.0.visible")  # Security off, then WPS first.
                self.option("networks.2.up")
                self.option("networks.1.up")
                self.option("cancel")
                self.assertEqual(self.stored(), before)
                self.assertTrue(any("WPA2/WPA3" in text for text in self.metadata()))
                click_text(p, "Options")
                self.option("networks.0.visible")
                self.option("networks.2.up")
                self.option("networks.1.up")
                self.option("apply")
                saved = self.stored()
                self.assertIsNotNone(saved)
                self.assertEqual(len(self.metadata()), 12)
                self.assertTrue(all(text.startswith("WPS") for text in self.metadata()))
                self.assertFalse(any("WPA2/WPA3" in text for text in self.metadata()))
                self.assertTrue(p.evaluate('emulator.device.device.observer("grove").running'))
                click_text(p, "Options")
                self.option("defaults")
                self.option("cancel")
                self.assertEqual(self.stored(), saved)
                self.start(rotation)  # A new module instance reloads the NVS blob.
                self.assertEqual(self.stored(), saved)
                self.assertTrue(all(text.startswith("WPS") for text in self.metadata()))
                click_text(p, "Options")
                self.option("defaults")
                self.option("apply")
                self.assertNotEqual(self.stored(), saved)
                self.assertTrue(any("WPA2/WPA3" in text for text in self.metadata()))
                self.assertEqual(self.errors, [])

    def test_detail_tabs_expose_security_wps_and_clients(self):
        p = self.page
        for rotation in (0, 1):
            with self.subTest(rotation=rotation):
                self.start(rotation)
                click_object(p, self.control("/observe"))
                self.assertIn("Sim Lab", self.text())
                self.assertIn("SSID:", self.text())
                click_text(p, "Security")
                p.wait_for_timeout(250)
                self.assertIn("RSN", self.text())
                p.evaluate("emulator.module._emu_wheel(emulator.module._emu_width()/2, emulator.module._emu_height()/2, 250)")
                click_text(p, "Technical details")
                self.assertIn("ext_ver=1", self.text())
                click_text(p, "WPS")
                p.wait_for_timeout(250)
                self.assertIn("Simulated", self.text())
                self.assertIn("Push button", self.text())
                click_text(p, "Clients")
                p.wait_for_timeout(250)
                self.assertIn("Vendor disabled", self.text())
                self.assertIn("dBm", self.text())
                self.assertIn("Seen", self.text())
                self.assertEqual(self.errors, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
