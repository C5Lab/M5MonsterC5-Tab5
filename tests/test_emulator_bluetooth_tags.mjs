import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';

const seed=JSON.parse(readFileSync(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url)));

test('AirTag detections follow scenario timing and disconnect',()=>{
  const device=new SimulatedDevice(seed);
  assert.deepEqual(device.bluetoothTagCounts('grove',0),{airtags:0,smarttags:0});
  assert.deepEqual(device.bluetoothTagCounts('grove',500),{airtags:1,smarttags:0});
  assert.deepEqual(device.bluetoothTagCounts('grove',800),{airtags:1,smarttags:1});
  assert.deepEqual(device.bluetoothTagCounts('grove',1200),{airtags:2,smarttags:1});
  device.setModule('grove',{connected:false});
  assert.deepEqual(device.bluetoothTagCounts('grove',1200),{airtags:0,smarttags:0});
});
