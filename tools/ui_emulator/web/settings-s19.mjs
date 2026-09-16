import {StepGuide,readUI} from './queue2-common.mjs';
function read(module){
 module._emu_settings_s19_state();const s={...globalThis.emulatorSettingsS19,...readUI(module)};
 const get=key=>Number(globalThis.emulatorSettings.get(key));
 s.saved={timeout:get('scr_timeout'),brightness:get('scr_bright'),dark:get('dark_mode'),rotation:get('scr_rot'),dashboard:get('dashboard'),boot:get('boot_sound'),alert:get('alert_sound')};
 s.filter=document.querySelector('#display').style.filter;return s;
}
const step=(title,instruction,accept)=>({title,instruction,accept});
const close=kind=>step('Close '+kind,'Press the native Close button to return to Settings.',s=>!s[kind+'Open']&&s.settings);
function make(id,label,steps,Guide=StepGuide){const story={id:'s19-'+id,label,tab:3,steps,completionTitle:label+' complete',completion:'Native preferences and the visible UI agree. Back to stories returns to the demo menu.'};return{id:story.id,label:'S19 · '+label,read,create:()=>new Guide(story)};}
function preference(kind,label,instruction){
 const valid=(s,value)=>s[kind]===value&&s.saved[kind]===value&&(kind!=='brightness'||s.filter===`brightness(${value/100})`);
 return make(kind,label,[
  step('Open '+label,'Open INTERNAL → Settings → '+label+'.',s=>s[kind+'Open']),
  step('Change '+kind,instruction,(s,g)=>{if(!s[kind+'Open']||s[kind]===g.baseline[kind]||!valid(s,s[kind]))return false;g.memory.value=s[kind];return true;}),
  close(kind),
  step('Reopen '+label,'Open the same tile again and check that your selected value was retained.',(s,g)=>s[kind+'Open']&&valid(s,g.memory.value)),
  step('Restore '+kind,'Restore the original '+kind+' value shown when this story started.',(s,g)=>{g.hint='Restore '+kind+' to '+g.baseline[kind]+(kind==='brightness'?'%.':' (0=10s, 1=30s, 2=1min, 3=5min, 4=Stays On).');return s[kind+'Open']&&valid(s,g.baseline[kind]);}),
  close(kind),
 ]);
}
const themeNames={dashboard:'Dashboard',boot:'Boot sound',alert:'Alert sound'};
function themePreference(key,restore=false){return step((restore?'Restore ':'Change ')+themeNames[key],restore?'Restore '+themeNames[key]+' to the starting preference shown below.':'Choose a different '+themeNames[key]+' preference. Sound controls save a preference only; the emulator stays silent.',(s,g)=>{
 const baseline=g.baseline[key];if(restore)g.hint='Restore '+themeNames[key]+' to '+(key==='boot'?['OFF','Nokia','Intel','Star Wars'][baseline]:baseline?'ON.':'OFF.');
 if(!s.themeOpen||s.saved[key]!==Number(s[key])||(restore?s[key]!==baseline:s[key]===baseline))return false;
 if(!restore)g.memory[key]=s[key];return true;
});}
const themePrefs=(s,values)=>['dashboard','boot','alert'].every(key=>s[key]===values[key]&&s.saved[key]===Number(values[key]));
function theme(){return make('theme','Theme',[
 step('Open Theme','Open INTERNAL → Settings → Theme.',s=>s.themeOpen),
 step('Change dark mode','Toggle Dark mode. The native UI rebuilds immediately and closes this popup.',(s,g)=>!s.themeOpen&&s.settings&&s.dark!==g.baseline.dark&&s.saved.dark===Number(s.dark)),
 step('Reopen Theme','Reopen Theme and inspect the changed Dark mode switch.',(s,g)=>s.themeOpen&&s.dark!==g.baseline.dark&&s.saved.dark===Number(s.dark)),
 step('Restore dark mode','Toggle Dark mode back to its original setting. The native UI rebuilds again.',(s,g)=>!s.themeOpen&&s.settings&&s.dark===g.baseline.dark&&s.saved.dark===Number(s.dark)),
 step('Check restored Theme','Open Theme again and check the restored Dark mode switch.',(s,g)=>s.themeOpen&&s.dark===g.baseline.dark),
 themePreference('dashboard'),themePreference('boot'),themePreference('alert'),close('theme'),
 step('Reopen saved Theme preferences','Reopen Theme and inspect the changed Dashboard, Boot sound and Alert sound preferences.',(s,g)=>s.themeOpen&&themePrefs(s,g.memory)),
 themePreference('dashboard',true),themePreference('boot',true),themePreference('alert',true),close('theme'),
 step('Check all restored preferences','Reopen Theme once more. All four preferences should match the values from the beginning of the story.',(s,g)=>s.themeOpen&&s.dark===g.baseline.dark&&themePrefs(s,g.baseline)),
 close('theme'),
]);}
export class RotationGuide extends StepGuide {
 prepareRestart(s,requestedRotation){
  if(!this.active||this.complete||!this.visible||this.tab!==3||s.tab!==3||this.step!==4||!s.rotationOpen||s.rotation!==this.memory.value||s.saved.rotation!==this.memory.value||requestedRotation!==this.memory.value||s.activeRotation!==this.baseline.activeRotation)return null;
  return {version:1,fromRotation:this.baseline.activeRotation,expectedRotation:this.memory.value};
 }
 resumeRestart(payload,s){
  if(this.resumed||!payload||Object.keys(payload).sort().join(',')!=='expectedRotation,fromRotation,version'||payload.version!==1||![0,1,2,3].includes(payload.expectedRotation)||![0,1,2,3].includes(payload.fromRotation)||payload.expectedRotation===payload.fromRotation||s.activeRotation!==payload.expectedRotation||s.rotation!==payload.expectedRotation||s.saved.rotation!==payload.expectedRotation)return false;
  this.start(s);this.resumed=true;this.memory.value=payload.expectedRotation;this.step=5;return true;
 }
}
function rotation(){return make('rotation','Screen Rotation',[
 step('Open Screen Rotation','Open INTERNAL → Settings → Screen Rotation.',s=>s.rotationOpen),
 step('Choose a new orientation','Select an orientation different from the active one. It is saved now; the display changes only after Restart.',(s,g)=>{if(!s.rotationOpen||s.rotation===g.baseline.activeRotation||s.saved.rotation!==s.rotation||s.activeRotation!==g.baseline.activeRotation)return false;g.memory.value=s.rotation;return true;}),
 close('rotation'),
 step('Reopen Screen Rotation','Reopen the tile. Your selected orientation is retained and Restart to apply remains visible.',(s,g)=>s.rotationOpen&&s.rotation===g.memory.value&&s.saved.rotation===g.memory.value&&s.activeRotation===g.baseline.activeRotation),
 step('Restart into the new orientation','Press the native Restart button. This one story resumes once after the local preview reloads.',()=>false),
 step('Check applied orientation','Open INTERNAL → Settings → Screen Rotation. Check Currently active and the new display orientation.',(s,g)=>s.rotationOpen&&s.activeRotation===g.memory.value&&s.rotation===g.memory.value&&s.saved.rotation===g.memory.value),
 close('rotation'),
],RotationGuide);}
export const settingsS19=[preference('timeout','Screen Timeout','Choose a different timeout. Only the saved preference is simulated; the browser does not physically sleep or dim on inactivity.'),preference('brightness','Screen Brightness','Move and release the native slider to a different value. Watch the display brightness change.'),rotation(),theme()];
