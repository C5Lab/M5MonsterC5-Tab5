import test from 'node:test';
import assert from 'node:assert/strict';
import {settingsS18} from '../tools/ui_emulator/web/settings-s18.mjs';
test('S18 exposes scan save/validation and both disclaimer choices',()=>assert.deepEqual(settingsS18.map(e=>e.id),['s18-scan','s18-scan-invalid','s18-red-cancel','s18-red-enable']));
const state=(g,extra={})=>({epoch:g.epoch,tab:3,scanPopup:false,redPage:false,disclaimer:false,redTeam:true,min:100,max:300,vendor:true,scanId:4,scanState:2,scanVisible:false,has:()=>false,...extra});
test('scan save requires changed native values, reopening and fresh GROVE scan',()=>{
 const g=settingsS18[0].create();g.start(state(g));g.setContext({tab:3,visible:true});
 g.accept(state(g,{scanPopup:true}));assert.equal(g.step,1);
 g.accept(state(g,{scanPopup:true}));assert.equal(g.step,1);
 g.accept(state(g,{scanPopup:true,minInput:200,maxInput:500,vendor:false}));assert.equal(g.step,2);
 g.accept(state(g,{min:200,max:500,vendor:false}));assert.equal(g.step,3);
 g.accept(state(g,{scanPopup:true,min:200,max:500,minInput:200,maxInput:500,vendor:false}));assert.equal(g.step,4);
 g.setContext({tab:0,visible:true});g.accept(state(g,{tab:0,scanId:4,scanVisible:true}));assert.equal(g.complete,false);
 g.accept(state(g,{tab:0,scanId:5,scanVisible:true}));assert.equal(g.complete,true);
});
test('disclaimer cancellation and old state do not count as enabling',()=>{
 const g=settingsS18[3].create();g.start(state(g));g.setContext({tab:3,visible:true});
 g.accept(state(g,{redPage:true,redTeam:false}));g.accept(state(g,{redPage:true,redTeam:false,disclaimer:true}));
 g.accept(state(g,{redPage:true,redTeam:false}));assert.equal(g.step,2);
 g.accept(state(g,{redPage:true,redTeam:true}));assert.equal(g.step,3);
 g.setContext({tab:0,visible:false});g.accept(state(g,{tab:0,has:()=>true}));assert.equal(g.step,3);
 g.setContext({tab:0,visible:true});g.accept(state(g,{tab:0,has:()=>true,epoch:g.epoch-1}));assert.equal(g.step,3);
 g.accept(state(g,{tab:0,has:()=>true}));assert.equal(g.step,4);
 g.accept(state(g,{tab:0,redTeam:true,scanVisible:true,scanId:5,has:()=>true}));assert.equal(g.complete,true);
 g.leave();assert.equal(g.active,false);
});
test('repeated scan story cannot credit Cancel against already saved defaults',()=>{
 const g=settingsS18[0].create();g.start(state(g,{min:200,max:500}));g.setContext({tab:3,visible:true});
 g.accept(state(g,{scanPopup:true,min:200,max:500}));
 g.accept(state(g,{scanPopup:true,minInput:200,maxInput:500,vendor:false}));assert.equal(g.step,1);
 g.accept(state(g,{scanPopup:true,minInput:150,maxInput:450,vendor:false}));assert.equal(g.step,2);
 g.accept(state(g,{min:200,max:500,vendor:false}));assert.equal(g.step,2);
 g.accept(state(g,{min:150,max:450,vendor:false}));assert.equal(g.step,3);
});
test('disabled Red Team needs the restricted action bar and wrong tabs are ignored',()=>{
 const g=settingsS18[2].create();g.start(state(g));g.setContext({tab:2,visible:true});
 g.accept(state(g,{redPage:true,redTeam:false}));assert.equal(g.step,0);
 g.setContext({tab:3,visible:true});g.accept(state(g,{redPage:true,redTeam:false}));
 g.accept(state(g,{redPage:true,redTeam:false,disclaimer:true}));g.accept(state(g,{redPage:true,redTeam:false}));
 g.setContext({tab:0,visible:true});g.accept(state(g,{tab:0,redTeam:false,has:()=>true}));
 g.accept(state(g,{tab:0,redTeam:false,scanVisible:true,scanId:5,has:()=>true}));assert.equal(g.complete,false);
 g.accept(state(g,{tab:0,redTeam:false,scanVisible:true,scanId:5,has:t=>['Scan & Test','ARP','Nmap'].includes(t)}));assert.equal(g.complete,true);
});
test('restarting validation with old open error requires closing and reopening',()=>{
 const g=settingsS18[1].create();const old={scanPopup:true,has:()=>true};g.start(state(g,old));g.setContext({tab:3,visible:true});
 g.accept(state(g,old));g.accept(state(g,old));assert.equal(g.step,0);
 g.accept(state(g));g.accept(state(g,{scanPopup:true}));assert.equal(g.step,1);
 g.accept(state(g,{scanPopup:true}));assert.equal(g.step,1);
});
