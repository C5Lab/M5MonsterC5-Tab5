/** Offline activity fixtures, never packet transmitters or network clients. */
export function attackOptions(kind,input,networks,clients) {
 const global=['blackout','snifferdog','global_handshaker'].includes(kind);
 if(!global&&!['deauth','handshake','arp','karma','beacon','rogue_ap','evil_twin','mitm','sae_overflow','portal_demo','ap_radar'].includes(kind))throw new Error('Unknown simulated operation');
 if(!input||typeof input!=='object'||Array.isArray(input))throw new Error('Invalid operation options');
 const requested=global?networks.map(n=>n.id):input.networkIds??input.bssids??[];
 if(!Array.isArray(requested)||requested.length>256)throw new Error('Invalid targets');
 const ids=[...new Set(requested.map(id=>{
   const n=networks.find(n=>n.id===id||n.bssid===id);
   if(!n)throw new Error('Choose a scenario network');return n.id;
 }))];
 if(!global&&!['karma','beacon'].includes(kind)&&!ids.length)throw new Error('Select a network');
 if(['arp','rogue_ap','mitm','sae_overflow','portal_demo','ap_radar'].includes(kind)&&ids.length!==1)throw new Error('Select exactly one network');
 const options={networkIds:ids};
 if(input.targetMac) {
   const demoArpHost=kind==='arp'&&['02:00:00:00:00:10','02:00:00:00:00:20'].includes(input.targetMac);
   if(typeof input.targetMac!=='string'||(!demoArpHost&&!clients.some(c=>c.mac===input.targetMac&&ids.includes(c.network_id))))throw new Error('Choose a scenario client');
   options.targetMac=input.targetMac;
 }
 if(input.customSSIDs!==undefined) {
   if(!Array.isArray(input.customSSIDs)||(kind==='beacon'&&!input.customSSIDs.length)||input.customSSIDs.length>32||input.customSSIDs.some(s=>typeof s!=='string'||!s.length||new TextEncoder().encode(s).length>32))throw new Error('Invalid demo SSIDs');
   options.customSSIDs=[...new Set(input.customSSIDs)];
 }
 if(input.ssid!==undefined) {
   if(typeof input.ssid!=='string'||new TextEncoder().encode(input.ssid).length>32)throw new Error('Invalid demo SSID');
   options.ssid=input.ssid;
 }
 if(input.portal)options.portal=String(input.portal);
 return options;
}

export function attackResult(kind,options,elapsed,networks,clients) {
 const step=Math.floor(elapsed/1000),ids=options.networkIds.length?options.networkIds:networks.map(n=>n.id);
 const candidates=clients.filter(c=>ids.includes(c.network_id)&&(!options.targetMac||c.mac===options.targetMac));
 const visible=step?candidates.slice(0,Math.min(step,candidates.length)):[];
 const packets=Math.min(2147483647,step*24);
 const events=Array.from({length:Math.min(step,8)},(_,i)=>{
   const second=step-Math.min(step,8)+i+1;
   return `Simulation ${kind}: ${second}s, ${Math.min(2147483647,second*24)} demo packets`;
 });
 const handshakeResults=['global_handshaker','handshake'].includes(kind)?{
   handshakes:networks.filter(n=>ids.includes(n.id)&&/WPA2/.test(n.security)&&clients.some(c=>c.network_id===n.id))
     .slice(0,Math.floor(elapsed/2000)).map(n=>({networkId:n.id,ssid:n.ssid,bssid:n.bssid}))
 }:{};
 const radarNetwork=networks.find(n=>n.id===ids[0]);
 const radar=kind==='ap_radar'?{signal:{bssid:radarNetwork.bssid,ssid:radarNetwork.ssid,
   rssi:Math.max(-100,Math.min(-20,radarNetwork.rssi+Math.round(8*Math.sin(elapsed/3000))))}}:{};
 const portalDemo=['evil_twin','rogue_ap','portal_demo'].includes(kind)?{
   demo:portalStory(elapsed,networks.find(n=>n.id===ids[0]),candidates.find(c=>c.network_id===ids[0]))
 }:{};
 return {simulated:true,elapsedMs:Math.floor(elapsed),packets,...handshakeResults,...portalDemo,...radar,
   clients:visible.map((c,i)=>({...c,ip:`192.0.2.${10+i}`,vendor:c.vendor||'Scenario device'})),events,
   ssids:options.customSSIDs??networks.filter(n=>ids.includes(n.id)).map(n=>n.ssid).filter(Boolean)};
}

function portalStory(elapsed,network,client) {
 // 2/5/8 wall seconds at the default 10x demo speed. Fixed fixture only.
 // The demonstration must also work for networks without traffic in the seed.
 // This actor belongs only to the story; it never becomes a discovered client.
 if(!client&&network)client={id:'demo-client:'+network.id,mac:'02:DE:00:00:00:01'};
 const step=client?(elapsed>=80000?3:elapsed>=50000?2:elapsed>=20000?1:0):0;
 const stage=['waiting','connected','portal_opened','submitted'][step];
 const password=step===3?'DEMO-only-2026!':null;
 const lines=['OFFLINE DEMO - synthetic client and password',
   'SSID: '+(network?.ssid||'(Hidden)'),
   step>=1?'1. Client connected: '+client.mac:'1. Waiting for a demo client...',
   step>=2?'2. Client opened the portal':'2. Portal visit pending',
   step>=3?'3. Demo password submitted':'3. Password submission pending',
   ...(password?['Password: '+password]:[])];
 return {stage,ssid:network?.ssid||'',client:step?{id:client.id,mac:client.mac}:null,password,lines};
}

export function observerResult(ids,elapsed,networks,clients) {
 const step=Math.floor(elapsed/1000),selected=networks.filter(n=>ids.includes(n.id));
 return {running:true,elapsedMs:Math.floor(elapsed),packets:Math.min(2147483647,step*37*selected.length),error:null,
   networks:selected.map((n,i)=>({...n,rssi:n.rssi+(step?(step+i)%7-3:0),
     clients:clients.filter(c=>c.network_id===n.id).map((c,j)=>({...c,active:(step+j)%4!==3,packets:step*(j+1)*3}))}))};
}
