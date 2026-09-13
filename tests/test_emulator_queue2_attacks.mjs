import test from 'node:test';
import assert from 'node:assert/strict';
import {queue2Attacks} from '../tools/ui_emulator/web/queue2-attacks.mjs';
test('all S09-S12 variants have isolated guides',()=>{
 assert.deepEqual(queue2Attacks.slice(0,9).map(x=>x.id),['karma-menu','deauth','sae','handshake','blackout','global-handshaker','snifferdog','beacon','beacon-empty']);
 for(const entry of queue2Attacks){const g=entry.create();g.start({run:9});assert.equal(g.complete,false);assert.ok(g.story.steps.length>=3);g.accept({epoch:g.epoch-1,tab:0,run:10,running:true});assert.equal(g.step,0);g.setContext({tab:2,visible:true});g.accept({epoch:g.epoch,tab:2,run:10,running:true});assert.equal(g.step,0);g.leave();assert.equal(g.active,false);}
});

for(const id of ['deauth','sae','handshake','beacon'])test(id+' rejects stale, wrong target, hidden and changed run',()=>{
 const g=queue2Attacks.find(x=>x.id===id).create();g.start({run:4});
 const emit=s=>g.accept({tab:0,epoch:g.epoch,connected:true,...s});
 emit({scan:true,targets:['net-2'],beaconList:false});assert.equal(g.step,0);
 emit({scan:true,targets:['net-1'],beaconList:true,ssidCount:2});assert.equal(g.step,1);
 g.setContext({tab:0,visible:false});emit({popup:true,running:true,run:5,jobTargets:['net-1']});assert.equal(g.step,1);
 g.setContext({tab:0,visible:true});emit({popup:true,running:true,run:4,jobTargets:['net-1']});assert.equal(g.step,1);
 emit({popup:true,running:true,run:5,jobTargets:['net-1'],packets:24});assert.equal(g.step,2);
 emit({popup:true,running:true,run:6,packets:100,hsSuccess:true});assert.equal(g.step,2);
 emit({popup:true,running:true,run:5,packets:24,hsSuccess:true});assert.equal(g.step,2);
 emit({popup:true,running:true,run:5,packets:48,hsSuccess:true});assert.equal(g.step,3);
 emit({run:5,state:'cancelled',popup:false});assert.equal(g.complete,false);
 emit({run:5,state:'completed',popup:false,active:null});assert.equal(g.complete,true);
 g.start({run:5});assert.equal(g.step,0);assert.equal(g.complete,false);
});
for(const id of ['blackout','global-handshaker','snifferdog'])test(id+' requires confirmation cancellation before start',()=>{
 const g=queue2Attacks.find(x=>x.id===id).create();g.start({});const emit=s=>g.accept({tab:0,epoch:g.epoch,connected:true,...s});
 emit({running:true,run:1});assert.equal(g.step,0);emit({confirm:true});assert.equal(g.step,1);
 emit({confirm:false,popup:false,active:1});assert.equal(g.step,1);emit({confirm:false,popup:false,active:null});assert.equal(g.step,2);
 emit({running:true,run:2,packets:0,popup:true});assert.equal(g.step,3);emit({running:true,run:2,popup:true,packets:24,handshakes:1});assert.equal(g.step,4);
 emit({run:2,state:'completed',popup:false,active:null});assert.equal(g.complete,true);
});
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
test('Handshake reserves its target and module and freezes on cancellation',()=>{
 const d=new SimulatedDevice(seed),n=seed.networks[1],id=d.attackStart('grove','handshake',{bssids:[n.bssid]});
 assert.deepEqual(d.job(id).options.networkIds,[n.id]);assert.throws(()=>d.attackStart('grove','karma'),/busy/);
 d.advance(3000);assert.ok(d.job(id).result.packets>0);assert.ok(d.job(id).result.handshakes.every(x=>x.networkId===n.id));
 d.setModule('grove',{connected:false});assert.equal(d.job(id).state,'cancelled');const final=d.job(id);d.advance(6000);assert.deepEqual(d.job(id),final);
 assert.equal(d.snapshot('mbus').active,null);
});
for(const id of ['karma-menu'])test(id+' requires correct entry and a second fresh portal session',()=>{
 const g=queue2Attacks.find(e=>e.id===id).create();g.start({run:8});const emit=s=>g.accept({tab:0,epoch:g.epoch,connected:true,...s});
 emit(id==='karma-menu'?{scan:true}:{home:true});assert.equal(g.step,0);
 emit(id==='karma-menu'?{home:true}:{scan:true});assert.equal(g.step,1);emit({karma:true});assert.equal(g.step,2);
 emit({karma:true,running:true,run:9,packets:0});emit({karma:true,running:true,run:9,packets:24});assert.equal(g.step,4);
 emit({run:9,state:'completed',karmaConfig:true});assert.equal(g.step,5);
 emit({run:9,running:true,karmaPopup:true,ssids:['NEON-BAZAAR']});assert.equal(g.step,5);
 emit({run:10,running:true,karmaPopup:true,ssids:['wrong']});assert.equal(g.step,5);
 emit({run:10,running:true,karmaPopup:true,ssids:['NEON-BAZAAR'],packets:0});assert.equal(g.step,6);
 emit({run:10,running:true,karmaPopup:true,clients:1,packets:24});assert.equal(g.step,7);
 emit({run:10,state:'completed',popup:false,active:null});assert.equal(g.step,8);emit({home:true,karma:false});assert.equal(g.complete,true);
});
test('Beacon empty refusal needs an empty native list and no reserved operation',()=>{
 const g=queue2Attacks.find(e=>e.id==='beacon-empty').create();g.start({});const emit=s=>g.accept({tab:0,epoch:g.epoch,...s});
 emit({beaconList:true,ssidCount:2});emit({beaconList:true,ssidCount:0});assert.equal(g.step,2);
 emit({beacon:true,ssidCount:0,emptyRefusal:true,active:17});assert.equal(g.step,2);
 emit({beacon:true,ssidCount:0,emptyRefusal:true,active:null});assert.equal(g.step,3);
 emit({beacon:false,beaconList:false,active:null});assert.equal(g.complete,true);
});
