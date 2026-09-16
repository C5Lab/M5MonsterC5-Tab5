"""Settings lifecycle through the built browser shell and retained LVGL UI."""
import unittest
from emulator_browser import DIST, browser_session, click_text, objects


class PersistenceTests(unittest.TestCase):
    def setUp(self):
        self.session = browser_session()
        self.browser, self.url = self.session.__enter__()
        self.context = self.browser.new_context(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(self.session.__exit__, None, None, None)
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        # Keep settings/reload acceptance independent of intermittent localhost
        # resets during the large Wasm transfer. Failure tests override this route.
        self.wasm = (DIST / 'emulator.wasm').read_bytes()
        self.page.route('**/emulator.wasm', lambda route: route.fulfill(
            body=self.wasm, content_type='application/wasm'))

    def ready(self):
        try:
            self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        except Exception:
            print(self.page.evaluate('''() => ({status: document.querySelector('#status')?.textContent,
                frames: globalThis.emulator?.measurements, visibility: document.visibilityState,
                resources: performance.getEntriesByType('resource').map(r => ({name:r.name, duration:r.duration}))})'''))
            raise

    def brightness(self, expected):
        click_text(self.page, 'INTERNAL')
        click_text(self.page, 'Settings')
        click_text(self.page, 'Screen\nBrightness')
        self.assertTrue(any(o['text'] == f'{expected}%' for o in objects(self.page)))

    def test_legacy_migration_reload_and_complete_reset(self):
        p = self.page
        p.goto(self.url)
        self.ready()
        p.evaluate('''() => {
            localStorage.removeItem('tab5-emulator:settings');
            localStorage.setItem('tab5-emulator-v1:scr_bright', '23');
            localStorage.setItem('tab5-emulator-v1:scr_rot', '3');
            localStorage.setItem('unrelated', 'keep');
        }''')
        p.reload(); self.ready()
        self.assertEqual(p.locator('#rotation').input_value(), '3')
        self.brightness(23)
        p.reload(); self.ready()
        self.brightness(23)
        p.evaluate("emulator.device.device.installPcapFixtures('grove')")
        self.assertGreater(p.evaluate("emulator.device.device.files('grove').length"), 0)
        p.locator('#reset').click()
        p.wait_for_url('**/?rotation=0'); self.ready()
        self.assertEqual(p.locator('#rotation').input_value(), '0')
        self.brightness(80)
        self.assertEqual(p.evaluate("emulator.device.device.files('grove')"), [])
        self.assertEqual(p.evaluate('localStorage.getItem("unrelated")'), 'keep')
        p.reload(); self.ready()
        self.brightness(80)

    def test_storage_denial_keeps_native_settings_usable(self):
        self.page.add_init_script('''Object.defineProperty(window, 'localStorage', {
            get() { throw new Error('storage denied'); }
        });''')
        p = self.page
        p.goto(self.url); self.ready()
        self.assertIn('session', p.locator('#storage-status').inner_text().lower())
        click_text(p, 'INTERNAL'); click_text(p, 'Settings')
        click_text(p, 'Screen\nBrightness')
        from test_emulator_phase2 import bound, position
        slider = bound(p, 'show_screen_brightness_popup.screen_brightness_slider.value_changed')
        p.mouse.click(*position(p, slider, .3))
        value = p.evaluate('emulatorSettings.get("scr_bright", 80)')
        self.assertLess(value, 50)
        click_text(p, 'Close'); click_text(p, 'Screen\nBrightness')
        self.assertTrue(any(o['text'] == f'{value}%' for o in objects(p)))
        p.reload(); self.ready()
        self.brightness(value)

    def test_future_schema_recovers_visibly(self):
        p = self.page
        p.goto(self.url); self.ready()
        p.evaluate('localStorage.setItem("tab5-emulator:settings", JSON.stringify({version:99,values:{scr_bright:1}}))')
        p.reload(); self.ready()
        self.assertIn('reset', p.locator('#storage-status').inner_text().lower())
        self.brightness(80)

    def test_reset_with_readable_but_unwritable_storage(self):
        p = self.page
        p.goto(self.url); self.ready()
        p.evaluate('emulatorSettings.set("scr_bright",23)')
        p.add_init_script('''Storage.prototype.setItem = () => { throw Error('quota'); };
            Object.defineProperty(window, 'sessionStorage', {get() {throw Error('denied');}});''')
        p.reload(); self.ready()
        self.brightness(23)
        with p.expect_navigation():
            p.locator('#reset').click()
        self.ready()
        self.brightness(80)

        p.reload(); self.ready()
        self.brightness(80)

    def test_interrupted_wasm_download_recovers(self):
        attempts = []
        def download(route):
            attempts.append(route.request.url)
            if len(attempts) <= 2:
                route.abort('connectionreset')
            else:
                route.fulfill(body=self.wasm, content_type='application/wasm')
        self.page.route('**/emulator.wasm', download)
        self.page.goto(self.url)
        self.ready()
        self.assertEqual(len(attempts), 3)

    def test_failed_wasm_download_stops_after_three_attempts(self):
        attempts = []
        def download(route):
            attempts.append(route.request.url)
            route.abort('connectionreset')
        self.page.route('**/emulator.wasm', download)
        self.page.goto(self.url)
        self.page.wait_for_function("document.querySelector('#status').textContent.includes('Could not load')")
        self.assertEqual(len(attempts), 3)
        self.assertIn('reset', self.page.locator('#status').inner_text().lower())


if __name__ == '__main__':
    unittest.main()
