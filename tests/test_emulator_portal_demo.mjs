import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const target={networkIds:['net-1']};

test('portal story reveals connection, page visit and synthetic submission in order',()=>{
 for(const kind of ['evil_twin','rogue_ap','portal_demo']){
  const d=new SimulatedDevice(seed),id=d.attackStart(kind==='portal_demo'?'internal':'grove',kind,target);
  assert.equal(d.job(id).result.demo.stage,'waiting');
  d.advance(20000);assert.equal(d.job(id).result.demo.stage,'connected');
  assert.equal(d.job(id).result.demo.password,null);
  d.advance(30000);assert.equal(d.job(id).result.demo.stage,'portal_opened');
  assert.equal(d.job(id).result.demo.password,null);
  d.advance(30000);const result=d.job(id).result.demo;
  assert.equal(result.stage,'submitted');assert.equal(result.password,'DEMO-only-2026!');
  assert.equal(result.ssid,'NEON-BAZAAR');
  assert.ok(seed.clients.some(c=>c.mac===result.client.mac&&c.network_id==='net-1'));
  assert.deepEqual(d.files('grove'),[]);
 }
});

test('stop or disconnect before submission freezes the story; other module stays independent',()=>{
 const d=new SimulatedDevice(seed),a=d.attackStart('grove','evil_twin',target),b=d.attackStart('mbus','rogue_ap',target);
 d.advance(20000);d.attackStop(a);const frozen=d.job(a);
 d.advance(60000);assert.deepEqual(d.job(a),frozen);assert.equal(d.job(b).result.demo.stage,'submitted');
 d.attackStop(b);const c=d.attackStart('grove','evil_twin',target);d.advance(20000);
 d.setModule('grove',{connected:false});d.advance(90000);assert.equal(d.job(c).result.demo.password,null);
 const empty=structuredClone(seed);empty.clients=[];
 const e=new SimulatedDevice(empty),id=e.attackStart('grove','evil_twin',target);e.advance(100000);
 assert.equal(e.job(id).result.demo.stage,'submitted');assert.equal(e.job(id).result.demo.password,'DEMO-only-2026!');
});

test('every selectable network completes the demo even without a seeded client',()=>{
 for(const network of seed.networks)for(const kind of ['evil_twin','rogue_ap','portal_demo']){
  const d=new SimulatedDevice(seed),id=d.attackStart(kind==='portal_demo'?'internal':'grove',kind,{networkIds:[network.id]});
  d.advance(80000);const demo=d.job(id).result.demo;
  assert.equal(demo.stage,'submitted',network.ssid+' '+kind);
  assert.equal(demo.ssid,network.ssid);assert.equal(demo.password,'DEMO-only-2026!');
  assert.ok(demo.client);assert.deepEqual(d.files('internal'),[]);
 }
});
