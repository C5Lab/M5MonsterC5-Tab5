"""Embedded JS must not hide later native controls from the inventory."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ui_emulator'))
from source_index import Source,ROOT

class RuntimeInventory(unittest.TestCase):
    def test_observer_capture_controls_remain_indexed(self):
        source=Source(ROOT/'tools/ui_emulator/runtime/observer.c',mask_embedded_js=True)
        body=source.function('app_capture_show')
        for name in ['generate','close','analyze']:
            self.assertIn('emu.phase3.capture.'+name,body)
        self.assertIn('app_observer_capture_cb',source.functions)
    def test_ota_controls_and_original_source_locations(self):
        source=Source(ROOT/'tools/ui_emulator/runtime/settings_ota.c',mask_embedded_js=True)
        for owner in ['ota_open_monitor','ota_scan_btn_cb','app_settings_ota_tick']:
            self.assertIn('app_bind_adapter',source.function(owner))
            node=source.functions[owner]
            self.assertEqual(source.data[:node.start_byte].count(b'\n'),node.start_point.row)

if __name__=='__main__':unittest.main()
