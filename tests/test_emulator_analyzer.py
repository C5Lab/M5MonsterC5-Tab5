"""Native Wi-Fi Analyzer view, driven by synthetic WFA/1 model results."""
import unittest
from emulator_browser import objects, click_text, click_object
import test_emulator_portal_demo as helpers


class AnalyzerBrowser(unittest.TestCase):
    setUp = helpers.PortalDemoBrowser.setUp
    load = helpers.PortalDemoBrowser.load
    ready = helpers.PortalDemoBrowser.ready
    clean = helpers.PortalDemoBrowser.clean
    advance = helpers.PortalDemoBrowser.advance
    text = helpers.PortalDemoBrowser.text

    def click(self, label):
        if label == 'Back':
            target = lambda item: item['text'].endswith(' Back')
        else:
            target = lambda item: item['text'] == label
        fixed = label in {'GROVE', 'MBUS', 'WiFi\nAnalyzer', 'Scan', 'Stop', 'Back', 'Filters / Setup'}
        for _ in range(12):
            matches = [item for item in objects(self.page) if target(item)]
            self.assertEqual(len(matches), 1, label)
            item = matches[0]
            height = self.page.evaluate('emulator.module._emu_height()')
            y = item['y'] + item['height'] / 2
            if (0 <= y < height) if fixed else (250 <= y <= height - 100):
                click_object(self.page, item)
                return
            self.page.evaluate('(args) => {emulator.module._emu_wheel(500, args.y, args.delta); emulator.module._emu_tick(30)}',
                               {'y': 500 if y > height - 100 else 250,
                                'delta': 250 if y > height - 100 else -250})
        self.fail(f'Could not scroll {label} into view')

    def test_native_screen_scan_details_filters_and_advice(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                self.click('GROVE')
                self.click('WiFi\nAnalyzer')
                self.assertIn('Wi-Fi Analyzer', self.text())
                self.assertIn('No completed snapshot yet', self.text())
                self.click('Scan')
                self.advance(2400)
                self.page.wait_for_function("() => emulator.module._emu_analyzer_state(0) === 2")
                text = self.text()
                self.assertIn('NEON-BAZAAR', text)
                self.assertIn('Observed APs:', text)
                self.assertIn('returned', text)
                self.click('NEON-BAZAAR')
                self.assertIn('Network details', self.text())
                self.assertIn('02:20:77:00:00:01', self.text())
                self.click('Propose channel')
                self.assertIn('Channel proposal / 20 MHz', self.text())
                self.click('Filters / Setup')
                self.assertIn('Display filters', self.text())
                self.click('Reset filters')
                self.click('Back')
                self.assertNotIn('Wi-Fi Analyzer', self.text())
                self.clean()

    def test_stop_retains_last_snapshot_and_ports_are_separate(self):
        self.load()
        self.click('GROVE')
        self.click('WiFi\nAnalyzer')
        self.click('Scan')
        self.advance(2400)
        self.assertIn('NEON-BAZAAR', self.text())
        self.click('Filters / Setup')
        self.click('Back')
        self.click('MBUS')
        self.click('WiFi\nAnalyzer')
        self.assertIn('No completed snapshot yet', self.text())
        self.click('Scan')
        self.click('Stop')
        self.advance(300)
        self.assertIn('No completed snapshot yet', self.text())
        self.assertEqual(self.page.evaluate('emulator.module._emu_analyzer_state(2)'), 3)
        self.click('Back')
        self.click('GROVE')
        self.click('WiFi\nAnalyzer')
        self.assertIn('NEON-BAZAAR', self.text())
        self.clean()

    def test_guided_analyzer_walkthrough(self):
        self.load()
        self.page.set_default_timeout(5000)
        self.page.locator('#queue2-story').select_option('analyzer')
        self.page.locator('#guide-queue2').click()
        title = self.page.locator('#guide-title')
        self.assertEqual(title.inner_text(), 'Open Wi-Fi Analyzer on GROVE')
        self.click('GROVE')
        self.click('WiFi\nAnalyzer')
        self.page.wait_for_function("() => document.querySelector('#guide-title').textContent === 'Scan synthetic networks'")
        self.click('Scan')
        self.advance(2400)
        self.page.wait_for_function("() => document.querySelector('#guide-title').textContent === 'Inspect NEON-BAZAAR'")
        self.click('NEON-BAZAAR')
        self.page.wait_for_function("() => document.querySelector('#guide-title').textContent === 'Propose a channel'")
        self.click('Propose channel')
        self.page.wait_for_function("() => document.querySelector('#guide-title').textContent === 'Return to GROVE'")
        self.click('Back')
        self.page.wait_for_function("() => document.querySelector('#guide-title').textContent === 'Analyzer story complete'")
        self.clean()

    def test_disconnected_module_marks_prior_channel_advice_stale(self):
        self.load()
        self.click('GROVE')
        self.click('WiFi\nAnalyzer')
        self.click('Scan')
        self.advance(2400)
        self.assertIn('NEON-BAZAAR', self.text())
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.click('Scan')
        self.advance(100)
        self.assertIn('STALE', self.text())
        self.click('Propose channel')
        self.assertIn('Snapshot is stale', self.text())
        self.clean()


if __name__ == '__main__':
    unittest.main(verbosity=2)
