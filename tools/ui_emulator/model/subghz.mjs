/** Deterministic command/reply model for the separate virtual Sub-GHz radio. */
export class SubghzRadio {
  constructor(signals) {
    this.signals = structuredClone(signals);
    this.availability = () => ({connected: true, sdPresent: true});
    this.tabs = Array.from({length: 4}, () => ({
      mode: 'idle', frequency: 433.92, correction: 0, elapsed: 0,
      cursor: 0, nextIndex: 1, captureCount: 0, memory: [], sd: [], replies: [], raw: false,
    }));
  }

  state(tab) { return this.tabs[tab]; }
  setAvailability(callback) { this.availability = callback; }
  flush(tab) { this.state(tab).replies.length = 0; }
  read(tab) { return this.state(tab).replies.shift() ?? ''; }
  drain(tab) { const replies = this.state(tab).replies.slice(); this.flush(tab); return replies; }
  emit(tab, line) { this.state(tab).replies.push(line); }

  signalLine(tag, signal) {
    const frequency = (signal.frequency_hz / 1e6).toFixed(2);
    return `[${tag}] idx=${signal.idx} type=${signal.protocol} freq=${frequency} bits=24 serial=${signal.serial} btn=1 proto=${signal.protocol} mf=OOK cnt=1 name=${signal.label}`;
  }

  command(tab, command) {
    const s = this.state(tab);
    const parts = command.trim().split(/\s+/);
    const head = parts[0];
    if (head === 'subghz_stop') { s.mode = 'idle'; s.elapsed = 0; return; }
    const available = this.availability(tab);
    if (!available.connected) {
      s.mode = 'idle'; s.elapsed = 0;
      this.emit(tab, 'CC1101 NOT DETECTED');
      if (head === 'subghz_list')
        this.emit(tab, `[SUBGHZ_LIST_END] source=${parts[1]} count=0`);
      return;
    }
    if (!available.sdPresent) {
      if (head === 'subghz_save') { this.emit(tab, '[SUBGHZ_SAVE_ERR] reason=sd_missing'); return; }
      if (head === 'subghz_rename') { this.emit(tab, '[SUBGHZ_RENAME_ERR] reason=sd_missing'); return; }
      if (head === 'subghz_delete') { this.emit(tab, '[SUBGHZ_DELETE_ERR] reason=sd_missing'); return; }
      if (head === 'subghz_tx' && parts[2] === 'sd') {
        this.emit(tab, '[SUBGHZ_TX_ERR] reason=sd_missing'); return;
      }
      if (head === 'subghz_list' && parts[1] === 'sd') {
        this.emit(tab, '[SUBGHZ_LIST_ERR] reason=sd_missing');
        this.emit(tab, '[SUBGHZ_LIST_END] source=sd count=0');
        return;
      }
    }
    if (head === 'subghz_freq') { const value = Number(parts[1]); if (Number.isFinite(value)) s.frequency = value; return; }
    if (head === 'subghz_scanner') { s.mode = 'scanner'; s.elapsed = 0; s.cursor = 0; return; }
    if (head === 'subghz_freq_analyzer') { s.mode = 'hunter'; s.elapsed = 0; s.cursor = 0; s.captureCount = 0; s.raw = parts.includes('raw'); this.emit(tab, '[SUBGHZ_FA_START]'); return; }
    if (head === 'subghz_rx') { s.mode = 'listen'; s.elapsed = 0; s.cursor = 0; s.captureCount = 0; s.raw = parts.includes('raw'); this.emit(tab, 'CC1101 initialized'); return; }
    if (head === 'subghz_weather') { s.mode = 'weather'; s.elapsed = 0; s.cursor = 0; this.emit(tab, '[SUBGHZ_WEATHER_START] freq=433.92'); return; }
    if (head === 'subghz_jam') { s.mode = 'jammer'; this.emit(tab, 'SubGHz jammer ACTIVE'); return; }
    if (head === 'subghz_get_freq_correction') { this.emit(tab, `[SUBGHZ_FREQ_CORRECTION] ${s.correction.toFixed(2)}`); return; }
    if (head === 'subghz_set_freq_correction') { const value = Number(parts[1]); if (Number.isFinite(value) && value >= -5 && value <= 5) s.correction = value; return; }
    if (head === 'subghz_tx') { this.emit(tab, `[SUBGHZ_TX] idx=${Number(parts[1]) || 0}`); return; }
    if (head === 'subghz_save') {
      const signal = s.memory.find(item => item.idx === Number(parts[1]));
      if (!signal) { this.emit(tab, '[SUBGHZ_SAVE_ERR] reason=not_found'); return; }
      const saved = {...signal, idx: s.sd.length ? Math.max(...s.sd.map(item => item.idx)) + 1 : 1};
      s.sd.push(saved); this.emit(tab, `[SUBGHZ_SAVE] idx=${saved.idx} name=${saved.label}`); return;
    }
    if (head === 'subghz_list') {
      const source = parts[1];
      for (const signal of source === 'sd' ? s.sd : s.memory) this.emit(tab, this.signalLine('SUBGHZ_LIST', signal));
      this.emit(tab, `[SUBGHZ_LIST_END] source=${source} count=${source === 'sd' ? s.sd.length : s.memory.length}`);
      return;
    }
    if (head === 'subghz_rename') {
      const signal = s.sd.find(item => item.idx === Number(parts[1]));
      if (!signal) { this.emit(tab, '[SUBGHZ_RENAME_ERR] reason=not_found'); return; }
      signal.label = parts.slice(2).join('_').replace(/[^A-Za-z0-9_-]/g, '').slice(0, 48) || signal.label;
      this.emit(tab, `[SUBGHZ_RENAME] idx=${signal.idx} name=${signal.label}`); return;
    }
    if (head === 'subghz_delete') {
      const at = s.sd.findIndex(item => item.idx === Number(parts[1]));
      if (at < 0) { this.emit(tab, '[SUBGHZ_DELETE_ERR] reason=not_found'); return; }
      s.sd.splice(at, 1); this.emit(tab, `[SUBGHZ_DELETE] idx=${parts[1]}`);
    }
  }

  advance(ms) {
    for (let tab = 0; tab < this.tabs.length; tab++) {
      const s = this.state(tab);
      if (!this.availability(tab).connected) { s.mode = 'idle'; s.elapsed = 0; continue; }
      if (!['scanner', 'hunter', 'listen', 'weather'].includes(s.mode)) continue;
      s.elapsed += ms;
      while (s.elapsed >= 400) {
        s.elapsed -= 400;
        const fixture = this.signals[s.cursor++ % this.signals.length];
        if (!fixture) break;
        if (s.mode === 'scanner') {
          this.emit(tab, `[SUBGHZ_SCAN_HIT] freq=${(fixture.frequency_hz / 1e6).toFixed(2)} edges=8 rssi=${fixture.rssi}`);
          this.emit(tab, '[SUBGHZ_SCAN_PASS]');
        } else if (s.mode === 'weather') {
          this.emit(tab, `[SUBGHZ_WEATHER] proto=${fixture.protocol} id=0xC5${((s.cursor - 1) % this.signals.length + 16).toString(16).toUpperCase()} ch=1 temp=21.5 hum=48 batt=ok`);
        } else {
          if (s.captureCount >= this.signals.length) continue;
          s.captureCount++;
          const signal = {...fixture, idx: s.nextIndex++, serial: `0x${(0xC50000 + s.nextIndex).toString(16).toUpperCase()}`};
          s.memory.push(signal);
          if (s.memory.length > 64) s.memory.shift();
          if (s.mode === 'hunter') this.emit(tab, '[SUBGHZ_FA] hunt capture');
          this.emit(tab, this.signalLine(s.raw ? 'SUBGHZ_RAW' : 'SUBGHZ_RX', signal));
        }
      }
    }
  }
}
