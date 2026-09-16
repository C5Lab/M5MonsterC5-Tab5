"""S27: shell lifecycle around native stories; synthetic device data only."""
import unittest
from emulator_browser import DIST, objects, click_text
import test_emulator_stories as story_helpers


class StoryLifecycleBrowser(unittest.TestCase):
    setUp = story_helpers.BluetoothStoryBrowser.setUp
    load = story_helpers.BluetoothStoryBrowser.load
    ready = story_helpers.BluetoothStoryBrowser.ready
    clean = story_helpers.BluetoothStoryBrowser.clean
    click = story_helpers.BluetoothStoryBrowser.click
    scan = story_helpers.BluetoothStoryBrowser.scan
    progress = story_helpers.BluetoothStoryBrowser.progress

    def test_restart_leave_fullscreen_and_rotation_all_orientations(self):
        p = self.page
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                self.load(rotation)
                p.evaluate("emulator.device.device.installPcapFixtures('grove')")
                files = p.evaluate("emulator.device.device.files('grove')")
                p.locator('#guide-start').click(); self.scan()
                self.click('02:20:77:02:00:01/locate')
                p.evaluate("""globalThis.s27Samples=[];
                  addEventListener('emulator-bluetooth',e=>{
                    if(e.detail.type==='sample')s27Samples.push(e.detail.rssi);
                  });""")
                p.locator('#guide-restart').click()
                p.evaluate('s27Samples.length=0')
                self.assertEqual(self.progress(), 'Step 1 of 4')
                self.assertEqual(p.evaluate("emulator.device.device.files('grove')"), files)
                p.wait_for_function('new Set(s27Samples).size>=2',timeout=6000)
                p.locator('#fullscreen').click()
                p.wait_for_function("document.fullscreenElement?.tagName==='MAIN'")
                # Playwright must be able to scroll to and click both controls.
                p.locator('#guide-restart').click(); p.locator('#guide-leave').click()
                p.evaluate('s27Samples.length=0')
                self.assertTrue(p.locator('#guide-panel').is_hidden())
                p.wait_for_function('new Set(s27Samples).size>=2',timeout=6000)
                p.locator('#guide-start').click()
                self.assertEqual(self.progress(), 'Step 1 of 4')
                p.locator('#fullscreen').click()
                p.wait_for_function('document.fullscreenElement===null')
                with p.expect_navigation():
                    p.locator('#rotation').select_option(str((rotation + 1) % 4))
                self.ready()
                self.assertTrue(p.locator('#guide-panel').is_hidden())
                self.assertEqual(p.evaluate("emulator.device.device.files('grove')"), [])
                self.clean()

    def test_cancel_busy_speed_and_reset_are_independent_of_guide(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                p = self.page; self.load(rotation); p.locator('#guide-start').click()
                p.locator('#timing').select_option('1')
                click_text(p, 'WiFi Scan\n& Attack')
                p.wait_for_function('emulator.module._emu_scan_state(0)===1')
                # Native input starts the job synchronously; shell controls are
                # refreshed by the next animation frame.
                p.wait_for_function("document.querySelector('#timing').disabled")
                self.assertTrue(p.locator('#timing').is_disabled())
                p.locator('#guide-restart').click()
                self.assertEqual(p.evaluate('emulator.module._emu_scan_state(0)'), 1)
                p.locator('#cancel').click()
                p.wait_for_function('emulator.device.snapshot(0).active===null')
                p.wait_for_function("!document.querySelector('#timing').disabled")
                self.assertTrue(p.locator('#cancel').is_disabled())
                p.locator('#timing').select_option('0')
                self.assertEqual(self.progress(), 'Step 1 of 4')
                p.evaluate("emulator.device.device.installPcapFixtures('grove')")
                with p.expect_navigation(): p.locator('#reset').click()
                self.ready()
                self.assertTrue(p.locator('#guide-panel').is_hidden())
                self.assertEqual(p.evaluate("emulator.device.device.files('grove')"), [])
                self.assertEqual(p.locator('#timing').input_value(), '0')
                self.clean()

    def test_each_module_condition_starts_a_fresh_volatile_session(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                p = self.page; self.load(rotation)
                for condition in ['missing-board', 'missing-sd', 'version-mismatch', 'normal']:
                    with self.subTest(condition=condition):
                        self.load(rotation)
                        p.evaluate("emulator.device.device.installPcapFixtures('grove')")
                        self.assertTrue(p.evaluate("emulator.device.device.files('grove')"))
                        p.locator('#guide-start').click()
                        p.locator('.demo-conditions summary').click()
                        with p.expect_navigation(): p.locator('#module-condition').select_option(condition)
                        self.ready()
                        self.assertTrue(p.locator('#guide-panel').is_hidden())
                        self.assertEqual(p.evaluate("emulator.device.device.files('grove')"), [])
                        self.assertEqual(p.locator('#module-condition').input_value(), condition)
                        self.clean()

    def test_download_retry_and_reset_recovery_keep_guide_disabled_until_ready(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                p = self.page; attempts = []; failing = [True]
                def download(route):
                    attempts.append(route.request.url)
                    if failing[0]: route.abort('connectionreset')
                    else: route.fulfill(path=str(DIST/'emulator.wasm'), content_type='application/wasm')
                p.route('**/emulator.wasm', download)
                p.goto(f'{self.url}/?rotation={rotation}')
                p.wait_for_function("document.querySelector('#status').textContent.includes('Could not load')")
                self.assertEqual(len(attempts), 3)
                self.assertTrue(p.locator('#guide-start').is_disabled())
                self.assertTrue(p.locator('#guide-panel').is_hidden())
                failing[0] = False
                with p.expect_navigation(): p.locator('#reset').click()
                self.ready()
                self.assertEqual(len(attempts), 4)
                self.assertTrue(p.locator('#guide-start').is_enabled())
                self.assertTrue(p.locator('#guide-panel').is_hidden())
                self.clean()

    def test_interrupted_download_recovers_on_third_attempt(self):
        for rotation in range(4):
            with self.subTest(rotation=rotation):
                p = self.page; attempts = []
                def download(route):
                    attempts.append(route.request.url)
                    if len(attempts) < 3: route.abort('connectionreset')
                    else: route.fulfill(path=str(DIST/'emulator.wasm'), content_type='application/wasm')
                p.route('**/emulator.wasm', download)
                p.goto(f'{self.url}/?rotation={rotation}'); self.ready()
                self.assertEqual(len(attempts), 3)
                p.locator('#guide-start').click()
                self.assertEqual(self.progress(), 'Step 1 of 4')
                self.clean()


if __name__ == '__main__': unittest.main(verbosity=2)
