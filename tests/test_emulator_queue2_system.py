import unittest
from emulator_browser import objects,click_text,click_object
import test_emulator_portal_demo as helpers
import test_emulator_phase4_pcap_deep as nav
class Queue2SystemBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    advance=helpers.PortalDemoBrowser.advance
    clean=helpers.PortalDemoBrowser.clean
    click=nav.PcapDeepBrowser.click
    def tick(self):self.page.evaluate('emulator.module._emu_tick(10)')
    def begin(self,id,rotation):
        self.load(rotation);self.page.locator('#queue2-story').select_option(id);self.page.locator('#guide-queue2').click()
    def title(self,text):self.page.wait_for_function('(t)=>document.querySelector("#guide-title").textContent===t',arg=text,timeout=9000)
    def finish(self):
        self.page.wait_for_function('document.querySelector("#guide-leave").textContent==="Back to stories"',timeout=9000)
        self.page.locator('#guide-leave').click();self.assertTrue(self.page.locator('#queue2-story').is_visible());self.clean()
    def settings(self):click_text(self.page,'INTERNAL');click_text(self.page,'Settings')
    def test_detectors_stop_restart_and_back_all_rotations(self):
        p=self.page
        for r in range(4):
            for anti in [False,True]:
                with self.subTest(rotation=r,anti=anti):
                    label='Anti-Surv' if anti else 'Deauth Detector';prefix='antisurv' if anti else 'deauth'
                    self.begin('s07-'+('anti' if anti else 'detector'),r)
                    click_text(p,'Anti-Surv' if anti else 'Deauth\nDetector');self.click(prefix+'_start_btn')
                    self.title('Read new detector events');self.advance(3000);self.title('Stop detection')
                    self.click(prefix+'_stop_btn');self.title('Check stopped results');self.title('Restart detection')
                    self.click(prefix+'_start_btn');self.title('Read the restarted session');self.advance(3000)
                    self.title('Leave and stop detection');self.click('show_antisurv_page.back_btn' if anti else 'show_deauth_detector_page.back_btn');self.finish()
    def test_each_module_reboot_all_rotations(self):
        p=self.page
        for r in range(4):
            for target in ['grove','mbus']:
                with self.subTest(rotation=r,target=target):
                    self.begin('s21-reboot-'+target,r);click_text(p,'INTERNAL');click_text(p,'Module\nStatus')
                    self.title('Reboot '+target.upper());self.click('emu.phase46.system.reboot.'+target);self.advance(3000)
                    self.title('Return to INTERNAL');self.click('emu.phase46.system.back');self.finish()
                    states=p.evaluate("[emulator.device.device.systemStatus('grove').reboots,emulator.device.device.systemStatus('mbus').reboots]")
                    self.assertEqual(states,[1,0] if target=='grove' else [0,1])
    def test_ota_info_releases_update_and_cancel_all_rotations(self):
        p=self.page
        for r in range(4):
            for cancel in [False,True]:
                with self.subTest(rotation=r,cancel=cancel):
                    self.begin('s21-ota-'+('cancel' if cancel else 'update'),r);self.settings();self.click('/Monster OTA')
                    if cancel:
                        self.title('Use realistic timing');p.locator('#timing').select_option('1')
                    else:
                        self.title('Read device info');self.click('show_ota_page.info_btn');self.title('Close the OTA monitor');self.click('emu.phase46.ota.monitor.close')
                        self.title('Read available releases');self.click('show_ota_page.list_btn');self.title('Close the OTA monitor');self.click('emu.phase46.ota.monitor.close')
                    self.title('Start a fresh OTA operation');self.click('show_ota_page.check_btn')
                    self.title('Cancel the OTA operation' if cancel else 'Wait for the updated image')
                    if cancel:self.click('emu.phase46.ota.monitor.close')
                    else:
                        self.advance(4000);self.title('Close the OTA monitor');self.click('emu.phase46.ota.monitor.close')
                    self.title('Return to Settings');self.click('show_ota_page.back');self.finish()
                    self.assertEqual(p.evaluate("emulator.device.device.systemStatus('grove').version"),'demo-main-1' if cancel else 'demo-main-2')
    def test_ota_slot_activate_and_cancel_all_rotations(self):
        p=self.page
        for r in range(4):
            for cancel in [False,True]:
                with self.subTest(rotation=r,cancel=cancel):
                    self.begin('s21-slot-'+('cancel' if cancel else 'activate'),r);self.settings();self.click('/Monster OTA')
                    if cancel:
                        self.title('Use realistic timing');p.locator('#timing').select_option('1')
                    self.title('Inspect boot slots');self.click('show_ota_page.info_btn');self.title('Activate the other slot')
                    self.click('/ota_1/activate');self.title('Cancel slot activation' if cancel else 'Wait for slot activation')
                    if cancel:self.click('emu.phase46.ota.monitor.close')
                    else:
                        self.advance(3000);self.title('Close the OTA monitor');self.click('emu.phase46.ota.monitor.close')
                    self.title('Return to Settings');self.click('show_ota_page.back');self.finish()
                    self.assertEqual(p.evaluate("emulator.device.device.systemStatus('grove').activeSlot"),0 if cancel else 1)
    def test_sd_admin_both_exit_choices_all_rotations(self):
        p=self.page
        for r in range(4):
            self.begin('s21-sd-admin',r);self.settings();self.click('/Monster SD Admin')
            self.title('Start simulated SD Admin');click_text(p,'Quick Start');self.title('Try leaving SD Admin')
            self.click('show_sd_admin_page.back');self.title('Keep SD Admin running');click_text(p,'Stay here')
            self.title('Open the leave choice again');self.click('show_sd_admin_page.back');self.title('Stop and return')
            click_text(p,'Stop and back');self.finish();self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
    def test_detector_full_history_still_accepts_new_session_events(self):
        p=self.page;self.begin('s07-detector',0);click_text(p,'Deauth\nDetector');self.click('deauth_start_btn')
        self.title('Read new detector events');self.advance(100000);self.title('Stop detection')
        self.click('deauth_stop_btn');self.title('Restart detection');self.click('deauth_start_btn')
        self.title('Read the restarted session');self.advance(3000);self.title('Leave and stop detection')
        self.click('show_deauth_detector_page.back_btn');self.finish()
if __name__=='__main__':unittest.main(verbosity=2)
