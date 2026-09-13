// S08 - offline client & password demo (Evil Twin, Rogue AP, INTERNAL portal).
// Guide predicates only observe native surfaces and the offline model; no
// control is clicked here and no radio, HTTP server or credential file exists.
import {readUI,StepGuide} from './queue2-common.mjs';

const specs={
 'evil-twin':{label:'Evil Twin',module:'grove',tab:0,op:'evil_twin',scan:true},
 'rogue-ap':{label:'Rogue AP',module:'grove',tab:0,op:'rogue_ap',scan:true},
 'internal-portal':{label:'INTERNAL Ad Hoc Portal',module:'internal',tab:3,op:'portal_demo',scan:false},
};

const step=(title,instruction,accept)=>({title,instruction,accept});
const started=(s,g)=>s.running&&s.run>(g.baseline.run||0)&&(g.memory.run=s.run,true);
const same=(s,g)=>s.run===g.memory.run;
const reached=(s,g,...stages)=>same(s,g)&&stages.includes(s.stage);
const stopped=(s,g)=>same(s,g)&&s.state==='completed'&&!s.active;

function spec(id){
 const cfg=specs[id],label=cfg.label,steps=[];
 if(cfg.scan){
  steps.push(step('Select NEON-BAZAAR','On GROVE open WiFi Scan & Attack, run a fresh Scan and select only NEON-BAZAAR (02:20:77:00:00:01).',s=>s.scanReady&&s.targets.length===1&&s.targets[0]==='net-1'));
  steps.push(step(`Start ${label}`,`Choose ${label} on the Scan action bar, then start the native attack.`,started));
 }else{
  steps.push(step('Open the Ad Hoc portal','On INTERNAL open Ad Hoc Portal & Karma.',s=>s.portalPage));
  steps.push(step('Start the offline portal','Show Probes, choose NEON-BAZAAR and press Start.',started));
 }
 steps.push(step('A demo client connects','Keep the native activity screen visible until a synthetic client connects.',(s,g)=>reached(s,g,'connected','portal_opened','submitted')));
 steps.push(step('The client visits the portal','Watch the ordered steps: the client opens the demo portal.',(s,g)=>reached(s,g,'portal_opened','submitted')));
 steps.push(step('The demo password is submitted',`Wait for the fixed demonstration password. It is always DEMO-only-2026! and no credential is stored.`,(s,g)=>reached(s,g,'submitted')&&s.password));
 steps.push(step('Stop the demo',cfg.scan?`Use the native ${label==='Evil Twin'?'STOP':'Stop Rogue AP'} button to end the session.`:'Use the native STOP PORTAL button to end the session.',stopped));
 return {id,tab:cfg.tab,steps,
  completionTitle:`${label} demo story complete`,
  completion:'A synthetic client connected, opened the offline portal and submitted the fixed demo password, then you stopped the session. No radio, HTTP server or credential file was involved.',
  guard(s,g){
   if(s.connected===false){g.hint='Reconnect the module, finish any busy operation and restart this story.';return false;}
   if(g.memory.run&&s.run!==g.memory.run&&g.step>2){g.hint='The demo changed. Restart the story for this session.';return false;}
   return true;
  }};
}

export function createPortalReader(id){
 const cfg=specs[id];
 let moduleOwner,last=0;
 return (module,device)=>{
  if(moduleOwner!==module){moduleOwner=module;last=0;}
  const ui=readUI(module),model=device.device,snap=model.snapshot(cfg.module);
  const active=model.job(snap.active);
  if(active?.operation===cfg.op&&snap.active===active.id)last=active.id;
  const job=model.job(last),demo=job?.result?.demo||{};
  let targets=[],scanReady=false;
  if(cfg.scan){
   module._emu_wifi_story_state();const grove=model.snapshot('grove');
   targets=globalThis.emulatorWifiEvidence.selected.map(b=>grove.networks.find(n=>n.bssid===b)?.id).filter(Boolean);
   scanReady=module._emu_scan_state(0)===2;
  }
  return {tab:module._emu_current_tab(),run:last,state:job?.state||'',running:job?.state==='running',
   active:snap.active,connected:snap.connected,stage:demo.stage||'',password:!!demo.password,
   scanReady,targets,portalPage:ui.has('Show Probes',true)||ui.has('STOP PORTAL',true)};
 };
}

export const portalStories=Object.keys(specs).map(id=>({
 id,label:specs[id].label+' - client & password demo',
 create:()=>new StepGuide(spec(id)),read:createPortalReader(id),
}));
