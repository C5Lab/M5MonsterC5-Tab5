"""Native offline wpa-sec acceptance. Run after rebuilding the emulator."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object


class WpasecBrowser(unittest.TestCase):
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
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(self.page, 'Compromised\nData')
        click_text(self.page, 'Handshakes')
        click_text(self.page, 'Send to wpa-sec', contains=True)
        self.tick(30)

    def tick(self, count=30):
        self.page.evaluate('(n)=>{for(let i=0;i<n;i++)emulator.module._emu_tick(100)}', count)

    def text(self):
        return '\n'.join(o['text'] for o in objects(self.page))

    def connect(self):
        rows = [o for o in objects(self.page)
                if o['binding'].startswith('emu.phase4.wpasec.network.')]
        self.assertGreater(len(rows), 0)
        click_object(self.page, rows[0])
        self.tick(2)
        toggles = [o for o in objects(self.page) if 'show_pass_checkbox' in o['binding']]
        self.assertEqual(len(toggles), 1, 'Password visibility control must be bound')
        click_text(self.page, 'Connect')
        self.tick(1)

    def test_simulated_upload_results(self):
        self.assertIn('Offline simulation', self.text())
        self.connect()
        self.tick(60)
        self.assertIn('Simulated upload complete', self.text())
        self.assertIn('Uploaded: 2', self.text())
        self.assertIn('No files sent', self.text())

    def test_close_cancels_and_reopens(self):
        self.connect()
        click_text(self.page, 'Close')
        self.tick(2)
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        click_text(self.page, 'Send to wpa-sec', contains=True)
        self.tick(30)
        self.assertIn('Offline simulation', self.text())

    def test_manual_credentials_require_ssid(self):
        row = next(o for o in objects(self.page) if o['binding'].startswith('emu.phase4.wpasec.other.pick/'))
        # The original network list scrolls; bring the manual row into view.
        for _ in range(8):
            if row['y'] < 600:
                break
            self.page.evaluate('emulator.module._emu_wheel(650,400,300)')
            row = next(o for o in objects(self.page) if o['binding'].startswith('emu.phase4.wpasec.other.pick/'))
        click_object(self.page, row)
        self.tick(2)
        click_text(self.page, 'Connect')
        self.tick(2)
        self.assertIn('enter SSID', self.text())

    def test_failure_is_visible(self):
        self.page.evaluate("globalThis.emulatorWpasecOutcome='failure'")
        self.connect()
        self.tick(60)
        self.assertIn('failed', self.text().lower())


if __name__ == '__main__':
    unittest.main()
