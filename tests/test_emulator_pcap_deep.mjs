import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('example installation is explicit, deterministic, isolated and idempotent',()=>{
  const d=new SimulatedDevice(seed);
  assert.deepEqual(d.files('grove'),[]);
  const files=d.installPcapFixtures('grove');
  assert.deepEqual(files.map(f=>f.fixtureId),['http-dns-icmp','dns-icmp','invalid-truncated']);
  assert.deepEqual(files.map(f=>f.packetCount),[14,4,0]);
  assert.deepEqual(d.installPcapFixtures('grove'),files);
  assert.deepEqual(d.files('mbus'),[]);
  for(const f of files) {
    const bytes=d.readFile('grove',f.path);assert.equal(bytes.length,f.sizeBytes);
    bytes.fill(0);assert.equal(d.readFile('grove',f.path)[0],0xd4);
  }
  d.reset();assert.deepEqual(d.files('grove'),[]);
});

test('fixture storage guards never partially install files',()=>{
  const full=new SimulatedDevice(seed,{sdCapacityBytes:200});
  assert.throws(()=>full.installPcapFixtures('grove'),/storage_full/);
  assert.deepEqual(full.files('grove'),[]);
  const d=new SimulatedDevice(seed);
  d.setModule('grove',{sdPresent:false});assert.throws(()=>d.installPcapFixtures('grove'),/SD missing/);
  d.setModule('grove',{sdPresent:true,connected:false});assert.throws(()=>d.installPcapFixtures('grove'),/disconnected/);
  d.setModule('grove',{connected:true});d.scan('grove');assert.throws(()=>d.installPcapFixtures('grove'),/busy/);
  assert.deepEqual(d.files('grove'),[]);
});

test('fixture capacity includes existing captures and exact capacity permits repeat installation',()=>{
  const exact=new SimulatedDevice(seed,{sdCapacityBytes:1591});
  assert.equal(exact.installPcapFixtures('grove').reduce((n,f)=>n+f.sizeBytes,0),1591);
  assert.equal(exact.installPcapFixtures('grove').length,3);
  const d=new SimulatedDevice(seed,{sdCapacityBytes:1591});
  d.scan('grove');d.advance(seed.settings.scan_time_ms);d.select('grove','net-1');d.capture('grove');d.advance(2000);
  const original=d.files('grove');assert.equal(original[0].sizeBytes,198);
  assert.throws(()=>d.installPcapFixtures('grove'),/storage_full/);
  assert.deepEqual(d.files('grove'),original);
});

function sum(bytes) {
  let n=0;for(let i=0;i<bytes.length;i+=2)n+=(bytes[i]<<8)+(bytes[i+1]||0);
  while(n>>>16)n=(n&65535)+(n>>>16);return n;
}

test('example frames have valid IPv4 and transport lengths and checksums',()=>{
  const d=new SimulatedDevice(seed),files=d.installPcapFixtures('grove');
  for(const file of files.filter(f=>f.valid)) {
    const bytes=d.readFile('grove',file.path),v=new DataView(bytes.buffer);
    assert.equal(v.getUint32(20,true),1);
    let count=0;const protocols={};let text='';
    for(let off=24;off<bytes.length;count++) {
      const len=v.getUint32(off+8,true);assert.equal(len,v.getUint32(off+12,true));
      const frame=bytes.slice(off+16,off+16+len);assert.equal(frame.length,len);
      const ip=frame.slice(14),iv=new DataView(ip.buffer);
      assert.equal(iv.getUint16(2),ip.length);assert.equal(sum(ip.slice(0,20)),65535);
      const proto=ip[9],payload=ip.slice(20);protocols[proto]=(protocols[proto]||0)+1;
      if(proto===1)assert.equal(sum(payload),65535);
      else {
        const pseudo=new Uint8Array(12+payload.length);pseudo.set(ip.slice(12,20));
        pseudo[9]=proto;new DataView(pseudo.buffer).setUint16(10,payload.length);pseudo.set(payload,12);
        assert.equal(sum(pseudo),65535);
        if(proto===17)assert.equal(new DataView(payload.buffer).getUint16(4),payload.length);
      }
      text+=new TextDecoder().decode(payload);off+=16+len;
      assert.ok(off<=bytes.length);
    }
    assert.equal(count,file.packetCount);
    assert.deepEqual(protocols,file.ipProtocolCounts);
    if(file.fixtureId==='http-dns-icmp') {assert.match(text,/GET \/demo HTTP\/1.1/);assert.match(text,/Host: example.com/);assert.match(text,/HTTP\/1.1 200 OK/);}
  }
  const bad=d.readFile('grove',files[2].path);assert.equal(bad.length,12);
});
