import test from 'node:test';
import assert from 'node:assert/strict';
import {StartupGuide, startupStory} from '../tools/ui_emulator/web/startup-story.mjs';

function run(condition){
 const g=new StartupGuide(startupStory(condition));g.start();
 const send=(state,timeMs=0)=>g.accept({epoch:1,timeMs,...state});
 if(condition==='missing-board'){send({noBoard:true});send({groveHome:true});}
 if(condition==='version-mismatch'){send({versionWarning:true});send({groveHome:true});}
 send({groveHome:true});
 if(condition==='missing-sd'){send({sdWarning:true,wardriveWarning:true});send({groveHome:true});}
 send({mbusHome:true});send({internalHome:true});send({status:true});
 send({status:true,groveCard:true},0);send({status:true,groveCard:true},500);send({status:true,groveCard:true},1000);
 send({status:true,mbusCard:true},1100);send({status:true,mbusCard:true},1600);send({status:true,mbusCard:true},2100);
 assert.equal(g.complete,false);send({internalHome:true},2200);assert.equal(g.complete,true);
}
for(const condition of ['normal','missing-board','missing-sd','version-mismatch'])test(`ordered startup tour: ${condition}`,()=>run(condition));

test('wrong screen, unseen warning, stale session and hidden document cannot advance',()=>{
 const g=new StartupGuide(startupStory('missing-board'));g.start();
 g.accept({epoch:1,groveHome:true});assert.equal(g.step,0);
 g.accept({epoch:0,noBoard:true});g.accept({epoch:1,groveHome:true});assert.equal(g.step,0);
 g.setContext({tab:0,visible:false});g.accept({epoch:1,noBoard:true});
 g.setContext({tab:0,visible:true});g.accept({epoch:1,groveHome:true});assert.equal(g.step,0);
 g.accept({epoch:1,noBoard:true});g.start();g.accept({epoch:2,groveHome:true});assert.equal(g.step,0);
 g.leave();g.accept({epoch:3,noBoard:true});assert.equal(g.active,false);
});

test('partial card viewing resets when the card is clipped or another tab is selected',()=>{
 const g=new StartupGuide(startupStory('normal'));g.start();
 for(const key of ['groveHome','mbusHome','internalHome','status'])g.accept({epoch:1,[key]:true});
 g.accept({epoch:1,status:true,groveCard:true,timeMs:0});
 g.accept({epoch:1,status:true,groveCard:false,timeMs:500});
 g.accept({epoch:1,status:true,groveCard:true,timeMs:1000});assert.equal(g.step,4);
 g.setContext({tab:2,visible:true});g.setContext({tab:1,visible:true});
 g.accept({epoch:1,status:true,groveCard:true,timeMs:1600});assert.equal(g.step,4);
});

test('SD warning must belong to the requested Grove Wardrive route',()=>{
 const g=new StartupGuide(startupStory('missing-sd'));g.start();
 g.accept({epoch:1,groveHome:true});
 g.accept({epoch:1,sdWarning:true,wardriveWarning:false});assert.equal(g.step,1);
 g.accept({epoch:1,sdWarning:true,wardriveWarning:true});assert.equal(g.step,2);
});
