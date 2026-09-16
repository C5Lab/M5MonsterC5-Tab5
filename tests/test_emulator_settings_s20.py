"""S20 real launcher/native controls; all rotations and cancellation/restart evidence."""
import unittest
from emulator_browser import click_text,objects
import test_emulator_time as time_native
import test_emulator_transfer_speed as baud_native
import test_emulator_portal_demo as helpers

class SettingsS20Browser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    text=helpers.PortalDemoBrowser.text
    toggle=time_native.TimeTests.toggle
    step_roller=time_native.TimeTests.step_roller
    choose=baud_native.TransferSpeedBrowser.choose
    def load(self,rotation=0):
        self.page.goto(f'{self.url}/?rotation={rotation}');self.ready()
    def begin(self,id,r):
        self.load(r)
        self.page.evaluate('emulatorSettings.set("rtc_offset",Date.parse("2027-01-31T13:05:00Z")-Date.now())')
        self.page.locator('#queue2-story').select_option(id);self.page.locator('#guide-queue2').click()
        if id=='s20-baud':baud_native.TransferSpeedBrowser.open(self)
        else:click_text(self.page,'INTERNAL');click_text(self.page,'Settings');click_text(self.page,'Time')
    def title(self,text):self.page.wait_for_function('(t)=>document.querySelector("#guide-title").textContent===t',arg=text,timeout=6000)
    def finish(self):
        self.page.wait_for_function('document.querySelector("#guide-leave").textContent==="Back to stories"',timeout=6000)
        self.page.locator('#guide-leave').click();self.assertTrue(self.page.locator('#queue2-story').is_visible());self.clean()
    def test_set_invalid_and_cancel_all_rotations(self):
        p=self.page
        for r in range(4):
            for kind in ['time','invalid','cancel']:
                with self.subTest(rotation=r,kind=kind):
                    self.begin('s20-'+kind,r)
                    self.title({'time':'Set a different date or time','invalid':'Try an invalid calendar date','cancel':'Edit without pressing Set'}[kind])
                    if kind=='invalid':
                        before=p.evaluate('emulatorSettings.get("rtc_offset",null)');self.step_roller(1);click_text(p,'\uf00c Set')
                        self.title('Correct the date and save');self.assertEqual(p.evaluate('emulatorSettings.get("rtc_offset",null)'),before)
                        self.step_roller(1);click_text(p,'\uf00c Set')
                    else:
                        self.step_roller(4)
                        if kind=='time':click_text(p,'\uf00c Set')
                    self.title('Return to Settings');click_text(p,'Close')
                    if kind=='cancel':
                        self.title('Check the original clock');click_text(p,'Time');self.title('Return to Settings');self.assertIn('13:05',self.text());click_text(p,'Close')
                    self.finish()
    def test_clock_preferences_all_rotations(self):
        p=self.page
        for r in range(4):
            for kind,label,title in [('format','24-hour format','Clock format'),('dst','Summer time (DST +1h)','Summer time (DST)'),('visibility','Show clock on top bar','Clock visibility')]:
                with self.subTest(rotation=r,kind=kind):
                    self.begin('s20-'+kind,r);self.title('Change '+title)
                    if kind=='format' and not p.evaluate('emulatorSettings.get("clock_show",1)'):self.toggle('Show clock on top bar')
                    self.toggle(label)
                    self.title('Return to Settings');click_text(p,'Close');self.title('Verify the saved preference');click_text(p,'Time')
                    self.title('Return to Settings');click_text(p,'Close');self.finish()
    def test_baud_saved_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                self.begin('s20-baud',r);self.title('Choose a different console baud');self.choose(3 if p.evaluate('emulatorSettings.get("ft_baud",460800)')!=921600 else 0)
                self.title('Return to Settings');click_text(p,'Close');self.title('Verify saved console baud');click_text(p,'Transfer\nSpeed')
                self.title('Return to Settings');click_text(p,'Close');self.finish()
    def test_restart_requires_reopen_and_fresh_change(self):
        p=self.page;self.begin('s20-format',0);self.title('Change Clock format');self.toggle('24-hour format');self.title('Return to Settings')
        p.locator('#guide-restart').click();self.title('Open Time');p.wait_for_timeout(300);self.title('Open Time')
        click_text(p,'Close');p.wait_for_timeout(150);click_text(p,'Time');self.title('Change Clock format');p.wait_for_timeout(300);self.title('Change Clock format')
        self.toggle('24-hour format');self.title('Return to Settings');click_text(p,'Close');self.title('Verify the saved preference');click_text(p,'Time');self.title('Return to Settings');click_text(p,'Close');self.finish()

if __name__=='__main__':unittest.main(verbosity=2)
