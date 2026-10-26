import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const input={victimId:'net-1',upstreamId:'net-2',mirrorPass:'DEMO-only-2026!',upstreamPass:'private-demo-uplink'};

test('Rogue GITM validates same-channel victim and uplink, then finalizes a synthetic PCAP',()=>{
 const d=new SimulatedDevice(seed);
 const id=d.toolStart('grove','rogue_gitm',input);
 let job=d.job(id);
 assert.equal(job.state,'running');assert.equal(job.result.stage,'connecting');
 assert.equal(job.options.victimId,'net-1');assert.equal(job.options.upstreamId,'net-2');
 assert.equal(JSON.stringify(job).includes(input.mirrorPass),false);
 assert.equal(JSON.stringify(job).includes(input.upstreamPass),false);
 assert.throws(()=>d.scan('grove'),/busy/);
 d.advance(2000);assert.equal(d.job(id).result.stage,'mirror_ready');
 d.advance(3000);job=d.job(id);
 assert.equal(job.result.stage,'capturing');assert.equal(job.result.victimSsid,'NEON-BAZAAR');
 assert.equal(job.result.upstreamSsid,'GHOST-LINE');
 assert.ok(job.result.packets>0);
 assert.equal(d.toolStop(id),true);
 job=d.job(id);assert.equal(job.state,'completed');assert.equal(job.result.file,job.file);
 const file=d.files('grove').find(f=>f.path===job.file);
 assert.equal(file.kind,'rogue_gitm');assert.equal(file.networkId,'net-1');
 assert.equal(file.upstreamId,'net-2');
 assert.ok(d.readFile('grove',job.file).length>24);
 assert.equal(d.snapshot('grove').active,null);
 const frozen=d.job(id);d.advance(10000);assert.deepEqual(d.job(id),frozen);
});

test('stopping Rogue GITM before capture does not invent packets or a PCAP',()=>{
 for(const elapsed of [0,3000]){
  const d=new SimulatedDevice(seed),id=d.toolStart('grove','rogue_gitm',input);
  d.advance(elapsed);
  assert.equal(d.job(id).result.packets,0);
  assert.equal(d.toolStop(id),true);
  const job=d.job(id);
  assert.equal(job.state,'completed');
  assert.equal(job.result.stage,'stopped_before_capture');
  assert.equal(job.result.packets,0);
  assert.equal(job.file,undefined);
  assert.deepEqual(d.files('grove'),[]);
  assert.equal(d.snapshot('grove').active,null);
 }
});

test('Rogue GITM rejects invalid selections, input and missing storage without acquiring a module',()=>{
 const d=new SimulatedDevice(seed);
 for(const [field,value,pattern] of [
  ['victimId','net-3',/channel/],['upstreamId','net-3',/channel/],
  ['upstreamId','net-1',/different/],['mirrorPass','short',/8-63/],
  ['mirrorPass','a\"password',/quote/],['upstreamPass','a\"password',/quote/]
 ])assert.throws(()=>d.toolStart('grove','rogue_gitm',{...input,[field]:value}),pattern);
 assert.equal(d.snapshot('grove').active,null);
 d.setModule('grove',{sdPresent:false});
 assert.throws(()=>d.toolStart('grove','rogue_gitm',input),/SD/);
 d.setModule('grove',{sdPresent:true});
 const job=d.toolStart('grove','rogue_gitm',input);
 d.setModule('grove',{sdPresent:false});
 assert.equal(d.toolStop(job),true);assert.equal(d.job(job).state,'failed');
 assert.deepEqual(d.files('grove'),[]);
});

test('Rogue GITM cancel and module isolation are deterministic',()=>{
 const d=new SimulatedDevice(seed),a=d.toolStart('grove','rogue_gitm',input);
 const b=d.toolStart('mbus','rogue_gitm',input);
 d.advance(6000);d.setModule('grove',{connected:false});
 assert.equal(d.job(a).state,'cancelled');assert.equal(d.job(b).state,'running');
 assert.equal(d.toolStop(b),true);assert.equal(d.job(b).state,'completed');
 assert.deepEqual(d.files('grove'),[]);assert.equal(d.files('mbus').length,1);
});

test('Hidden scenario victim requires an explicit locally resolved SSID',()=>{
 const hidden=structuredClone(seed);hidden.networks[0].ssid='';
 const d=new SimulatedDevice(hidden);
 assert.throws(()=>d.toolStart('grove','rogue_gitm',input),/resolve/);
 const id=d.toolStart('grove','rogue_gitm',{...input,resolvedVictimSsid:'Resolved Demo'});
 assert.equal(d.job(id).result.victimSsid,'Resolved Demo');
});
