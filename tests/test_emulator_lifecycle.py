"""Phase 3 lifecycle acceptance through Canvas input; no shared report writes.

Run with: python -m unittest discover -s tests -p test_emulator_lifecycle.py -v
The compiled emulator must already exist in tools/ui_emulator/dist.
"""
import unittest

from emulator_browser import DIST, browser_session, click_object, click_text, objects


class EmulatorLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        self.context = self.browser.new_context(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.add_init_script("""globalThis.unsupportedEvents = [];
            addEventListener('emulator-unavailable', e => unsupportedEvents.push(e.detail));""")
        self.page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))
        self.page.goto(self.url)
        self.ready()

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unsupportedEvents'), [])
        self.assertEqual(self.page.evaluate('emulatorModelErrors'), [])

    def ready(self):
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')

    def bound(self, fragment):
        found = [o for o in objects(self.page) if fragment in o['binding']]
        self.assertEqual(len(found), 1, (fragment, found))
        return found[0]

    def click(self, fragment):
        click_object(self.page, self.bound(fragment))

    def snapshot(self, tab=0):
        return self.page.evaluate('(tab) => emulator.device.snapshot(tab)', tab)

    def files(self, module='grove'):
        return self.page.evaluate('(id) => emulator.device.device.files(id)', module)

    def advance(self):
        # Exercise the normal C/JS bridge, including scheduled model completions.
        # The window must exceed the longest scheduled job at realistic speed
        # (scan_time_ms is 4500 ms; app_speed is 1) so scans and captures settle.
        self.page.evaluate('for (let i=0;i<50;i++) emulator.module._emu_tick(100)')

    def prepare_observer(self, tab=0):
        click_text(self.page, 'Network\nObserver')
        click_text(self.page, 'Start')
        self.assertEqual(len([o for o in objects(self.page) if o['binding'].endswith('/observe')]), 12)

    def open_capture(self):
        self.click('02:20:77:00:00:01/observe')
        self.click('emu.phase3.observer.synthetic_capture')

    def start_capture(self, tab=0):
        self.click('emu.phase3.capture.generate')
        job = self.snapshot(tab)['active']
        self.assertIsNotNone(job, 'Capture must still be active in realistic timing mode')
        self.assertEqual(self.page.evaluate('(id) => emulator.device.device.job(id).state', job), 'running')
        return job

    def test_cancel_reopen_and_repeated_observer_start_stop(self):
        self.prepare_observer()
        self.page.locator('#timing').select_option('1')
        for _ in range(3):
            self.open_capture()
            job = self.start_capture()
            self.click('emu.phase3.capture.close')
            self.assertIsNone(self.snapshot()['active'])
            self.assertEqual(self.page.evaluate('(id) => emulator.device.device.job(id).state', job), 'cancelled')
            self.advance()
            self.assertEqual(self.files(), [], 'A cancelled job must not commit a late file')
            self.click('ui.show_network_popup.close_btn.clicked')
            click_text(self.page, 'Stop')
            click_text(self.page, 'Start')
            self.assertEqual(len([o for o in objects(self.page) if o['binding'].endswith('/observe')]), 12)

        self.open_capture()
        self.start_capture()
        self.advance()
        self.assertEqual(len(self.files()), 1)
        self.assertEqual(self.files()[0]['networkId'], 'net-1')
        self.assertIsNone(self.snapshot()['active'])
        self.click('emu.phase3.capture.close')
        self.click('ui.show_network_popup.close_btn.clicked')
        self.click('ui.show_observer_page.back_btn.clicked')
        click_text(self.page, 'Stop and exit', contains=True)
        click_text(self.page, 'Network\nObserver')
        click_text(self.page, 'Start')
        self.assertEqual(len([o for o in objects(self.page) if o['binding'].endswith('/observe')]), 12)

    def test_reset_clears_saved_files_and_pending_capture(self):
        self.prepare_observer()
        self.page.locator('#timing').select_option('1')
        self.open_capture()
        self.start_capture()
        self.advance()
        self.assertEqual(len(self.files()), 1)
        self.click('emu.phase3.capture.close')
        self.click('emu.phase3.observer.synthetic_capture')
        self.start_capture()
        with self.page.expect_navigation():
            self.page.locator('#reset').click()
        self.ready()
        self.advance()
        for tab, module in [(0, 'grove'), (2, 'mbus')]:
            state = self.snapshot(tab)
            self.assertEqual(state['networks'], [])
            self.assertIsNone(state['selected'])
            self.assertIsNone(state['active'])
            self.assertEqual(self.files(module), [])
        # Reset leaves the actual navigation usable for a fresh operation.
        self.prepare_observer()

    def test_rejected_scan_releases_modal_for_navigation(self):
        self.page.evaluate("emulator.device.device.setModule('grove', {connected:false})")
        click_text(self.page, 'WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0) === 4')
        self.assertIsNone(self.snapshot()['active'])
        self.assertEqual(self.snapshot()['networks'], [])
        errors = self.page.evaluate('emulatorModelErrors')
        self.assertEqual(len(errors), 1, errors)
        self.assertIn('disconnected', str(errors[0]).lower())
        # This refusal is intentional; no later action may report another one.
        self.page.evaluate('emulatorModelErrors.length = 0')
        click_text(self.page, 'INTERNAL')
        click_text(self.page, 'Settings')
        self.assertTrue(any(o['text'] == 'Scan\nSetup' for o in objects(self.page)))

    def test_module_switch_keeps_capture_and_scan_independent(self):
        self.prepare_observer()
        self.page.locator('#timing').select_option('1')
        self.open_capture()
        grove_job = self.start_capture()
        click_text(self.page, 'MBUS')
        self.assertEqual(self.snapshot(0)['active'], grove_job)
        self.assertEqual(self.snapshot(2)['networks'], [])
        click_text(self.page, 'WiFi Scan\n& Attack')
        mbus_job = self.snapshot(2)['active']
        self.assertIsNotNone(mbus_job)
        self.assertNotEqual(grove_job, mbus_job)
        self.assertEqual(self.snapshot(0)['active'], grove_job)
        self.advance()
        self.assertEqual(len(self.files('grove')), 1)
        self.assertEqual(self.files('mbus'), [])
        self.assertEqual(len(self.snapshot(2)['networks']), 12)
        self.assertEqual(self.snapshot(0)['networks'], [])
        self.click('02:20:77:00:00:01/select')
        self.click('ui.show_scan_page.back_btn.clicked')
        click_text(self.page, 'Network\nObserver')
        click_text(self.page, 'Start')
        self.open_capture()
        self.start_capture(2)
        self.advance()
        grove, mbus = self.files('grove'), self.files('mbus')
        self.assertEqual((len(grove), len(mbus)), (1, 1))
        self.assertNotEqual(grove[0]['path'], mbus[0]['path'])
        self.assertEqual((grove[0]['moduleId'], mbus[0]['moduleId']), ('grove', 'mbus'))
        for tab in (0, 2):
            self.assertIsNone(self.snapshot(tab)['active'])
        click_text(self.page, 'GROVE')
        self.assertTrue(any('PCAP saved' in o['text'] for o in objects(self.page)))
        self.click('emu.phase3.capture.close')


if __name__ == '__main__':
    unittest.main()
