import test from 'node:test';import assert from 'node:assert/strict';
import {queue2System} from '../tools/ui_emulator/web/queue2-system.mjs';
const base=()=>({tab:3,grove:{connected:true,reboots:0,activeSlot:0,version:'demo-main-1'},mbus:{connected:true,reboots:0},groveReboot:4,mbusReboot:5,slotId:4,updateId:4,sdId:4,has:()=>true,bound:()=>true});
function driver(id){const g=queue2System.find(x=>x.id===id).create();g.start(base());g.setContext({tab:3,visible:true});let s={...base(),epoch:g.epoch,timeMs:0};return{g,send(v){s={...s,...v};g.accept(s);return g.step;}};}
for(const target of ['grove','mbus'])test('S21 reboot must belong to '+target+' and a fresh completed native job',()=>{
 const {g,send}=driver('s21-reboot-'+target);send({statusPage:true});
 send({[target+'RebootJob']:{state:'completed'},[target]:{reboots:1}});assert.equal(g.step,1);
 send({[target+'Reboot']:10,[target+'RebootJob']:{state:'cancelled'}});assert.equal(g.step,1);
 send({[target+'RebootJob']:{state:'completed'}});assert.equal(g.step,2);
 send({statusPage:false});assert.equal(g.complete,true);
});
test('S21 cancelled slot cannot credit activation and leaves original image',()=>{
 const {g,send}=driver('s21-slot-cancel');send({otaPage:true});send({realistic:true});send({otaMonitor:true});
 send({slotId:8});assert.equal(g.step,4);
 send({otaMonitor:false,slotIdJob:{state:'completed'}});assert.equal(g.step,4);
 send({slotIdJob:{state:'cancelled'},grove:{activeSlot:1,reboots:0}});assert.equal(g.step,4);
 send({grove:{activeSlot:0,reboots:0}});assert.equal(g.step,5);
 send({otaPage:false});assert.equal(g.complete,true);
});
test('S21 SD requires native Stop and back for current service',()=>{
 const {g,send}=driver('s21-sd-admin');send({sdPage:true});send({sdId:9,sdIdJob:{state:'running'}});
 send({sdConfirm:true});send({sdConfirm:false});send({sdConfirm:true});
 send({sdPage:false,sdConfirm:false,sdIdJob:{state:'cancelled'},sdStopped:4});assert.equal(g.complete,false);
 send({sdStopped:9});assert.equal(g.complete,true);
});
test('S07 old run and disconnection cannot credit detection',()=>{
 const g=queue2System.find(x=>x.id==='s07-detector').create();g.start({detectorRun:3});
 const s={tab:0,epoch:g.epoch,grove:{connected:true},detectorRunning:true,detectorRun:3,detectorCount:9,has:()=>true,bound:()=>true};
 g.accept(s);assert.equal(g.step,0);g.accept({...s,detectorRun:4});assert.equal(g.step,1);
 g.accept({...s,detectorRun:4,grove:{connected:false},detectorCount:10});assert.equal(g.step,0);
});
