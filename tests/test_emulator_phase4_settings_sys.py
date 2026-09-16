"""Native offline system acceptance; rebuild the Wasm artifact first."""
import unittest
from emulator_browser import browser_session, objects, click_object, click_text
import test_emulator_phase4_pcap_deep as navigation


class SettingsSystemBrowser(unittest.TestCase):
    # Reuse the clipped-parent scrolling helper, not direct callback dispatch.
    click = navigation.PcapDeepBrowser.click
    click_label = navigation.PcapDeepBrowser.click_label
    wait_text = navigation.PcapDeepBrowser.wait_text

    def setUp(self):
        self.session = browser_session()
        self.browser, url = self.session.__enter__()
        self.addCleanup(self.session.__exit__, None, None, None)
        self.context = self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda e: self.errors.append(str(e)))
        self.page.goto(url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate('emulator.module._emu_set_timing(1)')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page, 'INTERNAL')

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unavailable'), [])

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(10)')

    def advance(self):
        self.page.evaluate('()=>{for(let i=0;i<40;i++)emulator.module._emu_tick(100)}')

    def text(self):
        return '\n'.join(o['text'] for o in objects(self.page))

    def status(self, module='grove'):
        return self.page.evaluate('(id)=>emulator.device.device.systemStatus(id)', module)

    def settings(self):
        self.click_label('Settings')

    def test_status_reboot_preserves_sd_and_settings(self):
        self.page.evaluate("emulator.device.device.installPcapFixtures('grove');emulatorSettings.set('scr_bright',37)")
        files = self.page.evaluate("emulator.device.device.files('grove')")
        self.click_label('Module\nStatus')
        self.wait_text('Firmware: demo-main-1')
        self.click('emu.phase46.system.reboot.grove')
        self.advance()
        self.assertEqual(self.status()['reboots'], 1)
        self.assertEqual(self.status('mbus')['reboots'], 0)
        self.assertIn('Reboots: 1', self.text())
        self.assertEqual(self.page.evaluate("emulator.device.device.files('grove')"), files)
        self.page.reload()
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate('globalThis.unavailable=[]')
        self.assertEqual(self.page.evaluate('emulator.module._emu_brightness()'), 37)

    def test_status_disconnect_rejects_reboot(self):
        self.click_label('Module\nStatus')
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.tick()
        self.click('emu.phase46.system.reboot.grove')
        self.assertIn('Module disconnected', self.text())
        self.assertIsNone(self.status()['active'])
        self.click('emu.phase46.system.back')
        self.click_label('Module\nStatus')
        self.wait_text('Disconnected')

    def test_sd_admin_start_back_and_sd_failure(self):
        self.settings()
        self.click('/Monster SD Admin')
        self.click_label('Quick Start')
        self.tick()
        self.assertIsNotNone(self.status()['active'])
        self.wait_text('Offline simulation running')
        self.click('show_sd_admin_page.back')
        self.click_label('Stay here')
        self.assertIsNotNone(self.status()['active'])
        self.click('show_sd_admin_page.back')
        self.click_label('Stop and back')
        self.assertIsNone(self.status()['active'])
        self.click('/Monster SD Admin')
        self.click_label('Quick Start')
        self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:false})")
        self.tick()
        self.assertIsNone(self.status()['active'])
        self.wait_text('sd_missing')

    def test_update_success_and_reopen_info(self):
        self.settings()
        self.click('/Monster OTA')
        self.click('show_ota_page.check_btn')
        self.advance()
        self.assertEqual(self.status()['version'], 'demo-main-2')
        self.assertIn('completed', self.text())
        self.click_label('Close')
        self.click('show_ota_page.back')
        self.click('/Monster OTA')
        self.assertIn('demo-main-2', self.text())

    def test_update_failure_and_cancel_leave_version(self):
        self.settings()
        self.click('/Monster OTA')
        self.page.evaluate("globalThis.emulatorSystemOutcome='failure'")
        self.click('show_ota_page.check_btn')
        self.advance()
        self.assertEqual(self.status()['version'], 'demo-main-1')
        self.assertIn('simulated_update_failed', self.text())
        self.click_label('Close')
        self.page.evaluate("globalThis.emulatorSystemOutcome='success'")
        self.click('show_ota_page.check_btn')
        self.click_label('Cancel simulated update')
        self.advance()
        self.assertEqual(self.status()['version'], 'demo-main-1')
        self.assertIsNone(self.status()['active'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
