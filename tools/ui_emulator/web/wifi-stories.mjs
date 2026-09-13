const target='02:20:77:00:00:01',second='02:20:77:00:00:03',hidden='02:20:77:00:00:02';
export function wifiDemoSeed(seed,variant){
 const result=structuredClone(seed);
 if(variant==='hidden'){const n=result.networks.find(n=>n.bssid===hidden);if(n)n.ssid='';}
 if(variant==='empty'){result.networks=[];result.clients=[];}
 if(variant==='cancel')result.settings={...result.settings,scan_time_ms:30000};
 return result;
}
export function wifiStory(kind='scan',variant='normal'){
 const steps=[];const add=(key,title,instruction)=>steps.push({key,title,instruction});
 add('scan','Start a fresh GROVE scan','On GROVE, open WiFi Scan & Attack/Test, or press Scan again on its results page. An earlier scan does not count.');
 if(kind==='scan'&&variant==='cancel'){
  add('cancel','Cancel the scan','Press Cancel simulated operation in the browser panel while scanning. This demo uses a 30-second scan.');
  add('scan','Scan again','Press Scan on GROVE to start another scan, then let it finish.');
 }
 add('results','Read scan results',variant==='empty'?'Wait for the scan to finish with zero networks. An empty result is different from a cancelled scan.':'Wait for the completed network list. Each row identifies a network by its BSSID.');
 if(variant!=='empty'||kind==='radar'){
  if(kind==='radar')add('reject-none','Try Radar without a selection','With no networks selected, tap Radar. The app must reject this selection. Radar requires Red Team enabled in Settings.');
  if(kind==='scan')add('single','Select NEON-BAZAAR',`Select only NEON-BAZAAR (${target}) using its checkbox.`);
  add('multi','Select two networks',`Select NEON-BAZAAR (${target}) and CHROME-CLINIC (${second}). Clear any other selections.`);
  if(kind==='radar'){
   add('reject-multi','Try Radar with two selections','Tap Radar with both networks selected. The app must require exactly one target.');
   add('single','Keep only NEON-BAZAAR','Clear CHROME-CLINIC so only NEON-BAZAAR remains selected.');
   add('radar','Open Radar','Tap Radar for NEON-BAZAAR. If busy, finish or cancel the current operation first.');
   add('watch','Watch the Wi-Fi signal','Keep Radar visible for two seconds and at least three different RSSI readings. This is a simulated signal, not measured distance.');
   add('stop','Stop Radar','Use the native STOP button or Back arrow to end Radar and return to Scan.');
  }else{
   add('clear','Clear the selection','Clear both checkboxes. No networks should remain selected.');
   if(variant==='hidden'){
    add('hidden','Select the hidden SSID',`Find the unnamed/hidden network by BSSID ${hidden} and select only its checkbox. A missing name does not hide its BSSID.`);
    add('clear','Clear the hidden network','Clear its checkbox before returning.');
   }
  }
 }
 if(kind==='scan')add('back','Return to GROVE','Use the native Back arrow on Scan to return to the GROVE home tiles.');
 return {id:`wifi-${kind}`,kind,variant,steps,completionTitle:kind==='radar'?'Radar story complete':'Wi-Fi scan story complete',completion:kind==='radar'?'You checked selection rules, watched the correct network signal and stopped Radar.':'You completed a fresh scan, explored its results and returned to GROVE.'};
}
export class WiFiGuide{
 active=false;complete=false;epoch=0;step=0;hint='';tab=0;visible=true;floor=0;scanId=0;radarId=0;radarFloor=0;rejections=0;samples=new Set();first=null;last=null;
 constructor(story){this.story=story;}
 clearSamples(){this.samples.clear();this.first=this.last=null;}
 start(s={}){this.epoch++;this.active=true;this.complete=false;this.step=0;this.hint='';this.floor=s.scanId||0;this.scanId=0;this.radarFloor=s.radarId||0;this.radarId=0;this.rejections=s.rejections||0;this.clearSamples();}
 leave(){this.epoch++;this.active=false;this.clearSamples();}
 setContext({tab,visible}){if(tab!==this.tab||visible!==this.visible)this.clearSamples();this.tab=tab;this.visible=visible;}
 accept(s){
  if(!this.active||this.complete||!this.visible||this.tab!==0||s.tab!==0||s.epoch!==this.epoch)return;
  const key=this.story.steps[this.step].key;
  const selected=s.selected||[],only=(...ids)=>selected.length===ids.length&&ids.every(id=>selected.includes(id));
  let ready=false;
  if(key==='scan'){
   ready=s.scanVisible&&s.scanId>this.floor&&[1,2].includes(s.scanState);
   if(ready){this.scanId=s.scanId;this.rejections=s.rejections||0;}
   else if(s.scanState===4)this.hint='Scan could not start. Check the module connection and busy state, then press Scan again.';
  }else if(s.scanId!==this.scanId){
   if(s.scanId>this.scanId){this.floor=this.scanId;this.step=0;this.hint='A new scan started. Follow its results from the beginning.';this.clearSamples();}
   return;
  }else if(key==='cancel'){
   ready=s.scanState===3&&s.scanVisible;
   if(ready)this.floor=s.scanId;
   else if(s.scanState===2){this.step=0;this.floor=s.scanId;this.hint='That scan finished. Start another scan and cancel it while running.';}
  }else if([3,4].includes(s.scanState)){
   this.step=0;this.floor=s.scanId;this.hint='Scan cancelled or failed. Start a fresh scan to continue.';this.clearSamples();return;
  }else if(key==='results'){
   ready=s.scanVisible&&s.scanState===2&&(this.story.variant==='empty'&&this.story.kind==='scan'?s.count===0:s.count>0);
   if(s.scanState===2&&!s.count&&!ready)this.hint='No networks found. Choose Normal in Wi-Fi demo results and restart this story.';
  }else if(['single','multi','clear','hidden','reject-none','reject-multi'].includes(key)){
   if(!s.scanVisible||s.scanState!==2)return;
   if(key==='single')ready=only(target);
   if(key==='multi')ready=only(target,second);
   if(key==='clear')ready=only();
   if(key==='hidden')ready=only(hidden);
   if(key.startsWith('reject-')){
    const rejected=s.rejectedSelected||[];
    ready=(key==='reject-none'?only()&&s.rejectedCount===0:only(target,second)&&s.rejectedCount===2&&[target,second].every(id=>rejected.includes(id)))&&s.rejections>this.rejections;
    if(!s.redTeam)this.hint='Enable Red Team in INTERNAL Settings to make Radar available, then return to Scan.';
   }
   if(ready)this.rejections=s.rejections||0;
  }else if(key==='radar'){
   ready=s.radarVisible&&s.radarState===1&&s.radarTarget===target&&s.radarId>this.radarFloor;
   if(ready){this.radarId=s.radarId;this.radarFloor=s.radarId;this.clearSamples();}
  }else if(key==='watch'||key==='stop'){
   if(s.radarId!==this.radarId||s.radarState!==1){
    if(key==='stop'&&s.radarId===this.radarId&&s.radarState===2&&s.scanVisible&&!s.radarVisible)ready=true;
    else{this.step=this.story.steps.findIndex(x=>x.key==='radar');this.hint='Radar ended before completion. Restore the connection, select NEON-BAZAAR and open Radar again.';this.clearSamples();return;}
   }else if(key==='watch'){
    if(!s.radarVisible||s.radarTarget!==target||!Number.isFinite(s.rssi)||s.rssi<=-100||!Number.isFinite(s.timeMs)){this.clearSamples();return;}
    if(this.last!==null&&(s.timeMs<this.last||s.timeMs-this.last>1000))this.clearSamples();
    if(this.first===null)this.first=s.timeMs;
    this.last=s.timeMs;this.samples.add(s.rssi);ready=this.samples.size>=3&&s.timeMs-this.first>=2000;
   }
  }else if(key==='back')ready=s.homeVisible&&!s.scanVisible;
  if(ready){this.step++;this.hint='';this.complete=this.step===this.story.steps.length;}
 }
}
