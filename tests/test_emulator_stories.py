"""Guide completion follows real native Bluetooth controls, never Next clicks."""
import unittest
from emulator_browser import ROOT, objects, click_text, click_object
import test_emulator_portal_demo as helpers

class BluetoothStoryBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    advance=helpers.PortalDemoBrowser.advance
    def click(self,fragment):
        click_object(self.page,next(o for o in objects(self.page) if fragment in o['binding']))
    def progress(self):return self.page.locator('#guide-progress').inner_text()
    def scan(self):
        click_text(self.page,'Bluetooth');click_text(self.page,'BT Scan\n& Locate')

    def test_complete_native_story_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation);p.locator('#guide-start').click()
                self.assertEqual(self.progress(),'Step 1 of 4')
                self.scan();self.assertEqual(self.progress(),'Step 2 of 4')
                self.click('02:20:77:02:00:01/locate')
                p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 4 of 4'",timeout=6000)
                self.assertIn('dBm','\n'.join(o['text'] for o in objects(p)))
                if rotation in [0,1]:p.screenshot(path=str(ROOT/f'docs/ui-emulator/bluetooth-story-{rotation}.png'))
                self.click('show_bt_locator_page.back_btn')
                self.assertEqual(self.progress(),'4 of 4 complete')
                self.assertEqual(len([o for o in objects(p) if o['binding'].endswith('/locate')]),6)
                p.locator('#guide-leave').click();self.assertTrue(p.locator('#guide-panel').is_hidden())
                self.clean()

    def test_wrong_device_module_restart_and_reload_do_not_complete(self):
        p=self.page;self.load();p.locator('#guide-start').click()
        click_text(p,'MBUS');self.scan();self.assertEqual(self.progress(),'Step 1 of 4')
        self.click('02:20:77:02:00:01/locate');self.advance(4000)
        self.assertEqual(self.progress(),'Step 1 of 4')
        click_text(p,'GROVE');self.scan()
        self.click('02:20:77:02:00:02/locate');self.advance(2000)
        self.assertEqual(self.progress(),'Step 2 of 4')
        self.click('show_bt_locator_page.back_btn');self.click('02:20:77:02:00:01/locate')
        p.locator('#guide-restart').click();self.advance(4000)
        self.assertEqual(self.progress(),'Step 1 of 4')
        self.click('show_bt_locator_page.back_btn');self.click('show_bt_scan_page.rescan_btn')
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 2 of 4'")
        p.reload();self.ready();self.assertTrue(p.locator('#guide-panel').is_hidden());self.clean()

    def test_no_signal_cannot_complete_and_conditions_reset_to_normal(self):
        p=self.page;self.load();p.locator('#guide-start').click();self.scan()
        p.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.click('02:20:77:02:00:01/locate');self.advance(3000)
        self.assertEqual(self.progress(),'Step 3 of 4')
        self.assertIn('No signal',p.locator('#guide-instruction').inner_text())
        p.evaluate("emulator.device.device.setModule('grove',{connected:true})")
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 4 of 4'",timeout=6000)
        self.click('show_bt_locator_page.back_btn');self.assertEqual(self.progress(),'4 of 4 complete')
        p.locator('.demo-conditions summary').click()
        with p.expect_navigation():p.locator('#module-condition').select_option('missing-sd')
        self.ready();self.assertTrue(p.locator('#guide-panel').is_hidden())
        self.assertFalse(p.evaluate("emulator.device.snapshot(0).sdPresent"))
        with p.expect_navigation():p.locator('#reset').click()
        self.ready();self.assertEqual(p.locator('#module-condition').input_value(),'normal')
        self.assertTrue(p.evaluate("emulator.device.snapshot(0).sdPresent"));self.clean()

    def test_hidden_locator_does_not_advance_and_fullscreen_keeps_guide(self):
        p=self.page;self.load();p.locator('#guide-start').click();self.scan()
        # Realistic speed leaves time to switch tabs before three samples arrive.
        p.locator('#timing').select_option('1');self.click('02:20:77:02:00:01/locate')
        click_text(p,'MBUS');self.advance(4000)
        self.assertEqual(self.progress(),'Step 3 of 4')
        click_text(p,'GROVE')
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 4 of 4'",timeout=6000)
        p.locator('#fullscreen').click()
        p.wait_for_function("document.fullscreenElement?.tagName==='MAIN'")
        self.assertTrue(p.locator('#guide-panel').is_visible())
        p.locator('#fullscreen').click();p.wait_for_function('document.fullscreenElement===null')
        self.click('show_bt_locator_page.back_btn');self.assertEqual(self.progress(),'4 of 4 complete')
        self.clean()

    def test_restart_during_scan_delivery_rejects_previous_session(self):
        p=self.page
        # Register before the guide listener. Restart after native scan produced
        # evidence but before its delivery to the guide, without forging evidence.
        p.add_init_script("""addEventListener('emulator-bluetooth',e=>{
          if(globalThis.restartOnScan && e.detail.type==='scan'){
            globalThis.restartOnScan=false;
            document.querySelector('#guide-restart').click();
          }
        });""")
        self.load();p.locator('#guide-start').click()
        p.evaluate('globalThis.restartOnScan=true');self.scan()
        self.assertEqual(self.progress(),'Step 1 of 4')
        self.click('show_bt_scan_page.rescan_btn')
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 2 of 4'")
        self.clean()

    def test_hidden_browser_document_cannot_supply_viewing_evidence(self):
        p=self.page;self.load();p.locator('#guide-start').click();self.scan()
        self.click('02:20:77:02:00:01/locate')
        p.evaluate("""Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});
          document.dispatchEvent(new Event('visibilitychange'));""")
        p.wait_for_timeout(2400)
        self.assertEqual(self.progress(),'Step 3 of 4')
        p.evaluate("""delete document.hidden;
          document.dispatchEvent(new Event('visibilitychange'));""")
        self.assertEqual(self.progress(),'Step 3 of 4')
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 4 of 4'",timeout=6000)
        self.click('show_bt_locator_page.back_btn');self.assertEqual(self.progress(),'4 of 4 complete')
        self.clean()

    def test_rescan_queued_before_restart_keeps_its_original_session(self):
        p=self.page;self.load();p.locator('#guide-start').click();self.scan()
        button=next(o for o in objects(p) if 'show_bt_scan_page.rescan_btn' in o['binding'])
        # Deliver the real native pointer action and restart in one JS task,
        # before requestAnimationFrame can consume the deferred Rescan.
        p.evaluate("""o=>{
          const m=emulator.module,x=o.x+o.width/2,y=o.y+o.height/2;
          m._emu_pointer(x,y,1);m._emu_pointer(x,y,0);
          document.querySelector('#guide-restart').click();
          m._emu_tick(10);
        }""",button)
        self.assertEqual(self.progress(),'Step 1 of 4')
        self.click('show_bt_scan_page.rescan_btn')
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 2 of 4'")
        self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
