/** Native Analyzer guide; progress comes from committed C snapshot and LVGL view. */
const steps = [
  {key: 'open', title: 'Open Wi-Fi Analyzer on GROVE',
    instruction: 'On GROVE, open WiFi Analyzer. This demo uses scenario networks, not a radio.'},
  {key: 'scan', title: 'Scan synthetic networks',
    instruction: 'Press Scan and wait for a complete WFA/1 snapshot. The previous snapshot does not count.'},
  {key: 'select', title: 'Inspect NEON-BAZAAR',
    instruction: 'Tap the NEON-BAZAAR access point row to reveal its native details.'},
  {key: 'propose', title: 'Propose a channel',
    instruction: 'Tap Propose channel to view the native 20 MHz ranking and its coverage limits.'},
  {key: 'back', title: 'Return to GROVE',
    instruction: 'Use the native Back button to return to GROVE.'},
];

export class AnalyzerGuide {
  story = {id: 'analyzer', steps, completionTitle: 'Analyzer story complete',
    completion: 'You scanned scenario networks, inspected an access point and reviewed the channel proposal.'};
  epoch = 0; active = false; complete = false; step = 0; hint = '';
  tab = 3; visible = true; floor = 0; scan = 0;
  start(snapshot = {}) {
    this.epoch++;
    this.active = true;
    this.complete = false;
    this.step = 0;
    this.hint = '';
    this.floor = snapshot.scan || 0;
    this.scan = 0;
  }
  leave() {this.epoch++; this.active = false;}
  setContext({tab, visible}) {this.tab = tab; this.visible = visible;}
  accept(snapshot) {
    if (!this.active || this.complete || !this.visible || this.tab !== 0 || snapshot.tab !== 0) return;
    const key = steps[this.step].key;
    let ready = false;
    if (key === 'open') ready = snapshot.page;
    else if (key === 'scan') {
      ready = snapshot.page && snapshot.state === 2 && snapshot.scan > this.floor;
      if (ready) this.scan = snapshot.scan;
      else if (snapshot.state === 3 || snapshot.state === 4)
        this.hint = 'The scan stopped or failed. Press Scan for a complete synthetic snapshot.';
    } else if (key === 'select') ready = snapshot.page && snapshot.scan === this.scan && snapshot.selected;
    else if (key === 'propose') ready = snapshot.page && snapshot.scan === this.scan && snapshot.proposal;
    else if (key === 'back') ready = snapshot.home && !snapshot.page;
    if (ready) {
      this.step++;
      this.hint = '';
      this.complete = this.step === steps.length;
    }
  }
}

function analyzerEvidence(module) {
  module._emu_inspect();
  const labels = globalThis.emulatorObjects.map(object => object.text);
  const page = labels.includes('Wi-Fi Analyzer');
  return {
    tab: module._emu_current_tab(), page,
    home: labels.includes('WiFi\nAnalyzer'),
    scan: module._emu_analyzer_scan(0),
    state: module._emu_analyzer_state(0),
    selected: page && labels.some(text => text.startsWith('Details below: NEON-BAZAAR')) && labels.includes('Network details'),
    proposal: page && labels.includes('Channel proposal / 20 MHz'),
  };
}

export const analyzerStories = [{id: 'analyzer', label: 'Wi-Fi Analyzer',
  create: () => new AnalyzerGuide(), read: analyzerEvidence}];
