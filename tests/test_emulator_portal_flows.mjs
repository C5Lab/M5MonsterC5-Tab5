import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('Global Portal accepts a typed demo SSID and completes a deterministic local story',()=>{
 const a=new SimulatedDevice(seed),b=new SimulatedDevice(seed);
 const input={ssid:'LAB Portal',portal:'/sdcard/lab/portals/offline-demo.html'};
 const x=a.attackStart('grove','portal_demo',input),y=b.attackStart('grove','portal_demo',input);
 assert.deepEqual(a.job(x).options.networkIds,[]);
 assert.equal(a.job(x).result.demo.ssid,'LAB Portal');
 assert.equal(a.job(x).result.demo.stage,'waiting');
 assert.deepEqual(a.job(x).result.clients,[]);
 assert.throws(()=>a.scan('grove'),/busy/);
 a.advance(20000);assert.equal(a.job(x).result.demo.stage,'connected');
 a.advance(30000);assert.equal(a.job(x).result.demo.stage,'portal_opened');
 a.advance(30000);b.advance(80000);
 assert.equal(a.job(x).result.demo.stage,'submitted');
 assert.equal(a.job(x).result.demo.password,'DEMO-only-2026!');
 assert.deepEqual(a.job(x).result,b.job(y).result);
 assert.deepEqual(a.files('grove').map(file=>file.path),['/sdcard/lab/portals.txt']);
 assert.match(new TextDecoder().decode(a.readFile('grove','/sdcard/lab/portals.txt')),
              /LAB Portal.*password=DEMO-only-2026!/);
 assert.deepEqual(a.files('mbus'),[]);
 assert.equal(a.attackStop(x),true);
 const stopped=a.job(x);a.advance(30000);assert.deepEqual(a.job(x),stopped);
});

test('Portal input, SD and module boundaries are enforced',()=>{
 const d=new SimulatedDevice(seed);
 assert.throws(()=>d.attackStart('grove','portal_demo',{ssid:''}),/SSID/);
 assert.throws(()=>d.attackStart('grove','portal_demo',{ssid:'x'.repeat(33)}),/SSID/);
 assert.throws(()=>d.attackStart('grove','portal_demo',{}),/network|SSID/);
 d.setModule('grove',{sdPresent:false});
 assert.throws(()=>d.attackStart('grove','portal_demo',{ssid:'LAB Portal'}),/SD/);
 const internal=d.attackStart('internal','portal_demo',{networkIds:['net-1']});
 assert.equal(d.job(internal).state,'running');
 d.setModule('grove',{sdPresent:true});
 const observer=d.attackStart('grove','portal_demo',{networkIds:['net-2'],portal:'Offline demo'});
 assert.equal(d.job(observer).result.demo.ssid,seed.networks[1].ssid);
 assert.equal(d.snapshot('mbus').active,null);
 d.setModule('grove',{connected:false});
 assert.equal(d.job(observer).state,'cancelled');
 assert.equal(d.job(internal).state,'running');
});
