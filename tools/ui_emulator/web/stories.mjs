/** Session-only guidance. Only native Bluetooth events can complete a step. */
import {StartupGuide,startupStory,decodeStartupState} from './startup-story.mjs';
import {WiFiGuide,wifiStory} from './wifi-stories.mjs';
import {ReconGuide,reconStory} from './recon-stories.mjs';
import {queue2System} from './queue2-system.mjs';
import {queue2Attacks} from './queue2-attacks.mjs';
import {queue2Nettools} from './queue2-nettools.mjs';
import {queue2Wardrive} from './queue2-wardrive.mjs';
import {portalStories} from './portal-story.mjs';
import {pcapStories} from './pcap-story.mjs';
import {settingsS18} from './settings-s18.mjs';
import {settingsS19} from './settings-s19.mjs';
import {settingsS20} from './settings-s20.mjs';
export const queue2Stories=[...queue2System,...queue2Attacks,...queue2Nettools,...queue2Wardrive];
// All families share the observed-state guide engine and join the full gate.
export const guidedStories=[...portalStories,...pcapStories,...settingsS18,...settingsS19,...settingsS20,...queue2Stories];
export const bluetoothStory=Object.freeze({
  id:'bluetooth-locator',title:'Find a Bluetooth device',
  target:Object.freeze({name:'NEURODECK-07',mac:'02:20:77:02:00:01'}),
  steps:Object.freeze([
    {title:'Discover devices',instruction:'On GROVE, open Bluetooth → BT Scan & Locate. Already on the list? Tap Rescan to begin a fresh discovery.',flow:'show_bt_scan_page'},
    {title:'Choose NEURODECK-07',instruction:'Tap NEURODECK-07 (02:20:77:02:00:01) in the device list to open its locator.',flow:'show_bt_locator_page'},
    {title:'Watch the signal',instruction:'Watch for at least two seconds and three different RSSI readings. A value closer to 0 means a stronger signal; this is a simulated signal, not a measured distance.',flow:'app_bt_tick'},
    {title:'Stop locating',instruction:'Use the back arrow on the locator to stop tracking and return to the device list.',flow:'bt_locator_tracking_back_btn_event_cb'},
  ]),
});

export class BluetoothGuide {
  epoch=0;tab=0;visible=true;
  active=false;complete=false;step=0;hint='';samples=new Set();firstSampleAt=null;lastSampleAt=null;
  clearSamples(){this.samples.clear();this.firstSampleAt=null;this.lastSampleAt=null;}
  start(){this.epoch++;this.active=true;this.complete=false;this.step=0;this.hint='';this.clearSamples();}
  leave(){this.epoch++;this.active=false;this.clearSamples();}
  setContext({tab,visible}){
    if(tab!==this.tab||visible!==this.visible)this.clearSamples();
    this.tab=tab;this.visible=visible;
  }
  accept(e){
    if(!this.active||this.complete||!this.visible||this.tab!==0||e.tab!==0||e.epoch!==this.epoch)return;
    const target=e.mac===bluetoothStory.target.mac;
    if(e.type==='scan'){
      this.clearSamples();
      this.step=e.devices?.includes(bluetoothStory.target.mac)?1:0;
      this.hint=this.step?'':'No target found. Check the demo module condition, then Rescan.';
    }else if(e.type==='locate'&&this.step>0){
      this.clearSamples();this.step=target?2:1;
      this.hint=target?'':'Return to the list and choose NEURODECK-07.';
    }else if(e.type==='sample'&&target&&(this.step===2||this.step===3)){
      if(!Number.isFinite(e.rssi)||e.rssi<=-100){
        this.clearSamples();this.step=2;this.hint='No signal. Restore the module connection to continue.';return;
      }
      // A hidden locator emits no samples; a long gap starts a fresh viewing
      // window rather than crediting time spent on another tab or page.
      if(this.step===2&&this.lastSampleAt!==null&&e.timeMs-this.lastSampleAt>1000)this.clearSamples();
      this.hint='';this.samples.add(e.rssi);
      if(Number.isFinite(e.timeMs)){
        if(this.firstSampleAt===null)this.firstSampleAt=e.timeMs;
        this.lastSampleAt=e.timeMs;
        if(this.samples.size>=3&&e.timeMs-this.firstSampleAt>=2000)this.step=3;
      }
    }else if(e.type==='back'&&target){
      if(this.step===3){this.step=4;this.complete=true;}
      else if(this.step===2){this.step=1;this.clearSamples();}
    }
  }
}

export function mountBluetoothGuide(root,context=()=>({tab:0,visible:!document.hidden}),startupState=()=>0,wifiState=()=>({}),reconState=()=>({}),queueState=()=>({})){
  let guide=new BluetoothGuide(),story=bluetoothStory;
  const startup=root.querySelector('#guide-startup');
  const panel=root.querySelector('#guide-panel'),start=root.querySelector('#guide-start');
  const scan=root.querySelector('#guide-scan'),radar=root.querySelector('#guide-radar');
  const observer=root.querySelector('#guide-observer'),mesh=root.querySelector('#guide-mesh');
  const queueMenu=root.querySelector('#queue2-menu'),queueSelect=root.querySelector('#queue2-story'),queueStart=root.querySelector('#guide-queue2');
  for(const entry of guidedStories){const option=document.createElement('option');option.value=entry.id;option.textContent=entry.label;queueSelect.append(option);}
  let queueEntry=null,lastQueuePoll=0;
  const launchers=[start,startup,scan,radar,observer,mesh,queueStart];
  const title=root.querySelector('#guide-title'),instruction=root.querySelector('#guide-instruction');
  const progress=root.querySelector('#guide-progress'),list=root.querySelector('#guide-steps');
  function steps(){list.replaceChildren();for(const step of story.steps){const li=document.createElement('li');li.textContent=step.title;list.append(li);}}
  steps();
  let last='';
  function render(){
    globalThis.emulatorGuideEpoch=guide.epoch;
    const signature=JSON.stringify([guide.active,guide.complete,guide.step,guide.hint]);
    if(last===signature)return;last=signature;
    panel.hidden=!guide.active;for(const button of launchers)button.hidden=guide.active;
    queueMenu.hidden=guide.active;
    list.hidden=guide.complete;
    root.querySelector('#guide-leave').textContent=guide.complete?'Back to stories':'Free exploration';
    const count=story.steps.length;
    progress.textContent=guide.complete?`${count} of ${count} complete`:`Step ${guide.step+1} of ${count}`;
    title.textContent=guide.complete?(story.completionTitle||'Bluetooth story complete'):story.steps[guide.step].title;
    instruction.textContent=guide.complete?(story.completion||'You discovered a device, watched its signal change and stopped the locator. Keep exploring or try the story again.'):guide.hint||story.steps[guide.step].instruction;
    Array.from(list.children).forEach((li,i)=>{
      li.dataset.state=i<guide.step?'done':i===guide.step?'current':'pending';
      if(i===guide.step)li.setAttribute('aria-current','step');else li.removeAttribute('aria-current');
    });
    if(guide.active&&guide.complete)title.scrollIntoView({block:'nearest'});
  }
  function evidence(){return queueEntry?queueState(queueEntry):story.id.startsWith('recon-')?reconState(story.kind):wifiState();}
  function begin(next,entry=null){const epoch=guide.epoch;queueEntry=entry;story=next;guide=entry?entry.create():story.id==='startup-modules'?new StartupGuide(story):story.id.startsWith('wifi-')?new WiFiGuide(story):story.id.startsWith('recon-')?new ReconGuide(story):new BluetoothGuide();if(entry)story=guide.story;guide.epoch=epoch;guide.start(evidence());last='';steps();render();title.focus();}
  start.addEventListener('click',()=>begin(bluetoothStory));
  startup.addEventListener('click',()=>begin(startupStory(new URL(location.href).searchParams.get('moduleCondition'))));
  scan.addEventListener('click',()=>begin(wifiStory('scan',new URL(location.href).searchParams.get('wifiDemo')||'normal')));
  radar.addEventListener('click',()=>begin(wifiStory('radar',new URL(location.href).searchParams.get('wifiDemo')||'normal')));
  observer.addEventListener('click',()=>begin(reconStory('observer')));
  mesh.addEventListener('click',()=>begin(reconStory('mesh')));
  queueStart.addEventListener('click',()=>{const entry=guidedStories.find(x=>x.id===queueSelect.value);begin({id:entry.id},entry);});
  root.querySelector('#guide-restart').addEventListener('click',()=>{guide.start(evidence());render();title.focus();});
  root.querySelector('#guide-leave').addEventListener('click',()=>{guide.leave();render();start.focus();start.scrollIntoView({block:'nearest'});});
  function syncContext(){guide.setContext(context());if(queueEntry){if(guide.active&&!guide.complete&&performance.now()-lastQueuePoll>=100){lastQueuePoll=performance.now();guide.accept({...evidence(),epoch:guide.epoch,timeMs:performance.now()});render();}}else if(story.id==='startup-modules'){guide.accept({...decodeStartupState(startupState()),epoch:guide.epoch,timeMs:performance.now()});render();}else if(story.id.startsWith('wifi-')||story.id.startsWith('recon-')){guide.accept({...evidence(),epoch:guide.epoch,timeMs:performance.now()});render();}}
  document.addEventListener('visibilitychange',syncContext);
  globalThis.addEventListener('emulator-bluetooth',e=>{if(story.id!=='bluetooth-locator')return;syncContext();guide.accept({...e.detail,timeMs:performance.now()});render();});
  render();for(const button of launchers)button.disabled=false;queueSelect.disabled=false;
  return {
    syncContext,
    prepareRestart(rotation){
      if(queueEntry?.id!=='s19-rotation'||!guide.active||guide.complete)return null;
      guide.setContext(context());
      const payload=guide.prepareRestart?.(evidence(),rotation);
      return payload?{id:queueEntry.id,payload}:null;
    },
    resumeRestart(handoff){
      if(handoff?.id!=='s19-rotation')return false;
      const entry=guidedStories.find(x=>x.id===handoff.id);
      if(!entry)return false;
      const candidate=entry.create();
      if(!candidate.resumeRestart?.(handoff.payload,queueState(entry)))return false;
      queueEntry=entry;guide=candidate;story=guide.story;last='';
      steps();render();title.focus();return true;
    },
  };
}
