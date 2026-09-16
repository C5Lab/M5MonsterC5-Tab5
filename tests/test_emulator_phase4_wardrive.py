"""Phase 4.2 acceptance through the native Tab5 Wardrive screens and simulated data."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object

class WardriveBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session=browser_session();cls.browser,cls.url=cls.session.__enter__()
    @classmethod
    def tearDownClass(cls):cls.session.__exit__(None,None,None)
    def setUp(self):
        self.context=self.browser.new_context();self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.open()
    def open(self,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}')
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page,'Wardrive')
    def tearDown(self):
        self.assertEqual(self.errors,[])
        self.assertEqual(self.page.evaluate('unavailable'),[])
    def click(self,key):
        rows=[o for o in objects(self.page) if key in o['binding']]
        self.assertEqual(len(rows),1,key)
        for _ in range(12):
            all_rows=objects(self.page);obj=next(o for o in all_rows if key in o['binding'])
            lookup={o['id']:o for o in all_rows};parents=[];parent=lookup.get(obj['parent'])
            while parent:
                parents.append(parent);parent=lookup.get(parent['parent'])
            width,height=self.page.locator('#display').evaluate('(c)=>[c.width,c.height]')
            clip=[0,0,width,height]
            for parent in reversed(parents):
                candidate=[max(clip[0],parent['x']),max(clip[1],parent['y']),
                           min(clip[2],parent['x']+parent['width']),min(clip[3],parent['y']+parent['height'])]
                if candidate[2]>candidate[0] and candidate[3]>candidate[1]:clip=candidate
            x=obj['x']+obj['width']/2;y=obj['y']+obj['height']/2
            if clip[0]<=x<clip[2] and clip[1]<=y<clip[3]:
                click_object(self.page,obj);return
            self.page.evaluate('(a)=>emulator.module._emu_wheel(...a)',
                               [(clip[0]+clip[2])/2,(clip[1]+clip[3])/2,300 if y>=clip[3] else -300])
        self.fail('Control is not reachable by scrolling: '+key)
    def tick(self,n=20):self.page.evaluate('(n)=>{for(let i=0;i<n;i++)emulator.module._emu_tick(100)}',n)
    def state(self,module='grove'):
        return self.page.evaluate('(m)=>emulator.device.device.wardrive(m)',module)
    def text(self):return '\n'.join(o['text'] for o in objects(self.page))
    def start(self):self.click('show_wardrive_page.ctx--wardrive_start_btn')
    def stop(self):self.click('show_wardrive_page.ctx--wardrive_stop_btn')
    def test_record_save_upload_and_gps(self):
        self.start();self.tick();s=self.state()
        self.assertTrue(s['running']);self.assertTrue(s['gps']['fix'])
        self.assertIn(f"WiFi: {s['wifiCount']}  BT: {s['btCount']}",self.text())
        self.stop();self.start();self.tick(3);self.stop()
        self.assertEqual(len(self.state()['sessions']),2)
        self.click('show_wardrive_page.ctx--wardrive_setup_btn')
        self.assertIn('Wardrive Setup',self.text())
        self.assertIn('Memory cap',self.text())
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_gps_debug_btn')
        self.assertIn('GPS Debug',self.text())
        self.click('wardrive_gps_debug_btn_cb.ctx--wardrive_gps_debug_start_btn');self.tick(2)
        self.assertIn('$GPGGA',self.text())
        self.page.evaluate("emulator.device.device.wardriveSetGps('grove',false)");self.tick(2)
        self.assertIn('Fix: waiting',self.text())
        self.click('wardrive_gps_debug_btn_cb.ctx--wardrive_gps_debug_close_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_home_btn')
        self.assertIn('Home',self.text())
        self.click('show_home_mgmt_overlay.close_btn')
        self.click('wardrive_setup_btn_cb.bl_btn')
        self.assertIn('Blacklist',self.text())
        self.click('wardrive_blacklist_btn_cb.close_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_apply_btn')
        self.assertIn('Applied',self.text())
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
        self.click('show_wardrive_page.ctx--wardrive_upload_btn');self.tick(2)
        self.click('show_wardrive_upload_menu.wigle_btn')
        self.assertIn('WiGLE Upload',self.text())
        for outcome,uploaded in [('failure',0),('partial',1),('success',1)]:
            self.page.evaluate('(v)=>globalThis.emulatorWardriveUploadOutcome=v',outcome)
            self.click('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn');self.tick(3)
            self.assertEqual(self.state()['upload']['uploaded'],uploaded)
        self.assertTrue(all(s['uploadStatus']['wigle']=='done' for s in self.state()['sessions']))
        self.click('show_wardrive_upload_popup.close_btn');self.tick(1)
        self.click('show_wardrive_upload_menu.wdgwars_btn')
        self.assertIn('WDGWars Upload',self.text())
        self.click('show_wardrive_upload_popup.ctx--wardrive_wigle_sync_btn');self.tick(3)
        self.assertEqual(self.state()['upload']['uploaded'],2)
    def test_native_lists_use_scenario_rows(self):
        self.click('show_wardrive_page.ctx--wardrive_setup_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_home_btn')
        self.click('show_home_mgmt_overlay.scan_btn')
        rows=[o for o in objects(self.page) if 'emu.phase4.wardrive.home.' in o['binding']]
        self.assertEqual(len(rows),12)
        self.click(rows[0]['binding']);self.click('show_home_mgmt_overlay.add_btn')
        self.assertIn('NEON-BAZAAR',self.text())
        self.click('show_home_mgmt_overlay.close_btn')
        self.click('wardrive_setup_btn_cb.bl_btn')
        self.click('wardrive_blacklist_btn_cb.scan_btn')
        rows=[o for o in objects(self.page) if 'wardrive_blacklist_scan_btn_cb.row.clicked' in o['binding']]
        self.assertEqual(len(rows),6)
        self.click(rows[0]['binding']);self.tick(1)
        self.assertIn('02:20:77:02:00:01',self.text())
        self.click('wardrive_blacklist_btn_cb.close_btn')
        self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
        self.start();self.tick()
        self.assertEqual(self.state()['btCount'],5)

    def test_disconnect_and_missing_sd(self):
        self.start();self.tick(3)
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.tick(1);self.assertFalse(self.state()['running'])
        self.assertEqual(self.state()['sessions'],[])
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:true,sdPresent:false})")
        self.start();self.tick(3);self.stop()
        self.assertEqual(self.state()['error'],'sd_missing')
        self.assertIn('sd_missing',self.text())
    def test_modules_and_cancel(self):
        self.start();self.tick(3)
        self.click('create_tab_bar.mbus_tab_btn');click_text(self.page,'Wardrive')
        self.assertEqual(self.state('mbus')['wifiCount'],0)
        self.start();self.tick(3)
        self.page.locator('#cancel').click();self.tick(1)
        self.assertFalse(self.state('mbus')['running']);self.assertTrue(self.state()['running'])
        self.click('create_tab_bar.grove_tab_btn');self.stop()
        self.assertEqual(len(self.state()['sessions']),1)
    def test_start_stop_all_rotations(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.open(rotation);self.start();self.tick(3);self.stop()
                self.assertEqual(len(self.state()['sessions']),1)
                self.click('show_wardrive_page.ctx--wardrive_setup_btn')
                self.assertIn('Wardrive Setup',self.text())
                self.click('wardrive_setup_btn_cb.ctx--wardrive_setup_close_btn')
                self.assertEqual(self.page.evaluate('unavailable'),[])

if __name__=='__main__':unittest.main()
