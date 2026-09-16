/** Offline documentation-address examples. Never transmits packets. */
const client=[192,0,2,10],server=[198,51,100,20],resolver=[203,0,113,53];
const encode=text=>new TextEncoder().encode(text);
function checksum(bytes) {
  let value=0;
  for(let i=0;i<bytes.length;i+=2)value+=(bytes[i]<<8)+(bytes[i+1]||0);
  while(value>>>16)value=(value&65535)+(value>>>16);
  return (~value)&65535;
}
function frame(src,dst,protocol,segment,id) {
  const bytes=new Uint8Array(34+segment.length),v=new DataView(bytes.buffer);
  bytes.set([2,0,0,0,0,2,2,0,0,0,0,1]);v.setUint16(12,0x0800);
  bytes[14]=0x45;v.setUint16(16,20+segment.length);v.setUint16(18,id);
  v.setUint16(20,0x4000);bytes[22]=64;bytes[23]=protocol;
  bytes.set(src,26);bytes.set(dst,30);v.setUint16(24,checksum(bytes.slice(14,34)));
  if(protocol!==1) {
    const pseudo=new Uint8Array(12+segment.length),pv=new DataView(pseudo.buffer);
    pseudo.set(src);pseudo.set(dst,4);pseudo[9]=protocol;pv.setUint16(10,segment.length);pseudo.set(segment,12);
    new DataView(segment.buffer).setUint16(protocol===6?16:6,checksum(pseudo)||65535);
  }
  bytes.set(segment,34);return bytes;
}
function tcp(src,dst,sport,dport,seq,ack,flags,payload,id) {
  const bytes=new Uint8Array(20+payload.length),v=new DataView(bytes.buffer);
  v.setUint16(0,sport);v.setUint16(2,dport);v.setUint32(4,seq);v.setUint32(8,ack);
  bytes[12]=0x50;bytes[13]=flags;v.setUint16(14,16384);bytes.set(payload,20);
  return frame(src,dst,6,bytes,id);
}
function dns(response) {
  const name=new Uint8Array([7,...encode('example'),3,...encode('com'),0,0,1,0,1]);
  const bytes=new Uint8Array(12+name.length+(response?16:0)),v=new DataView(bytes.buffer);
  v.setUint16(0,0x1234);v.setUint16(2,response?0x8180:0x0100);v.setUint16(4,1);v.setUint16(6,response?1:0);bytes.set(name,12);
  if(response) {
    const o=12+name.length;v.setUint16(o,0xc00c);v.setUint16(o+2,1);v.setUint16(o+4,1);
    v.setUint32(o+6,60);v.setUint16(o+10,4);bytes.set(server,o+12);
  }
  const udp=new Uint8Array(8+bytes.length),uv=new DataView(udp.buffer);
  uv.setUint16(0,response?53:53000);uv.setUint16(2,response?53000:53);uv.setUint16(4,udp.length);udp.set(bytes,8);
  return frame(response?resolver:client,response?client:resolver,17,udp,response?2:1);
}
function ping(reply) {
  const bytes=new Uint8Array(8+12),v=new DataView(bytes.buffer);
  bytes[0]=reply?0:8;v.setUint16(4,123);v.setUint16(6,1);bytes.set(encode('offline-demo'),8);
  v.setUint16(2,checksum(bytes));return frame(reply?server:client,reply?client:server,1,bytes,reply?14:13);
}
function pcap(frames) {
  const bytes=new Uint8Array(24+frames.reduce((n,f)=>n+16+f.length,0)),v=new DataView(bytes.buffer);
  v.setUint32(0,0xa1b2c3d4,true);v.setUint16(4,2,true);v.setUint16(6,4,true);
  v.setUint32(16,65535,true);v.setUint32(20,1,true);
  let offset=24;
  frames.forEach((f,i)=>{
    v.setUint32(offset,1700000100+Math.floor(i/10),true);v.setUint32(offset+4,(i%10)*100000,true);
    v.setUint32(offset+8,f.length,true);v.setUint32(offset+12,f.length,true);bytes.set(f,offset+16);offset+=16+f.length;
  });return bytes;
}

export function pcapFixtures() {
  const request=encode('GET /demo HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n');
  const body='Offline synthetic example.\n';
  const response=encode(`HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ${encode(body).length}\r\nConnection: close\r\n\r\n${body}`);
  const empty=new Uint8Array(),c=1001+request.length,s=5001+response.length;
  const stream=[
    [false,1000,0,2,empty],[true,5000,1001,18,empty],[false,1001,5001,16,empty],
    [false,1001,5001,24,request],[true,5001,c,16,empty],[true,5001,c,24,response],
    [false,c,s,16,empty],[true,s,c,17,empty],[false,c,s+1,17,empty],[true,s+1,c+1,16,empty]
  ].map(([reverse,seq,ack,flags,payload],i)=>tcp(reverse?server:client,reverse?client:server,reverse?80:49152,reverse?49152:80,seq,ack,flags,payload,i+3));
  const common={synthetic:true,valid:true,endpoints:['192.0.2.10','198.51.100.20','203.0.113.53'],domain:'example.com'};
  return [
    {...common,fixtureId:'http-dns-icmp',packetCount:14,protocols:['TCP','HTTP','UDP','DNS','ICMP'],ipProtocolCounts:{6:10,17:2,1:2},bytes:pcap([dns(false),dns(true),...stream,ping(false),ping(true)])},
    {...common,fixtureId:'dns-icmp',packetCount:4,protocols:['UDP','DNS','ICMP'],ipProtocolCounts:{17:2,1:2},bytes:pcap([dns(false),dns(true),ping(false),ping(true)])},
    {synthetic:true,valid:false,fixtureId:'invalid-truncated',packetCount:0,protocols:[],endpoints:[],expectedError:'truncated PCAP global header',bytes:pcap([]).slice(0,12)}
  ];
}
