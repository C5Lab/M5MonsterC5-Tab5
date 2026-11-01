"""Browser acceptance for the retained selected-BSSID Deauth Detector UI.

Run after rebuilding tools/ui_emulator/dist. The browser transport supplies
JanOS-shaped replies; selection, status, filtering, and rows run in Tab5 C.
"""
import re
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


class SelectedDeauthDetectorBrowser(unittest.TestCase):
    @staticmethod
    def contrast(first, second):
        def luminance(color):
            channels = [(color >> shift) & 0xFF for shift in (16, 8, 0)]
            linear = [value / 12.92 if value <= 0.04045 else
                      ((value + 0.055) / 1.055) ** 2.4
                      for value in (channel / 255 for channel in channels)]
            return sum(weight * value for weight, value in
                       zip((0.2126, 0.7152, 0.0722), linear))

        bright, dark = sorted((luminance(first), luminance(second)), reverse=True)
        return (bright + 0.05) / (dark + 0.05)

    @staticmethod
    def text(page):
        return "\n".join(item["text"] for item in objects(page))

    @staticmethod
    def advance(page, ticks=12):
        page.evaluate("ticks => { for (let i = 0; i < ticks; i++) emulator.module._emu_tick(100); }", ticks)

    def click_binding(self, page, needle):
        matches = [item for item in objects(page) if needle in item["binding"]]
        self.assertEqual(len(matches), 1, (needle, [item["binding"] for item in matches]))
        click_object(page, matches[0])

    def wait_scan_complete(self, page):
        page.wait_for_function("""() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(item =>
                item.text === 'Scan complete; choose BSSID and press Start');
        }""", timeout=30000)

    def wait_button_enabled(self, page, label):
        page.wait_for_function("""label => {
            emulator.module._emu_inspect();
            const caption = emulatorObjects.find(item => item.text === label);
            const button = emulatorObjects.find(item => item.id === caption?.parent);
            return button && !(button.state & 0x80);
        }""", arg=label, timeout=30000)

    def checkboxes(self, page):
        inspected = objects(page)
        first = next(item for item in inspected
                     if "deauth_detector_render_networks.checkbox" in item["binding"])
        # LVGL's inspector exposes checkbox geometry but not checkbox text.
        return sorted((item for item in inspected
                       if item["parent"] == first["parent"] and item["clickable"]),
                      key=lambda item: item["y"])

    @staticmethod
    def visible_network_rows(page):
        return [item for item in objects(page)
                if "deauth_detector_render_networks.checkbox" in item["binding"]]

    @staticmethod
    def click_checkbox(page, checkbox):
        # Click its actual indicator square, inside the retained LVGL object.
        click_object(page, {**checkbox, "width": 24})

    def count(self, page):
        match = re.search(r"Observed: (\d+) DEAUTH \| (\d+) CSA \| Latest groups: (\d+)/200",
                          self.text(page))
        self.assertIsNotNone(match)
        return int(match.group(1)) + int(match.group(2))

    def test_selected_networks_use_a_popup_and_rescan_reopens_it(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                self.assertIn("Select networks to monitor", self.text(page))
                self.wait_scan_complete(page)
                inspected = objects(page)
                title = next(item for item in inspected
                             if item["text"] == "Select networks to monitor")
                rows = self.visible_network_rows(page)
                self.assertGreater(len(rows), 0)
                network_list = next(item for item in inspected
                                    if item["id"] == rows[0]["parent"])
                self.assertEqual(network_list["parent"], title["parent"])
                use_label = next(item for item in inspected
                                 if item["text"] == "Use selection")
                use_button = next(item for item in inspected
                                  if item["id"] == use_label["parent"])
                self.assertTrue(use_button["state"] & 0x80)
                self.click_checkbox(page, self.checkboxes(page)[0])
                self.wait_button_enabled(page, "Rescan")
                click_text(page, "Rescan")
                self.wait_scan_complete(page)
                self.assertIn("BSSID: 0 selected", self.text(page))
                self.click_checkbox(page, self.checkboxes(page)[0])
                self.wait_button_enabled(page, "Use selection")
                click_text(page, "Use selection")
                self.assertNotIn("Select networks to monitor", self.text(page))
                self.assertEqual(self.visible_network_rows(page), [])
                self.assertIn("BSSID: 1 selected", self.text(page))

                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('1 selected, 1/1 WATCH confirmed'));
                }""", timeout=30000)
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.wait_button_enabled(page, "Scan again")
                click_text(page, "Scan again")
                self.assertIn("Select networks to monitor", self.text(page))
                self.wait_scan_complete(page)
                self.assertGreater(len(self.visible_network_rows(page)), 0)
                self.assertIn("BSSID: 0 selected", self.text(page))
            finally:
                page.close()

    def test_cancel_popup_discards_selected_mode(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                self.wait_scan_complete(page)
                self.click_checkbox(page, self.checkboxes(page)[0])
                click_text(page, "Cancel")
                self.assertNotIn("Select networks to monitor", self.text(page))
                self.assertEqual(self.visible_network_rows(page), [])
                self.assertIn("All networks | all channels | Format: V2 on next Start", self.text(page))
                self.wait_button_enabled(page, "\uf04b Start")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                self.assertEqual(page.evaluate("emulator.device.device.detectorStatus('grove').mode"),
                                 "all")
            finally:
                page.close()

    def test_popup_scan_keeps_its_uart_when_visible_tab_changes(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                page.evaluate("""() => {
                    globalThis.detectorWrites = [];
                    const write = emulator.device.detectorWrite.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        detectorWrites.push({tab, command});
                        return write(tab, command);
                    };
                }""")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                click_text(page, "MBUS")
                self.assertEqual(page.evaluate("emulator.module._emu_current_tab()"), 2)
                click_text(page, "GROVE")
                self.wait_scan_complete(page)
                self.assertIn("Select networks to monitor", self.text(page))
                self.click_checkbox(page, self.checkboxes(page)[0])
                self.wait_button_enabled(page, "Use selection")
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    return detectorWrites.some(item =>
                        item.tab === 0 && item.command === 'deauth_detector_v2 --only 1');
                }""", timeout=30000)
                self.assertIn({"tab": 0, "command": "scan_networks"},
                              page.evaluate("detectorWrites"))
            finally:
                page.close()

    def test_popup_status_is_readable_in_light_theme(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                page.evaluate("emulatorSettings.set('dark_mode', 0)")
                page.reload()
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                self.wait_scan_complete(page)
                inspected = objects(page)
                title = next(item for item in inspected
                             if item["text"] == "Select networks to monitor")
                popup = next(item for item in inspected
                             if item["id"] == title["parent"])
                status = next(item for item in inspected
                              if item["parent"] == popup["id"] and
                              item["text"] == "Scan complete; choose BSSID and press Start")
                self.assertGreaterEqual(self.contrast(status["fg"], popup["bg"]), 4.5)
            finally:
                page.close()

    def test_selected_network_list_collapses_on_start_and_returns_after_rescan(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                self.wait_scan_complete(page)
                first_scan_count = len(self.visible_network_rows(page))
                self.assertGreater(first_scan_count, 0)
                self.assertIn("Select networks to monitor", self.text(page))
                self.click_checkbox(page, self.checkboxes(page)[0])
                click_text(page, "Use selection")
                self.assertNotIn("Select networks to monitor", self.text(page))
                self.assertEqual(self.visible_network_rows(page), [])

                self.click_binding(page, "deauth_start_btn")
                self.assertEqual(self.visible_network_rows(page), [])
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('1 selected, 1/1 WATCH confirmed'));
                }""", timeout=30000)
                self.assertEqual(self.visible_network_rows(page), [])

                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertIn("BSSID: 0 selected, 0/0 WATCH confirmed", self.text(page))
                self.assertEqual(self.visible_network_rows(page), [])

                self.wait_button_enabled(page, "Scan again")
                click_text(page, "Scan again")
                self.assertIn("Select networks to monitor", self.text(page))
                self.wait_scan_complete(page)
                self.assertEqual(len(self.visible_network_rows(page)), first_scan_count)
                self.assertIn("BSSID: 0 selected", self.text(page))
            finally:
                page.close()

    def test_header_mode_controls_reflow_without_overlapping_actions(self):
        with browser_session() as (browser, url):
            for rotation, width in ((1, 1280), (0, 720)):
                page = browser.new_page(viewport={"width": 1440, "height": 1000})
                try:
                    page.goto(f"{url}/?rotation={rotation}")
                    page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                    click_text(page, "Deauth\nDetector")

                    def item(text):
                        return next(obj for obj in objects(page) if obj["text"] == text)

                    title = item("Deauth Detector")
                    all_networks = item("All networks")
                    selected_networks = item("Selected networks")
                    start = next(obj for obj in objects(page)
                                 if "deauth_start_btn" in obj["binding"])
                    stop = next(obj for obj in objects(page)
                                if "deauth_stop_btn" in obj["binding"])
                    status = item("Stopped")
                    self.assertLess(all_networks["y"], status["y"])
                    self.assertLess(selected_networks["y"], status["y"])
                    self.assertLess(selected_networks["x"] + selected_networks["width"],
                                    start["x"])
                    self.assertLessEqual(stop["x"] + stop["width"], width)
                    if rotation == 1:
                        self.assertGreaterEqual(all_networks["x"],
                                                title["x"] + title["width"])
                        self.assertLess(abs(all_networks["y"] - title["y"]), 30)
                    else:
                        self.assertGreaterEqual(all_networks["y"],
                                                title["y"] + title["height"])

                    click_text(page, "Selected networks")
                    scan_again = item("Scan again")
                    self.assertLessEqual(scan_again["x"] + scan_again["width"], width)
                    if rotation == 1:
                        self.assertLess(abs(scan_again["y"] - all_networks["y"]), 12)
                    else:
                        self.assertGreaterEqual(scan_again["y"],
                                                selected_networks["y"] + selected_networks["height"])
                    popup_title = item("Select networks to monitor")
                    popup = next(obj for obj in objects(page)
                                 if obj["id"] == popup_title["parent"])
                    self.assertGreaterEqual(popup["x"], 0)
                    self.assertGreaterEqual(popup["y"], 0)
                    self.assertLessEqual(popup["x"] + popup["width"], width)
                    self.assertLessEqual(popup["y"] + popup["height"],
                                         720 if rotation == 1 else 1280)
                finally:
                    page.close()

    def test_fresh_scan_multiselect_watch_filter_tab_binding_and_stop(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                page.evaluate("""() => {
                    globalThis.detectorWrites = [];
                    const write = emulator.device.detectorWrite.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        detectorWrites.push({tab, command});
                        return write(tab, command);
                    };
                }""")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                self.wait_scan_complete(page)
                networks = page.evaluate("emulator.device.snapshot(0).networks")
                self.assertGreaterEqual(len(networks), 3)
                rows = self.checkboxes(page)
                self.assertEqual(len(rows), len(networks))
                inspected = objects(page)
                list_bg = next(item for item in inspected
                               if item["id"] == rows[0]["parent"])["bg"]
                self.assertGreaterEqual(self.contrast(rows[0]["fg"], list_bg), 4.5)
                self.assertIn({"tab": 0, "command": "scan_networks"},
                              page.evaluate("detectorWrites"))

                self.click_checkbox(page, rows[0])
                self.assertIn("BSSID: 1 selected, 0/1 WATCH confirmed", self.text(page))
                self.click_checkbox(page, rows[1])
                self.assertIn("BSSID: 2 selected, 0/2 WATCH confirmed", self.text(page))
                bound_rows = [item for item in self.checkboxes(page) if item["binding"]]
                self.assertEqual(len(bound_rows), len(networks))
                self.assertEqual(len({item["binding"] for item in bound_rows}), len(networks))

                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('2 selected, 2/2 WATCH confirmed'));
                }""", timeout=30000)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                self.assertIn("Detector running", self.text(page))
                self.assertIn({"tab": 0, "command": "deauth_detector_v2 --only 1 2"},
                              page.evaluate("detectorWrites"))
                selected = [network["bssid"].upper() for network in networks[:2]]
                state = page.evaluate("emulator.device.device.detectorStatus('grove')")
                self.assertEqual(state["mode"], "only")
                self.assertEqual(state["watchBssids"], selected)

                # Feed an unselected JanOS observation through the native parser.
                page.evaluate("""() => {
                    globalThis.detectorLineTabs = [];
                    emulator.device.deauthDetectorLine = (tab) => {
                        detectorLineTabs.push(tab);
                        return '[DEAUTH_V2] CH: 6 | BSSID: 02:20:77:00:00:03 | TX: AA:BB:CC:DD:EE:03 | DA: FF:FF:FF:FF:FF:FF | RSSI: -51 | REASON: 7 | SSID: UNSELECTED-POISON';
                    };
                }""")
                # Drain any selected observation queued before the mock replaced the feed.
                self.advance(page)
                before = self.count(page)
                self.advance(page)
                self.assertEqual(self.count(page), before)
                self.assertNotIn("UNSELECTED-POISON", self.text(page))

                # The captured Grove session keeps polling after MBUS becomes visible.
                click_text(page, "MBUS")
                self.assertEqual(page.evaluate("emulator.module._emu_current_tab()"), 2)
                self.assertFalse(page.evaluate("emulator.device.device.detectorStatus('mbus').running"))
                page.evaluate("""() => {
                    emulator.device.deauthDetectorLine = (tab) => {
                        detectorLineTabs.push(tab);
                        return '[DEAUTH_V2] CH: 1 | BSSID: 02:20:77:00:00:01 | TX: AA:BB:CC:DD:EE:01 | DA: FF:FF:FF:FF:FF:FF | RSSI: -42 | REASON: 7 | SSID: NEON-BAZAAR';
                    };
                }""")
                self.advance(page)
                self.assertTrue(page.evaluate("detectorLineTabs.length > 0"))
                self.assertEqual(set(page.evaluate("detectorLineTabs")), {0})
                click_text(page, "GROVE")
                self.assertGreater(self.count(page), before)
                self.assertIn("NEON-BAZAAR", self.text(page))
                self.assertNotIn("UNSELECTED-POISON", self.text(page))
                self.assertIn("2 selected, 2/2 WATCH confirmed", self.text(page))

                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertIn({"tab": 0, "command": "stop"}, page.evaluate("detectorWrites"))
                self.assertFalse(page.evaluate("emulator.device.device.detectorStatus('grove').running"))
                frozen = self.count(page)
                self.assertGreater(frozen, 0)
                self.advance(page)
                self.assertEqual(self.count(page), frozen)
                page.evaluate("emulator.device.deauthDetectorLine = () => null")
                click_text(page, "All networks")
                page.evaluate("""() => {
                    globalThis.originalDetectorWrite = emulator.device.detectorWrite;
                    globalThis.originalDetectorRead = emulator.device.detectorRead;
                    globalThis.rejectNextStart = false;
                    emulator.device.detectorWrite = (tab, command) => {
                        if (command === 'deauth_detector_v2') {
                            rejectNextStart = true;
                            return true;
                        }
                        return originalDetectorWrite(tab, command);
                    };
                    emulator.device.detectorRead = (tab) => {
                        if (rejectNextStart) {
                            rejectNextStart = false;
                            return 'Detector not started';
                        }
                        return originalDetectorRead(tab);
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector not started');
                }""", timeout=30000)
                self.assertEqual(self.count(page), frozen)
                page.evaluate("""() => {
                    emulator.device.detectorWrite = originalDetectorWrite;
                    emulator.device.detectorRead = originalDetectorRead;
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                self.assertEqual(self.count(page), 0)
                self.assertEqual(errors, [])
            finally:
                page.close()

    def test_back_waits_for_stop_ack_and_stop_can_retry(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                page.evaluate("""() => {
                    globalThis.detectorWrites = [];
                    const write = emulator.device.detectorWrite.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        detectorWrites.push({tab, command});
                        return write(tab, command);
                    };
                }""")
                click_text(page, "Deauth\nDetector")
                click_text(page, "Selected networks")
                self.wait_scan_complete(page)
                self.click_checkbox(page, self.checkboxes(page)[0])
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('1 selected, 1/1 WATCH confirmed'));
                }""", timeout=30000)

                # Hold both partial and exact-final Stop replies at the UART boundary.
                page.evaluate("""() => {
                    globalThis.heldStopReplies = [];
                    globalThis.holdStopReply = true;
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorRead = (tab) => {
                        if (!holdStopReply && heldStopReplies.length)
                            return heldStopReplies.shift();
                        const line = read(tab);
                        if (tab === 0 && holdStopReply &&
                            (line === 'Deauth Detector stopped.' ||
                             line === 'All operations stopped.')) {
                            heldStopReplies.push(line);
                            return null;
                        }
                        return line;
                    };
                }""")
                self.click_binding(page, "show_deauth_detector_page.back_btn")
                self.assertIn("Deauth Detector", self.text(page))
                self.assertIn("Stopping... waiting for JanOS confirmation", self.text(page))
                self.advance(page, 8)
                self.assertEqual(page.evaluate("heldStopReplies.length"), 2)
                self.assertIn("No stop confirmation from JanOS", self.text(page))
                self.assertIn("Deauth Detector", self.text(page))
                start = next(item for item in objects(page)
                             if "deauth_start_btn" in item["binding"])
                stop = next(item for item in objects(page)
                            if "deauth_stop_btn" in item["binding"])
                self.assertTrue(start["state"] & 0x80, "Start must remain disabled while remote state is unknown")
                self.assertFalse(stop["state"] & 0x80, "Stop must allow retry")

                page.evaluate("holdStopReply = false")
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertEqual(page.evaluate("detectorWrites.filter(item => item.command === 'stop')"),
                                 [{"tab": 0, "command": "stop"}, {"tab": 0, "command": "stop"}])
                self.click_binding(page, "show_deauth_detector_page.back_btn")
                self.assertIn("Deauth\nDetector", self.text(page))
            finally:
                page.close()

    def test_disconnected_selected_scan_reports_error_without_starting(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                click_text(page, "Deauth\nDetector")
                page.evaluate("emulator.device.device.setModule('grove', {connected: false})")
                click_text(page, "Selected networks")
                self.advance(page, 3)
                self.assertIn("Detector UART disconnected; scan selection cleared", self.text(page))
                self.assertNotIn("Scan complete; choose BSSID", self.text(page))
                self.assertFalse(page.evaluate("emulator.device.device.detectorStatus('grove').running"))
            finally:
                page.close()

    def test_pending_csa_remains_visible_after_uart_disconnect(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
                click_text(page, "Deauth\nDetector")
                page.evaluate("""() => {
                    globalThis.emitCsa = false;
                    globalThis.csaSent = false;
                    emulator.device.deauthDetectorLine = () => {
                        if (!emitCsa || csaSent) return '';
                        csaSent = true;
                        return '[CSA] RX: 6 | TO: 11 | BSSID: 02:20:77:00:00:01 | TX: 02:20:77:00:00:01 | RSSI: -42 | VIA: beacon | COUNT: 0 | MODE: 1 | CLASS: 81 | SSID: Lab (east)';
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                result = page.evaluate("""() => {
                    emitCsa = true;
                    for (let i = 0; i < 30; i++) {
                        emulator.module._emu_tick(100);
                        emulator.module._emu_queue2_system_state();
                        if (emulatorQueue2SystemEvidence.detectorCount > 0) {
                            const count = emulatorQueue2SystemEvidence.detectorCount;
                            emulator.module._emu_inspect();
                            const shownBefore = emulatorObjects.find(item =>
                                item.text.startsWith('Observed:'))?.text;
                            emulator.device.device.setModule('grove', {connected: false});
                            emulator.module._emu_tick(100);
                            return {count, shownBefore};
                        }
                    }
                    return {count: -1};
                }""")
                self.assertEqual(result["count"], 1)
                self.assertEqual(result["shownBefore"],
                                 "Observed: 0 DEAUTH | 0 CSA | Latest groups: 0/200")
                self.assertIn("Detector UART disconnected; scan selection cleared", self.text(page))
                self.assertEqual(self.count(page), 1)
            finally:
                page.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
