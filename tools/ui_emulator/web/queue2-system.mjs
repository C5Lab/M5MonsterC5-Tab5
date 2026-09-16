import {StepGuide,readUI} from './queue2-common.mjs';
function read(module,bridge){module._emu_queue2_system_state();const s={...globalThis.emulatorQueue2SystemEvidence,...readUI(module)};
 const d=bridge.device;s.grove=d.systemStatus('grove');s.mbus=d.systemStatus('mbus');
 for(const key of ['updateId','slotId','sdId','groveReboot','mbusReboot'])s[key+'Job']=s[key]?d.job(s[key]):null;
 s.realistic=document.querySelector('#timing').value==='1';return s;}
const step=(title,instruction,accept)=>({title,instruction,accept});
function entry(id,label,tab,steps,extra={}){const story={id,label,tab,steps,completionTitle:label+' complete',completion:'The native workflow is finished. Use Back to stories to choose another local demo.',...extra};return{id,label,read,create:()=>new StepGuide(story)};}
function detector(anti){const prefix=anti?'anti':'detector',label=anti?'Anti-Surv':'Deauth Detector',tile=anti?'Anti-Surv':'Deauth\nDetector';
 const running=s=>s[prefix+'Running'],run=s=>s[prefix+'Run'],count=s=>s[prefix+'Events']??s[prefix+'Count'];
 return entry('s07-'+prefix,'S07 · '+label,0,[
  step('Start '+label,'On GROVE, open '+label+' and press Start. If already running, Stop and Start again.',(s,g)=>{if(!s.grove.connected||!running(s)||run(s)<=g.baseline[prefix+'Run'])return false;g.memory.run=run(s);g.memory.count=count(s);return true;}),
  step('Read new detector events','Wait for new synthetic detections on the native page.',(s,g)=>running(s)&&run(s)===g.memory.run&&count(s)>g.memory.count&&s.has(anti?'FOLLOWER':'NEON-BAZAAR',true)),
  step('Stop detection','Use the native Stop button, keeping this page open.',(s,g)=>{if(running(s)||run(s)!==g.memory.run||!!!s.bound(anti?'show_antisurv_page.back_btn':'show_deauth_detector_page.back_btn'))return false;g.memory.frozen=count(s);g.memory.since=s.timeMs;return true;}),
  step('Check stopped results','Keep this stopped page visible for two seconds. No new events should arrive.',(s,g)=>!running(s)&&!!s.bound(anti?'show_antisurv_page.back_btn':'show_deauth_detector_page.back_btn')&&count(s)===g.memory.frozen&&s.timeMs-g.memory.since>=2000),
  step('Restart detection','Press Start again. Anti-Surv clears its previous rows on Start; Deauth Detector retains its history.',(s,g)=>{if(!running(s)||run(s)<=g.memory.run)return false;g.memory.run=run(s);g.memory.count=count(s);return true;}),
  step('Read the restarted session','Wait for a new event belonging to this run.',(s,g)=>running(s)&&run(s)===g.memory.run&&count(s)>g.memory.count),
  step('Leave and stop detection','Use the native Back arrow. Returning to the GROVE tiles stops this session.',(s,g)=>!running(s)&&run(s)===g.memory.run&&s.has(tile)),
 ],{onContextChange(g){if(g.step===3)g.memory.since=Infinity;},guard(s,g){if(g.step===3&&g.memory.since===Infinity)g.memory.since=s.timeMs;if(g.step>0&&!s.grove.connected){g.step=0;g.baseline[prefix+'Run']=run(s);g.hint='Restore the connection, then Stop and Start a fresh detector session.';return false;}return true;}});
}
function reboot(which){const key=which+'Reboot';return entry('s21-reboot-'+which,'S21 · Reboot '+which.toUpperCase(),3,[
 step('Open Module Status','Open INTERNAL → Module Status.',s=>s.statusPage),
 step('Reboot '+which.toUpperCase(),'Press Simulate reboot on the '+which.toUpperCase()+' card and wait for completion.',(s,g)=>s.statusPage&&s[key]>g.baseline[key]&&s[key+'Job']?.state==='completed'&&s[which].reboots>g.baseline[which].reboots),
 step('Return to INTERNAL','Use the native Back button on Module Status.',s=>!s.statusPage&&s.has('Settings')&&s.has('Ad Hoc\nPortal & Karma'))]);}
const otaOpen=()=>step('Open Monster OTA','Open INTERNAL → Settings → Monster OTA.',s=>s.otaPage);
const otaClose=()=>step('Close the OTA monitor','Press Close to return to the OTA form.',s=>s.otaPage&&!s.otaMonitor);
const otaBack=()=>step('Return to Settings','Use the native Back arrow on the OTA form.',s=>!s.otaPage&&!!s.bound('/Monster OTA'));
function update(cancel){return entry('s21-ota-'+(cancel?'cancel':'update'),'S21 · OTA '+(cancel?'cancel':'info, releases and update'),3,[otaOpen(),
 ...(cancel?[step('Use realistic timing','Select Realistic in the browser Simulation speed control before starting.',s=>s.realistic)]:[
 step('Read device info','Press Device Info to inspect the simulated board and its slots.',s=>s.otaMonitor&&s.has('Slot ota_0',true)),otaClose(),
 step('Read available releases','Press List Versions to view the offline main and dev releases.',s=>s.otaMonitor&&s.has('Offline demo releases',true)),otaClose()]),
 step('Start a fresh OTA operation','Press Download & Flash. This updates only the offline simulated firmware.',(s,g)=>{if(!s.otaMonitor||s.updateId<=g.baseline.updateId)return false;g.memory.job=s.updateId;g.memory.version=g.baseline.grove.version;return true;}),
 step(cancel?'Cancel the OTA operation':'Wait for the updated image',cancel?'Press Cancel simulated update inside the native monitor. The running version must stay unchanged.':'Wait until the native monitor reports completed.',(s,g)=>s.updateId===g.memory.job&&(cancel?!s.otaMonitor&&s.updateIdJob?.state==='cancelled'&&s.grove.version===g.memory.version:s.otaMonitor&&s.updateIdJob?.state==='completed'&&s.grove.reboots>g.baseline.grove.reboots&&s.grove.version!==g.memory.version)),
 ...(cancel?[]:[otaClose()]),otaBack()]);}
function slot(cancel){return entry('s21-slot-'+(cancel?'cancel':'activate'),'S21 · Slot '+(cancel?'cancel':'activation'),3,[otaOpen(),
 ...(cancel?[step('Use realistic timing','Select Realistic in the browser Simulation speed control.',s=>s.realistic)]:[]),
 step('Inspect boot slots','Press Device Info. Inspect the current slot before choosing the other slot.',s=>s.otaMonitor&&s.has('Slot ota_0',true)),
 step('Activate the other slot','Press Activate on the slot that is not currently running.',(s,g)=>{if(s.slotId<=g.baseline.slotId)return false;g.memory.job=s.slotId;return true;}),
 step(cancel?'Cancel slot activation':'Wait for slot activation',cancel?'Close the native monitor while activation is pending. No slot or reboot change should be applied.':'Wait for the simulated reboot to activate the other slot.',(s,g)=>s.slotId===g.memory.job&&(cancel?!s.otaMonitor&&s.slotIdJob?.state==='cancelled'&&s.grove.activeSlot===g.baseline.grove.activeSlot&&s.grove.reboots===g.baseline.grove.reboots:s.otaMonitor&&s.slotIdJob?.state==='completed'&&s.grove.activeSlot!==g.baseline.grove.activeSlot)),
 ...(cancel?[]:[otaClose()]),otaBack()]);}
function sd(){return entry('s21-sd-admin','S21 · SD Admin',3,[
 step('Open Monster SD Admin','Open INTERNAL → Settings → Monster SD Admin.',s=>s.sdPage),
 step('Start simulated SD Admin','Press Quick Start. No AP or HTTP server is created by this offline demo.',(s,g)=>{if(!s.sdPage||s.sdId<=g.baseline.sdId||s.sdIdJob?.state!=='running')return false;g.memory.job=s.sdId;return true;}),
 step('Try leaving SD Admin','Press the native Back arrow.',s=>s.sdPage&&s.sdConfirm),
 step('Keep SD Admin running','Choose Stay here. The simulated service remains running.',(s,g)=>s.sdPage&&!s.sdConfirm&&s.sdId===g.memory.job&&s.sdIdJob?.state==='running'),
 step('Open the leave choice again','Press Back again.',s=>s.sdPage&&s.sdConfirm),
 step('Stop and return','Choose Stop and back. The operation must stop and return to Settings.',(s,g)=>!s.sdPage&&s.sdStopped===g.memory.job&&s.sdIdJob?.state==='cancelled'&&!!s.bound('/Monster SD Admin'))]);}
export const queue2System=[detector(false),detector(true),reboot('grove'),reboot('mbus'),update(false),update(true),slot(false),slot(true),sd()];
