import {readUI,StepGuide} from './queue2-common.mjs';
const names={deauth:'Deauth',sae:'SAE Overflow',handshake:'Handshake',blackout:'Blackout','global-handshaker':'Handshaker',snifferdog:'SnifferDog',beacon:'Beacon Spam'};
const operations={deauth:'deauth',sae:'sae_overflow',handshake:'handshake',blackout:'blackout','global-handshaker':'global_handshaker',snifferdog:'snifferdog',beacon:'beacon'};
const globalKinds=['blackout','global-handshaker','snifferdog'];
const step=(title,instruction,accept)=>({title,instruction,accept});
const remember=(s,g)=>{g.memory.run=s.run;g.memory.packets=s.packets;return true;};
const fresh=(s,g)=>s.running&&s.run>(g.baseline.run||0)&&remember(s,g);
const same=(s,g)=>s.run===g.memory.run;
const growth=(s,g)=>{if(g.memory.packets===null){g.memory.packets=s.packets;return false;}return same(s,g)&&s.running&&s.packets>g.memory.packets&&s.packets>0;};
const stopped=(s,g)=>same(s,g)&&s.state==='completed'&&!s.popup&&!s.active;
function spec(id){
 const karma=id.startsWith('karma'),empty=id==='beacon-empty',cancel=id.endsWith('-cancel'),kind=cancel?id.slice(0,-7):empty?'beacon':id;
 const label=karma?'Karma':names[kind],global=globalKinds.includes(kind),steps=[];
 if(karma){
  steps.push(step('Begin at the GROVE menu','On GROVE return to the module menu before opening Karma.',s=>s.home));
  steps.push(step('Open Karma','Choose the Karma tile in the GROVE menu.',s=>s.karma));
  steps.push(step('Start a fresh sniffer','Press Start Sniffer. Stop any old session first.',(s,g)=>s.karma&&!s.karmaPopup&&fresh(s,g)));
  steps.push(step('Observe synthetic probes','Keep Karma visible while its packet counter increases.',(s,g)=>s.karma&&growth(s,g)));
  steps.push(step('Stop sniffer and select a probe','Press Stop Sniffer, then select NEON-BAZAAR from the probes.',(s,g)=>same(s,g)&&s.state==='completed'&&s.karmaConfig));
  steps.push(step('Start configured Karma','Keep demo-portal.html selected and press Start Karma.',(s,g)=>s.karmaPopup&&s.running&&s.run>g.memory.run&&s.ssids.includes('NEON-BAZAAR')&&remember(s,g)));
  steps.push(step('Observe Karma activity','Watch the synthetic client and packet counters change.',(s,g)=>s.karmaPopup&&growth(s,g)&&s.clients>0));
  steps.push(step('Stop Karma','Press the native STOP button in the Karma popup.',stopped));
  steps.push(step('Return to the module menu','Use the native Back arrow.',s=>s.home&&!s.karma));
 }else if(empty){
  steps.push(step('Open the Beacon SSID list','Global WiFi Attacks → Beacon Spam → List SSIDs.',s=>s.beaconList));
  steps.push(step('Empty the SSID list','Delete each configured demo SSID.',s=>s.beaconList&&s.ssidCount===0));
  steps.push(step('Check empty-list refusal','Go Back and press Start Spam. No operation should start.',s=>s.beacon&&s.ssidCount===0&&!s.active&&s.emptyRefusal));
  steps.push(step('Return to Global WiFi','Use the native Back arrow.',s=>!s.beacon&&!s.beaconList&&!s.active));
 }else{
  if(global){
   steps.push(step('Open the confirmation',`On GROVE open Global WiFi Attacks → ${label}.`,s=>s.confirm));
   steps.push(step('Cancel without starting','Choose No. The module must remain idle.',s=>!s.confirm&&!s.popup&&!s.active));
   if(!cancel)steps.push(step('Confirm a fresh session',`Open ${label} again and choose Yes.`,(s,g)=>s.popup&&fresh(s,g)));
  }else if(kind==='beacon'){
   steps.push(step('Inspect configured SSIDs','Global WiFi Attacks → Beacon Spam → List SSIDs.',s=>s.beaconList&&s.ssidCount>0));
   steps.push(step('Start Beacon Spam','Go Back and choose Start Spam.',(s,g)=>s.popup&&fresh(s,g)));
  }else{
   steps.push(step('Select NEON-BAZAAR','On GROVE open WiFi Scan & Attack. Select only NEON-BAZAAR (02:20:77:00:00:01).',s=>s.scan&&s.targets.length===1&&s.targets[0]==='net-1'));
   steps.push(step(`Start ${label}`,`Choose ${kind==='handshake'?'Handshake':label} on the Scan action bar. The native action starts immediately.`,(s,g)=>s.popup&&s.jobTargets.length===1&&s.jobTargets[0]==='net-1'&&fresh(s,g)));
  }
  if(!global||!cancel){
   if(cancel)steps.push(step('Cancel the operation','Use Cancel operation in the emulator controls.',(s,g)=>same(s,g)&&s.state==='cancelled'&&!s.active));
   else steps.push(step('Observe synthetic activity',kind==='handshake'?'Wait for the selected network handshake result.':'Keep the native activity screen visible while its counters increase.',(s,g)=>s.popup&&growth(s,g)&&(kind==='handshake'?s.hsSuccess:kind==='global-handshaker'?s.handshakes>0:true)));
   steps.push(step(cancel?'Close the stopped popup':'Stop the session',kind==='handshake'?'Use the native STOP / Close button.':'Use the native STOP button.',cancel?(s,g)=>same(s,g)&&s.state==='cancelled'&&!s.popup:stopped));
  }
  if(global&&cancel)steps.push(step('Return to the module menu','Use the Global WiFi Back arrow.',s=>s.home&&!s.active));
 }
 return {id,kind,steps,completionTitle:`${label}${cancel?' cancellation':empty?' empty-list':''} story complete`,completion:'The native controls and synthetic results matched this offline session. No radio or network operation was performed.',onContextChange(g){g.memory.packets=null;},guard(s,g){
  if(karma&&g.step===1&&!s.karma&&!s.home){g.step=0;g.hint='Return to the requested entry context before opening Karma.';return false;}
  if(s.connected===false){g.hint='Reconnect GROVE, finish any busy operation, and restart this story.';return false;}
  if(g.memory.run&&s.run!==g.memory.run&&!karma&&g.step>1){g.hint='The operation changed. Restart the story for this session.';return false;}
  return true;
 }};
}
export function createAttackReader(id){
 const kind=id.replace(/-cancel$/,''),karma=kind.startsWith('karma'),op=karma?'karma':operations[kind==='beacon-empty'?'beacon':kind];
 let moduleOwner,last=0;
 return (module,device)=>{
  if(moduleOwner!==module){moduleOwner=module;last=0;}
  const ui=readUI(module);module._emu_queue2_attacks_state();const s={...globalThis.emulatorQueue2AttackEvidence};
  const model=device.device,snap=device.snapshot(0),active=model.job(snap.active);
  module._emu_wifi_story_state();const targets=globalThis.emulatorWifiEvidence.selected.map(b=>snap.networks.find(n=>n.bssid===b)?.id).filter(Boolean);
  const nativeId=karma?s.karmaId:kind.startsWith('beacon')?s.beaconId:kind==='deauth'?s.deauthId:kind==='sae'?s.saeId:kind==='handshake'?s.hsId:s.globalId;
  if(active?.operation===op&&nativeId===active.id)last=active.id;
  const job=model.job(last),global=globalKinds.includes(kind);
  const popup=karma?s.karmaPopup:kind.startsWith('beacon')?s.beaconPopup:kind==='deauth'?s.deauthPopup:kind==='sae'?s.saePopup:kind==='handshake'?s.hsPopup:!!(global&&ui.has('Attack in Progress',true));
  return {...s,run:last,state:job?.state||'',running:job?.state==='running',active:snap.active,connected:snap.connected,targets,jobTargets:job?.options?.networkIds||[],packets:job?.result?.packets||0,clients:job?.result?.clients?.length||0,ssids:job?.result?.ssids||[],handshakes:job?.result?.handshakes?.length||0,popup,confirm:global&&ui.bound(`show_${kind==='global-handshaker'?'global_handshaker':kind}_confirm_popup.yes_btn`),emptyRefusal:ui.has('No SSIDs configured. Add at least one.')};
 };
}
export const queue2Attacks=['karma-menu','deauth','sae','handshake','blackout','global-handshaker','snifferdog','beacon','beacon-empty',...['deauth','sae','handshake','blackout','global-handshaker','snifferdog'].map(k=>k+'-cancel')].map(id=>({id,label:id==='karma-menu'?'Karma - module menu':spec(id).completionTitle.replace(' story complete',''),create:()=>new StepGuide(spec(id)),read:createAttackReader(id)}));
