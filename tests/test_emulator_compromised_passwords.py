"""Native saved password and portal lists backed by volatile browser files."""
import unittest

from emulator_browser import browser_session, objects, click_object, click_text


class CompromisedBrowser(unittest.TestCase):
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
        self.page.add_init_script("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')

    def tearDown(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unavailable'), [])

    def text(self):
        return '\n'.join(obj['text'] for obj in objects(self.page))

    def click_binding(self, needle):
        matches = [obj for obj in objects(self.page) if needle in obj['binding']]
        self.assertEqual(len(matches), 1, (needle, [obj['binding'] for obj in objects(self.page) if 'evil_twin_passwords_page.row' in obj['binding']]))
        click_object(self.page, matches[0])
        self.page.evaluate('emulator.module._emu_tick(100)')

    def seed(self, tab='grove'):
        self.page.evaluate("""tab => {
          const d=emulator.device.device, enc=new TextEncoder();
          d.commitFileChanges(tab,[
            {path:'/sdcard/lab/eviltwin.txt',bytes:enc.encode('"NEON-BAZAAR", "demo-only-pass"\\n"LANTERN-LAB", "second-demo"\\n')},
            {path:'/sdcard/lab/portals.txt',bytes:enc.encode('"NEON-BAZAAR", "username=demo", "password=demo-only-pass"\\n')},
          ]);
        }""", tab)

    def open_category(self, category):
        click_text(self.page, 'Compromised\nData')
        click_text(self.page, category)

    def test_empty_and_populated_saved_passwords_with_connect_and_back(self):
        self.open_category('Evil Twin\nPasswords')
        self.assertIn('Found 0 password(s)', self.text())
        self.click_binding('show_evil_twin_passwords_page.back_btn')
        self.seed()
        click_text(self.page, 'Evil Twin\nPasswords')
        self.assertIn('Found 2 password(s)', self.text())
        self.assertIn('Password: second-demo', self.text())
        self.click_binding('saved-passwords/Grove/LANTERN-LAB/')
        self.assertIn('Connect to Network?', self.text())
        self.assertIn('SSID: LANTERN-LAB', self.text())
        click_text(self.page, 'Cancel')
        self.assertNotIn('Connect to Network?', self.text())
        self.click_binding('saved-passwords/Grove/NEON-BAZAAR/')
        click_text(self.page, 'Connect')
        self.assertIn('Target: NEON-BAZAAR', self.text())
        self.clean()

    def test_portal_submissions_are_module_scoped_and_delete_refreshes(self):
        self.seed()
        self.open_category('Portal\nData')
        self.assertIn('Found 1 submission(s)', self.text())
        self.assertIn('username=demo', self.text())
        self.click_binding('show_portal_data_page.back_btn')
        self.click_binding('show_compromised_data_page.back_btn')
        click_text(self.page, 'MBUS')
        self.open_category('Portal\nData')
        self.assertIn('Found 0 submission(s)', self.text())
        self.click_binding('show_portal_data_page.back_btn')
        self.click_binding('show_compromised_data_page.back_btn')
        click_text(self.page, 'GROVE')
        self.open_category('Portal\nData')
        self.assertIn('Found 1 submission(s)', self.text())
        self.clean()

    def test_portal_delete_confirms_then_refreshes_native_list(self):
        self.seed()
        self.open_category('Portal\nData')
        self.click_binding('compromised_add_single_file_controls.delete_btn')
        self.assertIn('DELETE THIS portal FILE', self.text())
        self.click_binding('show_compromised_delete_confirm.no_btn')
        self.assertIn('Found 1 submission(s)', self.text())
        self.assertTrue(self.page.evaluate("emulator.device.device.files('grove').some(f=>f.path.endsWith('portals.txt'))"))
        self.click_binding('compromised_add_single_file_controls.delete_btn')
        self.click_binding('show_compromised_delete_confirm.yes_btn')
        self.assertIn('Deleted: 1, failed: 0', self.text())
        self.click_binding('show_compromised_cleanup_popup.ctx--compromised_cleanup_close_btn')
        self.assertIn('Found 0 submission(s)', self.text())
        self.assertFalse(self.page.evaluate("emulator.device.device.files('grove').some(f=>f.path.endsWith('portals.txt'))"))
        self.clean()

    def test_saved_row_keeps_its_module_after_switching_tabs(self):
        self.seed()
        self.open_category('Evil Twin\nPasswords')
        self.assertIn('Found 2 password(s)', self.text())
        click_text(self.page, 'MBUS')
        self.page.evaluate("""() => emulator.device.device.commitFileChanges('mbus',[
          {path:'/sdcard/lab/eviltwin.txt',bytes:new TextEncoder().encode('"MBUS-DEMO", "mbus-only"\\n')}
        ])""")
        self.open_category('Evil Twin\nPasswords')
        self.assertIn('Found 1 password(s)', self.text())
        click_text(self.page, 'GROVE')
        self.assertIn('Found 2 password(s)', self.text())
        self.click_binding('saved-passwords/Grove/LANTERN-LAB/')
        self.assertIn('SSID: LANTERN-LAB', self.text())
        self.assertIn('Password: second-demo', self.text())
        self.clean()

    def test_disconnected_module_hides_saved_data_until_reconnection(self):
        self.seed()
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.open_category('Evil Twin\nPasswords')
        self.assertIn('Found 0 password(s)', self.text())
        self.click_binding('show_evil_twin_passwords_page.back_btn')
        click_text(self.page, 'Portal\nData')
        self.assertIn('Found 0 submission(s)', self.text())
        self.click_binding('show_portal_data_page.back_btn')
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:true})")
        click_text(self.page, 'Evil Twin\nPasswords')
        self.assertIn('Found 2 password(s)', self.text())
        self.click_binding('show_evil_twin_passwords_page.back_btn')
        click_text(self.page, 'Portal\nData')
        self.assertIn('Found 1 submission(s)', self.text())

    def test_evil_twin_clean_confirms_and_refreshes(self):
        self.seed()
        self.open_category('Evil Twin\nPasswords')
        self.click_binding('compromised_add_single_file_controls.clean_btn')
        self.assertIn('eviltwin.txt', self.text())
        self.click_binding('show_compromised_delete_confirm.yes_btn')
        self.assertIn('Deleted: 1, failed: 0', self.text())
        self.click_binding('show_compromised_cleanup_popup.ctx--compromised_cleanup_close_btn')
        self.assertIn('Found 0 password(s)', self.text())
        self.clean()

    def clean(self):
        self.assertEqual(self.errors, [])
        self.assertEqual(self.page.evaluate('unavailable'), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
