const guest='AFTERLIFE-GUEST';
const stepsFor=kind=>{
 const gitm=kind.startsWith('gitm'), steps=[];
 const add=(key,title,instruction)=>steps.push({key,title,instruction});
 add('config',gitm?'Open GITM setup':`Configure ${kind.toUpperCase()}`,gitm?`On GROVE, ${kind==='gitm-scan'?'open WiFi Scan & Attack, select AFTERLIFE-GUEST and tap GITM':'open Global WiFi Attacks and tap GITM'}.`:`On GROVE, open WiFi Scan & Attack, select only AFTERLIFE-GUEST, then ${kind.toUpperCase()}. Scroll the action strip if needed.`);
 if(gitm){
  add('networks','Scan upstream networks','Expand 1. Internet if collapsed. Press SCAN and wait for synthetic networks. Select AFTERLIFE-GUEST.');
  add('connect','Connect upstream','Select AFTERLIFE-GUEST and press CONNECT. Wait for AP setup.');
  add('run','Start synthetic capture','Enter SIMULATED-GATEWAY as AP SSID, close the keyboard, then START GITM.');
  add('activity','Inspect capture activity','Watch synthetic packet and client results.');
  add('ask','Try leaving capture','Press the native Back arrow to open the exit confirmation.');
  add('keep','Keep capture running','Choose Keep capturing.');
  add('ask-again','Open exit confirmation again','Press the native Back arrow again.');
  add('exit','Stop, save and exit','Choose Stop and exit. Wait for a finalized PCAP on the virtual SD and the module menu.');
 }else if(kind==='mitm'){
  add('run','Start MITM demo','Press Connect & Start.');
  add('activity','Inspect synthetic traffic','Watch the synthetic packet counter increase.');
  add('file','Finalize the capture','Press STOP CAPTURE and wait for File: with a saved PCAP.');
  add('back','Return to Scan','Press Cancel to close the completed capture dialog.');
 }else{
  add('connect','Connect to the selected network','Press Connect and wait for Connected (synthetic). Start a fresh connection if restarting this guide.');
  add('hosts','Discover synthetic hosts','Press List Hosts. Verify 192.0.2.10 and 192.0.2.20.');
  if(kind==='arp'){
   add('run','Choose host 192.0.2.10','Tap 192.0.2.10 to start its synthetic ARP session.');
   add('activity','Inspect synthetic packets','Watch the packet count grow.');
   add('stop','Stop the ARP session','Press STOP to release the module.');
  }else for(const [level,target] of [['quick','192.0.2.10'],['medium','192.0.2.10'],['heavy','all']]){
   add('scan-'+level,`${level[0].toUpperCase()+level.slice(1)} scan: ${target}`,`Choose ${target==='all'?'Nmap All Hosts':'192.0.2.10'}, then ${level}. Wait for the completed result with port 80/tcp${target==='all'?' and both hosts':''}.`);
   add('close-'+level,'Close scan results','Press STOP to return to the host list.');
  }
  add('back','Return to Scan','Use the native Back arrow.');
 }
 steps[0].instruction+=' If this setup belongs to an earlier session, use native STOP/Back and reopen it before continuing.';
 return steps;
};
export class NettoolsGuide {
 active=false;complete=false;epoch=0;step=0;hint='';tab=0;visible=true;
 constructor(kind){this.kind=kind;this.story={id:`queue2-${kind}`,steps:stepsFor(kind),completionTitle:`${kind.toUpperCase()} complete`,completion:'The native demo results and cleanup were verified. Saved captures remain on the virtual SD for this session.'};}
 start(s={}){this.epoch++;this.active=true;this.complete=false;this.step=0;this.hint='';this.floor={run:s.run||0,connect:s.connectId||0,hosts:s.hostsId||0};this.run=0;this.connect=0;this.window=null;}
 leave(){this.epoch++;this.active=false;this.window=null;}
 setContext({tab,visible}){if(tab!==this.tab||visible!==this.visible)this.window=null;this.tab=tab;this.visible=visible;}
 accept(s){
  if(!this.active||this.complete||s.epoch!==this.epoch||!this.visible||this.tab!==0||s.tab!==0||s.visible===false){this.window=null;return;}
  if(!s.moduleConnected){this.hint='Reconnect GROVE, then restart the guide and native operation.';this.window=null;return;}
  const key=this.story.steps[this.step].key,gitm=this.kind.startsWith('gitm');let ok=false;
  const correct=s.target===guest;
  const correctCapture=this.kind!=='mitm'||s.job?.options?.networkIds?.length===1&&s.job.options.networkIds[0]===s.targetId;
  if(key==='config')ok=s.page&&(gitm?s.entry===(this.kind==='gitm-scan'?1:2):s.target===guest);
  else if(key==='networks')ok=s.page&&s.hostsId>this.floor.hosts&&s.hostsJob?.state==='completed'&&s.hosts>0;
  else if(key==='connect'){ok=s.page&&correct&&s.connected&&s.connectId>this.floor.connect&&s.connectJob?.state==='completed';if(ok)this.connect=s.connectId;}
  else if(key==='hosts')ok=s.page&&correct&&s.connectId===this.connect&&s.hostsId>this.floor.hosts&&s.hostsJob?.state==='completed'&&s.hosts>=2;
  else if(key==='run'){
   ok=s.page&&s.running&&s.run>this.floor.run&&correct&&correctCapture&&(!gitm||s.job?.options?.upstreamSsid===guest&&s.apSsid==='SIMULATED-GATEWAY')&&
    (this.kind!=='arp'||s.job?.options?.targetMac==='02:00:00:00:00:10');
   if(ok){this.run=s.run;this.window=null;}
  }else if(key.startsWith('scan-')){
   const level=key.slice(5),target=level==='heavy'?'all':'192.0.2.10';
   ok=s.page&&s.resultPopup&&s.run>this.floor.run&&correct&&s.job?.state==='completed'&&s.job?.options?.level===level&&s.job.options.target===target&&s.job.result?.text?.includes('80/tcp');
   if(ok){this.run=s.run;this.floor.run=s.run;}
  }else if(key.startsWith('close-'))ok=s.page&&!s.resultPopup&&s.run===this.run&&s.stopped;
  else if(key==='activity'){
   if(s.run!==this.run||!s.page||!s.running||s.confirm){this.window=null;return;}
   if(!this.window)this.window={time:s.timeMs,packets:s.packets};
   ok=Number.isFinite(s.timeMs)&&s.timeMs-this.window.time>=1000&&(gitm?s.packets>0&&s.job?.result?.clients?.length>0&&s.job?.result?.bytes>0:s.packets>this.window.packets);
  }else if(key==='ask'||key==='ask-again')ok=s.page&&s.run===this.run&&s.running&&s.confirm;
  else if(key==='keep')ok=s.page&&s.run===this.run&&s.running&&!s.confirm;
  else if(key==='exit')ok=!s.page&&s.home&&s.run===this.run&&s.stopped&&s.file&&!s.confirm;
  else if(key==='file')ok=s.page&&s.run===this.run&&s.stopped&&s.file;
  else if(key==='stop')ok=s.page&&s.run===this.run&&s.stopped&&!s.resultPopup;
  else if(key==='back')ok=!s.page&&s.scan&&(!this.run||s.run===this.run&&s.stopped);
  if(ok){this.step++;this.hint='';this.complete=this.step===this.story.steps.length;}
  else if(s.job&&['cancelled','failed'].includes(s.job.state)&&this.run===s.run)this.hint='Operation cancelled or failed. Restart the guide and start a fresh native session.';
 }
}
export const queue2Nettools=['arp','mitm','nmap','gitm-scan','gitm-global'].map((kind,index)=>({id:`queue2-${kind}`,label:kind.startsWith('gitm')?`GITM · ${kind.endsWith('scan')?'Scan':'Global WiFi'}`:kind.toUpperCase(),create:()=>new NettoolsGuide(kind),read(module,bridge){module._emu_queue2_nettools_state(index);return {...globalThis.emulatorQueue2NettoolsEvidence,targetId:bridge?.device?.snapshot('grove').networks.find(n=>n.ssid===guest)?.id};}}));
