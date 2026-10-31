import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
import { BrowserDevice } from '../tools/ui_emulator/model/bridge.mjs';

const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));

test('deauth events cycle the scenario networks and drift RSSI within bounds',()=>{
  const d=new SimulatedDevice(seed);
  const first=d.deauthEvent('grove',0);
  assert.equal(first.ssid,seed.networks[0].ssid);
  assert.equal(first.bssid,seed.networks[0].bssid);
  assert.equal(first.channel,seed.networks[0].channel);
  // Cycles through every network by sequence.
  const seen=new Set();
  for(let i=0;i<seed.networks.length;i++)seen.add(d.deauthEvent('grove',i).bssid);
  assert.equal(seen.size,seed.networks.length);
  // RSSI stays in the clamped range.
  for(let i=0;i<40;i++){const r=d.deauthEvent('grove',i).rssi;assert.ok(r<=-30&&r>=-90);}
});

test('a disconnected module detects nothing',()=>{
  const d=new SimulatedDevice(seed);
  d.setModule('grove',{connected:false});
  assert.equal(d.deauthEvent('grove',0),null);
});

test('the bridge formats a firmware-shaped [DEAUTH] line and empty text when idle',()=>{
  const b=new BrowserDevice(seed);
  assert.equal(b.deauthDetectorLine(0,0),'');
  b.detectorWrite(0,'deauth_detector');
  const line=b.deauthDetectorLine(0,0);
  assert.match(line,/^\[DEAUTH\] CH: \d+ \| AP: .+ \(.+\) \| RSSI: -?\d+$/);
  b.device.setModule('grove',{connected:false});
  assert.equal(b.deauthDetectorLine(0,0),'');
});

test('V2 command confirms format and emits one complete DEAUTH observation',()=>{
  const b=new BrowserDevice(seed);
  assert.equal(b.detectorWrite(0,'deauth_detector_v2\r\n'),true);
  const replies=drainDetector(b.device,'grove');
  assert.ok(replies.some(line=>line.startsWith('Deauth Detector started - scanning all channels')));
  assert.ok(replies.includes('[DETECTOR_INFO] format=deauth_v2'));
  assert.ok(replies.includes('Output: [DEAUTH_V2] CH: <ch> | BSSID: <bssid> | TX: <tx> | DA: <da> | RSSI: <rssi> | REASON: <reason> | SSID: <ssid>'));
  assert.ok(replies.indexOf('[DETECTOR_INFO] format=deauth_v2')>
    replies.findIndex(line=>line.startsWith('Output: [DEAUTH_V2]')));
  assert.equal(b.device.detectorStatus('grove').details,true);
  const line=b.deauthDetectorLine(0,0);
  assert.match(line,/^\[DEAUTH_V2\] CH: 1 \| BSSID: 02:20:77:00:00:01 \| TX: [0-9A-F:]{17} \| DA: [0-9A-F:]{17} \| RSSI: -?\d+ \| REASON: \d+ \| SSID: NEON-BAZAAR$/);
  assert.ok(!line.includes('[DEAUTH]'));
  assert.match(b.deauthDetectorLine(0,2),/^\[CSA\] RX:/);
  b.detectorWrite(0,'stop');
  assert.equal(b.device.detectorStatus('grove').details,false);
  assert.equal(b.deauthDetectorLine(0,0),'');
});

test('V2 trace emits armed and confirming before records and locks only after the deadline',()=>{
  const b=new BrowserDevice(seed);
  assert.equal(b.detectorWrite(0,'deauth_detector_v2 trace'),true);
  const replies=drainDetector(b.device,'grove');
  assert.ok(replies.includes('[DETECTOR_INFO] format=deauth_v2'));
  assert.equal(replies.at(-1),'[DETECTOR_TRACE] armed');
  assert.equal(b.device.detectorStatus('grove').traceRequested,true);
  assert.equal(b.device.detectorStatus('grove').traceState,'armed');
  assert.equal(b.deauthDetectorLine(0,0),
    '[DETECTOR_TRACE] confirming_bssid=02:20:77:00:00:01 first_channel=1 window_ms=1000');
  assert.equal(b.device.detectorStatus('grove').traceState,'confirming');
  assert.match(b.deauthDetectorLine(0,99),/^\[DEAUTH_V2\] CH: 1 /);
  b.advance(999);
  assert.equal(b.device.detectorRead('grove'),null);
  b.advance(1);
  assert.equal(b.device.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=1');
  assert.equal(b.device.detectorStatus('grove').traceState,'locked');
  assert.deepEqual(b.device.detectorStatus('grove').traceChannel,1);
});

test('trace scoring prefers repeated stronger adjacent observations on 2.4 and 5 GHz',()=>{
  const d=new SimulatedDevice(seed);
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  const bssid24='02:20:77:00:00:03';
  assert.match(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid:bssid24,channel:6,rssi:-70}),/confirming_bssid=/);
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:bssid24,channel:7,rssi:-48});
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:bssid24,channel:7,rssi:-46});
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=7');

  d.detectorCommand('mbus','deauth_detector_v2 trace');drainDetector(d,'mbus');
  const bssid5='02:20:77:00:00:06';
  assert.match(d.detectorTraceForEvent('mbus',
    {type:'deauth',bssid:bssid5,channel:44,rssi:-72}),/first_channel=44/);
  d.detectorTraceForEvent('mbus',{type:'deauth',bssid:bssid5,channel:40,rssi:-52});
  d.detectorTraceForEvent('mbus',{type:'deauth',bssid:bssid5,channel:40,rssi:-50});
  d.advance(1000);
  assert.equal(d.detectorRead('mbus'),'[DETECTOR_TRACE] locked_channel=40');
});

test('trace exact ties prefer the BSSID scan-cache channel then the first RX channel',()=>{
  const d=new SimulatedDevice(seed);
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  const cached='02:20:77:00:00:03'; /* Scan cache says channel 6. */
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:cached,channel:5,rssi:-50});
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:cached,channel:6,rssi:-50});
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=6');

  d.detectorCommand('mbus','deauth_detector_v2 trace');drainDetector(d,'mbus');
  const uncached='02:AA:BB:CC:DD:EE';
  d.detectorTraceForEvent('mbus',{type:'deauth',bssid:uncached,channel:5,rssi:-50});
  d.detectorTraceForEvent('mbus',{type:'deauth',bssid:uncached,channel:6,rssi:-50});
  d.advance(1000);
  assert.equal(d.detectorRead('mbus'),'[DETECTOR_TRACE] locked_channel=5');
});

test('trace ignores filtered BSSIDs CSA malformed records and unrelated confirming BSSIDs',()=>{
  const d=new SimulatedDevice(seed);
  d.detectorCommand('grove','scan_networks');d.advance(seed.settings.scan_time_ms);
  drainDetector(d,'grove');
  d.detectorCommand('grove','deauth_detector_v2 --only 1 trace');drainDetector(d,'grove');
  const outside={type:'deauth',bssid:'02:20:77:00:00:02',channel:1,rssi:-30};
  assert.equal(d.detectorTraceForEvent('grove',outside),null);
  assert.equal(d.detectorTraceForEvent('grove',
    {type:'csa',bssid:'02:20:77:00:00:01',channel:1,rssi:-30}),null);
  assert.equal(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid:'short',channel:1,rssi:-30}),null);
  assert.equal(d.detectorStatus('grove').traceState,'armed');
  const watched='02:20:77:00:00:01';
  assert.match(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid:watched,channel:1,rssi:-70}),/confirming_bssid=/);
  d.detectorTraceForEvent('grove',outside);
  d.detectorTraceForEvent('grove',outside);
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=1');
});

test('trace reports a failed tune returns to retry-ready locks later and resets on Stop',()=>{
  const custom=structuredClone(seed);
  custom.modules.find(module=>module.id==='grove').detector_trace_fail_channels=[1];
  const d=new SimulatedDevice(custom);
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  const bssid='02:20:77:00:00:01';
  assert.match(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid,channel:1,rssi:-40}),/confirming_bssid=/);
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),
    '[DETECTOR_TRACE] lock_failed_channel=1 error=ESP_FAIL');
  assert.equal(d.detectorStatus('grove').traceState,'retry_ready');
  assert.match(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid,channel:2,rssi:-42}),/confirming_bssid=/);
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=2');
  d.detectorCommand('grove','stop');
  assert.equal(d.detectorStatus('grove').traceState,'off');
  assert.deepEqual(d.detectorStatus('grove').traceChannel,null);
});

test('locked trace stays locked at 19999 ms then queues idle release before armed',()=>{
  const d=new SimulatedDevice(seed);
  const bssid='02:20:77:00:00:01';
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  d.detectorTraceForEvent('grove',{type:'deauth',bssid,channel:1,rssi:-40});
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=1');
  d.advance(18999);
  assert.equal(d.detectorStatus('grove').traceState,'locked');
  assert.equal(d.detectorRead('grove'),null);
  d.advance(1);
  assert.equal(d.detectorRead('grove'),
    '[DETECTOR_TRACE] released_channel=1 reason=idle idle_ms=20000');
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] armed');
  assert.equal(d.detectorStatus('grove').traceState,'armed');
  assert.equal(d.detectorStatus('grove').traceReleasedChannel,1);
});

test('only the locked BSSID refreshes the 20000 ms trace idle deadline',()=>{
  const d=new SimulatedDevice(seed);
  const locked='02:20:77:00:00:01';
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:locked,channel:1,rssi:-40});
  d.advance(1000);drainDetector(d,'grove');
  d.advance(18999);
  assert.equal(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid:locked,channel:1,rssi:-42}),null);
  d.advance(19999);
  assert.equal(d.detectorStatus('grove').traceState,'locked');
  assert.equal(d.detectorRead('grove'),null);
  d.advance(1);
  assert.equal(d.detectorRead('grove'),
    '[DETECTOR_TRACE] released_channel=1 reason=idle idle_ms=20000');
});

test('other BSSIDs and CSA do not refresh a locked trace idle deadline',()=>{
  const d=new SimulatedDevice(seed);
  const locked='02:20:77:00:00:01';
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:locked,channel:1,rssi:-40});
  d.advance(1000);drainDetector(d,'grove');
  d.advance(18999);
  assert.equal(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid:'02:20:77:00:00:02',channel:1,rssi:-30}),null);
  assert.equal(d.detectorTraceForEvent('grove',
    {type:'csa',bssid:locked,channel:1,to:6,rssi:-30}),null);
  d.advance(1);
  assert.equal(d.detectorRead('grove'),
    '[DETECTOR_TRACE] released_channel=1 reason=idle idle_ms=20000');
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] armed');
});

test('released trace reacquires while Stop and transports clear timers independently',()=>{
  const connected=structuredClone(seed);
  connected.modules.find(module=>module.id==='usb').connected=true;
  const d=new SimulatedDevice(connected);
  const groveBssid='02:20:77:00:00:01';
  const mbusBssid='02:20:77:00:00:03';
  d.detectorCommand('grove','deauth_detector_v2 trace');drainDetector(d,'grove');
  d.detectorCommand('mbus','deauth_detector_v2 trace');drainDetector(d,'mbus');
  d.detectorTraceForEvent('grove',{type:'deauth',bssid:groveBssid,channel:1,rssi:-40});
  d.detectorTraceForEvent('mbus',{type:'deauth',bssid:mbusBssid,channel:6,rssi:-50});
  d.advance(1000);drainDetector(d,'grove');drainDetector(d,'mbus');
  d.detectorCommand('mbus','stop');drainDetector(d,'mbus');
  d.advance(19000);
  assert.deepEqual(drainDetector(d,'grove'),[
    '[DETECTOR_TRACE] released_channel=1 reason=idle idle_ms=20000',
    '[DETECTOR_TRACE] armed'
  ]);
  assert.equal(d.detectorStatus('mbus').traceState,'off');
  assert.equal(d.detectorRead('mbus'),null);
  assert.match(d.detectorTraceForEvent('grove',
    {type:'deauth',bssid:groveBssid,channel:2,rssi:-41}),
    /confirming_bssid=.*first_channel=2 window_ms=1000/);
  d.advance(1000);
  assert.equal(d.detectorRead('grove'),'[DETECTOR_TRACE] locked_channel=2');
});

test('trace and pending records remain isolated across Grove USB and MBus',()=>{
  const connected=structuredClone(seed);
  connected.modules.find(module=>module.id==='usb').connected=true;
  const b=new BrowserDevice(connected);
  b.detectorWrite(0,'deauth_detector_v2 trace');drainDetector(b.device,'grove');
  b.detectorWrite(1,'deauth_detector_v2');drainDetector(b.device,'usb');
  b.detectorWrite(2,'deauth_detector_v2 trace');drainDetector(b.device,'mbus');
  assert.match(b.deauthDetectorLine(0,0),/confirming_bssid=/);
  assert.match(b.deauthDetectorLine(1,0),/^\[DEAUTH_V2\]/);
  assert.match(b.deauthDetectorLine(2,3),/confirming_bssid=/);
  assert.match(b.deauthDetectorLine(0,9),/^\[DEAUTH_V2\]/);
  assert.match(b.deauthDetectorLine(2,9),/^\[DEAUTH_V2\]/);
  b.advance(1000);
  assert.match(b.device.detectorRead('grove'),/^\[DETECTOR_TRACE\] locked_channel=/);
  assert.match(b.device.detectorRead('mbus'),/^\[DETECTOR_TRACE\] locked_channel=/);
  assert.equal(b.device.detectorRead('usb'),null);
  assert.equal(b.device.detectorStatus('usb').traceRequested,false);
  assert.equal(b.device.detectorStatus('grove').traceState,'locked');
  assert.equal(b.device.detectorStatus('mbus').traceState,'locked');
});

test('older V2 reports trace as an unsupported index instead of starting',()=>{
  const oldSeed=structuredClone(seed);
  oldSeed.modules.find(module=>module.id==='grove').detector_trace_supported=false;
  const d=new SimulatedDevice(oldSeed);
  d.detectorCommand('grove','deauth_detector_v2 trace');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.match(drainDetector(d,'grove').join('\n'),
    /Invalid detector index 'trace'.*Detector not started/);
  d.detectorCommand('grove','scan_networks');d.advance(seed.settings.scan_time_ms);
  drainDetector(d,'grove');
  d.detectorCommand('grove','deauth_detector_v2 --only 1 trace');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.match(drainDetector(d,'grove').join('\n'),
    /Invalid watch index 'trace'.*Detector not started/);
});

test('V2 bridge emits an explicit unknown reason for protected unicast fixtures',()=>{
  const b=new BrowserDevice(seed);
  b.detectorWrite(0,'deauth_detector_v2');drainDetector(b.device,'grove');
  assert.match(b.deauthDetectorLine(0,1),/\| REASON: - \| SSID:/);
});

test('selected V2 watch remains BSSID-scoped and module-local',()=>{
  const b=new BrowserDevice(seed);
  b.detectorWrite(0,'scan_networks');
  b.advance(seed.settings.scan_time_ms);
  assert.equal(drainDetector(b.device,'grove').at(-1),'Scan results printed.');
  b.detectorWrite(0,'deauth_detector_v2 --only 1 3');
  const groveReplies=drainDetector(b.device,'grove');
  assert.ok(groveReplies.includes('[DETECTOR_INFO] format=deauth_v2'));
  assert.equal(groveReplies.filter(line=>line.startsWith('[DETECTOR_WATCH]')).length,2);
  assert.ok(groveReplies.indexOf('[DETECTOR_INFO] format=deauth_v2')>
    groveReplies.findLastIndex(line=>line.startsWith('[DETECTOR_WATCH]')));
  assert.match(b.deauthDetectorLine(0,0),/^\[DEAUTH_V2\]/);
  assert.equal(b.deauthDetectorLine(0,1),'');
  assert.match(b.deauthDetectorLine(0,2),/^\[CSA\]/);
  assert.equal(b.deauthDetectorLine(0,3),'');
  assert.equal(b.device.detectorStatus('mbus').details,false);
  b.detectorWrite(2,'deauth_detector');
  drainDetector(b.device,'mbus');
  assert.match(b.deauthDetectorLine(2,0),/^\[DEAUTH\]/);
  b.detectorWrite(0,'stop');
  assert.equal(b.device.detectorStatus('mbus').running,true);
});

test('old JanOS model rejects unknown V2 command and accepts legacy retry',()=>{
  const oldSeed=structuredClone(seed);
  oldSeed.modules.find(module=>module.id==='grove').detector_v2_supported=false;
  const b=new BrowserDevice(oldSeed);
  b.detectorWrite(0,'deauth_detector_v2');
  assert.equal(b.device.detectorStatus('grove').running,false);
  assert.deepEqual(drainDetector(b.device,'grove'),
    ['Unrecognized command']);
  b.detectorWrite(0,'deauth_detector');
  assert.equal(b.device.detectorStatus('grove').running,true);
  assert.match(b.deauthDetectorLine(0,0),/^\[DEAUTH\]/);
});

test('V2 keeps bare-index channel semantics and rejects unsupported options',()=>{
  const d=new SimulatedDevice(seed);
  d.detectorCommand('grove','deauth_detector_v2 --only 1');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.match(drainDetector(d,'grove').join('\n'),/No completed scan results available/);
  d.detectorCommand('grove','scan_networks');
  d.advance(seed.settings.scan_time_ms);
  drainDetector(d,'grove');
  d.detectorCommand('grove','deauth_detector_v2 --details 1');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.match(drainDetector(d,'grove').join('\n'),/Invalid detector option/);
  d.detectorCommand('grove','deauth_detector_v2 1 3');
  assert.equal(d.detectorStatus('grove').details,true);
  assert.deepEqual(d.detectorStatus('grove').channels,[1,6]);
  assert.deepEqual(d.detectorStatus('grove').watchBssids,[]);
  assert.ok(d.deauthEvent('grove',1));
  assert.equal(d.deauthEvent('grove',3),null);
});

test('V2 SSID is bounded and safe for the line parser',()=>{
  const custom=structuredClone(seed);
  custom.networks[0].ssid='Lab (east) | test\n\x1b'+ 'x'.repeat(60);
  const b=new BrowserDevice(custom);
  b.detectorWrite(0,'deauth_detector_v2');
  drainDetector(b.device,'grove');
  const line=b.deauthDetectorLine(0,0);
  assert.match(line,/\| SSID: Lab \(east\) \? test\?\?/);
  assert.ok(!line.includes('\n'));
  assert.ok(!line.includes('\x1b'));
  assert.ok(Buffer.byteLength(line,'utf8')<256);
});

const drainDetector = (device,module) => {
  const lines=[];
  for(let line; (line=device.detectorRead(module))!==null;)lines.push(line);
  return lines;
};

test('legacy detector starts on all networks and bare scan indices only select channels',()=>{
  const d=new SimulatedDevice(seed);
  assert.equal(d.detectorCommand('grove','deauth_detector'),true);
  assert.match(drainDetector(d,'grove').join('\n'),/Deauth Detector started - scanning all channels/);
  assert.deepEqual(d.detectorStatus('grove').watchBssids,[]);
  assert.ok(d.deauthEvent('grove',1));
  assert.equal(d.detectorCommand('grove','stop'),true);
  assert.match(drainDetector(d,'grove').join('\n'),/Deauth Detector stopped/);
  assert.equal(d.detectorStatus('grove').running,false);

  assert.equal(d.detectorCommand('grove','scan_networks'),true);
  d.advance(seed.settings.scan_time_ms);
  assert.equal(drainDetector(d,'grove').at(-1),'Scan results printed.');
  assert.equal(d.detectorCommand('grove','deauth_detector 1 3'),true);
  assert.deepEqual(d.detectorStatus('grove').channels,[1,6]);
  assert.deepEqual(d.detectorStatus('grove').watchBssids,[]);
  // BSSIDs 1 and 2 share channel 1. Both remain visible; channel 36 does not.
  assert.ok(d.deauthEvent('grove',1));
  assert.equal(d.deauthEvent('grove',3),null);
});

test('selected detector requires completed scan and emits matching WATCH records',()=>{
  const d=new SimulatedDevice(seed);
  assert.equal(d.detectorCommand('grove','scan_networks'),true);
  assert.equal(d.detectorCommand('grove','deauth_detector --only 1'),true);
  const pending=drainDetector(d,'grove');
  assert.match(pending[0],/^Background scan started \(/);
  assert.match(pending.join('\n'),/No completed scan results available/);
  assert.equal(d.detectorStatus('grove').running,false);
  d.advance(seed.settings.scan_time_ms);
  const scan=drainDetector(d,'grove');
  assert.equal(scan.at(-1),'Scan results printed.');
  assert.match(scan[0],/^"1","NEON-BAZAAR","","02:20:77:00:00:01","1",/);
  assert.equal(d.detectorCommand('grove','deauth_detector --only 1 3'),true);
  const replies=drainDetector(d,'grove');
  assert.ok(replies.some(line=>line.startsWith('Deauth Detector started - monitoring selected channels')));
  assert.ok(replies.some(line=>line==='[DETECTOR_INFO] CSA/ECSA announcements enabled.'));
  assert.deepEqual(replies.filter(line=>line.startsWith('[DETECTOR_WATCH]')).map(line=>/BSSID=([0-9A-F:]+)/.exec(line)?.[1]),
    ['02:20:77:00:00:01','02:20:77:00:00:03']);
  assert.deepEqual(d.detectorStatus('grove').watchBssids,
    ['02:20:77:00:00:01','02:20:77:00:00:03']);
  assert.equal(d.deauthEvent('grove',1),null);
  assert.equal(d.deauthEvent('grove',2)?.bssid,'02:20:77:00:00:03');
});

test('selected detector filters both DEAUTH and CSA by BSSID and stops emissions',()=>{
  const b=new BrowserDevice(seed);
  assert.equal(b.detectorWrite(0,'scan_networks\r\n'),true);
  b.advance(seed.settings.scan_time_ms);
  assert.equal(drainDetector(b.device,'grove').at(-1),'Scan results printed.');
  assert.equal(b.detectorWrite(0,'deauth_detector --only 1 3\r\n'),true);
  drainDetector(b.device,'grove');
  assert.match(b.deauthDetectorLine(0,0),/^\[DEAUTH\].*02:20:77:00:00:01/);
  assert.equal(b.deauthDetectorLine(0,1),'');
  assert.match(b.deauthDetectorLine(0,2),/^\[CSA\] RX: 6 \| TO: \d+ \| BSSID: 02:20:77:00:00:03 \| TX:/);
  assert.equal(b.deauthDetectorLine(0,3),'');
  b.detectorWrite(0,'stop\r\n');
  assert.equal(b.deauthDetectorLine(0,0),'');
  assert.equal(b.device.detectorStatus('grove').running,false);
});

test('detector rejects empty, invalid, stale and oversized selected scans without claiming running',()=>{
  const d=new SimulatedDevice(seed);
  for(const command of ['deauth_detector --only','deauth_detector --only 0','deauth_detector --only 1']){
    d.detectorCommand('grove',command);
    assert.equal(d.detectorStatus('grove').running,false);
    assert.ok(drainDetector(d,'grove').some(line=>/Usage:|No completed|Invalid/.test(line)));
  }
  d.detectorCommand('grove','scan_networks');d.advance(seed.settings.scan_time_ms);drainDetector(d,'grove');
  d.detectorCommand('grove','deauth_detector --only 1 999');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.match(drainDetector(d,'grove').join('\n'),/Invalid watch index/);
  d.detectorCommand('grove','scan_networks');
  d.detectorCommand('grove','deauth_detector --only 1');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.match(drainDetector(d,'grove').join('\n'),/No completed scan results available/);
  const many=structuredClone(seed);
  many.networks=Array.from({length:65},(_,i)=>({...seed.networks[0],id:`many-${i}`,index:i+1,bssid:`02:20:77:00:${String(Math.floor(i/256)).padStart(2,'0')}:${i.toString(16).padStart(2,'0')}`}));
  many.clients=[];
  const large=new SimulatedDevice(many);
  large.detectorCommand('grove','scan_networks');large.advance(seed.settings.scan_time_ms);drainDetector(large,'grove');
  large.detectorCommand('grove','deauth_detector --only 1');
  assert.equal(large.detectorStatus('grove').running,false);
});

test('detector scan, watch, rejection and Stop are isolated by module',()=>{
  const d=new SimulatedDevice(seed);
  d.detectorCommand('grove','scan_networks');d.advance(seed.settings.scan_time_ms);drainDetector(d,'grove');
  d.detectorCommand('grove','deauth_detector --only 1');drainDetector(d,'grove');
  d.detectorCommand('mbus','deauth_detector --only 2');
  assert.match(drainDetector(d,'mbus').join('\n'),/No completed scan results available/);
  assert.deepEqual(d.detectorStatus('grove').watchBssids,['02:20:77:00:00:01']);
  assert.equal(d.detectorStatus('mbus').running,false);
  d.detectorCommand('mbus','scan_networks');d.advance(seed.settings.scan_time_ms);drainDetector(d,'mbus');
  d.detectorCommand('mbus','deauth_detector --only 2');drainDetector(d,'mbus');
  assert.deepEqual(d.detectorStatus('mbus').watchBssids,['02:20:77:00:00:02']);
  d.detectorCommand('grove','stop');
  assert.equal(d.detectorStatus('grove').running,false);
  assert.equal(d.detectorStatus('mbus').running,true);
  d.setModule('mbus',{connected:false});
  assert.equal(d.detectorStatus('mbus').running,false);
  assert.equal(d.detectorCommand('mbus','deauth_detector'),false);
});

test('explicit Stop after reconnect returns a global acknowledgement',()=>{
  const d=new SimulatedDevice(seed);
  d.detectorCommand('grove','deauth_detector_v2 trace');
  drainDetector(d,'grove');
  d.setModule('grove',{connected:false});
  d.setModule('grove',{connected:true});
  assert.equal(d.detectorCommand('grove','stop'),true);
  assert.deepEqual(drainDetector(d,'grove'),['All operations stopped.']);
  assert.equal(d.detectorStatus('grove').running,false);
});

test('same SSID on two BSSIDs does not widen selected detector watch',()=>{
  const sameName=structuredClone(seed);
  sameName.networks[1].ssid=sameName.networks[0].ssid;
  const d=new SimulatedDevice(sameName);
  d.detectorCommand('grove','scan_networks');d.advance(seed.settings.scan_time_ms);drainDetector(d,'grove');
  d.detectorCommand('grove','deauth_detector --only 1');
  assert.deepEqual(d.detectorStatus('grove').watchBssids,[sameName.networks[0].bssid]);
  assert.equal(d.deauthEvent('grove',1),null);
  assert.equal(d.deauthEvent('grove',0)?.bssid,sameName.networks[0].bssid);
});

test('anti-surv followers cycle the BT devices and stop when disconnected',()=>{
  const d=new SimulatedDevice(seed);
  const f=d.antisurvFollower('grove',0);
  assert.ok(f&&f.mac);
  const macs=new Set();
  for(let i=0;i<(seed.bluetooth||[]).length;i++)macs.add(d.antisurvFollower('grove',i).mac);
  assert.equal(macs.size,(seed.bluetooth||[]).length);
  d.setModule('grove',{connected:false});
  assert.equal(d.antisurvFollower('grove',0),null);
});

test('the bridge formats a MAC|name follower line and picks a handshaker target',()=>{
  const b=new BrowserDevice(seed);
  assert.match(b.antisurvFollowerLine(0,0),/^([0-9A-Fa-f:]+)\|/);
  assert.equal(b.handshakerTarget(0),seed.networks[0].ssid);
  b.device.setModule('grove',{connected:false});
  assert.equal(b.antisurvFollowerLine(0,0),'');
});
