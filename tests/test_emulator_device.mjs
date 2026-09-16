import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
import { syntheticPcap } from '../tools/ui_emulator/model/pcap.mjs';

const seed = JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json', import.meta.url), 'utf8'));

test('module captures have distinct paths when copied to the shared analysis filesystem',()=>{
  const device=new SimulatedDevice(seed);
  for(const id of ['grove','mbus']) {
    device.scan(id);device.advance(seed.settings.scan_time_ms);device.select(id,'net-1');
    device.capture(id);device.advance(2000);
  }
  assert.notEqual(device.files('grove')[0].path,device.files('mbus')[0].path);
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
