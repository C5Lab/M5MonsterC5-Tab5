const MAX_U64=(1n<<64n)-1n;
export const FORENSIC_CAPACITY=128;
const MAC='(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}';
const PATTERN=new RegExp(
  `^(?:> )?\\[DEAUTH_FORENSIC/1\\] FRAME=(deauth|disassoc) CH=(-|\\d+) `+
  `BSSID=(${MAC}) TX=(${MAC}) DA=(${MAC}) RSSI=(-|-?\\d+) `+
  `REASON=(-|\\d+) SEQ=(-|\\d+) RETRY=(-|[01]) PROTECTED=(-|[01]) `+
  `FC=(-|0x[0-9A-Fa-f]{4}) TS_US=(\\d+)$`);

const number=(text,{minimum=0,maximum})=>{
  if(text==='-')return null;
  if(!/^\d+$/.test(text))throw new Error('invalid numeric field');
  const value=Number(text);
  if(!Number.isSafeInteger(value)||value<minimum||value>maximum)
    throw new Error('numeric field out of range');
  return value;
};

export function parseForensicLine(line){
  if(typeof line!=='string'||line.length===0||line.length>=512)return null;
  const match=PATTERN.exec(line);
  if(!match)return null;
  try{
    const channel=number(match[2],{minimum:1,maximum:233});
    const rssi=match[6]==='-'?null:Number(match[6]);
    if(rssi!==null&&(!Number.isInteger(rssi)||rssi< -127||rssi>0||/^-0+$/.test(match[6])))
      return null;
    const reason=number(match[7],{maximum:65535});
    const sequence=number(match[8],{maximum:4095});
    const retry=match[9]==='-'?null:Number(match[9]);
    const protectedValue=match[10]==='-'?null:Number(match[10]);
    const frameControl=match[11]==='-'?null:Number.parseInt(match[11].slice(2),16);
    if(!/^\d+$/.test(match[12]))return null;
    const timestampUs=BigInt(match[12]);
    if(timestampUs>MAX_U64)return null;
    return {frame:match[1],channel,bssid:match[3].toUpperCase(),
      tx:match[4].toUpperCase(),da:match[5].toUpperCase(),rssi,reason,
      sequence,retry,protected:protectedValue,frameControl,timestampUs,
      gapUs:null,sequenceState:'Insufficient sequence data',sequenceDelta:null};
  }catch{return null;}
}

const sameStream=(a,b)=>a.frame===b.frame&&a.bssid===b.bssid&&
  a.tx===b.tx&&a.da===b.da;
const sameOwner=(a,b)=>a&&b&&a.tab===b.tab&&a.port===b.port&&
  a.transport===b.transport&&a.linkGeneration===b.linkGeneration&&
  a.commandGeneration===b.commandGeneration&&a.scanGeneration===b.scanGeneration;
const increment=value=>value<MAX_U64?value+1n:MAX_U64;

export class ForensicModel{
  constructor(capacity=FORENSIC_CAPACITY){
    if(!Number.isInteger(capacity)||capacity<1||capacity>FORENSIC_CAPACITY)
      throw new Error('invalid forensic capacity');
    this.capacity=capacity;this.reset();
  }
  reset(){
    this.rows=[];this.accepted=0n;this.localDropped=0n;this.burstCount=0n;
    this.largestBurst=0n;this.currentBurst=0n;this.active=false;
    this.finalized=false;this.markerSeen=false;this.owner=null;this.last=null;
  }
  begin(owner){this.reset();this.active=true;this.owner=structuredClone(owner);}
  finalize(){if(this.active){this.active=false;this.finalized=true;}}
  recomputeSequences(){
    for(let index=0;index<this.rows.length;index++){
      const row=this.rows[index];row.sequenceState='Insufficient sequence data';
      row.sequenceDelta=null;
      if(row.sequence===null)continue;
      for(let previousIndex=index-1;previousIndex>=0;previousIndex--){
        const previous=this.rows[previousIndex];
        if(previous.sequence===null||!sameStream(previous,row))continue;
        row.sequenceDelta=(row.sequence-previous.sequence+4096)%4096;
        row.sequenceState=row.sequenceDelta===0&&row.retry===1?'Retransmission':
          row.sequenceDelta===0&&row.retry===0?'Repeated sequence':'Delta';
        break;
      }
    }
  }
  acceptLine(line,owner,{bssidAllowed=true}={}){
    const row=parseForensicLine(line);
    if(!row||!this.active||this.finalized||!bssidAllowed||
       !sameOwner(owner,this.owner))return false;
    if(this.last&&row.timestampUs>=this.last.timestampUs)
      row.gapUs=row.timestampUs-this.last.timestampUs;
    const newBurst=!this.last||!sameStream(this.last,row)||row.gapUs===null||
      row.gapUs>20000000n;
    if(newBurst){this.burstCount=increment(this.burstCount);this.currentBurst=1n;}
    else this.currentBurst=increment(this.currentBurst);
    if(this.currentBurst>this.largestBurst)this.largestBurst=this.currentBurst;
    if(this.rows.length===this.capacity){this.rows.shift();this.localDropped=increment(this.localDropped);}
    this.rows.push(row);this.recomputeSequences();
    this.accepted=increment(this.accepted);this.markerSeen=true;
    this.last=row;return true;
  }
  stats(){
    const rssi=this.rows.map(row=>row.rssi).filter(value=>value!==null);
    const rawAverage=rssi.length?rssi.reduce((a,b)=>a+b,0)/rssi.length:null;
    const average=rawAverage===null?null:
      rawAverage<0?-Math.round(-rawAverage):Math.round(rawAverage);
    const reasonDistribution={};
    for(const row of this.rows){
      if(row.reason===null)continue;
      const key=String(row.reason);
      reasonDistribution[key]=(reasonDistribution[key]??0)+1;
    }
    return {uniqueBssid:new Set(this.rows.map(row=>row.bssid)).size,
      uniqueDa:new Set(this.rows.map(row=>row.da)).size,
      rssiMin:rssi.length?Math.min(...rssi):null,rssiAverage:average,
      rssiMax:rssi.length?Math.max(...rssi):null,
      retry:this.rows.filter(row=>row.retry===1).length,
      protected:this.rows.filter(row=>row.protected===1).length,
      unprotected:this.rows.filter(row=>row.protected===0).length,
      protectedUnknown:this.rows.filter(row=>row.protected===null).length,
      retryUnknown:this.rows.filter(row=>row.retry===null).length,
      reasonUnknown:this.rows.filter(row=>row.reason===null).length,
      reasonDistribution};
  }
}

export function longRunForensicFixture(){
  const sizes=[2,2,3,2,3,1,1,3,3,2,1];let timestamp=0n,sequence=0;
  const lines=[];
  sizes.forEach((size,burst)=>{
    if(burst)timestamp+=20000001n;
    for(let index=0;index<size;index++){
      const tx=`AA:BB:CC:DD:EE:${burst.toString(16).padStart(2,'0').toUpperCase()}`;
      lines.push(`[DEAUTH_FORENSIC/1] FRAME=deauth CH=6 BSSID=00:11:22:33:44:55 `+
        `TX=${tx} DA=12:34:56:78:9A:BC RSSI=${-80-(sequence%8)} REASON=6 `+
        `SEQ=${sequence%4096} RETRY=${sequence%5===0?1:0} PROTECTED=1 FC=0x00C0 TS_US=${timestamp}`);
      timestamp++;sequence++;
    }
  });
  return lines;
}
