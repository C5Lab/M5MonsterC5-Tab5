"""Run Phase 1 checks from fresh source and record an evidence-backed gate."""
from emulator_test_runner import run_logged
from pathlib import Path
import hashlib
import json
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
report_path=ROOT/'docs/ui-emulator/phase1-verification.json'
report={'status':'running','checks':[],'scope':'Phase 1 specifications and native parser tests; no live browser emulator'}
report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8'))
def run(path):
    result=run_logged([sys.executable,str(ROOT/path)],cwd=ROOT,capture_output=True,text=True)
    report['checks'].append({'command':path,'exit_code':result.returncode,'output':result.stdout+result.stderr})
    print(path+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
try:
    for p in ['tools/ui_emulator/inventory.py','tools/ui_emulator/control_contracts.py',
              'tools/ui_emulator/dependency_registry.py','tools/ui_emulator/scenario_schema.py',
              'tests/test_emulator_contracts.py','tests/validate_emulator_inventory.py',
              'tests/run_emulator_parser_tests.py']:
        run(p)
    coverage=read('tools/ui_emulator/coverage.json');controls=read('tools/ui_emulator/control-contracts.json')
    registry=read('tools/ui_emulator/dependency-registry.json');seed=read('docs/ui-emulator/neon-district.seed.json')
    assert controls['sources']==coverage['sources']==registry['sources']
    assert len(controls['controls'])==len(coverage['controls'])
    ids=[c['template_id'] for c in controls['controls']];assert len(ids)==len(set(ids))
    for c in controls['controls']:
        contract=c['contract']
        assert contract['preconditions'] and contract['handlers'] and contract['expected_outcome'] and contract['state_owner']
        assert contract['allow_noop_substitution'] is False
        assert all(h['function'] in coverage['functions'] for h in contract['handlers'])
    assert all(r['status']=='specified-for-implementation' and not r['allow_default_noop'] for r in registry['dependencies'])
    assert seed['experience']['excluded_features']==['subghz']
    assert seed['experience']['default_timing']=='demo' and seed['experience']['realistic_clock_scale']==1
    assert not any(m['producer']=='subghz-separate' for m in seed['modules'])
    report.update(status='passed',registration_sites=len(ids),current_contracts=sum(c['contract']['scope']=='current' for c in controls['controls']),deferred_contracts=sum(c['contract']['scope']!='current' for c in controls['controls']),external_symbols=len(registry['dependencies']),source_hashes=coverage['sources'])
    report['artifact_sha256']={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['tools/ui_emulator/control-identities.json','tools/ui_emulator/control-contracts.json','tools/ui_emulator/adapter-decisions.json','tools/ui_emulator/dependency-registry.json','docs/ui-emulator/neon-district.seed.json']}
except Exception as e:
    report.update(status='failed',error=str(e));raise
finally:
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('PASS: Phase 1 specification gate. Browser implementation is not included.')
