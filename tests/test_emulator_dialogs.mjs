import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
test('slot activates only after successful reboot; cancellation and disconnect retain running image',()=>{
 const d=new SimulatedDevice(seed);
 const first=d.systemActivateSlot('grove',1);assert.equal(d.systemStatus('grove').activeSlot,0);
 d.cancel(first);d.advance(2000);assert.equal(d.systemStatus('grove').activeSlot,0);
 d.systemActivateSlot('grove',1);d.setModule('grove',{connected:false});d.advance(2000);
 assert.equal(d.systemStatus('grove').activeSlot,0);d.setModule('grove',{connected:true});
 d.systemActivateSlot('grove',1);d.advance(1000);
 assert.equal(d.systemStatus('grove').activeSlot,1);assert.equal(d.systemStatus('grove').version,'demo-backup-1');
 assert.equal(d.systemStatus('mbus').activeSlot,0);
 d.systemActivateSlot('grove',0);d.advance(1000);assert.equal(d.systemStatus('grove').version,'demo-main-1');
 assert.throws(()=>d.systemActivateSlot('grove',2),/slot/i);
});
test('firmware update refreshes the running slot image',()=>{
 const d=new SimulatedDevice(seed);d.systemStart('grove','update');d.advance(3000);
 assert.equal(d.systemStatus('grove').slots[0],'demo-main-2');
 d.systemActivateSlot('grove',1);d.advance(1000);d.systemActivateSlot('grove',0);d.advance(1000);
 assert.equal(d.systemStatus('grove').version,'demo-main-2');
});
