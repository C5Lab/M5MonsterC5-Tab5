import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('Bluetooth blacklist affects only its module and WiFi picker returns immutable seed snapshots',()=>{
  const d=new SimulatedDevice(seed),mac=seed.bluetooth[0].mac;
  d.wardriveSetBlacklist('grove',[mac.toLowerCase()]);
  for(const id of ['grove','mbus'])d.wardriveStart(id);
  d.advance(20000);
  assert.ok(!d.wardrive('grove').rows.some(r=>r.bssid===mac));
  assert.ok(d.wardrive('mbus').rows.some(r=>r.bssid===mac));
  assert.equal(d.wardrive('grove').wifiCount,seed.networks.length);
  d.wardriveSetBlacklist('grove',[]);d.advance(1000);
  assert.ok(d.wardrive('grove').rows.some(r=>r.bssid===mac));
  const rows=d.nearbyNetworks('grove');rows.length=0;assert.equal(d.nearbyNetworks('grove').length,seed.networks.length);
  d.setModule('grove',{connected:false});assert.deepEqual(d.nearbyNetworks('grove'),[]);
});

test('native upload selection, pending and force-all modes preserve provider and module isolation',()=>{
  const d=new SimulatedDevice(seed);
  for(let i=0;i<3;i++){d.wardriveStart('grove');d.advance(3000);d.wardriveStop('grove');}
  const ids=d.wardrive('grove').sessions.map(s=>s.id);
  assert.equal(d.wardriveUpload('grove','success',{provider:'wigle',mode:'selected',sessionIds:[ids[1]]}).uploaded,1);
  assert.deepEqual(d.wardrive('grove').sessions.map(s=>s.uploadStatus.wigle),['pending','done','pending']);
  assert.deepEqual(d.wardrive('grove').sessions.map(s=>s.uploadStatus.wdgwars),['pending','pending','pending']);
  assert.equal(d.wardriveUpload('grove','success',{provider:'wigle',mode:'pending'}).uploaded,2);
  assert.equal(d.wardriveUpload('grove','success',{provider:'wigle',mode:'pending'}).total,0);
  assert.equal(d.wardriveUpload('grove','failure',{provider:'wigle',mode:'all'}).failed,3);
  assert.equal(d.wardriveUpload('grove','success',{provider:'wdgwars',mode:'all'}).uploaded,3);
  assert.ok(d.wardrive('grove').sessions.every(s=>!s.uploaded));
  assert.throws(()=>d.wardriveUpload('mbus','success',{provider:'wigle',mode:'selected',sessionIds:[ids[0]]}),/Select/);
  assert.equal(d.wardrive('mbus').upload,null);
});

test('Wardrive acquires GPS and discovers seed identities deterministically regardless of tick size',()=>{
  const a=new SimulatedDevice(seed),b=new SimulatedDevice(seed);
  a.wardriveStart('grove');b.wardriveStart('grove');
  a.advance(1000);assert.equal(a.wardrive('grove').gps.fix,false);
  a.advance(19000);for(let i=0;i<200;i++)b.advance(100);
  assert.deepEqual(a.wardrive('grove'),b.wardrive('grove'));
  const state=a.wardrive('grove');
  assert.equal(state.wifiCount,seed.networks.length);
  assert.equal(state.btCount,seed.bluetooth.length);
  assert.deepEqual(state.rows.filter(r=>r.kind==='wifi').map(r=>r.bssid),seed.networks.map(n=>n.bssid));
  assert.ok(state.gps.fix);assert.equal(state.track.length,19);
  state.rows.length=0;assert.ok(a.wardrive('grove').rows.length);
  assert.equal(a.wardrive('mbus').running,false);
});

test('Stop commits a session once, frees radio, upload outcomes are simulated and retry only pending sessions',()=>{
  const d=new SimulatedDevice(seed);
  for(let i=0;i<3;i++) {d.wardriveStart('grove');d.advance(4000);d.wardriveStop('grove');}
  d.wardriveStop('grove');assert.equal(d.wardrive('grove').sessions.length,3);
  assert.equal(d.snapshot('grove').active,null);
  assert.deepEqual(d.wardriveUpload('grove','failure'),{simulated:true,outcome:'failure',total:3,uploaded:0,failed:3});
  assert.equal(d.wardriveUpload('grove','partial').uploaded,1);
  assert.equal(d.wardriveUpload('grove').uploaded,2);
  assert.equal(d.wardriveUpload('grove').total,0);
  assert.equal(d.wardrive('mbus').sessions.length,0);
});

test('Disconnect and cancellation cannot later save stale sessions; reset clears all state',()=>{
  const d=new SimulatedDevice(seed),id=d.wardriveStart('grove');
  assert.throws(()=>d.scan('grove'),/busy/);
  d.wardriveStart('mbus');d.advance(3000);
  d.setModule('grove',{connected:false});d.advance(3000);d.wardriveStop('grove');
  assert.equal(d.job(id).state,'cancelled');assert.equal(d.wardrive('grove').sessions.length,0);
  assert.equal(d.wardrive('mbus').running,true);
  d.wardriveStop('mbus');assert.equal(d.wardrive('mbus').sessions.length,1);
  d.reset();assert.equal(d.job(id),null);assert.equal(d.wardrive('mbus').sessions.length,0);
});

test('SD errors never commit partial session metadata and do not leave the radio busy',()=>{
  for(const missing of [false,true]) {
    const d=new SimulatedDevice(seed,{sdCapacityBytes:0});d.wardriveStart('grove');d.advance(3000);
    if(missing)d.setModule('grove',{sdPresent:false});
    const s=d.wardriveStop('grove');assert.equal(s.error,missing?'sd_missing':'storage_full');
    assert.equal(s.sessions.length,0);assert.equal(d.snapshot('grove').active,null);
  }
});

test('GPS loss freezes track until restored, isolated by module; empty scenarios stay empty',()=>{
  const empty=structuredClone(seed);empty.networks=[];empty.clients=[];empty.bluetooth=[];
  const d=new SimulatedDevice(empty);d.wardriveStart('grove');d.advance(3000);
  const count=d.wardrive('grove').track.length;
  d.wardriveSetGps('grove',false);d.advance(4000);
  assert.equal(d.wardrive('grove').gps.fix,false);assert.equal(d.wardrive('grove').track.length,count);
  d.wardriveSetGps('grove',true);d.advance(1000);
  assert.ok(d.wardrive('grove').gps.fix);assert.ok(d.wardrive('grove').track.length>count);
  assert.deepEqual(d.wardrive('grove').track.map(p=>p.timeMs),[2000,3000,8000]);
  assert.deepEqual(d.wardrive('grove').rows,[]);
  assert.throws(()=>d.wardriveSetGps('grove',1),/GPS/);
});

test('Configured GPS coordinates are used and long sessions retain a bounded recent track',()=>{
  const custom=structuredClone(seed);custom.gps.route=[{latitude:50,longitude:20}];
  const d=new SimulatedDevice(custom);d.wardriveStart('grove');d.advance(1000000);
  const s=d.wardrive('grove');assert.equal(s.gps.lat,50);assert.equal(s.gps.lon,20);assert.equal(s.track.length,600);assert.equal(s.gps.distanceM,0);
  custom.gps.route=[{lat:NaN,lon:20}];const bad=new SimulatedDevice(custom);
  assert.throws(()=>bad.wardriveStart('grove'),/GPS/);assert.equal(bad.snapshot('grove').active,null);
});
