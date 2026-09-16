"""Native offline attack acceptance after rebuilding."""
import unittest

class NativeAttackBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from emulator_browser import browser_session
        cls.session=browser_session(); cls.browser,cls.url=cls.session.__enter__()
    @classmethod
    def tearDownClass(cls): cls.session.__exit__(None,None,None)
    def setUp(self):
        from emulator_browser import click_text,click_object,objects
        self.context=self.browser.new_context();self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.page.goto(self.url);self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(self.page,'WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')
        targets=[o for o in objects(self.page) if o['binding'].endswith('02:20:77:00:00:01/select')]
        self.assertEqual(len(targets),1);click_object(self.page,targets[0])
    def tearDown(self): self.assertEqual(self.errors,[])
    def tick(self): self.page.evaluate('()=>{for(let i=0;i<20;i++)emulator.module._emu_tick(100)}')
    def text(self):
        from emulator_browser import objects
        return '\n'.join(o['text'] for o in objects(self.page))
    def click(self,text):
        from emulator_browser import objects,click_object
        for attempt in range(12):
            candidates=[o for o in objects(self.page) if text==o['text'] or (text=='Start Rogue AP' and text in o['text'])]
            self.assertTrue(candidates,'Missing native control '+text)
            obj=candidates[-1];canvas=self.page.locator('#display');size=canvas.evaluate('(c)=>({w:c.width,h:c.height})')
            if 0<obj['x']+obj['width']/2<size['w'] and 0<obj['y']+obj['height']/2<size['h']:
                click_object(self.page,obj);self.page.evaluate('emulator.module._emu_tick(100)');return
            box=canvas.bounding_box()
            # Drag the real horizontal attack strip to reach offscreen tiles.
            y=box['y']+max(10,min(size['h']-10,obj['y']+obj['height']/2))*box['height']/size['h']
            start,end = (.65,.35) if obj['x']+obj['width']/2 >= size['w'] else (.35,.65)
            self.page.mouse.move(box['x']+box['width']*start,y);self.page.mouse.down()
            self.page.mouse.move(box['x']+box['width']*end,y,steps=12);self.page.mouse.up()
            self.page.wait_for_timeout(350)
        self.fail('Native control unreachable '+text)
    def test_mitm_stop_finalizes_file(self):
        self.click('MITM');self.assertIn('MITM Capture',self.text());self.click('Connect & Start');self.tick()
        self.assertIn('Simulated running',self.text());self.click('STOP CAPTURE');self.tick()
        self.assertIn('File:',self.text())
        self.assertTrue(self.page.evaluate("emulator.device.device.files('grove').some(f=>f.path.endsWith('.pcap'))"))
    def test_mitm_cancel_does_not_create_file(self):
        before=self.page.evaluate("emulator.device.device.files('grove').length")
        self.click('MITM');self.click('Cancel');self.tick()
        self.assertEqual(self.page.evaluate("emulator.device.device.files('grove').length"),before)
    def test_evil_twin_offline_template_and_disconnect(self):
        self.click('Evil Twin');self.assertIn('Portal HTML:',self.text());self.click('START ATTACK');self.tick()
        self.assertIn('Simulated running',self.text())
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})");self.tick()
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        self.assertNotIn('Simulated running',self.text())
    def test_rogue_ap_reset_releases_job(self):
        self.click('RogueAP');self.assertIn('Target SSID:',self.text());self.click('Start Rogue AP');self.tick()
        self.assertIn('Simulated running',self.text())
        self.page.evaluate('emulator.device.device.reset()');self.tick()
        self.assertIn('Operation reset',self.text())
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

if __name__ == '__main__': unittest.main()
