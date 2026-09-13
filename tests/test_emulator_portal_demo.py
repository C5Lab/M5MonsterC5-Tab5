"""Offline client/portal/submission demonstration through native controls."""
import unittest
from emulator_browser import browser_session,objects,click_text,click_object,DIST
import test_emulator_attacks_rogue_evil_mitm as native

class PortalDemoBrowser(unittest.TestCase):
    click=native.NativeAttackBrowser.click
    def setUp(self):
        self.session=browser_session();self.browser,self.url=self.session.__enter__()
        self.addCleanup(self.session.__exit__,None,None,None)
        self.context=self.browser.new_context(viewport={'width':1440,'height':1000})
        self.addCleanup(self.context.close);self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.page.add_init_script("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        self.page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))
    def load(self,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}');self.ready()
    def ready(self):self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
    def text(self):return '\n'.join(o['text'] for o in objects(self.page))
    def advance(self,ms):self.page.evaluate('(ms)=>{for(let i=0;i<ms;i+=100)emulator.module._emu_tick(Math.min(100,ms-i))}',ms)
    def sequence(self):
        self.advance(100)
        self.assertIn('Waiting for a demo client',self.text())
        self.assertNotIn('Password: DEMO',self.text())
        self.advance(21000);self.assertIn('1. Client connected:',self.text())
        self.assertNotIn('Password: DEMO',self.text())
        self.advance(30000);self.assertIn('2. Client opened the portal',self.text())
        self.assertNotIn('Password: DEMO',self.text())
        self.advance(30000);self.assertIn('3. Demo password submitted',self.text())
        self.assertIn('Password: DEMO-only-2026!',self.text())
    def clean(self):
        self.assertEqual(self.page.evaluate('unavailable'),[]);self.assertEqual(self.errors,[])
    def scan(self,bssid="02:20:77:00:00:01"):
        click_text(self.page,'WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        click_object(self.page,next(o for o in objects(self.page) if o['binding'].endswith(bssid+'/select')))
        self.page.evaluate('emulator.module._emu_set_timing(1)')
    def portal(self):
        p=self.page;click_text(p,'INTERNAL');click_text(p,'Ad Hoc\nPortal & Karma')
        click_text(p,'\uf00b Show Probes');click_text(p,'1. NEON-BAZAAR')
        click_text(p,'\uf04b Start')
    def test_evil_and_rogue_sequence_all_rotations(self):
        for rotation in range(4):
            for label,start,stop in [('Evil Twin','START ATTACK','STOP'),('RogueAP','Start Rogue AP','Stop Rogue AP')]:
                with self.subTest(rotation=rotation,label=label):
                    self.load(rotation);self.scan();self.click(label)
                    self.clean() # Opening the form must enroll every native action before Start.
                    self.click(start)
                    self.sequence();job=self.page.evaluate('emulator.device.snapshot(0).active')
                    self.click(stop)
                    self.assertEqual(self.page.evaluate('(id)=>emulator.device.device.job(id).state',job),'completed')
                    self.clean()
    def test_chrome_clinic_without_seeded_client_completes(self):
        self.load();self.scan('02:20:77:00:00:03');self.click('Evil Twin');self.clean();self.click('START ATTACK')
        self.sequence();self.assertIn('SSID: CHROME-CLINIC',self.text());self.clean()

    def test_internal_portal_sequence_stop_and_reopen(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation);p.evaluate('emulator.module._emu_set_timing(1)');self.portal();self.sequence()
                job=p.evaluate('emulator.device.device.snapshot("internal").active')
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                click_text(p,'STOP PORTAL',contains=True)
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'completed')
                self.assertNotIn('Password: DEMO',self.text());self.clean()
    def test_cancel_before_submission_and_reset(self):
        p=self.page;self.load();p.evaluate('emulator.module._emu_set_timing(1)');self.portal()
        self.advance(21000);p.locator('#cancel').click();self.advance(90000)
        self.assertNotIn('Password: DEMO',self.text())
        self.assertIsNone(p.evaluate('emulator.device.device.snapshot("internal").active'))
        click_text(p,'STOP PORTAL',contains=True)
        click_text(p,'\uf00b Show Probes');click_text(p,'1. NEON-BAZAAR');click_text(p,'\uf04b Start')
        with p.expect_navigation():p.locator('#reset').click()
        self.ready();self.assertIsNone(p.evaluate('emulator.device.device.snapshot("internal").active'));self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
