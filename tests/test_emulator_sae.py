"""SAE popup uses the native Canvas flow and an offline model operation."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object


class SaeBrowser(unittest.TestCase):
    def setUp(self):
        self.session = browser_session()
        self.browser, self.url = self.session.__enter__()
        self.addCleanup(self.session.__exit__, None, None, None)
        self.context = self.browser.new_context(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.add_init_script("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")

    def ready(self):
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')

    def scan(self, rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}'); self.ready()
        click_text(self.page, 'WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        nets = self.page.evaluate('emulator.device.snapshot(0).networks')
        target = nets[0]
        self.select(target['bssid'])
        return target

    def select(self, bssid):
        click_object(self.page, next(o for o in objects(self.page) if o['binding'].endswith(bssid + '/select')))

    def text(self):
        return '\n'.join(o['text'] for o in objects(self.page))

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def test_native_popup_target_stop_and_reopen_in_four_rotations(self):
        p = self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                target = self.scan(rotation)
                click_text(p, 'SAE Overflow')
                self.assertIn('SAE Overflow Active', self.text())
                self.assertIn(target['bssid'], self.text())
                job = p.evaluate('emulator.device.snapshot(0).active')
                self.assertIsNotNone(job)
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).operation', job), 'sae_overflow')
                click_text(p, 'STOP')
                self.assertNotIn('SAE Overflow Active', self.text())
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state', job), 'completed')
                click_text(p, 'SAE Overflow')
                self.assertNotEqual(p.evaluate('emulator.device.snapshot(0).active'), job)
                click_text(p, 'STOP')
                self.assertEqual(p.evaluate('unavailable'), [])
        self.assertEqual(self.errors, [])

    def test_invalid_selection_disconnect_and_reset(self):
        p = self.page
        self.scan()
        second = p.evaluate('emulator.device.snapshot(0).networks[1].bssid')
        self.select(second); click_text(p, 'SAE Overflow')
        self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
        self.assertIn('Select exactly 1 network', self.text())
        self.select(second); click_text(p, 'SAE Overflow')
        job = p.evaluate('emulator.device.snapshot(0).active')
        self.assertIsNotNone(job, 'SAE action must start an offline operation')
        p.evaluate("emulator.device.device.setModule('grove',{connected:false})"); self.tick()
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state', job), 'cancelled')
        self.assertNotIn('SAE Overflow Active', self.text())
        click_text(p, 'STOP')
        click_text(p, 'SAE Overflow')
        self.assertIn('disconnected', self.text().lower())
        self.assertNotIn('SAE Overflow Active', self.text())
        p.evaluate("emulator.device.device.setModule('grove',{connected:true})")
        click_text(p, 'SAE Overflow')
        with p.expect_navigation(): p.locator('#reset').click()
        self.ready()
        self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
        self.assertNotIn('SAE Overflow Active', self.text())
        self.assertEqual(p.evaluate('unavailable'), [])
        self.assertEqual(self.errors, [])

    def test_two_modules_stop_and_shell_cancel_are_isolated(self):
        p = self.page
        self.scan(); click_text(p, 'SAE Overflow')
        grove = p.evaluate('emulator.device.snapshot(0).active')
        click_text(p, 'MBUS'); click_text(p, 'WiFi Scan\n& Attack')
        p.wait_for_function('emulator.module._emu_scan_state(2)===2')
        self.select(p.evaluate('emulator.device.snapshot(2).networks[0].bssid'))
        click_text(p, 'SAE Overflow')
        mbus = p.evaluate('emulator.device.snapshot(2).active')
        self.assertIsNotNone(mbus)
        self.assertNotEqual(mbus, grove)
        click_text(p, 'STOP')
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state', mbus), 'completed')
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state', grove), 'running')
        click_text(p, 'GROVE')
        self.assertIn('SAE Overflow Active', self.text())
        p.locator('#cancel').click(); self.tick()
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state', grove), 'cancelled')
        self.assertNotIn('SAE Overflow Active', self.text())
        click_text(p, 'STOP')
        click_text(p, 'SAE Overflow')
        self.assertIsNotNone(p.evaluate('emulator.device.snapshot(0).active'))
        click_text(p, 'STOP')
        self.assertEqual(p.evaluate('unavailable'), [])
        self.assertEqual(self.errors, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
