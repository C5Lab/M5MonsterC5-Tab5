"""Browser acceptance for two production Settings routes added to the emulator."""
import unittest

from emulator_browser import browser_session, click_object, click_text, objects


class SettingsParityTests(unittest.TestCase):
    def setUp(self):
        self.session = browser_session()
        self.browser, self.url = self.session.__enter__()
        self.addCleanup(self.session.__exit__, None, None, None)
        self.page = self.browser.new_page(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(self.page.close)
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')

    def settings(self):
        click_text(self.page, 'INTERNAL')
        self.page.evaluate('emulator.module._emu_tick(100)')
        self.page.evaluate('emulator.module._emu_tick(100)')
        click_text(self.page, 'Settings')
        self.page.wait_for_function('''() => {
            emulator.module._emu_inspect();
            return emulatorObjects.some(o => o.text === 'Tool\\nOrder');
        }''')

    def test_tool_order_moves_and_persists_after_reload(self):
        self.settings()
        click_text(self.page, 'Tool\nOrder')
        self.assertIn('Move later', [obj['text'] for obj in objects(self.page)])
        click_text(self.page, 'WiFi Scan & Attack')
        click_text(self.page, 'Move later')
        click_text(self.page, 'Save')
        self.page.reload()
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        self.settings()
        click_text(self.page, 'Tool\nOrder')
        labels = [obj for obj in objects(self.page) if obj['text'] in ('WiFi Scan & Attack', 'Global WiFi Attacks')]
        if any(obj['width'] == 0 for obj in labels):
            self.page.wait_for_function('''() => {
                emulator.module._emu_inspect();
                return emulatorObjects.filter(o => o.text === 'WiFi Scan & Attack' || o.text === 'Global WiFi Attacks')
                    .every(o => o.width > 0);
            }''')
            labels = [obj for obj in objects(self.page) if obj['text'] in ('WiFi Scan & Attack', 'Global WiFi Attacks')]
        self.assertEqual(len(labels), 2)
        by_text = {obj['text']: obj for obj in labels}
        self.assertLess((by_text['Global WiFi Attacks']['y'], by_text['Global WiFi Attacks']['x']),
                        (by_text['WiFi Scan & Attack']['y'], by_text['WiFi Scan & Attack']['x']))

    def test_screen_lock_settings_and_unlock(self):
        self.settings()
        click_text(self.page, 'Screen\nLock')
        self.assertIn('Show lock button', [obj['text'] for obj in objects(self.page)])
        click_text(self.page, 'Lock now', contains=True)
        self.assertIn('Slide to unlock', ' '.join(obj['text'] for obj in objects(self.page)))
        slider = next(obj for obj in objects(self.page) if obj['binding'].startswith('ui.lock_screen_activate.slider.'))
        canvas_element = self.page.locator('#display')
        canvas = canvas_element.bounding_box()
        size = canvas_element.evaluate('(c) => ({width:c.width,height:c.height})')
        def point(x, y):
            return canvas['x'] + x * canvas['width'] / size['width'], canvas['y'] + y * canvas['height'] / size['height']
        self.page.mouse.move(*point(slider['x'] + 16, slider['y'] + slider['height'] / 2))
        self.page.mouse.down()
        self.page.mouse.move(*point(slider['x'] + slider['width'] - 8,
                                    slider['y'] + slider['height'] / 2), steps=12)
        self.page.mouse.up()
        self.assertNotIn('Slide to unlock', ' '.join(obj['text'] for obj in objects(self.page)))

    def test_screen_lock_switches_persist_and_reset(self):
        self.settings()
        click_text(self.page, 'Screen\nLock')
        for prefix in ('ui.show_screen_lock_popup.sw.', 'ui.show_screen_lock_popup.auto_sw.'):
            click_object(self.page, next(obj for obj in objects(self.page) if obj['binding'].startswith(prefix)))
        self.assertEqual(self.page.evaluate("[emulatorSettings.get('scr_lock', 1), emulatorSettings.get('scr_autolock', 0)]"), [0, 1])
        self.page.reload()
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        self.assertEqual(self.page.evaluate("[emulatorSettings.get('scr_lock', 1), emulatorSettings.get('scr_autolock', 0)]"), [0, 1])
        self.page.locator('#reset').click()
        self.page.wait_for_url('**/?rotation=0')
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        self.assertEqual(self.page.evaluate("[emulatorSettings.get('scr_lock', 1), emulatorSettings.get('scr_autolock', 0)]"), [1, 0])


if __name__ == '__main__':
    unittest.main()
