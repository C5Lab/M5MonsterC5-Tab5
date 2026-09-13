import { syntheticPcap } from './pcap.mjs';
import { pcapFixtures } from './pcap-deep.mjs';
import { toolOptions, toolResult } from './nettools.mjs';
import { attackOptions, attackResult, observerResult } from './attacks.mjs';

/** Deterministic virtual device. No network, timers, filesystem or hardware I/O. */
export class SimulatedDevice {
  #seed; #modules; #jobs=new Map(); #nextId=1; #now=0; #capacity;

  constructor(seed,{sdCapacityBytes=8*1024*1024}={}) {
    if(seed.schema_version!==1) throw new Error('Unsupported scenario version');
    for(const [key,max] of [['modules',16],['networks',256],['clients',1024]]) {
      if(!Array.isArray(seed[key])||seed[key].length>max) throw new Error('Invalid scenario '+key);
      const ids=seed[key].map(item=>item.id);
      if(ids.some(id=>typeof id!=='string'||!id)||new Set(ids).size!==ids.length)
        throw new Error('Invalid scenario identities: '+key);
    }
    if(seed.clients.some(c=>!seed.networks.some(n=>n.id===c.network_id)))
      throw new Error('Client references unknown network');
    if(!Number.isSafeInteger(sdCapacityBytes)||sdCapacityBytes<0) throw new Error('Invalid SD capacity');
    this.#seed=structuredClone(seed);this.#capacity=sdCapacityBytes;this.reset();
  }

  reset() {
    this.#now=0;this.#jobs.clear();
    // Operation IDs never repeat, including across reset. Planned HTML seed
    // entries have no bytes yet and are intentionally not advertised as files.
    this.#modules=new Map(this.#seed.modules.map(module=>[module.id,{
      connected:module.connected,sdPresent:module.sd_present,networks:[],selected:null,
      version:'demo-main-1',activeSlot:0,slots:['demo-main-1','demo-backup-1'],boardName:`Offline demo ${module.id}`,bootedAt:0,reboots:0,revision:1,
      active:null,files:new Map(),sequence:0,gpsEnabled:true,wardriveSecond:1,wardrivePoints:0,wardriveBlacklist:[],
      observer:{running:false,networks:[],packets:0,elapsedMs:0,error:null},observerStarted:0,observerIds:[],
      wardrive:{running:false,gps:{fix:false,lat:0,lon:0,satellites:0,distanceM:0},wifiCount:0,btCount:0,rows:[],track:[],sessions:[],error:null,upload:null}
    }]));
  }

  #module(id) {
    const module=this.#modules.get(id);
    if(!module) throw new Error('Unknown module');
    return module;
  }

  #available(id) {
    const module=this.#module(id);
    if(!module.connected) throw new Error('Module disconnected');
    if(module.active!==null) throw new Error('Module radio busy');
    return module;
  }

  #start(kind,moduleId,duration,payload={}) {
    const module=this.#available(moduleId);
    // Retain bounded diagnostic history; never evict active operations.
    if(this.#jobs.size>=256) {
      const old=[...this.#jobs].find(([,j])=>j.state!=='running');
      if(old)this.#jobs.delete(old[0]);
    }
    const id=this.#nextId++;
    this.#jobs.set(id,{id,kind,moduleId,state:'running',progress:0,error:null,
                       started:this.#now,duration,...structuredClone(payload)});
    module.active=id;return id;
  }

  systemStatus(moduleId) {
    const module=this.#module(moduleId);
    return structuredClone({connected:module.connected,sdPresent:module.sdPresent,
      version:module.version,activeSlot:module.activeSlot,slots:module.slots,boardName:module.boardName,uptimeMs:this.#now-module.bootedAt,
      reboots:module.reboots,active:module.active,sdUsedBytes:this.#used(module),sdCapacityBytes:this.#capacity});
  }

  systemActivateSlot(moduleId,slot) {
    if(![0,1].includes(slot))throw new Error('Invalid OTA slot');
    const id=this.systemStart(moduleId,'reboot');this.#jobs.get(id).slot=slot;return id;
  }

  systemStart(moduleId,operation,options={}) {
    const module=this.#available(moduleId);
    if(!['update','reboot','sd_admin'].includes(operation))throw new Error('Invalid system operation');
    if(!options||typeof options!=='object'||Array.isArray(options)||
      ![Object.prototype,null].includes(Object.getPrototypeOf(options))||
      Reflect.ownKeys(options).some(key=>!['channel','outcome'].includes(key)||operation!=='update'))
      throw new Error('Invalid system options');
    const channel=options.channel===undefined?'main':options.channel,outcome=options.outcome===undefined?'success':options.outcome;
    if(!['main','dev'].includes(channel)||!['success','failure'].includes(outcome))
      throw new Error('Invalid system options');
    const retained=operation==='update'?{channel,outcome}:{};
    if(operation==='sd_admin'&&!module.sdPresent)throw new Error('SD missing');
    return this.#start('system',moduleId,operation==='sd_admin'?Infinity:operation==='update'?3000:1000,
      {operation,options:retained,result:{stage:operation==='sd_admin'?'running':operation==='update'?'preparing':'restarting',simulated:true,...(operation==='update'?{channel}:{})}});
  }

  #advanceSystem(module,job) {
    if(job.operation==='sd_admin') {
      job.progress=Math.min(.95,(this.#now-job.started)/1000);
      return;
    }
    job.progress=Math.min(1,(this.#now-job.started)/job.duration);
    job.result.stage=job.operation==='reboot'?'restarting':job.progress<1/3?'preparing':job.progress<2/3?'downloading':'installing';
    if(job.progress<1)return;
    if(job.operation==='update'&&job.options.outcome==='failure') {
      job.error='simulated_update_failed';job.result.stage='failed';job.state='failed';
    } else {
      if(job.operation==='update'){module.version=`demo-${job.options.channel}-${++module.revision}`;module.slots[module.activeSlot]=module.version;}
      if(job.slot!==undefined){module.activeSlot=job.slot;module.version=module.slots[job.slot];}
      module.reboots++;module.bootedAt=job.started+job.duration;module.connected=true;
      module.networks=[];module.selected=null;
      module.observer={running:false,networks:[],packets:0,elapsedMs:0,error:null};
      module.observerIds=[];module.observerStarted=module.bootedAt;
      job.result.stage='completed';job.state='completed';
    }
    module.active=null;
  }

  scan(moduleId) {
    const duration=this.#seed.settings?.scan_time_ms ?? 1000;
    if(!Number.isFinite(duration)||duration<=0) throw new Error('Invalid scan duration');
    const id=this.#start('scan',moduleId,duration);
    const module=this.#module(moduleId);module.networks=[];module.selected=null;
    return id;
  }

  select(moduleId,networkId) {
    const module=this.#available(moduleId);
    if(![...module.networks,...module.observer.networks].some(n=>n.id===networkId)) throw new Error('Network not discovered');
    module.selected=networkId;
  }

  clients(moduleId) {
    const selected=this.#module(moduleId).selected;
    return structuredClone(this.#seed.clients.filter(c=>c.network_id===selected));
  }

  capture(moduleId) {
    const module=this.#available(moduleId);
    if(!module.sdPresent) throw new Error('SD missing');
    const network=[...module.networks,...module.observer.networks].find(n=>n.id===module.selected);
    if(!network) throw new Error('Select a discovered network');
    const clients=this.clients(moduleId);
    if(!clients.length) throw new Error('No clients for selected network');
    // Validate the data before acquiring the radio; encoding errors cannot
    // strand a running operation or commit an incomplete file.
    syntheticPcap(network,clients);
    return this.#start('capture',moduleId,2000,{network,clients});
  }

  cancel(id) {
    const job=this.#jobs.get(id);
    if(!job||job.state!=='running') return false;
    job.state='cancelled';const module=this.#module(job.moduleId);module.active=null;
    if(job.kind==='wardrive') {module.wardrive.running=false;module.wardrive.error='cancelled';module.wardrive.gps.fix=false;module.wardrive.gps.satellites=0;}
    return true;
  }

  toolStart(moduleId,kind,input={}) {
    const module=this.#available(moduleId);
    const options=toolOptions(kind,input,this.#seed.networks);
    if((kind==='gitm'||kind==='wpasec')&&!module.sdPresent)throw new Error('SD missing');
    return this.#start('tool',moduleId,2000,{tool:kind,options,result:null});
  }

  attackTemplates(moduleId) {
    const module=this.#module(moduleId);
    if(!module.connected)throw new Error('Module disconnected');
    if(!module.sdPresent)throw new Error('SD missing');
    return [{name:'Offline demo',path:'/sdcard/lab/portals/offline-demo.html'},
      ...this.files(moduleId).filter(f=>f.path.endsWith('.html')).map(f=>({name:f.path.split('/').pop(),path:f.path}))];
  }

  attackStart(moduleId,kind,input={}) {
    const module=this.#available(moduleId);
    const options=attackOptions(kind,input,this.#seed.networks,this.#seed.clients);
    if(['mitm','evil_twin','rogue_ap'].includes(kind)&&!module.sdPresent)throw new Error('SD missing');
    if(options.portal&&!this.attackTemplates(moduleId).some(p=>p.path===options.portal||p.name===options.portal))throw new Error('Unknown demo portal');
    return this.#start('attack',moduleId,2000,{operation:kind,options,
      result:attackResult(kind,options,0,this.#seed.networks,this.#seed.clients)});
  }

  attackStop(id) {
    const job=this.#jobs.get(id);
    if(!job||job.kind!=='attack'||job.state!=='running')return false;
    const module=this.#module(job.moduleId);
    if(job.operation==='mitm') {
      const network=this.#seed.networks.find(n=>n.id===job.options.networkIds[0]);
      const clients=this.#seed.clients.filter(c=>c.network_id===network.id);
      const bytes=syntheticPcap(network,clients,1700000000+Math.floor(job.started/1000));
      if(!module.sdPresent)job.error='sd_missing';
      else if(this.#used(module)+bytes.length>this.#capacity)job.error='storage_full';
      else {
        const path=`/sdcard/lab/pcaps/${job.moduleId}/mitm-demo-${++module.sequence}.pcap`;
        module.files.set(path,{path,moduleId:job.moduleId,networkId:network.id,
          clientIds:clients.map(c=>c.id),packetCount:clients.length,kind:'mitm',bytes});
        job.file=path;job.result.file=path;
      }
    }
    job.progress=1;job.state=job.error?'failed':'completed';module.active=null;return true;
  }

  observerStart(moduleId,ids) {
    const module=this.#available(moduleId);
    const requested=ids??this.#seed.networks.map(n=>n.id);
    if(!Array.isArray(requested))throw new Error('Invalid Observer targets');
    const selected=[...new Set(requested.map(id=>{
      const n=this.#seed.networks.find(n=>n.id===id||n.bssid===id);
      if(!n)throw new Error('Unknown Observer network');return n.id;
    }))];
    module.observerIds=selected;module.observerStarted=this.#now;
    module.observer=observerResult(selected,0,this.#seed.networks,this.#seed.clients);
    return this.observer(moduleId);
  }

  observerStop(moduleId) {this.#module(moduleId).observer.running=false;return this.observer(moduleId);}
  observer(moduleId) {return structuredClone(this.#module(moduleId).observer);}

  toolStop(id) {
    const job=this.#jobs.get(id);
    if(!job||job.kind!=='tool'||job.tool!=='gitm'||job.state!=='running')return false;
    const module=this.#module(job.moduleId);
    const bytes=pcapFixtures()[0].bytes;
    if(!module.sdPresent)job.error='sd_missing';
    else if(this.#used(module)+bytes.length>this.#capacity)job.error='storage_full';
    else {
      const path=`/sdcard/lab/pcaps/${job.moduleId}/${job.options.prefix}-${++module.sequence}.pcap`;
      module.files.set(path,{path,moduleId:job.moduleId,kind:'gitm',packetCount:14,bytes:bytes.slice()});
      job.file=path;job.result={...this.#gitmResult(),file:path};
    }
    job.state=job.error?'failed':'completed';job.progress=1;module.active=null;
    return true;
  }

  #gitmResult() {
    return {packets:14,bytes:pcapFixtures()[0].bytes.length,clients:[
      {mac:'02:00:00:00:00:10',ip:'192.0.2.10',hostname:'offline-demo'}]};
  }

  setModule(moduleId,{connected,sdPresent}={}) {
    const module=this.#module(moduleId);
    if(connected!==undefined) {
      if(typeof connected!=='boolean') throw new Error('Invalid connection state');
      module.connected=connected;
      if(!connected){this.cancel(module.active);module.observer.running=false;module.observer.error='disconnected';}
    }
    if(sdPresent!==undefined) {
      if(typeof sdPresent!=='boolean') throw new Error('Invalid SD state');
      module.sdPresent=sdPresent;
      const job=this.#jobs.get(module.active);
      if(!sdPresent&&job?.kind==='system'&&job.operation==='sd_admin') {
        job.state='failed';job.error='sd_missing';job.result.stage='failed';module.active=null;
      }
    }
  }

  advance(elapsed) {
    if(!Number.isFinite(elapsed)||elapsed<0||!Number.isSafeInteger(Math.floor(this.#now+elapsed)))
      throw new Error('Invalid elapsed time');
    this.#now+=elapsed;
    // At most one operation per module, with bounded scenario cardinalities.
    for(const [moduleId,module] of this.#modules) {
      if(module.observer.running)module.observer=observerResult(module.observerIds,this.#now-module.observerStarted,this.#seed.networks,this.#seed.clients);
      if(module.active===null)continue;
      const job=this.#jobs.get(module.active);
      if(job.kind==='system') {this.#advanceSystem(module,job);continue;}
      if(job.kind==='attack') {
        job.progress=Math.min(.95,(this.#now-job.started)/job.duration);
        job.result=attackResult(job.operation,job.options,this.#now-job.started,this.#seed.networks,this.#seed.clients);
        continue;
      }
      if(job.kind==='wardrive') {this.#advanceWardrive(module,job);continue;}
      if(job.kind==='tool'&&job.tool==='iot') {
        const elapsed=this.#now-job.started;
        if(elapsed>=20000)job.result=toolResult('iot',job.options,this.#seed.networks,elapsed);
        continue;
      }
      if(job.kind==='tool'&&job.tool==='gitm') {
        job.progress=Math.min(.95,(this.#now-job.started)/job.duration);
        if(this.#now-job.started>=500)job.result=this.#gitmResult();
        continue;
      }
      job.progress=Math.min(1,(this.#now-job.started)/job.duration);
      if(job.kind==='scan') {
        module.networks=structuredClone(this.#seed.networks.slice(0,Math.floor(job.progress*this.#seed.networks.length)));
      }
      if(job.progress<1)continue;
      if(job.kind==='tool') {
        if(job.tool==='wpasec'&&!module.sdPresent)job.error='sd_missing';
        else {
          job.result=toolResult(job.tool,job.options,this.#seed.networks);
          if(job.tool==='wpasec'&&job.options.outcome==='failure')job.error='simulated_upload_failed';
        }
      }
      if(job.kind==='capture') {
        const bytes=syntheticPcap(job.network,job.clients,1700000000+Math.floor(job.started/1000));
        const used=this.#used(module);
        if(!module.sdPresent)job.error='sd_missing';
        else if(used+bytes.length>this.#capacity)job.error='storage_full';
        else {
          const path=`/sdcard/lab/pcaps/${moduleId}/synthetic-${++module.sequence}.pcap`;
          module.files.set(path,{path,networkId:job.network.id,moduleId,
            clientIds:job.clients.map(c=>c.id),packetCount:job.clients.length,bytes});
          job.file=path;
        }
      }
      job.state=job.error?'failed':'completed';module.active=null;
    }
  }

  snapshot(moduleId) {
    const module=this.#module(moduleId);
    return structuredClone({connected:module.connected,sdPresent:module.sdPresent,
      networks:module.networks,selected:module.selected,active:module.active,now:this.#now});
  }

  job(id) {return structuredClone(this.#jobs.get(id)??null);}

  files(moduleId) {
    return [...this.#module(moduleId).files.values()].map(({bytes,...metadata})=>
      structuredClone({...metadata,sizeBytes:bytes.length}));
  }

  installPcapFixtures(moduleId) {
    const module=this.#available(moduleId);
    if(!module.sdPresent)throw new Error('SD missing');
    const files=pcapFixtures().map(file=>({...file,moduleId,
      path:`/sdcard/lab/pcaps/${moduleId}/example-${file.fixtureId}.pcap`}));
    const replaced=files.reduce((sum,f)=>sum+(module.files.get(f.path)?.bytes.length||0),0);
    if(this.#used(module)-replaced+files.reduce((sum,f)=>sum+f.bytes.length,0)>this.#capacity)
      throw new Error('storage_full');
    for(const file of files)module.files.set(file.path,file);
    return this.files(moduleId).filter(file=>files.some(f=>f.path===file.path));
  }

  readFile(moduleId,path) {
    const module=this.#module(moduleId);
    if(!module.sdPresent)throw new Error('SD missing');
    const file=module.files.get(path);
    if(!file)throw new Error('File not found');
    return file.bytes.slice();
  }

  commitFileChanges(moduleId,writes=[],deletes=[]) {
    const module=this.#available(moduleId);
    if(!module.sdPresent)throw new Error('SD missing');
    if(module.active!==null)throw new Error('Module busy');
    // Absolute paths have exactly one leading empty segment.
    const pathOK=p=>typeof p==='string'&&p.startsWith('/sdcard/')&&!p.includes('\\')&&
      !p.slice(1).split('/').some(part=>part==='.'||part==='..'||part==='')&&!p.includes('\0');
    if(!Array.isArray(writes)||!Array.isArray(deletes)||writes.some(w=>!pathOK(w.path)||!(w.bytes instanceof Uint8Array))||deletes.some(p=>!pathOK(p)))
      throw new Error('Invalid virtual file change');
    const next=new Map(module.files);
    for(const path of deletes)next.delete(path);
    for(const {path,bytes} of writes)next.set(path,{...(next.get(path)||{kind:'artifact'}),moduleId,path,bytes:bytes.slice()});
    const used=[...next.values()].reduce((n,f)=>n+f.bytes.length,0)+module.wardrive.sessions.reduce((n,s)=>n+s.sizeBytes,0);
    if(used>this.#capacity)throw new Error('storage_full');
    module.files=next;
    return this.files(moduleId);
  }

  deleteFile(moduleId,path) {
    const module=this.#available(moduleId);
    if(!module.sdPresent)throw new Error('SD missing');
    if(!module.files.delete(path))throw new Error('File not found');
    return {path,deleted:true};
  }

  wardriveDeleteFiles(moduleId,paths) {
    const module=this.#available(moduleId);
    if(!module.sdPresent)throw new Error('SD missing');
    if(!Array.isArray(paths)||paths.some(p=>!module.wardrive.sessions.some(s=>s.path===p)))
      throw new Error('Wardrive file not found');
    module.wardrive.sessions=module.wardrive.sessions.filter(s=>!paths.includes(s.path));
    return {deleted:paths.length};
  }

  wardriveCleanup(moduleId,{service='all',status='done',move=false}={}) {
    const module=this.#available(moduleId);
    if(!module.sdPresent)throw new Error('SD missing');
    if(!['all','wigle','wdgwars'].includes(service)||status!=='done')throw new Error('Unsupported cleanup filter');
    const sessions=module.wardrive.sessions.filter(s=>!s.archived);
    const matched=sessions.filter(s=>service==='all'
      ? ['wigle','wdgwars'].every(p=>s.uploadStatus?.[p]==='done')
      : s.uploadStatus?.[service]==='done');
    const paths=matched.map(s=>s.path);
    if(move)for(const s of matched){s.originalPath=s.path;s.path=s.path.replace(/\/([^/]+)$/,'/uploaded/$1');s.archived=true;}
    return {scanned:sessions.length,matched:matched.length,moved:move?matched.length:0,paths};
  }

  #used(module) {
    return [...module.files.values()].reduce((sum,f)=>sum+f.bytes.length,0)+
      module.wardrive.sessions.reduce((sum,s)=>sum+s.sizeBytes,0);
  }

  wardriveStart(moduleId) {
    if(this.#seed.gps?.route&&(!Array.isArray(this.#seed.gps.route)||this.#seed.gps.route.length>600))
      throw new Error('Invalid GPS route');
    for(const p of this.#seed.gps?.route||[]) {
      const lat=p.lat??p.latitude,lon=p.lon??p.longitude;
      if(!Number.isFinite(lat)||!Number.isFinite(lon)||Math.abs(lat)>90||Math.abs(lon)>180)
        throw new Error('Invalid GPS route');
    }
    const id=this.#start('wardrive',moduleId,Infinity);
    const module=this.#module(moduleId),w=module.wardrive;
    module.wardriveSecond=1;module.wardrivePoints=0;
    Object.assign(w,{running:true,gps:{fix:false,lat:0,lon:0,satellites:0,distanceM:0},
      wifiCount:0,btCount:0,rows:[],track:[],error:null,upload:null});
    return id;
  }

  #advanceWardrive(module,job) {
    const w=module.wardrive,seconds=Math.floor((this.#now-job.started)/1000);
    if(!module.gpsEnabled) {module.wardriveSecond=Math.max(1,seconds);w.gps.fix=false;w.gps.satellites=0;return;}
    if(seconds<2)return; // Deterministic simulated GPS acquisition.
    const added=seconds-module.wardriveSecond;
    if(added<=0)return;
    const configured=this.#seed.gps?.route;
    const route=Array.isArray(configured)&&configured.length ? configured :
      [{lat:52.2297,lon:21.0122},{lat:52.2301,lon:21.0130},{lat:52.2305,lon:21.0134},{lat:52.2301,lon:21.0126}];
    const point=index=>{
      const p=route[index%route.length];
      const lat=p.lat??p.latitude,lon=p.lon??p.longitude;
      if(!Number.isFinite(lat)||!Number.isFinite(lon)||Math.abs(lat)>90||Math.abs(lon)>180)
        throw new Error('Invalid GPS route');
      return {lat,lon};
    };
    // Only valid-fix seconds generate points. Route progression pauses through a
    // no-fix interval; resuming must not invent samples for the missing period.
    const count=module.wardrivePoints+added;
    const fresh=Array.from({length:Math.min(added,600)},(_,i)=>{
      const offset=Math.max(0,added-600)+i;
      return {...point(module.wardrivePoints+offset),timeMs:job.started+(module.wardriveSecond+offset+1)*1000};
    });
    module.wardrivePoints=count;module.wardriveSecond=seconds;
    w.track=[...w.track,...fresh].slice(-600);
    const distance=(a,b)=>{
      const radians=x=>x*Math.PI/180;
      const h=Math.sin(radians(b.lat-a.lat)/2)**2+
        Math.cos(radians(a.lat))*Math.cos(radians(b.lat))*Math.sin(radians(b.lon-a.lon)/2)**2;
      return 6371000*2*Math.asin(Math.sqrt(Math.min(1,h)));
    };
    const segments=route.map((_,i)=>distance(point(i),point(i+1)));
    const steps=count-1,distanceM=Math.floor(steps/route.length)*segments.reduce((a,b)=>a+b,0)+
      segments.slice(0,steps%route.length).reduce((a,b)=>a+b,0);
    w.gps={fix:true,...point(count-1),satellites:8,distanceM};
    w.wifiCount=Math.min(count,this.#seed.networks.length);
    const bluetooth=(this.#seed.bluetooth||[]).filter(n=>!module.wardriveBlacklist.includes(n.mac.toUpperCase()));
    w.btCount=Math.min(Math.floor(count/2),bluetooth.length);
    w.rows=[...this.#seed.networks.slice(0,w.wifiCount).map((n,i)=>({
      bssid:n.bssid,ssid:n.ssid,security:n.security,kind:'wifi',...point(i)})),
      ...bluetooth.slice(0,w.btCount).map((n,i)=>({
        bssid:n.mac,ssid:n.name,security:'BLE',kind:'bluetooth',...point(i*2+1)}))];
  }

  wardriveStop(moduleId) {
    const module=this.#module(moduleId),w=module.wardrive;
    if(!w.running)return this.wardrive(moduleId);
    const job=this.#jobs.get(module.active);
    w.running=false;module.active=null;
    const sizeBytes=new TextEncoder().encode(JSON.stringify({track:w.track,rows:w.rows})).length;
    w.error=!module.sdPresent?'sd_missing':w.sessions.length>=128?'session_limit':
      this.#used(module)+sizeBytes>this.#capacity?'storage_full':null;
    job.error=w.error;job.state=w.error?'failed':'completed';job.progress=1;
    if(!w.error)w.sessions.push({id:job.id,moduleId,path:`/sdcard/lab/wardrive/${moduleId}/session-${job.id}.json`,
      started:job.started,ended:this.#now,wifiCount:w.wifiCount,btCount:w.btCount,
      track:structuredClone(w.track),rows:structuredClone(w.rows),sizeBytes,uploaded:false,
      uploadStatus:{wigle:'pending',wdgwars:'pending'}});
    return this.wardrive(moduleId);
  }

  wardrive(moduleId) {return structuredClone(this.#module(moduleId).wardrive);}

  wardriveSetBlacklist(moduleId,macs) {
    if(!Array.isArray(macs)||macs.length>256||macs.some(mac=>typeof mac!=='string'||!/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(mac)))
      throw new Error('Invalid Bluetooth blacklist');
    const module=this.#module(moduleId);module.wardriveBlacklist=[...new Set(macs.map(mac=>mac.toUpperCase()))];
    module.wardrive.rows=module.wardrive.rows.filter(row=>row.kind!=='bluetooth'||!module.wardriveBlacklist.includes(row.bssid.toUpperCase()));
    module.wardrive.btCount=module.wardrive.rows.filter(row=>row.kind==='bluetooth').length;
  }

  wardriveSetGps(moduleId,fix) {
    if(typeof fix!=='boolean')throw new Error('Invalid GPS fix');
    const module=this.#module(moduleId);module.gpsEnabled=fix;
    if(module.wardrive.running)module.wardriveSecond=Math.max(1,Math.floor((this.#now-this.#jobs.get(module.active).started)/1000));
    if(!fix) {module.wardrive.gps.fix=false;module.wardrive.gps.satellites=0;}
    return this.wardrive(moduleId);
  }

  wardriveUpload(moduleId,outcome='success',options={}) {
    if(!['success','failure','partial'].includes(outcome))throw new Error('Invalid upload outcome');
    const {provider=null,mode='pending',sessionIds=[]}=options;
    if(provider!==null&&!['wigle','wdgwars'].includes(provider))throw new Error('Invalid upload provider');
    if(!['selected','pending','all'].includes(mode))throw new Error('Invalid upload mode');
    if(!Array.isArray(sessionIds)||sessionIds.some(id=>!Number.isSafeInteger(id)))throw new Error('Invalid session selection');
    const module=this.#available(moduleId),w=module.wardrive;
    if(!module.sdPresent)throw new Error('SD missing');
    if(mode==='selected'&&(!sessionIds.length||sessionIds.some(id=>!w.sessions.some(s=>s.id===id))))
      throw new Error('Select saved sessions from this module');
    const pending=w.sessions.filter(s=>mode==='all'||(mode==='selected'?sessionIds.includes(s.id):
      provider?s.uploadStatus[provider]!=='done':!s.uploaded));
    const uploaded=outcome==='success'?pending.length:outcome==='partial'?Math.floor(pending.length/2):0;
    pending.forEach((s,index)=>{
      for(const service of provider?[provider]:['wigle','wdgwars'])s.uploadStatus[service]=index<uploaded?'done':'failed';
      s.uploaded=s.uploadStatus.wigle==='done'&&s.uploadStatus.wdgwars==='done';
    });
    w.upload={simulated:true,outcome,total:pending.length,uploaded,failed:pending.length-uploaded};
    return structuredClone(w.upload);
  }

  // Bluetooth discovery. The scenario owns one shared device set; a scan on any
  // connected module surfaces it. A disconnected module sees no devices.
  bluetooth(moduleId) {
    return this.#module(moduleId).connected ? structuredClone(this.#seed.bluetooth||[]) : [];
  }

  nearbyNetworks(moduleId) {
    return this.#module(moduleId).connected ? structuredClone(this.#seed.networks) : [];
  }

  // Locator signal for one tracked device, drifting deterministically with time
  // so the tracking view shows changing strength. Unknown MAC reads as no signal.
  bluetoothRssi(moduleId,mac,tMs) {
    if(!this.#module(moduleId).connected)return -100;
    const dev=(this.#seed.bluetooth||[]).find(d=>d.mac===mac);
    if(!dev)return -100;
    const drift=Math.round(5*Math.sin((tMs??this.#now)/600));
    return Math.max(-95,Math.min(-30,dev.rssi+drift));
  }

  // One synthetic deauth-flood observation for the Deauth Detector. It cycles
  // the discovered networks and drifts RSSI so the table changes over time. A
  // disconnected module or an empty scenario yields null (nothing detected).
  deauthEvent(moduleId,seq) {
    if(!this.#module(moduleId).connected)return null;
    const nets=this.#seed.networks;
    if(!nets.length)return null;
    const n=nets[((seq%nets.length)+nets.length)%nets.length];
    const drift=Math.round(6*Math.sin((seq+1)/1.7));
    return {channel:n.channel,ssid:n.ssid,bssid:n.bssid,
            rssi:Math.max(-90,Math.min(-30,n.rssi+drift))};
  }

  // One flagged "follower" for the Anti-surveillance page: cycles the scenario's
  // Bluetooth devices as tailing radios. Disconnected/empty yields null.
  antisurvFollower(moduleId,seq) {
    if(!this.#module(moduleId).connected)return null;
    const list=this.#seed.bluetooth||[];
    if(!list.length)return null;
    const d=list[((seq%list.length)+list.length)%list.length];
    return {mac:d.mac,name:d.name||''};
  }

  // The network the Handshaker captures (first discovered/seed network).
  handshakerTarget(moduleId) {
    void moduleId;
    const nets=this.#seed.networks;
    return nets.length?nets[0].ssid:'target';
  }
}
