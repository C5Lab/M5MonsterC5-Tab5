export const SETTINGS_KEY = 'tab5-emulator:settings';
const ranges = Object.freeze({rtc_offset: [-8000000000000, 8000000000000], clock_24h: [0, 1], clock_dst: [0, 1], clock_show: [0, 1],
  ft_baud: [115200, 4000000], scr_rot: [0, 3], scr_bright: [1, 100], scr_timeout: [0, 4],
  red_team: [0, 1], dark_mode: [0, 1], dashboard: [0, 1], boot_sound: [0, 3], alert_sound: [0, 1],
  wd_auto: [0, 1], wd_wigle: [0, 1], wd_wars: [0, 1], wd_archive: [0, 1], wd_power: [0, 1]});
const valid = (key, value) => Object.hasOwn(ranges, key) && Number.isInteger(value)
  && value >= ranges[key][0] && value <= ranges[key][1]
  && (key !== 'ft_baud' || [115200,230400,460800,921600,1000000,1500000,2000000,3000000,4000000].includes(value));

// One versioned record, written atomically. An empty record after reset prevents
// old per-key values from being imported again. Never clear other applications.
export class SettingsStore {
  #storage;
  #session;
  #values = {};
  mode = 'persistent';
  recovered = false;

  constructor(storage, session) {
    this.#storage = storage;
    this.#session = session;
    try {
      let raw = null;
      try { raw = session?.getItem(SETTINGS_KEY) ?? null; } catch { /* Memory fallback. */ }
      if (raw === null) {
        try { raw = storage?.getItem(SETTINGS_KEY) ?? null; } catch { this.mode = 'session'; }
      }
      if (raw === null) {
        for (const key of Object.keys(ranges)) {
          let legacy = null;
          try { legacy = storage?.getItem('tab5-emulator-v1:' + key) ?? null; } catch { /* No legacy storage. */ }
          if (legacy !== null && /^\d+$/.test(legacy) && valid(key, Number(legacy)))
            this.#values[key] = Number(legacy);
        }
      } else {
        let record;
        try { record = JSON.parse(raw); } catch { /* Recover below. */ }
        if (record?.version === 1 && record.values && typeof record.values === 'object' && !Array.isArray(record.values)) {
          for (const [key, value] of Object.entries(record.values)) {
            if (valid(key, value)) this.#values[key] = value;
            else this.recovered = true;
          }
        } else this.recovered = true;
      }
      this.#save();
    } catch { this.mode = 'session'; }
  }

  get(key, fallback) { return Object.hasOwn(this.#values, key) ? this.#values[key] : fallback; }
  set(key, value) {
    if (!valid(key, value)) throw new Error('Invalid emulator setting: ' + key);
    this.#values[key] = value;
    this.#save();
  }
  reset() { this.#values = {}; this.#save(); }
  #save() {
    const raw = JSON.stringify({version: 1, values: this.#values});
    try {
      this.#storage.setItem(SETTINGS_KEY, raw);
      this.mode = 'persistent';
      try { this.#session?.removeItem(SETTINGS_KEY); } catch { /* Storage may be disabled. */ }
    } catch {
      this.mode = 'session';
      try { this.#session.setItem(SETTINGS_KEY, raw); } catch { this.mode = 'memory'; }
    }
  }
}
