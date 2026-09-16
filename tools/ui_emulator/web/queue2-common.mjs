// Guide predicates consume observed native state; no control is clicked here.
export function readUI(module){
 module._emu_inspect();
 const objects=globalThis.emulatorObjects.map(o=>({...o,binding:module.UTF8ToString(module._emu_binding_id(o.id))}));
 return {tab:module._emu_current_tab(),objects,
  has:(text,contains=false)=>objects.some(o=>contains?o.text?.includes(text):o.text===text),
  bound:fragment=>objects.find(o=>o.binding.includes(fragment))};
}
export class StepGuide{
 active=false;complete=false;epoch=0;step=0;hint='';tab=0;visible=true;
 constructor(story){this.story=story;}
 start(s={}){this.epoch++;this.active=true;this.complete=false;this.step=0;this.hint='';this.baseline={...s};this.memory={};this.story.onStart?.(s,this);}
 leave(){this.epoch++;this.active=false;this.memory={};}
 setContext({tab,visible}){if(tab!==this.tab||visible!==this.visible)this.story.onContextChange?.(this);this.tab=tab;this.visible=visible;}
 accept(s){
  if(!this.active||this.complete||!this.visible||this.tab!==(this.story.tab??0)||s.tab!==(this.story.tab??0)||s.epoch!==this.epoch)return;
  if(this.story.guard?.(s,this)===false)return;
  if(this.story.steps[this.step].accept(s,this)){this.step++;this.hint='';this.complete=this.step===this.story.steps.length;}
 }
}
