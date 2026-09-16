"""Native AP Radar with offline samples and per-module lifecycle."""
import unittest
from emulator_browser import objects,click_text,click_object
import test_emulator_portal_demo as helpers

class RadarBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    text=helpers.PortalDemoBrowser.text
    scan=helpers.PortalDemoBrowser.scan
    click=helpers.PortalDemoBrowser.click
    advance=helpers.PortalDemoBrowser.advance
    clean=helpers.PortalDemoBrowser.clean
    def bound(self,fragment):return next(o for o in objects(self.page) if fragment in o['binding'])
    def stop(self):click_object(self.page,self.bound('show_ap_radar_page.stop_btn'))
    def test_target_rssi_animation_stop_back_and_reopen_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation);self.scan('02:20:77:00:00:03');self.click('Radar')
                self.assertIn('AP Radar',self.text());self.assertIn('CHROME-CLINIC',self.text())
                job=p.evaluate('emulator.device.snapshot(0).active');self.assertIsNotNone(job)
                before=p.evaluate('(id)=>emulator.device.device.job(id).result.signal.rssi',job)
                p.wait_for_function('(a)=>emulator.device.device.job(a.id).result.signal.rssi!==a.before',arg={'id':job,'before':before},timeout=5000)
                self.assertIn('dBm (SIM)',self.text())
                self.stop();self.assertNotIn('AP Radar',self.text())
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'completed')
                self.click('Radar');click_object(p,self.bound('show_ap_radar_page.back_btn'))
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'));self.clean()
    def test_busy_invalid_selection_disconnect_and_cancel(self):
        p=self.page;self.load();self.scan()
        select=self.bound('02:20:77:00:00:01/select');click_object(p,select);self.click('Radar')
        self.assertIn('Select exactly 1 network',self.text())
        click_object(p,self.bound('02:20:77:00:00:01/select'))
        old=p.evaluate("emulator.device.device.attackStart('grove','karma')")
        self.click('Radar');self.assertIn('busy',self.text().lower())
        self.assertEqual(p.evaluate('emulator.device.snapshot(0).active'),old)
        p.evaluate('(id)=>emulator.device.cancel(id)',old);self.click('Radar')
        p.evaluate("emulator.device.device.setModule('grove',{connected:false})");self.advance(100)
        self.assertIn('No signal',self.text());self.stop()
        p.evaluate("emulator.device.device.setModule('grove',{connected:true})");self.click('Radar')
        p.locator('#cancel').click();self.advance(100);self.assertIn('No signal',self.text());self.stop();self.clean()
    def test_two_modules_are_independent_and_reset_clears_jobs(self):
        p=self.page;self.load();self.scan();self.click('Radar');grove=p.evaluate('emulator.device.snapshot(0).active')
        click_text(p,'MBUS');click_text(p,'WiFi Scan\n& Attack')
        p.wait_for_function('emulator.module._emu_scan_state(2)===2')
        click_object(p,self.bound('02:20:77:00:00:03/select'));self.click('Radar')
        mbus=p.evaluate('emulator.device.snapshot(2).active');self.stop()
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',mbus),'completed')
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',grove),'running')
        click_text(p,'GROVE');self.assertIn('NEON-BAZAAR',self.text());self.assertNotIn('CHROME-CLINIC',self.text())
        with p.expect_navigation():p.locator('#reset').click()
        self.ready();self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'));self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
