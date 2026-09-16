import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
test('radar tracks exactly one target with bounded changing RSSI and reserves its module',()=>{
 const d=new SimulatedDevice(seed);
 assert.throws(()=>d.attackStart('grove','ap_radar',{}));
 assert.throws(()=>d.attackStart('grove','ap_radar',{networkIds:['net-1','net-3']}));
 const id=d.attackStart('grove','ap_radar',{networkIds:['net-3']});
 assert.equal(d.job(id).result.signal.rssi,-56);
 assert.equal(d.job(id).result.signal.bssid,'02:20:77:00:00:03');
 assert.throws(()=>d.scan('grove'),/busy/);
 d.advance(1000);assert.notEqual(d.job(id).result.signal.rssi,-56);
 for(let i=0;i<30;i++){d.advance(1000);const r=d.job(id).result.signal.rssi;assert.ok(r>=-100&&r<=-20);}
 d.attackStop(id);const stopped=d.job(id);d.advance(5000);assert.deepEqual(d.job(id),stopped);
 assert.deepEqual(d.files('grove'),[]);
});
test('radar disconnect and reset cannot affect another module or restart old readings',()=>{
 const d=new SimulatedDevice(seed),a=d.attackStart('grove','ap_radar',{networkIds:['net-1']}),b=d.attackStart('mbus','ap_radar',{networkIds:['net-3']});
 d.advance(1000);d.setModule('grove',{connected:false});const stopped=d.job(a);
 d.advance(5000);assert.deepEqual(d.job(a),stopped);assert.equal(d.job(b).state,'running');
 d.reset();assert.equal(d.job(b),null);assert.equal(d.snapshot('mbus').active,null);
});
