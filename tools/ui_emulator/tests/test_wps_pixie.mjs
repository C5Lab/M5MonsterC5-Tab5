import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../model/device.mjs';

const seed=JSON.parse(await readFile(new URL('../../../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const success=seed.networks[0].bssid;

function records(device,module='grove') {
  const out=[];
  for(let line;(line=device.wpsRead(module))!==null;)out.push(line);
  return out;
}

test('Pixie demo accepts one discovered AP and reveals its fixture only after verified terminal',()=>{
  const device=new SimulatedDevice(seed);
  const scan=device.scan('grove');device.advance(5000);
  assert.equal(device.job(scan).state,'completed');
  device.wpsCommand('grove',`wps pixie ${success}`);
  const accepted=records(device).map(JSON.parse);
  assert.deepEqual(accepted,[{schema_version:1,record_type:'accepted',run_id:2,operation:'wps_pixie'}]);
  assert.equal(device.snapshot('grove').active,2);
  device.advance(5000);
  const progress=records(device).map(JSON.parse);
  assert.deepEqual(progress.filter(r=>r.type==='wps_stage').map(r=>r.stage),[
    'pixie_capture_start','pixie_capture','pixie_done','pixie_verify_attempt','pixie_verify']);
  assert.equal(progress.find(r=>r.stage==='pixie_done').found,true);
  assert.equal(progress.at(-1).operation_result,'pixie_pin_and_m7_verified');
  assert.equal(progress.at(-1).cleanup_result,'restored');
  assert.equal(progress.at(-1).lease_state,'released');
  assert.equal(JSON.stringify(progress).includes('example-key'),false);
  assert.equal(device.snapshot('grove').active,null);
  device.wpsCommand('grove','secret reveal');
  assert.match(records(device)[0],/^SENSITIVE operation_id=2 recovered_pin=12345670 network_key="example-key"$/);
  device.wpsCommand('grove','secret reveal');
  assert.equal(JSON.parse(records(device)[0]).result,'empty');
});

test('a repeated stop does not extend cleanup or permit another start until release',()=>{
  const device=new SimulatedDevice(seed);
  device.scan('grove');device.advance(5000);
  device.wpsCommand('grove',`wps pixie ${success}`);records(device);
  device.wpsCommand('grove','wps stop');
  const stop=JSON.parse(records(device)[0]);
  assert.equal(stop.record_type,'wps_stop');
  assert.equal(stop.result,'stop_pending');
  device.advance(250);
  device.wpsCommand('grove','wps stop');records(device);
  device.wpsCommand('grove','wps status');
  const status=JSON.parse(records(device)[0]);
  assert.equal(status.record_type,'wps_status');
  assert.equal(status.phase,'cleanup');
  assert.throws(()=>device.wpsCommand('grove',`wps pixie ${success}`),/busy/);
  device.advance(50);
  const final=records(device).map(JSON.parse);
  assert.equal(final.at(-1).operation_result,'cancelled');
  assert.equal(final.at(-1).cleanup_result,'restored');
  assert.equal(final.some(record=>record.type==='wps_stage'),false);
  assert.equal(device.snapshot('grove').active,null);
  assert.equal(device.wpsRead('mbus'),null);
});

test('a new Pixie run hides an earlier fixture and no-match never exposes one',()=>{
  const device=new SimulatedDevice(seed);
  assert.throws(()=>device.wpsCommand('grove',`wps pixie ${success}`),/discovered/);
  device.scan('grove');device.advance(5000);
  device.wpsCommand('grove',`wps pixie ${success}`);records(device);
  device.advance(3000);records(device);
  device.wpsCommand('grove',`wps pixie ${seed.networks[1].bssid}`);records(device);
  device.wpsCommand('grove','secret reveal');
  assert.equal(JSON.parse(records(device)[0]).result,'empty');
  device.advance(3000);
  const report=records(device).map(JSON.parse);
  assert.equal(report.find(r=>r.stage==='pixie_done').found,false);
  assert.equal(report.at(-1).operation_result,'no_match');
  device.wpsCommand('grove','secret reveal');
  assert.equal(JSON.parse(records(device)[0]).result,'empty');
});

test('disconnect during a Pixie run reports unconfirmed cleanup without revealing a secret',()=>{
  const device=new SimulatedDevice(seed);
  device.scan('grove');device.advance(5000);
  device.wpsCommand('grove',`wps pixie ${success}`);records(device);
  device.setModule('grove',{connected:false});
  const terminal=records(device).map(JSON.parse).at(-1);
  assert.equal(terminal.record_type,'terminal');
  assert.equal(terminal.cleanup_result,'unknown');
  assert.equal(terminal.lease_state,'quarantined');
  device.wpsCommand('grove','secret reveal');
  assert.equal(JSON.parse(records(device)[0]).result,'empty');
  device.wpsCommand('grove','wps status');
  assert.equal(JSON.parse(records(device)[0]).phase,'quarantined');
  device.setModule('grove',{connected:true});
  assert.throws(()=>device.wpsCommand('grove',`wps pixie ${success}`),/quarantined/);
});

test('verified fixture expires before one-time secret reveal',()=>{
  const device=new SimulatedDevice(seed);
  device.scan('grove');device.advance(5000);
  device.wpsCommand('grove',`wps pixie ${success}`);records(device);
  device.advance(3000);records(device);
  device.advance(300001);
  device.wpsCommand('grove','secret reveal');
  const expired=JSON.parse(records(device)[0]);
  assert.equal(expired.record_type,'secret_reveal');
  assert.equal(expired.result,'expired');
});

test('Pixie radio ownership refuses fresh scan and Observer without stale scan rows',()=>{
  const device=new SimulatedDevice(seed);
  device.scan('grove');device.advance(5000);
  assert.ok(device.snapshot('grove').networks.length>0);
  device.wpsCommand('grove',`wps pixie ${success}`);records(device);
  assert.throws(()=>device.scan('grove'),/busy/);
  assert.deepEqual(device.snapshot('grove').networks,[]);
  assert.throws(()=>device.observerStart('grove',['net-1']),/busy/);
  assert.equal(device.observer('grove').running,false);
  assert.equal(device.job(2).state,'running');
});
