import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('nmap returns deterministic host and service data without storing credentials',()=>{
  const d=new SimulatedDevice(seed);
  const id=d.toolStart('grove','nmap',{action:'connect',ssid:seed.networks[0].ssid,password:'private-demo'});
  assert.equal(d.job(id).result,null);
  assert.ok(!JSON.stringify(d.job(id)).includes('private-demo'));
  d.advance(3000);assert.equal(d.job(id).state,'completed');
  assert.match(d.job(id).result.text,/SUCCESS/);
  const hosts=d.toolStart('grove','nmap',{action:'hosts'});d.advance(3000);
  assert.match(d.job(hosts).result.text,/192\.0\.2\.10/);
  const scan=d.toolStart('grove','nmap',{action:'scan',target:'192.0.2.10',level:'fast'});d.advance(3000);
  assert.match(d.job(scan).result.text,/80\/tcp open HTTP/);
  for(const [level,count] of [['quick',20],['medium',50],['heavy',100]]) {
    const id=d.toolStart('grove','nmap',{action:'scan',target:'all',level});d.advance(3000);
    assert.ok(d.job(id).result.text.includes(`(${count} ports)`));
    assert.match(d.job(id).result.text,/Scanned 2 hosts, found 2 open ports/);
  }
  assert.throws(()=>d.toolStart('grove','nmap',{action:'scan',target:'8.8.8.8',level:'fast'}));
});

test('tool busy, cancellation, disconnect and reset cannot publish stale results',()=>{
  const d=new SimulatedDevice(seed),id=d.toolStart('grove','iot');
  assert.throws(()=>d.scan('grove'),/busy/);
  const other=d.toolStart('mbus','iot');d.cancel(id);d.advance(3000);
  assert.equal(d.job(id).result,null);assert.equal(d.job(other).state,'running');
  const next=d.toolStart('grove','iot');d.setModule('grove',{connected:false});d.advance(3000);
  assert.equal(d.job(next).state,'cancelled');assert.equal(d.job(next).result,null);
  assert.throws(()=>d.toolStart('grove','iot'),/disconnected/);
  d.reset();assert.equal(d.job(other),null);assert.ok(d.toolStart('grove','iot')>next);
});

test('IoT result is detached and agrees with PAN/node inventory',()=>{
  const d=new SimulatedDevice(seed),id=d.toolStart('grove','iot');d.advance(30000);
  const result=d.job(id).result;assert.match(result.text,/pans=1/);
  assert.equal(result.text.split('\n').filter(l=>l.includes('[ZIG] node ')).length,2);
  result.text='mutated';assert.notEqual(d.job(id).result.text,'mutated');
});

test('Mesh monitors until stopped, discovers gradually and freezes on cancellation',()=>{
  const d=new SimulatedDevice(seed),id=d.toolStart('grove','iot');
  d.advance(1000);assert.equal(d.job(id).result,null);
  d.advance(19000);assert.equal(d.job(id).state,'running');
  assert.match(d.job(id).result.text,/active=1.*pans=1/);
  const first=d.job(id).result.text;
  d.advance(40000);assert.match(d.job(id).result.text,/pans=2/);
  assert.notEqual(d.job(id).result.text,first);
  assert.throws(()=>d.scan('grove'),/busy/);
  d.cancel(id);const frozen=d.job(id).result;d.advance(100000);
  assert.deepEqual(d.job(id).result,frozen);
  const next=d.toolStart('grove','iot');assert.equal(d.job(next).result,null);
  d.advance(60000);d.setModule('grove',{connected:false});
  const lost=d.job(next).result;d.advance(100000);
  assert.equal(d.job(next).state,'cancelled');assert.deepEqual(d.job(next).result,lost);
});

test('GITM stop saves valid capture once; cancellation and SD failures save nothing',()=>{
  const d=new SimulatedDevice(seed),id=d.toolStart('grove','gitm',{upstreamSsid:seed.networks[0].ssid,prefix:'demo'});
  d.advance(4000);assert.equal(d.job(id).state,'running');assert.ok(d.job(id).result.packets>0);
  assert.equal(d.toolStop(id),true);const job=d.job(id);
  assert.equal(job.state,'completed');assert.equal(d.toolStop(id),false);
  const bytes=d.readFile('grove',job.file);assert.equal(new DataView(bytes.buffer).getUint32(0,true),0xa1b2c3d4);
  assert.equal(bytes.length,job.result.bytes);assert.equal(d.files('grove').length,1);assert.equal(d.files('mbus').length,0);
  const cancelled=d.toolStart('mbus','gitm',{upstreamSsid:seed.networks[0].ssid});d.advance(3000);d.cancel(cancelled);d.advance(3000);
  assert.equal(d.files('mbus').length,0);
  const small=new SimulatedDevice(seed,{sdCapacityBytes:200}),full=small.toolStart('grove','gitm',{upstreamSsid:seed.networks[0].ssid});
  small.advance(3000);small.toolStop(full);assert.equal(small.job(full).error,'storage_full');assert.equal(small.files('grove').length,0);
  const lost=d.toolStart('mbus','gitm',{upstreamSsid:seed.networks[0].ssid});d.advance(3000);d.setModule('mbus',{sdPresent:false});d.toolStop(lost);
  assert.equal(d.job(lost).error,'sd_missing');assert.equal(d.snapshot('mbus').active,null);
});

test('GITM deterministic timing and input validation do not strand radio',()=>{
  const a=new SimulatedDevice(seed),b=new SimulatedDevice(seed);
  assert.throws(()=>a.toolStart('grove','gitm',{prefix:'../bad'}));
  assert.throws(()=>a.toolStart('grove','unexpected'));
  assert.equal(a.snapshot('grove').active,null);
  const options={upstreamSsid:seed.networks[0].ssid},x=a.toolStart('grove','gitm',options),y=b.toolStart('grove','gitm',options);
  a.advance(3000);for(let i=0;i<30;i++)b.advance(100);
  assert.deepEqual(a.job(x).result,b.job(y).result);
  a.cancel(x);const valid=a.toolStart('grove','gitm',{...options,prefix:'-demo'});a.toolStop(valid);
  assert.equal(a.job(valid).state,'completed');
});

test('wpa-sec simulated outcomes expose no credentials and require SD',()=>{
  const d=new SimulatedDevice(seed);
  for(const outcome of ['success','partial','failure']){
    const id=d.toolStart('grove','wpasec',{outcome,password:'secret'});d.advance(3000);
    const job=d.job(id);assert.ok(!JSON.stringify(job).includes('secret'));
    assert.equal(job.result.uploaded+job.result.failed+job.result.skipped,2);
    assert.equal(job.state,outcome==='failure'?'failed':'completed');
  }
  d.setModule('grove',{sdPresent:false});assert.throws(()=>d.toolStart('grove','wpasec'),/SD/);
});
