"""Pixie Dust action uses the native scan screen and an offline JanOS stream."""
import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tests'))


class PixieBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from emulator_browser import browser_session
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        from emulator_browser import click_text
        self.context = self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(self.page, 'WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('globalThis.emulatorModelErrors'), [])

    def text(self):
        from emulator_browser import objects
        return '\n'.join(item['text'] for item in objects(self.page))

    def tick(self, count=40):
        self.page.evaluate('(count)=>{for(let i=0;i<count;i++)emulator.module._emu_tick(100)}', count)

    def click(self, label):
        from emulator_browser import click_object, objects
        for _ in range(12):
            matches = [item for item in objects(self.page) if item['text'] == label]
            self.assertTrue(matches, f'Missing native control: {label}')
            item = matches[-1]
            canvas = self.page.locator('#display')
            size = canvas.evaluate('(c)=>({w:c.width,h:c.height})')
            x = item['x'] + item['width'] / 2
            y = item['y'] + item['height'] / 2
            if 0 < x < size['w'] and 0 < y < size['h']:
                click_object(self.page, item)
                self.page.evaluate('emulator.module._emu_tick(100)')
                return
            box = canvas.bounding_box()
            y = box['y'] + max(10, min(size['h'] - 10, y)) * box['height'] / size['h']
            start, end = (.65, .35) if x >= size['w'] else (.35, .65)
            self.page.mouse.move(box['x'] + box['width'] * start, y)
            self.page.mouse.down()
            self.page.mouse.move(box['x'] + box['width'] * end, y, steps=12)
            self.page.mouse.up()
            self.page.wait_for_timeout(200)
        self.fail(f'Native control unreachable: {label}')

    def test_selected_ap_reaches_verified_result_and_secret_only_on_pixie_page(self):
        from emulator_browser import click_object, objects
        target = [item for item in objects(self.page)
                  if item['binding'].endswith('02:20:77:00:00:01/select')]
        self.assertEqual(len(target), 1)
        click_object(self.page, target[0])
        self.click('Pixie Dust')
        self.assertIn('NEON-BAZAAR', self.text())
        self.tick()
        self.assertIn('PIN and M7 verified', self.text())
        self.assertIn('PIN: 12345670', self.text())
        self.assertIn('Key: example-key', self.text())
        self.click('ESC / Stop and back')
        self.tick()
        self.assertNotIn('Key: example-key', self.text())
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

    def test_pixie_requires_exactly_one_selected_ap(self):
        from emulator_browser import click_object, objects
        self.click('Pixie Dust')
        self.assertIn('Select exactly one AP', self.text())
        self.assertNotIn('ESC / Stop and back', self.text())
        for bssid in ('02:20:77:00:00:01', '02:20:77:00:00:02'):
            target = [item for item in objects(self.page)
                      if item['binding'].endswith(f'{bssid}/select')]
            self.assertEqual(len(target), 1)
            click_object(self.page, target[0])
        self.click('Pixie Dust')
        self.assertIn('Select exactly one AP', self.text())
        self.assertNotIn('ESC / Stop and back', self.text())
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

    def test_stop_during_capture_waits_for_release_and_shows_no_secret(self):
        from emulator_browser import click_object, objects
        target = [item for item in objects(self.page)
                  if item['binding'].endswith('02:20:77:00:00:01/select')]
        self.assertEqual(len(target), 1)
        click_object(self.page, target[0])
        self.click('Pixie Dust')
        self.assertIn('ESC / Stop and back', self.text())
        self.click('ESC / Stop and back')
        self.tick(10)
        self.assertNotIn('PIN: 12345670', self.text())
        self.assertNotIn('ESC / Stop and back', self.text())
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))


if __name__ == '__main__':
    unittest.main()
