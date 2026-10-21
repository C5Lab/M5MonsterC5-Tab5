"""Execute the production receiver against multiple AP identities and snapshots."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_observer_incremental_ui_contract import function_body

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'main/main.c').read_text(encoding='utf-8')

def definition(name, result):
    body = function_body(SOURCE, 'static ' + result + ' ' + name)
    start = SOURCE.index(body, SOURCE.index('static ' + result + ' ' + name))
    signature = re.search('static ' + result + ' ' + name + r'\([^;{}]*\)\s*$', SOURCE[:start], re.S)
    return signature.group() + body

def execute_c(source):
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        (folder / 'test.c').write_text(source, encoding='utf-8')
        compiler = shutil.which('gcc') or shutil.which('cc')
        if compiler:
            binary = folder / 'test.exe'
            subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'main'), str(folder / 'test.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        else:
            subprocess.run(['docker', 'run', '--rm', '--mount', f'type=bind,source={ROOT},target=/repo', '--mount', f'type=bind,source={folder},target=/work', 'emscripten/emsdk:4.0.14', 'bash', '-lc', 'cc -std=c11 -Wall -Wextra -Werror -I/repo/main /work/test.c -o /work/test && /work/test'], check=True)

class ObserverReceiveTests(unittest.TestCase):
    def test_production_identity_and_client_receiver(self):
        model = SOURCE[SOURCE.index('// Observer network info structure'):SOURCE.index('// LVGL handles for one Observer')]
        helpers = '\n'.join(definition(name, kind) for name, kind in [
            ('trim_ascii_whitespace', 'void'), ('observer_extract_vendor_bracket', 'bool'), ('parse_sniffer_network_line', 'bool'),
            ('parse_sniffer_client_line', 'bool'), ('add_client_mac', 'bool'),
            ('observer_scan_field_is_mac', 'bool')])
        harness = r'''
#include <assert.h>
#include <strings.h>
#include <ctype.h>
#include "observer_extended.h"
#define MAX_CLIENTS_PER_NETWORK 20
#define MAX_OBSERVER_NETWORKS 100
#define MALLOC_CAP_SPIRAM 0
#define heap_caps_calloc(n,s,c) calloc(n,s)
'''+model+r'''
typedef struct {
 observer_network_t *observer_networks;
 int observer_network_count, observer_extended_support;
 bool observer_has_new_networks;
} tab_context_t;
static uint32_t observer_clock_ms(void) { return 5000; }
'''+helpers+'\n'+(ROOT/'main/observer_receive.inc').read_text()+r'''
int main(void) {
 tab_context_t ctx={0};ctx.observer_networks=calloc(100,sizeof(observer_network_t));assert(ctx.observer_networks);
 const char *first=", CH11: 2 | ext_ver=1 | bssid=94:2A:6F:A4:EB:DE | hidden=1 | resolved_ssid_hex=7839392D6163 | rssi=-54 | age_ms=577";
 const char *second=", CH11: 1 | ext_ver=1 | bssid=9A:2A:6F:A4:EB:DE | hidden=1 | resolved_ssid_hex=7837";
 assert(observer_receive_ap(&ctx,first,false,-1)==0);
 assert(observer_receive_ap(&ctx,second,false,-1)==1);
 assert(ctx.observer_network_count==2 && ctx.observer_has_new_networks);
 assert(!strcmp(ctx.observer_networks[0].extended.resolved_ssid,"x99-ac"));
 assert(!strcmp(ctx.observer_networks[1].extended.resolved_ssid,"x7"));
 assert(ctx.observer_networks[0].scan_index==0 && ctx.observer_networks[0].snapshot_live);
 assert(observer_receive_ap(&ctx,second,false,0)==-1);
 assert(observer_receive_ap(&ctx,", CH11: 1",false,-1)==-1); /* ambiguous legacy */
 assert(observer_receive_ap(&ctx,"Bad, CH1: 0 | ext_ver=1 | bssid=invalid",false,-1)==-1);
 assert(observer_receive_client(&ctx.observer_networks[0]," 54:C9:DF:67:77:EA [Maker] | ext_ver=1 | rssi=-56 | age_ms=8104",true));
 assert(ctx.observer_networks[0].client_count==1 && ctx.observer_networks[1].client_count==0);
 assert(!strcmp(ctx.observer_networks[0].client_vendors[0],"Maker"));
 assert(ctx.observer_networks[0].client_rssi[0]==-56 && ctx.observer_networks[0].client_age_ms[0]==8104);
 assert(strstr(ctx.observer_networks[0].client_extended[0],"age_ms=8104"));
 assert(observer_receive_client(&ctx.observer_networks[0]," 54:C9:DF:67:77:EA | ext_ver=1 | rssi=unknown | age_ms=1",false));
 assert(ctx.observer_networks[0].client_count==1 && !ctx.observer_networks[0].client_rssi_known[0]);
 assert(!strcmp(ctx.observer_networks[0].client_vendors[0],"Maker"));
 assert(!observer_receive_client(&ctx.observer_networks[0]," not-a-mac | ext_ver=1",false));
 assert(observer_receive_ap(&ctx,"Office|Lab, CH12: 0 | ext_ver=1 | bssid=00:11:22:33:44:55",false,-1)==2);
 assert(observer_receive_ap(&ctx,"Office, CH fake, CH12: 0 | ext_ver=1 | bssid=00:11:22:33:44:66",false,-1)==3);
 assert(observer_receive_ap(&ctx,"Office | ext_ver=fake, CH12: 0 | ext_ver=1 | bssid=00:11:22:33:44:77",false,-1)==4);
 assert(observer_is_noise_line("Sniffer packet count: 9244"));
 assert(observer_is_prompt_line(" > \t"));
 assert(!observer_is_prompt_line(" 54:C9:DF:67:77:EA"));
 assert(observer_extended_rejected("Usage: show_sniffer_results"));
 assert(strstr(observer_query_command(&ctx,true),"vendor extended"));
 ctx.observer_extended_support=-1;assert(!strstr(observer_query_command(&ctx,true),"extended"));
 free(ctx.observer_networks);puts("receiver assertions passed");
 return 0;
}
'''
        execute_c(harness)

    def test_options_model(self):
        execute_c((ROOT/'tests/observer_options_test.c').read_text().replace('../main/observer_options.h', 'observer_options.h'))

if __name__ == '__main__':
    unittest.main()
