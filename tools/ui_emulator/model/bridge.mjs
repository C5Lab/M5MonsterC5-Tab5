import { SimulatedDevice } from './device.mjs';
import { SubghzRadio } from './subghz.mjs';
import { NfcDevice } from './nfc.mjs';

const modules = {0:'grove', 1:'usb', 2:'mbus', 3:'internal'};
const states = {running:1, completed:2, cancelled:3, failed:4};

/** Synchronous boundary used by the retained C interface on the browser thread. */
export class BrowserDevice {
  constructor(seed) {
    this.device=new SimulatedDevice(seed);
    this.subghz=new SubghzRadio(seed.subghz_signals);
    this.nfc=new NfcDevice(seed.nfc_cards);
    this.detectorPending=new Map();
  }
  module(tab) {
    if (!modules[tab]) throw new Error('This module is not available in the scenario');
    return modules[tab];
  }
  scan(tab) { return this.device.scan(this.module(tab)); }
  cancel(id) { return this.device.cancel(id); }
  state(id) { return states[this.device.job(id)?.state] || 0; }
  advance(ms) { this.device.advance(ms); this.subghz.advance(ms); this.nfc.advance(ms); }
  snapshot(tab) { return this.device.snapshot(this.module(tab)); }
  rows(tab) {
    return this.device.scanCsvRows(this.module(tab));
  }
  select(tab,bssid) {
    const id=[...this.snapshot(tab).networks,...this.device.observer(this.module(tab)).networks].find(n=>n.bssid===bssid)?.id;
    if (!id) throw new Error('Selected network has not been discovered');
    this.device.select(this.module(tab),id);
  }
  // Retained scan_bt handler drains this text through transport_read_bytes and
  // parses it with the production parse_bt_device_line(). The Summary line ends
  // the read and carries no RSSI, so the parser skips it.
  bluetoothScanText(tab) {
    const list=this.device.bluetooth(this.module(tab));
    const lines=list.map(d=>`Device: ${d.mac} RSSI:${d.rssi}`+(d.name?` Name: ${d.name}`:''));
    lines.push(`Summary: ${list.length} devices`);
    return lines.join('\n')+'\n';
  }
  bluetoothLocatorRssi(tab,mac) {
    return this.device.bluetoothRssi(this.module(tab),mac);
  }
  bluetoothTagCounts(tab,elapsedMs) {
    return this.device.bluetoothTagCounts(this.module(tab),elapsedMs);
  }
  // The native Compromised Data pages issue show_pass and parse the returned
  // lines. Keep their response in the module's volatile SD model, so deletion
  // and a second module are visible on the next visit.
  compromisedText(tab,kind) {
    const path={evil:'/sdcard/lab/eviltwin.txt',portal:'/sdcard/lab/portals.txt'}[kind];
    if(!path)throw new Error('Unknown compromised data kind');
    const module=this.module(tab);
    const status=this.device.systemStatus(module);
    if(!status.connected||!status.sdPresent)return '';
    try{return new TextDecoder().decode(this.device.readFile(module,path));}
    catch(error){if(error.message==='File not found')return '';throw error;}
  }
  detectorWrite(tab,command) {
    const module=this.module(tab);
    const cmd=String(command).trim();
    if(cmd==='stop'||cmd==='scan_networks'||cmd.startsWith('deauth_detector'))
      this.detectorPending.delete(module);
    return this.device.detectorCommand(module,command);
  }
  detectorRead(tab) {
    return this.device.detectorRead(this.module(tab));
  }
  detectorHasPendingObservation(tab) {
    return (this.detectorPending.get(this.module(tab))?.length??0)>0;
  }
  // The retained parser consumes one JanOS-shaped observation per tick.
  deauthDetectorLine(tab,seq) {
    const module=this.module(tab);
    const status=this.device.detectorStatus(module);
    if(!status.running){this.detectorPending.delete(module);return '';}
    const pending=this.detectorPending.get(module);
    if(pending?.length)return pending.shift();
    const e=this.device.deauthEvent(module,seq);
    if(!e)return '';
    let line='';
    if(e.type==='csa'){
      const to=e.channel<=11?e.channel+5:e.channel<165?e.channel+4:149;
      line=`[CSA] RX: ${e.channel} | TO: ${to} | BSSID: ${e.bssid} | TX: 02:11:22:33:44:55 | RSSI: ${e.rssi} | VIA: beacon | COUNT: 3 | MODE: 1 | CLASS: - | SSID: ${e.ssid}`;
    } else if(status.details){
      const ssid=String(e.ssid||'<Unknown>').slice(0,32).replace(/[^\x20-\x7E]|\|/g,'?');
      line=`[DEAUTH_V2] CH: ${e.channel} | BSSID: ${e.bssid} | TX: ${e.tx} | DA: ${e.da} | RSSI: ${e.rssi} | REASON: ${e.reason??'-'} | SSID: ${ssid}`;
    } else line=`[DEAUTH] CH: ${e.channel} | AP: ${e.ssid} (${e.bssid}) | RSSI: ${e.rssi}`;
    const marker=this.device.detectorTraceForEvent(module,e);
    const defence=this.device.detectorDefenceForEvent(module,{...e,sta:e.sta??e.da});
    const forensic=this.device.detectorForensicForEvent(module,e,seq);
    const forensicRecords=forensic?[forensic]:[];
    if(marker){this.detectorPending.set(module,[...defence,...forensicRecords,line]);return marker;}
    if(defence.length){
      this.detectorPending.set(module,[...defence.slice(1),...forensicRecords,line]);
      return defence[0];
    }
    if(forensic){this.detectorPending.set(module,[line]);return forensic;}
    return line;
  }
  // "MAC|name" for one flagged follower, or '' when nothing is detected.
  antisurvFollowerLine(tab,seq) {
    const f=this.device.antisurvFollower(this.module(tab),seq);
    return f?`${f.mac}|${f.name}`:'';
  }
  handshakerTarget(tab) {
    return this.device.handshakerTarget(this.module(tab));
  }
}
