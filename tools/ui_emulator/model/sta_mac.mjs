/* A deterministic, offline JanOS STA identity model. The MACs here are demo data. */

const envelope=(record_type,fields)=>JSON.stringify({schema_version:1,record_type,...fields});
const result=(kind,fields={})=>envelope('sta_mac_result',{result:kind,...fields});
const macText=bytes=>bytes.map(byte=>byte.toString(16).padStart(2,'0').toUpperCase()).join(':');
const plain=value=>value!==null&&typeof value==='object'&&!Array.isArray(value)&&
  Object.getPrototypeOf(value)===Object.prototype;
const fields=(value,names)=>plain(value)&&
  Object.keys(value).sort().join('|')===names.slice().sort().join('|');
const policy=mode=>['factory','random','fixed'].includes(mode);
const mac=value=>typeof value==='string'&&/^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(value);
const localUnicast=value=>mac(value)&&(Number.parseInt(value.slice(0,2),16)&3)===2;

function randomMac(seed,boot) {
  // Repeatable demo randomness keeps browser stories reproducible. Each boot
  // receives a distinct locally administered unicast address.
  let value=(seed ^ Math.imul(boot,0x9e3779b1))>>>0;
  const bytes=[];
  for(let index=0;index<6;index++) {
    value^=value<<13;value^=value>>>17;value^=value<<5;
    bytes.push(value&255);
  }
  bytes[0]=(bytes[0]&0xfc)|2;
  return macText(bytes);
}

export function createStaMacState(moduleId,index,scenarioSeed) {
  let seed=(scenarioSeed ^ Math.imul(index+1,0x85ebca6b))>>>0;
  for(const char of moduleId)seed=(Math.imul(seed,33)^char.charCodeAt(0))>>>0;
  return {
    seed,boot:0,factory:macText([0x24,0x6f,0x28,0x10,(index+1)>>8,(index+1)&255]),
    configuredMode:'factory',configuredMac:null,bootMode:'factory',bootMac:null,
    effectiveMac:null,recordHealth:'missing'
  };
}

export function bootStaMac(state) {
  state.boot++;
  state.bootMode=state.configuredMode;
  state.bootMac=state.configuredMac;
  state.effectiveMac=state.bootMode==='fixed'?state.bootMac:
    state.bootMode==='random'?randomMac(state.seed,state.boot):state.factory;
}

const snapshotFields=['boot','configuredMode','configuredMac','bootMode','bootMac','effectiveMac','recordHealth'];

export function staMacSnapshot(state) {
  return Object.fromEntries(snapshotFields.map(key=>[key,state[key]]));
}

export function restoreStaMacSnapshot(state,raw) {
  if(!fields(raw,snapshotFields)||!Number.isSafeInteger(raw.boot)||raw.boot<1||raw.boot>1000000000||
     !policy(raw.configuredMode)||!policy(raw.bootMode)||
     !['missing','valid'].includes(raw.recordHealth))return null;
  if(raw.configuredMode==='fixed'?!localUnicast(raw.configuredMac):raw.configuredMac!==null)return null;
  if(raw.bootMode==='fixed'?!localUnicast(raw.bootMac):raw.bootMac!==null)return null;
  if(raw.recordHealth==='missing'&&
     (raw.configuredMode!=='factory'||raw.bootMode!=='factory'))return null;
  const effective=raw.bootMode==='fixed'?raw.bootMac:
    raw.bootMode==='random'?randomMac(state.seed,raw.boot):state.factory;
  if(raw.effectiveMac!==effective)return null;
  return {...state,...staMacSnapshot(raw)};
}

export const STA_MAC_KEY='tab5-emulator:sta-mac';

/* Storage is injected by the browser shell; the virtual device itself is pure. */
export class StaMacStore {
  #disk;#session;#snapshot=null;
  mode='persistent';
  recovered=false;

  constructor(disk,session) {
    this.#disk=disk;this.#session=session;
    let raw=null;
    try {raw=session?.getItem(STA_MAC_KEY)??null;} catch { /* Use disk if readable. */ }
    if(raw===null) {
      try {raw=disk?.getItem(STA_MAC_KEY)??null;} catch {this.mode='session';}
    }
    if(raw===null)return;
    try {
      const record=JSON.parse(raw);
      if(!fields(record,['version','modules'])||record.version!==1||!plain(record.modules))
        throw new Error('Invalid STA MAC record');
      this.#snapshot=record.modules;
    } catch {this.recovered=true;}
  }

  load() {return this.#snapshot&&Object.keys(this.#snapshot).length?
    structuredClone(this.#snapshot):null;}

  save(snapshot) {
    if(!plain(snapshot))throw new Error('Invalid STA MAC snapshot');
    this.#snapshot=structuredClone(snapshot);
    const raw=JSON.stringify({version:1,modules:this.#snapshot});
    try {
      this.#disk.setItem(STA_MAC_KEY,raw);
      this.mode='persistent';
      try {this.#session?.removeItem(STA_MAC_KEY);} catch { /* Session unavailable. */ }
    } catch {
      this.mode='session';
      try {this.#session.setItem(STA_MAC_KEY,raw);} catch {this.mode='memory';}
    }
  }

  reset() {this.save({});}
}

export function attachStaMacPersistence(device,store) {
  const saved=store.load();
  if(saved&&!device.restoreStaMacSnapshot(saved))store.recovered=true;
  device.setStaMacChangeHandler(()=>store.save(device.staMacSnapshot()));
  store.save(device.staMacSnapshot());
}

export function staMacCommand(state,command,{connected,active}) {
  if(!connected)return result('store_error',{error:'disconnected'});
  if(command==='sta_mac status') {
    return envelope('sta_mac_status',{
      configured_mode:state.configuredMode,configured_mac:state.configuredMac,
      boot_mode:state.bootMode,effective_boot_mac:state.effectiveMac,
      active_sta_mac:state.effectiveMac,record_health:state.recordHealth,
      identity_ready:true,
      pending_reboot:state.configuredMode!==state.bootMode||
        (state.configuredMode==='fixed'&&state.configuredMac!==state.bootMac)
    });
  }
  const tokens=typeof command==='string'?command.split(' '):[];
  if(tokens[0]!=='sta_mac'||tokens[1]!=='set')return result('usage');
  if(active!==null)return result('store_error',{error:'radio_lease'});
  let mode,mac=null;
  if(tokens.length===3&&['factory','random'].includes(tokens[2]))mode=tokens[2];
  else if(tokens.length===4&&tokens[2]==='fixed') {
    if(!/^(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$/.test(tokens[3])||
       (Number.parseInt(tokens[3].slice(0,2),16)&3)!==2)return result('invalid_mac');
    mode='fixed';mac=tokens[3].toUpperCase();
  } else return result('usage');
  state.configuredMode=mode;
  state.configuredMac=mac;
  state.recordHealth='valid';
  return result('set',{configured_mode:mode,configured_mac:mac,applies_after_reboot:true});
}
