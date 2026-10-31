import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
import { syntheticPcap } from '../tools/ui_emulator/model/pcap.mjs';

const seed = JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json', import.meta.url), 'utf8'));

test('compromised PCAP lists keep Handshakes separate and use firmware directory priority',()=>{
  const device=new SimulatedDevice(seed),bytes=new Uint8Array([1]);
  device.commitFileChanges('grove',[
    {path:'/sdcard/lab/handshakes/canonical.pcap',bytes},
    {path:'/sdcard/handshakes/legacy.pcap',bytes},
    {path:'/sdcard/lab/handshakes/ignored.pcapng',bytes},
    {path:'/sdcard/lab/pcaps/grove/general.pcap',bytes}
  ]);
  assert.deepEqual(device.compromisedPcapFiles('grove','handshake').map(f=>f.path),
    ['/sdcard/lab/handshakes/canonical.pcap']);
  assert.deepEqual(device.compromisedPcapFiles('grove','pcap').map(f=>f.path),
    ['/sdcard/lab/pcaps/grove/general.pcap']);
  device.deleteFile('grove','/sdcard/lab/handshakes/canonical.pcap');
  assert.deepEqual(device.compromisedPcapFiles('grove','handshake').map(f=>f.path),
    ['/sdcard/handshakes/legacy.pcap']);
});

test('module captures have distinct paths when copied to the shared analysis filesystem',()=>{
  const device=new SimulatedDevice(seed);
  for(const id of ['grove','mbus']) {
    device.scan(id);device.advance(seed.settings.scan_time_ms);device.select(id,'net-1');
    device.capture(id);device.advance(2000);
  }
  assert.notEqual(device.files('grove')[0].path,device.files('mbus')[0].path);
});

test('OTA variant and explicit release choices change only the simulated module',()=>{
  const rfSeed=structuredClone(seed);
  rfSeed.modules.find(module=>module.id==='mbus').capabilities.push('subghz');
  const device=new SimulatedDevice(rfSeed);
  assert.deepEqual(device.systemReleases('grove',{channel:'main',variant:'rf'}).map(r=>r.tag),
    ['demo-rf-main-2','demo-rf-main-1']);
  assert.throws(()=>device.systemStart('grove','update',{variant:'rf'}),/variant blocked/i);
  assert.throws(()=>device.systemStart('mbus','update',{variant:'classic'}),/variant blocked/i);
  assert.throws(()=>device.systemStart('mbus','update',{variant:'rf',force:true}),/release/i);
  assert.throws(()=>device.systemStart('mbus','update',{variant:'rf',release:'demo-main-2'}),/release/i);
  const initial=device.systemStatus('mbus');
  assert.equal(initial.version,'demo-rf-main-1');
  const id=device.systemStart('mbus','update',{channel:'main',variant:'rf'});
  assert.equal(device.job(id).options.variant,'rf');
  device.advance(3000);
  assert.equal(device.systemStatus('mbus').version,'demo-rf-main-2');
  assert.equal(device.systemStatus('grove').version,'demo-main-1');
  const reinstall=device.systemStart('mbus','update',
    {channel:'main',variant:'rf',force:true,release:'demo-rf-main-2'});
  device.advance(3000);
  assert.equal(device.systemStatus('mbus').version,'demo-rf-main-2');
  assert.equal(device.systemStatus('mbus').reboots,initial.reboots+2);
  assert.equal(device.job(reinstall).options.release,'demo-rf-main-2');
  const downgrade=device.systemStart('mbus','update',
    {channel:'main',variant:'rf',release:'demo-rf-main-1'});
  device.advance(3000);
  assert.equal(device.systemStatus('mbus').version,'demo-rf-main-1');
  device.systemActivateSlot('mbus',1);
  device.advance(1000);
  assert.equal(device.systemStatus('mbus').version,'demo-rf-backup-1');
  assert.equal(device.systemStatus('mbus').firmwareVariant,'rf');
});

test('OTA latest stays on the published catalog head after repeat checks',()=>{
  const device=new SimulatedDevice(seed);
  const first=device.systemStart('grove','update');device.advance(3000);
  assert.equal(device.systemStatus('grove').version,'demo-main-2');
  const rebootCount=device.systemStatus('grove').reboots;
  const second=device.systemStart('grove','update');
  assert.equal(device.job(second).result.stage,'checking');
  device.advance(1200);
  assert.equal(device.job(second).result.stage,'checking');
  assert.equal(device.job(second).progress,0);
  device.advance(1200);
  assert.equal(device.job(second).result.stage,'checking');
  assert.equal(device.job(second).progress,0);
  device.advance(600);
  assert.equal(device.systemStatus('grove').version,'demo-main-2');
  assert.equal(device.systemStatus('grove').reboots,rebootCount);
  assert.equal(device.job(second).result.stage,'up_to_date');
  assert.equal(device.job(first).result.stage,'completed');
});

test('OTA metadata status is module scoped and remains invalid across simulated jobs',()=>{
  const metadataSeed=structuredClone(seed);
  metadataSeed.modules.find(module=>module.id==='mbus').ota_metadata_invalid=true;
  const device=new SimulatedDevice(metadataSeed);
  assert.equal(device.systemStatus('grove').otaDataStatus,'valid');
  assert.equal(device.systemStatus('mbus').otaDataStatus,'invalid');
  device.systemStart('mbus','update');device.advance(3000);
  assert.equal(device.systemStatus('mbus').otaDataStatus,'invalid');
  device.systemActivateSlot('mbus',1);device.advance(1000);
  assert.equal(device.systemStatus('mbus').otaDataStatus,'invalid');
  assert.equal(device.systemStatus('grove').otaDataStatus,'valid');
  device.reset();
  assert.equal(device.systemStatus('mbus').otaDataStatus,'invalid');
});

test('OTA future RF demo version does not downgrade on check but allows explicit and cross-channel updates',()=>{
  const rfSeed=structuredClone(seed);
  const mbus=rfSeed.modules.find(module=>module.id==='mbus');
  mbus.capabilities.push('subghz');
  mbus.demo_version='demo-rf-main-3';
  const device=new SimulatedDevice(rfSeed);
  const initial=device.systemStatus('mbus');
  assert.equal(initial.version,'demo-rf-main-3');
  assert.deepEqual(initial.slots,['demo-rf-main-3','demo-rf-main-3']);

  const check=device.systemStart('mbus','update',{channel:'main',variant:'rf'});
  device.advance(2000);
  assert.equal(device.job(check).result.stage,'checking');
  assert.equal(device.job(check).progress,0);
  device.advance(1000);
  const afterCheck=device.systemStatus('mbus');
  assert.equal(device.job(check).result.stage,'up_to_date');
  assert.equal(afterCheck.version,initial.version);
  assert.deepEqual(afterCheck.slots,initial.slots);
  assert.equal(afterCheck.reboots,initial.reboots);

  const downgrade=device.systemStart('mbus','update',
    {channel:'main',variant:'rf',force:true,release:'demo-rf-main-2'});
  device.advance(3000);
  assert.equal(device.job(downgrade).result.stage,'completed');
  assert.equal(device.systemStatus('mbus').version,'demo-rf-main-2');
  const reinstall=device.systemStart('mbus','update',
    {channel:'main',variant:'rf',force:true,release:'demo-rf-main-2'});
  device.advance(3000);
  assert.equal(device.job(reinstall).result.stage,'completed');
  assert.equal(device.systemStatus('mbus').reboots,2);

  const otherChannel=new SimulatedDevice(rfSeed);
  const devUpdate=otherChannel.systemStart('mbus','update',{channel:'dev',variant:'rf'});
  otherChannel.advance(3000);
  assert.equal(otherChannel.job(devUpdate).result.stage,'completed');
  assert.equal(otherChannel.systemStatus('mbus').version,'demo-rf-dev-2');
  assert.equal(otherChannel.systemStatus('mbus').reboots,1);

  const unknownSeed=structuredClone(rfSeed);
  unknownSeed.modules.find(module=>module.id==='mbus').demo_version='demo-rf-main-3-preview';
  const unknown=new SimulatedDevice(unknownSeed);
  const unknownCheck=unknown.systemStart('mbus','update',{channel:'main',variant:'rf'});
  unknown.advance(3000);
  assert.equal(unknown.job(unknownCheck).result.stage,'completed');
  assert.equal(unknown.systemStatus('mbus').version,'demo-rf-main-2');
});

test('PCAP packet timestamps carry into seconds rather than overflowing microseconds',()=>{
  const bytes=syntheticPcap(seed.networks[0],Array(1001).fill(seed.clients[0]));
  const view=new DataView(bytes.buffer), record=24+1000*58;
  assert.equal(view.getUint32(record,true),1700000001);
  assert.equal(view.getUint32(record+4,true),0);
});

test('scan selection and clients use the seed identities, independently for each module', () => {
  const device = new SimulatedDevice(seed);
  device.scan('grove');
  device.advance(seed.settings.scan_time_ms);
  assert.equal(device.snapshot('grove').networks.length, seed.networks.length);
  assert.equal(device.snapshot('mbus').networks.length, 0);
  device.select('grove', 'net-1');
  assert.deepEqual(device.clients('grove').map(c => c.id), ['client-1','client-2','client-3']);
  assert.throws(() => device.select('mbus','net-1'), /discovered/);
});

test('cancelled captures cannot create files; reset makes all previous job IDs stale', () => {
  const device = new SimulatedDevice(seed);
  device.scan('grove');device.advance(seed.settings.scan_time_ms);device.select('grove','net-1');
  const capture = device.capture('grove');
  assert.throws(() => device.scan('grove'), /busy/);
  assert.equal(device.cancel(capture), true);
  device.advance(10000);
  assert.equal(device.files('grove').length, 0);
  const next = device.capture('grove');
  device.reset();device.advance(10000);
  assert.equal(device.cancel(next), false);
  assert.equal(device.files('grove').length, 0);
  assert.equal(device.snapshot('grove').selected, null);
});

test('completed capture stores valid PCAP bytes for the selected network and clients', () => {
  const device = new SimulatedDevice(seed);
  device.scan('grove');device.advance(seed.settings.scan_time_ms);device.select('grove','net-1');
  device.capture('grove');device.advance(2000);
  const [file] = device.files('grove');
  assert.equal(file.networkId, 'net-1');
  const bytes = device.readFile('grove', file.path);
  const view = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  assert.equal(view.getUint32(0,true),0xa1b2c3d4);
  assert.equal(view.getUint16(4,true),2);
  assert.equal(view.getUint16(6,true),4);
  assert.equal(view.getUint32(20,true),1); // LINKTYPE_ETHERNET
  let packets=0;
  for(let offset=24;offset<bytes.length;packets++) {
    const length=view.getUint32(offset+8,true);
    assert.equal(length,42);assert.equal(view.getUint32(offset+12,true),42);
    assert.deepEqual([...bytes.slice(offset+16+6,offset+16+12)],[2,32,119,0,0,1]);
    assert.equal(view.getUint16(offset+16+12,false),0x0806); // ARP
    offset+=16+length;
    assert.ok(offset<=bytes.length);
  }
  assert.equal(packets,3);
  assert.equal(device.files('mbus').length,0);
  bytes.fill(0);
  assert.equal(device.readFile('grove',file.path)[0],0xd4); // Defensive copy
});

test('missing/full SD and missing clients fail explicitly without committing a file', () => {
  const device = new SimulatedDevice(seed,{sdCapacityBytes:50});
  device.scan('grove');device.advance(seed.settings.scan_time_ms);device.select('grove','net-1');
  const job=device.capture('grove');device.advance(2000);
  assert.equal(device.job(job).error,'storage_full');
  assert.equal(device.files('grove').length,0);
  device.setModule('grove',{sdPresent:false});
  assert.throws(()=>device.capture('grove'),/SD/);
  device.setModule('grove',{sdPresent:true});
  device.select('grove','net-6');
  assert.deepEqual(device.clients('grove'),[]);
  assert.throws(()=>device.capture('grove'),/clients/);
});

test('Bluetooth respects connection state, explicit time zero and immutable discovery snapshots', () => {
  const device=new SimulatedDevice(seed);
  const target=device.bluetooth('grove')[0];
  device.advance(1000);
  assert.equal(device.bluetoothRssi('grove',target.mac,0),target.rssi);
  const rows=device.bluetooth('grove');rows[0].name='changed';
  assert.equal(device.bluetooth('grove')[0].name,target.name);
  device.setModule('grove',{connected:false});
  assert.deepEqual(device.bluetooth('grove'),[]);
  assert.equal(device.bluetoothRssi('grove',target.mac),-100);
  assert.ok(device.bluetooth('mbus').length>0);
  device.setModule('grove',{connected:true});
  assert.equal(device.bluetooth('grove')[0].mac,target.mac);
  assert.equal(device.bluetoothRssi('grove','00:00:00:00:00:00'),-100);
});

test('disconnect cancels work, snapshots cannot mutate the model, invalid clocks are rejected', () => {
  const device=new SimulatedDevice(seed);
  const job=device.scan('grove');device.setModule('grove',{connected:false});device.advance(seed.settings.scan_time_ms);
  assert.equal(device.job(job).state,'cancelled');
  assert.throws(()=>device.scan('grove'),/disconnected/);
  assert.throws(()=>device.advance(Infinity),/elapsed/);
  assert.throws(()=>device.advance(-1),/elapsed/);
  const snapshot=device.snapshot('mbus');snapshot.networks.push({id:'fake'});
  assert.deepEqual(device.snapshot('mbus').networks,[]);
});
