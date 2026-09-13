import { SimulatedDevice } from './device.mjs';

const modules = {0:'grove', 2:'mbus', 3:'internal'};
const states = {running:1, completed:2, cancelled:3, failed:4};

/** Synchronous boundary used by the retained C interface on the browser thread. */
export class BrowserDevice {
  constructor(seed) { this.device=new SimulatedDevice(seed); }
  module(tab) {
    if (!modules[tab]) throw new Error('This module is not available in the scenario');
    return modules[tab];
  }
  scan(tab) { return this.device.scan(this.module(tab)); }
  cancel(id) { return this.device.cancel(id); }
  state(id) { return states[this.device.job(id)?.state] || 0; }
  advance(ms) { this.device.advance(ms); }
  snapshot(tab) { return this.device.snapshot(this.module(tab)); }
  rows(tab) {
    return this.snapshot(tab).networks.map(n =>
      [n.index,n.ssid,n.vendor,n.bssid,n.channel,n.security,n.rssi,n.band]
        .map(value => '"'+String(value)+'"').join(','));
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
  // The retained deauth_detector_task parses "[DEAUTH] CH: .. | AP: .. (..) |
  // RSSI: .." lines. The emulator intercepts that task and feeds one synthesized
  // line per detection tick, parsed by the production parse_deauth_line().
  deauthDetectorLine(tab,seq) {
    const e=this.device.deauthEvent(this.module(tab),seq);
    return e?`[DEAUTH] CH: ${e.channel} | AP: ${e.ssid} (${e.bssid}) | RSSI: ${e.rssi}`:'';
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
