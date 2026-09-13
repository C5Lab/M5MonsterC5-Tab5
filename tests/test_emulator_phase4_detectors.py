"""Phase 4.7 Deauth Detector browser acceptance. Run only after rebuilding.

The detector page renders byte-exact from firmware; Start sends "deauth_detector"
and its infinite UART task is intercepted, so app_deauth_tick() feeds synthesized
"[DEAUTH] ..." lines through the production parse_deauth_line() into the table.
"""
import re
import unittest
from emulator_browser import browser_session, objects, click_text, click_object


class DeauthDetectorBrowser(unittest.TestCase):
    def text(self, page):
        return '\n'.join(o['text'] for o in objects(page))

    def advance(self, page, ticks=80):
        page.evaluate(f'()=>{{for(let i=0;i<{ticks};i++)emulator.module._emu_tick(100)}}')

    def click_binding(self, page, needle):
        matches = [o for o in objects(page) if needle in o['binding']]
        assert len(matches) == 1, (needle, [m['binding'] for m in matches])
        click_object(page, matches[0])

    def rows(self, page):
        # Each detected entry renders a "CH<n>" channel label in the table.
        return [o['text'] for o in objects(page) if re.match(r'^CH\d', o['text'])]

    def open_detector(self, page):
        page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(page, 'Deauth\nDetector')

    def test_start_detects_and_stop_freezes(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            try:
                page.goto(url)
                self.open_detector(page)
                self.assertIn('Deauth Detector', self.text(page))
                self.click_binding(page, 'deauth_start_btn')
                self.advance(page)
                detected = self.rows(page)
                self.assertGreater(len(detected), 0)
                # The scenario's first network drives the first detected frame.
                self.assertIn('NEON-BAZAAR', self.text(page))
                # Detection keeps adding rows while running.
                self.advance(page)
                self.assertGreater(len(self.rows(page)), len(detected))
                # Stop freezes the list.
                self.click_binding(page, 'deauth_stop_btn')
                frozen = len(self.rows(page))
                self.advance(page)
                self.assertEqual(len(self.rows(page)), frozen)
                # Back returns to the tiles.
                self.click_binding(page, 'show_deauth_detector_page.back_btn')
                self.assertIn('Deauth\nDetector', self.text(page))
                self.assertEqual(page.evaluate('unavailable'), [])
                self.assertEqual(errors, [])
            finally:
                page.close()

    def test_back_while_running_stops_detection(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                self.click_binding(page, 'deauth_start_btn')
                self.advance(page)
                self.assertGreater(len(self.rows(page)), 0)
                self.click_binding(page, 'show_deauth_detector_page.back_btn')
                self.advance(page)
                # Detection is stopped: reopening shows no growth beyond the frozen list.
                click_text(page, 'Deauth\nDetector')
                frozen = len(self.rows(page))
                self.advance(page)
                self.assertEqual(len(self.rows(page)), frozen)
            finally:
                page.close()

    def test_disconnected_module_detects_nothing(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                self.open_detector(page)
                page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
                self.click_binding(page, 'deauth_start_btn')
                self.advance(page)
                self.assertEqual(self.rows(page), [])
            finally:
                page.close()


class AntiSurvBrowser(unittest.TestCase):
    def text(self, page):
        return '\n'.join(o['text'] for o in objects(page))

    def advance(self, page, ticks=80):
        page.evaluate(f'()=>{{for(let i=0;i<{ticks};i++)emulator.module._emu_tick(100)}}')

    def click_binding(self, page, needle):
        matches = [o for o in objects(page) if needle in o['binding']]
        assert len(matches) == 1, (needle, [m['binding'] for m in matches])
        click_object(page, matches[0])

    def test_start_flags_followers_then_stop_summary(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
                click_text(page, 'Anti-Surv')
                self.click_binding(page, 'antisurv_start_btn')
                self.advance(page)
                self.assertIn('! FOLLOWER', self.text(page))
                self.assertIn('Followers:', self.text(page))
                self.click_binding(page, 'antisurv_stop_btn')
                self.advance(page, 5)
                # Stop reads the synthesized summary and shows the stopped state.
                self.assertIn('Stopped', self.text(page))
                self.click_binding(page, 'show_antisurv_page.back_btn')
                self.assertIn('Anti-Surv', self.text(page))
                self.assertEqual(page.evaluate('unavailable'), [])
                self.assertEqual(errors, [])
            finally:
                page.close()

    def test_disconnected_flags_nothing(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page, 'Anti-Surv')
                page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
                self.click_binding(page, 'antisurv_start_btn')
                self.advance(page)
                self.assertNotIn('! FOLLOWER', self.text(page))
            finally:
                page.close()


class HandshakerBrowser(unittest.TestCase):
    def text(self, page):
        return '\n'.join(o['text'] for o in objects(page))

    def advance(self, page, ticks=80):
        page.evaluate(f'()=>{{for(let i=0;i<{ticks};i++)emulator.module._emu_tick(100)}}')

    def click_binding(self, page, needle):
        matches = [o for o in objects(page) if needle in o['binding']]
        assert len(matches) == 1, (needle, [m['binding'] for m in matches])
        click_object(page, matches[0])

    def test_handshaker_captures_and_closes(self):
        with browser_session() as (browser, url):
            page = browser.new_page()
            errors = []
            page.on('pageerror', lambda e: errors.append(str(e)))
            try:
                page.goto(url)
                page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
                click_text(page, 'WiFi Scan\n& Attack')
                page.wait_for_function('emulator.module._emu_scan_state(0)===2')
                bssid = page.evaluate("emulator.device.snapshot(0).networks[0].bssid")
                click_object(page, next(o for o in objects(page) if o['binding'].endswith(bssid + '/select')))
                click_text(page, 'Handshake')
                self.advance(page)
                self.assertIn('Handshake captured', self.text(page))
                # The Stop/Done button closes the popup and sends "stop".
                self.click_binding(page, 'handshaker_stop_btn')
                # The success path plays a win chime; audio is an intentionally
                # inert boundary in the emulator, so tolerate only that.
                self.assertEqual([u for u in page.evaluate('unavailable') if u != 'alert_chime_play'], [])
                self.assertEqual(errors, [])
            finally:
                page.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
