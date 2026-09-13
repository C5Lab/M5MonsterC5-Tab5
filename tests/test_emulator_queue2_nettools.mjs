import test from 'node:test';
import assert from 'node:assert/strict';
import {queue2Nettools} from '../tools/ui_emulator/web/queue2-nettools.mjs';
for (const entry of queue2Nettools) test(`${entry.id}: stale, hidden, module and restart isolation`,()=>{
 const g=entry.create();g.start({run:7,connectId:6,hostsId:5});
 const s={tab:0,page:true,epoch:g.epoch,run:8,connectId:8,hostsId:9,target:'AFTERLIFE-GUEST',targetId:'guest-id',apSsid:'SIMULATED-GATEWAY',entry:entry.id.endsWith('scan')?1:2,moduleConnected:true};
 g.accept({...s,epoch:g.epoch-1});assert.equal(g.step,0);
 g.setContext({tab:2,visible:true});g.accept(s);assert.equal(g.step,0);
 g.setContext({tab:0,visible:false});g.accept(s);assert.equal(g.step,0);
 g.setContext({tab:0,visible:true});g.accept({...s,moduleConnected:false});assert.equal(g.step,0);
 g.accept(s);assert.equal(g.step,1);
 const old=g.epoch;g.start(s);g.accept({...s,epoch:old});assert.equal(g.step,0);
 g.leave();assert.equal(g.active,false);
});
for(const entry of queue2Nettools)test(`${entry.id}: complete only with current native outcomes`,()=>{
 const g=entry.create();g.start({run:7,connectId:7,hostsId:7});
 let s={tab:0,epoch:g.epoch,page:true,scan:false,home:false,moduleConnected:true,target:'AFTERLIFE-GUEST',targetId:'guest-id',apSsid:'SIMULATED-GATEWAY',entry:entry.id.endsWith('scan')?1:2,connectId:8,hostsId:9,connected:true,hosts:2,connectJob:{state:'completed'},hostsJob:{state:'completed'},run:10,running:true,packets:10,job:{state:'running',options:{networkIds:['guest-id'],targetMac:'02:00:00:00:00:10',upstreamSsid:'AFTERLIFE-GUEST'},result:{packets:10,bytes:100,clients:[{}]}}};
 while(!g.complete){
  const key=g.story.steps[g.step].key, before=g.step;
  if(key==='config'){g.accept({...s,target:'WRONG',entry:0});assert.equal(g.step,before);}
  if(key==='connect'){g.accept({...s,connectId:7});assert.equal(g.step,before);}
  if(key==='hosts'){g.accept({...s,hosts:0});assert.equal(g.step,before);}
  if(key==='run'){g.accept({...s,run:7});assert.equal(g.step,before);g.accept({...s,target:'GHOST-LINE',job:{...s.job,options:{...s.job.options,networkIds:['net-2']}}});assert.equal(g.step,before);if(entry.id.startsWith('queue2-gitm')){g.accept({...s,apSsid:'WRONG-AP'});assert.equal(g.step,before);}if(entry.id==='queue2-mitm'){g.accept({...s,job:{...s.job,options:{networkIds:['wrong-id']}}});assert.equal(g.step,before);}} 
  if(key==='activity'){g.accept({...s,timeMs:0});g.accept({...s,timeMs:2000,run:99,packets:30});assert.equal(g.step,before);g.accept({...s,timeMs:2000});s={...s,timeMs:4000,packets:30};}
  if(key.startsWith('scan-')){const level=key.slice(5);s={...s,run:s.run+1,resultPopup:true,stopped:true,running:false,job:{state:'completed',options:{level,target:level==='heavy'?'all':'192.0.2.10'},result:{text:'80/tcp open HTTP'}}};g.accept({...s,job:{...s.job,options:{...s.job.options,target:'192.0.2.20'}}});assert.equal(g.step,before);}
  if(key.startsWith('close-')||key==='stop')s={...s,resultPopup:false,stopped:true,running:false};
  if(key==='ask'||key==='ask-again')s={...s,confirm:true};
  if(key==='keep')s={...s,confirm:false};
  if(key==='file'||key==='exit'){s={...s,stopped:true,running:false,file:true};g.accept({...s,file:false});assert.equal(g.step,before);}
  if(key==='exit')s={...s,page:false,home:true,confirm:false};
  if(key==='back')s={...s,page:false,scan:true};
  g.accept(s);assert.equal(g.step,before+1,key);
 }
 assert.equal(g.complete,true);
});
