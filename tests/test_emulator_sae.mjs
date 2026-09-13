import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
const seed = JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json', import.meta.url), 'utf8'));

test('SAE requires exactly one scenario target and rejects invalid selection without reserving radio', () => {
  const d = new SimulatedDevice(seed);
  for (const networkIds of [[], ['net-1', 'net-2'], ['missing']]) {
    assert.throws(() => d.attackStart('grove', 'sae_overflow', {networkIds}));
    assert.equal(d.snapshot('grove').active, null);
  }
  const id = d.attackStart('grove', 'sae_overflow', {networkIds: ['net-1']});
  assert.equal(d.job(id).operation, 'sae_overflow');
  assert.deepEqual(d.job(id).options.networkIds, ['net-1']);
  assert.throws(() => d.scan('grove'), /busy/);
  assert.equal(d.snapshot('mbus').active, null);
  d.advance(5000);
  assert.equal(d.job(id).state, 'running');
  assert.equal(d.job(id).result.simulated, true);
  assert.equal(d.attackStop(id), true);
  const stopped = d.job(id);
  d.advance(5000);
  assert.deepEqual(d.job(id), stopped);
  assert.equal(d.snapshot('grove').active, null);
  assert.deepEqual(d.files('grove'), []);
});

test('SAE cancellation, module disconnect and reset cannot resume old activity', () => {
  const d = new SimulatedDevice(seed);
  const start = () => d.attackStart('grove', 'sae_overflow', {networkIds: ['net-1']});
  const cancelled = start(); d.cancel(cancelled); d.advance(10000);
  assert.equal(d.job(cancelled).state, 'cancelled');
  const disconnected = start(); d.setModule('grove', {connected: false});
  assert.equal(d.job(disconnected).state, 'cancelled');
  assert.throws(start, /disconnected/);
  d.reset();
  assert.equal(d.job(disconnected), null);
  assert.ok(start() > disconnected);
});
