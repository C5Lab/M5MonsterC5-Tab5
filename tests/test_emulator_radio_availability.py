"""Browser wiring carries live module/SD availability into both radio models."""
import unittest

from emulator_browser import browser_session, click_text, objects


class RadioAvailability(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def load(self, condition):
        page = self.browser.new_page(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(page.close)
        page.goto(f'{self.url}/?moduleCondition={condition}')
        page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=30000)
        return page

    def wait_label(self, page, label):
        page.wait_for_function('''text => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(item => item.text === text);
        }''', arg=label, timeout=5000)

    def test_disconnected_mbus_rejects_both_radio_commands(self):
        page = self.load('grove-only')
        result = page.evaluate('''() => {
            const bridge = emulator.device;
            bridge.subghz.command(2, 'subghz_scanner');
            bridge.nfc.command(2, 'init_nfc');
            return {
                connected: bridge.device.systemStatus('mbus').connected,
                mode: bridge.subghz.state(2).mode,
                subghz: bridge.subghz.drain(2),
                nfc: bridge.nfc.drain(2),
            };
        }''')
        self.assertFalse(result['connected'])
        self.assertEqual(result['mode'], 'idle')
        self.assertIn('CC1101 NOT DETECTED', result['subghz'])
        self.assertEqual(result['nfc'], ['[NFC] not detected', '[NFC] END'])

    def test_missing_sd_rejects_saves_while_read_and_capture_still_work(self):
        page = self.load('missing-sd')
        result = page.evaluate('''() => {
            const bridge = emulator.device;
            bridge.nfc.command(2, 'nfc_read');
            const card = bridge.nfc.drain(2);
            bridge.nfc.command(2, 'nfc_save DemoTag');
            const nfcSave = bridge.nfc.drain(2);
            bridge.subghz.command(2, 'subghz_rx rssi=-80');
            bridge.subghz.advance(400);
            const captures = bridge.subghz.state(2).memory.length;
            bridge.subghz.drain(2);
            bridge.subghz.command(2, 'subghz_save 1');
            return {
                sdPresent: bridge.device.systemStatus('mbus').sdPresent,
                card, captures, nfcSave,
                subghzSave: bridge.subghz.drain(2),
                nfcSd: bridge.nfc.state(2).sd.length,
                subghzSd: bridge.subghz.state(2).sd.length,
            };
        }''')
        self.assertFalse(result['sdPresent'])
        self.assertIn('[NFC] type: NTAG213', result['card'])
        self.assertEqual(result['captures'], 1)
        self.assertIn('[NFC] save failed: SD missing', result['nfcSave'])
        self.assertIn('[SUBGHZ_SAVE_ERR] reason=sd_missing', result['subghzSave'])
        self.assertEqual((result['nfcSd'], result['subghzSd']), (0, 0))

    def test_native_sd_lists_explain_missing_card_and_refresh_after_reinsert(self):
        page = self.load('missing-sd')
        click_text(page, 'MBUS')
        click_text(page, 'NFC')
        self.wait_label(page, 'NFC ready')
        click_text(page, 'List')
        self.wait_label(page, 'SD unavailable')
        self.assertNotIn('No saved cards', [item['text'] for item in objects(page)])
        page.evaluate("emulator.device.device.setModule('mbus', {sdPresent:true})")
        self.wait_label(page, 'No saved cards')
        self.assertNotIn('SD unavailable', [item['text'] for item in objects(page)])
        page.keyboard.press('Escape')
        self.wait_label(page, 'NFC ready')
        page.keyboard.press('Escape')

        page.evaluate("emulator.device.device.setModule('mbus', {sdPresent:false})")
        click_text(page, 'Sub-GHz')
        click_text(page, 'SD Signals')
        self.wait_label(page, 'SD unavailable')
        self.assertNotIn('No signals saved on SD', [item['text'] for item in objects(page)])
        page.evaluate("emulator.device.device.setModule('mbus', {sdPresent:true})")
        self.wait_label(page, '0 signals on SD')
        self.assertNotIn('SD unavailable', [item['text'] for item in objects(page)])


if __name__ == '__main__':
    unittest.main(verbosity=2)
