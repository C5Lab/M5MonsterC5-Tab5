"""S09-S12 native canvas acceptance against fresh offline Wasm."""
import unittest
from emulator_browser import objects,click_text,click_object
import test_emulator_portal_demo as portal_helpers

class Queue2AttacksBrowser(unittest.TestCase):
    setUp=portal_helpers.PortalDemoBrowser.setUp
    load=portal_helpers.PortalDemoBrowser.load
    ready=portal_helpers.PortalDemoBrowser.ready
    advance=portal_helpers.PortalDemoBrowser.advance
    clean=portal_helpers.PortalDemoBrowser.clean
    scan=portal_helpers.PortalDemoBrowser.scan
    def begin(self,id,rotation=0):
        self.load(rotation)
        self.page.locator('#queue2-story').select_option(id)
        self.page.locator('#guide-queue2').click()
        self.page.evaluate("async id=>{const {queue2Attacks}=await import('./queue2-attacks.mjs');globalThis.qaStory=queue2Attacks.find(e=>e.id===id).create().story;}",id)
    def stage(self,n):
        self.page.wait_for_function('(n)=>document.querySelector("#guide-title").textContent===(n===qaStory.steps.length?qaStory.completionTitle:qaStory.steps[n].title)',arg=n,timeout=7000)
    def done(self):
        self.page.wait_for_function('document.querySelector("#guide-title").textContent===qaStory.completionTitle',timeout=7000)
        self.assertIn('complete',self.page.locator('#guide-progress').inner_text());self.clean()
    def tap(self,fragment):
        click_object(self.page,next(o for o in objects(self.page) if fragment in o['binding']))
    def test_scan_actions_success_cancel_and_rotations(self):
        for rotation in range(4):
            for id,label,stop in [('deauth','Deauth','scan_deauth_popup_close_cb'),('sae','SAE Overflow','show_sae_popup.stop_btn'),('handshake','Handshake','show_handshaker_popup')]:
                for cancel in (False,True):
                    with self.subTest(rotation=rotation,id=id,cancel=cancel):
                        self.begin(id+('-cancel' if cancel else ''),rotation);self.scan();self.stage(1)
                        click_text(self.page,label);self.stage(2)
                        if cancel:self.page.locator('#cancel').click();self.stage(3)
                        else:self.stage(3)
                        if id=='handshake':self.tap('show_handshaker_popup.ctx--handshaker_stop_btn')
                        elif id=='deauth':self.tap('show_scan_deauth_popup.stop_btn')
                        else:self.tap(stop)
                        self.done()
    def test_global_confirm_no_yes_stop_and_rotations(self):
        for rotation in range(4):
            for id,label,kind in [('blackout','Blackout','blackout'),('global-handshaker','Handshaker','global_handshaker'),('snifferdog','SnifferDog','snifferdog')]:
                for cancel in (False,True):
                    with self.subTest(rotation=rotation,id=id,cancel=cancel):
                        self.begin(id+('-cancel' if cancel else ''),rotation);click_text(self.page,'Global WiFi\nAttacks');click_text(self.page,label);self.stage(1)
                        click_text(self.page,'No');self.stage(2)
                        if cancel:self.tap('show_global_attacks_page.back_btn')
                        else:
                            click_text(self.page,label);click_text(self.page,'Yes');self.stage(3);self.stage(4)
                            self.tap('show_'+kind+'_active_popup.stop_btn')
                        self.done()
    def test_karma_menu_and_rotations(self):
        for rotation in range(4):
            for id in ['karma-menu']:
                with self.subTest(rotation=rotation,id=id):
                    self.begin(id,rotation)
                    self.stage(1);click_text(self.page,'Karma');self.stage(2);click_text(self.page,'Start Sniffer');self.stage(3);self.stage(4)
                    click_text(self.page,'Stop Sniffer');self.page.wait_for_timeout(100);self.tap('emu.phase45.karma.probe.0.0');self.stage(5);click_text(self.page,'Start Karma');self.stage(6);self.stage(7)
                    click_text(self.page,'STOP');self.stage(8);self.tap('emu.phase45.show_karma_page.back_btn');self.done()
    def test_beacon_list_start_empty_and_rotations(self):
        for rotation in range(4):
            for empty in (False,True):
                with self.subTest(rotation=rotation,empty=empty):
                    self.begin('beacon-empty' if empty else 'beacon',rotation);click_text(self.page,'Global WiFi\nAttacks');click_text(self.page,'Beacon Spam');click_text(self.page,'List SSIDs');self.stage(1)
                    if empty:
                        for _ in range(2):self.tap('beacon_spam_rebuild_ssid_grid.delete_btn')
                        self.stage(2);self.tap('show_beacon_ssids_page.back_btn');click_text(self.page,'Start Spam');self.stage(3);self.tap('show_beacon_spam_page.back_btn')
                    else:
                        self.tap('show_beacon_ssids_page.back_btn');click_text(self.page,'Start Spam');self.stage(2);self.stage(3);self.tap('emu.phase45.beacon_spam_start_cb.beacon_spam_active_close_cb')
                    self.done()
    def test_handshake_wrong_target_and_busy_refusal(self):
        self.begin('handshake');self.scan('02:20:77:00:00:02')
        click_text(self.page,'Handshake');self.advance(3000)
        self.assertEqual(self.page.locator('#guide-title').inner_text(),'Select NEON-BAZAAR')
        job=self.page.evaluate('emulator.device.device.job(emulator.device.snapshot(0).active)')
        self.assertEqual(job['options']['networkIds'],['net-2'])
        self.assertIn('02:20:77:00:00:02',str(self.page.evaluate('emulator.device.snapshot(0).networks')))
        self.tap('show_handshaker_popup.ctx--handshaker_stop_btn');self.clean()
        self.begin('handshake');self.scan();self.stage(1)
        job=self.page.evaluate("emulator.device.device.attackStart('grove','karma')")
        click_text(self.page,'Handshake');self.advance(500)
        self.assertEqual(self.page.evaluate('emulator.device.snapshot(0).active'),job)
        self.assertEqual(self.page.locator('#guide-title').inner_text(),'Start Handshake')
        self.assertIn('busy',' '.join(o['text'] for o in objects(self.page)))
        self.tap('show_handshaker_popup.ctx--handshaker_stop_btn');self.page.locator('#cancel').click();self.clean()
    def test_handshake_no_result_does_not_claim_capture(self):
        self.begin('handshake');self.scan('02:20:77:00:00:04');click_text(self.page,'Handshake');self.advance(4000)
        text=' '.join(o['text'] for o in objects(self.page))
        self.assertIn('No synthetic handshake available',text);self.assertNotIn('Handshake captured:',text)
        self.assertEqual(self.page.locator('#guide-title').inner_text(),'Select NEON-BAZAAR')
        self.tap('show_handshaker_popup.ctx--handshaker_stop_btn');self.clean()
    def test_restart_wrong_module_disconnect_and_busy(self):
        self.begin('deauth');self.scan();self.stage(1);click_text(self.page,'Deauth');self.stage(2)
        self.page.locator('#guide-restart').click();self.page.wait_for_timeout(300)
        self.assertNotIn('complete',self.page.locator('#guide-progress').inner_text())
        click_text(self.page,'MBUS');self.page.wait_for_timeout(200);self.assertNotIn('complete',self.page.locator('#guide-progress').inner_text())
        click_text(self.page,'GROVE');self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})");self.advance(300)
        self.assertNotIn('complete',self.page.locator('#guide-progress').inner_text());self.assertIsNone(self.page.evaluate('emulator.device.snapshot(0).active'))
        self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
