/** Synthetic Ethernet/ARP fixture, not an over-the-air handshake capture. */
function mac(value) {
  if (!/^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(value)) throw new Error('Invalid synthetic MAC');
  return value.split(':').map(v=>parseInt(v,16));
}

function ip(value) {
  if (!/^\d{1,3}(?:\.\d{1,3}){3}$/.test(value)) throw new Error('Invalid synthetic IPv4');
  const octets=value.split('.').map(Number);
  if (octets.some(v=>v>255)) throw new Error('Invalid synthetic IPv4');
  return octets;
}

export function syntheticPcap(network, clients, seconds=1700000000) {
  const ap=mac(network.bssid), gateway=ip(network.gateway || '192.0.2.1');
  const bytes=new Uint8Array(24+clients.length*58);
  const view=new DataView(bytes.buffer);
  view.setUint32(0,0xa1b2c3d4,true);
  view.setUint16(4,2,true);view.setUint16(6,4,true);
  view.setUint32(16,65535,true);view.setUint32(20,1,true);
  clients.forEach((client,index)=>{
    const peer=mac(client.mac), address=ip(client.ip), offset=24+index*58;
    view.setUint32(offset,seconds+Math.floor(index/1000),true);
    view.setUint32(offset+4,(index%1000)*1000,true);
    view.setUint32(offset+8,42,true);view.setUint32(offset+12,42,true);
    const frame=offset+16;
    bytes.set(peer,frame);bytes.set(ap,frame+6);view.setUint16(frame+12,0x0806);
    const arp=frame+14;
    view.setUint16(arp,1);view.setUint16(arp+2,0x0800);
    bytes[arp+4]=6;bytes[arp+5]=4;view.setUint16(arp+6,2);
    bytes.set(ap,arp+8);bytes.set(gateway,arp+14);
    bytes.set(peer,arp+18);bytes.set(address,arp+24);
  });
  return bytes;
}
