import test from 'node:test';
import assert from 'node:assert/strict';
import {ReconGuide,reconStory} from '../tools/ui_emulator/web/recon-stories.mjs';
function driver(kind){
 const g=new ReconGuide(reconStory(kind));g.start({run:4,captureId:10});
 let s={epoch:g.epoch,tab:0,visible:true,page:true,run:5,running:true,home:false,packets:1,networks:2,clients:3,nodes:3,expanded:'',timeMs:0,captureId:0};
 return {g,send(p={}){s={...s,...p};g.accept(s);return g.story.steps[g.step]?.key;}};
}
test('Observer: fresh independent discovery, capture, both exit decisions and home',()=>{
 const {g,send}=driver('observer');
 assert.equal(send(),'activity');assert.equal(send(),'activity');
 assert.equal(send({packets:20,timeMs:2200}),'client');
 assert.equal(send({target:'02:20:77:00:00:01',popup:true}),'capture');
 assert.equal(send({captureId:11,captureState:2,captureTarget:'net-1',file:true,captureVisible:true}),'close');
 assert.equal(send({captureVisible:false,popup:false}),'ask-keep');
 assert.equal(send({confirm:true}),'keep');
 assert.equal(send({confirm:false}),'ask-stop');
 assert.equal(send({confirm:true}),'exit');
 send({confirm:false,running:false,stopped:5,page:false,home:true});assert.equal(g.complete,true);
});
test('Mesh: changing counters, native nodes, Stop preserves results, Clear and Back',()=>{
 const {g,send}=driver('mesh');send();send();
 assert.equal(send({packets:20,timeMs:2200}),'nodes');
 assert.equal(send({expanded:'0x1A2B'}),'stop');
 assert.equal(send({running:false,stopped:5}),'clear');
 assert.equal(send({cleared:5,networks:0,nodes:0,expanded:''}),'back');
 send({page:false,home:true});assert.equal(g.complete,true);
});
test('Old sessions, modules, hidden views, earlier runs and restart do not advance',()=>{
 const {g,send}=driver('observer');
 send({run:4});assert.equal(g.step,0);send({run:5,tab:2});assert.equal(g.step,0);
 send({tab:0,visible:false});assert.equal(g.step,0);
 send({visible:true,epoch:g.epoch-1});assert.equal(g.step,0);
 send({epoch:g.epoch});assert.equal(g.step,1);
 g.start({run:5});send({epoch:g.epoch});assert.equal(g.step,0);
});
test('Hidden observation and a new run reset partial evidence',()=>{
 const {g,send}=driver('mesh');send();send();send({visible:false,timeMs:3000});
 send({visible:true,timeMs:3100,packets:20});assert.equal(g.step,1);
 send({timeMs:5200,packets:40});assert.equal(g.step,2);
 send({run:6});assert.equal(g.step,0);
});
test('External cancellation cannot replace native Stop or preserve progress',()=>{
 const {g,send}=driver('mesh');send();send();send({timeMs:2200,packets:20});send({expanded:'0x1A2B'});
 send({running:false,stopped:0});assert.equal(g.complete,false);assert.equal(g.step,0);
});
test('Wrong target, previous capture and absent file cannot complete capture',()=>{
 const {g,send}=driver('observer');send();send();send({timeMs:2200,packets:20});
 send({popup:true,target:'wrong'});assert.equal(g.step,2);
 send({target:'02:20:77:00:00:01'});assert.equal(g.step,3);
 send({captureId:10,captureState:2,captureVisible:true,file:true,captureTarget:'net-1'});assert.equal(g.step,3);
 send({captureId:11,captureTarget:'net-2'});assert.equal(g.step,3);
 send({captureTarget:'net-1',file:false});assert.equal(g.step,3);
});
