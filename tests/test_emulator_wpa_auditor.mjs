import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {WpaAuditorModel, WpaAuditorStore} from '../tools/ui_emulator/model/wpa_auditor.mjs';
import {SimulatedDevice} from '../tools/ui_emulator/model/device.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
const make=()=>new WpaAuditorModel(seed);

test('catalog merges a duplicate across sources and preserves invalid capture evidence',()=>{
  const audit=make(),rows=audit.catalog();
  assert.equal(rows.length,4);
  assert.deepEqual(rows[0].sources,['grove','usb']);
  assert.equal(rows[0].ssid,'NEON-BAZAAR');
  assert.equal(rows[0].validation,'ready');
  assert.equal(rows[2].validation,'invalid');
  assert.match(rows[2].reason,/missing M2/i);
  assert.throws(()=>audit.start(rows[2].id,'Demo shortlist'),/invalid/i);
  assert.deepEqual(audit.catalog()[0].sources,['grove','usb']);
});

test('sync all copies only missing valid remote captures and can be repeated',()=>{
  const audit=make();
  assert.deepEqual(audit.syncAll(),{copied:2,alreadyLocal:1,skippedInvalid:1});
  assert.deepEqual(audit.syncAll(),{copied:0,alreadyLocal:3,skippedInvalid:1});
  assert.equal(audit.catalog().filter(item=>item.local).length,3);
  assert.equal(audit.catalog()[0].sources.filter(source=>source==='local').length,1);
});

test('sync all persists earlier virtual copies if a later copy runs out of space',()=>{
  const snapshots=[];let writes=0;
  const audit=new WpaAuditorModel(seed,{onChange:state=>snapshots.push(state),
    localFiles:{write:()=>{if(++writes===2)throw new Error('storage_full');}}});
  assert.throws(()=>audit.syncAll(),/storage_full/);
  assert.equal(audit.catalog().find(row=>row.id==='capture-neon').local,true);
  assert.equal(audit.catalog().find(row=>row.id==='capture-ghost').local,false);
  assert.ok(snapshots.at(-1).local.includes('capture-neon'));
});

test('paused audit resumes from safe offset and a separate audit keeps its own checkpoint',()=>{
  const audit=make();
  audit.start('capture-neon','Demo shortlist');audit.advance(1750);audit.pause();
  const first=audit.sessions()[0];
  assert.equal(first.state,'paused');assert.ok(first.tried>0);
  audit.start('capture-ghost','Expanded demo');audit.advance(500);audit.pause();
  assert.equal(audit.sessions().length,2);
  audit.resume(first.id);audit.advance(500);audit.pause();
  const resumed=audit.sessions().find(session=>session.id===first.id);
  assert.ok(resumed.tried>first.tried);
  assert.equal(audit.sessions().find(session=>session.captureId==='capture-ghost').tried,10);
});

test('found result shows only fixture credential while history excludes it',()=>{
  const audit=make();
  audit.start('capture-neon','Demo shortlist');audit.advance(100000);
  assert.equal(audit.active(),null);
  assert.equal(audit.sessions().length,0);
  assert.equal(audit.recovered()[0].credential,'NeonDemo-2026!');
  assert.equal(audit.history()[0].outcome,'FOUND');
  assert.ok(!JSON.stringify(audit.history()).includes('NeonDemo-2026!'));
  assert.throws(()=>audit.start('capture-neon','Unknown words'),/wordlist/i);
});

test('repeat audit keeps one recovered credential per capture',()=>{
  const audit=make();
  audit.start('capture-neon','Demo shortlist');audit.advance(100000);
  audit.start('capture-neon','Demo shortlist');audit.advance(100000);
  assert.equal(audit.history().length,2);
  assert.equal(audit.recovered().length,1);
});

test('start over and delete audit affect only the chosen checkpoint',()=>{
  const audit=make();
  audit.start('capture-neon','Demo shortlist');audit.advance(500);audit.pause();
  audit.start('capture-ghost','Expanded demo');audit.advance(500);audit.pause();
  const [first,second]=audit.sessions();
  audit.startOver(first.id);assert.equal(audit.active().tried,0);
  audit.pause();audit.deleteAudit(second.id);
  assert.equal(audit.sessions().length,1);
  assert.equal(audit.sessions()[0].captureId,'capture-neon');
  assert.equal(audit.catalog().length,4);
});

test('batch queue runs sequentially, pauses safely, resumes and records outcomes',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  assert.equal(audit.queue().length,2);
  audit.startBatch();audit.advance(2500);audit.pauseBatch();
  assert.equal(audit.queue()[0].state,'paused');
  const offset=audit.queue()[0].tried;
  audit.resumeBatch();audit.advance(100000);
  assert.equal(audit.queue()[0].state,'found');
  assert.equal(audit.queue()[1].state,'not_found');
  assert.ok(audit.queue()[0].tried>offset);
  assert.equal(audit.history().length,2);
  assert.equal(audit.active(),null);
});

test('queue rejects duplicate captures across separate add actions',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon'],'Demo shortlist');
  assert.throws(()=>audit.addToQueue(['capture-neon'],'Expanded demo'),/already in the queue/i);
  assert.equal(audit.queue().length,1);
});

test('pre-start candidate change updates every queued capture and locks after launch',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.setQueueWordlist('Expanded demo');
  assert.deepEqual(audit.queue().map(item=>item.wordlist),['Expanded demo','Expanded demo']);
  audit.startBatch();
  assert.throws(()=>audit.setQueueWordlist('Demo shortlist'),/locked/i);
  audit.advance(100000);
  assert.deepEqual(audit.history().map(item=>item.outcome),['NOT FOUND','NOT FOUND']);
});

test('new batch clears queue while keeping a paused audit checkpoint',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();audit.advance(500);
  assert.throws(()=>audit.newBatch(),/active/i);
  audit.pauseBatch();const checkpoint=audit.sessions()[0];
  audit.newBatch();
  assert.equal(audit.queue().length,0);
  assert.equal(audit.sessions()[0].tried,checkpoint.tried);
  assert.equal(audit.sessions()[0].queueIndex,-1);
  audit.resume(checkpoint.id);audit.advance(100000);
  assert.equal(audit.history()[0].outcome,'FOUND');
});

test('paused batch checkpoint cannot be orphaned by clearing its queue',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon'],'Demo shortlist');
  audit.startBatch();audit.advance(500);audit.pauseBatch();
  assert.throws(()=>audit.clearQueue(),/paused checkpoint/i);
  assert.equal(audit.queue()[0].state,'paused');
  audit.resumeBatch();audit.advance(500);
  assert.equal(audit.active().tried,20);
});

test('Start batch cannot skip an earlier paused queue item',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();audit.advance(500);audit.pauseBatch();
  assert.throws(()=>audit.startBatch(),/resume paused batch/i);
  assert.equal(audit.queue()[1].state,'pending');
  audit.resumeBatch();assert.equal(audit.active().captureId,'capture-neon');
});

test('deleting a paused batch checkpoint returns that queue item to pending',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();audit.advance(500);audit.pauseBatch();
  const session=audit.sessions()[0];
  audit.deleteAudit(session.id);
  assert.deepEqual(audit.queue().map(item=>item.state),['pending','pending']);
  assert.equal(audit.queue()[0].tried,0);
  audit.startBatch();assert.equal(audit.active().captureId,'capture-neon');
});

test('resuming a queued session from its row continues the remaining batch',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();audit.advance(500);audit.pauseBatch();
  audit.resume(audit.sessions()[0].id);audit.advance(100000);
  assert.deepEqual(audit.queue().map(item=>item.state),['found','not_found']);
});

test('versioned snapshot restores independent progress; invalid storage recovers empty',()=>{
  const disk=new Map(),io={getItem:key=>disk.get(key)??null,setItem:(key,value)=>disk.set(key,value),removeItem:key=>disk.delete(key)};
  const audit=make(),store=new WpaAuditorStore(io);
  audit.start('capture-neon','Demo shortlist');audit.advance(550);audit.pause();
  store.save(audit.snapshot());
  const restored=make();assert.equal(restored.restore(store.load()),true);
  assert.equal(restored.sessions()[0].tried,11);
  assert.equal(restored.sessions()[0].state,'paused');
  disk.set('tab5-emulator:wpa-auditor','{"version":99,"state":{}}');
  const recovered=new WpaAuditorStore(io);
  assert.equal(recovered.load(),null);assert.equal(recovered.recovered,true);
});

test('reload preserves WPA checkpoints and history but discards synced virtual SD copies',()=>{
  const path=capture=>`/sdcard/lab/handshakes/${capture.name}`;
  const makeWithFiles=device=>new WpaAuditorModel(seed,{localFiles:{
    write:capture=>device.commitFileChanges('internal',[{
      path:path(capture),bytes:new TextEncoder().encode(`OFFLINE WPA AUDIT FIXTURE ${capture.id}\n`)
    }]),
    remove:capture=>device.commitFileChanges('internal',[],[path(capture)])
  }});
  const firstDevice=new SimulatedDevice(seed),audit=makeWithFiles(firstDevice);
  audit.reconcileLocalFiles();
  audit.syncCapture('capture-neon');
  audit.start('capture-ghost','Demo shortlist');audit.advance(100000);
  audit.start('capture-neon','Demo shortlist');audit.advance(550);audit.pause();
  assert.equal(firstDevice.files('internal').length,2);
  const reloadedDevice=new SimulatedDevice(seed),restored=makeWithFiles(reloadedDevice);
  assert.equal(restored.restore(audit.snapshot()),true);
  restored.reconcileLocalFiles();
  assert.equal(restored.history()[0].captureId,'capture-ghost');
  assert.equal(restored.sessions()[0].tried,11);
  assert.equal(restored.sessions()[0].state,'paused');
  assert.equal(restored.catalog().find(row=>row.id==='capture-neon').local,false);
  assert.equal(restored.catalog().find(row=>row.id==='capture-synapse').local,true);
  assert.deepEqual(reloadedDevice.files('internal').map(file=>file.path),
    ['/sdcard/lab/handshakes/synapse-relay-local.hccapx']);
});

test('reset reports refusal to remove a stale persistent audit record',()=>{
  const disk={getItem:()=>'{"version":1,"state":{"version":1}}',
    setItem:()=>{},removeItem:()=>{throw new Error('denied')}};
  const store=new WpaAuditorStore(disk);
  store.reset();
  assert.equal(store.mode,'memory');
  assert.equal(store.load(),null);
});

test('batch skip advances to the next capture and retains an independent checkpoint',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();audit.advance(500);
  const offset=audit.active().tried;
  audit.skipCurrent();
  assert.equal(audit.queue()[0].state,'cancelled');
  assert.equal(audit.queue()[1].state,'running');
  assert.equal(audit.active().captureId,'capture-ghost');
  assert.equal(audit.sessions().find(item=>item.captureId==='capture-neon').tried,offset);
  assert.equal(audit.sessions().find(item=>item.captureId==='capture-neon').queueIndex,-1);
  audit.skipCurrent();
  assert.equal(audit.batchRunning(),false);
  assert.throws(()=>audit.skipCurrent(),/batch/i);
});

test('queue row removal rejects the active row and repairs checkpoint indices',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();
  assert.throws(()=>audit.removeQueueItem(0),/active/i);
  audit.removeQueueItem(1);
  assert.deepEqual(audit.queue().map(item=>item.captureId),['capture-neon']);
  audit.advance(500);audit.pauseBatch();
  audit.removeQueueItem(0);
  assert.equal(audit.queue().length,0);
  assert.equal(audit.sessions()[0].queueIndex,-1);
  audit.resume(audit.sessions()[0].id);
  audit.advance(100000);
  assert.equal(audit.history()[0].outcome,'FOUND');
});

test('cancel batch marks unfinished rows and keeps a resumable audit',()=>{
  const audit=make();
  audit.addToQueue(['capture-neon','capture-ghost'],'Demo shortlist');
  audit.startBatch();audit.advance(500);
  audit.cancelBatch();
  assert.deepEqual(audit.queue().map(item=>item.state),['cancelled','cancelled']);
  assert.equal(audit.batchRunning(),false);
  assert.equal(audit.active(),null);
  assert.equal(audit.sessions()[0].state,'paused');
  assert.equal(audit.sessions()[0].queueIndex,-1);
  audit.resume(audit.sessions()[0].id);audit.advance(100000);
  assert.equal(audit.queue()[1].state,'cancelled');
  assert.equal(audit.history()[0].outcome,'FOUND');
});

test('local deletion removes the Tab5 copy, preserves worker copies, and obeys busy guard',()=>{
  const writes=[],removes=[];
  const audit=new WpaAuditorModel(seed,{localFiles:{write:capture=>writes.push(capture.name),
    remove:capture=>removes.push(capture.name)}});
  audit.reconcileLocalFiles();
  assert.deepEqual(writes,['synapse-relay-local.hccapx']);
  audit.syncCapture('capture-neon');
  assert.ok(writes.includes('neon-bazaar-4way.hccapx'));
  audit.start('capture-neon','Demo shortlist');
  assert.throws(()=>audit.deleteLocalCapture('capture-neon'),/active/i);
  audit.pause();
  audit.deleteLocalCapture('capture-neon');
  assert.equal(audit.catalog().find(row=>row.id==='capture-neon').local,false);
  assert.deepEqual(audit.catalog().find(row=>row.id==='capture-neon').sources,['grove','usb']);
  audit.deleteLocalCapture('capture-synapse');
  assert.equal(audit.catalog().some(row=>row.id==='capture-synapse'),false);
  assert.deepEqual(removes,['neon-bazaar-4way.hccapx','synapse-relay-local.hccapx']);
});

test('local capture actions reconcile the internal virtual SD file listing',()=>{
  const device=new SimulatedDevice(seed);
  const path=capture=>`/sdcard/lab/handshakes/${capture.name}`;
  const audit=new WpaAuditorModel(seed,{localFiles:{
    write:capture=>device.commitFileChanges('internal',[{
      path:path(capture),bytes:new TextEncoder().encode(`OFFLINE WPA AUDIT FIXTURE ${capture.id}\n`)}]),
    remove:capture=>device.commitFileChanges('internal',[],[path(capture)])
  }});
  audit.reconcileLocalFiles();
  assert.deepEqual(device.files('internal').map(file=>file.path),[path(audit.catalog()[3])]);
  audit.syncCapture('capture-neon');
  assert.equal(device.files('internal').length,2);
  audit.deleteLocalCapture('capture-neon');
  assert.deepEqual(device.files('internal').map(file=>file.path),[path(audit.catalog()[3])]);
});

test('catalogue state classifies new, resumable, found, not found, error and invalid',()=>{
  const audit=make();
  assert.equal(audit.catalogState('capture-neon'),'New');
  assert.equal(audit.catalogState('capture-chrome'),'Invalid');
  audit.start('capture-neon','Demo shortlist');audit.advance(500);audit.pause();
  assert.equal(audit.catalogState('capture-neon'),'Resumable');
  audit.resume(audit.sessions()[0].id);audit.advance(100000);
  assert.equal(audit.catalogState('capture-neon'),'Found');
  audit.start('capture-ghost','Demo shortlist');audit.advance(100000);
  assert.equal(audit.catalogState('capture-ghost'),'Not found');
  audit.addToQueue(['capture-neon'],'Expanded demo');audit.cancelBatch();
  assert.equal(audit.catalogState('capture-neon'),'Error');
});

test('Handshakes file route matches only a known capture on its scenario source',()=>{
  const audit=make();
  const neon='/sdcard/lab/handshakes/neon-bazaar-4way.pcap';
  const fixture=id=>new TextEncoder().encode(`OFFLINE WPA AUDIT FIXTURE ${id}\n`);
  assert.deepEqual(audit.captureForFile('grove', neon, fixture('capture-neon')), {
    id:'capture-neon',ssid:'NEON-BAZAAR',validation:'ready',reason:'Complete M1 + M2 pair'
  });
  assert.equal(audit.captureForFile('grove',neon,new TextEncoder().encode('arbitrary PCAP')),null);
  assert.equal(audit.captureForFile('grove',neon,fixture('capture-ghost')),null);
  assert.equal(audit.captureForFile('mbus',neon,fixture('capture-neon')),null);
  assert.equal(audit.captureForFile('grove','/sdcard/lab/pcaps/grove/neon-bazaar-4way.pcap',fixture('capture-neon')),null);
  assert.equal(audit.captureForFile('grove','/sdcard/lab/handshakes/unmatched.pcap',fixture('capture-neon')),null);
  assert.equal(audit.captureForFile('grove','/sdcard/lab/handshakes/chrome-clinic-incomplete.pcap',fixture('capture-chrome')).validation,'invalid');
});
