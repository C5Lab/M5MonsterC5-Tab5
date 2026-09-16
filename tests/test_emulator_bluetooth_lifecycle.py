"""Bluetooth rescan, disconnect and module lifecycle through the retained UI."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object

class BluetoothLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.session=browser_session();cls.browser,cls.url=cls.session.__enter__()
    @classmethod
    def tearDownClass(cls): cls.session.__exit__(None,None,None)
    def setUp(self):
        self.context=self.browser.new_context();self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.page.goto(self.url);self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.page.evaluate("globalThis.unavailable=[]; addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
        click_text(self.page,'Bluetooth');click_text(self.page,'BT Scan\n& Locate')
    def tearDown(self):
        self.assertEqual(self.errors,[])
        self.assertEqual(self.page.evaluate('unavailable'),[])
    def click(self,fragment):
        matches=[o for o in objects(self.page) if fragment in o['binding']]
        self.assertEqual(len(matches),1,[(m['binding'],m['text']) for m in matches])
        click_object(self.page,matches[0])
    def rows(self): return [o for o in objects(self.page) if o['binding'].endswith('/locate')]
    def test_rescan_requeries_disconnected_then_reconnected_module(self):
        p=self.page
        initial={o['binding'] for o in self.rows()};self.assertEqual(len(initial),6)
        p.evaluate("emulator.device.device.setModule('grove',{connected:false})")
        self.click('show_bt_scan_page.rescan_btn')
        p.evaluate('emulator.module._emu_tick(100)')
        self.assertEqual(self.rows(),[])
        p.evaluate("emulator.device.device.setModule('grove',{connected:true})")
        self.click('show_bt_scan_page.rescan_btn')
        p.evaluate('emulator.module._emu_tick(100)')
        self.assertEqual({o['binding'] for o in self.rows()},initial)
    def test_locator_exact_signal_disconnect_and_return(self):
        self.click('02:20:77:02:00:01/locate')
        samples=self.page.evaluate(r'''() => {
            const samples=[];
            for(let i=0;i<8;i++) {
                emulator.module._emu_tick(100);
                emulator.module._emu_inspect();
                const text=emulatorObjects.find(o=>/^-?\d+ dBm$/.test(o.text))?.text;
                samples.push([parseInt(text),emulator.device.device.bluetoothRssi('grove','02:20:77:02:00:01')]);
            }
            return samples;
        }''')
        self.assertTrue(all(a==b for a,b in samples),samples)
        self.assertGreater(len({a for a,b in samples}),1)
        self.page.evaluate("emulator.device.device.setModule('grove',{connected:false});emulator.module._emu_tick(100)")
        self.assertTrue(any('No signal' in o['text'] for o in objects(self.page)))
        self.click('show_bt_locator_page.back_btn')
        self.page.evaluate('emulator.module._emu_tick(1000)')
        self.assertTrue(any('Tap device to locate' in o['text'] for o in objects(self.page)))

    def test_cached_grove_list_survives_empty_mbus_scan(self):
        p=self.page
        initial={o['binding'] for o in self.rows()}
        p.evaluate("emulator.device.device.setModule('mbus',{connected:false})")
        self.click('create_tab_bar.mbus_tab_btn')
        click_text(p,'Bluetooth');click_text(p,'BT Scan\n& Locate')
        self.assertEqual(self.rows(),[])
        self.click('create_tab_bar.grove_tab_btn')
        self.assertEqual({o['binding'] for o in self.rows()},initial)
        self.click('02:20:77:02:00:01/locate')
        self.assertTrue(any('02:20:77:02:00:01' in o['text'] for o in objects(p)))

    def test_discovery_and_locator_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                p.goto(f'{self.url}/?rotation={rotation}')
                p.wait_for_function('globalThis.emulator?.measurements.frames>2')
                p.evaluate("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail))")
                click_text(p,'Bluetooth');click_text(p,'BT Scan\n& Locate')
                self.assertEqual(len(self.rows()),6)
                self.click('02:20:77:02:00:01/locate')
                p.evaluate('emulator.module._emu_tick(100)')
                self.assertTrue(any('dBm' in o['text'] for o in objects(p)))
                self.click('show_bt_locator_page.back_btn')
                self.assertEqual(len(self.rows()),6)
                self.assertEqual(p.evaluate('unavailable'),[])

if __name__=='__main__':unittest.main()
