import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const kinds=['blackout','snifferdog','global_handshaker'];

test('global operations need no selection, own one module, advance and freeze on stop',()=>{
 for(const kind of kinds){
  const d=new SimulatedDevice(seed),id=d.attackStart('grove',kind);
  assert.equal(d.job(id).state,'running');
  assert.equal(d.job(id).options.networkIds.length,12);
  assert.throws(()=>d.scan('grove'),/busy/);
  assert.equal(d.snapshot('mbus').active,null);
  d.advance(3000);assert.ok(d.job(id).result.packets>0);
  assert.equal(d.job(id).result.simulated,true);
  assert.equal(d.attackStop(id),true);
  const final=d.job(id);d.advance(10000);assert.deepEqual(d.job(id),final);
  assert.equal(d.snapshot('grove').active,null);
  assert.deepEqual(d.files('grove'),[]);
 }
});

test('global Handshaker emits deterministic bounded scenario results, including an empty scenario',()=>{
 const small=structuredClone(seed);
 small.networks=small.networks.slice(0,1);
 small.clients=small.clients.filter(c=>c.network_id==='net-1');
 const a=new SimulatedDevice(small),b=new SimulatedDevice(small);
 const x=a.attackStart('grove','global_handshaker'),y=b.attackStart('grove','global_handshaker');
 a.advance(5000);for(let i=0;i<50;i++)b.advance(100);
 assert.deepEqual(a.job(x).result,b.job(y).result);
 assert.deepEqual(a.job(x).result.handshakes,[{networkId:'net-1',ssid:'NEON-BAZAAR',bssid:'02:20:77:00:00:01'}]);
 a.advance(100000);assert.equal(a.job(x).result.handshakes.length,1);
 small.networks=[];small.clients=[];
 const empty=new SimulatedDevice(small),id=empty.attackStart('grove','global_handshaker');
 empty.advance(10000);assert.deepEqual(empty.job(id).result.handshakes,[]);
});

test('global jobs cancel on disconnect/reset and do not stop another module',()=>{
 for(const kind of kinds){
  const d=new SimulatedDevice(seed),a=d.attackStart('grove',kind),b=d.attackStart('mbus',kind);
  d.setModule('grove',{connected:false});d.advance(2000);
  assert.equal(d.job(a).state,'cancelled');assert.equal(d.job(b).state,'running');
  assert.throws(()=>d.attackStart('grove',kind),/disconnected/);
  d.cancel(b);assert.equal(d.job(b).state,'cancelled');
  d.reset();assert.equal(d.job(a),null);assert.equal(d.job(b),null);
  assert.ok(d.attackStart('grove',kind)>b);
 }
});
