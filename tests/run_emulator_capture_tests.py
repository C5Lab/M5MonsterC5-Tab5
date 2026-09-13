"""Read a model-generated PCAP using the actual firmware reader under sanitizers."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
report={'status':'running','scope':'Phase 3 standalone model capture; browser integration pending'}
code=r'''
#include <assert.h>
#include <stdio.h>
#include <strings.h>
#include "pcap_reader.h"
int main(int argc,char **argv) {
 assert(argc==2);
 pcap_reader_t *reader=NULL;pcap_capture_info_t info;
 assert(pcap_reader_open(argv[1],&reader,&info)==PCAP_READER_OK);
 assert(info.link_type==PCAP_LINKTYPE_ETHERNET && info.file_size==198);
 pcap_packet_index_t index[4];pcap_scan_summary_t summary;
 assert(pcap_reader_scan(reader,index,4,&summary,NULL,NULL,NULL)==PCAP_READER_OK);
 assert(summary.packet_count==3 && summary.captured_bytes==126);
 assert(!summary.truncated_tail && !summary.malformed_records);
 for(int i=0;i<3;i++) {
  pcap_packet_details_t detail;
  assert(pcap_reader_describe_packet(reader,&index[i],&detail)==PCAP_READER_OK);
  assert(detail.flags & PCAP_PACKET_FLAG_ARP);
  assert(!detail.malformed && !detail.payload_truncated);
  assert(!strcasecmp(detail.source_mac,"02:20:77:00:00:01"));
  char peer[18],ip[20];snprintf(peer,sizeof(peer),"02:20:77:01:00:%02x",i+1);
  snprintf(ip,sizeof(ip),"192.0.2.%d",20+i);
  assert(!strcasecmp(detail.destination_mac,peer));
  assert(!strcasecmp(detail.arp_target_ip,ip));
 }
 pcap_reader_close(reader);
 puts("PASS: production PCAP reader parsed three synthetic ARP frames, selected AP and all three clients");
}
'''
try:
    with tempfile.TemporaryDirectory(prefix='tab5-capture-') as folder:
        temp=Path(folder);fixture=temp/'capture.pcap';test=temp/'test.c';exe=temp/'test'
        test.write_text(code,encoding='utf-8')
        subprocess.run(['node',str(ROOT/'tests/export_emulator_capture.mjs'),str(fixture)],check=True)
        report['capture_sha256']=hashlib.sha256(fixture.read_bytes()).hexdigest()
        source=ROOT/'components/pcap_reader/pcap_reader.c'
        report['reader_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
        def path(p):
            if os.name=='nt':
                return subprocess.check_output(['wsl','--exec','wslpath','-a',str(p)],text=True).strip()
            return str(p)
        prefix=['wsl','--exec'] if os.name=='nt' else []
        subprocess.run(prefix+['cc','-std=gnu11','-g','-fsanitize=address,undefined',
            '-I'+path(ROOT/'components/pcap_reader/include'),path(source),path(test),'-o',path(exe)],check=True)
        result=subprocess.run(prefix+[path(exe),path(fixture)],text=True,capture_output=True)
        print(result.stdout,end='');print(result.stderr,end='')
        report.update(status='passed' if result.returncode==0 else 'failed',output=result.stdout,errors=result.stderr)
        if result.returncode:raise RuntimeError('Production reader rejected model capture')
except Exception as error:
    report.update(status='failed',error=str(error));raise
finally:
    (ROOT/'docs/ui-emulator/phase3-capture-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
