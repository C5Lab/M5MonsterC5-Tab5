import {StepGuide,readUI} from './queue2-common.mjs';

const TARGET='02:20:77:00:00:01',MAC='02:20:77:02:00:01';
const observed=new WeakMap();
export function readWardrive(module,bridge){
 const ui=readUI(module),tab=ui.tab;
 // Stories target GROVE. Capture its baseline even when launched from another tab.
 const d=bridge.device??bridge,id='grove',w=d.wardrive(id),m=d.snapshot(id);
 const n=field=>module._emu_queue2_wardrive_state(0,field);
 const text=field=>module.UTF8ToString(module._emu_queue2_wardrive_text(0,field));
 let cache=observed.get(module);if(!cache){cache={};observed.set(module,cache);}
 const wpasecId=n(25);if(wpasecId)cache[0]=wpasecId;
 const run=n(1),job=d.job(run),files=d.files(id);
 return {tab,connected:m.connected,sd:m.sdPresent,page:!!n(0),run,jobState:job?.state,running:w.running,
  gps:w.gps.fix,gpsMissing:globalThis.document?.querySelector('#live-gps')?.value==='missing',distance:w.gps.distanceM,rows:w.rows,sessions:w.sessions,error:w.error||text(3),files,
  setup:!!n(2),applied:n(3),config:n(4),gpsDebug:!!n(5),gpsRunning:!!n(6),gpsSamples:n(7),nmea:text(4),
  homeEditor:!!n(8),homeCount:n(9),homeTarget:text(1),blackEditor:!!n(10),blackCount:n(11),blackTarget:text(2),
  homeConfirm:!!n(12),auto:!!n(13),wigle:!!n(14),wdgwars:!!n(15),handshakes:!!n(16),wardriveFiles:!!n(17),
  deleteConfirm:!!n(18),selected:n(19),uploadPopup:!!n(20),provider:n(21),home:!!n(22),loaded:!!n(23)||!!ui.bound('show_compromised_file_page.delete_btn')||ui.objects.some(o=>/No .*files found|Found \d+ .*file/i.test(o.text||'')),cleanup:!!n(24),
  confirmPath:text(0),selectedPaths:ui.objects.filter(o=>(o.state&1)&&o.binding.endsWith('/select')).map(o=>o.binding),copied:n(26),deleted:n(27),copyPath:text(5),deletePath:text(6),
  wpasec:d.job(cache[0]),realistic:globalThis.document?.querySelector('#timing')?.value==='1',transferActive:ui.has('Cancel'),uiText:ui.objects.map(o=>o.text||'').join('\n')};
}
const step=(title,instruction,accept)=>({title,instruction,accept});
function story(id,label,steps,extra={}){
 return {id,label,create:()=>new StepGuide({id,steps,completionTitle:label+' complete',
  completion:'The native controls and offline results agree. Files last for this demo session.',
  onContextChange:g=>{delete g.memory.sample;delete g.memory.wait;},guard:(s,g)=>{
   if(s.visible===false)return false;
   if(!s.connected){g.hint='Restore the GROVE module connection. Restart the guide after a cancelled recording.';return false;}
   if(g.memory.run&&['cancelled','failed'].includes(s.jobState)&&!(id==='s17-no-sd'&&s.error==='sd_missing')){
    g.baseline={...s};g.memory={};g.step=0;g.hint='Recording was cancelled. Start a fresh session.';return false;
   }
   return true;
  },...extra}),read:readWardrive};
}
const startRecording=()=>step('Start a fresh recording','On GROVE open Wardrive and press Start. Stop an older session first.',(s,g)=>{
 if(!s.page||!s.running||s.run<=(g.baseline.run||0))return false;
 g.memory.run=s.run;return true;
});
const observeRecording=()=>step('Watch GPS and networks','Wait for GPS fix, NEON-BAZAAR and a changing distance on the native Wardrive page.',(s,g)=>{
 if(!s.page||!s.running||s.run!==g.memory.run||!s.gps||!s.rows?.some(r=>r.bssid===TARGET)||!Number.isFinite(s.timeMs))return false;
 if(!g.memory.sample){g.memory.sample={time:s.timeMs,distance:s.distance||0};return false;}
 return s.timeMs-g.memory.sample.time>=2000&&s.distance>g.memory.sample.distance;
});
const saveRecording=()=>step('Stop and save this recording','Press the native Stop button. The saved session must contain the networks you just observed.',(s,g)=>{
 const file=s.sessions?.find(f=>f.id===g.memory.run&&f.wifiCount>0);
 if(s.running||!s.page||!file)return false;g.memory.path=file.path;return true;
});
const back=()=>step('Return to GROVE','Close any dialogs, then use the native Back arrow to return to GROVE.',s=>s.home&&!s.running);
const setup=()=>step('Open Wardrive Setup','On GROVE open Wardrive, then Setup.',s=>s.setup);
const closeSetup=()=>step('Close Setup','Close the Setup dialog.',s=>s.page&&!s.setup);
const addHome=()=>step('Add NEON-BAZAAR as Home','Open Home in Setup, Scan, choose NEON-BAZAAR (02:20:77:00:00:01), then Add. Remove an old entry first if needed.',(s,g)=>s.homeEditor&&s.homeCount>(g.baseline.homeCount||0)&&s.homeTarget===TARGET);
const armHome=()=>step('Arm offline Home upload','Close Home. Enable auto-upload and WiGLE in Setup, then Apply and close Setup.',s=>s.page&&!s.setup&&s.auto&&s.wigle);
const askHome=()=>step('Wait for the Home decision','Start a fresh Wardrive run and wait for the NEON-BAZAAR Home confirmation.',(s,g)=>{
 if(s.run<=(g.baseline.run||0)||!s.running||!s.homeConfirm||s.homeTarget!==TARGET)return false;
 g.memory.run=s.run;return true;
});
const providerSteps=provider=>[
 step('Open '+(provider==='wigle'?'WiGLE':'WDGWars')+' upload','Open Upload and choose '+(provider==='wigle'?'WiGLE':'WDGWars')+'.',(s)=>s.uploadPopup&&s.provider===(provider==='wigle'?1:2)),
 step('Sync the new recording','Press Sync. Wait for this new session to show a successful simulated upload; no network request is sent.',(s,g)=>s.uploadPopup&&s.provider===(provider==='wigle'?1:2)&&s.sessions?.some(f=>f.id===g.memory.run&&f.uploadStatus?.[provider]==='done')),
 back()
];
const entries=[
 story('s17-record','Wardrive: record and save',[startRecording(),observeRecording(),saveRecording(),back()]),
 story('s17-no-gps','Wardrive: missing GPS',[step('Disable the demo GPS','Open Demo module conditions and set Live GROVE GPS to No GPS fix before starting a fresh run.',s=>s.gpsMissing&&!s.running),startRecording(),step('Observe missing GPS','Watch the waiting state; there must be no invented networks or track.',(s,g)=>{
  if(!s.page||!s.running||s.gps||s.rows?.length||!s.uiText?.includes('Waiting for GPS'))return false;
  if(g.memory.wait===undefined)g.memory.wait=s.timeMs;return s.timeMs-g.memory.wait>=2000;
 }),step('Restore GPS','Restore the GPS demo condition and observe actual scenario networks.',s=>s.page&&s.running&&s.gps&&s.rows?.some(r=>r.bssid===TARGET)),saveRecording(),back()]),
 story('s17-no-sd','Wardrive: missing SD',[startRecording(),observeRecording(),step('Stop with missing SD','Under Demo module conditions set Live GROVE SD to SD missing, then Stop. Confirm sd_missing; no session file may appear.',(s,g)=>!s.running&&s.error==='sd_missing'&&!s.sessions?.some(f=>f.id===g.memory.run)),step('Restore SD','Set Live GROVE SD back to SD available before leaving.',s=>s.sd),back()]),
 story('s17-settings','Wardrive: settings',[setup(),step('Change and Apply','Change Trace or a Wardrive option, then Apply. A new Apply and changed configuration are required.',(s,g)=>s.setup&&s.applied>(g.baseline.applied||0)&&s.config!==g.baseline.config),closeSetup()]),
 story('s17-gps-debug','Wardrive: GPS debug',[setup(),step('Open GPS Debug','Open GPS Debug, then press Start.',(s,g)=>{if(!s.gpsDebug||!s.gpsRunning)return false;g.memory.samples=s.gpsSamples;return true;}),step('Read NMEA','Wait for fresh $GPGGA NMEA lines on the native GPS Debug panel.',(s,g)=>s.gpsDebug&&s.gpsRunning&&s.gpsSamples>g.memory.samples&&s.nmea?.includes('$GPGGA')),step('Stop debug','Press Stop in GPS Debug. The panel must remain open.',s=>s.gpsDebug&&!s.gpsRunning),step('Close debug and Setup','Close GPS Debug, then Setup.',s=>s.page&&!s.gpsDebug&&!s.setup)]),
 story('s17-home','Wardrive: Home network',[setup(),addHome(),step('Close Home','Close Home, keeping the new entry.',s=>s.setup&&!s.homeEditor&&s.homeTarget===TARGET),closeSetup()]),
 story('s17-blacklist','Wardrive: MAC blacklist',[setup(),step('Blacklist NEURODECK-07','Open MAC Blacklist, Scan and select 02:20:77:02:00:01. Close the picker after adding it.',(s,g)=>s.blackEditor&&s.blackCount>(g.baseline.blackCount||0)&&s.blackTarget===MAC),closeSetup(),startRecording(),step('Check the filtered results','Wait for Bluetooth rows: other devices must appear while NEURODECK-07 stays absent.',s=>s.page&&s.running&&s.rows?.some(r=>r.kind==='bluetooth')&&!s.rows.some(r=>r.bssid===MAC)),saveRecording(),back()]),
 ...['wigle','wdgwars'].map(p=>story('s17-'+p,'Wardrive: '+(p==='wigle'?'WiGLE':'WDGWars'),[startRecording(),observeRecording(),saveRecording(),...providerSteps(p)])),
 story('s17-home-keep','Wardrive: keep recording at Home',[setup(),addHome(),armHome(),askHome(),step('Keep recording','Choose No in the Home confirmation. Recording must remain active.',s=>s.page&&!s.homeConfirm&&s.running),observeRecording(),saveRecording(),back()]),
 story('s17-home-upload','Wardrive: upload at Home',[setup(),addHome(),armHome(),askHome(),step('Stop and upload','Choose Yes. The current run must save and its WiGLE upload must finish.',(s,g)=>!s.homeConfirm&&!s.running&&s.sessions?.some(f=>f.id===g.memory.run&&f.uploadStatus?.wigle==='done')),back()])
];

function fileStory(kind,action){
 const wardrive=kind==='wardrive',page=s=>wardrive?s.wardriveFiles:s.handshakes;
 const files=s=>wardrive?s.sessions||[]:(s.files||[]).filter(f=>/\.pcap(?:ng)?$/i.test(f.path));
 const hasTarget=(s,g)=>files(s).some(f=>f.path===g.memory.path);
 const steps=wardrive&&action!=='empty'?[startRecording(),observeRecording(),saveRecording()]:[];
 steps.push(step('Open '+(wardrive?'Wardrive Files':'Handshakes'),action==='empty'?'Start from a reset demo. On GROVE open Compromised Data → '+(wardrive?'Wardrive Files':'Handshakes')+'.':wardrive?'Use Back, then Compromised Data → Wardrive Files.':'Load PCAP examples if needed using the browser control, then return to GROVE → Compromised Data → Handshakes. Use example-http-dns-icmp.pcap.',(s,g)=>{
  if(!page(s)||!s.loaded)return false;
  if(action==='empty')return files(s).length===0;
  if(wardrive){g.memory.peers=files(s).filter(f=>f.path!==g.memory.path).map(f=>f.path);return hasTarget(s,g);}
  const target=files(s).find(f=>f.path.endsWith('/example-http-dns-icmp.pcap'));
  if(!target)return false;g.memory.path=target.path;g.memory.peers=files(s).filter(f=>f.path!==target.path).map(f=>f.path);return true;
 }));
 if(action==='empty')steps.push(step('Return from the empty list','Use the native Back arrow. No files should have been created.',s=>!page(s)&&files(s).length===0));
 else if(action==='delete')steps.push(
  step('Ask to delete the target','Select the new recording and Delete, or use the HTTP/DNS fixture trash button.',(s,g)=>s.deleteConfirm&&s.confirmPath===g.memory.path&&hasTarget(s,g)),
  step('Cancel deletion','Choose No. The exact file must still exist.',(s,g)=>page(s)&&!s.deleteConfirm&&hasTarget(s,g)),
  step('Confirm the same target again','Open Delete for the same file again.',(s,g)=>s.deleteConfirm&&s.confirmPath===g.memory.path&&hasTarget(s,g)),
  step('Delete and verify','Choose Yes. Only the requested target should disappear.',(s,g)=>!s.deleteConfirm&&s.deleted>(g.baseline.deleted||0)&&s.deletePath===g.memory.path&&!hasTarget(s,g)&&(g.memory.peers||[]).every(p=>files(s).some(f=>f.path===p))),
  step('Close the result','Close the deletion result and return from the file list.',s=>!page(s)&&!s.cleanup)
 );
 else if(wardrive)steps.push(step('Select this recording','Tick the checkbox for the new session file, then return through GROVE → Wardrive → Upload → WiGLE.',(s,g)=>s.wardriveFiles&&s.selected===1&&s.selectedPaths?.some(p=>p.includes(g.memory.path+'/select'))),...providerSteps('wigle'));
 else steps.push(step('Copy the HTTP/DNS fixture','Use Copy on example-http-dns-icmp.pcap. Wait for the native transfer result.',(s,g)=>s.copied>(g.baseline.copied||0)&&s.copyPath===g.memory.path&&hasTarget(s,g)),
  step('Close the transfer and return','Close the transfer result and use the native file-list Back arrow.',s=>!page(s)&&!s.transferActive));
 return story('s16-'+kind+(action==='normal'?'':'-'+action),(wardrive?'Wardrive Files':'Handshakes')+': '+action,steps);
}
entries.push(...['handshakes','wardrive'].flatMap(k=>['normal','delete','empty'].map(a=>fileStory(k,a))));
entries.push(story('s16-handshakes-upload','Handshakes: demo upload and cancel',[
 step('Choose realistic timing','Set Simulation speed to Realistic · 1×. You will close the first upload before it finishes, then retry it.',s=>s.realistic),
 step('Open Handshakes','On GROVE open Compromised Data → Handshakes.',s=>s.handshakes&&s.loaded),
 step('Start a demo WPA-SEC upload','Choose Send to wpa-sec, select NEON-BAZAAR and Connect. This service simulates two fixed files, offline-demo-1.pcap and offline-demo-2.pcap; no user file is transmitted.',(s,g)=>{
  const j=s.wpasec;if(j?.tool!=='wpasec'||j.state!=='running'||j.id<=(g.baseline.wpasec?.id||0))return false;g.memory.upload=j.id;return true;
 }),
 step('Cancel this upload','Close the native upload popup before completion. Its module job must be cancelled.',(s,g)=>{
  if(s.wpasec?.id!==g.memory.upload)return false;
  if(s.wpasec.state==='completed'){g.step--;g.hint='The upload finished before cancellation. Close its popup and start another upload, then close it immediately.';return false;}
  return s.wpasec.state==='cancelled'&&s.handshakes;
 }),
 step('Retry the demo upload','Send to wpa-sec again, choose NEON-BAZAAR and Connect.',(s,g)=>{
  if(s.wpasec?.tool!=='wpasec'||s.wpasec.id<=g.memory.upload||s.wpasec.state!=='running')return false;g.memory.upload=s.wpasec.id;return true;
 }),
 step('Verify both demo results','Wait for Uploaded: 2 and both offline demo filenames. No file is sent outside the browser.',(s,g)=>{
  const j=s.wpasec;return j?.id===g.memory.upload&&j.state==='completed'&&j.result?.simulated&&j.result.uploaded===2&&
   ['offline-demo-1.pcap','offline-demo-2.pcap'].every(name=>j.result.files?.some(f=>f.name===name&&f.status==='uploaded'))&&s.uiText?.includes('Simulated upload complete');
 }),step('Close and return','Close the upload popup and use Back from Handshakes.',s=>!s.handshakes&&!s.uiText?.includes('Simulated upload complete'))
]));
export const queue2Wardrive=entries;
