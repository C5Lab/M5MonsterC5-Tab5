import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {BrowserDevice} from '../tools/ui_emulator/model/bridge.mjs';

const seed=JSON.parse(readFileSync(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url)));
const evil='/sdcard/lab/eviltwin.txt';
const portal='/sdcard/lab/portals.txt';
const bytes=text=>new TextEncoder().encode(text);

test('show_pass reads per-module volatile capture files and observes deletion',()=>{
  const bridge=new BrowserDevice(seed);
  assert.equal(bridge.compromisedText(0,'evil'),'');
  assert.equal(bridge.compromisedText(0,'portal'),'');
  bridge.device.commitFileChanges('grove',[
    {path:evil,bytes:bytes('"NEON-BAZAAR", "demo-only-pass"\n"LANTERN-LAB", "second-demo"\n')},
    {path:portal,bytes:bytes('"NEON-BAZAAR", "username=demo", "password=demo-only-pass"\n')},
  ]);
  assert.match(bridge.compromisedText(0,'evil'),/second-demo/);
  assert.match(bridge.compromisedText(0,'portal'),/username=demo/);
  assert.equal(bridge.compromisedText(2,'evil'),'');
  bridge.device.deleteFile('grove',evil);
  assert.equal(bridge.compromisedText(0,'evil'),'');
  assert.throws(()=>bridge.compromisedText(0,'unknown'),/Unknown compromised/);
});

test('show_pass is unavailable while its module is disconnected, then resumes',()=>{
  const bridge=new BrowserDevice(seed);
  bridge.device.commitFileChanges('grove',[
    {path:evil,bytes:bytes('"NEON-BAZAAR", "demo-only-pass"\n')},
    {path:portal,bytes:bytes('"NEON-BAZAAR", "password=demo-only-pass"\n')},
  ]);
  bridge.device.setModule('grove',{connected:false});
  assert.equal(bridge.compromisedText(0,'evil'),'');
  assert.equal(bridge.compromisedText(0,'portal'),'');
  assert.equal(bridge.device.files('grove').length,2);
  bridge.device.setModule('grove',{connected:true});
  assert.match(bridge.compromisedText(0,'evil'),/demo-only-pass/);
  assert.match(bridge.compromisedText(0,'portal'),/demo-only-pass/);
});

test('offline attack submission writes one demo record for each relevant page',()=>{
  const bridge=new BrowserDevice(seed),networkId=seed.networks[0].id;
  const evilJob=bridge.device.attackStart('grove','evil_twin',{networkIds:[networkId]});
  assert.equal(bridge.compromisedText(0,'evil'),'');
  bridge.device.advance(80000);
  assert.equal(bridge.device.job(evilJob).result.demo.stage,'submitted');
  assert.match(bridge.compromisedText(0,'evil'),/DEMO-only-2026!/);
  bridge.device.advance(10000);
  assert.equal(bridge.compromisedText(0,'evil').trim().split('\n').length,1);
  bridge.device.attackStop(evilJob);
  const portalJob=bridge.device.attackStart('internal','portal_demo',{networkIds:[networkId]});
  bridge.device.advance(80000);
  assert.equal(bridge.device.job(portalJob).result.demo.stage,'submitted');
  assert.match(bridge.compromisedText(3,'portal'),/password=DEMO-only-2026!/);
  assert.equal(bridge.compromisedText(0,'portal'),'');
  bridge.device.reset();
  assert.equal(bridge.compromisedText(0,'evil'),'');
  assert.equal(bridge.compromisedText(3,'portal'),'');
});
