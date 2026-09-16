/** Offline demonstrations only. This module performs no socket or service I/O. */
export function toolOptions(kind,input,networks) {
  if(!input||typeof input!=='object'||Array.isArray(input))throw new Error('Invalid tool options');
  const options={};
  if(!['nmap','iot','gitm_scan','gitm_connect','gitm','wpasec'].includes(kind))throw new Error('Unknown simulated tool');
  if(kind==='nmap') {
    options.action=input.action??'hosts';
    if(!['connect','hosts','scan'].includes(options.action))throw new Error('Invalid nmap action');
    if(options.action==='scan') {
      options.target=input.target??'192.0.2.10';options.level=input.level??'fast';
      if(!['192.0.2.10','192.0.2.20','all'].includes(options.target))throw new Error('Choose a simulated host');
      if(!['quick','medium','heavy','fast','normal','full'].includes(options.level))throw new Error('Invalid scan level');
    }
  }
  if(kind==='gitm_connect'||(kind==='nmap'&&options.action==='connect')) {
    if(typeof input.ssid!=='string'||!networks.some(n=>n.ssid===input.ssid))throw new Error('Choose a scenario network');
    options.ssid=input.ssid;
  }
  if(kind==='gitm') {
    options.upstreamSsid=input.upstreamSsid??networks[0]?.ssid;
    if(!networks.some(n=>n.ssid===options.upstreamSsid))throw new Error('Choose a scenario upstream');
    options.prefix=input.prefix||'gitm-demo';
    if(typeof options.prefix!=='string'||! /^[A-Za-z0-9_-][A-Za-z0-9_.-]{0,62}$/.test(options.prefix))throw new Error('Invalid capture prefix');
  }
  if(kind==='wpasec') {
    options.outcome=input.outcome??'success';
    if(!['success','partial','failure'].includes(options.outcome))throw new Error('Invalid simulated upload outcome');
  }
  // Deliberately whitelist options. Password/API-key input never enters a job.
  return options;
}

export function toolResult(kind,options,networks,elapsed=30000) {
  if(kind==='gitm_scan')return {networks:networks.map(n=>({...n,auth:n.security}))};
  if(kind==='gitm_connect')return {connected:true,ssid:options.ssid,ip:'192.0.2.2'};
  if(kind==='nmap') {
    if(options.action==='connect')return {text:'SUCCESS\nOur IP: 192.0.2.2,\n',connected:true,ssid:options.ssid};
    if(options.action==='hosts')return {text:'Our IP: 192.0.2.2,\nDiscovered Hosts\n  192.0.2.10 -> 02:00:00:00:00:10\n  192.0.2.20 -> 02:00:00:00:00:20\n'};
    const hosts=options.target==='all'?['192.0.2.10','192.0.2.20']:[options.target];
    const rows=hosts.map(ip=>`Host: ${ip} (02:00:00:00:00:${ip.endsWith('.10')?'10':'20'})\n80/tcp open HTTP\n`);
    const ports={quick:20,medium:50,heavy:100,fast:100,normal:1000,full:65535}[options.level];
    return {text:`Scan level: ${options.level} (${ports} ports)\nScanning ${hosts.length} host(s),\n${rows.join('')}Scanned ${hosts.length} hosts, found ${hosts.length} open ports\n`};
  }
  if(kind==='iot') {
    const sample=Math.floor(elapsed/10000), second=elapsed>=50000;
    const packets=Math.min(100000000,sample*14);
    const rows=[
    `[ZIG] status active=1 channel=${second&&sample%2?20:15} packets=${packets*(second?2:1)} pans=${second?2:1} dropped=0`,
    '[ZIG] pan id=0x1A2B proto=zigbee confidence=high nodes=2 packets=42 best_rssi=-48 age_ms=100 channels=32768',
    '[ZIG] node pan=0x1A2B role=coordinator vendor=Synthetic device_hint=Gateway battery=na packets=22 last_rssi=-48 best_rssi=-45 avg_rssi=-49 lqi=220 sample_count=5 last_channel=15 age_ms=100 addr_type=short short=0x0000',
    '[ZIG] node pan=0x1A2B role=end_device vendor=Synthetic device_hint=Sensor battery=yes packets=20 last_rssi=-62 best_rssi=-58 avg_rssi=-61 lqi=185 sample_count=4 last_channel=15 age_ms=100 addr_type=short short=0x0042',
    '[ZIG] edge pan=0x1A2B src=0x0000 dst=0x0042 packets=20 age_ms=100',
    ];
    if(second)rows.push(
      '[ZIG] pan id=0x3C4D proto=zigbee confidence=high nodes=2 packets=42 best_rssi=-65 age_ms=100 channels=1048576',
      '[ZIG] node pan=0x3C4D role=coordinator vendor=Synthetic device_hint=Bridge battery=na packets=22 last_rssi=-65 best_rssi=-62 avg_rssi=-64 lqi=190 sample_count=5 last_channel=20 age_ms=100 addr_type=short short=0x0000',
      '[ZIG] node pan=0x3C4D role=end_device vendor=Synthetic device_hint=Lamp battery=no packets=20 last_rssi=-72 best_rssi=-68 avg_rssi=-71 lqi=165 sample_count=4 last_channel=20 age_ms=100 addr_type=short short=0x0077',
      '[ZIG] edge pan=0x3C4D src=0x0000 dst=0x0077 packets=20 age_ms=100');
    return {sample,text:rows.map(row=>row.replace(/packets=42\b/g,`packets=${packets}`)
      .replace(/packets=22\b/g,`packets=${Math.ceil(packets/2)}`)
      .replace(/packets=20\b/g,`packets=${Math.floor(packets/2)}`)).join('\n')+'\n'};
  }
  if(kind==='wpasec') {
    const uploaded=options.outcome==='success'?2:options.outcome==='partial'?1:0;
    return {simulated:true,uploaded,skipped:0,failed:2-uploaded,files:[0,1].map(i=>({name:`offline-demo-${i+1}.pcap`,status:i<uploaded?'uploaded':'failed'}))};
  }
  return null;
}
