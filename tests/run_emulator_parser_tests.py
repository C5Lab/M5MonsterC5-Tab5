"""Compile actual Tab5 parsing code, then feed JanOS fixture bytes.

Run with Python 3.13 on Windows (WSL cc) or Python 3 on Linux (cc).
Only transport, clock and logging are substituted. No firmware is changed.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ui_emulator'))
from source_index import Source

src = Source(ROOT / 'main/main.c')
responses = json.loads((ROOT / 'docs/ui-emulator/neon-district.responses.json').read_text())['responses']
seed = json.loads((ROOT / 'docs/ui-emulator/neon-district.seed.json').read_text())
names = ['trim_ascii_whitespace', 'parse_csv_mixed_fields', 'parse_network_line',
         'parse_bt_device_line', 'parse_probes_from_buffer', 'fetch_html_files_from_sd']
prelude = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#define ESP_LOGI(...) ((void)0)
#define KARMA2_MAX_PROBES 50
static int adhoc_probe_count;
static char adhoc_probes[100][33];
static int evil_twin_html_count;
static char evil_twin_html_files[20][64];
typedef int uart_port_t;
typedef unsigned TickType_t;
static int current_tab;
static unsigned ticks;
static const char *wire;
static size_t offset, chunk;
static int uart_port_for_tab(int t) { return t; }
static void uart_flush(int p) { (void)p; }
static void uart_send_command_for_tab(const char *s) { assert(!strcmp(s,"list_sd")); }
#define pdMS_TO_TICKS(x) (x)
static unsigned xTaskGetTickCount(void) { return ticks; }
static int transport_read_bytes(int p, void *out, size_t n, int timeout) {
 (void)p;
 size_t left=strlen(wire)-offset;
 if(!left) { ticks+=timeout; return 0; }
 if(n>chunk)n=chunk; if(n>left)n=left;
 memcpy(out,wire+offset,n); offset+=n; return (int)n;
}
static void reset_wire(const char *s, size_t size) { wire=s;offset=0;chunk=size;ticks=0; }
static int arp_host_count;
static char arp_our_ip[20];
#define ARP_MAX_HOSTS 32
static struct { char ip[20]; char mac[18]; } arp_hosts[ARP_MAX_HOSTS];
'''
types = '\n'.join(src.text(src.types[n]) for n in ['wifi_network_t','bt_device_t'])
functions = '\n'.join(src.function(n) for n in names)
hosts = src.span('    // Parse response\n    arp_host_count = 0;', '    bsp_display_lock(0);')
functions += '\nstatic void parse_hosts(char *rx_buffer) {\n'+hosts+'\n}\n'
q = lambda x: json.dumps(x, ensure_ascii=True)
constants = '\n'.join('static const char *R_'+k+' = '+q(v)+';' for k,v in responses.items())
checks = []
for i,n in enumerate(seed['networks']):
    checks.append(f'assert(!strcmp(nets[{i}].ssid,{q(n["ssid"])})); assert(!strcmp(nets[{i}].bssid,{q(n["bssid"])})); assert(nets[{i}].channel=={n["channel"]}); assert(nets[{i}].rssi=={n["rssi"]}); assert(!strcmp(nets[{i}].security,{q(n["security"])}));')
for i,b in enumerate(seed['bluetooth']):
    checks.append(f'assert(!strcmp(devs[{i}].name,{q(b["name"])})); assert(!strcmp(devs[{i}].mac,{q(b["mac"])})); assert(devs[{i}].rssi=={b["rssi"]});')
body = r'''
static wifi_network_t nets[20]; static bt_device_t devs[50];
/* Test framing adapter, NOT the production receive task. The line parsers
   and HTML receiver are production code; task scheduling is outside this test. */
static int feed(const char *s, size_t size, bool wifi) {
 char line[512]={0}; size_t pos=0; int count=0;
 for(size_t base=0;base<strlen(s);base+=size) {
  size_t end=base+size; if(end>strlen(s))end=strlen(s);
  for(size_t i=base;i<end;i++) {
   if(s[i]=='\n'||s[i]=='\r') {
    if(pos) {line[pos]=0;
     if(wifi) {if(parse_network_line(line,&nets[count]))count++;}
     else {if(parse_bt_device_line(line,&devs[count]))count++;}
     pos=0;
    }
   } else if(pos<sizeof(line)-1)line[pos++]=s[i];
  }
 }
 return count;
}
int main(void) {
 size_t sizes[]={1,2,7,31,512,4096};
 for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
  assert(feed(R_show_scan_results,sizes[i],true)==EXPECTED_NETWORK_COUNT);
  assert(feed(R_scan_bt,sizes[i],false)==6);
  reset_wire(R_list_sd,sizes[i]); fetch_html_files_from_sd();
  assert(evil_twin_html_count==2);
  assert(!strcmp(evil_twin_html_files[0],"neon-bazaar.html"));
  assert(!strcmp(evil_twin_html_files[1],"afterlife-guest.html"));
 }
 /* EXPECTED_FIELDS */
 char probes[2048]; strcpy(probes,R_list_probes);
 parse_probes_from_buffer(probes,"GROVE"); assert(adhoc_probe_count==3);
 strcpy(probes,R_list_probes); parse_probes_from_buffer(probes,"MBUS");
 assert(adhoc_probe_count==3); assert(!strcmp(adhoc_probes[0],"NEON-BAZAAR"));
 char hosts[4096];strcpy(hosts,R_list_hosts);parse_hosts(hosts);
 assert(arp_host_count==3);assert(!strcmp(arp_hosts[0].ip,"192.0.2.20"));
 assert(!strcmp(arp_our_ip,"192.0.2.10"));
 assert(!strcmp(arp_hosts[0].mac,"02:20:77:01:00:01"));
 strcpy(hosts,"=== Discovered Hosts ===\n  192.0.2.99  ->  (MAC unknown)  [ICMP]\n");
 parse_hosts(hosts);assert(arp_host_count==0);
 wifi_network_t net={0};bt_device_t dev={0};
 assert(!parse_network_line("Scan results printed.",&net));
 assert(!parse_network_line("\"0\",\"X\",\"\",\"02:20:77:00:00:01\",\"1\",\"WPA2\",\"-40\",\"2.4GHz\"",&net));
 assert(!parse_network_line("\"1\",\"missing\"",&net));
 assert(parse_network_line("\"1\",\"NEON, \"\"GHOST\"\"\",\"\",\"02:20:77:00:00:01\",\"1\",\"WPA2\",\"-40\",\"2.4GHz\"",&net));
 assert(!strcmp(net.ssid,"NEON, \"GHOST\""));
 assert(!parse_bt_device_line("BLE scan start failed: -1",&dev));
 assert(!parse_bt_device_line("Summary: 0 AirTags, 0 SmartTags, 0 total devices",&dev));
 assert(feed("Scan results printed.\r\n",1,true)==0);
 assert(feed("Scan failed\r\n",1,true)==0);
 reset_wire("No HTML files found on SD card.\r\n",1);fetch_html_files_from_sd();assert(evil_twin_html_count==0);
 reset_wire("Error opening SD card.\r\n",7);fetch_html_files_from_sd();assert(evil_twin_html_count==0);
 reset_wire("",1);fetch_html_files_from_sd();assert(evil_twin_html_count==0 && ticks>=3000);
 adhoc_probe_count=0;strcpy(probes,"No probe requests captured. Use 'start_sniffer' to collect data.\n");
 parse_probes_from_buffer(probes,"GROVE");assert(adhoc_probe_count==0);
 puts("PASS: five response families, six chunk sizes, fields, duplicate probes, ICMP exclusion, CSV escaping, empty/error/timeout cases");
}
'''.replace('/* EXPECTED_FIELDS */','\n'.join(checks))
code=prelude+types+functions+constants+body.replace('EXPECTED_NETWORK_COUNT',str(len(seed['networks'])))
with tempfile.TemporaryDirectory(prefix='tab5-parser-') as tmp:
    c=Path(tmp)/'test.c';c.write_text(code,encoding='utf-8')
    if os.name=='nt':
        target=subprocess.check_output(['wsl','--exec','wslpath','-a',str(c)],text=True).strip()
        # Fixed script and positional path arguments avoid shell interpolation.
        result=subprocess.run(['wsl','--exec','sh','-c','set -eu; d=$(mktemp -d); trap \'rm -rf "$d"\' EXIT; cc -std=gnu11 -g -fsanitize=address,undefined "$1" -o "$d/test"; "$d/test"','sh',target],capture_output=True,text=True)
    else:
        exe=Path(tmp)/'test';subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
    print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
    report={'status':'passed' if result.returncode==0 else 'failed','source_sha256':hashlib.sha256(src.data).hexdigest(),'fixture_sha256':{name:hashlib.sha256((ROOT/'docs/ui-emulator'/name).read_bytes()).hexdigest() for name in ['neon-district.seed.json','neon-district.responses.json']},'functions':names,'extracted_block':'ARP host parsing block','chunk_sizes':[1,2,7,31,512,4096],'sanitizers':['address','undefined'],'limitations':['Wi-Fi/BLE framing uses a test adapter, not full production receive tasks.','HTML executes the full production fetch function with simulated transport and time.','No live UI, scheduler, hardware or all-command coverage.'],'output':result.stdout,'errors':result.stderr}
    (ROOT/'docs/ui-emulator/parser-test-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    sys.exit(result.returncode)
