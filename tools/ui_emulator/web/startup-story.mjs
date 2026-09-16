/** S01: ordered evidence from visible native pages, never synthetic Next clicks. */
export function startupStory(condition='normal'){
 const steps=[];
 const add=(key,title,instruction,flow)=>steps.push({key,title,instruction,flow});
 if(condition==='missing-board')add('noBoard','No external boards','Read No Board Detected, then choose Continue Anyway. If already dismissed, reload this page and start this tour again.','show_no_board_popup');
 if(condition==='version-mismatch')add('versionWarning','Check the version warning','Read JanOS Version Mismatch and choose OK. If you chose Monster OTA, return to GROVE. If already dismissed, reload this page and start this tour again.','show_version_mismatch_popup');
 if(condition!=='missing-board')add('groveHome','Explore GROVE','Select GROVE and view its home tiles. This module has its own connection and operation state.','show_uart1_tiles');
 if(condition==='missing-sd'){
  add('sdWarning','Check the SD warning','On GROVE, open Wardrive to see the missing SD warning.','show_sd_warning_popup');
  add('groveHome','Return from the SD warning','Choose Cancel to return to the tiles. If you continued, use Back to return before proceeding.','close_sd_warning_popup');
 }
 if(condition!=='missing-board')add('mbusHome','Explore MBUS','Select MBUS. Its networks and operations are independent of GROVE. Return to its home tiles if another page is open.','show_mbus_tiles');
 add('internalHome','Explore INTERNAL','Select INTERNAL to find device settings and Module Status.','show_internal_tiles');
 add('status','Open Module Status','Open Module Status to compare the two simulated external modules.','app_system_show');
 add('groveCard','Read GROVE status','Keep the complete GROVE status text visible for one second. Check connection, firmware, SD and operation. Scroll inside the device if needed.','app_system_status_text');
 add('mbusCard','Read MBUS status','Scroll inside the device to see the complete MBUS status text for one second. Compare its state with GROVE.','app_system_status_text');
 add('internalHome','Return to INTERNAL','Use the Back button in Module Status to return to the INTERNAL tiles. Scroll back up inside the device if needed.','app_system_back_cb');
 return {id:'startup-modules',title:'Explore modules',steps,completionTitle:'Module tour complete',completion:condition==='missing-board'?'You continued without external boards, checked both disconnected modules in INTERNAL and returned from Module Status.':'You explored GROVE, MBUS and INTERNAL, checked the module conditions and returned from Module Status.'};
}

export class StartupGuide{
 active=false;complete=false;epoch=0;step=0;hint='';visible=true;tab=0;seenWarning=false;viewStart=null;lastView=null;
 constructor(story){this.story=story;}
 resetView(){this.viewStart=null;this.lastView=null;}
 start(){this.epoch++;this.active=true;this.complete=false;this.step=0;this.seenWarning=false;this.resetView();}
 leave(){this.epoch++;this.active=false;this.resetView();}
 setContext({tab,visible}){if(tab!==this.tab||visible!==this.visible)this.resetView();this.tab=tab;this.visible=visible;}
 accept(s){
  if(!this.active||this.complete||!this.visible||s.epoch!==this.epoch)return;
  const key=this.story.steps[this.step].key;
  let ready=false;
  if(key==='noBoard'||key==='versionWarning'){
   if(s[key])this.seenWarning=true;
   ready=this.seenWarning&&!s[key]&&(s.groveHome||s.internalHome);
  }else if(key==='sdWarning')ready=!!s.sdWarning&&!!s.wardriveWarning;
  else if(!s.noBoard&&!s.versionWarning&&!s.sdWarning){
   ready=!!s[key];
   if(key==='groveCard'||key==='mbusCard'){
    if(!ready||!s.status||!Number.isFinite(s.timeMs)){this.resetView();return;}
    if(this.lastView!==null&&(s.timeMs<this.lastView||s.timeMs-this.lastView>1000))this.resetView();
    if(this.viewStart===null)this.viewStart=s.timeMs;
    this.lastView=s.timeMs;ready=s.timeMs-this.viewStart>=1000;
   }
  }else this.resetView();
  if(ready){this.step++;this.resetView();this.complete=this.step===this.story.steps.length;}
 }
}

export function decodeStartupState(bits){
 return Object.fromEntries(['groveHome','mbusHome','internalHome','status','noBoard','versionWarning','sdWarning','groveCard','mbusCard','wardriveWarning'].map((key,i)=>[key,!!(bits&(1<<i))]));
}
