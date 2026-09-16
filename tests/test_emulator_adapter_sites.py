"""Copied native forms must bind permanent identities, never old source offsets."""
import json
import re
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ui_emulator'))
from source_index import Source,walk

# Reviewed adapter equivalents: asynchronous completion builds the original
# host list, and Radar's renderer builds the original show_ap_radar_page.
OWNERS={'app_arp_hosts_done':'arp_list_hosts_cb','app_nmap_hosts_done':'nmap_list_hosts_cb','app_radar_render':'show_ap_radar_page'}
def normalized(value):
    # Whitespace inside quoted user data is part of a permanent identity.
    return re.sub(r'"(?:\\.|[^"\\])*"|\s+',lambda m:m[0] if m[0].startswith('"') else '',value.replace('ctx->',''))
def sites():
    for path in (ROOT/'tools/ui_emulator/runtime').glob('*.c'):
        source=Source(path,mask_embedded_js=True)
        for owner,fn in source.functions.items():
            for node in walk(fn):
                if node.type!='call_expression' or source.text(node.child_by_field_name('function'))!='app_bind':continue
                args=node.child_by_field_name('arguments').named_children
                if len(args)!=5:continue
                values=[source.text(a) for a in args]
                branches=[];parent=node.parent
                while parent and parent!=fn:
                    if parent.type=='if_statement':
                        body=parent.child_by_field_name('consequence')
                        branches.append([source.text(parent.child_by_field_name('condition')),'then' if body.start_byte<=node.start_byte<body.end_byte else 'else'])
                    parent=parent.parent
                obj='ap_radar_panel' if owner=='app_radar_render' and values[0]=='r->panel' else values[0]
                identity=['main/main.c::'+OWNERS.get(owner,owner),obj,*values[1:4],list(reversed(branches))]
                yield path,source,node,args,identity,values[-1]
def identity_normalized(identity):
    return [*[normalized(v) for v in identity[:5]],[[normalized(c),b] for c,b in identity[5]]]
def matching_ids(identity,registry):
    return [value for key,value in registry.items() if identity_normalized(json.loads(key))==identity_normalized(identity)]

class AdapterSites(unittest.TestCase):
    def test_no_copied_form_uses_a_numeric_source_line(self):
        all_sites=list(sites());self.assertGreaterEqual(len(all_sites),49)
        for path,_,node,_,_,last in all_sites:
            with self.subTest(file=path.name,line=node.start_point.row+1):self.assertRegex(last,r'^app_template_line\("ui\.[^"]+"\)$')
    def test_each_permanent_id_matches_constructor_callback_event_data_and_branch(self):
        registry=json.loads((ROOT/'tools/ui_emulator/control-identities.json').read_text())['identities']
        contracts={c['template_id']:c for c in json.loads((ROOT/'tools/ui_emulator/control-contracts.json').read_text())['controls']}
        for path,_,node,_,identity,last in sites():
            with self.subTest(file=path.name,line=node.start_point.row+1):
                candidates=matching_ids(identity,registry);self.assertEqual(len(candidates),1,identity)
                self.assertEqual(last,'app_template_line('+json.dumps(candidates[0])+')')
                contract=contracts[candidates[0]]
                self.assertEqual(contract['source_owner'],identity[0])
                self.assertEqual(contract['contract']['callback_expression'],identity[2])
                self.assertEqual(contract['contract']['event'],identity[3])

if __name__=='__main__':unittest.main(verbosity=2)
