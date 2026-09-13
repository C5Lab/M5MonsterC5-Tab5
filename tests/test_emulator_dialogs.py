import unittest
from pathlib import Path
import test_emulator_portal_demo as portal
from emulator_browser import click_text,objects
import test_emulator_phase4_wardrive as wd
class NativeDialogs(unittest.TestCase):
    setUp=portal.PortalDemoBrowser.setUp
    load=portal.PortalDemoBrowser.load
    ready=portal.PortalDemoBrowser.ready
    text=portal.PortalDemoBrowser.text
    advance=portal.PortalDemoBrowser.advance
    clean=portal.PortalDemoBrowser.clean
    def shot(self,name,rotation):
        folder=Path(__file__).resolve().parents[1]/"docs/ui-emulator/dialogs-evidence"
        folder.mkdir(exist_ok=True)
        self.page.locator("#display").screenshot(path=str(folder/f"{name}-rotation{rotation}.png"))
    def condition(self,value,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}&moduleCondition={value}');self.ready()
    def test_missing_board_all_rotations(self):
        for rotation in range(4):
            self.condition('missing-board',rotation)
            self.assertIn('No Board Detected',self.text());self.shot('missing-board',rotation)
            self.assertFalse(self.page.evaluate('emulator.device.device.systemStatus("grove").connected'))
            self.assertFalse(self.page.evaluate('emulator.device.device.systemStatus("mbus").connected'))
            click_text(self.page,'Continue Anyway')
            self.assertNotIn('No Board Detected',self.text());self.clean()
    def test_sd_cancel_continue_all_rotations(self):
        for rotation in range(4):
            self.condition('missing-sd',rotation)
            click_text(self.page,'Wardrive')
            self.assertIn('NO SD CARD DETECTED',self.text());self.shot('missing-sd',rotation)
            click_text(self.page,'Cancel');self.assertNotIn('NO SD CARD DETECTED',self.text())
            click_text(self.page,'Wardrive');click_text(self.page,'Continue')
            self.assertIn('Press Start to begin wardrive',self.text());self.clean()
    def test_version_actions_all_rotations(self):
        for rotation in range(4):
            self.condition('version-mismatch',rotation)
            self.assertIn('JanOS Version Mismatch',self.text());self.shot('version-mismatch',rotation)
            click_text(self.page,'OK');self.assertNotIn('JanOS Version Mismatch',self.text())
            self.condition('version-mismatch',rotation)
            click_text(self.page,'Monster OTA',contains=True)
            self.assertNotIn('JanOS Version Mismatch',self.text())
            self.assertIn('Download & Flash',self.text());self.clean()
    click_binding=wd.WardriveBrowser.click
    def test_home_confirmation_no_yes_and_reopen(self):
        for rotation in range(4):
            self.load(rotation);click_text(self.page,'Wardrive')
            self.click_binding('show_wardrive_page.ctx--wardrive_setup_btn')
            self.click_binding('wardrive_setup_btn_cb.ctx--wardrive_setup_home_btn')
            self.click_binding('show_home_mgmt_overlay.scan_btn')
            rows=[o for o in objects(self.page) if 'emu.phase4.wardrive.home.' in o['binding']]
            self.click_binding(rows[0]['binding']);self.click_binding('show_home_mgmt_overlay.add_btn')
            self.click_binding('show_home_mgmt_overlay.close_btn')
            self.click_binding('wardrive_setup_btn_cb.ctx--wardrive_setup_autoup_sw')
            self.click_binding('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
            self.click_binding('show_wardrive_page.ctx--wardrive_start_btn');self.advance(1000)
            self.assertIn('Home network detected',self.text());self.shot('home-network',rotation)
            self.click_binding('show_wardrive_home_confirm.no')
            self.advance(1000);self.assertNotIn('Home network detected',self.text())
            self.assertTrue(self.page.evaluate('emulator.device.device.wardrive("grove").running'))
            self.click_binding('show_wardrive_page.ctx--wardrive_stop_btn')
            self.click_binding('show_wardrive_page.ctx--wardrive_start_btn');self.advance(1000)
            self.click_binding('show_wardrive_home_confirm.yes')
            self.assertFalse(self.page.evaluate('emulator.device.device.wardrive("grove").running'))
            self.assertGreater(self.page.evaluate('emulator.device.device.wardrive("grove").upload.uploaded'),0)
            self.clean()
    def test_ota_slot_activation_all_rotations(self):
        for rotation in range(4):
            self.load(rotation);click_text(self.page,'INTERNAL');click_text(self.page,'Settings');self.click_binding('/Monster OTA')
            self.click_binding('show_ota_page.info_btn')
            self.assertIn('Slot ota_0',self.text());self.assertIn('Slot ota_1',self.text());self.shot('ota-slots',rotation)
            self.click_binding('/ota_1/activate')
            self.advance(2000)
            self.assertIn('Simulated boot slot activated',self.text())
            self.assertEqual(self.page.evaluate('emulator.device.device.systemStatus("grove").activeSlot'),1)
            self.page.evaluate('emulator.module._emu_set_timing(1)')
            self.click_binding('/ota_0/activate')
            self.click_binding('emu.phase46.ota.monitor.close');self.advance(2000)
            self.click_binding('show_ota_page.info_btn')
            self.assertEqual(self.page.evaluate('emulator.device.device.systemStatus("grove").activeSlot'),1)
            self.click_binding('/ota_0/activate');self.advance(2000)
            self.assertEqual(self.page.evaluate('emulator.device.device.systemStatus("grove").activeSlot'),0)
            self.assertEqual(self.page.evaluate('emulator.device.device.systemStatus("grove").reboots'),2)
            self.clean()
if __name__=='__main__':unittest.main(verbosity=2)

