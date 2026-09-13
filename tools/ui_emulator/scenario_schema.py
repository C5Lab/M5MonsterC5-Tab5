"""Generate the proposed scenario contract, independent of the live runtime."""
import json
from source_index import ROOT

def obj(properties):
    return {'type':'object','properties':properties,'required':list(properties),'additionalProperties':False}
def arr(items):return {'type':'array','items':items}
def enum(*values):return {'enum':list(values)}
string={'type':'string'}
integer={'type':'integer'}
number={'type':'number'}
boolean={'type':'boolean'}
ident={'type':'string','minLength':1}
mac={'type':'string','pattern':r'^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$'}
rssi={'type':'integer','minimum':-127,'maximum':0}
network=obj(dict(id=ident,index={'type':'integer','minimum':1},ssid={'type':'string','maxLength':32},bssid=mac,vendor=string,channel={'type':'integer','minimum':1,'maximum':196},security=enum('Open','WEP','WPA','WPA2','WPA/WPA2 Mixed','WPA2 Enterprise','WPA3','WPA2/WPA3 Mixed','WAPI','OWE','Unknown'),rssi=rssi,band=enum('2.4GHz','5GHz')))
schema={'$schema':'https://json-schema.org/draft/2020-12/schema','title':'Monster emulator scenario draft',**obj({
 'schema_version':{'const':1},'id':ident,'status':string,'seed':integer,
 'experience':obj(dict(default_timing=enum('demo','realistic'),demo_clock_scale={'type':'number','minimum':1},realistic_clock_scale={'const':1},default_module_profile={'const':'available-virtual-modules'},excluded_features={'const':['subghz']})),
 'source':obj(dict(repository=string,head=string,main_sha256={'type':'string','pattern':'^[0-9a-f]{64}$'})),
 'networks':arr(network),'clients':arr(obj(dict(id=ident,label=string,mac=mac,network_id=ident,ip={'type':'string','format':'ipv4'}))),
 'lan':obj(dict(network_id=ident,module_id=ident,own_ip={'type':'string','format':'ipv4'},netmask={'type':'string','format':'ipv4'})),
 'bluetooth':arr(obj(dict(id=ident,name={'type':'string','maxLength':63},mac=mac,rssi=rssi))),
 'probe_network_ids':arr(ident),'portal_filenames':arr(string),'notes':arr(string),
 'modules':arr(obj(dict(id=ident,transport=enum('grove','mbus','usb','internal'),connected=boolean,sd_present=boolean,radio_mode=enum('wifi','ble','idle'),producer=enum('janos','tab5-internal','subghz-separate'),capabilities=arr(string)))),
 'settings':obj(dict(rotation=enum(0,90,180,270),red_team=boolean,theme=string,scan_time_ms={'type':'integer','minimum':1})),
 'gps':obj(dict(fix=boolean,latitude={'type':'number','minimum':-90,'maximum':90},longitude={'type':'number','minimum':-180,'maximum':180},speed_kmh={'type':'number','minimum':0},route=arr(obj(dict(at_ms={'type':'integer','minimum':0},latitude=number,longitude=number))))),
 'subghz_signals':arr(obj(dict(id=ident,label=string,frequency_hz={'type':'integer','minimum':1},rssi=rssi,protocol=string))),
 'files':arr(obj(dict(id=ident,path=string,kind=enum('pcap','html','wardrive','subghz','credentials','report'),content_status=enum('planned','materialized'),network_id={'type':['string','null']},size_bytes={'type':['integer','null'],'minimum':0}))),
 'jobs':arr(obj(dict(id=ident,module_id=ident,kind=string,state=enum('queued','running','completed','failed','cancelled'),generation={'type':'integer','minimum':0},started_at_ms={'type':'integer','minimum':0},progress={'type':'number','minimum':0,'maximum':1},result_file_ids=arr(ident)))),
 'variants':arr(obj(dict(id=ident,domain=string,condition=enum('populated','empty','disconnected','timeout','failure'),expected_outcome=string)))
})}
if __name__=='__main__':
    (ROOT/'tools/ui_emulator/scenario.schema.json').write_text(json.dumps(schema,indent=2)+'\n',encoding='utf-8')
    print('Wrote scenario.schema.json')
