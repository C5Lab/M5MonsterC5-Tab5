"""Native GITM acceptance; authored for a rebuilt browser, not run by cheap checks."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object, DIST


class GitmBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        self.context = self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.route('**/emulator.wasm', lambda route: route.fulfill(path=str(DIST/'emulator.wasm'), content_type='application/wasm'))
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(self.page, 'Global WiFi\nAttacks')
        click_text(self.page, 'GITM')

    def tearDown(self):
        self.assertEqual(self.errors, [])

    def tick(self, count=20):
        self.page.evaluate('(n)=>{for(let i=0;i<n;i++)emulator.module._emu_tick(100)}', count)

    def text(self):
        return '\n'.join(item['text'] for item in objects(self.page))

    def click(self, text):
        # Scroll the native body; never invoke firmware callbacks from JS.
        for _ in range(15):
            all_objects = objects(self.page)
            matches = [item for item in all_objects if text in item['text']]
            if matches:
                item = matches[-1]
                width, height = self.page.locator('#display').evaluate('(c)=>[c.width,c.height]')
                if 0 < item['y'] < height - 50:
                    click_object(self.page, item)
                    return
            self.page.evaluate('emulator.module._emu_wheel(350,600,250)')
        self.fail('Native control not reachable: ' + text)

    def setup_capture(self):
        self.click('SCAN'); self.tick()
        self.assertIn('AFTERLIFE-GUEST', self.text())
        self.click('AFTERLIFE-GUEST'); self.click('CONNECT'); self.tick()
        self.assertIn('Name your GITM access point', self.text())
        fields = [item for item in objects(self.page) if '/ap:AP SSID:/field' in item['binding']]
        # The AP SSID field is the first visible field after the Internet section folds.
        self.assertTrue(fields)
        click_object(self.page, fields[0])
        self.page.keyboard.type('SIMULATED-GATEWAY')
        # Native keyboard Done is LVGL's checkmark, delivered as a pointer event.
        keyboard = [item for item in objects(self.page) if 'show_gitm_page.ctx--gitm--keyboard' in item['binding']]
        if keyboard:
            item = keyboard[0]
            click_object(self.page, {'x': item['x'] + item['width'] * .94,
                                     'y': item['y'] + item['height'] * .875, 'width': 1, 'height': 1})
        self.click('START GITM'); self.tick()
        self.assertIn('Simulated capture running', self.text())

    def test_stop_saves_genuine_pcap_and_copy_opens_espshark(self):
        self.setup_capture()
        self.click('STOP GITM'); self.tick(2)
        self.assertIn('Simulated PCAP finalized', self.text())
        files = self.page.evaluate("emulator.device.device.files('grove')")
        self.assertTrue(any(item['path'].endswith('.pcap') for item in files))
        self.click('COPY TO TAB5 / ESPSHARK')
        self.tick(); self.assertIn('.pcap', self.text())

    def test_disconnect_while_scanning_releases_busy_state(self):
        self.click('SCAN')
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.tick(2)
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        self.assertNotIn('Scanning...', self.text())

    def test_reset_invalidates_pending_scan_without_stale_rows(self):
        self.click('SCAN')
        self.page.evaluate('emulator.device.device.reset()')
        self.tick(2)
        self.assertIn('Operation reset', self.text())
        self.assertNotIn('AFTERLIFE-GUEST', self.text())


if __name__ == '__main__':
    unittest.main()
