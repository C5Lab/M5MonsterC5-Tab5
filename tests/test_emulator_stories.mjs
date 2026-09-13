import test from 'node:test';
import assert from 'node:assert/strict';
import {BluetoothGuide, bluetoothStory} from '../tools/ui_emulator/web/stories.mjs';
const mac=bluetoothStory.target.mac;
const event=(type,extra={})=>({type,tab:0,mac,timeMs:0,epoch:1,...extra});

test('guide advances only from ordered native events for the right module and target',()=>{
  const g=new BluetoothGuide();g.start();
  g.accept(event('sample',{rssi:-40}));assert.equal(g.step,0);
  g.accept(event('scan',{tab:2,devices:[mac]}));assert.equal(g.step,0);
  g.accept(event('scan',{devices:[]}));assert.equal(g.step,0);
  g.accept(event('scan',{devices:[mac]}));assert.equal(g.step,1);
  g.accept(event('locate',{mac:'02:00:00:00:00:99'}));assert.equal(g.step,1);
  g.accept(event('locate'));assert.equal(g.step,2);
  for(let i=0;i<10;i++)g.accept(event('sample',{rssi:-40}));assert.equal(g.step,2);
  g.accept(event('sample',{rssi:-42}));g.accept(event('sample',{rssi:-45}));assert.equal(g.step,2);
  for(const timeMs of [500,1000,1500,2000])g.accept(event('sample',{rssi:-45,timeMs}));assert.equal(g.step,3);
  g.accept(event('back',{tab:2}));assert.equal(g.complete,false);
  g.accept(event('back'));assert.equal(g.complete,true);
});

test('disconnect, premature back, restart and leaving cannot manufacture completion',()=>{
  const g=new BluetoothGuide();g.start();
  g.accept(event('scan',{devices:[mac]}));g.accept(event('locate'));
  g.accept(event('sample',{rssi:-40}));g.accept(event('sample',{rssi:-100}));
  g.accept(event('sample',{rssi:-41}));g.accept(event('sample',{rssi:-42}));assert.equal(g.step,2);
  g.accept(event('back'));assert.equal(g.step,1);
  g.accept(event('locate'));g.accept(event('sample',{rssi:-40}));
  g.start();assert.equal(g.step,0);assert.equal(g.complete,false);
  g.accept(event('sample',{rssi:-41}));assert.equal(g.step,0);
  g.leave();g.accept(event('scan',{devices:[mac]}));assert.equal(g.active,false);assert.equal(g.step,0);
});

test('events from a previous guide session cannot enter a restarted story',()=>{
  const g=new BluetoothGuide();g.start();
  g.accept(event('scan',{devices:[mac]}));g.start();
  g.accept(event('scan',{devices:[mac]}));assert.equal(g.step,0);
  g.accept(event('scan',{devices:[mac],epoch:2}));assert.equal(g.step,1);
  g.accept(event('locate'));assert.equal(g.step,1);
  g.accept(event('locate',{epoch:2}));assert.equal(g.step,2);
  g.leave();g.start();
  g.accept(event('scan',{devices:[mac],epoch:2}));assert.equal(g.step,0);
});

test('visibility and module transitions reset an unfinished observation window',()=>{
  const g=new BluetoothGuide();g.start();
  g.accept(event('scan',{devices:[mac]}));g.accept(event('locate'));
  g.accept(event('sample',{rssi:-40,timeMs:0}));
  g.setContext({tab:0,visible:false});
  for(let i=1;i<=5;i++)g.accept(event('sample',{rssi:-40-i,timeMs:i*500}));
  assert.equal(g.step,2);assert.equal(g.samples.size,0);
  g.setContext({tab:0,visible:true});
  g.accept(event('sample',{rssi:-50,timeMs:3000}));
  g.setContext({tab:2,visible:true});g.setContext({tab:0,visible:true});
  assert.equal(g.samples.size,0);
  for(let i=0;i<=4;i++)g.accept(event('sample',{rssi:-50-i,timeMs:3500+i*500}));
  assert.equal(g.step,3);
});
