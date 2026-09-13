// S15 - ESPShark PCAP analysis. Guide predicates only read native surfaces of
// the offline PCAP viewer; captures are synthetic documentation-address
// fixtures and no packet is ever transmitted.
import {readUI,StepGuide} from './queue2-common.mjs';

const step=(title,instruction,accept)=>({title,instruction,accept});

function views(ui){
 const has=t=>ui.has(t,true);
 const captureOpen=has('14 packets')||has('indexed 14');
 const map=has('ESPShark Network Topology'),devices=has('Local Devices'),
  endpoints=has('Remote Endpoints'),connections=has('Connections (tap'),
  protocols=has('Top Application Protocols'),objects=has('Extracted Objects'),
  tools=has('ESPShark Cache & Export'),packet=has('HEX / ASCII'),
  investigation=has('ESPShark Offline Investigation');
 const preview=has('Offline synthetic example.'),hash=has('SHA-256');
 // The object preview replaces the list with its own summary popup; it must
 // count as an open view so "return to the capture" waits for it to close.
 const analysisOpen=map||devices||endpoints||connections||protocols||objects||tools||packet||investigation||preview||hash;
 return {captureOpen,map,devices,endpoints,connections,protocols,objects,tools,packet,investigation,
  http:has('HTTP'),http200:has('HTTP 200'),preview,hash,
  intel:has('Offline intelligence:'),fqdnOn:has('FQDN ON'),
  invalid:has('Truncated PCAP'),export:has('EXPORT FILTERED'),
  analysisOpen,capturePage:captureOpen&&!analysisOpen};
}

const openCapture=step('Open a fixture capture','On ESPShark press Load examples, then open example-http-dns-icmp.pcap. Wait for the 14-packet index.',s=>s.captureOpen);
const back=step('Return to the capture','Close the analysis view to return to the indexed capture.',s=>s.capturePage);

const specs={
 'espshark-analysis':{steps:[openCapture,
  step('View the network topology','Open the ESPShark map and read the synthetic nodes.',s=>s.map),
  step('List the local devices','Open Devices and read the local inventory.',s=>s.devices),
  step('List the remote endpoints','Switch to the remote endpoints and confirm 198.51.100.20.',s=>s.endpoints),
  step('Inspect the connections','Open Connections and find the HTTP flow to 198.51.100.20:80.',s=>s.connections),
  step('Check the application protocols','Open Protocols and confirm HTTP is classified.',s=>s.protocols&&s.http),
  back]},
 'espshark-investigate':{steps:[openCapture,
  step('Open a packet detail','Tap packet #1 and read its HEX / ASCII detail.',s=>s.packet),
  step('Read the offline investigation','Open the health view (ESPShark Offline Investigation) and reach its intelligence.',s=>s.investigation&&s.intel),
  step('Toggle FQDN hints','Enable the SHOW FQDN toggle so name hints appear.',s=>s.fqdnOn),
  step('Open the export tools','Open Cache & Export and confirm EXPORT FILTERED PCAP.',s=>s.tools&&s.export),
  back]},
 'espshark-extract':{steps:[openCapture,
  step('Open the extracted objects','Open Objects and confirm the HTTP 200 artifact.',s=>s.objects&&s.http200),
  step('Preview the object and its SHA-256','Preview the object; read its content and the SHA-256 hash.',s=>s.preview&&s.hash),
  back]},
 'espshark-invalid':{steps:[
  step('Open the truncated capture','Open example-invalid-truncated.pcap and read the Truncated PCAP error.',s=>s.invalid),
  step('Open a valid capture instead','Recover by opening example-http-dns-icmp.pcap.',s=>s.captureOpen)]},
};

const titles={'espshark-analysis':'ESPShark analysis (devices, endpoints, connections, protocols)',
 'espshark-investigate':'ESPShark investigation (packet, health, FQDN, export)',
 'espshark-extract':'ESPShark object extraction and hash',
 'espshark-invalid':'ESPShark invalid capture and recovery'};

function spec(id){
 return {id,tab:0,steps:specs[id].steps,
  completionTitle:`${titles[id]} story complete`,
  completion:'You analyzed the offline synthetic capture through the native ESPShark views. No packet was transmitted and the capture file was left unchanged.',
  guard(s,g){
   if(g.step>0&&!s.captureOpen&&!s.invalid&&id!=='espshark-invalid'){g.hint='Reopen a fixture capture to continue this analysis.';return false;}
   return true;
  }};
}

export const pcapStories=Object.keys(specs).map(id=>({
 id,label:titles[id],
 create:()=>new StepGuide(spec(id)),
 read:module=>({tab:module._emu_current_tab(),...views(readUI(module))}),
}));
