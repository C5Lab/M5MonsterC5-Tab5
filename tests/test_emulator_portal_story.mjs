import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {portalStories} from '../tools/ui_emulator/web/portal-story.mjs';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';

const entry=id=>portalStories.find(x=>x.id===id);

test('S08 exposes three independent client & password demos',()=>{
 assert.deepEqual(portalStories.map(x=>x.id),['evil-twin','rogue-ap','internal-portal']);
 for(const e of portalStories){
  const g=e.create();g.start({run:0});
  assert.equal(g.complete,false);assert.equal(g.story.steps.length,6);
  g.accept({epoch:g.epoch-1,tab:g.story.tab,running:true,run:5,scanReady:true,targets:['net-1']});
  assert.equal(g.step,0,'stale epoch rejected');
  g.leave();assert.equal(g.active,false);
 }
});

for(const id of ['evil-twin','rogue-ap']){
 test(`${id} rejects wrong target, hidden view and stale run, then completes`,()=>{
  const g=entry(id).create();g.start({run:0});
  const emit=s=>g.accept({tab:0,epoch:g.epoch,connected:true,...s});
  emit({scanReady:true,targets:['net-2']});assert.equal(g.step,0);
  emit({scanReady:false,targets:['net-1']});assert.equal(g.step,0);
  emit({scanReady:true,targets:['net-1']});assert.equal(g.step,1);
  g.setContext({tab:0,visible:false});emit({running:true,run:5});assert.equal(g.step,1);
  g.setContext({tab:0,visible:true});emit({running:false,run:0});assert.equal(g.step,1);
  emit({running:true,run:5});assert.equal(g.step,2);
  emit({run:5,stage:'connected'});assert.equal(g.step,3);
  emit({run:5,stage:'portal_opened'});assert.equal(g.step,4);
  emit({run:5,stage:'submitted',password:false});assert.equal(g.step,4,'password label required');
  emit({run:5,stage:'submitted',password:true});assert.equal(g.step,5);
  emit({run:9,stage:'submitted',password:true});assert.equal(g.step,5,'changed run frozen');
  emit({run:5,state:'completed',active:null});assert.equal(g.complete,true);
  g.start({run:5});assert.equal(g.step,0);assert.equal(g.complete,false);
 });
}

test('internal-portal runs on the INTERNAL tab and needs the portal page',()=>{
 const g=entry('internal-portal').create();assert.equal(g.story.tab,3);g.start({run:0});
 const emit=s=>g.accept({tab:3,epoch:g.epoch,connected:true,...s});
 g.setContext({tab:0,visible:true});emit({portalPage:true});assert.equal(g.step,0,'wrong tab rejected');
 g.setContext({tab:3,visible:true});emit({portalPage:false});assert.equal(g.step,0);
 emit({portalPage:true});assert.equal(g.step,1);
 emit({running:true,run:4});assert.equal(g.step,2);
 emit({run:4,stage:'connected'});emit({run:4,stage:'portal_opened'});emit({run:4,stage:'submitted',password:true});
 assert.equal(g.step,5);
 emit({run:4,state:'completed',active:null});assert.equal(g.complete,true);
});

test('disconnection pauses any portal story',()=>{
 const g=entry('evil-twin').create();g.start({run:0});
 const emit=s=>g.accept({tab:0,epoch:g.epoch,...s});
 emit({connected:true,scanReady:true,targets:['net-1']});assert.equal(g.step,1);
 emit({connected:false,running:true,run:5});assert.equal(g.step,1);assert.match(g.hint,/Reconnect/);
});

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
for(const [op,mod] of [['evil_twin','grove'],['rogue_ap','grove'],['portal_demo','internal']]){
 test(`${op} yields the fixed demo password and freezes on disconnect`,()=>{
  const d=new SimulatedDevice(seed),net=seed.networks[0];
  const id=d.attackStart(mod,op,{networkIds:[net.id]});
  assert.equal(d.job(id).result.demo.stage,'waiting');
  d.advance(21000);assert.equal(d.job(id).result.demo.stage,'connected');
  d.advance(30000);assert.equal(d.job(id).result.demo.stage,'portal_opened');
  d.advance(30000);assert.equal(d.job(id).result.demo.stage,'submitted');
  assert.equal(d.job(id).result.demo.password,'DEMO-only-2026!');
  d.setModule(mod,{connected:false});assert.equal(d.job(id).state,'cancelled');
  const frozen=d.job(id);d.advance(9000);assert.deepEqual(d.job(id),frozen);
 });
}
