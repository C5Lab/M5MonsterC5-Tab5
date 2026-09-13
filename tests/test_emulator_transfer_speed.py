"""Native transfer baud choice, persistence, reset and storage fallback."""
import unittest
from emulator_browser import objects,click_text,click_object
from test_emulator_phase2 import position
import test_emulator_portal_demo as helpers
class TransferSpeedBrowser(unittest.TestCase):
    setUp=helpers.PortalDemoBrowser.setUp
    load=helpers.PortalDemoBrowser.load
    ready=helpers.PortalDemoBrowser.ready
    clean=helpers.PortalDemoBrowser.clean
    def open(self):
        p=self.page;click_text(p,'INTERNAL');click_text(p,'Settings')
        for _ in range(8):
            obj=next(o for o in objects(p) if o['text']=='Transfer\nSpeed')
            height=p.locator('#display').evaluate('(c)=>c.height')
            if 0<=obj['y']+obj['height']/2<height:click_object(p,obj);return
            canvas=p.locator('#display');box=canvas.bounding_box()
            p.mouse.move(box['x']+box['width']*.75,box['y']+box['height']*.65);p.mouse.wheel(0,450);p.wait_for_timeout(150)
        self.fail('Transfer Speed tile unreachable')
    def choose(self,index):
        p=self.page;obj=next(o for o in objects(p) if 'show_ft_baud_popup.dropdown' in o['binding']);click_object(p,obj)
        for _ in range(6):
            rows=objects(p);label=next(o for o in rows if o['text'].startswith('115200\n230400'))
            parent=next(o for o in rows if o['id']==label['parent'])
            y=label['y']+label['height']*(index+.5)/9
            if parent['y']+3<y<parent['y']+parent['height']-3:
                p.mouse.click(*position(p,label,.5,(index+.5)/9));return
            p.mouse.move(*position(p,parent));p.mouse.wheel(0,-200 if y<parent['y'] else 200);p.wait_for_timeout(150)
        self.fail('Baud option unreachable')
    def test_selection_reload_and_reset_all_rotations(self):
        p=self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation);self.open();self.choose(3)
                self.assertEqual(p.evaluate('emulatorSettings.get("ft_baud",null)'),921600)
                click_text(p,'Close');p.reload();self.ready();self.open();self.choose(0)
                self.assertEqual(p.evaluate('emulatorSettings.get("ft_baud",null)'),115200)
                with p.expect_navigation():p.locator('#reset').click()
                self.ready();self.assertEqual(p.evaluate('emulatorSettings.get("ft_baud",460800)'),460800);self.clean()
    def test_denied_disk_uses_session_storage(self):
        self.page.add_init_script("Object.defineProperty(window,'localStorage',{get(){throw Error('denied')}})")
        self.load();self.open();self.choose(3);click_text(self.page,'Close')
        self.page.reload();self.ready()
        self.assertEqual(self.page.evaluate('emulatorSettings.get("ft_baud",null)'),921600);self.clean()
if __name__=='__main__':unittest.main(verbosity=2)
