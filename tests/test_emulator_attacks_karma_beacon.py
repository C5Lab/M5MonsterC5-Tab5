"""Native synthetic Karma/Beacon acceptance; execute only after an emulator rebuild."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object


class KarmaBeaconBrowser(unittest.TestCase):
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

    def tick(self, count=20):
        self.page.evaluate('(n)=>{for(let i=0;i<n;i++)emulator.module._emu_tick(100)}', count)

    def text(self):
        return '\n'.join(o['text'] for o in objects(self.page))

    def bound(self, fragment):
        matches = [o for o in objects(self.page) if fragment in o['binding']]
        self.assertEqual(len(matches), 1, fragment)
        click_object(self.page, matches[0])

    def test_beacon_start_stop_and_settings(self):
        click_text(self.page, 'Global WiFi\nAttacks')
        click_text(self.page, 'Beacon Spam')
        click_text(self.page, 'List SSIDs')
        self.assertIn('Demo Beacon 1', self.text())
        self.bound('show_beacon_ssids_page.back_btn.')
        click_text(self.page, 'Start Spam')
        self.tick()
        self.assertIn('Offline simulation: running', self.text())
        self.assertIn('Packets:', self.text())
        self.bound('emu.phase45.beacon_spam_start_cb.beacon_spam_active_close_cb.')
        self.tick(1)
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        click_text(self.page, 'Start Spam')
        self.tick()
        self.assertIn('Offline simulation: running', self.text())

    def test_karma_sniffer_probe_portal_and_stop(self):
        click_text(self.page, 'Karma')
        click_text(self.page, 'Start Sniffer')
        self.tick()
        self.assertIn('Offline simulation: running', self.text())
        click_text(self.page, 'Stop Sniffer')
        self.tick(1)
        click_text(self.page, 'NEON-BAZAAR')
        self.tick(1)
        click_text(self.page, 'Start Karma')
        self.tick()
        self.assertIn('Packets:', self.text())
        click_text(self.page, 'STOP')
        self.tick(1)
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

    def test_karma_back_cancels(self):
        click_text(self.page, 'Karma')
        click_text(self.page, 'Start Sniffer')
        self.tick()
        self.bound('emu.phase45.show_karma_page.back_btn.')
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

    def test_karma_tab_switch_preserves_owner_and_controls(self):
        click_text(self.page, 'Karma')
        click_text(self.page, 'Start Sniffer')
        self.tick()
        job = self.page.evaluate("emulator.device.device.snapshot('grove').active")
        click_text(self.page, 'MBUS')
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('mbus').active"))
        click_text(self.page, 'GROVE')
        self.tick(1)
        self.assertEqual(self.page.evaluate("emulator.device.device.snapshot('grove').active"), job)
        click_text(self.page, 'Stop Sniffer')
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))


if __name__ == '__main__':
    unittest.main()
