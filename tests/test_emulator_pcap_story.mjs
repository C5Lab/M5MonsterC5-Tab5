import test from 'node:test';
import assert from 'node:assert/strict';
import {pcapStories} from '../tools/ui_emulator/web/pcap-story.mjs';

const entry=id=>pcapStories.find(x=>x.id===id);
const base={tab:0,captureOpen:false,map:false,devices:false,endpoints:false,connections:false,
 protocols:false,objects:false,tools:false,packet:false,investigation:false,http:false,http200:false,
 preview:false,hash:false,intel:false,fqdnOn:false,invalid:false,export:false,analysisOpen:false,capturePage:false};
// Mirror the reader: capturePage is captureOpen with no analysis view open.
const view=(open,...flags)=>{const s={...base,captureOpen:open};for(const f of flags)s[f]=true;
 s.analysisOpen=['map','devices','endpoints','connections','protocols','objects','tools','packet','investigation','preview','hash'].some(k=>s[k]);
 s.capturePage=s.captureOpen&&!s.analysisOpen;return s;};

test('S15 exposes analysis, investigate, extract and invalid variants',()=>{
 assert.deepEqual(pcapStories.map(x=>x.id),['espshark-analysis','espshark-investigate','espshark-extract','espshark-invalid']);
 for(const e of pcapStories){const g=e.create();g.start({});assert.ok(g.story.steps.length>=2);
  g.accept({...view(true),epoch:g.epoch-1});assert.equal(g.step,0,'stale epoch rejected');g.leave();assert.equal(g.active,false);}
});

test('analysis walks every inspection submenu then returns',()=>{
 const g=entry('espshark-analysis').create();g.start({});const emit=s=>g.accept({...s,epoch:g.epoch});
 emit(view(false));assert.equal(g.step,0,'needs an open capture');
 emit(view(true));assert.equal(g.step,1);
 emit(view(true,'map'));assert.equal(g.step,2);
 emit(view(true,'devices'));assert.equal(g.step,3);
 emit(view(true,'endpoints'));assert.equal(g.step,4);
 emit(view(true,'connections'));assert.equal(g.step,5);
 emit(view(true,'protocols'));assert.equal(g.step,5,'protocols step needs HTTP');
 emit(view(true,'protocols','http'));assert.equal(g.step,6);
 emit(view(true));assert.equal(g.complete,true);
});

test('investigate covers packet, health, FQDN and export',()=>{
 const g=entry('espshark-investigate').create();g.start({});const emit=s=>g.accept({...s,epoch:g.epoch});
 emit(view(true));assert.equal(g.step,1);
 emit(view(true,'packet'));assert.equal(g.step,2);
 emit(view(true,'investigation'));assert.equal(g.step,2,'needs intel');
 emit(view(true,'investigation','intel'));assert.equal(g.step,3);
 emit(view(true,'fqdnOn'));assert.equal(g.step,4);
 emit(view(true,'tools'));assert.equal(g.step,4,'needs export label');
 emit(view(true,'tools','export'));assert.equal(g.step,5);
 emit(view(true));assert.equal(g.complete,true);
});

test('extract reaches objects, preview and the SHA-256 hash',()=>{
 const g=entry('espshark-extract').create();g.start({});const emit=s=>g.accept({...s,epoch:g.epoch});
 emit(view(true));assert.equal(g.step,1);
 emit(view(true,'objects'));assert.equal(g.step,1,'needs HTTP 200');
 emit(view(true,'objects','http200'));assert.equal(g.step,2);
 emit(view(true,'objects','preview'));assert.equal(g.step,2,'needs hash');
 emit(view(true,'objects','preview','hash'));assert.equal(g.step,3);
 emit(view(true));assert.equal(g.complete,true);
});

test('invalid capture guides recovery to a valid one',()=>{
 const g=entry('espshark-invalid').create();g.start({});const emit=s=>g.accept({...s,epoch:g.epoch});
 emit(view(false));assert.equal(g.step,0);
 emit({...view(false),invalid:true});assert.equal(g.step,1);
 emit(view(true));assert.equal(g.complete,true);
});

test('losing the capture pauses an analysis story',()=>{
 const g=entry('espshark-analysis').create();g.start({});const emit=s=>g.accept({...s,epoch:g.epoch});
 emit(view(true));emit(view(true,'map'));assert.equal(g.step,2);
 emit(view(false));assert.equal(g.step,2);assert.match(g.hint,/Reopen/);
});
