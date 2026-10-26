/** Deterministic offline WPA audit story. No radio, PCAP parsing or key derivation. */
export const WPA_AUDITOR_KEY='tab5-emulator:wpa-auditor';
export const WPA_WORDLISTS=['Demo shortlist','Expanded demo'];
export const wpaFixtureBytes=id=>new TextEncoder().encode(`OFFLINE WPA AUDIT FIXTURE ${id}\n`);
const CANDIDATE_MS=50;
const clone=value=>structuredClone(value);

function fixtures(seed) {
  const network=id=>seed.networks.find(item=>item.id===id);
  for(const id of ['net-1','net-2','net-3','net-6'])
    if(!network(id))throw new Error(`Missing WPA demo network ${id}`);
  return [
    {id:'capture-neon',ssid:network('net-1').ssid,bssid:network('net-1').bssid,
      name:'neon-bazaar-4way.hccapx',format:'HCCAPX',validation:'ready',reason:'Complete M1 + M2 pair',
      sources:['grove','usb'],local:false,match:'Demo shortlist',credential:'NeonDemo-2026!'},
    {id:'capture-ghost',ssid:network('net-2').ssid,bssid:network('net-2').bssid,
      name:'ghost-line-pmkid.22000',format:'22000',validation:'ready',reason:'PMKID ready for audit',
      sources:['mbus'],local:false,match:null,credential:null},
    {id:'capture-chrome',ssid:network('net-3').ssid,bssid:network('net-3').bssid,
      name:'chrome-clinic-incomplete.pcap',format:'PCAP',validation:'invalid',reason:'Missing M2: incomplete handshake',
      sources:['grove'],local:false,match:null,credential:null},
    {id:'capture-synapse',ssid:network('net-6').ssid,bssid:network('net-6').bssid,
      name:'synapse-relay-local.hccapx',format:'HCCAPX',validation:'ready',reason:'Complete M1 + M2 pair',
      sources:[],local:true,match:'Expanded demo',credential:'SynapseDemo-2026!'}
  ];
}

export class WpaAuditorModel {
  #captures;#sessions=[];#history=[];#recovered=[];#queue=[];
  #activeId=null;#batchRunning=false;#nextId=1;#now=0;#uiRevision=0;
  #available;#onChange=null;#localFiles;#seedLocal;
  constructor(seed,{available=()=>true,onChange=null,localFiles=null}={}) {
    this.#captures=fixtures(seed);this.#available=available;this.#localFiles=localFiles;
    this.#seedLocal=new Set(this.#captures.filter(item=>item.local).map(item=>item.id));
    this.setChangeHandler(onChange);
  }
  setChangeHandler(handler) {
    if(handler!==null&&typeof handler!=='function')throw new Error('Invalid audit change handler');
    this.#onChange=handler;
  }
  #changed(ui=true){if(ui)this.#uiRevision++;this.#onChange?.(this.snapshot());}
  revision(){return this.#uiRevision;}
  #capture(id){
    const capture=this.#captures.find(item=>item.id===id);
    if(!capture)throw new Error('Capture not found');
    return capture;
  }
  #checkCapture(id){
    const capture=this.#capture(id);
    if(capture.validation!=='ready')throw new Error('Invalid capture: '+capture.reason);
    if(!capture.local&&!capture.sources.some(source=>this.#available(source)))
      throw new Error('Capture source disconnected');
    return capture;
  }
  #checkWordlist(wordlist){
    if(!WPA_WORDLISTS.includes(wordlist))throw new Error('Unknown wordlist');
  }
  catalog(){return clone(this.#captures.filter(item=>item.local||item.sources.length)
    .map(({match,credential,...item})=>({
    ...item,sources:item.local?['local',...item.sources]:[...item.sources],
    available:item.local||item.sources.some(source=>this.#available(source))
  })));}
  catalogState(id){
    const capture=this.#capture(id);
    if(capture.validation!=='ready')return 'Invalid';
    const queue=[...this.#queue].reverse().find(item=>item.captureId===id);
    if(queue){
      if(queue.state==='found')return 'Found';
      if(queue.state==='not_found')return 'Not found';
      if(['cancelled','error'].includes(queue.state))return 'Error';
      if(['paused','running'].includes(queue.state))return 'Resumable';
    }
    if(this.#sessions.some(item=>item.captureId===id))return 'Resumable';
    const latest=this.#history.find(item=>item.captureId===id);
    return latest?.outcome==='FOUND'?'Found':latest?.outcome==='NOT FOUND'?'Not found':
      latest?.outcome==='ERROR'?'Error':'New';
  }
  captureForFile(source,path,bytes){
    if(typeof source!=='string'||typeof path!=='string'||!this.#available(source))return null;
    const capture=this.#captures.find(item=>item.sources.includes(source)&&
      path===`/sdcard/lab/handshakes/${item.name.replace(/\.[^.]+$/,'.pcap')}`);
    if(!capture||!(bytes instanceof Uint8Array))return null;
    const marker=wpaFixtureBytes(capture.id);
    if(bytes.length!==marker.length||marker.some((value,index)=>bytes[index]!==value))return null;
    return clone({id:capture.id,ssid:capture.ssid,
      validation:capture.validation,reason:capture.reason});
  }
  reconcileLocalFiles(){
    for(const capture of this.#captures)if(capture.local)this.#localFiles?.write(capture);
  }
  wordlists(){return [...WPA_WORDLISTS];}
  sessions(){return clone(this.#sessions);}
  history(){return clone(this.#history);}
  recovered(){return clone(this.#recovered);}
  queue(){return clone(this.#queue);}
  active(){return clone(this.#sessions.find(item=>item.id===this.#activeId)||null);}
  batchRunning(){return this.#batchRunning;}

  syncCapture(id){
    const capture=this.#checkCapture(id);
    if(capture.local)return false;
    this.#localFiles?.write(capture);
    capture.local=true;this.#changed();return true;
  }
  syncAll(){
    const result={copied:0,alreadyLocal:0,skippedInvalid:0};
    try{
      for(const capture of this.#captures){
        if(capture.validation!=='ready'){result.skippedInvalid++;continue;}
        if(capture.local){result.alreadyLocal++;continue;}
        if(capture.sources.some(source=>this.#available(source))){
          this.#localFiles?.write(capture);capture.local=true;result.copied++;
        }
      }
    }finally{if(result.copied)this.#changed();}
    return result;
  }
  deleteLocalCapture(id){
    if(this.#activeId!==null)throw new Error('Delete unavailable while an audit is active');
    const capture=this.#capture(id);
    if(!capture.local)throw new Error('Capture is not on Tab5');
    this.#localFiles?.remove(capture);
    capture.local=false;this.#changed();return true;
  }
  #launch(captureId,wordlist,queueIndex=-1){
    const capture=this.#checkCapture(captureId);this.#checkWordlist(wordlist);
    if(this.#activeId!==null)throw new Error('Audit already running');
    const session={id:`audit-${this.#nextId++}`,captureId:capture.id,
      ssid:capture.ssid,name:capture.name,wordlist,state:'running',tried:0,
      total:wordlist==='Demo shortlist'?100:200,elapsedMs:0,
      workers:capture.local?2:1,queueIndex};
    this.#sessions.push(session);this.#activeId=session.id;
    if(queueIndex>=0)this.#queue[queueIndex].state='running';
    this.#changed();return clone(session);
  }
  start(captureId,wordlist){return this.#launch(captureId,wordlist);}
  pause(){
    const session=this.#sessions.find(item=>item.id===this.#activeId);
    if(!session)return false;
    session.state='paused';this.#activeId=null;
    if(session.queueIndex>=0){
      const item=this.#queue[session.queueIndex];item.state='paused';item.tried=session.tried;
      this.#batchRunning=false;
    }
    this.#changed();return true;
  }
  resume(id){
    if(this.#activeId!==null)throw new Error('Audit already running');
    const session=this.#sessions.find(item=>item.id===id);
    if(!session||session.state!=='paused')throw new Error('No paused audit');
    this.#checkCapture(session.captureId);this.#checkWordlist(session.wordlist);
    session.state='running';this.#activeId=id;
    if(session.queueIndex>=0){this.#queue[session.queueIndex].state='running';this.#batchRunning=true;}
    this.#changed();return clone(session);
  }
  startOver(id){
    if(this.#activeId!==null)throw new Error('Audit already running');
    const session=this.#sessions.find(item=>item.id===id);
    if(!session||session.state!=='paused')throw new Error('No paused audit');
    this.#checkCapture(session.captureId);this.#checkWordlist(session.wordlist);
    session.tried=0;session.elapsedMs=0;session.state='running';this.#activeId=id;
    if(session.queueIndex>=0){
      this.#queue[session.queueIndex].state='running';
      this.#queue[session.queueIndex].tried=0;
      this.#batchRunning=true;
    }
    this.#changed();return clone(session);
  }
  deleteAudit(id){
    const index=this.#sessions.findIndex(item=>item.id===id);
    if(index<0||id===this.#activeId)throw new Error('Cannot delete active or missing audit');
    const session=this.#sessions[index];
    if(session.queueIndex>=0&&this.#queue[session.queueIndex]){
      this.#queue[session.queueIndex].state='pending';
      this.#queue[session.queueIndex].tried=0;
    }
    this.#sessions.splice(index,1);this.#changed();
  }
  addToQueue(ids,wordlist){
    this.#checkWordlist(wordlist);
    if(this.#batchRunning||this.#activeId!==null)throw new Error('Audit busy');
    if(!Array.isArray(ids)||ids.length===0||new Set(ids).size!==ids.length||this.#queue.length+ids.length>8)
      throw new Error('Choose 1 to 8 distinct captures');
    if(ids.some(id=>this.#queue.some(item=>item.captureId===id)))
      throw new Error('Capture already in the queue');
    for(const id of ids)this.#checkCapture(id);
    for(const id of ids)this.#queue.push({captureId:id,wordlist,state:'pending',tried:0});
    this.#changed();return this.queue();
  }
  clearQueue(){
    if(this.#batchRunning||this.#activeId!==null)throw new Error('Audit busy');
    if(this.#queue.some(item=>item.state==='paused'))
      throw new Error('Resume or delete the paused checkpoint before clearing the queue');
    this.#queue=[];this.#changed();
  }
  setQueueWordlist(wordlist){
    this.#checkWordlist(wordlist);
    if(!this.#queue.length||this.#batchRunning||this.#activeId!==null||
       this.#queue.some(item=>item.state!=='pending'))
      throw new Error('Batch candidate source is locked');
    for(const item of this.#queue)item.wordlist=wordlist;
    this.#changed();return true;
  }
  newBatch(){
    if(this.#activeId!==null||this.#batchRunning)throw new Error('Cannot start a new batch while an audit is active');
    for(const session of this.#sessions)if(session.queueIndex>=0)session.queueIndex=-1;
    this.#queue=[];this.#changed();return true;
  }
  removeQueueItem(index){
    if(!Number.isInteger(index)||index<0||index>=this.#queue.length)throw new Error('Queue row not found');
    if(this.#queue[index].state==='running')throw new Error('Cannot remove active queue row');
    this.#queue.splice(index,1);
    for(const session of this.#sessions){
      if(session.queueIndex===index)session.queueIndex=-1;
      else if(session.queueIndex>index)session.queueIndex--;
    }
    if(!this.#queue.length)this.#batchRunning=false;
    this.#changed();return true;
  }
  skipCurrent(){
    const session=this.#sessions.find(item=>item.id===this.#activeId);
    if(!this.#batchRunning||!session||session.queueIndex<0)
      throw new Error('No active batch item to skip');
    this.#queue[session.queueIndex].state='cancelled';
    this.#queue[session.queueIndex].tried=session.tried;
    session.state='paused';session.queueIndex=-1;this.#activeId=null;
    this.#changed();this.#nextBatch();return true;
  }
  cancelBatch(){
    if(!this.#queue.some(item=>['pending','running','paused'].includes(item.state)))
      throw new Error('No unfinished batch to cancel');
    const active=this.#sessions.find(item=>item.id===this.#activeId&&item.queueIndex>=0);
    if(active){active.state='paused';this.#activeId=null;}
    for(const session of this.#sessions)if(session.queueIndex>=0)session.queueIndex=-1;
    for(const item of this.#queue)if(['pending','running','paused'].includes(item.state))
      item.state='cancelled';
    this.#batchRunning=false;this.#changed();return true;
  }
  #nextBatch(){
    const index=this.#queue.findIndex(item=>item.state==='pending');
    if(index<0){this.#batchRunning=false;this.#changed();return;}
    const item=this.#queue[index];this.#launch(item.captureId,item.wordlist,index);
  }
  startBatch(){
    if(this.#queue.some(item=>item.state==='paused'))
      throw new Error('Resume paused batch before starting another item');
    if(!this.#queue.some(item=>item.state==='pending'))throw new Error('Queue is empty');
    if(this.#activeId!==null)throw new Error('Audit already running');
    this.#batchRunning=true;this.#nextBatch();
  }
  pauseBatch(){return this.pause();}
  resumeBatch(){
    if(this.#activeId!==null)throw new Error('Audit already running');
    const paused=this.#queue.findIndex(item=>item.state==='paused');
    this.#batchRunning=true;
    if(paused>=0){
      const session=this.#sessions.find(item=>item.queueIndex===paused&&item.state==='paused');
      if(!session)throw new Error('Queue checkpoint missing');
      this.resume(session.id);
    } else this.#nextBatch();
  }
  #finish(session){
    const capture=this.#capture(session.captureId);
    const found=capture.match===session.wordlist;
    const outcome=found?'FOUND':'NOT FOUND';
    this.#history.unshift({id:session.id,captureId:session.captureId,ssid:session.ssid,
      name:session.name,wordlist:session.wordlist,outcome,tried:session.tried,
      elapsedMs:session.elapsedMs,workers:session.workers});
    this.#history.length=Math.min(this.#history.length,8);
    if(found){
      this.#recovered=this.#recovered.filter(item=>item.captureId!==session.captureId);
      this.#recovered.unshift({captureId:session.captureId,ssid:session.ssid,
        credential:capture.credential,source:'Synthetic fixture'});
    }
    if(session.queueIndex>=0){
      const item=this.#queue[session.queueIndex];item.tried=session.tried;
      item.state=found?'found':'not_found';
    }
    this.#sessions=this.#sessions.filter(item=>item.id!==session.id);
    this.#activeId=null;this.#changed();
    if(this.#batchRunning)this.#nextBatch();
  }
  advance(ms){
    if(!Number.isFinite(ms)||ms<0)throw new Error('Invalid audit time');
    this.#now+=ms;
    let remaining=ms;
    while(remaining>0&&this.#activeId!==null){
      const session=this.#sessions.find(item=>item.id===this.#activeId);
      const before=Math.floor(session.tried/20);
      const untilDone=session.total*CANDIDATE_MS-session.elapsedMs;
      const step=Math.min(remaining,untilDone);
      session.elapsedMs+=step;session.tried=Math.min(session.total,Math.floor(session.elapsedMs/CANDIDATE_MS));
      if(session.queueIndex>=0)this.#queue[session.queueIndex].tried=session.tried;
      remaining-=step;
      if(session.tried>=session.total)this.#finish(session);
      else if(Math.floor(session.tried/20)>before)this.#changed(false);
    }
  }
  snapshot(){return clone({version:1,local:this.#captures.filter(item=>item.local).map(item=>item.id),
    sessions:this.#sessions,history:this.#history,recovered:this.#recovered,queue:this.#queue,
    activeId:this.#activeId,batchRunning:this.#batchRunning,nextId:this.#nextId,now:this.#now});}
  restore(raw){
    if(!raw||raw.version!==1||!Array.isArray(raw.local)||!Array.isArray(raw.sessions)||
      !Array.isArray(raw.history)||!Array.isArray(raw.recovered)||!Array.isArray(raw.queue)||
      !Number.isSafeInteger(raw.nextId)||raw.nextId<1||!Number.isFinite(raw.now)||raw.now<0||
      raw.sessions.length>8||raw.queue.length>8||raw.history.length>8)return false;
    const ids=new Set(this.#captures.map(item=>item.id));
    if(raw.local.some(id=>!ids.has(id))||raw.sessions.some(s=>!ids.has(s.captureId)||
       !WPA_WORDLISTS.includes(s.wordlist)||!['running','paused'].includes(s.state)||
       !Number.isInteger(s.tried)||s.tried<0||s.tried>s.total)||
       raw.queue.some(item=>!ids.has(item.captureId)||!WPA_WORDLISTS.includes(item.wordlist)))return false;
    // Audit history is durable, but virtual SD copies belong to this page session.
    for(const capture of this.#captures)capture.local=this.#seedLocal.has(capture.id);
    this.#sessions=clone(raw.sessions).map(s=>({...s,state:'paused'}));
    this.#history=clone(raw.history);this.#recovered=clone(raw.recovered);
    this.#queue=clone(raw.queue).map(item=>item.state==='running'?{...item,state:'paused'}:item);
    this.#activeId=null;this.#batchRunning=false;this.#nextId=raw.nextId;this.#now=raw.now;
    return true;
  }
}

export class WpaAuditorStore {
  #disk;#session;#state=null;
  recovered=false;mode='persistent';
  constructor(disk,session=null){
    this.#disk=disk;this.#session=session;
    let raw=null;
    try{raw=session?.getItem(WPA_AUDITOR_KEY)??null;}catch{}
    if(raw===null)try{raw=disk?.getItem(WPA_AUDITOR_KEY)??null;}catch{this.mode='session';}
    if(raw===null)return;
    try{
      const record=JSON.parse(raw);
      if(!record||record.version!==1||!record.state||record.state.version!==1)
        throw new Error('Invalid WPA audit record');
      this.#state=clone(record.state);
    }catch{this.recovered=true;}
  }
  load(){return clone(this.#state);}
  save(state){
    this.#state=clone(state);
    const raw=JSON.stringify({version:1,state:this.#state});
    try{this.#disk.setItem(WPA_AUDITOR_KEY,raw);this.mode='persistent';
      try{this.#session?.removeItem(WPA_AUDITOR_KEY);}catch{}
    }catch{this.mode='session';
      try{this.#session.setItem(WPA_AUDITOR_KEY,raw);}catch{this.mode='memory';}
    }
  }
  reset(){
    this.#state=null;
    try{this.#disk?.removeItem(WPA_AUDITOR_KEY);}catch{this.mode='memory';}
    try{this.#session?.removeItem(WPA_AUDITOR_KEY);}catch{this.mode='memory';}
  }
}
