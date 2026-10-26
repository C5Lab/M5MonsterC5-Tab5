import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { BrowserDevice } from '../tools/ui_emulator/model/bridge.mjs';

const seed = JSON.parse(readFileSync(new URL('../docs/ui-emulator/neon-district.seed.json', import.meta.url)));

test('Quick Scan emits deterministic scenario frequencies and a pass marker', () => {
  const radio = new BrowserDevice(seed).subghz;
  radio?.command(0, 'subghz_scanner dwell=80 edges=4 -80 fast');
  radio?.advance(1000);
  const lines = radio?.drain(0) ?? [];
  assert.ok(lines.some(line => line.includes('[SUBGHZ_SCAN_HIT] freq=433.92')));
  assert.ok(lines.some(line => line.includes('[SUBGHZ_SCAN_PASS]')));
});

test('Hunter presents a bounded set of captures during a long demo session', () => {
  const radio = new BrowserDevice(seed).subghz;
  radio.command(0, 'subghz_freq_analyzer -70 hunt timeout=2000');
  radio.advance(20_000);
  const captures = radio.drain(0).filter(line => line.startsWith('[SUBGHZ_RX]'));
  assert.equal(captures.length, 3);
  assert.equal(radio.state(0).memory.length, 3);
});

test('Save, rename and delete affect only volatile SD of the selected tab', () => {
  const radio = new BrowserDevice(seed).subghz;
  radio.command(2, 'subghz_rx rssi=-80');
  radio.advance(400);
  radio.command(2, 'subghz_save 1');
  radio.command(2, 'subghz_rename 1 New_Gate');
  radio.command(2, 'subghz_list sd');
  assert.ok(radio.drain(2).some(line => line.includes('[SUBGHZ_LIST] idx=1') && line.includes('name=New_Gate')));
  radio.command(0, 'subghz_list sd');
  assert.deepEqual(radio.drain(0), ['[SUBGHZ_LIST_END] source=sd count=0']);
  radio.command(2, 'subghz_delete 1');
  assert.equal(radio.state(2).sd.length, 0);
});

test('disconnected module stops activity and missing SD rejects file commands', () => {
  const radio = new BrowserDevice(seed).subghz;
  const availability = {connected:true, sdPresent:true};
  radio.setAvailability(() => availability);
  radio.command(2, 'subghz_rx rssi=-80');
  radio.advance(400);
  radio.drain(2);
  availability.sdPresent = false;
  radio.command(2, 'subghz_save 1');
  assert.ok(radio.drain(2).some(line => line.includes('[SUBGHZ_SAVE_ERR]') && line.includes('sd_missing')));
  assert.equal(radio.state(2).sd.length, 0);
  availability.sdPresent = true;
  radio.command(2, 'subghz_save 1');
  radio.drain(2);
  assert.equal(radio.state(2).sd.length, 1);
  availability.sdPresent = false;
  radio.command(2, 'subghz_list sd');
  assert.deepEqual(radio.drain(2), [
    '[SUBGHZ_LIST_ERR] reason=sd_missing', '[SUBGHZ_LIST_END] source=sd count=0',
  ]);
  radio.command(2, 'subghz_rename 1 Denied');
  radio.command(2, 'subghz_delete 1');
  assert.ok(radio.drain(2).every(line => line.includes('sd_missing')));
  assert.equal(radio.state(2).sd[0].label, 'Gate_433');
  availability.connected = false;
  radio.advance(1000);
  assert.equal(radio.state(2).mode, 'idle');
  radio.command(2, 'subghz_scanner');
  assert.equal(radio.state(2).mode, 'idle');
  assert.ok(radio.drain(2).includes('CC1101 NOT DETECTED'));
});
