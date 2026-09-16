"""Check fixture/schema integrity and the source inventory's evidence links."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import jsonschema
ROOT=Path(__file__).resolve().parents[1]
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8'))
schema=read('tools/ui_emulator/scenario.schema.json')
seed=read('docs/ui-emulator/neon-district.seed.json')
validator=jsonschema.Draft202012Validator(schema,format_checker=jsonschema.FormatChecker())
def validate(s):
    validator.validate(s)
    for key in ['networks','clients','bluetooth','modules','subghz_signals','files','jobs','variants']:
        ids=[x['id'] for x in s[key]];assert len(ids)==len(set(ids)),key
    nets={n['id']:n for n in s['networks']}
    assert all(c['network_id'] in nets for c in s['clients'])
    assert all(n in nets for n in s['probe_network_ids'])
    assert all(f['network_id'] is None or f['network_id'] in nets for f in s['files'])
    modules={m['id'] for m in s['modules']};files={f['id'] for f in s['files']}
    assert all(j['module_id'] in modules and set(j['result_file_ids'])<=files for j in s['jobs'])
    assert all(len(n['ssid'].encode('utf-8'))<=32 for n in s['networks'])
    assert all(n['band']==('2.4GHz' if n['channel']<=14 else '5GHz') for n in s['networks'])
    for kind,field in [('networks','bssid'),('clients','mac'),('bluetooth','mac')]:
        addresses=[x[field] for x in s[kind]];assert len(addresses)==len(set(addresses))
    assert len({f['path'] for f in s['files']})==len(s['files'])
    assert not s['subghz_signals'], 'SubGHz is excluded from the current scope'
    assert all(m['producer']!='subghz-separate' and 'subghz' not in m['capabilities'] for m in s['modules'])
    assert all(v['domain']!='subghz' for v in s['variants'])
    assert all(f['kind']!='subghz' for f in s['files'])
    assert s['lan']['network_id'] in nets and s['lan']['module_id'] in modules
validate(seed)
bad=copy.deepcopy(seed);bad['clients'][0]['network_id']='missing'
try:validate(bad)
except AssertionError:pass
else:raise AssertionError('dangling references accepted')
bad=copy.deepcopy(seed);bad['settings']['rotation']=45
try:validate(bad)
except jsonschema.ValidationError:pass
else:raise AssertionError('invalid rotation accepted')
x=read('tools/ui_emulator/coverage.json')
assert not x['syntax_diagnostics']
for path,digest in x['sources'].items():assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==digest,path
ids=[c['id'] for c in x['controls']];assert len(ids)==len(set(ids))
for c in x['controls']:
    assert c['callback_candidates'],c['id']
    assert all(fid in x['functions'] for fid in c['callback_candidates']+c['reachable_functions'])
for route in x['routes']:
    text=(ROOT/route['file']).read_text(encoding='utf-8')
    assert route['anchor'] in text,route['id']
print('PASS: schema, references, invalid-data rejection, source hashes, callback candidates and route anchors')
