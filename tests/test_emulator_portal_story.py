"""S08 guided client & password demo driven through the native canvas."""
import unittest
from emulator_browser import click_text
import test_emulator_portal_demo as portal_helpers
import test_emulator_attacks_rogue_evil_mitm as native

class PortalStoryBrowser(unittest.TestCase):
    setUp=portal_helpers.PortalDemoBrowser.setUp
    load=portal_helpers.PortalDemoBrowser.load
    ready=portal_helpers.PortalDemoBrowser.ready
    advance=portal_helpers.PortalDemoBrowser.advance
    clean=portal_helpers.PortalDemoBrowser.clean
    scan=portal_helpers.PortalDemoBrowser.scan
    click=native.NativeAttackBrowser.click

    def begin(self,story_id,rotation=0):
        self.load(rotation)
        self.page.locator('#queue2-story').select_option(story_id)
        self.page.locator('#guide-queue2').click()
        self.page.evaluate("async id=>{const {portalStories}=await import('./portal-story.mjs');globalThis.psStory=portalStories.find(e=>e.id===id).create().story;}",story_id)

    def stage(self,n):
        self.page.wait_for_function('(n)=>document.querySelector("#guide-title").textContent===(n===psStory.steps.length?psStory.completionTitle:psStory.steps[n].title)',arg=n,timeout=8000)

    def done(self):
        self.page.wait_for_function('document.querySelector("#guide-title").textContent===psStory.completionTitle',timeout=8000)
        self.assertIn('complete',self.page.locator('#guide-progress').inner_text());self.clean()

    def sequence(self):
        self.page.evaluate('emulator.module._emu_set_timing(1)')
        self.advance(21000);self.stage(3)
        self.advance(30000);self.stage(4)
        self.advance(30000);self.stage(5)

    def run_story(self,story_id,rotation):
        self.begin(story_id,rotation)
        if story_id=='internal-portal':
            self.page.evaluate('emulator.module._emu_set_timing(1)')
            click_text(self.page,'INTERNAL');click_text(self.page,'Ad Hoc\nPortal & Karma');self.stage(1)
            click_text(self.page,' Show Probes');click_text(self.page,'1. NEON-BAZAAR');click_text(self.page,' Start');self.stage(2)
            self.sequence();click_text(self.page,'STOP PORTAL',contains=True)
        else:
            label,start,stop=('Evil Twin','START ATTACK','STOP') if story_id=='evil-twin' else ('RogueAP','Start Rogue AP','Stop Rogue AP')
            self.scan();self.stage(1)
            self.click(label);self.click(start);self.stage(2)
            self.sequence();self.click(stop)
        self.done()

    def test_all_three_demos_across_rotations(self):
        for rotation in range(4):
            for story_id in ('evil-twin','rogue-ap','internal-portal'):
                with self.subTest(rotation=rotation,story=story_id):
                    self.run_story(story_id,rotation)

    def test_stories_are_independent_of_the_previous_session(self):
        # Complete Evil Twin, return to the list, then run Rogue AP from scratch.
        self.run_story('evil-twin',0)
        self.page.locator('#guide-leave').click()
        self.run_story('rogue-ap',0)

    def test_cancel_before_submission_does_not_complete(self):
        self.begin('evil-twin')
        self.scan();self.stage(1);self.click('Evil Twin');self.click('START ATTACK');self.stage(2)
        self.page.evaluate('emulator.module._emu_set_timing(1)');self.advance(21000);self.stage(3)
        self.page.locator('#cancel').click();self.advance(90000)
        self.assertNotEqual(self.page.locator('#guide-title').inner_text(),self.page.evaluate('psStory.completionTitle'))
        self.clean()

if __name__=='__main__':unittest.main(verbosity=2)
