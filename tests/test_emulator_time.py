"""Native RTC acceptance; serve identical Wasm bytes via Playwright to avoid local transfer resets."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object, DIST

class TimeTests(unittest.TestCase):
    def setUp(self):
        self.session=browser_session();self.browser,self.url=self.session.__enter__()
        self.addCleanup(self.session.__exit__,None,None,None)
        self.context=self.browser.new_context(viewport={'width':1440,'height':1000})
        self.addCleanup(self.context.close)
        self.page=self.context.new_page();self.errors=[]
        self.page.on('pageerror',lambda e:self.errors.append(str(e)))
        self.page.add_init_script("globalThis.unavailable=[];addEventListener('emulator-unavailable',e=>unavailable.push(e.detail));Date.now=()=>1789228800000")
        self.page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))

    def ready(self):self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
    def text(self):return '\n'.join(o['text'] for o in objects(self.page))
    def open(self):
        click_text(self.page,'INTERNAL');click_text(self.page,'Settings');click_text(self.page,'Time')
    def load(self,rotation=0,date='2028-02-29T23:45:00Z'):
        p=self.page;p.goto(f'{self.url}/?rotation={rotation}');self.ready()
        p.evaluate('(s)=>emulatorSettings.set("rtc_offset",Date.parse(s)-Date.now())',date)
        self.open()
    def toggle(self,label):
        rows=objects(self.page);text=next(o for o in rows if o['text']==label)
        click_object(self.page,next(o for o in rows if o['parent']==text['parent'] and o['clickable']))
    def step_roller(self,index):
        rollers=sorted((o for o in objects(self.page) if o['width'] in (86,104) and o['height']==100 and o['clickable']),key=lambda o:o['x'])
        self.assertEqual(len(rollers),5)
        target=dict(rollers[index]);target['y']+=75;target['height']=1
        click_object(self.page,target)
        self.page.wait_for_timeout(250) # Native roller snap animation.
    def clean(self):
        self.assertEqual(self.page.evaluate('unavailable'),[]);self.assertEqual(self.errors,[])

    def test_picker_edits_persist_and_clock_ticks_in_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                self.assertIn('RTC now: 2028-02-29 23:45:00',self.text())
                self.step_roller(4);click_text(p,'\uf00c Set')
                self.assertIn('RTC set: 2028-02-29 23:46:00',self.text())
                click_text(p,'Close');p.reload();self.ready();self.open()
                self.assertIn('RTC now: 2028-02-29 23:46:00',self.text())
                p.evaluate('Date.now=()=>1789228861000')
                click_text(p,'Close');click_text(p,'Time')
                self.assertIn('RTC now: 2028-02-29 23:47:01',self.text())
                self.clean()

    def test_invalid_calendar_date_does_not_change_clock(self):
        self.load(date='2027-01-31T13:05:00Z')
        self.step_roller(1);click_text(self.page,'\uf00c Set')
        self.assertIn('Failed to write RTC.',self.text())
        click_text(self.page,'Close');click_text(self.page,'Time')
        self.assertIn('RTC now: 2027-01-31 13:05:00',self.text());self.clean()

    def test_format_visibility_dst_reload_and_reset(self):
        p=self.page;self.load()
        self.toggle('Summer time (DST +1h)')
        self.assertIn('RTC now: 2028-03-01 00:45:00',self.text())
        self.toggle('24-hour format')
        self.assertIn('12:45',self.text())
        self.toggle('Show clock on top bar');self.assertNotIn('12:45',self.text())
        click_text(p,'Close');p.reload();self.ready();self.open()
        self.assertEqual(p.evaluate('["clock_dst","clock_24h","clock_show"].map(k=>emulatorSettings.get(k,-1))'),[1,0,0])
        self.assertIn('RTC now: 2028-03-01 00:45:00',self.text())
        self.toggle('Show clock on top bar');self.toggle('Summer time (DST +1h)')
        self.assertIn('RTC now: 2028-02-29 23:45:00',self.text())
        with p.expect_navigation():p.locator('#reset').click()
        self.ready();self.open()
        self.assertNotIn('2028-02-29',self.text())
        self.assertEqual(p.evaluate('emulatorSettings.get("rtc_offset",null)'),None)
        self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
