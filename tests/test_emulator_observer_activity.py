"""Native Observer activity acceptance after rebuilding the emulator."""
import unittest
from emulator_browser import DIST, browser_session,objects,click_text,click_object

class ObserverActivityBrowser(unittest.TestCase):
    def test_live_activity_preserves_controls_then_freezes_on_stop(self):
        with browser_session() as (browser,url):
            page=browser.new_page()
            try:
                errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
                page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))
                page.goto(url);page.wait_for_function('globalThis.emulator?.measurements.frames>2')
                click_text(page,'WiFi Scan\n& Attack')
                page.wait_for_function('emulator.module._emu_scan_state(0)===2')
                row=next(o for o in objects(page) if o['binding'].endswith('02:20:77:00:00:01/select'))
                click_object(page,row)
                page.evaluate('emulator.module._emu_show(0,0)')
                click_text(page,'Network\nObserver');click_text(page,'Start')
                before={o['binding']:o['id'] for o in objects(page) if o['binding'].endswith(('/observe','/client'))}
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                self.assertGreater(page.evaluate("emulator.device.device.observer('grove').packets"),0)
                self.assertIn('clients active','\n'.join(o['text'] for o in objects(page)))
                after={o['binding']:o['id'] for o in objects(page) if o['binding'].endswith(('/observe','/client'))}
                self.assertEqual(before,after)
                click_text(page,'Stop')
                frozen=page.evaluate("emulator.device.device.observer('grove')")
                page.evaluate('()=>{for(let i=0;i<30;i++)emulator.module._emu_tick(100)}')
                self.assertEqual(page.evaluate("emulator.device.device.observer('grove')"),frozen)
                self.assertEqual(errors,[])
            finally:page.close()

if __name__=='__main__':unittest.main()
