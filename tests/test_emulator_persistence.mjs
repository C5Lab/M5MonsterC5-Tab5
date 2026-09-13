import test from 'node:test';
import assert from 'node:assert/strict';
import { SettingsStore, SETTINGS_KEY } from '../tools/ui_emulator/web/persistence.mjs';

function storage(initial = {}) {
  const data = new Map(Object.entries(initial));
  return { getItem: key => data.get(key) ?? null,
    setItem: (key, value) => data.set(key, String(value)), removeItem: key => data.delete(key) };
}

test('transfer baud accepts only firmware choices, survives reload and resets',()=>{
 const disk=storage(),s=new SettingsStore(disk);
 for(const rate of [115200,230400,460800,921600,1000000,1500000,2000000,3000000,4000000]){
  s.set('ft_baud',rate);assert.equal(new SettingsStore(disk).get('ft_baud',460800),rate);
 }
 for(const rate of [0,115201,460800.5,'921600',5000000])assert.throws(()=>s.set('ft_baud',rate));
 s.reset();assert.equal(new SettingsStore(disk).get('ft_baud',460800),460800);
 const bad=new SettingsStore(storage({[SETTINGS_KEY]:JSON.stringify({version:1,values:{ft_baud:123456}})}));
 assert.equal(bad.get('ft_baud',460800),460800);assert.equal(bad.recovered,true);
});

test('settings survive reconstruction; reset restores defaults without clearing unrelated storage', () => {
  const disk = storage({unrelated: 'keep'});
  const settings = new SettingsStore(disk);
  settings.set('scr_bright', 23);
  settings.set('scr_rot', 3);
  const rebooted = new SettingsStore(disk);
  assert.equal(rebooted.get('scr_bright', 80), 23);
  assert.equal(rebooted.get('scr_rot', 0), 3);
  rebooted.reset();
  assert.equal(new SettingsStore(disk).get('scr_bright', 80), 80);
  assert.equal(disk.getItem('unrelated'), 'keep');
});

test('legacy settings migrate once and do not return after reset', () => {
  const disk = storage({'tab5-emulator-v1:scr_rot': '3', 'tab5-emulator-v1:scr_bright': '1'});
  const settings = new SettingsStore(disk);
  assert.equal(settings.get('scr_rot', 0), 3);
  assert.equal(settings.get('scr_bright', 80), 1);
  settings.reset();
  assert.equal(new SettingsStore(disk).get('scr_rot', 0), 0);
});

test('corrupt, future and invalid settings recover to safe defaults', () => {
  for (const raw of ['{', 'null', '{"version":99,"values":{"scr_rot":3}}',
    '{"version":1,"values":{"scr_rot":4,"scr_bright":0,"dark_mode":"1"}}']) {
    const settings = new SettingsStore(storage({[SETTINGS_KEY]: raw}));
    assert.equal(settings.get('scr_rot', 0), 0);
    assert.equal(settings.get('scr_bright', 80), 80);
    assert.equal(settings.get('dark_mode', 1), 1);
  }
});

test('denied storage and quota exhaustion keep settings usable for this session', () => {
  const denied = {getItem() {throw Error('denied');}, setItem() {throw Error('denied');}};
  for (const disk of [denied, {getItem: () => null, setItem() {throw Error('quota');}}]) {
    const settings = new SettingsStore(disk);
    settings.set('scr_bright', 32);
    assert.equal(settings.get('scr_bright', 80), 32);
    assert.equal(settings.mode, 'memory');
    settings.reset();
    assert.equal(settings.get('scr_bright', 80), 80);
  }
});

test('invalid writes cannot replace a valid value', () => {
  const settings = new SettingsStore(storage());
  settings.set('scr_bright', 42);
  for (const value of [0, 101, NaN, '40', 1.5]) assert.throws(() => settings.set('scr_bright', value));
  assert.throws(() => settings.set('__proto__', 1));
  assert.equal(settings.get('scr_bright', 80), 42);
});

test('existing Wardrive adapter settings remain writable and migrate', () => {
  const disk = storage({'tab5-emulator-v1:wd_wigle': '1'});
  const settings = new SettingsStore(disk);
  assert.equal(settings.get('wd_wigle', 0), 1);
  for (const key of ['wd_auto', 'wd_wigle', 'wd_wars', 'wd_archive', 'wd_power']) settings.set(key, 1);
  const next = new SettingsStore(disk);
  assert.equal(next.get('wd_power', 0), 1);
  assert.equal(next.get('wd_auto', 0), 1);
});

test('session fallback survives reboot and overrides stale disk after a failed reset write', () => {
  const disk = storage();
  const session = storage();
  new SettingsStore(disk, session).set('scr_bright', 23);
  disk.setItem = () => { throw Error('quota'); };
  const settings = new SettingsStore(disk, session);
  settings.set('scr_bright', 32);
  assert.equal(new SettingsStore(disk, session).get('scr_bright', 80), 32);
  settings.reset();
  assert.equal(new SettingsStore(disk, session).get('scr_bright', 80), 80);
});
