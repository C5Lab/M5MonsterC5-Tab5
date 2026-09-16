import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(readFileSync(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url)));
test('virtual artifact transactions validate paths and roll back quota failures',()=>{
 const d=new SimulatedDevice(seed,{sdCapacityBytes:10}),path='/sdcard/artifacts/test.txt';
 d.commitFileChanges('grove',[{path,bytes:new Uint8Array([1,2,3])}]);
 assert.deepEqual([...d.readFile('grove',path)],[1,2,3]);
 for(const path of ['/sdcard/../bad','/sdcard//bad','/sdcard/a\\bad','/outside/file'])
  assert.throws(()=>d.commitFileChanges('grove',[{path,bytes:new Uint8Array()}]),/Invalid/);
 assert.throws(()=>d.commitFileChanges('grove',[{path:'/sdcard/large',bytes:new Uint8Array(11)}],[path]),/storage_full/);
 assert.equal(d.readFile('grove',path).length,3);
 d.deleteFile('grove',path);assert.equal(d.files('grove').length,0);
});
test('wardrive cleanup previews, then archives only sessions done on both services',()=>{
 const d=new SimulatedDevice(seed);d.wardriveStart('grove');d.advance(6000);d.wardriveStop('grove');
 const before=d.wardrive('grove').sessions[0];assert.equal(d.wardriveCleanup('grove').matched,0);
 d.wardriveUpload('grove','success',{provider:'wigle',mode:'all'});
 assert.equal(d.wardriveCleanup('grove').matched,0);
 d.wardriveUpload('grove','success',{provider:'wdgwars',mode:'all'});
 assert.equal(d.wardriveCleanup('grove').matched,1);assert.equal(d.wardrive('grove').sessions[0].path,before.path);
 assert.equal(d.wardriveCleanup('grove',{move:true}).moved,1);
 const archived=d.wardrive('grove').sessions[0];assert.ok(archived.path.includes('/uploaded/'));
 assert.equal(d.wardriveCleanup('grove').matched,0);
 d.wardriveDeleteFiles('grove',[archived.path]);assert.equal(d.wardrive('grove').sessions.length,0);
});
test('artifact writes are detached and module isolated',()=>{
 const d=new SimulatedDevice(seed),bytes=new Uint8Array([7]),path='/sdcard/output.bin';
 d.commitFileChanges('grove',[{path,bytes}]);bytes[0]=9;
 assert.equal(d.readFile('grove',path)[0],7);assert.equal(d.files('mbus').length,0);
 d.commitFileChanges('grove',[{path:'/sdcard/new.bin',bytes:new Uint8Array([2])}],[path]);
 assert.throws(()=>d.readFile('grove',path),/not found/);assert.equal(d.files('grove').length,1);
});
test('unavailable or busy module rejects an entire artifact mutation',()=>{
 const d=new SimulatedDevice(seed),path='/sdcard/original.bin';d.commitFileChanges('grove',[{path,bytes:new Uint8Array([3])}]);
 for(const state of [{connected:false},{connected:true,sdPresent:false}]){
  d.setModule('grove',state);assert.throws(()=>d.commitFileChanges('grove',[],[path]));
 }
 d.setModule('grove',{sdPresent:true});const job=d.scan('grove');
 assert.throws(()=>d.commitFileChanges('grove',[],[path]),/busy/);d.cancel(job);
 assert.equal(d.readFile('grove',path)[0],3);
});
