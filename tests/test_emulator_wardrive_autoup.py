"""Browser coverage for the local Wardrive auto-upload result flow."""
import unittest

from emulator_browser import click_text, objects
import test_emulator_phase4_wardrive as wardrive
import test_emulator_portal_demo as portal


class WardriveAutoUploadBrowser(unittest.TestCase):
    setUp = portal.PortalDemoBrowser.setUp
    load = portal.PortalDemoBrowser.load
    ready = portal.PortalDemoBrowser.ready
    text = portal.PortalDemoBrowser.text
    advance = portal.PortalDemoBrowser.advance
    clean = portal.PortalDemoBrowser.clean
    click = wardrive.WardriveBrowser.click
    state = wardrive.WardriveBrowser.state

    def preference_checked(self, suffix):
        binding = f'wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_{suffix}'
        row = next(item for item in objects(self.page) if binding in item['binding'])
        return bool(row['state'] & 1)

    def start_home_upload(self, outcome, poweroff=False, rotation=0,
                          archive=False, wars=False):
        self.load(rotation)
        click_text(self.page, 'Wardrive')
        self.click('show_wardrive_page.ctx--wardrive_setup_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_home_btn')
        self.click('show_home_mgmt_overlay.scan_btn')
        row = next(o for o in objects(self.page)
                   if 'emu.phase4.wardrive.home.' in o['binding'])
        self.click(row['binding'])
        self.click('show_home_mgmt_overlay.add_btn')
        self.click('show_home_mgmt_overlay.close_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_sw')
        if wars:
            self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_wdgwars_cb')
        if archive:
            self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_archive_cb')
        if poweroff:
            self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_poweroff_cb')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
        if poweroff:
            self.page.evaluate('emulator.module._emu_set_timing(1)')
        self.click('show_wardrive_page.ctx--wardrive_start_btn')
        self.advance(10000 if poweroff else 1000)
        self.assertIn('Home network detected', self.text())
        self.page.evaluate('(value)=>globalThis.emulatorWardriveUploadOutcome=value', outcome)
        self.click('show_wardrive_home_confirm.yes')

    def test_failed_upload_can_retry_and_resume_recording(self):
        self.start_home_upload('failure')
        self.assertIn('Auto-upload failed', self.text())
        self.assertIn('Retry', self.text())
        self.assertIn('Resume', self.text())
        self.assertIn('Close', self.text())
        self.assertEqual(self.state()['upload']['failed'], 1)

        self.page.evaluate("globalThis.emulatorWardriveUploadOutcome='success'")
        click_text(self.page, 'Retry')
        self.assertIn('Auto-upload complete', self.text())
        self.assertEqual(self.state()['upload']['uploaded'], 1)
        click_text(self.page, 'Resume')
        self.assertTrue(self.state()['running'])
        self.assertNotIn('Auto-upload complete', self.text())
        self.clean()

    def test_stay_on_cancels_demo_poweroff_then_close_dismisses(self):
        self.start_home_upload('success', poweroff=True, rotation=1)
        self.assertIn('Powering off in', self.text())
        self.assertIn('Stay on', self.text())
        self.advance(2000)
        click_text(self.page, 'Stay on')
        self.assertIn('Power off cancelled', self.text())
        self.advance(12000)
        self.assertNotIn('Simulated power off', self.text())
        click_text(self.page, 'Close')
        self.assertNotIn('Power off cancelled', self.text())
        self.assertFalse(self.state()['running'])
        self.clean()

    def test_poweroff_deadline_has_local_result_and_close(self):
        self.start_home_upload('success', poweroff=True, rotation=2)
        self.advance(12000)
        self.assertIn('Simulated power off', self.text())
        self.assertNotIn('Stay on', self.text())
        click_text(self.page, 'Close')
        self.assertNotIn('Simulated power off', self.text())
        self.clean()

    def test_archive_moves_done_sessions_after_success(self):
        self.start_home_upload('success', rotation=3, archive=True, wars=True)
        session = self.state()['sessions'][0]
        self.assertTrue(session.get('archived', False))
        self.assertIn('/uploaded/', session['path'])
        self.assertNotEqual(session['originalPath'], session['path'])
        self.clean()

    def test_failed_upload_does_not_archive_older_done_session(self):
        self.start_home_upload('success', wars=True)
        original = self.state()['sessions'][0]
        self.assertFalse(original.get('archived', False))
        click_text(self.page, 'Close')
        self.click('show_wardrive_page.ctx--wardrive_setup_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_archive_cb')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
        self.click('show_wardrive_page.ctx--wardrive_start_btn')
        self.advance(1000)
        self.assertIn('Home network detected', self.text())
        self.page.evaluate("globalThis.emulatorWardriveUploadOutcome='failure'")
        self.click('show_wardrive_home_confirm.yes')
        unchanged = next(s for s in self.state()['sessions'] if s['id'] == original['id'])
        self.assertFalse(unchanged.get('archived', False))
        self.assertEqual(unchanged['path'], original['path'])
        self.clean()

    def test_autoupload_preferences_survive_reload_and_drive_home_result(self):
        self.load()
        click_text(self.page, 'Wardrive')
        self.click('show_wardrive_page.ctx--wardrive_setup_btn')
        for suffix in ('sw', 'wigle_cb', 'wdgwars_cb', 'archive_cb', 'poweroff_cb'):
            self.click(f'wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_{suffix}')
        expected = {'sw': True, 'wigle_cb': False, 'wdgwars_cb': True,
                    'archive_cb': True, 'poweroff_cb': True}
        self.assertEqual({key: self.preference_checked(key) for key in expected}, expected)

        self.page.reload()
        self.ready()
        click_text(self.page, 'Wardrive')
        self.click('show_wardrive_page.ctx--wardrive_setup_btn')
        self.assertEqual({key: self.preference_checked(key) for key in expected}, expected)

        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_home_btn')
        self.click('show_home_mgmt_overlay.scan_btn')
        row = next(item for item in objects(self.page)
                   if 'emu.phase4.wardrive.home.' in item['binding'])
        self.click(row['binding'])
        self.click('show_home_mgmt_overlay.add_btn')
        self.click('show_home_mgmt_overlay.close_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
        self.page.evaluate('emulator.module._emu_set_timing(1)')
        self.click('show_wardrive_page.ctx--wardrive_start_btn')
        self.advance(10000)
        self.assertIn('Home network detected', self.text())
        self.click('show_wardrive_home_confirm.yes')
        self.assertIn('Powering off in', self.text())
        session = self.state()['sessions'][-1]
        self.assertEqual(session['uploadStatus'], {'wigle': 'pending', 'wdgwars': 'done'})
        self.clean()


if __name__ == '__main__':
    unittest.main(verbosity=2)
