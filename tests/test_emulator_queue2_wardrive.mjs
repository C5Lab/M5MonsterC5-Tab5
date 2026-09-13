import test from 'node:test';
import assert from 'node:assert/strict';
const {queue2Wardrive,readWardrive}=await import('../tools/ui_emulator/web/queue2-wardrive.mjs').catch(()=>({queue2Wardrive:[]}));
const entry=id=>{const e=queue2Wardrive.find(e=>e.id===id);assert.ok(e,`Missing ${id} story`);return e;};
const sample={tab:0,connected:true,page:true,run:0,running:false,sessions:[],files:[],gps:false,rows:[],home:false};
function begin(id,s=sample){const g=entry(id).create();g.start(s);return g;}
function send(g,s){g.accept({...sample,...s,epoch:g.epoch});}

test('Wardrive requires a fresh run, visible target module, observations and its own saved file',()=>{
 const g=begin('s17-record',{...sample,run:4});
 send(g,{run:4,running:true});assert.equal(g.step,0);
 send(g,{tab:2,run:5,running:true});assert.equal(g.step,0);
 send(g,{run:5,running:true});assert.equal(g.step,1);
 send(g,{run:5,running:true,gps:true,rows:[{bssid:'02:20:77:00:00:01'}],timeMs:0});
 send(g,{run:5,running:true,gps:true,rows:[{bssid:'02:20:77:00:00:01'}],timeMs:2200,distance:20});
 assert.equal(g.step,2);
 send(g,{run:5,sessions:[{id:4,path:'old'}]});assert.equal(g.complete,false);
 send(g,{run:5,sessions:[{id:5,path:'fresh',wifiCount:1}]});assert.equal(g.step,3);
 send(g,{run:5,page:false,home:true,sessions:[{id:5,path:'fresh',wifiCount:1}]});assert.equal(g.complete,true);
});
test('restart, stale epochs and hidden context do not advance recording',()=>{
 const g=begin('s17-record');const old=g.epoch;g.start({...sample,run:2});
 g.accept({...sample,run:3,running:true,epoch:old});assert.equal(g.step,0);
 g.setContext({tab:0,visible:false});send(g,{run:3,running:true});assert.equal(g.step,0);
 g.setContext({tab:0,visible:true});send(g,{run:3,running:true});assert.equal(g.step,1);
 send(g,{run:3,running:false,jobState:'cancelled'});assert.equal(g.step,0);
});
test('all Wardrive and file variants have observable predicates',()=>{
 for(const id of ['s17-record','s17-no-gps','s17-no-sd','s17-settings','s17-gps-debug','s17-home','s17-blacklist','s17-wigle','s17-wdgwars','s17-home-keep','s17-home-upload','s16-handshakes','s16-handshakes-delete','s16-handshakes-empty','s16-wardrive','s16-wardrive-delete','s16-wardrive-empty','s16-handshakes-upload']){
  const g=begin(id);for(const step of g.story.steps)assert.equal(typeof step.accept,'function');
  for(let i=0;i<20;i++)send(g,{});assert.equal(g.complete,false,id+' completed without evidence');
 }
});

const fixture={path:'/sdcard/lab/pcaps/grove/example-http-dns-icmp.pcap'};
const peer={path:'/sdcard/lab/pcaps/grove/example-dns-icmp.pcap'};
test('copy accepts an existing fixture but requires a fresh successful native copy of that exact file',()=>{
 const g=begin('s16-handshakes',{...sample,files:[fixture,peer],copied:3});
 send(g,{handshakes:true,loaded:true,files:[fixture,peer]});assert.equal(g.step,1);
 send(g,{copied:3,copyPath:fixture.path,files:[fixture,peer]});assert.equal(g.step,1);
 send(g,{copied:4,copyPath:peer.path,files:[fixture,peer]});assert.equal(g.step,1);
 send(g,{copied:4,copyPath:fixture.path,files:[fixture,peer]});assert.equal(g.step,2);
 send(g,{handshakes:false,transferActive:false});assert.equal(g.complete,true);
});
test('delete requires both native confirmation decisions and preserves unrelated files',()=>{
 const g=begin('s16-handshakes-delete',{...sample,files:[fixture,peer],deleted:4});
 send(g,{handshakes:true,loaded:true,files:[fixture,peer]});
 send(g,{deleteConfirm:true,confirmPath:peer.path,files:[fixture,peer]});assert.equal(g.step,1);
 send(g,{deleteConfirm:true,confirmPath:fixture.path,files:[fixture,peer]});assert.equal(g.step,2);
 send(g,{handshakes:true,deleteConfirm:false,files:[fixture,peer]});assert.equal(g.step,3);
 send(g,{deleteConfirm:true,confirmPath:fixture.path,files:[fixture,peer]});assert.equal(g.step,4);
 send(g,{deleted:5,deletePath:fixture.path,files:[]});assert.equal(g.step,4,'Deleting peers is not completion');
 send(g,{deleted:5,deletePath:fixture.path,files:[peer]});assert.equal(g.step,5);
 send(g,{handshakes:false,cleanup:false,files:[peer]});assert.equal(g.complete,true);
});
test('GPS failure setup precedes the fresh run; recovery requires native networks',()=>{
 const g=begin('s17-no-gps');send(g,{running:true,run:1,gps:false});assert.equal(g.step,0);
 send(g,{gpsMissing:true});send(g,{gpsMissing:true,running:true,run:2});assert.equal(g.step,2);
 send(g,{run:2,running:true,rows:[],gps:false,uiText:'Waiting for GPS fix',timeMs:100});
 send(g,{run:2,running:true,rows:[],gps:false,uiText:'Waiting for GPS fix',timeMs:2200});assert.equal(g.step,3);
 send(g,{run:2,running:true,gps:true,rows:[]});assert.equal(g.step,3);
 send(g,{run:2,running:true,gps:true,rows:[{bssid:'02:20:77:00:00:01'}]});assert.equal(g.step,4);
});
test('Home target and provider must match before confirming a fresh session',()=>{
 const g=begin('s17-home-upload',{...sample,homeCount:0,run:4});
 send(g,{setup:true});
 send(g,{homeEditor:true,homeCount:1,homeTarget:'wrong'});assert.equal(g.step,1);
 send(g,{homeEditor:true,homeCount:1,homeTarget:'02:20:77:00:00:01'});
 send(g,{setup:false,auto:true,wigle:false});assert.equal(g.step,2);
 send(g,{setup:false,auto:true,wigle:true});
 send(g,{run:4,running:true,homeConfirm:true,homeTarget:'02:20:77:00:00:01'});assert.equal(g.step,3);
 send(g,{run:5,running:true,homeConfirm:true,homeTarget:'02:20:77:00:00:01'});
 send(g,{run:5,sessions:[{id:4,uploadStatus:{wigle:'done'}}]});assert.equal(g.step,4);
 send(g,{run:5,sessions:[{id:5,uploadStatus:{wigle:'done'}}]});assert.equal(g.step,5);
 send(g,{run:5,home:true});assert.equal(g.complete,true);
});
test('WPA-SEC uses a new service job and verifies the two fixed demo results after cancel/retry',()=>{
 const g=begin('s16-handshakes-upload',{...sample,wpasec:{id:3}});
 send(g,{realistic:true});
 send(g,{handshakes:true,loaded:true});
 send(g,{wpasec:{id:3,tool:'wpasec',state:'running'}});assert.equal(g.step,2);
 send(g,{wpasec:{id:4,tool:'wpasec',state:'running'}});
 send(g,{handshakes:true,wpasec:{id:3,state:'cancelled'}});assert.equal(g.step,3);
 send(g,{handshakes:true,wpasec:{id:4,state:'cancelled'}});
 send(g,{wpasec:{id:5,tool:'wpasec',state:'running'}});
 send(g,{wpasec:{id:5,state:'completed',result:{simulated:true,uploaded:2,files:[]}},uiText:'Simulated upload complete'});assert.equal(g.step,5);
 send(g,{wpasec:{id:5,state:'completed',result:{simulated:true,uploaded:2,files:[{name:'offline-demo-1.pcap',status:'uploaded'},{name:'offline-demo-2.pcap',status:'uploaded'}]}},uiText:'Simulated upload complete'});
 send(g,{handshakes:false,uiText:''});assert.equal(g.complete,true);
});
test('settings require a changed value and a new Apply, not a previously applied label',()=>{
 const g=begin('s17-settings',{...sample,applied:8,config:10});
 send(g,{setup:true,applied:8,config:10});assert.equal(g.step,1);
 send(g,{setup:true,applied:8,config:10});assert.equal(g.step,1);
 send(g,{setup:true,applied:9,config:11});assert.equal(g.step,2);
 send(g,{setup:false,applied:9,config:11});assert.equal(g.complete,true);
});
test('launching on INTERNAL still captures the existing GROVE run as the freshness floor',()=>{
 const prior=globalThis.emulatorObjects;globalThis.emulatorObjects=[];
 const module={_emu_inspect(){},_emu_current_tab:()=>3,UTF8ToString:s=>s,
  _emu_queue2_wardrive_state:(tab,field)=>{assert.equal(tab,0);return field===1?17:0;},
  _emu_queue2_wardrive_text:(tab)=>{assert.equal(tab,0);return '';}};
 const device={wardrive:id=>{assert.equal(id,'grove');return{running:true,gps:{fix:true},rows:[],sessions:[]};},
  snapshot:()=>({connected:true,sdPresent:true}),job:id=>id?{id,state:'running'}:null,files:()=>[]};
 try{
  const baseline=readWardrive(module,device);assert.equal(baseline.tab,3);assert.equal(baseline.run,17);
  const g=begin('s17-record',baseline);send(g,{run:17,running:true});assert.equal(g.step,0);
  send(g,{run:18,running:true});assert.equal(g.step,1);
 }finally{globalThis.emulatorObjects=prior;}
});
