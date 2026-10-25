"""Browser acceptance for the retained Settings -> Wi-Fi MAC screen.

Run after rebuilding the emulator so the native LVGL screen and the virtual
JanOS command adapter are present in tools/ui_emulator/dist.
"""
import json
import unittest

from emulator_browser import browser_session, objects, click_text
from test_emulator_phase2 import position
import test_emulator_phase4_pcap_deep as navigation


PORT_OPTIONS = 'Select port\nGrove\nUSB\nMBus'
POLICY_OPTIONS = 'Choose policy\nFactory\nRandom\nFixed'


class StaMacBrowser(unittest.TestCase):
    # Navigate through clipped LVGL parents with real pointer events.
    click = navigation.PcapDeepBrowser.click
    click_label = navigation.PcapDeepBrowser.click_label

    @classmethod
    def setUpClass(cls):
        cls.session = browser_session()
        cls.browser, cls.url = cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None, None, None)

    def setUp(self):
        self.context = self.browser.new_context(viewport={'width': 1440, 'height': 1000})
        self.addCleanup(self.context.close)
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda error: self.errors.append(str(error)))
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page, 'INTERNAL')
        click_text(self.page, 'Settings')

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unavailable'), [])

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def text(self):
        return '\n'.join(obj['text'] for obj in objects(self.page))

    def wait_text(self, needle):
        for _ in range(100):
            if needle in self.text():
                return
            self.tick()
            self.page.wait_for_timeout(20)
        self.fail(f'Native text missing: {needle}\n{self.text()}')

    def status_value(self, heading):
        rows = objects(self.page)
        key = next(obj for obj in rows if obj['text'] == heading)
        values = [obj['text'] for obj in rows if obj['parent'] == key['parent']
                  and obj['id'] != key['id']]
        self.assertEqual(len(values), 1, (heading, values))
        return values[0]

    def wait_value(self, heading, expected):
        for _ in range(100):
            if self.status_value(heading) == expected:
                return
            self.tick()
            self.page.wait_for_timeout(20)
        self.fail(f'{heading}: expected {expected!r}, got {self.status_value(heading)!r}')

    def connected(self, *ports):
        self.page.evaluate('''ports => {
            for (const id of ['grove', 'usb', 'mbus'])
                emulator.device.device.setModule(id, {connected: ports.includes(id)});
        }''', list(ports))

    def status(self, port='grove'):
        line = self.page.evaluate('''port => emulator.device.device.staMacCommand(
            port, 'sta_mac status')''', port)
        return json.loads(line)

    def open_mac(self):
        click_text(self.page, 'Wi-Fi\nMAC')
        self.wait_text('This JanOS device')

    def choose(self, binding, options, index):
        self.click(binding)
        matches = [obj for obj in objects(self.page) if obj['text'] == options]
        self.assertEqual(len(matches), 1, (binding, matches))
        self.page.mouse.click(*position(self.page, matches[0], .5,
                                        (index + .5) / len(options.splitlines())))
        self.tick()

    def choose_policy(self, index):
        self.choose('sta_mac_ui.mode_dropdown', POLICY_OPTIONS, index)

    def save(self):
        self.click('sta_mac_ui.apply_button')

    def test_unique_monster_is_selected_and_status_read_automatically(self):
        for port in ('grove', 'usb', 'mbus'):
            with self.subTest(port=port):
                self.connected(port)
                if port == 'grove':
                    self.open_mac()
                else:
                    self.click('show_sta_mac_settings_page.back.')
                    self.open_mac()
                expected = self.status(port)
                self.wait_value('Effective start MAC', expected['effective_boot_mac'])
                self.assertEqual(self.status_value('Saved for next start'), 'factory')
                self.assertEqual(self.status_value('Policy this start'), 'factory')
                self.assertEqual(self.status_value('NVS record'), expected['record_health'])

    def test_grove_only_demo_option_autoreads_without_devtools(self):
        self.page.goto(f'{self.url}/?moduleCondition=grove-only')
        self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        self.assertEqual(self.page.locator('#module-condition').input_value(), 'grove-only')
        self.assertFalse(self.page.evaluate(
            "emulator.device.device.systemStatus('mbus').connected"))
        click_text(self.page, 'INTERNAL')
        click_text(self.page, 'Settings')
        self.open_mac()
        self.wait_value('Effective start MAC',
                        self.status('grove')['effective_boot_mac'])

    def test_no_device_and_multiple_devices_require_a_port_choice(self):
        self.connected()
        self.open_mac()
        self.wait_text('No connected JanOS device detected')
        self.assertEqual(self.status_value('Saved for next start'), 'Not loaded')

        self.click('show_sta_mac_settings_page.back.')
        self.connected('grove', 'mbus')
        self.open_mac()
        self.wait_text('Multiple JanOS devices detected')
        self.assertEqual(self.status_value('Saved for next start'), 'Not loaded')
        self.choose('sta_mac_ui.target_dropdown', PORT_OPTIONS, 3)
        self.wait_value('Effective start MAC', self.status('mbus')['effective_boot_mac'])

    def test_fixed_random_factory_save_only_for_next_janos_start(self):
        self.connected('grove')
        self.open_mac()
        initial = self.status()
        self.wait_value('Effective start MAC', initial['effective_boot_mac'])

        self.choose_policy(3)
        self.click('sta_mac_ui.fixed_input')
        self.page.keyboard.type('02:11:22:33:44:55')
        self.page.keyboard.press('Enter')
        self.save()
        self.wait_value('Saved for next start', 'fixed 02:11:22:33:44:55')
        pending = self.status()
        self.assertEqual(pending['configured_mode'], 'fixed')
        self.assertEqual(pending['boot_mode'], initial['boot_mode'])
        self.assertEqual(pending['effective_boot_mac'], initial['effective_boot_mac'])
        self.assertTrue(pending['pending_reboot'])

        self.page.evaluate("emulator.device.device.systemStart('grove','reboot')")
        self.page.evaluate('emulator.device.device.advance(1000)')
        self.click('sta_mac_ui.refresh_button')
        self.wait_value('Policy this start', 'fixed')
        after_restart = self.status()
        self.assertEqual(after_restart['effective_boot_mac'], '02:11:22:33:44:55')
        self.assertFalse(after_restart['pending_reboot'])

        self.choose_policy(2)
        self.save()
        self.wait_value('Saved for next start', 'random')
        self.assertEqual(self.status()['effective_boot_mac'], '02:11:22:33:44:55')
        self.choose_policy(1)
        self.save()
        self.wait_value('Saved for next start', 'factory')
        self.assertEqual(self.status()['effective_boot_mac'], '02:11:22:33:44:55')

    def test_invalid_fixed_mac_does_not_change_saved_policy(self):
        self.connected('grove')
        self.open_mac()
        self.wait_value('Saved for next start', 'factory')
        before = self.status()

        self.choose_policy(3)
        self.click('sta_mac_ui.fixed_input')
        self.page.keyboard.type('01:11:22:33:44:55')
        self.page.keyboard.press('Enter')
        self.save()
        self.wait_text('first octet must be local and unicast')
        self.assertEqual(self.status(), before)

    def test_busy_radio_allows_status_refresh_but_rejects_random_save(self):
        self.connected('grove')
        self.open_mac()
        self.wait_value('Saved for next start', 'factory')
        before = self.status()

        job_id = self.page.evaluate(
            "emulator.device.device.systemStart('grove', 'sd_admin')")
        self.choose_policy(2)
        self.save()
        self.wait_text('radio_lease')
        self.assertEqual(self.status()['configured_mode'], 'factory')
        self.assertEqual(self.status_value('Saved for next start'), 'factory')

        self.click('sta_mac_ui.refresh_button')
        self.wait_text('Status read from the selected virtual JanOS device.')
        self.assertEqual(self.status_value('Saved for next start'), 'factory')
        self.assertEqual(self.status_value('Effective start MAC'),
                         before['effective_boot_mac'])
        self.assertEqual(self.status(), before)
        self.assertEqual(self.page.evaluate('''id =>
            emulator.device.device.job(id).state''', job_id), 'running')

    def test_saving_factory_on_factory_boot_needs_no_restart(self):
        self.connected('grove')
        self.open_mac()
        self.wait_value('Saved for next start', 'factory')
        self.assertEqual(self.status_value('Policy this start'), 'factory')

        self.choose_policy(1)
        self.save()
        self.wait_text('Virtual policy saved. No reboot is needed for this policy.')
        self.assertEqual(self.status_value('Saved for next start'), 'factory')
        self.assertEqual(self.status_value('Restart required'), 'No')
        self.assertFalse(self.status()['pending_reboot'])

    def test_usb_fixed_policy_applies_after_module_status_reboot(self):
        self.connected('usb')
        self.open_mac()
        initial = self.status('usb')
        self.wait_value('Effective start MAC', initial['effective_boot_mac'])

        self.choose_policy(3)
        self.click('sta_mac_ui.fixed_input')
        self.page.keyboard.type('02:22:33:44:55:66')
        self.page.keyboard.press('Enter')
        self.save()
        self.wait_value('Saved for next start', 'fixed 02:22:33:44:55:66')
        self.assertEqual(self.status_value('Restart required'), 'Yes')
        self.assertEqual(self.status('usb')['effective_boot_mac'],
                         initial['effective_boot_mac'])

        self.click('show_sta_mac_settings_page.back.')
        self.click('show_settings_page.back_btn.')
        self.click_label('Module\nStatus')
        self.wait_text('USB - Connected')
        self.click('emu.phase46.system.reboot.usb')
        self.page.evaluate('''() => {
            for (let i = 0; i < 40; i++) emulator.module._emu_tick(100);
        }''')
        self.assertEqual(self.page.evaluate(
            "emulator.device.device.systemStatus('usb').reboots"), 1)

        self.click('emu.phase46.system.back')
        self.click_label('Settings')
        self.open_mac()
        self.wait_value('Effective start MAC', '02:22:33:44:55:66')
        self.assertEqual(self.status_value('Policy this start'), 'fixed')
        self.assertEqual(self.status_value('Restart required'), 'No')
        self.assertFalse(self.status('usb')['pending_reboot'])

    def test_pending_fixed_survives_page_reload_and_reset_clears_it(self):
        self.connected('grove')
        self.open_mac()
        initial = self.status('grove')
        self.wait_value('Effective start MAC', initial['effective_boot_mac'])

        fixed = '02:44:55:66:77:88'
        self.choose_policy(3)
        self.click('sta_mac_ui.fixed_input')
        self.page.keyboard.type(fixed)
        self.page.keyboard.press('Enter')
        self.save()
        self.wait_value('Saved for next start', f'fixed {fixed}')
        pending = self.status('grove')
        self.assertTrue(pending['pending_reboot'])
        self.assertEqual(pending['effective_boot_mac'], initial['effective_boot_mac'])
        self.assertEqual(pending['active_sta_mac'], initial['active_sta_mac'])

        def reopen_grove():
            self.page.wait_for_function('globalThis.emulator?.measurements.frames > 2')
            self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
            click_text(self.page, 'INTERNAL')
            click_text(self.page, 'Settings')
            self.open_mac()
            # The normal seed has Grove and MBus connected again after reload.
            self.choose('sta_mac_ui.target_dropdown', PORT_OPTIONS, 1)

        self.page.reload()
        reopen_grove()
        self.wait_value('Saved for next start', f'fixed {fixed}')
        self.assertEqual(self.status_value('Policy this start'), 'factory')
        self.assertEqual(self.status_value('Effective start MAC'),
                         initial['effective_boot_mac'])
        self.assertEqual(self.status_value('Restart required'), 'Yes')
        self.assertEqual(self.status('grove'), pending)

        with self.page.expect_navigation():
            self.page.locator('#reset').click()
        reopen_grove()
        self.wait_value('Saved for next start', 'factory')
        reset = self.status('grove')
        self.assertEqual(reset['record_health'], 'missing')
        self.assertEqual(reset['boot_mode'], 'factory')
        self.assertEqual(reset['effective_boot_mac'], initial['effective_boot_mac'])
        self.assertFalse(reset['pending_reboot'])
        self.assertEqual(self.status_value('Restart required'), 'No')


if __name__ == '__main__':
    unittest.main(verbosity=2)
