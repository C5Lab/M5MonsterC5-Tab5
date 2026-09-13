import test from 'node:test';import assert from 'node:assert/strict';
import {StepGuide} from '../tools/ui_emulator/web/queue2-common.mjs';
test('queue guide enforces native evidence, module, visibility and session',()=>{
 const g=new StepGuide({steps:[{accept:(s,g)=>s.job>g.baseline.job},{accept:s=>s.done}]});
 g.start({job:4});const s={tab:0,epoch:g.epoch,job:5};
 g.accept({...s,tab:2});assert.equal(g.step,0);
 g.setContext({tab:0,visible:false});g.accept(s);assert.equal(g.step,0);
 g.setContext({tab:0,visible:true});g.accept({...s,job:4});assert.equal(g.step,0);
 g.accept(s);assert.equal(g.step,1);g.accept({...s,done:true});assert.equal(g.complete,true);
 g.start({job:5});g.accept({...s,done:true});assert.equal(g.step,0);
 g.leave();g.accept({...s,epoch:g.epoch,job:6});assert.equal(g.step,0);
});
