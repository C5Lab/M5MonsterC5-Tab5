"""S01 module tour through real native navigation and dialog controls."""
import unittest
from emulator_browser import ROOT,objects,click_text,click_object
import test_emulator_portal_demo as helpers

class StartupStoryBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    def title(self,value):
        self.page.wait_for_function('(s)=>document.querySelector("#guide-title").textContent===s',arg=value,timeout=6000)
    def wheel(self,amount):
        box=self.page.locator('#display').bounding_box()
        self.page.mouse.move(box['x']+box['width']*.75,box['y']+box['height']*.65)
        self.page.mouse.wheel(0,amount)
    def test_all_conditions_and_rotations(self):
        p=self.page
        for rotation in range(4):
            for condition in ['normal','missing-board','missing-sd','version-mismatch']:
                with self.subTest(rotation=rotation,condition=condition):
                    p.goto(f'{self.url}/?rotation={rotation}&moduleCondition={condition}');self.ready()
                    p.locator('#guide-startup').click()
                    if condition=='missing-board':
                        self.title('No external boards');click_text(p,'Continue Anyway')
                    if condition=='version-mismatch':
                        self.title('Check the version warning');click_text(p,'OK')
                    if condition=='missing-sd':
                        self.title('Check the SD warning');click_text(p,'Wardrive')
                        self.title('Return from the SD warning');click_text(p,'Cancel')
                    if condition!='missing-board':
                        self.title('Explore MBUS');click_text(p,'MBUS')
                    self.title('Explore INTERNAL');click_text(p,'INTERNAL')
                    self.title('Open Module Status');click_text(p,'Module\nStatus')
                    self.title('Read GROVE status')
                    self.title('Read MBUS status')
                    # MBUS text can be below the native viewport in landscape.
                    for _ in range(8):
                        if p.evaluate('!!(emulator.module._emu_startup_state()&256)'):break
                        self.wheel(130);p.wait_for_timeout(120)
                    self.title('Return to INTERNAL')
                    if condition=='normal':
                        p.screenshot(path=str(ROOT/f'docs/ui-emulator/startup-story-{rotation}.png'))
                    for _ in range(8):
                        back=next(o for o in objects(p) if 'emu.phase46.system.back' in o['binding'])
                        if back['y']>=0:break
                        self.wheel(-180);p.wait_for_timeout(120)
                    click_object(p,back)
                    self.title('Module tour complete')
                    self.assertTrue(p.locator('#guide-start').is_hidden())
                    p.locator('#guide-leave').click()
                    self.assertTrue(p.locator('#guide-start').is_visible())
                    self.assertTrue(p.locator('#guide-startup').is_visible())
                    self.clean()

    def test_switching_story_requires_new_bluetooth_evidence(self):
        p=self.page;p.goto(self.url);self.ready()
        p.locator('#guide-start').click();click_text(p,'Bluetooth');click_text(p,'BT Scan\n& Locate')
        p.wait_for_function("document.querySelector('#guide-progress').textContent==='Step 2 of 4'")
        old=p.evaluate('emulatorGuideEpoch')
        p.locator('#guide-leave').click();p.locator('#guide-startup').click()
        self.title('Explore GROVE')
        p.locator('#guide-leave').click();p.locator('#guide-start').click()
        self.assertGreater(p.evaluate('emulatorGuideEpoch'),old)
        self.assertEqual(p.locator('#guide-progress').inner_text(),'Step 1 of 4')
        self.clean()

    def test_wrong_module_warning_and_independent_status_cards(self):
        p=self.page;p.goto(self.url+'/?moduleCondition=missing-sd');self.ready()
        p.locator('#guide-startup').click();self.title('Check the SD warning')
        click_text(p,'MBUS');click_text(p,'Wardrive')
        p.wait_for_timeout(150);self.title('Check the SD warning')
        click_text(p,'Cancel');click_text(p,'GROVE');click_text(p,'Wardrive')
        self.title('Return from the SD warning');click_text(p,'Cancel')
        self.title('Explore MBUS');click_text(p,'MBUS')
        self.title('Explore INTERNAL');click_text(p,'INTERNAL')
        p.evaluate("emulator.device.device.setModule('grove',{connected:true,sdPresent:true});emulator.device.device.setModule('mbus',{connected:false,sdPresent:false})")
        click_text(p,'Module\nStatus');self.title('Read GROVE status')
        texts=[o['text'] for o in objects(p)]
        self.assertTrue(any(t.startswith('GROVE - Connected') and 'SD: present' in t for t in texts))
        self.assertTrue(any(t.startswith('MBUS - Disconnected') and 'SD: missing' in t for t in texts))
        # Restart within the status page must not credit the old navigation.
        p.locator('#guide-restart').click();self.title('Explore GROVE')
        p.wait_for_timeout(1200);self.title('Explore GROVE');self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
