/** JanOS-style NFC command replies backed by cards and session-only virtual SD. */
export class NfcDevice {
  constructor(cards) {
    this.cards = structuredClone(cards);
    this.availability = () => ({connected: true, sdPresent: true});
    this.tabs = Array.from({length: 4}, () => ({
      replies: [], sd: [], nextRead: 0, lastRead: null, loaded: null, emulating: false,
    }));
  }

  state(tab) { return this.tabs[tab]; }
  setAvailability(callback) { this.availability = callback; }
  flush(tab) { this.state(tab).replies.length = 0; }
  read(tab) { return this.state(tab).replies.shift() ?? ''; }
  drain(tab) { const lines = this.state(tab).replies.slice(); this.flush(tab); return lines; }
  reply(tab, ...lines) { this.state(tab).replies.push(...lines, '[NFC] END'); }

  cardLines(card) {
    return [
      `[NFC] type: ${card.type}`,
      `[NFC] uid: ${card.uid}`,
      `[NFC] atqa: ${card.atqa}  sak: ${card.sak}`,
      `[NFC] data: ${card.data_bytes} bytes, ${Math.ceil(card.data_bytes / 4)} blocks x 4`,
    ];
  }

  command(tab, command) {
    const s = this.state(tab);
    const [head, ...args] = command.trim().split(/\s+/);
    if (head === 'stop') { s.emulating = false; return; }
    const available = this.availability(tab);
    if (!available.connected) {
      s.emulating = false;
      this.reply(tab, '[NFC] not detected');
      return;
    }
    if (!available.sdPresent) {
      if (head === 'nfc_save') { this.reply(tab, '[NFC] save failed: SD missing'); return; }
      if (head === 'nfc_list') { this.reply(tab, '[NFC] SD not present'); return; }
      if (head === 'nfc_load') { this.reply(tab, '[NFC] load failed: SD missing'); return; }
      if (head === 'nfc_rename') { this.reply(tab, '[NFC] rename failed: SD missing'); return; }
      if (head === 'nfc_delete') { this.reply(tab, '[NFC] delete failed: SD missing'); return; }
    }
    if (head === 'init_nfc') { this.reply(tab, '[NFC] detected'); return; }
    if (head === 'nfc_read') {
      const card = this.cards[s.nextRead++ % this.cards.length];
      s.lastRead = card ? structuredClone(card) : null;
      this.reply(tab, ...(card ? ['[NFC] present a card', ...this.cardLines(card)] : ['[NFC] no card detected']));
      return;
    }
    if (head === 'nfc_save') {
      const name = args[0];
      if (!s.lastRead) { this.reply(tab, '[NFC] nothing to save'); return; }
      if (!name || !/^[A-Za-z0-9_.-]+$/.test(name)) { this.reply(tab, '[NFC] save failed'); return; }
      const path = `/nfc/${name}.nfc`;
      s.sd.push({...structuredClone(s.lastRead), name, path});
      this.reply(tab, `[NFC] saved: ${path}`);
      return;
    }
    if (head === 'nfc_list') {
      this.reply(tab, ...s.sd.map((card, idx) => `${idx} ${card.name}.nfc`), `[NFC] ${s.sd.length} card(s)`);
      return;
    }
    if (head === 'nfc_load') {
      const card = s.sd[Number(args[0])];
      if (!card) { s.loaded = null; this.reply(tab, '[NFC] load failed'); return; }
      s.loaded = card;
      this.reply(tab, `[NFC] loaded: ${card.path}`, ...this.cardLines(card));
      return;
    }
    if (head === 'nfc_rename') {
      const card = s.sd[Number(args[0])];
      const name = args[1];
      if (!card || !name || !/^[A-Za-z0-9_.-]+$/.test(name)) { this.reply(tab, '[NFC] rename failed'); return; }
      const oldPath = card.path;
      card.name = name;
      card.path = `/nfc/${name}.nfc`;
      this.reply(tab, `[NFC] renamed: ${oldPath} -> ${card.path}`);
      return;
    }
    if (head === 'nfc_delete') {
      const idx = Number(args[0]);
      if (!Number.isInteger(idx) || idx < 0 || idx >= s.sd.length) { this.reply(tab, '[NFC] not found'); return; }
      const [card] = s.sd.splice(idx, 1);
      if (s.loaded === card) s.loaded = null;
      this.reply(tab, `[NFC] deleted: ${card.path}`);
      return;
    }
    if (head === 'start_nfc_emulate') {
      if (!s.loaded) { this.reply(tab, '[NFC] no card loaded'); return; }
      s.emulating = true;
      const full = /NTAG|Ultralight/.test(s.loaded.type);
      this.reply(tab, `[NFC] emulating ${s.loaded.type} ${s.loaded.uid}. Use stop to exit`,
        full ? '[NFC] full NTAG/Ultralight page data emulated' : '[NFC] UID/ATQA/SAK level only');
      return;
    }
    this.reply(tab, '[NFC] unknown command');
  }

  advance(_ms) {
    for (let tab = 0; tab < this.tabs.length; tab++)
      if (!this.availability(tab).connected) this.state(tab).emulating = false;
  }
}
