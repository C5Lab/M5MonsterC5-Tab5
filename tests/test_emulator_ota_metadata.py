"""Browser regression for the opt-in RF OTA metadata diagnostic fixture."""

import unittest

from emulator_browser import browser_session, click_text, objects
import test_emulator_phase4_pcap_deep as navigation


class OtaMetadataBrowser(unittest.TestCase):
    click = navigation.PcapDeepBrowser.click

    def setUp(self):
        self.session = browser_session()
        self.browser, self.url = self.session.__enter__()
        self.addCleanup(self.session.__exit__, None, None, None)
        self.context = self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))

    def tearDown(self):
        self.assertEqual(self.errors, [])

    def load(self, condition=''):
        suffix = f'/?moduleCondition={condition}' if condition else '/'
        self.page.goto(self.url + suffix)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')

    def text(self):
        return '\n'.join(row['text'] for row in objects(self.page))

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def open_info(self):
        click_text(self.page, 'INTERNAL')
        self.page.evaluate('emulator.module._emu_tick(10)')
        self.click('/Settings')
        self.page.evaluate('emulator.module._emu_tick(10)')
        self.click('/Monster OTA')
        self.click('show_ota_page.info_btn')

    def advance(self, ms):
        self.page.evaluate('(ms)=>{for(let t=0;t<ms;t+=100)emulator.module._emu_tick(100)}', ms)

    def test_invalid_metadata_is_visible_without_changing_boot_or_forcing_update(self):
        self.load('otadata-invalid')
        grove = self.page.evaluate("emulator.device.device.systemStatus('grove')")
        mbus = self.page.evaluate("emulator.device.device.systemStatus('mbus')")
        self.assertFalse(grove['connected'])
        self.assertEqual((mbus['firmwareVariant'], mbus['otaDataStatus']), ('rf', 'invalid'))
        self.assertEqual((mbus['version'], mbus['activeSlot'], mbus['reboots']),
                         ('demo-rf-main-3', 0, 0))

        self.open_info()
        info = self.text()
        self.assertIn('ota data invalid, no current app. Assuming factory', info)
        self.assertIn('State unavailable', info)
        self.assertIn('Slot ota_0', info)
        self.assertIn('Slot ota_1', info)
        self.assertIn('demo-rf-main-3', info)
        self.assertEqual(self.page.evaluate("emulator.device.device.systemStatus('mbus').reboots"), 0)

        self.click('emu.phase46.ota.monitor.close')
        self.click('show_ota_page.check_btn')
        self.advance(4000)
        self.assertIn('No newer release', self.text())
        after = self.page.evaluate("emulator.device.device.systemStatus('mbus')")
        self.assertEqual((after['version'], after['activeSlot'], after['reboots'],
                          after['otaDataStatus']), ('demo-rf-main-3', 0, 0, 'invalid'))

        self.load()
        self.open_info()
        self.assertNotIn('ota data invalid', self.text())


if __name__ == '__main__':
    unittest.main(verbosity=2)
