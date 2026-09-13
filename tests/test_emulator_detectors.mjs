import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
import { BrowserDevice } from '../tools/ui_emulator/model/bridge.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('deauth events cycle the scenario networks and drift RSSI within bounds',()=>{
  const d=new SimulatedDevice(seed);
  const first=d.deauthEvent('grove',0);
  assert.equal(first.ssid,seed.networks[0].ssid);
  assert.equal(first.bssid,seed.networks[0].bssid);
  assert.equal(first.channel,seed.networks[0].channel);
  // Cycles through every network by sequence.
  const seen=new Set();
  for(let i=0;i<seed.networks.length;i++)seen.add(d.deauthEvent('grove',i).bssid);
  assert.equal(seen.size,seed.networks.length);
  // RSSI stays in the clamped range.
  for(let i=0;i<40;i++){const r=d.deauthEvent('grove',i).rssi;assert.ok(r<=-30&&r>=-90);}
});

test('a disconnected module detects nothing',()=>{
  const d=new SimulatedDevice(seed);
  d.setModule('grove',{connected:false});
  assert.equal(d.deauthEvent('grove',0),null);
});

test('the bridge formats a firmware-shaped [DEAUTH] line and empty text when idle',()=>{
  const b=new BrowserDevice(seed);
  const line=b.deauthDetectorLine(0,0);
  assert.match(line,/^\[DEAUTH\] CH: \d+ \| AP: .+ \(.+\) \| RSSI: -?\d+$/);
  b.device.setModule('grove',{connected:false});
  assert.equal(b.deauthDetectorLine(0,0),'');
});

test('anti-surv followers cycle the BT devices and stop when disconnected',()=>{
  const d=new SimulatedDevice(seed);
  const f=d.antisurvFollower('grove',0);
  assert.ok(f&&f.mac);
  const macs=new Set();
  for(let i=0;i<(seed.bluetooth||[]).length;i++)macs.add(d.antisurvFollower('grove',i).mac);
  assert.equal(macs.size,(seed.bluetooth||[]).length);
  d.setModule('grove',{connected:false});
  assert.equal(d.antisurvFollower('grove',0),null);
});

test('the bridge formats a MAC|name follower line and picks a handshaker target',()=>{
  const b=new BrowserDevice(seed);
  assert.match(b.antisurvFollowerLine(0,0),/^([0-9A-Fa-f:]+)\|/);
  assert.equal(b.handshakerTarget(0),seed.networks[0].ssid);
  b.device.setModule('grove',{connected:false});
  assert.equal(b.antisurvFollowerLine(0,0),'');
});
