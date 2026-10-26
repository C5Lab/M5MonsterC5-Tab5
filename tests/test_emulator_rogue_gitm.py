"""Native Rogue GITM route and offline Capture Gateway handoff; rebuild first."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object
import test_emulator_phase4_pcap_deep as navigation


class RogueGitmBrowser(unittest.TestCase):
    click = navigation.PcapDeepBrowser.click
    click_label = navigation.PcapDeepBrowser.click_label
    wait_text = navigation.PcapDeepBrowser.wait_text

    @classmethod
    def setUpClass(cls):
        cls.session=browser_session()
        cls.browser,cls.url=cls.session.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.session.__exit__(None,None,None)

    def setUp(self):
        self.context=self.browser.new_context()
        self.addCleanup(self.context.close)
        self.page=self.context.new_page()
        self.errors=[]
        self.page.on('pageerror',lambda error:self.errors.append(str(error)))
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")

    def tearDown(self):
        self.assertEqual(self.errors,[])
        self.assertEqual(self.page.evaluate('unavailable'),[])

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def text(self):
        return '\n'.join(o['text'] for o in objects(self.page))

    def rogue(self):
        for _ in range(15):
            rows=objects(self.page)
            label=next(o for o in rows if o['text']=='Rogue GITM')
            button=next(o for o in rows if o['id']==label['parent'])
            strip=next(o for o in rows if o['id']==button['parent'])
            width=self.page.locator('#display').evaluate('(c)=>c.width')
            if 0<=button['x']<width-button['width']:
                click_object(self.page,button)
                self.tick()
                return
            self.page.evaluate('(p)=>emulator.module._emu_wheel(...p)',
                               [max(20,min(width-20,strip['x']+strip['width']/2)),
                                strip['y']+strip['height']/2,300])
        self.fail('Rogue GITM strip action unreachable')

    def scan(self):
        self.click_label('WiFi Scan\n& Attack')
        self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')

    def select(self,bssid):
        self.click(bssid+'/select')

    def test_selected_victim_uplink_live_stop_and_pcap(self):
        self.scan()
        self.rogue()
        self.assertIn('Select exactly 1 network for Rogue GITM',self.text())
        self.select('02:20:77:00:00:01')
        self.rogue()
        self.wait_text('Uplink (Internet, ch 1)')
        self.assertIn('NEON-BAZAAR',self.text())
        self.click('emu.rogue-gitm.start')
        job=self.page.evaluate("emulator.device.device.snapshot('grove').active")
        self.assertIsNotNone(job)
        self.assertEqual(self.page.evaluate('(id)=>emulator.device.device.job(id).tool',job),'rogue_gitm')
        self.page.evaluate('()=>{for(let i=0;i<60;i++)emulator.module._emu_tick(100)}')
        self.wait_text('Simulated Rogue GITM capture running')
        self.click('/gitm/Grove/static/stop')
        self.wait_text('Simulated PCAP finalized')
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        files=self.page.evaluate("emulator.device.device.files('grove')")
        self.assertEqual(len(files),1)
        self.assertEqual(files[0]['kind'],'rogue_gitm')
        self.assertEqual(files[0]['networkId'],'net-1')
        self.assertEqual(files[0]['upstreamId'],'net-2')

    def test_single_channel_candidate_and_observer_boundary(self):
        self.scan()
        self.select('02:20:77:00:00:04')
        self.rogue()
        self.assertIn('No other network on ch 36 for the uplink',self.text())
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        self.page.evaluate('emulator.module._emu_show(0,0)')
        self.click_label('Network\nObserver')
        self.click_label('Start')
        self.click('02:20:77:00:00:01/observe')
        self.rogue()
        self.assertIn('open it from WiFi Scan & Attack',self.text())

    def test_stop_before_capture_does_not_report_or_save_pcap(self):
        self.scan()
        self.select('02:20:77:00:00:01')
        self.rogue()
        self.click('emu.rogue-gitm.start')
        self.click('/gitm/Grove/static/stop')
        self.wait_text('Stopped before capture')
        self.assertNotIn('Simulated PCAP finalized',self.text())
        self.assertEqual(self.page.evaluate("emulator.device.device.files('grove')"),[])

    def test_storage_and_busy_refusal_leave_setup_reviewable(self):
        self.scan()
        self.select('02:20:77:00:00:01')
        self.rogue()
        self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:false})")
        self.click('emu.rogue-gitm.start')
        self.assertIn('SD missing',self.text())
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
        self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:true})")
        active=self.page.evaluate("emulator.device.device.attackStart('grove','karma')")
        self.click('emu.rogue-gitm.start')
        self.assertIn('busy',self.text())
        self.assertEqual(self.page.evaluate("emulator.device.device.snapshot('grove').active"),active)
        self.click('emu.rogue-gitm.cancel')
        self.assertNotIn('Demo mirror pass',self.text())


if __name__=='__main__':
    unittest.main(verbosity=2)
