"""Production slice checks; no compiler or browser required."""
import importlib.util,json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ui_emulator'))
import prepare_browser
from source_index import Source
class SliceTests(unittest.TestCase):
 def test_retained_functions_are_byte_exact(self):
  prepare_browser.generate();src=Source(ROOT/'main/main.c')
  manifest=json.loads((prepare_browser.BASE/'generated/manifest.json').read_text())
  generated=(prepare_browser.BASE/'generated/browser.c').read_text()
  for fn in manifest['retained']:
   self.assertIn(src.function(fn),generated,fn)
  self.assertIn('scan_btn_click_cb',manifest['retained'])
  self.assertIn('parse_csv_mixed_fields',manifest['retained'])
  self.assertIn('network_checkbox_event_cb',manifest['retained'])
  self.assertIn('screen_rotation_dropdown_cb',manifest['retained'])
 def test_unknown_decision_fails(self):
  original=prepare_browser.BASE
  with tempfile.TemporaryDirectory() as tmp:
   base=Path(tmp);(base/'runtime').mkdir()
   policy=json.loads((original/'slice-policy.json').read_text())
   del policy['functions']['show_scan_page']
   (base/'slice-policy.json').write_text(json.dumps(policy))
   (base/'control-contracts.json').write_text((original/'control-contracts.json').read_text())
   # Mirror the real inputs apart from the one removed decision: phase overlays
   # and every runtime boundary file, or the fixture fails for the wrong reason.
   for overlay in original.glob('slice-*-*.json'):
    (base/overlay.name).write_text(overlay.read_text())
   for source in (original/'runtime').glob('*.c'):
    (base/'runtime'/source.name).write_text(source.read_text())
   try:
    prepare_browser.BASE=base
    with self.assertRaisesRegex(ValueError,'Missing slice decision: show_scan_page'): prepare_browser.generate()
   finally:prepare_browser.BASE=original
 def test_boundaries_are_visible_or_implemented(self):
  manifest=json.loads((prepare_browser.BASE/'generated/manifest.json').read_text())
  app=''.join(p.read_text() for p in (prepare_browser.BASE/'runtime').glob('*.c'))
  generated=(prepare_browser.BASE/'generated/browser.c').read_text()
  for fn,mode in manifest['boundaries'].items():
   if mode=='unsupported':self.assertIn('emu_unsupported("'+fn+'")',generated)
   else:self.assertIn(fn+'(',app)
  self.assertNotIn('#define xTaskCreate', (prepare_browser.BASE/'runtime/host.h').read_text())
if __name__=='__main__':unittest.main()
