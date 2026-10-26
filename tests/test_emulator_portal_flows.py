"""Native Global Portal and Observer Probes & Karma offline flows; rebuild first."""
import unittest
from emulator_browser import browser_session, objects, click_text
import test_emulator_phase4_pcap_deep as navigation


class PortalFlowsBrowser(unittest.TestCase):
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

    def text(self):
        return '\n'.join(o['text'] for o in objects(self.page))

    def advance(self,ms):
        self.page.evaluate('(ms)=>{for(let left=ms;left>0;left-=100)emulator.module._emu_tick(Math.min(left,100))}',ms)

    def tick(self):
        self.page.evaluate('emulator.module._emu_tick(100)')

    def test_global_portal_setup_story_background_reopen_and_stop(self):
        self.click_label('Global WiFi\nAttacks')
        self.click_label('Portal')
        self.wait_text('Offline Portal')
        self.click('emu.portal.ssid')
        self.page.evaluate("()=>{for(let i=0;i<32;i++)emulator.module._emu_key(8);emulator.module.ccall('emu_text',null,['string'],['LAB Portal Browser'])}")
        self.click('emu.portal.start')
        job=self.page.evaluate("emulator.device.device.snapshot('grove').active")
        self.assertIsNotNone(job)
        self.assertEqual(self.page.evaluate('(id)=>emulator.device.device.job(id).operation',job),'portal_demo')
        self.assertEqual(self.page.evaluate('(id)=>emulator.device.device.job(id).result.demo.ssid',job),'LAB Portal Browser')
        self.advance(8000)
        self.wait_text('DEMO-only-2026!')
        self.click('emu.portal.background')
        self.assertEqual(self.page.evaluate("emulator.device.device.snapshot('grove').active"),job)
        self.click_label('Portal')
        self.wait_text('DEMO-only-2026!')
        self.click('emu.portal.stop')
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

    def test_observer_probe_template_story_and_stop(self):
        self.click_label('Network\nObserver')
        self.click_label('Start')
        self.click('02:20:77:00:00:01/observe')
        self.page.evaluate('()=>{for(let i=0;i<3;i++)emulator.module._emu_wheel(300,950,300)}')
        self.click_label('Karma')
        self.wait_text('Synthetic probe catalogue')
        self.click('emu.karma2.probe.0')
        self.wait_text('Select Portal HTML')
        self.click('emu.karma2.start')
        job=self.page.evaluate("emulator.device.device.snapshot('grove').active")
        self.assertIsNotNone(job)
        self.advance(8000)
        self.wait_text('DEMO-only-2026!')
        self.click('emu.karma2.background')
        self.assertEqual(self.page.evaluate("emulator.device.device.snapshot('grove').active"),job)
        self.click_label('Karma')
        self.wait_text('DEMO-only-2026!')
        self.click('emu.karma2.stop')
        self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))

    def test_global_portal_rejects_missing_sd_and_busy_module(self):
        self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:false})")
        self.click_label('Global WiFi\nAttacks')
        self.click_label('Portal')
        self.wait_text('SD missing')
        self.click('emu.portal.cancel')
        self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:true})")
        self.click_label('Portal')
        active=self.page.evaluate("emulator.device.device.attackStart('grove','karma')")
        self.click('emu.portal.start')
        self.wait_text('busy')
        self.assertEqual(self.page.evaluate("emulator.device.device.snapshot('grove').active"),active)


if __name__=='__main__':
    unittest.main(verbosity=2)
