"""Native Sub-GHz screens driven by the virtual radio, with no RF hardware."""
import unittest

from emulator_browser import browser_session, click_text, objects


class SubghzBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        self.page = self.browser.new_page(viewport={'width': 1440, 'height': 1000})
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=30000)
        click_text(self.page, 'MBUS')

    def tearDown(self):
        self.page.close()

    def labels(self):
        return [obj['text'] for obj in objects(self.page) if obj['text']]

    def test_disconnected_mbus_is_not_offered_as_a_radio_tab(self):
        self.page.goto(f'{self.url}/?moduleCondition=grove-only')
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=30000)
        self.assertFalse(self.page.evaluate('emulator.device.device.systemStatus("mbus").connected'))
        self.assertNotIn('MBUS', self.labels())
        self.assertNotIn('Sub-GHz', self.labels())
        self.assertNotIn('NFC', self.labels())

    def test_native_menu_opens_all_eight_tools(self):
        click_text(self.page, 'Sub-GHz')
        for tool in ('Quick Scan', 'Hunter', 'Listen', 'SD Signals',
                     'Weather', 'Jammer', 'Tesla', 'Settings'):
            with self.subTest(tool=tool):
                self.assertIn(tool, self.labels())
                click_text(self.page, tool)
                self.assertTrue(any(tool in label for label in self.labels()))
                self.page.keyboard.press('Escape')
                self.assertIn('Quick Scan', self.labels())

    def test_quick_scan_shows_numeric_frequency(self):
        click_text(self.page, 'Sub-GHz')
        click_text(self.page, 'Quick Scan')
        self.page.wait_for_timeout(300)
        self.assertIn('433.92 MHz', self.labels())

    def test_listen_weather_jammer_and_tesla_actions(self):
        click_text(self.page, 'Sub-GHz')
        click_text(self.page, 'Listen')
        click_text(self.page, 'Start', contains=True)
        self.page.wait_for_timeout(250)
        self.assertIn('Sig: 3', self.labels())
        click_text(self.page, 'Stop', contains=True)
        self.page.keyboard.press('Escape')
        click_text(self.page, 'Leave')
        self.page.wait_for_timeout(80)
        click_text(self.page, 'Weather')
        self.page.wait_for_timeout(400)
        self.assertIn('21.5 C', self.labels())
        self.assertIn('48%', self.labels())
        self.page.keyboard.press('Escape')
        self.page.wait_for_timeout(80)
        click_text(self.page, 'Jammer')
        click_text(self.page, 'START JAM', contains=True)
        self.page.wait_for_timeout(100)
        self.assertTrue(any('Jamming 433.92 MHz' in label for label in self.labels()))
        click_text(self.page, 'STOP', contains=True)
        self.page.keyboard.press('Escape')
        self.page.wait_for_timeout(80)
        click_text(self.page, 'Tesla')
        click_text(self.page, 'Open Port', contains=True)
        self.assertIn('Signal sent!', self.labels())

    def test_hunter_capture_and_leave_confirmation(self):
        click_text(self.page, 'Sub-GHz')
        click_text(self.page, 'Hunter')
        self.page.wait_for_function('emulator.device.subghz.state(2).memory.length === 3', timeout=5000)
        self.page.wait_for_timeout(250)
        self.assertIn('Captures: 3', self.labels())
        self.page.keyboard.press('Escape')
        self.assertIn('Leave Hunter?', self.labels())
        click_text(self.page, 'Leave')
        self.assertIn('Quick Scan', self.labels())

    def test_hunter_capture_can_be_saved_to_virtual_sd(self):
        click_text(self.page, 'Sub-GHz')
        click_text(self.page, 'Hunter')
        self.page.wait_for_function('emulator.device.subghz.state(2).memory.length === 3', timeout=5000)
        self.page.wait_for_timeout(250)
        click_text(self.page, '#1  Princeton', contains=True)
        click_text(self.page, 'Save')
        self.page.wait_for_function('emulator.device.subghz.state(2).sd.length === 1', timeout=5000)
        self.assertEqual(self.page.evaluate('emulator.device.subghz.state(2).sd[0].label'), 'Gate_433')


if __name__ == '__main__':
    unittest.main(verbosity=2)
