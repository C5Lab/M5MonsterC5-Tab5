"""Canvas smoke for native radio pages at every rotation and two CSS widths."""
import unittest

from emulator_browser import browser_session, click_text, objects


def has_text(page, text):
    return any(item['text'] == text for item in objects(page))


class RadioRotations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def test_subghz_and_nfc_actions_back_at_all_rotations_and_widths(self):
        for width, height in ((1440, 1000), (390, 844)):
            for rotation in range(4):
                with self.subTest(width=width, rotation=rotation):
                    context = self.browser.new_context(viewport={'width': width, 'height': height})
                    try:
                        page = context.new_page()
                        errors = []
                        page.on('pageerror', lambda error: errors.append(str(error)))
                        page.add_init_script('''
                            globalThis.radioUnavailable = [];
                            globalThis.radioModelErrors = [];
                            addEventListener('emulator-unavailable', event => radioUnavailable.push(event.detail));
                            addEventListener('emulator-model-error', event => radioModelErrors.push(event.detail));
                        ''')
                        page.goto(f'{self.url}/?rotation={rotation}')
                        page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=30000)
                        self.assertEqual(
                            page.evaluate('[emulator.module._emu_width(), emulator.module._emu_height()]'),
                            [1280, 720] if rotation % 2 else [720, 1280])

                        click_text(page, 'MBUS')
                        click_text(page, 'Sub-GHz')
                        click_text(page, 'Tesla')
                        click_text(page, 'Open Port', contains=True)
                        self.assertTrue(has_text(page, 'Signal sent!'))
                        page.keyboard.press('Escape')
                        self.assertTrue(has_text(page, 'Quick Scan'))
                        page.keyboard.press('Escape')
                        self.assertTrue(has_text(page, 'Sub-GHz'))

                        click_text(page, 'NFC')
                        page.wait_for_function('''() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item => item.text === 'NFC ready');
                        }''')
                        click_text(page, 'Read')
                        click_text(page, '\uf021 Read')  # LV_SYMBOL_REFRESH + Read
                        page.wait_for_function('''() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item => item.text === 'NTAG213');
                        }''')
                        page.keyboard.press('Escape')
                        page.wait_for_function('''() => {
                            emulator.module._emu_inspect();
                            return emulatorObjects.some(item => item.text === 'NFC ready');
                        }''')
                        page.keyboard.press('Escape')
                        self.assertTrue(has_text(page, 'NFC'))

                        self.assertEqual(errors, [])
                        self.assertEqual(page.evaluate('radioUnavailable'), [])
                        self.assertEqual(page.evaluate('radioModelErrors'), [])
                    finally:
                        context.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
