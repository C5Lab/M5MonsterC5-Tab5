"""Browser acceptance for the JanOS Deauth Detector detail wire format.

The tests drive the retained Tab5 C/LVGL page through the emulator UART. The
module replies are synthetic so the observations exercise the production
parser and UI without requiring a radio or firmware build.
"""
import csv
import io
import os
from pathlib import Path
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


V2_EVENT = (
    "[DEAUTH_V2] CH: 1 | BSSID: 02:20:77:00:00:01 | "
    "TX: AA:BB:CC:DD:EE:FF | DA: FF:FF:FF:FF:FF:FF | "
    "RSSI: -42 | REASON: 7 | SSID: Lab (east)"
)

FORENSIC_ROWS = (
    "[DEAUTH_FORENSIC/1] FRAME=deauth CH=1 BSSID=02:20:77:00:00:01 "
    "TX=AA:BB:CC:DD:EE:01 DA=12:34:56:78:9A:01 RSSI=-80 REASON=6 "
    "SEQ=10 RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1000",
    "[DEAUTH_FORENSIC/1] FRAME=deauth CH=1 BSSID=02:20:77:00:00:01 "
    "TX=AA:BB:CC:DD:EE:01 DA=12:34:56:78:9A:01 RSSI=-82 REASON=6 "
    "SEQ=10 RETRY=1 PROTECTED=0 FC=0x00C0 TS_US=2000",
    "[DEAUTH_FORENSIC/1] FRAME=disassoc CH=6 BSSID=02:20:77:00:00:02 "
    "TX=AA:BB:CC:DD:EE:02 DA=12:34:56:78:9A:02 RSSI=-87 REASON=7 "
    "SEQ=- RETRY=- PROTECTED=- FC=- TS_US=20000002",
)


def long_forensic_rows():
    sizes = (2, 2, 3, 2, 3, 1, 1, 3, 3, 2, 1)
    timestamp = 0
    sequence = 0
    rows = []
    for burst, size in enumerate(sizes):
        if burst:
            timestamp += 20_000_001
        for _ in range(size):
            tx = f"AA:BB:CC:DD:EE:{burst:02X}"
            rows.append(
                "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 "
                "BSSID=00:11:22:33:44:55 "
                f"TX={tx} DA=12:34:56:78:9A:BC "
                f"RSSI={-80 - sequence % 8} REASON=6 SEQ={sequence % 4096} "
                f"RETRY={1 if sequence % 5 == 0 else 0} PROTECTED=1 "
                f"FC=0x00C0 TS_US={timestamp}"
            )
            timestamp += 1
            sequence += 1
    return rows


def rollover_forensic_rows():
    rows = []
    for index in range(128):
        bssid = f"02:30:00:00:{index >> 8:02X}:{index & 0xFF:02X}"
        recipient = f"12:40:00:00:{index >> 8:02X}:{index & 0xFF:02X}"
        rows.append(
            "[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 "
            f"BSSID={bssid} TX=AA:BB:CC:DD:EE:01 DA={recipient} "
            f"RSSI=-80 REASON=7 SEQ={index} RETRY=0 PROTECTED=0 "
            f"FC=0x00C0 TS_US={100_000_000 + index}"
        )
    return rows


class DeauthV2Browser(unittest.TestCase):
    @staticmethod
    def labels(page):
        return [item["text"] for item in objects(page) if item["text"]]

    @staticmethod
    def advance(page, ticks=15):
        page.evaluate("ticks => { for (let i = 0; i < ticks; i++) "
                      "emulator.module._emu_tick(100); }", ticks)

    def click_binding(self, page, needle):
        matches = [item for item in objects(page) if needle in item["binding"]]
        self.assertEqual(len(matches), 1, needle)
        click_object(page, matches[0])

    def open_detector(self, page):
        page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
        page.evaluate("""() => {
            globalThis.detectorWrites = [];
            const write = emulator.device.detectorWrite.bind(emulator.device);
            emulator.device.detectorWrite = (tab, command) => {
                detectorWrites.push({tab, command});
                return write(tab, command);
            };
            globalThis.v2Wire = [];
            emulator.device.deauthDetectorLine = () => v2Wire.shift() || '';
        }""")
        click_text(page, "Deauth\nDetector")

    def open_detector_with_model_trace(self, page):
        page.wait_for_function("globalThis.emulator?.measurements.frames > 2")
        page.evaluate("""() => {
            globalThis.detectorWrites = [];
            globalThis.traceWire = [];
            const write = emulator.device.detectorWrite.bind(emulator.device);
            const read = emulator.device.detectorRead.bind(emulator.device);
            const event = emulator.device.deauthDetectorLine.bind(emulator.device);
            emulator.device.detectorWrite = (tab, command) => {
                detectorWrites.push({tab, command});
                return write(tab, command);
            };
            emulator.device.detectorRead = tab => {
                const line = read(tab);
                if (line) traceWire.push({line, now: emulator.device.snapshot(tab).now});
                return line;
            };
            emulator.device.deauthDetectorLine = (tab, sequence) => {
                const line = event(tab, sequence);
                if (line) traceWire.push({line, now: emulator.device.snapshot(tab).now});
                return line;
            };
        }""")
        click_text(page, "Deauth\nDetector")

    def test_v2_identity_and_reason_are_visible_in_both_orientations(self):
        with browser_session() as (browser, url):
            for rotation, display_size in ((1, (1280, 720)), (0, (720, 1280))):
                with self.subTest(rotation=rotation):
                    page = browser.new_page(viewport={"width": 1440, "height": 1400})
                    try:
                        page.goto(f"{url}/?rotation={rotation}")
                        self.open_detector(page)
                        self.click_binding(page, "deauth_start_btn")
                        page.wait_for_function("detectorWrites.length > 0", timeout=5000)
                        self.assertIn({"tab": 0, "command": "deauth_detector_v2"},
                                      page.evaluate("detectorWrites"))
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text === 'Detector running');
                        }""", timeout=30000)
                        self.assertRegex("\n".join(self.labels(page)), r"(?i)\bV2\b")
                        page.evaluate("line => v2Wire.push(line)", V2_EVENT)
                        page.wait_for_function("""() => {
                            for (let i = 0; i < 15; i++) emulator.module._emu_tick(100);
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text.includes('Claimed TX: AA:BB:CC:DD:EE:FF'));
                        }""", timeout=30000)
                        labels = self.labels(page)
                        shown = "\n".join(labels)
                        self.assertIn("Lab (east)", labels)
                        self.assertIn("Claimed TX: AA:BB:CC:DD:EE:FF", shown)
                        self.assertIn("Frame recipient", shown)
                        self.assertIn("FF:FF:FF:FF:FF:FF", shown)
                        self.assertRegex(shown, r"\bReason:\s*7\b")
                        self.assertIn("Observed: 1 DEAUTH | 0 CSA | Latest groups: 1/200", shown)
                        for item in objects(page):
                            if "Claimed TX:" not in item["text"]:
                                continue
                            self.assertGreaterEqual(item["x"], 0)
                            self.assertGreaterEqual(item["y"], 0)
                            self.assertLessEqual(item["x"] + item["width"], display_size[0])
                            self.assertLessEqual(item["y"] + item["height"], display_size[1])
                        if screenshot_dir := os.environ.get("DEAUTH_V2_SCREENSHOT_DIR"):
                            Path(screenshot_dir).mkdir(parents=True, exist_ok=True)
                            page.locator("#display").screenshot(
                                path=str(Path(screenshot_dir) / f"deauth-v2-rotation-{rotation}.png"))
                    finally:
                        page.close()

    def test_selected_details_and_stop_stay_on_the_original_uart(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                inspected = objects(page)
                first = next(item for item in inspected
                             if "deauth_detector_render_networks.checkbox" in item["binding"])
                networks = sorted((item for item in inspected
                                   if item["parent"] == first["parent"] and item["clickable"]),
                                  key=lambda item: item["y"])
                self.assertGreaterEqual(len(networks), 2)
                expected_bssids = [next(csv.reader([row]))[3].upper() for row in
                                   page.evaluate("emulator.device.device.scanCsvRows('grove')")[:2]]
                for network in networks[:2]:
                    click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.command.startsWith('deauth_detector_v2 --only'))
                """, timeout=5000)
                self.assertIn({"tab": 0, "command": "deauth_detector_v2 --only 1 2"},
                              page.evaluate("detectorWrites"))
                click_text(page, "MBUS")
                self.assertEqual(page.evaluate("emulator.module._emu_current_tab()"), 2)
                self.advance(page, 20)
                click_text(page, "GROVE")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('2 selected, 2/2 WATCH confirmed'));
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertRegex(shown, r"(?i)\bV2\b")

                writes_before = page.evaluate("detectorWrites.slice()")
                click_text(page, "Selected networks")
                popup_labels = self.labels(page)
                self.assertIn("Watched networks", popup_labels)
                self.assertIn("Close", popup_labels)
                self.assertNotIn("Rescan", popup_labels)
                self.assertNotIn("Use selection", popup_labels)
                inspected = objects(page)
                first = next(item for item in inspected
                             if "deauth_detector_render_networks.checkbox" in item["binding"])
                rows = [item for item in inspected
                        if item["parent"] == first["parent"] and item["clickable"]]
                selected_rows = [item for item in rows if item["state"] & 0x01]
                self.assertEqual(len(selected_rows), 2)
                for row in selected_rows:
                    self.assertTrue(row["state"] & 0x80, "Running watch is read-only")
                    self.assertIn("WATCH confirmed", row["text"])
                for bssid in expected_bssids:
                    self.assertTrue(any(bssid in row["text"] for row in selected_rows),
                                    f"Watched popup must show selected BSSID {bssid}")
                self.assertEqual(page.evaluate("detectorWrites.slice()"), writes_before)

                click_text(page, "Close")
                self.assertNotIn("Watched networks", self.labels(page))
                self.assertIn("Detector running", self.labels(page))
                self.assertEqual(page.evaluate("detectorWrites.slice()"), writes_before)
                shown = "\n".join(self.labels(page))
                for bssid in expected_bssids:
                    self.assertIn(bssid, shown,
                                  "Each selected BSSID must remain visible after the picker closes")
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command === 'stop')
                """, timeout=5000)
                self.assertFalse(any(item["tab"] == 2 for item in page.evaluate("detectorWrites")))
            finally:
                page.close()

    def test_explicit_old_janos_rejection_retries_legacy_once(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("""() => {
                    let pendingRejection = false;
                    const write = emulator.device.detectorWrite.bind(emulator.device);
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        if (tab === 0 && command === 'deauth_detector_v2') {
                            detectorWrites.push({tab, command});
                            pendingRejection = true;
                            return true;
                        }
                        return write(tab, command);
                    };
                    emulator.device.detectorRead = (tab) => {
                        if (tab === 0 && pendingRejection) {
                            pendingRejection = false;
                            return 'Unrecognized command';
                        }
                        return read(tab);
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command === 'deauth_detector')
                """, timeout=30000)
                self.assertEqual([item["command"] for item in page.evaluate("detectorWrites")],
                                 ["deauth_detector_v2", "deauth_detector"])
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Detector running');
                }""", timeout=30000)
                self.assertRegex("\n".join(self.labels(page)), r"(?i)\bLegacy\b")
                page.evaluate("line => v2Wire.push(line)",
                              "[DEAUTH] CH: 1 | AP: Lab (east) (02:20:77:00:00:01) | RSSI: -42")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 15; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Lab (east)');
                }""", timeout=30000)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA | Latest groups: 1/200",
                              "\n".join(self.labels(page)))
            finally:
                page.close()

    def test_selected_fallback_keeps_only_and_bound_uart_across_tab_switch(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                page.evaluate("""() => {
                    globalThis.releaseOptionRejection = false;
                    let pendingRejection = false;
                    const write = emulator.device.detectorWrite.bind(emulator.device);
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        if (tab === 0 && command === 'deauth_detector_v2 --only 1') {
                            detectorWrites.push({tab, command});
                            pendingRejection = true;
                            return true;
                        }
                        return write(tab, command);
                    };
                    emulator.device.detectorRead = (tab) => {
                        if (tab === 0 && pendingRejection) {
                            if (!releaseOptionRejection) return null;
                            pendingRejection = false;
                            return 'Unrecognized command';
                        }
                        return read(tab);
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command.startsWith('deauth_detector_v2'))
                """, timeout=5000)
                self.assertIn({"tab": 0, "command": "deauth_detector_v2 --only 1"},
                              page.evaluate("detectorWrites"))
                click_text(page, "MBUS")
                page.evaluate("releaseOptionRejection = true")
                self.advance(page, 20)
                writes = page.evaluate("detectorWrites")
                self.assertIn({"tab": 0, "command": "deauth_detector --only 1"}, writes)
                self.assertNotIn({"tab": 0, "command": "deauth_detector 1"}, writes)
                self.assertFalse(any(item["tab"] == 2 for item in writes))
                click_text(page, "GROVE")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('1 selected, 1/1 WATCH confirmed'));
                }""", timeout=30000)
                self.assertRegex("\n".join(self.labels(page)), r"(?i)\bLegacy\b")
            finally:
                page.close()

    def test_no_reply_does_not_retry_legacy(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("""() => {
                    const write = emulator.device.detectorWrite.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        if (tab === 0 && command === 'deauth_detector_v2') {
                            detectorWrites.push({tab, command});
                            return true; // UART write succeeds; JanOS remains silent.
                        }
                        return write(tab, command);
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                self.advance(page, 310)
                commands = [item["command"] for item in page.evaluate("detectorWrites")]
                self.assertEqual(commands.count("deauth_detector_v2"), 1)
                self.assertNotIn("deauth_detector", commands)
                shown = "\n".join(self.labels(page))
                self.assertIn("remote state unknown", shown)
                self.assertNotIn("Detector running", shown)
            finally:
                page.close()

    def test_unconfirmed_start_and_stop_keep_selection_locked_until_bound_stop_ack(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")

                # JanOS accepts writes but its Start and first Stop replies never
                # reach Tab5. Preserve Stop ACK for an explicit bound retry.
                page.evaluate("""() => {
                    globalThis.holdDetectorReplies = true;
                    globalThis.heldStopReplies = [];
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorRead = (tab) => {
                        if (tab !== 0) return read(tab);
                        if (!holdDetectorReplies && heldStopReplies.length)
                            return heldStopReplies.shift();
                        const line = read(tab);
                        if (holdDetectorReplies) {
                            if (line?.includes('Deauth Detector stopped.'))
                                heldStopReplies.push(line);
                            return null;
                        }
                        return line;
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 &&
                    item.command === 'deauth_detector_v2 --only 1')
                """, timeout=5000)
                self.advance(page, 380)
                shown = "\n".join(self.labels(page))
                self.assertIn("remote state unknown", shown)
                self.assertNotIn("Detector running", shown)
                self.assertEqual(page.evaluate("heldStopReplies.length"), 1)

                inspected = objects(page)
                def button_for(label):
                    if label in ("Start", "Stop"):
                        binding = "deauth_start_btn" if label == "Start" else "deauth_stop_btn"
                        return next(item for item in inspected if binding in item["binding"])
                    caption = next(item for item in inspected if item["text"] == label)
                    return next(item for item in inspected if item["id"] == caption["parent"])

                buttons = {label: button_for(label) for label in
                           ("Start", "Stop", "All networks", "Selected networks", "Scan again")}
                disabled = {label: bool(button["state"] & 0x80)
                            for label, button in buttons.items()}

                writes_before = page.evaluate("detectorWrites.slice()")
                click_object(page, buttons["Selected networks"])
                self.advance(page, 5)
                writes_after = page.evaluate("detectorWrites.slice()")
                self.assertEqual(disabled, {
                    "Start": True, "Stop": False, "All networks": True,
                    "Selected networks": True, "Scan again": True,
                })
                self.assertEqual(writes_after, writes_before,
                                 "Unknown JanOS state must not launch another scan")

                click_text(page, "MBUS")
                self.assertEqual(page.evaluate("emulator.module._emu_current_tab()"), 2)
                click_text(page, "GROVE")
                self.assertFalse(any(item["tab"] == 2 for item in page.evaluate("detectorWrites")))

                page.evaluate("holdDetectorReplies = false")
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertEqual([item for item in page.evaluate("detectorWrites")
                                  if item["command"] == "stop"],
                                 [{"tab": 0, "command": "stop"},
                                  {"tab": 0, "command": "stop"}])
                inspected = objects(page)
                start = next(item for item in inspected
                             if "deauth_start_btn" in item["binding"])
                stop = next(item for item in inspected
                            if "deauth_stop_btn" in item["binding"])
                all_button = next(item for item in inspected
                                  if item["text"] == "All networks")
                all_button = next(item for item in inspected
                                  if item["id"] == all_button["parent"])
                self.assertTrue(start["state"] & 0x80,
                                "A stopped selected watch requires a fresh scan")
                self.assertTrue(stop["state"] & 0x80)
                self.assertFalse(all_button["state"] & 0x80,
                                 "Confirmed Stop must release the uncertain-state lock")
            finally:
                page.close()

    def test_v2_groups_by_declared_addresses_and_exports_them_to_tab5_sd(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("detectorWrites.length > 0", timeout=5000)
                self.assertIn({"tab": 0, "command": "deauth_detector_v2"},
                              page.evaluate("detectorWrites"))
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Detector running');
                }""", timeout=30000)
                other_tx = V2_EVENT.replace("AA:BB:CC:DD:EE:FF", "02:11:22:33:44:55")
                for line in (V2_EVENT, V2_EVENT, other_tx):
                    page.evaluate("line => v2Wire.push(line)", line)
                    self.advance(page, 15)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Observed: 3 DEAUTH | 0 CSA | Latest groups: 2/200');
                }""", timeout=30000)
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                click_text(page, "Save CSV")
                page.wait_for_function("""() => {
                    emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.startsWith('Saved snapshot of 2 groups to Tab5 SD: '));
                }""", timeout=30000)
                saved = page.evaluate("""() => {
                    const fs = emulator.module.FS;
                    const dir = '/sdcard/lab/detector';
                    const name = fs.readdir(dir).find(name => name.endsWith('.csv'));
                    return new TextDecoder().decode(fs.readFile(`${dir}/${name}`));
                }""")
                rows = list(csv.DictReader(io.StringIO(saved)))
                self.assertEqual(len(rows), 2, rows)
                by_tx = {row["claimed_tx"]: row for row in rows}
                self.assertEqual(set(by_tx),
                                 {"AA:BB:CC:DD:EE:FF", "02:11:22:33:44:55"})
                self.assertEqual(by_tx["AA:BB:CC:DD:EE:FF"]["observations"], "2")
                self.assertEqual(by_tx["02:11:22:33:44:55"]["observations"], "1")
                for row in rows:
                    self.assertEqual(row["type"], "DEAUTH")
                    self.assertEqual(row["frame_da"], "FF:FF:FF:FF:FF:FF")
                    self.assertEqual(row["reason_code"], "7")
                    self.assertEqual(row["reason_status"], "known")
            finally:
                page.close()

    def test_two_uart_sessions_keep_v2_events_and_stop_separate(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("""() => {
                    globalThis.v2ByTab = {0: [], 2: []};
                    emulator.device.deauthDetectorLine = (tab) =>
                        v2ByTab[tab]?.shift() || '';
                }""")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("detectorWrites.length > 0", timeout=5000)
                self.assertIn({"tab": 0, "command": "deauth_detector_v2"},
                              page.evaluate("detectorWrites"))
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)

                click_text(page, "MBUS")
                click_text(page, "Deauth\nDetector")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 2 && item.command === 'deauth_detector_v2')
                """, timeout=5000)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)

                mbus_event = (V2_EVENT.replace("02:20:77:00:00:01", "02:20:77:00:00:02")
                                   .replace("Lab (east)", "MBUS-Lab"))
                page.evaluate("""([grove, mbus]) => {
                    v2ByTab[0].push(grove);
                    v2ByTab[2].push(mbus);
                }""", [V2_EVENT, mbus_event])
                self.advance(page, 25)
                self.assertIn("MBUS-Lab", self.labels(page))
                self.assertNotIn("Lab (east)", self.labels(page))

                click_text(page, "GROVE")
                self.assertIn("Lab (east)", self.labels(page))
                self.assertNotIn("MBUS-Lab", self.labels(page))
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertTrue(page.evaluate(
                    "emulator.device.device.detectorStatus('mbus').running"))
                click_text(page, "MBUS")
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertEqual([item for item in page.evaluate("detectorWrites")
                                  if item["command"] == "stop"],
                                 [{"tab": 0, "command": "stop"},
                                  {"tab": 2, "command": "stop"}])
            finally:
                page.close()

    def test_channel_trace_marker_failure_lock_and_unknown_reason_are_status_only(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("globalThis.emulatorAlertChimes = []")
                page.evaluate("""() => {
                    globalThis.v2StatusWire = [];
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorRead = tab => v2StatusWire.shift() || read(tab);
                }""")
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                click_object(page, trace)
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command === 'deauth_detector_v2 trace')
                """, timeout=5000)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running') &&
                           emulatorObjects.some(item =>
                               item.text.includes('Trace: armed; waiting for first eligible DEAUTH'));
                }""", timeout=30000)
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                self.assertTrue(trace["state"] & 0x80, "Trace must freeze while active")

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] confirming_bssid=02:20:77:00:00:01 "
                              "first_channel=1 window_ms=999")
                self.advance(page, 12)
                self.assertIn("Trace: armed; waiting for first eligible DEAUTH",
                              "\n".join(self.labels(page)))

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] confirming_bssid=02:20:77:00:00:01 "
                              "first_channel=1 window_ms=1000")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text.includes(
                        'Trace: confirming 02:20:77:00:00:01 from CH 1 (1000 ms)'));
                }""", timeout=30000)
                self.assertIn("Observed: 0 DEAUTH | 0 CSA", "\n".join(self.labels(page)))
                self.assertEqual(page.evaluate("emulatorAlertChimes.length"), 0)

                unknown = V2_EVENT.replace("REASON: 7", "REASON: -") \
                                  .replace("FF:FF:FF:FF:FF:FF", "11:22:33:44:55:66")
                page.evaluate("line => v2Wire.push(line)", unknown)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 15; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Reason: unknown'));
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertIn("Observed: 1 DEAUTH | 0 CSA", shown)
                self.assertIn("Trace: confirming 02:20:77:00:00:01", shown)
                self.assertEqual(page.evaluate("emulatorAlertChimes.length"), 1)

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] lock_failed_channel=1 error=ESP_FAIL")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('tune failed on channel 1'));
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertIn("armed for retry", shown)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA", shown)

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] confirming_bssid=02:20:77:00:00:01 "
                              "first_channel=6 window_ms=1000")
                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] locked_channel=6")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Trace lock: channel 6'));
                }""", timeout=30000)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA", "\n".join(self.labels(page)))

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] released_channel=6 "
                              "reason=idle idle_ms=20000")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('released CH 6 after 20000 ms idle'));
                }""", timeout=30000)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA", "\n".join(self.labels(page)))

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] armed")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text.includes(
                        'armed after idle release of CH 6 (20000 ms)'));
                }""", timeout=30000)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA", "\n".join(self.labels(page)))

                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] confirming_bssid=02:20:77:00:00:01 "
                              "first_channel=2 window_ms=1000")
                page.evaluate("line => v2StatusWire.push(line)",
                              "[DETECTOR_TRACE] locked_channel=2")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Trace lock: channel 2'));
                }""", timeout=30000)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA", "\n".join(self.labels(page)))

                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped') &&
                           emulatorObjects.some(item => item.text.includes('Trace lock: off'));
                }""", timeout=30000)
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                self.assertFalse(trace["state"] & 0x80)
                self.assertTrue(trace["state"] & 0x01,
                                "Stop clears the lock, not the user's default-off control choice")
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
                    const name = fs.readdir(dir).find(name => name.endsWith('.csv'));
                    return new TextDecoder().decode(fs.readFile(`${dir}/${name}`));
                }""")
                row = next(csv.DictReader(io.StringIO(saved)))
                self.assertEqual(row["reason_code"], "")
                self.assertEqual(row["reason_status"], "unknown_encrypted")
                self.assertEqual(page.evaluate("emulatorAlertChimes.length"), 1,
                                 "Trace status must never reach the alarm path")
            finally:
                page.close()

    def test_model_trace_emits_triggering_v2_before_the_deadline_lock(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector_with_model_trace(page)
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Channel trace (passive)"))
                self.click_binding(page, "deauth_start_btn")
                self.advance(page, 45)

                wire = page.evaluate("traceWire")
                lines = [item["line"] for item in wire]
                format_index = lines.index("[DETECTOR_INFO] format=deauth_v2")
                armed_index = lines.index("[DETECTOR_TRACE] armed")
                confirming_index = next(i for i, line in enumerate(lines)
                                        if line.startswith("[DETECTOR_TRACE] confirming_bssid="))
                event_index = next(i for i, line in enumerate(lines)
                                   if line.startswith("[DEAUTH_V2] "))
                locked_index = next(i for i, line in enumerate(lines)
                                    if line.startswith("[DETECTOR_TRACE] locked_channel="))
                self.assertLess(format_index, armed_index)
                self.assertLess(armed_index, confirming_index)
                self.assertLess(confirming_index, event_index)
                self.assertLess(event_index, locked_index)
                self.assertEqual(wire[event_index]["now"], wire[confirming_index]["now"])
                self.assertGreaterEqual(wire[locked_index]["now"] - wire[confirming_index]["now"],
                                        1000)
                shown = "\n".join(self.labels(page))
                self.assertIn("Trace lock: channel", shown)
                self.assertRegex(shown, r"Observed: [1-9]\d* DEAUTH")
            finally:
                page.close()

    def test_status_tokens_inside_v2_ssid_remain_observations(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                status_ssid = (V2_EVENT.rsplit("SSID: ", 1)[0] +
                               "SSID: [DETECTOR_WATCH] count=9 ap=9/9")
                page.evaluate("line => v2Wire.push(line)", status_ssid)
                self.advance(page, 15)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Observed: 1 DEAUTH | 0 CSA | Latest groups: 1/200');
                }""", timeout=30000)
                self.assertIn("Detector running", self.labels(page))
            finally:
                page.close()

    def test_selected_start_keeps_history_until_watch_proof(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                page.evaluate("line => v2Wire.push(line)", V2_EVENT)
                self.advance(page, 15)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Observed: 1 DEAUTH | 0 CSA | Latest groups: 1/200');
                }""", timeout=30000)
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)

                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                page.evaluate("""() => {
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorRead = tab => {
                        const line = read(tab);
                        return tab === 0 && line?.startsWith('[DETECTOR_WATCH] ')
                            ? null : line;
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")
                self.advance(page, 70)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('JanOS did not confirm selected BSSID watch'));
                }""", timeout=30000)
                shown = self.labels(page)
                self.assertIn("Observed: 1 DEAUTH | 0 CSA | Latest groups: 1/200", shown)
                self.assertNotIn("Detector running", shown)
                joined = "\n".join(shown)
                self.assertIn("safety stop confirmed", joined)
                self.assertNotIn("remote state unknown", joined)
            finally:
                page.close()

    def test_selected_watch_timeout_starts_after_ack_and_format(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.assertEqual(page.evaluate("emulator.module._emu_set_timing(1)"), 1)
                page.evaluate("""() => {
                    globalThis.releaseSelectedProof = false;
                    globalThis.releaseSelectedWatch = false;
                    globalThis.heldSelectedProof = [];
                    globalThis.heldSelectedWatch = [];
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorRead = tab => {
                        if (releaseSelectedProof && heldSelectedProof.length)
                            return heldSelectedProof.shift();
                        if (releaseSelectedWatch && heldSelectedWatch.length)
                            return heldSelectedWatch.shift();
                        const line = read(tab);
                        if (tab !== 0 || !line) return line;
                        if (line === '[DETECTOR_INFO] format=deauth_v2' ||
                            line.startsWith('Deauth Detector started - monitoring selected channels')) {
                            heldSelectedProof.push(line);
                            return null;
                        }
                        if (line.startsWith('[DETECTOR_WATCH] ')) {
                            heldSelectedWatch.push(line);
                            return null;
                        }
                        return line;
                    };
                }""")
                self.click_binding(page, "deauth_start_btn")

                # A valid but slow ACK/format must not consume the separate
                # five-second WATCH-verification window.
                self.advance(page, 49)
                self.assertEqual(page.evaluate("heldSelectedProof.length"), 2)
                self.assertGreaterEqual(page.evaluate("heldSelectedWatch.length"), 1)
                page.evaluate("releaseSelectedProof = true")
                self.advance(page, 3)
                self.assertEqual(page.evaluate("heldSelectedProof.length"), 0)
                shown = "\n".join(self.labels(page))
                self.assertIn("Start acknowledged; waiting for BSSID WATCH confirmation", shown)
                self.assertNotIn("JanOS did not confirm selected BSSID watch", shown)
                self.advance(page, 12)
                self.assertNotIn("JanOS did not confirm selected BSSID watch",
                                 "\n".join(self.labels(page)))

                page.evaluate("releaseSelectedWatch = true")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 5; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                self.assertIn("1 selected, 1/1 WATCH confirmed",
                              "\n".join(self.labels(page)))
            finally:
                page.close()

    def test_model_idle_release_rearms_before_the_next_confirmation(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector_with_model_trace(page)
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Channel trace (passive)"))
                self.click_binding(page, "deauth_start_btn")
                self.advance(page, 45)
                page.wait_for_function("""() => traceWire.some(item =>
                    item.line.startsWith('[DETECTOR_TRACE] locked_channel='))
                """, timeout=30000)

                # Keep accepted traffic flowing, but only for another BSSID.
                # It must not refresh the locked BSSID's idle deadline.
                page.evaluate("""() => {
                    const status = emulator.device.device.detectorStatus('grove');
                    const alternate = status.traceLockedBssid === '02:20:77:00:00:02'
                        ? '02:20:77:00:00:03' : '02:20:77:00:00:02';
                    emulator.device.device.deauthEvent = () => ({
                        type: 'deauth', bssid: alternate, channel: 1, rssi: -52,
                        ssid: 'Idle release alternate', tx: '02:11:22:33:44:55',
                        da: 'FF:FF:FF:FF:FF:FF', reason: 7
                    });
                }""")
                self.advance(page, 220)

                lines = [item["line"] for item in page.evaluate("traceWire")]
                locked_index = next(i for i, line in enumerate(lines)
                                    if line.startswith("[DETECTOR_TRACE] locked_channel="))
                release_index = next(i for i, line in enumerate(lines)
                                     if i > locked_index and
                                     line.startswith("[DETECTOR_TRACE] released_channel="))
                armed_index = next(i for i, line in enumerate(lines)
                                   if i > release_index and line == "[DETECTOR_TRACE] armed")
                confirming_index = next(i for i, line in enumerate(lines)
                                        if i > release_index and
                                        line.startswith("[DETECTOR_TRACE] confirming_bssid="))
                self.assertLess(release_index, armed_index)
                self.assertLess(armed_index, confirming_index)
            finally:
                page.close()

    def test_older_v2_trace_index_rejection_is_unsupported_without_legacy_retry(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("""() => {
                    let pendingTraceRejection = false;
                    const write = emulator.device.detectorWrite;
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        if (tab === 0 && command === 'deauth_detector_v2 trace') {
                            detectorWrites.push({tab, command});
                            pendingTraceRejection = true;
                            return true;
                        }
                        return write(tab, command);
                    };
                    emulator.device.detectorRead = (tab) => {
                        if (tab === 0 && pendingTraceRejection) {
                            pendingTraceRejection = false;
                            return "Invalid detector index 'trace' (valid: 1-48). Detector not started.";
                        }
                        return read(tab);
                    };
                }""")
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                click_object(page, trace)
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Trace unsupported'));
                }""", timeout=30000)
                commands = [item["command"] for item in page.evaluate("detectorWrites")]
                self.assertEqual(commands, ["deauth_detector_v2 trace"])
                self.assertNotIn("deauth_detector", commands)
                self.assertNotIn("Detector running", self.labels(page))
            finally:
                page.close()

    def test_selected_trace_and_missing_stop_ack_keep_bound_lock_uncertain(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                click_object(page, trace)
                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command === 'deauth_detector_v2 --only 1 trace')
                """, timeout=5000)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                page.evaluate("""() => {
                    v2Wire.push('[DETECTOR_TRACE] locked_channel=1');
                    v2Wire.push('[DEAUTH_FORENSIC/1] FRAME=deauth CH=1 '
                        + 'BSSID=02:20:77:00:00:01 TX=AA:BB:CC:DD:EE:01 '
                        + 'DA=12:34:56:78:9A:01 RSSI=-80 REASON=6 SEQ=10 '
                        + 'RETRY=0 PROTECTED=0 FC=0x00C0 TS_US=1000');
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    let injectedStopLikeEvent = false;
                    emulator.device.detectorRead = (tab) => {
                        const line = read(tab);
                        if (tab === 0 && (line === 'Deauth Detector stopped.' ||
                            line === 'All operations stopped.')) {
                            if (!injectedStopLikeEvent) {
                                injectedStopLikeEvent = true;
                                return '[DEAUTH_V2] CH: 1 | BSSID: 02:20:77:00:00:01 | '
                                    + 'TX: 02:11:22:33:44:55 | DA: FF:FF:FF:FF:FF:FF | '
                                    + 'RSSI: -50 | REASON: 7 | SSID: Deauth Detector stopped.';
                            }
                            return null;
                        }
                        return line;
                    };
                }""")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Trace lock: channel 1')) &&
                           emulatorObjects.some(item =>
                        item.text.includes('Rows: 1 retained 1'));
                }""", timeout=30000)
                self.click_binding(page, "deauth_stop_btn")
                self.advance(page, 80)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('No stop confirmation from JanOS')) &&
                           emulatorObjects.some(item =>
                        item.text.includes('channel 1 (remote state uncertain)'));
                }""", timeout=30000)
                self.assertFalse(any(item["tab"] != 0 for item in
                                     page.evaluate("detectorWrites")))
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                self.assertTrue(trace["state"] & 0x80)
                self.assertIn("Rows: 1 retained 1", "\n".join(self.labels(page)))
            finally:
                page.close()

    def test_uart_disconnect_clears_selection_but_preserves_trace_uncertainty(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                click_object(page, trace)
                click_text(page, "Selected networks")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                self.assertIn("1 selected, 1/1 WATCH confirmed",
                              "\n".join(self.labels(page)))
                page.evaluate("v2Wire.push('[DETECTOR_TRACE] locked_channel=1')")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Trace lock: channel 1'));
                }""", timeout=30000)
                page.evaluate("emulator.device.device.setModule('grove', {connected: false})")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 4; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Detector UART disconnected; scan selection cleared')) &&
                           emulatorObjects.some(item =>
                        item.text.includes('channel 1 (remote state uncertain)')) &&
                           emulatorObjects.some(item =>
                        item.text.includes('BSSID: 0 selected, 0/0 WATCH confirmed'));
                }""", timeout=30000)
                page.evaluate("emulator.device.device.setModule('grove', {connected: true})")
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 8; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped') &&
                           emulatorObjects.some(item => item.text.includes('Trace lock: off'));
                }""", timeout=30000)
                self.assertIn({"tab": 0, "command": "stop"},
                              page.evaluate("detectorWrites"))
            finally:
                page.close()

    def test_defence_requires_selection_keeps_markers_out_of_v2_counts_and_stops_globally(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1100})
            try:
                page.goto(url)
                self.open_detector(page)
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Defence"))
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertIn("Defence: enabled for next Start", shown)
                trace = next(item for item in objects(page)
                             if item["text"] == "Channel trace (passive)")
                self.assertTrue(trace["state"] & 0x01)
                self.assertTrue(trace["state"] & 0x80, trace)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in
                               item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command ===
                    'deauth_detector_v2 --only 1 trace defence')
                """, timeout=5000)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 10; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Defence: armed for 1 protected BSSID'));
                }""", timeout=30000)
                page.evaluate("""() => v2Wire.push(
                    '[DETECTOR_DEFENCE] trigger=deauth BSSID=02:20:77:00:00:01 '
                        + 'STA=AA:BB:CC:DD:EE:FF RX=1 decision=decoy_4way reason=eligible')
                """)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 4; i++) emulator.module._emu_tick(50);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('responding for 02:20:77:00:00:01'));
                }""", timeout=5000)
                page.evaluate("""() => v2Wire.push(
                    '[DETECTOR_DEFENCE] provisional_bssid=02:20:77:00:00:01 channel=1',
                    '[DETECTOR_DEFENCE] tx=decoy_4way stage=m1_m2 result=ok '
                        + 'frames=2 latency_us=900 error=none')
                """)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('decoy TX confirmed by JanOS'));
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertIn("responses: 1", shown)
                self.assertIn("Observed: 0 DEAUTH | 0 CSA | Latest groups: 0/200", shown)
                page.evaluate("line => v2Wire.push(line)", V2_EVENT)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 12; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Observed: 1 DEAUTH | 0 CSA'));
                }""", timeout=30000)
                click_text(page, "MBUS")
                self.advance(page, 4)
                click_text(page, "GROVE")
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 10; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped');
                }""", timeout=30000)
                self.assertFalse(any(item["tab"] != 0 for item in
                                     page.evaluate("detectorWrites")))
                self.assertNotIn("remote state uncertain", "\n".join(self.labels(page)))
            finally:
                page.close()

    def test_defence_release_observe_and_second_response_keep_confirmed_state(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1100})
            try:
                page.goto(url)
                self.open_detector(page)
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Defence"))
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in
                               item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 10; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Defence: armed for 1 protected BSSID'));
                }""", timeout=30000)

                page.evaluate("""() => v2Wire.push(
                    '[DETECTOR_DEFENCE] trigger=deauth BSSID=02:20:77:00:00:01 '
                        + 'STA=- RX=1 decision=decoy_4way reason=selected_wpa2',
                    '[DETECTOR_DEFENCE] provisional_bssid=02:20:77:00:00:01 channel=1',
                    '[DETECTOR_DEFENCE] tx=decoy_4way stage=m1_m2 result=ok '
                        + 'frames=2 latency_us=9681 error=none',
                    '[DETECTOR_DEFENCE] released_bssid=02:20:77:00:00:01 '
                        + 'channel=1 reason=idle idle_ms=20000 '
                        + 'response=decoy_4way suppressed=299')
                """)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 24; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return v2Wire.length === 0 && emulatorObjects.some(item => item.text.includes(
                        'released 02:20:77:00:00:01 from channel 1 after idle; suppressed: 299'));
                }""", timeout=30000)

                page.evaluate("""() => v2Wire.push(
                    '[DETECTOR_DEFENCE] trigger=pmkid_probe '
                        + 'BSSID=02:20:77:00:00:01 STA=02:61:E8:69:3E:4F RX=1 '
                        + 'decision=observe reason=pmkid_monitor')
                """)
                page.wait_for_function("""() => {
                    emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return v2Wire.length === 0;
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertIn(
                    "released 02:20:77:00:00:01 from channel 1 after idle; suppressed: 299",
                    shown)
                self.assertNotIn("Defence: observing", shown)

                page.evaluate("""() => v2Wire.push(
                    '[DETECTOR_DEFENCE] trigger=deauth BSSID=02:20:77:00:00:01 '
                        + 'STA=- RX=1 decision=decoy_4way reason=selected_wpa2',
                    '[DETECTOR_DEFENCE] provisional_bssid=02:20:77:00:00:01 channel=1',
                    '[DETECTOR_DEFENCE] tx=decoy_4way stage=m1_m2 result=ok '
                        + 'frames=2 latency_us=9218 error=none')
                """)
                page.wait_for_function("""() => {
                    emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return v2Wire.length === 0 && emulatorObjects.some(item =>
                        item.text.includes('decoy TX confirmed by JanOS | responses: 2'));
                }""", timeout=30000)

                page.evaluate("""() => v2Wire.push(
                    '[DETECTOR_DEFENCE] released_bssid=02:20:77:00:00:01 '
                        + 'channel=1 reason=idle idle_ms=20000 '
                        + 'response=decoy_4way suppressed=608',
                    '[DETECTOR_DEFENCE] stopped observations=910 tx_pairs=2 '
                        + 'pmkid_m1=0 responses=2 satisfied=907 errors=0 dropped=0 '
                        + 'outcome=responded')
                """)
                page.wait_for_function("""() => {
                    for (let i = 0; i < 16; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return v2Wire.length === 0 && emulatorObjects.some(item => item.text.includes(
                        'released 02:20:77:00:00:01 from channel 1 after idle; suppressed: 608'));
                }""", timeout=30000)
                self.assertIn("Observed: 0 DEAUTH | 0 CSA | Latest groups: 0/200",
                              "\n".join(self.labels(page)))
            finally:
                page.close()

    def test_unsupported_defence_never_retries_legacy(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("""() => {
                    let rejectDefence = false;
                    const write = emulator.device.detectorWrite;
                    const read = emulator.device.detectorRead.bind(emulator.device);
                    emulator.device.detectorWrite = (tab, command) => {
                        if (tab === 0 && command ===
                            'deauth_detector_v2 --only 1 trace defence') {
                            detectorWrites.push({tab, command});
                            rejectDefence = true;
                            return true;
                        }
                        return write(tab, command);
                    };
                    emulator.device.detectorRead = tab => {
                        if (tab === 0 && rejectDefence) {
                            rejectDefence = false;
                            return 'Unrecognized command';
                        }
                        return read(tab);
                    };
                }""")
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Defence"))
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in
                               item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 10; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Defence unsupported by this JanOS'));
                }""", timeout=30000)
                commands = [item["command"] for item in page.evaluate("detectorWrites")]
                defence_commands = [command for command in commands
                                    if command.startswith("deauth_detector")]
                self.assertEqual(defence_commands,
                                 ["deauth_detector_v2 --only 1 trace defence"])
                self.assertNotIn("Detector running", self.labels(page))
            finally:
                page.close()

    def test_pmkid_lab_requires_confirmation_and_appends_exact_command_token(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1100})
            try:
                page.goto(url)
                self.open_detector(page)
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Defence"))
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Scan complete; choose BSSID and press Start');
                }""", timeout=30000)
                network = next(item for item in objects(page)
                               if "deauth_detector_render_networks.checkbox" in
                               item["binding"])
                click_object(page, {**network, "width": 24})
                click_text(page, "Use selection")
                pmkid = next(item for item in objects(page)
                             if item["text"] == "PMKID Lab (advanced)")
                self.assertFalse(pmkid["state"] & 0x80, pmkid)
                click_object(page, pmkid)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'Enable PMKID Lab?');
                }""", timeout=5000)
                warning = "\n".join(self.labels(page))
                self.assertIn("controlled RF laboratory", warning)
                self.assertIn("not attack prevention", warning)
                click_object(page, next(item for item in objects(page)
                                        if item["text"] == "Enable PMKID Lab"))
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => detectorWrites.some(item =>
                    item.tab === 0 && item.command ===
                    'deauth_detector_v2 --only 1 trace defence pmkid-decoy')
                """, timeout=5000)
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('PMKID Lab: enabled'));
                }""", timeout=30000)
                commands = [item["command"] for item in page.evaluate("detectorWrites")]
                self.assertEqual(
                    [command for command in commands if command.startswith("deauth_detector")],
                    ["deauth_detector_v2 --only 1 trace defence pmkid-decoy"])
            finally:
                page.close()

    def test_forensics_summary_evidence_and_filters_do_not_change_v2_history(self):
        with browser_session() as (browser, url):
            for rotation in (1, 0):
                with self.subTest(rotation=rotation):
                    page = browser.new_page(viewport={"width": 1440, "height": 1400})
                    try:
                        page.goto(f"{url}/?rotation={rotation}")
                        self.open_detector(page)
                        page.evaluate("globalThis.emulatorAlertChimes = []")
                        self.click_binding(page, "deauth_start_btn")
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item => item.text === 'Detector running');
                        }""", timeout=30000)
                        page.evaluate("rows => v2Wire.push(...rows)", list(FORENSIC_ROWS))
                        page.wait_for_function("""() => {
                            for (let i = 0; i < 16; i++) emulator.module._emu_tick(100);
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text.includes('Rows: 3 retained 3'));
                        }""", timeout=30000)
                        shown = "\n".join(self.labels(page))
                        self.assertIn("Bursts: 2 | Largest: 2", shown)
                        self.assertIn("Unique BSSID/DA: 2/2", shown)
                        self.assertIn("RSSI min/avg/max: -87/-83/-80 dBm", shown)
                        self.assertIn("Evidence only - not transmitter attribution", shown)
                        self.assertNotIn("Evidence only — not transmitter attribution", shown)
                        self.assertIn("Observed: 0 DEAUTH | 0 CSA | Latest groups: 0/200", shown)
                        self.assertNotIn("Lab (east)", shown)
                        self.assertEqual(page.evaluate("emulatorAlertChimes.length"), 0)

                        click_text(page, "View evidence")
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text.includes('Retransmission'));
                        }""", timeout=30000)
                        evidence = "\n".join(self.labels(page))
                        self.assertIn("Claimed TX AA:BB:CC:DD:EE:01", evidence)
                        self.assertIn("Frame recipient 12:34:56:78:9A:01", evidence)
                        self.assertIn("Insufficient sequence data", evidence)
                        self.assertNotIn("Attacker MAC", evidence)
                        self.assertNotIn("Attack prevented", evidence)
                        self.assertEqual(
                            page.evaluate("emulator.module._emu_forensic_filter(0, 1)"), 1)
                        self.assertEqual(
                            page.evaluate("emulator.module._emu_forensic_filter(3, 2)"), 1)
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item => item.text ===
                                'All forensic rows are hidden by the current filters');
                        }""", timeout=5000)
                        self.assertEqual(
                            page.evaluate("emulator.module._emu_forensic_filter(0, 0)"), 1)
                        self.assertEqual(
                            page.evaluate("emulator.module._emu_forensic_filter(3, 0)"), 1)
                        self.assertEqual(
                            page.evaluate("emulator.module._emu_forensic_filter(1, 2)"), 1)
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item =>
                                item.text.startsWith('Disassoc | CH 6'));
                        }""", timeout=5000)
                        self.assertEqual(
                            page.evaluate("emulator.module._emu_forensic_filter(2, 2)"), 1)
                        page.wait_for_function("""() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item => item.text ===
                                'All forensic rows are hidden by the current filters');
                        }""", timeout=5000)
                        title = next(item for item in objects(page)
                                     if item["text"] == "Forensic evidence")
                        display = (1280, 720) if rotation == 1 else (720, 1280)
                        self.assertGreaterEqual(title["x"], 0)
                        self.assertGreaterEqual(title["y"], 0)
                        self.assertLessEqual(title["x"] + title["width"], display[0])
                        self.assertLessEqual(title["y"] + title["height"], display[1])
                        click_text(page, "Close")
                    finally:
                        page.close()

    def test_forensics_absence_and_late_marker_keep_compatibility_state(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                self.assertIn("Forensics unavailable for this firmware",
                              "\n".join(self.labels(page)))
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running') &&
                           emulatorObjects.some(item =>
                               item.text === 'No forensic records yet - waiting for optional frame details from JanOS');
                }""", timeout=30000)
                self.click_binding(page, "deauth_stop_btn")
                page.wait_for_function("""() => {
                    for (let i = 0; i < 10; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Stopped') &&
                           emulatorObjects.some(item =>
                               item.text === 'Forensics unavailable for this firmware');
                }""", timeout=30000)
                page.evaluate("line => v2Wire.push(line)", FORENSIC_ROWS[0])
                self.advance(page, 8)
                shown = "\n".join(self.labels(page))
                self.assertIn("Forensics unavailable for this firmware", shown)
                self.assertIn("Observed: 0 DEAUTH | 0 CSA | Latest groups: 0/200", shown)
            finally:
                page.close()

    def test_forensics_long_run_fixture_has_exact_bursts_and_no_v2_side_effects(self):
        with browser_session() as (browser, url):
            page = browser.new_page(viewport={"width": 1440, "height": 1000})
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("globalThis.emulatorAlertChimes = []")
                self.click_binding(page, "deauth_start_btn")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item => item.text === 'Detector running');
                }""", timeout=30000)
                click_text(page, "View evidence")
                page.wait_for_function("""() => {
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text === 'No forensic records yet - waiting for optional frame details from JanOS');
                }""", timeout=5000)
                page.evaluate("rows => v2Wire.push(...rows)", long_forensic_rows())
                page.wait_for_function("""() => {
                    for (let i = 0; i < 48; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Rows: 23 retained 23')) &&
                           emulatorObjects.some(item =>
                        item.text.startsWith('Deauth | CH 6'));
                }""", timeout=30000)
                shown = "\n".join(self.labels(page))
                self.assertIn("Bursts: 11 | Largest: 3", shown)
                self.assertIn("Unique BSSID/DA: 1/1", shown)
                self.assertIn("RSSI min/avg/max: -87/-83/-80 dBm", shown)
                self.assertIn("Observed: 0 DEAUTH | 0 CSA | Latest groups: 0/200", shown)
                self.assertEqual(page.evaluate("emulatorAlertChimes.length"), 0)
                self.assertEqual(
                    page.evaluate("emulator.module._emu_forensic_filter(0, 1)"), 1)
                self.assertEqual(
                    page.evaluate("emulator.module._emu_forensic_filter(1, 1)"), 1)
                self.assertEqual(
                    page.evaluate("emulator.module._emu_forensic_filter(2, 2)"), 1)
                self.assertEqual(
                    page.evaluate("emulator.module._emu_forensic_filter(2, 0)"), 1)
                self.assertEqual(
                    page.evaluate("emulator.module._emu_forensic_filter(0, 1)"), 1)
                page.evaluate("rows => v2Wire.push(...rows)",
                              rollover_forensic_rows())
                page.wait_for_function("""() => {
                    for (let i = 0; i < 180; i++) emulator.module._emu_tick(100);
                    emulator.module._emu_inspect();
                    return emulatorObjects.some(item =>
                        item.text.includes('Rows: 151 retained 128'));
                }""", timeout=30000)
                rollover = "\n".join(self.labels(page))
                self.assertNotIn(
                    "All forensic rows are hidden by the current filters", rollover)
                self.assertIn("BSSID 02:30:00:00:00:00", rollover)
            finally:
                page.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
