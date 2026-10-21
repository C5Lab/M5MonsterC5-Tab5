from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "observer_extended.h"
#include <assert.h>
int main(void) {
 observer_extended_t e; char buf[4096];
 assert(sizeof(e) < 3072);
 assert(observer_ext_suffix_start("Office | Lab, CH1: 0 [Vendor|Corp]")==NULL);
 assert(observer_ext_suffix_start(" 02:AA:BB:CC:DD:EE [Vendor|Corp]")==NULL);
 assert(observer_ext_parse("x | age_ms=12 | rssi=-44 | ext_ver=1 | hidden=1", &e) && e.age_known && e.age_ms==12 && e.rssi==-44);
 assert(observer_ext_parse("Office|Lab, CH1: 0 [Vendor|Corp] | age_ms=12 | ext_ver=1 | rssi=-55", &e) && e.age_ms==12);
 assert(observer_ext_parse(" 02:AA:BB:CC:DD:EE [Vendor|Corp] | age_ms=12 | ext_ver=1 | rssi=-55", &e) && e.age_ms==12);
 assert(!observer_ext_parse("x | ext_ver=1 | ext_ver=1",&e));
 assert(observer_ext_parse("x | ext_ver=1 | rsn_status=valid | rsn_akm=000fac02,000FaC08 | rsn_pairwise=000fac04 | wpa_status=valid | wpa_pairwise=0050f202 | wpa_akm=0050f202",&e));
 assert(strstr(e.security,"WPA2/WPA3") && strstr(e.pairwise,"CCMP-128") && strstr(e.pairwise,"TKIP"));
 assert(strstr(e.akm,"PSK") && strstr(e.akm,"SAE"));
 assert(observer_ext_value(&e,"rsn_akm",buf,sizeof buf) && !strcmp(buf,"000fac02,000FaC08"));
 assert(observer_ext_parse("Office|Lab, CH1: 0 | ext_ver=1 | rssi=-55", &e) && e.rssi==-55);
 assert(observer_ext_parse("Office | ext_ver=999, CH1: 0 | ext_ver=1 | rssi=-55", &e) && e.rssi==-55);
 assert(observer_ext_parse("x | ext_ver=1 | rsn_status=valid | rsn_pairwise= | rsn_akm=", &e));
 char longline[2200]; memset(longline,'A',sizeof longline); memcpy(longline,"x | ext_ver=1 | future=",23); longline[sizeof longline-1]=0;
 assert(observer_ext_parse(longline,&e));
 assert(observer_ext_parse("Name, CH1: 0 | ext_ver=1 | hidden=1 | resolved_ssid_hex=412300FF | rssi=-71 | age_ms=4294967295 | pmf_capable=unknown | pmf_required=unknown | rsn_status=valid | rsn_akm=000FAC02,000FAC08 | rsn_pairwise=000FAC04 | rsn_group=000FAC04 | wpa_status=absent | wps_present=1 | wps_status=valid | wps_device_name_hex=412300FF | future=hello", &e));
 assert(e.present && e.hidden==1 && e.rssi_known && e.rssi==-71 && e.age_known && e.age_ms==UINT32_MAX);
 assert(e.pmf_capable==-1 && e.pmf_required==-1);
 assert(!strcmp(e.resolved_ssid,"A\\x23\\x00\\xFF"));
 assert(strstr(e.security,"WPA2/WPA3"));
 assert(strstr(e.akm,"PSK") && strstr(e.akm,"SAE"));
 assert(observer_ext_value(&e,"future",buf,sizeof buf) && !strcmp(buf,"hello"));
 observer_ext_details(&e,2,buf,sizeof buf); assert(strstr(buf,"A\\x23\\x00\\xFF"));
 observer_ext_details(&e,4,buf,sizeof buf); assert(strstr(buf,"future=hello"));
 assert(observer_ext_parse("x | ext_ver=1 | rsn_status=absent | wpa_status=valid | wpa_akm=0050F202 | wpa_pairwise=0050F202 | wpa_group=0050F202 | wps_present=0", &e));
 assert(!strcmp(e.security,"WPA")); assert(strstr(e.pairwise,"TKIP"));
 observer_ext_details(&e,1,buf,sizeof buf); assert(strstr(buf,"Legacy WPA") && strstr(buf,"RSN"));
 observer_ext_details(&e,2,buf,sizeof buf); assert(strstr(buf,"not advertised"));
 assert(observer_ext_parse("x | ext_ver=1 | rsn_status=valid | rsn_akm=123456AB | rsn_pairwise=123456AB", &e));
 assert(strstr(e.akm,"123456AB"));
 assert(observer_ext_parse("x | ext_ver=1 | privacy=unknown | rsn_status=unknown | wpa_status=unknown", &e)); assert(!strcmp(e.security,"Unknown"));
 assert(observer_ext_parse("x | ext_ver=1 | privacy=0 | rsn_status=absent | wpa_status=absent", &e)); assert(!strcmp(e.security,"Open"));
 assert(observer_ext_parse("x | age_ms=0 | ext_ver=1 | resolved_ssid_hex= | rssi=unknown", &e)); assert(e.resolved_ssid[0]==0 && !e.rssi_known);
 assert(!observer_ext_parse("legacy",&e) && !e.present);
 assert(!observer_ext_parse("x | ext_ver=2",&e) && !e.present);
 assert(!observer_ext_parse("x | ext_ver=1 | resolved_ssid_hex=0",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | wps_model_name_hex=GG",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | age_ms=4294967296",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | rssi=-200",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | hidden=0 | hidden=1",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | broken",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | future=1\n | hidden=0",&e));
 assert(!observer_ext_parse("x | ext_ver=1 | rsn_pairwise=123",&e));
 assert(observer_ext_parse("x | ext_ver=1 | wps_present=1 | wps_state=2 | wps_config_methods=10120 | wps_setup_locked=1",&e));
 observer_ext_details(&e,2,buf,sizeof buf); assert(strstr(buf,"Configured") && strstr(buf,"Push button") && strstr(buf,"Keypad"));
 char huge[2500]; memset(huge,'a',sizeof huge); memcpy(huge,"x | ext_ver=1 | z=",17); huge[sizeof huge-1]=0; assert(!observer_ext_parse(huge,&e));
 assert(observer_ext_parse("x | ext_ver=1 | resolved_ssid_hex=C5BCC3B3C582776965 | hidden=0",&e)); assert(!strcmp(e.resolved_ssid,"żółwie"));
 char tiny[2]={1,1}; observer_ext_details(&e,4,tiny,sizeof tiny); assert(tiny[1]==0);
 puts("extended parser: all assertions passed");
 return 0;
}
'''

FIXTURE = ROOT / "tests/fixtures/observer_extended.log"

class ExtendedParser(unittest.TestCase):
    def test_compiled_protocol_and_display_cases(self):
        self.assertTrue((ROOT/'main/observer_extended.h').exists(), 'extended parser header is missing')
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            folder = Path(temporary)
            harness = HARNESS
            fixture_lines = [line.strip() for line in FIXTURE.read_text(encoding='utf-8').splitlines() if ' | ext_ver=1' in line]
            self.assertGreater(len(fixture_lines), 50)
            fixture_assertions = '\n'.join(' assert(observer_ext_parse(' + json.dumps(line, ensure_ascii=False) + ', &e));' for line in fixture_lines)
            harness = harness.replace(' puts("extended parser:', fixture_assertions + '\n puts("extended parser:')
            (folder/'test.c').write_text(harness, encoding='utf-8')
            compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
            if compiler:
                exe=folder/'test.exe'
                subprocess.run([compiler,'-std=c99','-Wall','-Wextra','-Werror','-I',str(ROOT/'main'),str(folder/'test.c'),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)
            elif shutil.which('docker'):
                subprocess.run(['docker','run','--rm','-v',f'{ROOT}:/work','-w','/work','emscripten/emsdk:4.0.14','bash','-lc',f'cc -std=c99 -Wall -Wextra -Werror -I main {folder.name}/test.c -o /tmp/ext-test && /tmp/ext-test'],check=True)
            else:
                self.fail('A C compiler or Docker is required; parser tests must execute')

if __name__ == '__main__': unittest.main()


