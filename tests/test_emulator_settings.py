"""Behavior checks for portable Settings adapters beyond persistence alone."""
import unittest
from emulator_browser import browser_session, objects, click_text, click_object
from test_emulator_phase2 import bound, position, dropdown, stored


class SettingsTests(unittest.TestCase):
    def setUp(self):
        self.session=browser_session()
        self.browser,self.url=self.session.__enter__()
        self.context=self.browser.new_context(viewport={'width':1440,'height':1000})
        self.page=self.context.new_page()
        self.page.add_init_script('''globalThis.unavailable=[];
            addEventListener('emulator-unavailable', e=>unavailable.push(e.detail));''')
        self.page.goto(self.url)
        self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(self.page,'INTERNAL');click_text(self.page,'Settings')

    def tearDown(self):
        unavailable=self.page.evaluate('unavailable')
        self.context.close();self.session.__exit__(None,None,None)
        self.assertEqual(unavailable,[])

    def test_brightness_has_visible_effect(self):
        p=self.page
        click_text(p,'Screen\nBrightness')
        slider=bound(p,'show_screen_brightness_popup.screen_brightness_slider.value_changed')
        p.mouse.click(*position(p,slider,.3))
        value=int(stored(p,'scr_bright'))
        self.assertLess(value,50)
        p.wait_for_function('(v)=>document.querySelector("#display").style.filter===`brightness(${v/100})`',arg=value,timeout=2000)

    def test_low_brightness_survives_restart(self):
        p=self.page
        # A legitimate persisted value from the production slider's 1..100 range.
        p.evaluate('emulatorSettings.set("scr_bright",1)')
        p.reload();p.wait_for_function('globalThis.emulator?.measurements.frames>2')
        click_text(p,'INTERNAL');click_text(p,'Settings');click_text(p,'Screen\nBrightness')
        self.assertTrue(any(o['text']=='1%' for o in objects(p)))

    def test_saved_rotation_applies_when_opening_base_preview(self):
        p=self.page
        p.evaluate('emulatorSettings.set("scr_rot",3)')
        p.goto(self.url);p.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.assertEqual(p.evaluate('[emulator.module._emu_width(),emulator.module._emu_height()]'),[1280,720])
        self.assertEqual(p.locator('#rotation').input_value(),'3')
        p.goto(self.url+'/?rotation=0');p.wait_for_function('globalThis.emulator?.measurements.frames>2')
        self.assertEqual(p.evaluate('[emulator.module._emu_width(),emulator.module._emu_height()]'),[720,1280])

    def test_theme_rebuild_and_red_team_back_routes(self):
        p=self.page
        click_text(p,'Theme')
        click_object(p,bound(p,'show_theme_popup.dashboard_switch.'))
        self.assertEqual(stored(p,'dashboard'),'0')
        click_object(p,bound(p,'show_theme_popup.dark_switch.'))
        self.assertEqual(stored(p,'dark_mode'),'0')
        click_text(p,'Theme')
        self.assertFalse(bound(p,'show_theme_popup.dark_switch.')['state'] & 1)
        click_text(p,'Close')
        click_text(p,'Red\nTeam')
        click_object(p,bound(p,'show_red_team_settings_page.red_team_switch.'))
        click_object(p,bound(p,'show_red_team_settings_page.back_btn.'))
        click_text(p,'GROVE')
        click_text(p,'WiFi Scan\n& Test')
        p.wait_for_function('emulator.module._emu_scan_state(0)===2')
        self.assertEqual(p.evaluate('emulator.module._emu_network_count(0)'),12)

    def test_numeric_cancel_invalid_and_range(self):
        p=self.page
        click_text(p,'Scan\nSetup')
        def focus_min():
            obs=objects(p);label=next(o for o in obs if o['text']=='0100')
            click_object(p,next(o for o in obs if o['id']==label['parent']))
        focus_min()
        p.keyboard.type('250');p.keyboard.press('Escape');p.keyboard.type('abc')
        click_text(p,'Save');click_text(p,'Scan\nSetup')
        self.assertEqual(len([o for o in objects(p) if o['text']=='0100']),2)
        focus_min()
        p.keyboard.type('9999');p.keyboard.press('Enter')
        self.assertTrue(any(o['text']=='1500' for o in objects(p)))
        click_text(p,'Save')
        self.assertTrue(any('min must be < max' in o['text'] for o in objects(p)))


if __name__=='__main__':unittest.main()
