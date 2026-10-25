import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { BrowserDevice } from '../tools/ui_emulator/model/bridge.mjs';
import { StaMacStore, STA_MAC_KEY, attachStaMacPersistence } from '../tools/ui_emulator/model/sta_mac.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const command=(device,module,cli)=>JSON.parse(device.staMacCommand(module,cli));
const reboot=(device,module)=>{const id=device.systemStart(module,'reboot');device.advance(1000);assert.equal(device.job(id).state,'completed');};
const storage=(initial={})=>{const data=new Map(Object.entries(initial));return {
  getItem:key=>data.get(key)??null,setItem:(key,value)=>data.set(key,String(value)),
  removeItem:key=>data.delete(key)
};};
const browser=(disk,session)=>{const store=new StaMacStore(disk,session),device=new BrowserDevice(seed).device;
  attachStaMacPersistence(device,store);return {device,store};};

test('default JanOS status is a complete factory record isolated by module',()=>{
  const d=new BrowserDevice(seed).device;
  const grove=command(d,'grove','sta_mac status');
  const mbus=command(d,'mbus','sta_mac status');
  assert.deepEqual(Object.keys(grove).sort(),[
    'active_sta_mac','boot_mode','configured_mac','configured_mode','effective_boot_mac',
    'identity_ready','pending_reboot','record_health','record_type','schema_version'].sort());
  assert.equal(grove.schema_version,1);
  assert.equal(grove.record_type,'sta_mac_status');
  assert.equal(grove.configured_mode,'factory');
  assert.equal(grove.boot_mode,'factory');
  assert.equal(grove.configured_mac,null);
  assert.equal(grove.record_health,'missing');
  assert.equal(grove.pending_reboot,false);
  assert.equal(grove.identity_ready,true);
  assert.match(grove.effective_boot_mac,/^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$/);
  assert.equal(grove.active_sta_mac,grove.effective_boot_mac);
  assert.notEqual(mbus.effective_boot_mac,grove.effective_boot_mac);
});

test('fixed setting is saved for next virtual boot and stays isolated',()=>{
  const d=new BrowserDevice(seed).device;
  const before=command(d,'grove','sta_mac status').effective_boot_mac;
  assert.deepEqual(command(d,'grove','sta_mac set fixed 02:11:22:33:44:55'),{
    schema_version:1,record_type:'sta_mac_result',result:'set',configured_mode:'fixed',
    configured_mac:'02:11:22:33:44:55',applies_after_reboot:true});
  const pending=command(d,'grove','sta_mac status');
  assert.equal(pending.configured_mode,'fixed');
  assert.equal(pending.configured_mac,'02:11:22:33:44:55');
  assert.equal(pending.boot_mode,'factory');
  assert.equal(pending.effective_boot_mac,before);
  assert.equal(pending.pending_reboot,true);
  assert.equal(pending.record_health,'valid');
  assert.equal(command(d,'mbus','sta_mac status').record_health,'missing');
  reboot(d,'grove');
  const applied=command(d,'grove','sta_mac status');
  assert.equal(applied.boot_mode,'fixed');
  assert.equal(applied.effective_boot_mac,'02:11:22:33:44:55');
  assert.equal(applied.active_sta_mac,'02:11:22:33:44:55');
  assert.equal(applied.pending_reboot,false);
});

test('random policy picks a local unicast MAC once per boot',()=>{
  const d=new BrowserDevice(seed).device;
  assert.equal(command(d,'grove','sta_mac set random').result,'set');
  const pending=command(d,'grove','sta_mac status');
  assert.equal(pending.boot_mode,'factory');
  assert.equal(pending.pending_reboot,true);
  reboot(d,'grove');
  const first=command(d,'grove','sta_mac status');
  assert.equal(first.boot_mode,'random');
  assert.equal(first.pending_reboot,false);
  assert.equal(first.active_sta_mac,first.effective_boot_mac);
  assert.equal(Number.parseInt(first.effective_boot_mac.slice(0,2),16)&3,2);
  assert.equal(command(d,'grove','sta_mac status').effective_boot_mac,first.effective_boot_mac);
  d.scan('grove');d.advance(seed.settings.scan_time_ms);
  assert.equal(command(d,'grove','sta_mac status').effective_boot_mac,first.effective_boot_mac);
  reboot(d,'grove');
  assert.notEqual(command(d,'grove','sta_mac status').effective_boot_mac,first.effective_boot_mac);
});

test('invalid fixed addresses and argument counts do not change the saved record',()=>{
  const d=new BrowserDevice(seed).device;
  for(const cli of ['sta_mac set fixed 00:11:22:33:44:55',
                    'sta_mac set fixed 03:11:22:33:44:55',
                    'sta_mac set fixed 02:11:22:33:44',
                    'sta_mac set fixed 02:11:22:33:44:5G',
                    'sta_mac set fixed 02:11:22:33:44:55 extra']) {
    assert.notEqual(command(d,'grove',cli).result,'set',cli);
    assert.equal(command(d,'grove','sta_mac status').record_health,'missing',cli);
  }
  assert.equal(command(d,'grove','sta_mac set random extra').result,'usage');
  assert.equal(command(d,'grove','sta_mac status').configured_mode,'factory');
});

test('status remains readable during a radio job, while set is rejected',()=>{
  const d=new BrowserDevice(seed).device;
  d.scan('grove');
  assert.equal(command(d,'grove','sta_mac status').record_type,'sta_mac_status');
  const rejected=command(d,'grove','sta_mac set factory');
  assert.equal(rejected.record_type,'sta_mac_result');
  assert.equal(rejected.error,'radio_lease');
  assert.equal(command(d,'grove','sta_mac status').record_health,'missing');
});

test('disconnected module rejects commands and USB is a separate transport',()=>{
  const bridge=new BrowserDevice(seed),d=bridge.device;
  assert.equal(bridge.module(1),'usb');
  assert.equal(command(d,'usb','sta_mac status').error,'disconnected');
  d.setModule('usb',{connected:true});
  assert.equal(command(d,'usb','sta_mac status').record_health,'missing');
  assert.equal(command(d,'usb','sta_mac set factory').result,'set');
  d.setModule('usb',{connected:false});
  assert.equal(command(d,'usb','sta_mac set random').error,'disconnected');
  assert.equal(command(d,'grove','sta_mac status').record_health,'missing');
});

test('virtual reset clears saved policies, while successful update applies them',()=>{
  const d=new BrowserDevice(seed).device;
  command(d,'mbus','sta_mac set fixed 02:11:22:33:44:55');
  const id=d.systemStart('mbus','update');d.advance(3000);
  assert.equal(d.job(id).state,'completed');
  assert.equal(command(d,'mbus','sta_mac status').effective_boot_mac,'02:11:22:33:44:55');
  d.reset();
  const cleared=command(d,'mbus','sta_mac status');
  assert.equal(cleared.record_health,'missing');
  assert.equal(cleared.configured_mode,'factory');
});

test('page reconstruction keeps pending policy and exact effective MAC until virtual reboot',()=>{
  const disk=storage({unrelated:'keep'});
  const first=browser(disk);
  const factory=command(first.device,'grove','sta_mac status').effective_boot_mac;
  command(first.device,'grove','sta_mac set random');
  const pending=browser(disk);
  assert.equal(command(pending.device,'grove','sta_mac status').configured_mode,'random');
  assert.equal(command(pending.device,'grove','sta_mac status').boot_mode,'factory');
  assert.equal(command(pending.device,'grove','sta_mac status').effective_boot_mac,factory);
  assert.equal(command(pending.device,'grove','sta_mac status').pending_reboot,true);
  reboot(pending.device,'grove');
  const effective=command(pending.device,'grove','sta_mac status').effective_boot_mac;
  const reloaded=browser(disk);
  assert.equal(command(reloaded.device,'grove','sta_mac status').effective_boot_mac,effective);
  assert.equal(command(reloaded.device,'grove','sta_mac status').boot_mode,'random');
  assert.equal(command(reloaded.device,'grove','sta_mac status').pending_reboot,false);
  assert.equal(JSON.parse(disk.getItem(STA_MAC_KEY)).version,1);
  reloaded.store.reset();
  const reset=browser(disk);
  assert.equal(command(reset.device,'grove','sta_mac status').record_health,'missing');
  assert.equal(command(reset.device,'grove','sta_mac status').boot_mode,'factory');
  assert.equal(disk.getItem('unrelated'),'keep');
});

test('session tier wins over stale disk when persistence writes fail',()=>{
  const disk=storage(),session=storage();
  const first=browser(disk,session);
  command(first.device,'grove','sta_mac set fixed 02:11:22:33:44:55');
  disk.setItem=()=>{throw Error('quota');};
  const second=browser(disk,session);
  command(second.device,'grove','sta_mac set random');
  assert.equal(second.store.mode,'session');
  const third=browser(disk,session);
  assert.equal(command(third.device,'grove','sta_mac status').configured_mode,'random');
  third.store.reset();
  assert.equal(command(browser(disk,session).device,'grove','sta_mac status').record_health,'missing');
});

test('corrupt and unsupported persisted identity records recover without applying a forged MAC',()=>{
  const valid=browser(storage()).device.staMacSnapshot();
  for(const raw of ['{',JSON.stringify({version:99,modules:valid}),
                    JSON.stringify({version:1,modules:{...valid,grove:{...valid.grove,effectiveMac:'02:11:22:33:44:55'}}}),
                    JSON.stringify({version:1,modules:{...valid,grove:{...valid.grove,unexpected:true}}}),
                    JSON.stringify({version:1,modules:{...valid,grove:{...valid.grove,
                      bootMode:'fixed',bootMac:'02:11:22:33:44:55',effectiveMac:'02:11:22:33:44:55'}}})]) {
    const disk=storage({[STA_MAC_KEY]:raw});
    const restored=browser(disk);
    assert.equal(restored.store.recovered,true);
    assert.equal(command(restored.device,'grove','sta_mac status').record_health,'missing');
    assert.equal(command(restored.device,'grove','sta_mac status').boot_mode,'factory');
  }
});

test('denied browser storage keeps a memory-only policy and does not crash',()=>{
  const denied={getItem(){throw Error('denied');},setItem(){throw Error('denied');}};
  const current=browser(denied,denied);
  command(current.device,'mbus','sta_mac set fixed 02:11:22:33:44:55');
  assert.equal(current.store.mode,'memory');
  assert.equal(command(current.device,'mbus','sta_mac status').configured_mac,'02:11:22:33:44:55');
  const reload=browser(denied,denied);
  assert.equal(command(reload.device,'mbus','sta_mac status').record_health,'missing');
});
