import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';

const seed = JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json', import.meta.url), 'utf8'));
const request = {band: 'both', profile: 'quick', limit: 64, channels: ''};

test('analyzer produces a complete deterministic WFA/1 transaction from scenario networks', () => {
  const device = new SimulatedDevice(seed);
  device.advance(33.3);
  const id = device.analyzerStart('grove', request);
  assert.equal(device.job(id).kind, 'analyzer');
  assert.throws(() => device.scan('grove'), /busy/);
  device.advance(2000);
  const lines = device.job(id).result.lines.map(line => {
    const match = /^\[(WFACTL1|WFA1)\] (.*)\n$/.exec(line);
    assert.ok(match, line);
    return JSON.parse(match[2]);
  });
  assert.equal(lines[0].type, 'caps');
  assert.equal(lines[1].type, 'begin');
  assert.ok(Number.isInteger(lines[1].started_ms), 'WFA/1 started_ms must be an integer');
  assert.equal(lines.at(-1).type, 'end');
  assert.equal(lines.at(-1).status, 'ok');
  assert.equal(lines.at(-1).returned, seed.networks.length);
  assert.equal(lines.filter(line => line.type === 'ap').length, seed.networks.length);
  assert.equal(lines.find(line => line.type === 'ap').bssid, seed.networks[0].bssid);
  assert.equal(device.snapshot('grove').active, null);
});

test('analyzer honors acquisition band and channels, supports cancellation and per-module independence', () => {
  const device = new SimulatedDevice(seed);
  const grove = device.analyzerStart('grove', {...request, band: '5', channels: '36,44'});
  const mbus = device.analyzerStart('mbus', request);
  assert.equal(device.cancel(grove), true);
  device.advance(2000);
  assert.equal(device.job(grove).state, 'cancelled');
  assert.equal(device.job(mbus).state, 'completed');
  const lines = device.job(mbus).result.lines.map(line => JSON.parse(line.slice(line.indexOf('{'))));
  assert.equal(lines.filter(line => line.type === 'ap').length, seed.networks.length);
  const limited = device.analyzerStart('grove', {...request, band: '5', channels: '36,44'});
  device.advance(2000);
  const only = device.job(limited).result.lines.map(line => JSON.parse(line.slice(line.indexOf('{'))));
  assert.deepEqual(only.filter(line => line.type === 'ap').map(line => line.primary), [36, 44]);
  assert.deepEqual(only.find(line => line.type === 'begin').channels, [36, 44]);
  assert.throws(() => device.analyzerStart('grove', {...request, channels: '36,36'}), /channel/i);
});
