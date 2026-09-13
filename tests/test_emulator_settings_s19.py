"""S19 native setting changes, real canvas effects and one-shot rotation reload."""
import unittest
from emulator_browser import objects,click_text,click_object
import test_emulator_portal_demo as helpers
import test_emulator_phase4_pcap_deep as nav
from test_emulator_phase2 import dropdown,bound,position,stored

class SettingsS19Browser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    click=nav.PcapDeepBrowser.click
    def tick(self):self.page.evaluate('emulator.module._emu_tick(10)')
    def begin(self,kind,r):
        self.load(r);self.page.locator('#queue2-story').select_option('s19-'+kind);self.page.locator('#guide-queue2').click()
        click_text(self.page,'INTERNAL');click_text(self.page,'Settings')
    def title(self,text):self.page.wait_for_function('(t)=>document.querySelector("#guide-title").textContent===t',arg=text,timeout=6000)
    def finish(self):
        self.page.wait_for_function('document.querySelector("#guide-leave").textContent==="Back to stories"',timeout=6000)
        self.page.locator('#guide-leave').click();self.assertTrue(self.page.locator('#queue2-story').is_visible());self.clean()
    def test_timeout_saved_reopened_restored_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                self.begin('timeout',r);self.click('/Screen Timeout');self.title('Change timeout')
                dropdown(p,'show_screen_timeout_popup.dropdown.',0);self.title('Close timeout');click_text(p,'Close')
                self.title('Reopen Screen Timeout');self.click('/Screen Timeout');self.title('Restore timeout')
                self.assertEqual(stored(p,'scr_timeout'),'0');dropdown(p,'show_screen_timeout_popup.dropdown.',4)
                self.title('Close timeout');click_text(p,'Close');self.finish();self.assertEqual(stored(p,'scr_timeout'),'4')
    def test_brightness_effect_reopened_restored_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                self.begin('brightness',r);self.click('/Screen Brightness');self.title('Change brightness')
                slider=bound(p,'show_screen_brightness_popup.screen_brightness_slider.value_changed')
                p.mouse.click(*position(p,slider,.3));self.title('Close brightness');value=int(stored(p,'scr_bright'))
                self.assertLess(value,50);self.assertEqual(p.locator('#display').evaluate('(c)=>c.style.filter'),f'brightness({value/100:g})')
                click_text(p,'Close');self.title('Reopen Screen Brightness');self.click('/Screen Brightness');self.title('Restore brightness')
                self.assertTrue(any(o['text']==f'{value}%' for o in objects(p)))
                slider=bound(p,'show_screen_brightness_popup.screen_brightness_slider.value_changed')
                # The native slider track spans 1..100; restore by real canvas pointer input.
                p.mouse.click(*position(p,slider,.8))
                current=int(stored(p,'scr_bright'))
                if current!=80:
                    p.mouse.click(*position(p,slider,(80-1)/99))
                self.title('Close brightness');click_text(p,'Close');self.finish();self.assertEqual(stored(p,'scr_bright'),'80')
    def test_theme_rebuild_reopen_restore_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                self.begin('theme',r);self.click('/Theme');self.title('Change dark mode')
                before=p.locator('#display').screenshot();self.click('show_theme_popup.dark_switch.')
                self.title('Reopen Theme');self.assertEqual(stored(p,'dark_mode'),'0')
                self.click('/Theme');self.title('Restore dark mode');self.assertNotEqual(before,p.locator('#display').screenshot());self.assertFalse(bound(p,'show_theme_popup.dark_switch.')['state']&1)
                self.click('show_theme_popup.dark_switch.');self.title('Check restored Theme');self.click('/Theme');self.title('Change Dashboard')
                self.assertTrue(bound(p,'show_theme_popup.dark_switch.')['state']&1)
                self.click('show_theme_popup.dashboard_switch.');self.title('Change Boot sound')
                dropdown(p,'show_theme_popup.boot_sound_dropdown.',0);self.title('Change Alert sound')
                self.click('show_theme_popup.alert_sound_switch.');self.title('Close theme');click_text(p,'Close')
                self.title('Reopen saved Theme preferences');self.click('/Theme');self.title('Restore Dashboard')
                self.assertFalse(bound(p,'show_theme_popup.dashboard_switch.')['state']&1)
                self.assertFalse(bound(p,'show_theme_popup.alert_sound_switch.')['state']&1)
                self.assertEqual(stored(p,'boot_sound'),'0')
                self.click('show_theme_popup.dashboard_switch.');self.title('Restore Boot sound')
                dropdown(p,'show_theme_popup.boot_sound_dropdown.',1);self.title('Restore Alert sound')
                self.click('show_theme_popup.alert_sound_switch.');self.title('Close theme');click_text(p,'Close')
                self.title('Check all restored preferences');self.click('/Theme');self.title('Close theme')
                self.assertTrue(bound(p,'show_theme_popup.dashboard_switch.')['state']&1)
                self.assertTrue(bound(p,'show_theme_popup.alert_sound_switch.')['state']&1)
                click_text(p,'Close');self.finish()
    def test_rotation_native_restart_resumes_once_all_rotations(self):
        p=self.page
        for r in range(4):
            with self.subTest(rotation=r):
                target=(r+1)%4;self.begin('rotation',r);self.click('/Screen Rotation');self.title('Choose a new orientation')
                dropdown(p,'show_screen_rotation_popup.dropdown.',target);self.title('Close rotation');click_text(p,'Close')
                self.title('Reopen Screen Rotation');self.click('/Screen Rotation');self.title('Restart into the new orientation')
                self.assertTrue(any('Restart to apply' in o['text'] for o in objects(p)))
                with p.expect_navigation():self.click('show_screen_rotation_popup.restart_btn.')
                self.ready();self.title('Check applied orientation');self.assertNotIn('emu-story-once',p.url)
                expected=[1280,720] if target%2 else [720,1280]
                self.assertEqual(p.evaluate('[emulator.module._emu_width(),emulator.module._emu_height()]'),expected)
                click_text(p,'INTERNAL');click_text(p,'Settings');self.click('/Screen Rotation');self.title('Close rotation')
                self.assertTrue(any(o['text']=='Currently active' for o in objects(p)));click_text(p,'Close');self.finish()
                p.reload();self.ready();self.assertTrue(p.locator('#queue2-story').is_visible());self.assertTrue(p.locator('#guide-panel').is_hidden())
    def test_close_without_change_restart_guide_and_wrong_tab(self):
        p=self.page;self.begin('timeout',0);self.click('/Screen Timeout');self.title('Change timeout');click_text(p,'Close')
        self.title('Change timeout');click_text(p,'GROVE');self.title('Change timeout')
        p.locator('#guide-restart').click();self.title('Open Screen Timeout');click_text(p,'INTERNAL');self.click('/Screen Timeout')
        self.title('Change timeout');p.locator('#guide-leave').click();self.assertTrue(p.locator('#queue2-story').is_visible());self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
