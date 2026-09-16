import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { BrowserDevice } from '../tools/ui_emulator/model/bridge.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
test('Observer discovers independently and its targets work without a WiFi scan',()=>{
  const b=new BrowserDevice(seed),d=b.device;
  d.observerStart('grove');
  assert.equal(d.observer('grove').networks.length,seed.networks.length);
  assert.ok(d.observer('grove').networks.some(n=>n.clients.length));
  assert.deepEqual(b.snapshot(0).networks,[]);assert.equal(b.snapshot(0).selected,null);
  assert.deepEqual(d.observer('mbus').networks,[]);
  b.select(0,seed.networks[0].bssid);
  const capture=d.capture('grove');d.advance(5000);
  assert.equal(d.job(capture).state,'completed');
  assert.deepEqual(b.snapshot(0).networks,[]);
});
test('C boundary scan rows preserve scenario identities and cancellation',()=>{
  const bridge=new BrowserDevice(seed);
  const job=bridge.scan(0);
  assert.equal(bridge.state(job),1);
  bridge.advance(seed.settings.scan_time_ms);
  assert.equal(bridge.state(job),2);
  assert.equal(bridge.rows(0).length,seed.networks.length);
  assert.ok(bridge.rows(0)[0].includes('"NEON-BAZAAR","","02:20:77:00:00:01"'));
  bridge.select(0,seed.networks[0].bssid);
  assert.equal(bridge.snapshot(0).selected,'net-1');
  const other=bridge.scan(2);bridge.cancel(other);bridge.advance(10000);
  assert.equal(bridge.state(other),3);
  assert.equal(bridge.rows(2).length,0);
  assert.equal(bridge.snapshot(0).selected,'net-1');
});
