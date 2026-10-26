"""Native OTA variant and explicit-release browser journey. Rebuild Wasm first."""
import unittest
from emulator_browser import browser_session, objects, click_text
from test_emulator_phase2 import position
import test_emulator_phase4_pcap_deep as navigation


class OtaVariantBrowser(unittest.TestCase):
    click = navigation.PcapDeepBrowser.click
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
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page, 'INTERNAL')
        self.tick()
        self.tick()
        self.click('/Settings')
        self.tick()
        self.wait_text('Monster\nOTA')
        self.click('/Monster OTA')
        self.wait_text('Firmware Variant')

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unavailable'), [])

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def advance(self, ms):
        self.page.evaluate('(ms)=>{for(let t=0;t<ms;t+=100)emulator.module._emu_tick(100)}', ms)

    def text(self):
        return '\n'.join(row['text'] for row in objects(self.page))

    def use_rf_module(self):
        self.click('show_ota_page.back')
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.click('/Monster OTA')

    def test_rf_choice_force_catalog_confirmation_and_reinstall(self):
        self.use_rf_module()
        self.click('emu.ota.variant')
        option = next(row for row in objects(self.page)
                      if row['text'] == 'Auto-detect\nClassic\nRF (Monster RF)')
        self.page.mouse.click(*position(self.page, option, .5, 2.5 / 3))
        self.tick()
        self.click('show_ota_page.check_btn')
        self.wait_text('RF variant')
        self.advance(4000)
        self.assertEqual(self.page.evaluate("emulator.device.device.systemStatus('mbus').version"),
                         'demo-rf-main-2')
        self.click('emu.phase46.ota.monitor.close')
        self.click('emu.ota.force')
        self.click('show_ota_page.check_btn')
        self.wait_text('Offline demo releases')
        self.click('emu.ota.release.demo-rf-main-2')
        self.wait_text('Reinstall demo-rf-main-2?')
        self.click('emu.ota.confirm.cancel')
        self.assertEqual(self.page.evaluate("emulator.device.device.systemStatus('mbus').reboots"), 1)
        self.click('emu.ota.release.demo-rf-main-2')
        self.click('emu.ota.confirm.yes')
        self.advance(4000)
        status=self.page.evaluate("emulator.device.device.systemStatus('mbus')")
        self.assertEqual((status['version'],status['reboots']),('demo-rf-main-2',2))

    def test_auto_detect_uses_rf_capable_mbus_module(self):
        self.use_rf_module()
        self.click('show_ota_page.check_btn')
        self.advance(4000)
        grove=self.page.evaluate("emulator.device.device.systemStatus('grove')")
        mbus=self.page.evaluate("emulator.device.device.systemStatus('mbus')")
        self.assertEqual(grove['version'],'demo-main-1')
        self.assertEqual((mbus['version'],mbus['firmwareVariant']),('demo-rf-main-2','rf'))

    def test_rf_choice_on_classic_module_is_blocked_before_install(self):
        self.click('emu.ota.variant')
        option = next(row for row in objects(self.page)
                      if row['text'] == 'Auto-detect\nClassic\nRF (Monster RF)')
        self.page.mouse.click(*position(self.page, option, .5, 2.5 / 3))
        self.tick()
        self.click('show_ota_page.check_btn')
        self.wait_text('Firmware variant blocked')
        self.assertEqual(self.page.evaluate("emulator.device.device.systemStatus('grove').active"), None)
        self.assertEqual(self.page.evaluate("emulator.device.device.systemStatus('grove').version"),
                         'demo-main-1')


if __name__ == '__main__':
    unittest.main(verbosity=2)
