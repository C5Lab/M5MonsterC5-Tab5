"""S02/S03 acceptance through native Scan, selection and Radar controls."""
import unittest
from emulator_browser import ROOT,objects,click_text,click_object
import test_emulator_portal_demo as helpers

class WiFiStoriesBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    advance=helpers.PortalDemoBrowser.advance
    click=helpers.PortalDemoBrowser.click
    def title(self,value):
        self.page.wait_for_function('(s)=>document.querySelector("#guide-title").textContent===s',arg=value,timeout=6000)
    def bound(self,fragment):return next(o for o in objects(self.page) if fragment in o['binding'])
    def select(self,last):click_object(self.page,self.bound(f'02:20:77:00:00:{last}/select'))
    def begin(self,kind,variant='normal',rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}&wifiDemo={variant}');self.ready()
        self.page.locator('#guide-'+kind).click();click_text(self.page,'WiFi Scan\n& Attack')
    def test_scan_variants_all_rotations(self):
        p=self.page
        for rotation in range(4):
            for variant in ['normal','hidden','empty','cancel']:
                with self.subTest(rotation=rotation,variant=variant):
                    self.begin('scan',variant,rotation)
                    if variant=='cancel':
                        self.title('Cancel the scan');p.locator('#cancel').click();self.title('Scan again')
                        click_object(p,self.bound('show_scan_page.ctx--scan_btn'));self.advance(31000)
                    if variant!='empty':
                        self.title('Select NEON-BAZAAR');self.select('01')
                        self.title('Select two networks');self.select('03')
                        self.title('Clear the selection');self.select('01');self.select('03')
                        if variant=='hidden':
                            self.title('Select the hidden SSID')
                            self.assertEqual(p.evaluate("emulator.device.snapshot(0).networks.find(n=>n.bssid.endsWith(':02')).ssid"),'')
                            self.select('02');self.title('Clear the hidden network');self.select('02')
                    else:
                        p.wait_for_function('emulator.module._emu_scan_state(0)===2')
                        self.assertEqual(p.evaluate('emulator.module._emu_network_count(0)'),0)
                    self.title('Return to GROVE');click_object(p,self.bound('show_scan_page.back_btn'))
                    self.title('Wi-Fi scan story complete');self.clean()

    def radar_setup(self,rotation=0):
        self.begin('radar',rotation=rotation)
        self.title('Try Radar without a selection');self.click('Radar')
        self.title('Select two networks');self.select('01');self.select('03')
        self.title('Try Radar with two selections');self.click('Radar')
        self.title('Keep only NEON-BAZAAR');self.select('03')
        self.title('Open Radar');self.click('Radar');self.title('Watch the Wi-Fi signal')

    def test_radar_signal_stop_and_back_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.radar_setup(rotation);self.title('Stop Radar')
                p.screenshot(path=str(ROOT/f'docs/ui-emulator/radar-story-{rotation}.png'))
                job=p.evaluate('emulator.device.snapshot(0).active')
                click_object(p,self.bound('show_ap_radar_page.'+('stop_btn' if rotation%2==0 else 'back_btn')))
                self.title('Radar story complete')
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'completed')
                self.assertEqual(p.locator('#guide-leave').inner_text(),'Back to stories')
                self.assertTrue(p.locator('#guide-steps').is_hidden())
                p.evaluate('globalThis.storyReturnDevice=emulator.device')
                p.locator('#guide-leave').click()
                self.assertTrue(p.locator('#guide-panel').is_hidden())
                for name in ['start','startup','scan','radar']:
                    self.assertTrue(p.locator('#guide-'+name).is_visible())
                self.assertTrue(p.evaluate('storyReturnDevice===emulator.device'))
                p.locator('#guide-scan').click();self.title('Start a fresh GROVE scan')
                self.assertEqual(p.locator('#guide-leave').inner_text(),'Free exploration')
                self.assertTrue(p.locator('#guide-steps').is_visible());self.clean()

    def test_radar_background_cancel_reopen_and_restart(self):
        p=self.page;self.radar_setup()
        click_text(p,'MBUS');p.wait_for_timeout(2300)
        self.assertEqual(p.locator('#guide-title').inner_text(),'Watch the Wi-Fi signal')
        click_text(p,'GROVE');self.title('Stop Radar')
        p.locator('#cancel').click();self.title('Open Radar')
        click_object(p,self.bound('show_ap_radar_page.stop_btn'));self.click('Radar')
        self.title('Watch the Wi-Fi signal');self.title('Stop Radar')
        p.locator('#guide-restart').click();self.title('Start a fresh GROVE scan')
        click_object(p,self.bound('show_ap_radar_page.stop_btn'))
        self.title('Start a fresh GROVE scan');self.clean()

    def test_preset_reload_reset_and_stale_scan(self):
        p=self.page;self.begin('scan');self.title('Select NEON-BAZAAR')
        p.locator('#guide-restart').click();self.select('01')
        self.title('Start a fresh GROVE scan')
        p.locator('.demo-conditions summary').click()
        with p.expect_navigation():p.locator('#wifi-condition').select_option('hidden')
        self.ready();self.assertTrue(p.locator('#guide-panel').is_hidden())
        self.assertEqual(p.locator('#wifi-condition').input_value(),'hidden')
        with p.expect_navigation():p.locator('#reset').click()
        self.ready();self.assertEqual(p.locator('#wifi-condition').input_value(),'normal');self.clean()

    def test_rejection_must_match_selection_at_the_time_of_the_action(self):
        self.begin('radar');self.title('Try Radar without a selection')
        self.select('01');self.select('03');self.click('Radar')
        self.select('01');self.select('03');self.title('Try Radar without a selection')
        self.click('Radar');self.title('Select two networks');self.select('01');self.select('03')
        self.title('Try Radar with two selections')
        self.select('01');self.select('03');self.click('Radar')
        self.select('01');self.select('03');self.title('Try Radar with two selections')
        self.click('Radar');self.title('Keep only NEON-BAZAAR');self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
