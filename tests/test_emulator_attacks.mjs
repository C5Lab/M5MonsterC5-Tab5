import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const target={bssids:[seed.networks[0].bssid]};

test('all seven simulated operations start, change and stop without another module changing',()=>{
 for(const kind of ['deauth','arp','karma','beacon','rogue_ap','evil_twin','mitm']){
  const d=new SimulatedDevice(seed),id=d.attackStart('grove',kind,target);
  assert.equal(d.job(id).state,'running');assert.throws(()=>d.scan('grove'),/busy/);
  d.advance(2500);assert.ok(d.job(id).result.packets>0);assert.ok(d.job(id).result.events.length);
  assert.equal(d.snapshot('mbus').active,null);assert.equal(d.files('mbus').length,0);
  assert.equal(d.attackStop(id),true);const final=d.job(id);d.advance(2000);
  assert.deepEqual(d.job(id),final);assert.equal(d.attackStop(id),false);
  assert.equal(d.snapshot('grove').active,null);
 }
});

test('cancel, disconnect and reset never resurrect jobs or write captures',()=>{
 const d=new SimulatedDevice(seed),id=d.attackStart('grove','mitm',target);
 d.advance(1500);d.cancel(id);d.advance(30000);assert.equal(d.job(id).state,'cancelled');assert.deepEqual(d.files('grove'),[]);
 const next=d.attackStart('grove','deauth',target);d.setModule('grove',{connected:false});d.advance(3000);
 assert.equal(d.job(next).state,'cancelled');assert.throws(()=>d.attackStart('grove','karma'),/disconnected/);
 d.reset();assert.equal(d.job(next),null);assert.ok(d.attackStart('grove','karma')>next);
});

test('invalid inputs never reserve the module and credentials never enter model history',()=>{
 const d=new SimulatedDevice(seed);
 for(const [kind,options] of [['unknown',{}],['arp',{}],['deauth',{bssids:['00:00:00:00:00:00']}],['beacon',{customSSIDs:['x'.repeat(33)]}],['arp',{...target,targetMac:'invalid'}]])
  assert.throws(()=>d.attackStart('grove',kind,options));
 assert.equal(d.snapshot('grove').active,null);
 const id=d.attackStart('grove','evil_twin',{...target,password:'never-log-this'});d.advance(3000);
 assert.ok(!JSON.stringify(d.job(id)).includes('never-log-this'));
 const mutable=d.job(id);mutable.result.clients.length=0;assert.ok(d.job(id).result.clients.length>0);
});

test('MITM creates target-specific PCAP only on stop and handles SD/quota failures atomically',()=>{
 for(const capacity of [8*1024*1024,1]){
  const d=new SimulatedDevice(seed,{sdCapacityBytes:capacity}),id=d.attackStart('grove','mitm',target);
  d.advance(3000);assert.equal(d.files('grove').length,0);d.attackStop(id);
  const job=d.job(id);
  if(capacity===1){assert.equal(job.error,'storage_full');assert.equal(d.files('grove').length,0);}
  else {const bytes=d.readFile('grove',job.file);assert.equal(new DataView(bytes.buffer).getUint32(0,true),0xa1b2c3d4);assert.equal(d.files('grove')[0].networkId,'net-1');}
  assert.equal(d.snapshot('grove').active,null);
 }
 const d=new SimulatedDevice(seed),id=d.attackStart('grove','mitm',target);d.setModule('grove',{sdPresent:false});d.attackStop(id);
 assert.equal(d.job(id).error,'sd_missing');assert.deepEqual(d.files('grove'),[]);
});

test('clock slicing produces identical bounded simulated results',()=>{
 const a=new SimulatedDevice(seed),b=new SimulatedDevice(seed),x=a.attackStart('grove','karma'),y=b.attackStart('grove','karma');
 a.advance(10000);for(let i=0;i<100;i++)b.advance(100);
 assert.deepEqual(a.job(x).result,b.job(y).result);
 a.advance(100000000);assert.ok(a.job(x).result.events.length<=8);
});

test('Observer preserves scenario identities, changes activity, freezes on stop and resets',()=>{
 const d=new SimulatedDevice(seed);d.observerStart('grove',['net-1']);const first=d.observer('grove');
 assert.equal(first.networks[0].bssid,seed.networks[0].bssid);
 assert.equal(first.networks[0].clients.length,seed.clients.filter(c=>c.network_id==='net-1').length);
 d.advance(2000);const second=d.observer('grove');assert.ok(second.packets>first.packets);
 assert.notEqual(second.networks[0].rssi,first.networks[0].rssi);
 assert.equal(d.observer('mbus').running,false);
 d.observerStop('grove');const frozen=d.observer('grove');d.advance(5000);assert.deepEqual(d.observer('grove'),frozen);
 d.observerStart('grove',['net-1']);d.setModule('grove',{connected:false});assert.equal(d.observer('grove').running,false);
 d.reset();assert.deepEqual(d.observer('grove').networks,[]);
});
