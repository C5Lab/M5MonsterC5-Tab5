import test from 'node:test';
import assert from 'node:assert/strict';
import {encodeRestartHandoff,consumeRestartHandoff} from '../tools/ui_emulator/web/story-restart.mjs';

test('native rotation handoff is consumed from the URL without storing progress',()=>{
 const payload={version:1,expectedRotation:2,fromRotation:0};
 const hash=encodeRestartHandoff('s19-rotation',payload);
 let url=new URL('http://localhost/?rotation=2'+hash);
 const history={state:{keep:true},replaceState(state,title,next){assert.deepEqual(state,{keep:true});url=new URL(next);}};
 assert.deepEqual(consumeRestartHandoff(url,history),{id:'s19-rotation',payload});
 assert.equal(url.hash,'');assert.equal(url.searchParams.get('rotation'),'2');
 assert.equal(consumeRestartHandoff(url,history),null);
});

test('unrelated, malformed and unsupported handoffs cannot restore a guide',()=>{
 assert.equal(encodeRestartHandoff('s18-scan',{}),'');
 assert.equal(encodeRestartHandoff('s19-rotation',null),'');
 for(const hash of ['#emu-story-once=%',' #unrelated'.trim(),'#emu-story-once='+encodeURIComponent(JSON.stringify({id:'unknown',payload:{}}))]){
  let url=new URL('http://localhost/'+hash);let cleared=false;
  const history={state:null,replaceState(_s,_t,next){cleared=true;url=new URL(next);}};
  assert.equal(consumeRestartHandoff(url,history),null);
  assert.equal(cleared,hash.startsWith('#emu-story-once='));
 }
});
