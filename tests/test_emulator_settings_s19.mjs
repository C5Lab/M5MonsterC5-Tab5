import test from 'node:test';
import assert from 'node:assert/strict';
import {settingsS19} from '../tools/ui_emulator/web/settings-s19.mjs';
const state=(extra={})=>({tab:3,timeout:4,brightness:80,dark:true,dashboard:true,boot:1,alert:true,rotation:0,activeRotation:0,timeoutOpen:false,brightnessOpen:false,themeOpen:false,rotationOpen:false,settings:true,saved:{timeout:4,brightness:80,dark:1,rotation:0,dashboard:1,boot:1,alert:1},filter:'brightness(0.8)',...extra});
function setup(id){const g=settingsS19.find(e=>e.id==='s19-'+id).create();g.start(state());g.setContext({tab:3,visible:true});return g;}
function feed(g,extra={}){g.accept({...state(extra),epoch:g.epoch});}
test('timeout needs fresh changed preference and close/reopen before restoration',()=>{
 const g=setup('timeout');feed(g,{timeoutOpen:true});assert.equal(g.step,1);
 feed(g,{timeoutOpen:true});assert.equal(g.step,1);
 const changed={timeout:0,saved:{timeout:0},timeoutOpen:true};feed(g,changed);assert.equal(g.step,2);
 feed(g,changed);assert.equal(g.step,2);feed(g,{...changed,timeoutOpen:false});feed(g,changed);assert.equal(g.step,4);
 feed(g,{timeoutOpen:true});feed(g);assert.equal(g.complete,true);
});
test('brightness requires saved release and actual canvas filter',()=>{
 const g=setup('brightness');feed(g,{brightnessOpen:true});
 feed(g,{brightnessOpen:true,brightness:30,saved:{brightness:80},filter:'brightness(0.3)'});assert.equal(g.step,1);
 feed(g,{brightnessOpen:true,brightness:30,saved:{brightness:30},filter:'brightness(0.8)'});assert.equal(g.step,1);
 feed(g,{brightnessOpen:true,brightness:30,saved:{brightness:30},filter:'brightness(0.3)'});assert.equal(g.step,2);
});
test('hidden, wrong tab and stale epochs never advance',()=>{
 for(const kind of ['timeout','brightness','theme','rotation']){
  const g=setup(kind),s=state({[kind+'Open']:true,epoch:g.epoch});
  g.accept({...s,epoch:0});assert.equal(g.step,0);g.setContext({tab:0,visible:true});g.accept(s);assert.equal(g.step,0);
  g.setContext({tab:3,visible:false});g.accept(s);assert.equal(g.step,0);g.leave();g.accept(s);assert.equal(g.step,0);
 }
});
test('theme automatic rebuild still requires changed native preference and reopening',()=>{
 const g=setup('theme');feed(g,{themeOpen:true});feed(g,{dark:false,saved:{dark:0}});assert.equal(g.step,2);
 feed(g,{dark:false,themeOpen:true,saved:{dark:0}});assert.equal(g.step,3);
 feed(g,{themeOpen:false});assert.equal(g.step,4);feed(g,{themeOpen:true});assert.equal(g.step,5);
 feed(g,{themeOpen:true});assert.equal(g.step,5);
 const changed={themeOpen:true,dashboard:false,boot:0,alert:false,saved:{dashboard:0,boot:0,alert:0}};
 feed(g,changed);feed(g,changed);feed(g,changed);assert.equal(g.step,8);
 feed(g,{...changed,themeOpen:false});feed(g,changed);assert.equal(g.step,10);
 feed(g,{themeOpen:true});feed(g,{themeOpen:true});feed(g,{themeOpen:true});assert.equal(g.step,13);
 feed(g);feed(g,{themeOpen:true});feed(g);assert.equal(g.complete,true);
});
test('rotation handoff only follows native pending change and rejects invalid or replayed data',()=>{
 const g=setup('rotation');assert.equal(g.prepareRestart(state(),1),null);
 feed(g,{rotationOpen:true});feed(g,{rotationOpen:true,rotation:1,saved:{rotation:1}});
 feed(g,{rotation:1,saved:{rotation:1}});feed(g,{rotationOpen:true,rotation:1,saved:{rotation:1}});assert.equal(g.step,4);
 const pending=state({rotationOpen:true,rotation:1,saved:{rotation:1}});
 assert.equal(g.prepareRestart(pending,2),null);const payload=g.prepareRestart(pending,1);assert.deepEqual(payload,{version:1,fromRotation:0,expectedRotation:1});
 const next=setup('rotation'),applied=state({tab:0,rotation:1,activeRotation:1,saved:{rotation:1}});
 assert.equal(next.resumeRestart({},applied),false);assert.equal(next.resumeRestart({...payload,extra:1},applied),false);
 assert.equal(next.resumeRestart({...payload,fromRotation:1},applied),false);assert.equal(next.resumeRestart(payload,state()),false);
 assert.equal(next.resumeRestart(payload,applied),true);assert.equal(next.step,5);assert.equal(next.resumeRestart(payload,applied),false);
 next.setContext({tab:3,visible:true});feed(next,{rotationOpen:true,rotation:1,activeRotation:1,saved:{rotation:1}});feed(next,{rotation:1,activeRotation:1,saved:{rotation:1}});assert.equal(next.complete,true);
});
