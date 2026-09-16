import test from 'node:test';
import assert from 'node:assert/strict';
import {WiFiGuide,wifiStory,wifiDemoSeed} from '../tools/ui_emulator/web/wifi-stories.mjs';
const a='02:20:77:00:00:01',b='02:20:77:00:00:03',hidden='02:20:77:00:00:02';
function setup(kind='scan',variant='normal'){
 const g=new WiFiGuide(wifiStory(kind,variant));g.start({scanId:5,rejections:0});
 let s={epoch:1,tab:0,scanId:6,scanState:2,scanVisible:true,count:12,selected:[],rejections:0,redTeam:true};
 return {g,send(extra={}){s={...s,...extra};if(Object.hasOwn(extra,'rejections')){s.rejectedSelected=[...s.selected];s.rejectedCount=s.selected.length;}g.accept(s);}};
}
for(const variant of ['normal','hidden','empty','cancel'])test(`scan story ${variant}`,()=>{
 const {g,send}=setup('scan',variant);send({scanState:1});
 if(variant==='cancel'){send({scanState:3});send({scanId:7,scanState:1});}
 send({scanState:2,count:variant==='empty'?0:12});
 if(variant!=='empty'){
  send({selected:[a]});send({selected:[a,b]});send({selected:[]});
  if(variant==='hidden'){send({selected:[hidden]});send({selected:[]});}
 }
 send({scanVisible:false,homeVisible:true});assert.equal(g.complete,true);
});
test('radar requires rejected invalid selections, correct target, live samples and native stop',()=>{
 const {g,send}=setup('radar');send();send();send({rejections:1});
 send({selected:[a,b]});send({rejections:2});send({selected:[a]});
 send({radarId:10,radarState:1,radarVisible:true,radarTarget:a,rssi:-50});
 for(let i=0;i<=4;i++)send({timeMs:i*500,rssi:-50-i});
 send({radarState:3,radarVisible:false});assert.equal(g.complete,false);
});
test('stale scans, wrong module, stale epoch, cancelled scans and restart cannot supply evidence',()=>{
 const {g,send}=setup();send({scanId:5});assert.equal(g.step,0);
 send({scanId:6,tab:2});assert.equal(g.step,0);
 send({tab:0,epoch:0});assert.equal(g.step,0);
 send({epoch:1,scanState:1});send({scanState:3});assert.equal(g.step,0);
 send({scanId:7,scanState:2});send();assert.equal(g.step,2);
 g.start({scanId:7});send({epoch:2});assert.equal(g.step,0);
});
test('demo variants do not mutate the original seed',()=>{
 const seed={settings:{scan_time_ms:1000},networks:[{bssid:hidden,ssid:'GHOST-LINE'}]};
 assert.equal(wifiDemoSeed(seed,'hidden').networks[0].ssid,'');
 assert.equal(seed.networks[0].ssid,'GHOST-LINE');assert.equal(wifiDemoSeed(seed,'empty').networks.length,0);
 assert.equal(wifiDemoSeed(seed,'cancel').settings.scan_time_ms,30000);
});

function radar(){
 const {g,send}=setup('radar');send();send();send({rejections:1});
 send({selected:[a,b]});send({rejections:2});send({selected:[a]});
 send({radarId:10,radarState:1,radarVisible:true,radarTarget:a,rssi:-50});
 return {g,send};
}
test('Radar completes only after an observed live signal and completed native return',()=>{
 const {g,send}=radar();
 for(let i=0;i<=4;i++)send({timeMs:i*500,rssi:-50-i});
 assert.equal(g.story.steps[g.step].key,'stop');
 send({radarState:2,radarVisible:false,scanVisible:true});assert.equal(g.complete,true);
});
test('Radar does not credit hidden time, wrong targets, loss of signal or previous jobs',()=>{
 const {g,send}=radar();
 g.setContext({tab:0,visible:false});
 for(let i=0;i<=4;i++)send({timeMs:i*500,rssi:-50-i});
 assert.equal(g.story.steps[g.step].key,'watch');
 g.setContext({tab:0,visible:true});
 send({timeMs:3000,rssi:-50});send({timeMs:3500,rssi:-100});assert.equal(g.samples.size,0);
 send({timeMs:4000,rssi:-50,radarTarget:b});assert.equal(g.samples.size,0);
 send({radarId:9,radarTarget:a});assert.equal(g.story.steps[g.step].key,'radar');
 send({radarId:10});assert.equal(g.story.steps[g.step].key,'radar');
 send({radarId:11});assert.equal(g.story.steps[g.step].key,'watch');
});

test('selection changes cannot reuse a rejection from another selection',()=>{
 const {g,send}=setup('radar');send();send();
 send({selected:[a,b],rejections:1});send({selected:[]});
 assert.equal(g.story.steps[g.step].key,'reject-none');
 send({rejections:2});send({selected:[a,b]});
 assert.equal(g.story.steps[g.step].key,'reject-multi');
 send({selected:[],rejections:3});send({selected:[a,b]});
 assert.equal(g.story.steps[g.step].key,'reject-multi');
 send({rejections:4});assert.equal(g.story.steps[g.step].key,'single');
});
