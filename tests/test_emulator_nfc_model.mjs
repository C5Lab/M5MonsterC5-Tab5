import test from 'node:test';
import assert from 'node:assert/strict';
import {NfcDevice} from '../tools/ui_emulator/model/nfc.mjs';

const fixtures = [
  {id:'ntag-demo', label:'NTAG Demo', type:'NTAG213', uid:'04 A1 B2 C3 D4 E5 F6', atqa:'00 44', sak:'00', data_bytes:144},
  {id:'classic-demo', label:'Classic Demo', type:'MIFARE Classic 1K', uid:'12 34 56 78', atqa:'00 04', sak:'08', data_bytes:1024},
];

test('probe and read return native NFC lines, then the second fixture on another read', () => {
  const model = new NfcDevice(fixtures);
  model.command(2, 'init_nfc');
  assert.deepEqual(model.drain(2), ['[NFC] detected', '[NFC] END']);
  model.command(2, 'nfc_read');
  const first = model.drain(2);
  assert.ok(first.includes('[NFC] type: NTAG213'));
  assert.ok(first.includes('[NFC] uid: 04 A1 B2 C3 D4 E5 F6'));
  assert.equal(first.at(-1), '[NFC] END');
  model.command(2, 'nfc_read');
  assert.ok(model.drain(2).includes('[NFC] type: MIFARE Classic 1K'));
});

test('save, list, load, rename, emulate and delete use volatile per-tab SD', () => {
  const model = new NfcDevice(fixtures);
  model.command(2, 'nfc_read'); model.drain(2);
  model.command(2, 'nfc_save DemoTag');
  assert.ok(model.drain(2).includes('[NFC] saved: /nfc/DemoTag.nfc'));
  model.command(2, 'nfc_list');
  assert.ok(model.drain(2).includes('0 DemoTag.nfc'));
  model.command(2, 'nfc_load 0');
  assert.ok(model.drain(2).includes('[NFC] loaded: /nfc/DemoTag.nfc'));
  model.command(2, 'start_nfc_emulate');
  assert.ok(model.drain(2).some(line => line.includes('full NTAG/Ultralight page data emulated')));
  assert.equal(model.state(2).emulating, true);
  model.command(2, 'stop');
  assert.equal(model.state(2).emulating, false);
  model.command(2, 'nfc_rename 0 Renamed');
  assert.ok(model.drain(2).includes('[NFC] renamed: /nfc/DemoTag.nfc -> /nfc/Renamed.nfc'));
  model.command(2, 'nfc_delete 0'); model.drain(2);
  model.command(2, 'nfc_list');
  assert.deepEqual(model.drain(2), ['[NFC] 0 card(s)', '[NFC] END']);
  model.command(1, 'nfc_list');
  assert.deepEqual(model.drain(1), ['[NFC] 0 card(s)', '[NFC] END']);
});

test('Classic emulation reports UID-only capability', () => {
  const model = new NfcDevice(fixtures);
  model.command(2, 'nfc_read'); model.drain(2);
  model.command(2, 'nfc_read'); model.drain(2);
  model.command(2, 'nfc_save Classic'); model.drain(2);
  model.command(2, 'nfc_load 0'); model.drain(2);
  model.command(2, 'start_nfc_emulate');
  assert.ok(model.drain(2).some(line => line.includes('UID/ATQA/SAK level only')));
});

test('missing SD rejects file operations and disconnected module rejects commands', () => {
  const model = new NfcDevice(fixtures);
  const availability = {connected:true, sdPresent:true};
  model.setAvailability(() => availability);
  model.command(2, 'nfc_read'); model.drain(2);
  availability.sdPresent = false;
  model.command(2, 'nfc_save DemoTag');
  assert.ok(model.drain(2).some(line => line.includes('SD missing')));
  assert.equal(model.state(2).sd.length, 0);
  availability.sdPresent = true;
  model.command(2, 'nfc_save DemoTag'); model.drain(2);
  assert.equal(model.state(2).sd.length, 1);
  availability.sdPresent = false;
  model.command(2, 'nfc_list');
  assert.deepEqual(model.drain(2), ['[NFC] SD not present', '[NFC] END']);
  model.command(2, 'nfc_rename 0 Denied');
  model.command(2, 'nfc_delete 0');
  model.command(2, 'nfc_load 0');
  assert.ok(model.drain(2).every(line => line.includes('failed') || line === '[NFC] END'));
  assert.equal(model.state(2).sd[0].name, 'DemoTag');
  availability.connected = false;
  model.command(2, 'init_nfc');
  assert.deepEqual(model.drain(2), ['[NFC] not detected', '[NFC] END']);
  model.command(2, 'nfc_read');
  assert.deepEqual(model.drain(2), ['[NFC] not detected', '[NFC] END']);
  assert.equal(model.state(2).nextRead, 1);
});

test('active NFC emulation ends when its module disconnects', () => {
  const model = new NfcDevice(fixtures);
  const availability = {connected:true, sdPresent:true};
  model.setAvailability(() => availability);
  model.command(2, 'nfc_read'); model.drain(2);
  model.command(2, 'nfc_save DemoTag'); model.drain(2);
  model.command(2, 'nfc_load 0'); model.drain(2);
  model.command(2, 'start_nfc_emulate'); model.drain(2);
  assert.equal(model.state(2).emulating, true);
  availability.connected = false;
  model.advance(16);
  assert.equal(model.state(2).emulating, false);
});
