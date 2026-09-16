import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('system status is detached and module-local',()=>{
  const d=new SimulatedDevice(seed),status=d.systemStatus('grove');
  assert.equal(status.version,'demo-main-1');assert.equal(status.uptimeMs,0);
  assert.equal(status.sdUsedBytes,0);assert.equal(status.sdCapacityBytes,8*1024*1024);
  status.version='changed';assert.equal(d.systemStatus('grove').version,'demo-main-1');
  const id=d.systemStart('grove','update',{channel:'dev'});
  assert.equal(d.systemStatus('grove').active,id);
  assert.throws(()=>d.scan('grove'),/busy/);
  d.advance(3000);
  assert.equal(d.systemStatus('grove').version,'demo-dev-2');
  assert.equal(d.systemStatus('grove').reboots,1);
  assert.equal(d.systemStatus('mbus').version,'demo-main-1');
  assert.equal(d.systemStatus('mbus').reboots,0);
});

test('system progress and completion are invariant under timestep sizes',()=>{
  for(const operation of ['update','reboot']) {
    const a=new SimulatedDevice(seed),b=new SimulatedDevice(seed);
    const x=a.systemStart('grove',operation),y=b.systemStart('grove',operation);
    a.advance(4200);for(let i=0;i<42;i++)b.advance(100);
    assert.deepEqual(a.job(x),b.job(y));
    assert.deepEqual(a.systemStatus('grove'),b.systemStatus('grove'));
    assert.equal(a.systemStatus('grove').uptimeMs,operation==='update'?1200:3200);
  }
  const d=new SimulatedDevice(seed),id=d.systemStart('grove','update');
  assert.equal(d.job(id).result.stage,'preparing');d.advance(1000);
  assert.equal(d.job(id).result.stage,'downloading');d.advance(1000);
  assert.equal(d.job(id).result.stage,'installing');d.advance(1000);
  assert.equal(d.job(id).result.stage,'completed');
});

test('failed update leaves version and boot time unchanged',()=>{
  const d=new SimulatedDevice(seed),id=d.systemStart('grove','update',{outcome:'failure'});
  d.advance(4000);assert.equal(d.job(id).state,'failed');
  assert.equal(d.systemStatus('grove').version,'demo-main-1');
  assert.equal(d.systemStatus('grove').reboots,0);assert.equal(d.systemStatus('grove').uptimeMs,4000);
  assert.equal(d.systemStatus('grove').active,null);
});

test('cancel disconnect and reset invalidate pending completions',()=>{
  for(const operation of ['update','reboot'])for(const interruption of ['cancel','disconnect','reset']) {
    const d=new SimulatedDevice(seed),id=d.systemStart('grove',operation);
    d.advance(500);
    if(interruption==='cancel')assert.equal(d.cancel(id),true);
    if(interruption==='disconnect')d.setModule('grove',{connected:false});
    if(interruption==='reset')d.reset();
    d.advance(5000);
    assert.equal(d.systemStatus('grove').version,'demo-main-1');
    assert.equal(d.systemStatus('grove').reboots,0);
    assert.equal(d.systemStatus('grove').active,null);
    assert.equal(d.job(id)?.state??null,interruption==='reset'?null:'cancelled');
    if(interruption==='disconnect')assert.throws(()=>d.systemStart('grove',operation),/disconnected/);
  }
});

test('reboot preserves SD bytes and clears discovery and observer state',()=>{
  const d=new SimulatedDevice(seed);
  d.commitFileChanges('grove',[{path:'/sdcard/demo.txt',bytes:new Uint8Array([1,2,3])}]);
  d.scan('grove');d.advance(seed.settings.scan_time_ms);d.select('grove','net-1');
  d.observerStart('grove');d.advance(1000);
  const files=d.files('grove');d.systemStart('grove','reboot');d.advance(1000);
  assert.deepEqual(d.files('grove'),files);assert.deepEqual([...d.readFile('grove',files[0].path)],[1,2,3]);
  assert.equal(d.systemStatus('grove').sdUsedBytes,3);
  assert.equal(d.snapshot('grove').selected,null);assert.deepEqual(d.snapshot('grove').networks,[]);
  assert.deepEqual(d.observer('grove'),{running:false,networks:[],packets:0,elapsedMs:0,error:null});
  d.reset();assert.equal(d.systemStatus('grove').reboots,0);assert.equal(d.systemStatus('grove').sdUsedBytes,0);
});

test('system inputs use a strict whitelist and never retain credentials',()=>{
  const d=new SimulatedDevice(seed);
  assert.throws(()=>d.systemStatus('absent'),/Unknown module/);
  assert.throws(()=>d.systemStart('grove','download'),/operation/);
  for(const options of [null,[],42,{channel:'nightly'},{channel:null},{outcome:'partial'},{password:'secret'},{url:'https://example.test'},new Date(),{[Symbol('secret')]:'value'}]) {
    assert.throws(()=>d.systemStart('grove','update',options),/options/);
    assert.equal(d.systemStatus('grove').active,null);
  }
  assert.throws(()=>d.systemStart('grove','reboot',{channel:'main'}),/options/);
  const options={channel:'dev'},id=d.systemStart('grove','update',options);options.password='secret';
  assert.deepEqual(d.job(id).options,{channel:'dev',outcome:'success'});
});

test('SD admin reserves a module until stopped and fails immediately on SD removal',()=>{
  const d=new SimulatedDevice(seed),id=d.systemStart('grove','sd_admin');
  d.advance(100000);assert.equal(d.job(id).state,'running');assert.equal(d.job(id).progress,.95);
  assert.throws(()=>d.systemStart('grove','reboot'),/busy/);
  assert.equal(d.systemStatus('mbus').active,null);
  d.setModule('grove',{sdPresent:false});assert.equal(d.job(id).state,'failed');
  assert.equal(d.job(id).error,'sd_missing');assert.equal(d.systemStatus('grove').active,null);
  assert.throws(()=>d.systemStart('grove','sd_admin'),/SD missing/);
  d.setModule('grove',{sdPresent:true});
  for(const stop of ['cancel','disconnect','reset']) {
    d.setModule('grove',{connected:true});const current=d.systemStart('grove','sd_admin');
    if(stop==='cancel')d.cancel(current);
    if(stop==='disconnect')d.setModule('grove',{connected:false});
    if(stop==='reset')d.reset();
    d.advance(100000);assert.equal(d.systemStatus('grove').active,null);
    assert.equal(d.job(current)?.state??null,stop==='reset'?null:'cancelled');
  }
});
