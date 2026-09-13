import {StepGuide,readUI} from './queue2-common.mjs';
function read(module){module._emu_settings_s18_state();module._emu_wifi_story_state();return {...globalThis.emulatorWifiEvidence,...globalThis.emulatorSettingsS18,...readUI(module)};}
class SettingsGuide extends StepGuide{
 accept(s){this.story.tab=this.story.steps[this.step]?.tab??3;super.accept(s);}
}
const step=(title,instruction,accept,tab=3)=>({title,instruction,accept,tab});
function entry(id,label,steps,extra={}){return {id,label,read,create:()=>new SettingsGuide({id,label,steps,completionTitle:label+' complete',completion:'The native settings workflow is finished. Back to stories returns to the story menu.',...extra})};}
const open=()=>step('Open Scan Setup','Open INTERNAL → Settings → Scan Setup. If already open, Cancel and reopen it.',(s,g)=>{if(!s.scanPopup){g.baseline.scanPopup=false;return false;}return !g.baseline.scanPopup;});
const freshScan=()=>step('Check a fresh Scan','Close the settings popup, switch to GROVE and open WiFi Scan. If Scan was already open, press Scan again. Wait for the synthetic results. Channel timings are retained by the module adapter; simulation speed controls demo time.',(s,g)=>!s.scanPopup&&s.scanVisible&&s.scanState===2&&s.scanId>g.baseline.scanId,0);
function scan(){return entry('s18-scan','S18 · Scan Setup save',[
 open(),
 step('Change GROVE scan settings','',(s,g)=>s.scanPopup&&s.minInput===g.memory.min&&s.maxInput===g.memory.max&&s.vendor!==g.baseline.vendor),
 step('Save the scan timings','Press Save. The GROVE module must retain the new timings.',(s,g)=>!s.scanPopup&&s.min===g.memory.min&&s.max===g.memory.max&&s.vendor!==g.baseline.vendor),
 step('Reopen and verify settings','Open Scan Setup again. Check the new GROVE timings and changed Show Vendors switch.',(s,g)=>s.scanPopup&&s.minInput===g.memory.min&&s.maxInput===g.memory.max&&s.vendor!==g.baseline.vendor),
 freshScan()],{onStart(s,g){g.memory.min=s.min===200&&s.max===500?150:200;g.memory.max=g.memory.min===150?450:500;g.story.steps[1].instruction=`Set GROVE Min to ${g.memory.min} and Max to ${g.memory.max}. Toggle Show Vendors to the opposite of its value when this story started. Vendor changes apply immediately.`;}});}
function invalid(){return entry('s18-scan-invalid','S18 · Scan Setup validation and cancel',[
 open(),
 step('Try an invalid range','Set GROVE Min to 1500 and Max to 300, then press Save. The native form must show a validation error.',(s,g)=>s.scanPopup&&s.has('Grove min must be < max',true)&&s.min===g.baseline.min&&s.max===g.baseline.max),
 step('Cancel the invalid edit','Press Cancel. Timing values remain unchanged. Show Vendors is an immediate setting, so Cancel does not undo a vendor toggle.',(s,g)=>!s.scanPopup&&s.min===g.baseline.min&&s.max===g.baseline.max),
 step('Verify the original timings','Open Scan Setup again and inspect the original GROVE timings.',(s,g)=>s.scanPopup&&s.minInput===g.baseline.min&&s.maxInput===g.baseline.max),
 step('Return to Settings','Press Cancel to leave the settings popup.',s=>!s.scanPopup&&!!s.bound('/Scan Setup'))]);}
function red(enable){return entry('s18-red-'+(enable?'enable':'cancel'),'S18 · Red Team '+(enable?'accept':'cancel'),[
 step('Start with Red Team off','Open INTERNAL → Settings → Red Team. Turn the switch off if it is currently on.',s=>s.redPage&&!s.redTeam&&!s.disclaimer),
 step('Read the disclaimer','Turn Red Team on to open the native disclaimer.',s=>s.redPage&&s.disclaimer&&!s.redTeam),
 step(enable?'Accept the disclaimer':'Cancel the disclaimer',enable?'Press the native confirmation button.':'Press Cancel. Red Team must remain disabled.',s=>s.redPage&&!s.disclaimer&&s.redTeam===enable),
 step('Check the GROVE menu','Use Back and switch to GROVE. Inspect the '+(enable?'Attack / Attacks':'Test / Tests')+' tile names.',s=>s.redTeam===enable&&s.has(enable?'WiFi Scan\n& Attack':'WiFi Scan\n& Test')&&s.has(enable?'Global WiFi\nAttacks':'Global WiFi\nTests'),0),
 step('Check the Scan actions','Open WiFi Scan and wait for a fresh result list. Inspect the '+(enable?'Scan & Attack title and Deauth / Evil Twin actions.':'Scan & Test title. ARP and Nmap remain; Deauth and Evil Twin are absent.'),(s,g)=>s.redTeam===enable&&s.scanVisible&&s.scanState===2&&s.scanId>g.baseline.scanId&&s.has(enable?'Scan & Attack':'Scan & Test')&&s.has('ARP')&&s.has('Nmap')&&s.has('Deauth')===enable&&s.has('Evil Twin')===enable,0)]);}
export const settingsS18=[scan(),invalid(),red(false),red(true)];
