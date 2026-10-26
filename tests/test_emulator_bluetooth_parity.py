"""Browser acceptance for the retained AirTag and nRF24 Bluetooth pages."""
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


class BluetoothParityTests(unittest.TestCase):
    def setUp(self):
        self.session = browser_session()
        self.browser, self.url = self.session.__enter__()
        self.addCleanup(self.session.__exit__, None, None, None)
        self.page = self.browser.new_page(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(self.page.close)
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')

    def test_airtag_scan_counts_and_back(self):
        click_text(self.page, 'Bluetooth')
        self.assertNotIn('Jammer', [obj['text'] for obj in objects(self.page)])
        click_text(self.page, 'AirTag\nScan')
        texts = [obj['text'] for obj in objects(self.page)]
        self.assertIn('AirTags', texts)
        self.assertIn('SmartTags', texts)
        self.page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o => o.text === '2') && emulatorObjects.some(o => o.text === '1');
        }''')
        back = next(obj for obj in objects(self.page) if obj['binding'].startswith('ui.show_airtag_scan_page.back_btn.'))
        click_object(self.page, back)
        self.assertIn('BT Scan\n& Locate', [obj['text'] for obj in objects(self.page)])

    def test_nrf24_jammer_changes_band_and_stops_on_back(self):
        click_text(self.page, 'MBUS')
        click_text(self.page, 'Bluetooth')
        click_text(self.page, 'Jammer')
        self.page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o => o.text === 'nRF24: OK');
        }''')
        self.assertIn('nRF24: OK', [obj['text'] for obj in objects(self.page)])
        click_text(self.page, 'wifi')
        click_text(self.page, 'Start Jammer', contains=True)
        self.page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o => o.text === 'Simulating wifi...');
        }''')
        self.assertNotIn('Duplicate active control binding', self.page.locator('#notice').inner_text())
        click_text(self.page, 'Stop Jammer', contains=True)
        self.assertIn('Idle', [obj['text'] for obj in objects(self.page)])
        click_text(self.page, 'Start Jammer', contains=True)
        back = next(obj for obj in objects(self.page) if obj['binding'].startswith('ui.show_jammer_page.back_btn.'))
        click_object(self.page, back)
        self.assertIn('BT Scan\n& Locate', [obj['text'] for obj in objects(self.page)])

    def test_nrf24_reports_missing_module(self):
        click_text(self.page, 'MBUS')
        self.page.evaluate("emulator.device.device.setModule('mbus', {connected:false})")
        click_text(self.page, 'Bluetooth')
        click_text(self.page, 'Jammer')
        self.page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o => o.text === 'nRF24: not detected');
        }''')
        click_text(self.page, 'Start Jammer', contains=True)
        self.page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o => o.text === 'nRF24 not detected!');
        }''')
        self.assertTrue(any('Start Jammer' in obj['text'] for obj in objects(self.page)))


if __name__ == '__main__':
    unittest.main()
