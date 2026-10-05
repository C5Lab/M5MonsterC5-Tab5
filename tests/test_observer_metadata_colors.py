"""Compile the actual AP metadata formatter, including preferences and escaping."""
import unittest
from test_observer_extended_receive import ROOT, SOURCE, execute_c


class ObserverMetadataColors(unittest.TestCase):
    def test_actual_formatter(self):
        model = SOURCE[SOURCE.index('// Observer network info structure'):SOURCE.index('// LVGL handles for one Observer')]
        execute_c(r'''
#include <assert.h>
#include "observer_extended.h"
#include "observer_options.h"
#define MAX_CLIENTS_PER_NETWORK 20
'''+model+r'''
static observer_view_prefs_t observer_view_prefs;
static bool observer_resolve_vendors = true;
static int64_t esp_timer_get_time(void) { return 5000000; }
#include "observer_view.inc"
int main(void) {
 observer_network_t n={0}; char out[1024], field[256];
 observer_prefs_defaults(&observer_view_prefs);
 strcpy(n.security,"WPA3");strcpy(n.band,"5GHz");strcpy(n.bssid,"00:11:22:33:44:55");n.channel=36;
 observer_field_text(&n,OBS_FIELD_CHANNEL,field,sizeof field);
 assert(!strcmp(field,"#5599FF CH36# / #CC66FF 5GHz#"));
 observer_field_text(&n,OBS_FIELD_BSSID,field,sizeof field);assert(!strcmp(field,"#5599FF 00:11:22:33:44:55#"));
 strcpy(n.band,"2.4GHz");observer_field_text(&n,OBS_FIELD_CHANNEL,field,sizeof field);
 assert(strstr(field,"#FFAA33 2.4GHz#"));
 int levels[]={-49,-50,-69,-70};const char *colors[]={"#55DD55","#FFAA00","#FFAA00","#FF5555"};
 for(int i=0;i<4;i++){n.rssi=levels[i];observer_field_text(&n,OBS_FIELD_RSSI,field,sizeof field);assert(strstr(field,colors[i])==field);}
 observer_field_text(&n,OBS_FIELD_SECURITY,field,sizeof field);assert(!strcmp(field,"#55DD55 WPA3#"));
 strcpy(n.security,"WPA2");observer_field_text(&n,OBS_FIELD_SECURITY,field,sizeof field);assert(!strcmp(field,"#00CCCC WPA2#"));
 strcpy(n.security,"Open");observer_field_text(&n,OBS_FIELD_SECURITY,field,sizeof field);assert(strstr(field,"#FF6666"));
 strcpy(n.security,"Unknown");observer_field_text(&n,OBS_FIELD_SECURITY,field,sizeof field);assert(strstr(field,"#888888"));
 observer_field_text(&n,OBS_FIELD_PMF,field,sizeof field);assert(strstr(field,"#888888"));
 n.inspected=true;n.mfp_capable=true;observer_field_text(&n,OBS_FIELD_PMF,field,sizeof field);assert(strstr(field,"#FF5555"));
 n.mfp_capable=false;observer_field_text(&n,OBS_FIELD_PMF,field,sizeof field);assert(strstr(field,"#55DD55"));
 n.extended.present=true;strcpy(n.extended.pmf,"Unknown");n.extended.pmf_capable=0;n.extended.pmf_required=-1;
 observer_field_text(&n,OBS_FIELD_PMF,field,sizeof field);assert(strstr(field,"#888888"));
 n.extended.pmf_required=0;observer_field_text(&n,OBS_FIELD_PMF,field,sizeof field);assert(strstr(field,"#55DD55"));
 n.extended.pmf_capable=1;observer_field_text(&n,OBS_FIELD_PMF,field,sizeof field);assert(strstr(field,"#FF5555"));
 n.extended.rssi_known=false;observer_field_text(&n,OBS_FIELD_RSSI,field,sizeof field);assert(!strcmp(field,"#888888 RSSI unknown#"));
 int states[]={-1,0,1};const char *wpscolors[]={"#888888","#CCCCCC","#FFAA00"};
 for(int i=0;i<3;i++){n.extended.wps_present=states[i];strcpy(n.extended.wps,"WPS");observer_field_text(&n,OBS_FIELD_WPS,field,sizeof field);assert(strstr(field,wpscolors[i])==field);}
 strcpy(n.extended.security,"WPA2 #FF0000 injected#");strcpy(n.vendor,"Maker #ABCDEF injected#");
 observer_format_fields(&n,out,sizeof out);assert(strstr(out,"#00CCCC WPA2 _FF0000 injected_#"));assert(!strstr(out,"#ABCDEF"));
 assert(!strcmp(n.extended.security,"WPA2 #FF0000 injected#")); /* Raw evidence stays intact. */
 char tiny[12];observer_format_fields(&n,tiny,sizeof tiny);assert(!tiny[0]); /* No partial color marker. */
 observer_view_prefs.ap_visible[OBS_FIELD_SECURITY]=0;
 assert(observer_prefs_move(observer_view_prefs.ap_order,OBS_FIELD_COUNT,2,-1));
 assert(observer_prefs_move(observer_view_prefs.ap_order,OBS_FIELD_COUNT,1,-1));
 observer_format_fields(&n,out,sizeof out);assert(strstr(out,"#FFAA00 WPS#")==out);assert(!strstr(out,"injected_#"));
 observer_format_client(&n,0,field,sizeof field); /* Compile unchanged client formatter too. */
 puts("metadata color assertions passed");
 return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
