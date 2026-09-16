export function reconStory(kind){
 const observer=kind==='observer',steps=[];
 const add=(key,title,instruction)=>steps.push({key,title,instruction});
 add('start',`Start a fresh ${observer?'Observer':'Mesh'} session`,observer?'On GROVE, open Network Observer and press Start. It discovers its own networks and clients; Wi-Fi Scan is not required. If already running, Stop and Start again.':'On GROVE, open Mesh Recon and press Start. If already running, Stop and Start again.');
 add('activity',observer?'Watch networks and clients':'Watch Mesh networks appear',observer?'Keep Observer visible for two seconds while the packet count grows and networks and clients appear.':'Keep Mesh visible for two seconds while packet counts grow and at least two synthetic networks appear. Stop is available throughout monitoring.');
 if(observer){
  add('client','Open NEON-BAZAAR','Tap the NEON-BAZAAR network row (02:20:77:00:00:01) to view its clients.');
  add('capture','Save a synthetic capture','Choose Simulation: generate PCAP, then Generate synthetic PCAP. Wait for the saved file. This is an offline Ethernet/ARP fixture.');
  add('close','Return to Observer','Close the capture dialog and the network details to return to the Observer list.');
  add('ask-keep','Try leaving Observer','Press the native Back arrow. Observer must ask before stopping its running session.');
  add('keep','Keep Observer running','Choose Keep running. You stay in Observer with its session active.');
  add('ask-stop','Open the exit choice again','Press the native Back arrow again.');
  add('exit','Stop and exit','Choose Stop and exit. The session must stop and return to the GROVE menu.');
 }else{
  add('nodes','Expand Mesh network 0x1A2B','Tap network 0x1A2B to reveal its synthetic nodes.');
  add('stop','Stop Mesh monitoring','Press the native Stop button. Networks and nodes should remain visible.');
  add('clear','Clear retained Mesh results','Press Clear to remove the stopped session results.');
  add('back','Return to GROVE','Use the native Back arrow to return to the GROVE menu.');
 }
 return {id:`recon-${kind}`,kind,steps,completionTitle:`${observer?'Observer':'Mesh'} story complete`,completion:observer?'You discovered networks independently, saved a synthetic PCAP, kept Observer running and then stopped it. The file remains available in this demo session.':'You watched Mesh discovery, expanded its nodes, stopped monitoring and cleared the retained results.'};
}
export class ReconGuide {
 active=false;complete=false;epoch=0;step=0;hint='';tab=0;visible=true;
 constructor(story){this.story=story;}
 resetWindow(){this.first=null;this.packets=null;}
 start(s={}){this.epoch++;this.active=true;this.complete=false;this.step=0;this.hint='';this.floor=s.run||0;this.captureFloor=s.captureId||0;this.run=0;this.resetWindow();}
 leave(){this.epoch++;this.active=false;this.resetWindow();}
 setContext({tab,visible}){if(tab!==this.tab||visible!==this.visible)this.resetWindow();this.tab=tab;this.visible=visible;}
 accept(s){
  if(!this.active||this.complete)return;
  if(!this.visible||this.tab!==0||s.visible===false||s.tab!==0||s.epoch!==this.epoch){this.resetWindow();return;}
  const key=this.story.steps[this.step].key,observer=this.story.kind==='observer';let ready=false;
  if(key==='start'){
   ready=s.page&&s.running&&s.run>this.floor;
   if(ready){this.run=s.run;this.captureFloor=Math.max(this.captureFloor,s.captureId||0);this.resetWindow();}
   else this.hint='Open the page on GROVE and press Start. If unavailable, check the module connection and finish any busy operation first.';
  }else if(s.run!==this.run){
   if(s.run>this.run){this.floor=this.run;this.step=0;this.resetWindow();this.hint='A new session started. Follow it from the beginning.';}return;
  }else if(!s.running&&!(['clear','back'].includes(key)||key==='stop'&&s.stopped===this.run||key==='exit'&&s.stopped===this.run)){
   this.floor=this.run;this.step=0;this.resetWindow();this.hint='The session stopped early or disconnected. Restore the module and start a fresh session.';return;
  }else if(key==='activity'){
   if(!s.page||s.popup||s.confirm||!Number.isFinite(s.timeMs)||!Number.isFinite(s.packets)){this.resetWindow();return;}
   if(this.first===null||s.timeMs<this.first||s.packets<this.packets){this.first=s.timeMs;this.packets=s.packets;}
   ready=s.timeMs-this.first>=2000&&s.packets>this.packets&&s.networks>=(observer?1:2)&&(observer?s.clients>0:true);
   if(!s.networks)this.hint='Waiting for demo networks. If using Empty results, choose Normal in Wi-Fi demo results and start again.';
  }else if(key==='client')ready=s.page&&s.popup&&s.target==='02:20:77:00:00:01'&&s.clients>0;
  else if(key==='capture'){
   ready=s.page&&s.captureVisible&&s.captureId>this.captureFloor&&s.captureState===2&&s.captureTarget==='net-1'&&s.file;
   if(s.captureState===3||s.captureState===4)this.hint='Capture cancelled or failed. Close its dialog, reopen the NEON-BAZAAR capture and generate it again.';
  }else if(key==='close')ready=s.page&&!s.popup&&!s.captureVisible;
  else if(key==='ask-keep'||key==='ask-stop')ready=s.page&&s.confirm;
  else if(key==='keep')ready=s.page&&!s.confirm&&s.running;
  else if(key==='exit')ready=!s.running&&s.stopped===this.run&&s.home&&!s.confirm;
  else if(key==='nodes')ready=s.page&&s.expanded==='0x1A2B'&&s.nodes>0;
  else if(key==='stop')ready=s.page&&!s.running&&s.stopped===this.run&&s.networks>=2&&s.nodes>0;
  else if(key==='clear')ready=s.page&&!s.running&&s.cleared===this.run&&s.networks===0&&s.nodes===0;
  else if(key==='back')ready=s.home&&!s.page&&!s.running;
  if(ready){this.step++;this.hint='';this.complete=this.step===this.story.steps.length;}
 }
}
