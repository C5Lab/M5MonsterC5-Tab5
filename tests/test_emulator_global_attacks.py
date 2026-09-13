"""Native Global WiFi confirmation/active screens backed by offline jobs."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object

KINDS=[('Blackout','blackout'),('Handshaker','global_handshaker'),('SnifferDog','snifferdog')]

class GlobalAttacksBrowser(unittest.TestCase):
    def setUp(self):
        self.session=browser_session();self.browser,self.url=self.session.__enter__()
        self.addCleanup(self.session.__exit__,None,None,None)
        self.context=self.browser.new_context(viewport={'width':1440,'height':1000})
        self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.page.add_init_script("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")

    def ready(self):
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')

    def menu(self):
        click_text(self.page,'Global WiFi\nAttacks')

    def load(self,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}');self.ready();self.menu()

    def text(self):return '\n'.join(o['text'] for o in objects(self.page))

    def start(self,label):
        click_text(self.page,label);click_text(self.page,'Yes')
        job=self.page.evaluate('emulator.device.snapshot(emulator.module._emu_current_tab()).active')
        self.assertIsNotNone(job,'Confirmation must start a simulated operation')
        return job

    def stop(self,kind):
        fragment=f'show_{kind}_active_popup.stop_btn'
        click_object(self.page,next(o for o in objects(self.page) if fragment in o['binding']))

    def advance(self):self.page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')

    def test_confirm_no_yes_stop_all_operations_and_rotations(self):
        p=self.page
        for rotation in range(4):
            for label,kind in KINDS:
                with self.subTest(rotation=rotation,kind=kind):
                    self.load(rotation)
                    click_text(p,label);click_text(p,'No')
                    self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                    job=self.start(label);self.advance()
                    self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).operation',job),kind)
                    self.assertIn('Attack in Progress',self.text())
                    if kind=='global_handshaker':
                        result=p.evaluate('(id)=>emulator.device.device.job(id).result.handshakes',job)
                        self.assertGreater(len(result),0)
                        self.assertIn(f'Total: {len(result)}',self.text())
                        self.assertIn(result[-1]['ssid'],self.text())
                    self.stop(kind)
                    self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'completed')
                    self.assertNotIn('Attack in Progress',self.text())
                    self.assertEqual(p.evaluate('unavailable'),[])
        self.assertEqual(self.errors,[])

    def test_disconnect_rejects_start_cancel_reset_and_reopen(self):
        p=self.page
        for label,kind in KINDS:
            with self.subTest(kind=kind):
                self.load()
                p.evaluate("emulator.device.device.setModule('grove',{connected:false})")
                click_text(p,label);click_text(p,'Yes')
                self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                self.assertIn('disconnected',self.text().lower())
                click_text(p,'No')
                p.evaluate("emulator.device.device.setModule('grove',{connected:true})")
                job=self.start(label)
                p.locator('#cancel').click();self.advance()
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'cancelled')
                self.assertNotIn('Attack in Progress',self.text())
                self.stop(kind);self.menu();job=self.start(label)
                p.evaluate("emulator.device.device.setModule('grove',{connected:false})");self.advance()
                self.assertNotIn('Attack in Progress',self.text())
                self.stop(kind)
                with p.expect_navigation():p.locator('#reset').click()
                self.ready();self.menu();self.start(label)
                with p.expect_navigation():p.locator('#reset').click()
                self.ready();self.assertIsNone(p.evaluate('emulator.device.snapshot(0).active'))
                self.assertEqual(p.evaluate('unavailable'),[])
        self.assertEqual(self.errors,[])

    def test_two_modules_have_independent_global_handshake_results_and_stop(self):
        p=self.page;self.load()
        grove=self.start('Handshaker');self.advance()
        click_text(p,'MBUS');self.menu();mbus=self.start('Handshaker')
        self.advance();self.stop('global_handshaker')
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',mbus),'completed')
        self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',grove),'running')
        click_text(p,'GROVE');self.advance()
        result=p.evaluate('(id)=>emulator.device.device.job(id).result.handshakes',grove)
        self.assertIn(f'Total: {len(result)}',self.text())
        self.stop('global_handshaker')
        self.assertEqual(p.evaluate('unavailable'),[]);self.assertEqual(self.errors,[])

    def test_busy_refusal_preserves_existing_operation(self):
        p=self.page
        for label,kind in KINDS:
            with self.subTest(kind=kind):
                self.load()
                job=p.evaluate("emulator.device.device.attackStart('grove','karma')")
                click_text(p,label);click_text(p,'Yes')
                self.assertIn('busy',self.text().lower())
                click_text(p,'No')
                self.assertEqual(p.evaluate('emulator.device.snapshot(0).active'),job)
                self.assertEqual(p.evaluate('(id)=>emulator.device.device.job(id).state',job),'running')
                p.evaluate('(id)=>emulator.device.device.cancel(id)',job)
                self.start(label);self.stop(kind)
                self.assertEqual(p.evaluate('unavailable'),[])
        self.assertEqual(self.errors,[])

if __name__=='__main__':unittest.main(verbosity=2)
