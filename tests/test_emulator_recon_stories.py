"""S05/S06: native UI evidence and guide lifecycle, local Chromium."""
import unittest
from emulator_browser import ROOT,objects,click_text,click_object
import test_emulator_portal_demo as helpers

class ReconStoriesBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    advance=helpers.PortalDemoBrowser.advance
    clean=helpers.PortalDemoBrowser.clean
    click=helpers.PortalDemoBrowser.click
    def title(self,text):
        self.page.wait_for_function('(s)=>document.querySelector("#guide-title").textContent===s',arg=text,timeout=9000)
    def bound(self,fragment):
        return next(o for o in objects(self.page) if fragment in o['binding'])
    def tap(self,fragment):click_object(self.page,self.bound(fragment))
    def begin(self,kind,rotation=0):
        self.load(rotation);self.page.locator('#guide-'+kind).click()
        click_text(self.page,'Network\nObserver' if kind=='observer' else 'Mesh\nRecon')
        click_text(self.page,'Start',contains=kind=='mesh')
    def menu(self,kind):
        self.title(kind+' story complete')
        self.assertEqual(self.page.locator('#guide-leave').inner_text(),'Back to stories')
        self.page.locator('#guide-leave').click()
        self.assertTrue(self.page.locator('#guide-observer').is_visible())
        self.assertTrue(self.page.locator('#guide-mesh').is_visible());self.clean()
    def test_observer_capture_and_both_exit_choices_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.begin('observer',rotation);self.title('Open NEON-BAZAAR')
                self.assertEqual(p.evaluate('emulator.device.snapshot(0).networks'),[])
                self.assertTrue(any(o['binding'].endswith('/client') for o in objects(p)))
                self.tap('02:20:77:00:00:01/observe');self.title('Save a synthetic capture')
                self.tap('emu.phase3.observer.synthetic_capture');self.tap('emu.phase3.capture.generate')
                self.title('Return to Observer')
                files=p.evaluate("emulator.device.device.files('grove')")
                self.assertEqual(files[-1]['networkId'],'net-1');self.assertGreater(files[-1]['sizeBytes'],24)
                self.tap('emu.phase3.capture.close');self.tap('show_network_popup.close_btn')
                self.title('Try leaving Observer');self.tap('show_observer_page.back_btn')
                self.title('Keep Observer running');click_text(p,'Keep running')
                self.title('Open the exit choice again')
                self.assertTrue(p.evaluate("emulator.device.device.observer('grove').running"))
                self.tap('show_observer_page.back_btn');self.title('Stop and exit')
                p.screenshot(path=str(ROOT/f'docs/ui-emulator/observer-story-{rotation}.png'))
                click_text(p,'Stop and exit',contains=True)
                self.menu('Observer');self.assertFalse(p.evaluate("emulator.device.device.observer('grove').running"))
                self.assertEqual(p.evaluate("emulator.device.device.files('grove')"),files)
    def test_mesh_stop_clear_and_home_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.begin('mesh',rotation);self.title('Expand Mesh network 0x1A2B')
                self.tap('0x1A2B/pan');self.title('Stop Mesh monitoring')
                self.assertTrue(any(o['binding'].endswith('/node') for o in objects(p)))
                p.screenshot(path=str(ROOT/f'docs/ui-emulator/mesh-story-{rotation}.png'))
                click_text(p,'Stop',contains=True);self.title('Clear retained Mesh results')
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                click_text(p,'Clear');self.title('Return to GROVE')
                self.assertFalse(any(o['binding'].endswith('/pan') for o in objects(p)))
                self.tap('show_zig_recon_page.back_btn');self.menu('Mesh')
    def test_restart_hidden_module_and_cancel_require_fresh_session(self):
        p=self.page;self.begin('mesh');self.title('Watch Mesh networks appear')
        click_text(p,'MBUS');p.wait_for_timeout(2300)
        self.assertEqual(p.locator('#guide-title').inner_text(),'Watch Mesh networks appear')
        click_text(p,'GROVE');self.title('Expand Mesh network 0x1A2B')
        p.locator('#cancel').click();self.title('Start a fresh Mesh session')
        click_text(p,'Start',contains=True);self.title('Watch Mesh networks appear')
        p.locator('#guide-restart').click();self.title('Start a fresh Mesh session')
        p.wait_for_timeout(2200);self.title('Start a fresh Mesh session')
        click_text(p,'Stop',contains=True);click_text(p,'Start',contains=True)
        self.title('Watch Mesh networks appear');self.clean()
    def test_observer_restart_and_disconnect_do_not_complete(self):
        p=self.page;self.begin('observer');self.title('Open NEON-BAZAAR')
        p.locator('#guide-restart').click();self.title('Start a fresh Observer session')
        click_text(p,'Stop');click_text(p,'Start');self.title('Watch networks and clients')
        p.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.title('Start a fresh Observer session');self.clean()
    def test_empty_observer_and_cancelled_capture_cannot_supply_result(self):
        p=self.page
        p.goto(self.url+'/?wifiDemo=empty');self.ready()
        p.locator('#guide-observer').click();click_text(p,'Network\nObserver');click_text(p,'Start')
        self.title('Watch networks and clients');p.wait_for_timeout(2300)
        self.title('Watch networks and clients')
        self.assertEqual(p.evaluate("emulator.device.device.files('grove')"),[])
        self.begin('observer');self.title('Open NEON-BAZAAR')
        self.tap('02:20:77:00:00:01/observe');self.title('Save a synthetic capture')
        p.locator('#timing').select_option('1')
        self.tap('emu.phase3.observer.synthetic_capture');self.tap('emu.phase3.capture.generate')
        self.tap('emu.phase3.capture.close');self.title('Save a synthetic capture')
        self.assertEqual(p.evaluate("emulator.device.device.files('grove')"),[])
        self.tap('emu.phase3.observer.synthetic_capture');self.tap('emu.phase3.capture.generate')
        self.advance(10000);self.title('Return to Observer');self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
