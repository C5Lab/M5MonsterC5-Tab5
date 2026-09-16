"""S18 acceptance through the real guide picker and native canvas controls."""
import unittest
from emulator_browser import objects, click_text, click_object
import test_emulator_portal_demo as helpers
import test_emulator_phase4_pcap_deep as nav

class SettingsS18Browser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    click=nav.PcapDeepBrowser.click
    def tick(self):self.page.evaluate('emulator.module._emu_tick(10)')
    def begin(self,id,rotation):
        self.load(rotation)
        self.page.locator('#queue2-story').select_option(id)
        self.page.locator('#guide-queue2').click()
        click_text(self.page,'INTERNAL');click_text(self.page,'Settings')
    def title(self,text):self.page.wait_for_function('(t)=>document.querySelector("#guide-title").textContent===t',arg=text,timeout=8000)
    def finish(self):
        self.page.wait_for_function('document.querySelector("#guide-leave").textContent==="Back to stories"',timeout=8000)
        self.page.locator('#guide-leave').click()
        self.assertTrue(self.page.locator('#queue2-story').is_visible());self.clean()
    def snapshot(self):
        return self.page.evaluate('()=>{emulator.module._emu_settings_s18_state();return emulatorSettingsS18}')
    def number(self,current,value):
        obs=objects(self.page)
        label=next(o for o in obs if o['text']==f'{current:04d}')
        click_object(self.page,next(o for o in obs if o['id']==label['parent']))
        self.page.keyboard.type(str(value));self.page.keyboard.press('Enter')
    def test_scan_save_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                self.begin('s18-scan',r);click_text(p,'Scan\nSetup');self.title('Change GROVE scan settings')
                s=self.snapshot();self.number(s['minInput'],200);self.number(s['maxInput'],500)
                self.click('show_scan_time_popup.scan_setup_vendor_switch.')
                self.title('Save the scan timings');click_text(p,'Save');self.title('Reopen and verify settings')
                click_text(p,'Scan\nSetup');self.title('Check a fresh Scan');click_text(p,'Cancel')
                click_text(p,'GROVE');click_text(p,'WiFi Scan\n& ',contains=True);self.finish()
                self.assertEqual([self.snapshot()['min'],self.snapshot()['max']],[200,500])
    def test_invalid_range_and_cancel_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                self.begin('s18-scan-invalid',r);baseline=self.snapshot()
                click_text(p,'Scan\nSetup');self.title('Try an invalid range')
                s=self.snapshot();self.number(s['minInput'],1500);self.number(s['maxInput'],300)
                click_text(p,'Save');self.title('Cancel the invalid edit');click_text(p,'Cancel')
                self.title('Verify the original timings');click_text(p,'Scan\nSetup');self.title('Return to Settings')
                click_text(p,'Cancel');self.finish()
                self.assertEqual([self.snapshot()['min'],self.snapshot()['max']],[baseline['min'],baseline['max']])
    def test_red_team_both_choices_all_rotations(self):
        p=self.page
        for r in range(4):
            for enable in [False,True]:
                with self.subTest(rotation=r,enable=enable):
                    self.begin('s18-red-'+('enable' if enable else 'cancel'),r)
                    click_text(p,'Red\nTeam')
                    if self.snapshot()['redTeam']:self.click('show_red_team_settings_page.red_team_switch.')
                    self.title('Read the disclaimer');self.click('show_red_team_settings_page.red_team_switch.')
                    self.title('Accept the disclaimer' if enable else 'Cancel the disclaimer')
                    self.click('show_red_team_disclaimer_popup.'+('confirm_btn' if enable else 'cancel_btn'))
                    self.title('Check the GROVE menu');self.click('show_red_team_settings_page.back_btn.')
                    click_text(p,'GROVE');self.title('Check the Scan actions')
                    click_text(p,'WiFi Scan\n& Attack' if enable else 'WiFi Scan\n& Test');self.finish()
                    self.assertEqual(self.snapshot()['redTeam'],enable)
    def test_wrong_module_and_restart_do_not_credit_existing_dialog(self):
        p=self.page;self.begin('s18-red-enable',0);click_text(p,'Red\nTeam')
        if self.snapshot()['redTeam']:self.click('show_red_team_settings_page.red_team_switch.')
        self.title('Read the disclaimer');self.click('show_red_team_settings_page.red_team_switch.')
        self.title('Accept the disclaimer');click_text(p,'MBUS')
        self.assertEqual(p.locator('#guide-title').inner_text(),'Accept the disclaimer')
        click_text(p,'INTERNAL');self.click('show_red_team_disclaimer_popup.cancel_btn')
        self.assertEqual(p.locator('#guide-title').inner_text(),'Accept the disclaimer')
        p.locator('#guide-restart').click();self.title('Read the disclaimer')
        self.click('show_red_team_settings_page.red_team_switch.');self.title('Accept the disclaimer');self.clean()
    def test_restart_rejects_old_validation_error_and_cancel_as_save(self):
        p=self.page;self.begin('s18-scan-invalid',0);click_text(p,'Scan\nSetup');self.title('Try an invalid range')
        s=self.snapshot();self.number(s['minInput'],1500);click_text(p,'Save');self.title('Cancel the invalid edit')
        p.locator('#guide-restart').click();self.title('Open Scan Setup')
        p.wait_for_timeout(300);self.assertEqual(p.locator('#guide-title').inner_text(),'Open Scan Setup')
        click_text(p,'Cancel');click_text(p,'Scan\nSetup');self.title('Try an invalid range')
        click_text(p,'Cancel');p.locator('#guide-leave').click()
        p.locator('#queue2-story').select_option('s18-scan');p.locator('#guide-queue2').click()
        click_text(p,'Scan\nSetup');self.title('Change GROVE scan settings')
        s=self.snapshot();self.number(s['minInput'],200);self.number(s['maxInput'],500)
        self.click('show_scan_time_popup.scan_setup_vendor_switch.');self.title('Save the scan timings')
        click_text(p,'Save');self.title('Reopen and verify settings');p.locator('#guide-restart').click()
        self.title('Open Scan Setup');click_text(p,'Scan\nSetup');self.title('Change GROVE scan settings')
        self.assertIn('Min to 150',p.locator('#guide-instruction').inner_text())
        s=self.snapshot();self.number(s['minInput'],150);self.number(s['maxInput'],450)
        self.click('show_scan_time_popup.scan_setup_vendor_switch.');self.title('Save the scan timings')
        click_text(p,'Cancel');p.wait_for_timeout(300)
        self.assertEqual(p.locator('#guide-title').inner_text(),'Save the scan timings');self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
