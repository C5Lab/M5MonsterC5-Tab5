"""NFC native-page navigation and session-only virtual card/SD behavior."""
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


class NfcBrowser(unittest.TestCase):
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
        return [item['text'] for item in objects(self.page) if item['text']]

    def click_last(self, text):
        matches = [item for item in objects(self.page) if text in item['text']]
        self.assertTrue(matches, text)
        click_object(self.page, matches[-1])

    def save_name(self, name):
        self.click_last('Save')
        self.assertIn('Save card', self.labels())
        snapshot = objects(self.page)
        card_id = next(item['parent'] for item in snapshot if item['text'] == 'Save card')
        textarea = [item for item in snapshot if item['parent'] == card_id and
                    item['clickable'] and item['width'] > 500 and item['height'] < 55]
        self.assertTrue(textarea)
        click_object(self.page, textarea[-1])
        self.page.keyboard.type(name)
        self.click_last('Save')

    def test_read_save_list_detail_and_emulate(self):
        click_text(self.page, 'NFC')
        self.page.wait_for_timeout(150)
        self.assertIn('NFC ready', self.labels())
        click_text(self.page, 'Read')
        self.click_last('\uf021 Read')  # LV_SYMBOL_REFRESH + Read
        self.page.wait_for_timeout(150)
        self.assertIn('NTAG213', self.labels())
        self.save_name('DemoTag')
        self.page.wait_for_timeout(150)
        self.assertIn('Saved', self.labels())
        self.page.keyboard.press('Escape')
        self.page.wait_for_timeout(150)
        click_text(self.page, 'List')
        self.page.wait_for_timeout(150)
        self.assertIn('0  DemoTag.nfc', self.labels())
        click_text(self.page, '0  DemoTag.nfc')
        self.page.wait_for_timeout(150)
        self.assertIn('NTAG213', self.labels())
        click_text(self.page, 'Emulate')
        self.page.wait_for_timeout(150)
        self.assertIn('Emulating', self.labels())
        self.assertTrue(self.page.evaluate('emulator.device.nfc.state(2).emulating'))
        self.page.keyboard.press('Escape')
        self.assertFalse(self.page.evaluate('emulator.device.nfc.state(2).emulating'))
        self.assertIn('NFC Card', self.labels())
        self.page.keyboard.press('Escape')
        self.page.wait_for_timeout(150)
        self.assertIn('0  DemoTag.nfc', self.labels())
        self.page.keyboard.press('Escape')
        self.page.wait_for_timeout(150)
        self.assertIn('NFC ready', self.labels())

    def test_cancel_and_delete(self):
        click_text(self.page, 'NFC')
        self.page.wait_for_timeout(150)
        click_text(self.page, 'Read')
        self.click_last('\uf021 Read')
        self.page.wait_for_timeout(150)
        self.click_last('Save')
        self.assertIn('Save card', self.labels())
        self.click_last('Cancel')
        self.assertEqual(self.page.evaluate('emulator.device.nfc.state(2).sd.length'), 0)
        self.save_name('DeleteMe')
        self.page.wait_for_timeout(150)
        self.page.keyboard.press('Escape')
        self.page.wait_for_timeout(150)
        click_text(self.page, 'List')
        self.page.wait_for_timeout(150)
        click_text(self.page, '0  DeleteMe.nfc')
        self.page.wait_for_timeout(150)
        click_text(self.page, 'Rename')
        snapshot = objects(self.page)
        card_id = next(item['parent'] for item in snapshot if item['text'] == 'Rename card')
        textarea = next(item for item in snapshot if item['parent'] == card_id and
                        item['clickable'] and item['width'] > 500 and item['height'] < 55)
        click_object(self.page, textarea)
        for _ in 'DeleteMe':
            self.page.keyboard.press('Backspace')
        self.page.keyboard.type('Renamed')
        self.click_last('Save')
        self.page.wait_for_timeout(150)
        self.assertEqual(self.page.evaluate('emulator.device.nfc.state(2).sd[0].name'), 'Renamed')
        click_text(self.page, 'Delete')
        self.click_last('Cancel')
        self.assertEqual(self.page.evaluate('emulator.device.nfc.state(2).sd.length'), 1)
        click_text(self.page, 'Delete')
        self.click_last('Delete')
        self.page.wait_for_timeout(350)
        self.assertEqual(self.page.evaluate('emulator.device.nfc.state(2).sd.length'), 0)
        self.assertIn('No saved cards', self.labels())


if __name__ == '__main__':
    unittest.main(verbosity=2)
