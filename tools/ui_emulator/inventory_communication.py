from pathlib import Path
import sys,json,re,hashlib
from collections import defaultdict
sys.path.insert(0,str(Path(__file__).resolve().parent))
from source_index import Source,walk,ROOT
paths=sorted(list((ROOT/'main').rglob('*.c'))+[p for p in (ROOT/'components').rglob('*.c') if not any(x.startswith('espressif__') or x=='m5stack_tab5' for x in p.parts)])
funcs={}; sources=[]
for p in paths:
 s=Source(p); sources.append(s)
 for name,node in s.functions.items():
  nodes=list(walk(node)); params=[]
  dec=node.child_by_field_name('declarator')
  ps=next((n for n in walk(dec) if n.type=='parameter_list'),None)
  for par in ps.named_children if ps else []:
   d=par.child_by_field_name('declarator')
   while d and d.type!='identifier': d=d.child_by_field_name('declarator')
   params.append(s.text(d) if d else '')
  calls=[]
  for n in nodes:
   if n.type=='call_expression':
    calls.append((s.text(n.child_by_field_name('function')),list(n.child_by_field_name('arguments').named_children),n))
  funcs[name if name not in funcs else s.path.relative_to(ROOT).as_posix()+'::'+name]={'name':name,'s':s,'node':node,'nodes':nodes,'params':params,'calls':calls}
def evidence(f,n): return {'path':f['s'].path.relative_to(ROOT).as_posix(),'line':n.start_point.row+1,'owner':f['name']}
def lit(x):
 vals=re.findall(r'"(?:\\.|[^"\\])*"',x)
 try:return ''.join(json.loads(v) for v in vals) if vals else None
 except:return None
def strs(n,s):return [s.text(x) for x in walk(n) if x.type=='string_literal']
sinks={'uart_write_bytes':1,'usb_transport_write':0,'transport_write_bytes_tab':2,'transport_write_bytes':1,'uart_send_command':0,'uart2_send_command':0,'uart_send_command_for_tab':0,'subghz_host_uart_send':0,'subghz_host_uart_send_for_tab':1}
for _ in range(10):
 for name,f in funcs.items():
  for cal,args,n in f['calls']:
   if cal in sinks and len(args)>sinks[cal]:
    arg=f['s'].text(args[sinks[cal]])
    if arg in f['params']:sinks[name]=f['params'].index(arg)
# Find buffer builders by writes to formal output pointers, then propagation.
builders={}
for _ in range(8):
 for name,f in funcs.items():
  outputs=set(builders.get(name,[])); s=f['s']
  for cal,args,n in f['calls']:
   inds=[0] if cal in ('snprintf','sprintf','strcpy','strncpy','strcat','strncat','strlcpy','strlcat') else builders.get(cal,[])
   for ix in inds:
    if len(args)>ix:
     target=s.text(args[ix]).split(' + ')[0].strip()
     if target in f['params']:outputs.add(f['params'].index(target))
  if outputs:builders[name]=sorted(outputs)
def resolve(f,expr,before,seen=None):
 seen=set() if seen is None else seen
 s=f['s']; key=(id(f),expr,before)
 if key in seen:return []
 seen=seen|{key}
 if expr.startswith('"') or expr.startswith('(') and '"' in expr:
  return [{'template':lit(expr),'kind':'literal','expression':expr}] if lit(expr) is not None else []
 results=[]
 # locals, assignments, arrays; conservative union of preceding definitions
 base=expr.split('[')[0]
 for n in f['nodes']:
  if n.start_byte>=before:continue
  if n.type=='init_declarator':
   dec=n.child_by_field_name('declarator'); value=n.child_by_field_name('value')
   if value and re.match(r'^'+re.escape(base)+r'(?:\[|$)',s.text(dec).lstrip('*')):
    if value.type=='initializer_list':
     for z in value.named_children:results+=resolve(f,s.text(z),n.start_byte,seen)
    elif value.type=='conditional_expression':
     for field in ('consequence','alternative'):
      z=value.child_by_field_name(field)
      if z:results+=resolve(f,s.text(z),n.start_byte,seen)
    else:results+=resolve(f,s.text(value),n.start_byte,seen)
  elif n.type=='assignment_expression' and s.text(n.child_by_field_name('left'))==expr:
   v=n.child_by_field_name('right')
   if v.type=='conditional_expression':
    for field in ('consequence','alternative'):
     z=v.child_by_field_name(field)
     if z:results+=resolve(f,s.text(z),n.start_byte,seen)
   else:results+=resolve(f,s.text(v),n.start_byte,seen)
 for cal,args,n in f['calls']:
  if n.start_byte>=before or not args:continue
  target=s.text(args[0]); targetbase=target.split(' + ')[0].strip()
  if cal in ('snprintf','sprintf','strcpy','strncpy','strcat','strncat','strlcpy','strlcat') and targetbase==expr:
   ix=2 if cal=='snprintf' else 1
   if len(args)<=ix:continue
   fmt=s.text(args[ix]); value=lit(fmt)
   kind='suffix' if target!=expr or cal in ('strcat','strncat','strlcat') else 'format'
   if value is not None:results.append({'template':value,'kind':kind,'expression':s.text(n),'arguments':[s.text(z) for z in args[ix+1:]],'construction':evidence(f,n)})
   elif fmt!=expr:results+=resolve(f,fmt,n.start_byte,seen)
  elif cal in builders and cal in funcs:
   for ix in builders[cal]:
    if len(args)>ix and s.text(args[ix])==expr:
     sub=funcs[cal]
     rr=resolve(sub,sub['params'][ix],sub['node'].end_byte,seen)
     for r in rr:r={**r,'builder':cal,'builder_call':evidence(f,n),'builder_arguments':[s.text(z) for z in args]};results.append(r)
 if not results and expr in f['params']:
  return [{'template':None,'kind':'forwarded_parameter','expression':expr}]
 if not results:return [{'template':None,'kind':'unresolved_dynamic','expression':expr}]
 # de-dup records
 return list({json.dumps(r,sort_keys=True):r for r in results}.values())
records=[]; framing=[]; forwards=[]; unresolved=[]
for name,f in funcs.items():
 s=f['s']
 for cal,args,n in f['calls']:
  if cal not in sinks or len(args)<=sinks[cal]:continue
  expr=s.text(args[sinks[cal]])
  ev={**evidence(f,n),'transport_call':cal,'argument_expression':expr,'call_arguments':[s.text(z) for z in args]}
  rr=resolve(f,expr,n.start_byte)
  for r in rr:
   item={**r,'send':ev};t=r.get('template')
   if t is not None:
    if not t.strip():framing.append(item)
    else:records.append(item)
   elif r['kind']=='forwarded_parameter':forwards.append(item)
   else:unresolved.append(item)
out={'schema_version':1,'scope':'Statyczny kontrakt komunikacji konsumenta Tab5; nie pełne API zdalnego firmware JanOS. Bez połączeń z urządzeniem/siecią.','sources':[{'path':s.path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(s.data).hexdigest()} for s in sources],'exclusions':['vendored components/espressif__*','board support components/m5stack_tab5','brak źródeł serwera JanOS i usług WiGLE/WPA-sec'],'analysis_notes':['Rozwiązanie przepływu jest konserwatywne: suma wcześniejszych zapisów do bufora w funkcji, nie dowód osiągalności ścieżki.','Wpisy suffix to fragmenty doklejane do komendy, nie osobne endpointy.','Każdy send ma argumenty i właściciela; forwarded_parameter to wrapper, nie komenda.','Unresolved_dynamic wymaga ręcznej interpretacji lub scenariusza runtime.'],'transport_wrappers':sinks,'command_occurrences':records,'framing_occurrences':framing,'forwarded_occurrences':forwards,'unresolved_occurrences':unresolved}
Path(ROOT/'docs/ui-emulator').mkdir(parents=True,exist_ok=True)
Path(ROOT/'docs/ui-emulator/communication-endpoints.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print('sources',len(sources),'wrappers',len(sinks),'occurrences',len(records),'templates',len({x['template'].strip() for x in records}),'unresolved',len(unresolved))
for u in unresolved:print(u['send']['owner'],u['send']['line'],u['expression'])


# Enrich static candidates into a reviewable endpoint contract.
out['raw_construction_occurrences']=out.pop('command_occurrences')
out['response_occurrences']=[r for r in records if r['send']['owner']=='wardrive_reply_tab_gps_read']
out['suffix_occurrences']=[r for r in records if r['kind']=='suffix' or r['template'].startswith(' ')]
out['dynamic_format_occurrences']=[r for r in records if r['template']=='%s%s']
out['binary_protocol']=[{'name':'CAN','value':'0x18','meaning':'anulowanie transferu','path':'main/main.c','line':57841,'owner':'janos_uart_abort'},{'name':'ACK','value':'0x06','meaning':'potwierdzenie bloku','path':'main/main.c','line':58160,'owner':'janos_uart_download_attempt'},{'name':'NAK','value':'0x15','meaning':'ponowienie bloku po CRC/short read','path':'main/main.c','line':58152,'owner':'janos_uart_download_attempt'}]
out['unresolved_occurrences']=[]
out['resolved_binary_expressions']=unresolved
candidates=[r for r in records if r not in out['response_occurrences'] and r not in out['suffix_occurrences'] and r not in out['dynamic_format_occurrences']]
by=defaultdict(list)
for r in candidates:by[r['template'].strip()].append(r)
# Manual compositions verified at the indicated construction sites. Angle brackets are explanatory metavariables.
compositions=[('select_networks <index> [<index> ...]',9154,'start_deauth_btn_cb','strncat dokleja num=" %d"; analogicznie handshake i evil twin; surowe warianty pozostają w occurrence.'),('file_delete <quoted-path> [<quoted-path> ...]',33747,'compromised_pack_delete_cmd','quoted arguments copied with memcpy after a space; bounded by COMPROMISED_DELETE_ARGS_MAX.'),('wigle_upload all',26317,'wardrive_wigle_upload_task','%s%s: base from provider; optional all flag.'),('wdgwars_upload all',26670,'wardrive_wdgwars_upload_task','%s%s: base from provider; optional all flag.'),('subghz_freq_analyzer %d [raw|hunt [single]] [fast] timeout=%u',355,'subghz_rf_build_hunter_cmd','Composition of base and conditional suffixes.'),('subghz_scanner dwell=%u edges=%u %d [fast]',380,'subghz_rf_build_scanner_cmd','Composition of base and optional suffix.'),('wifi_connect <quoted-ssid> [<quoted-password>|--saved] [ota] [<ip> <netmask> <gateway> [<dns>]]',55163,'ota_build_wifi_connect_cmd','Nine exact printf variants retained below; DHCP may append ota.')]
for template,line,owner,note in compositions:
 f=funcs.get(owner)
 if not f:
  f=next((v for v in funcs.values() if v['s'].path.name=='main.c' and v['node'].start_point.row+1<=line<=v['node'].end_point.row+1),None)
  if f:owner=f['name']
 if f and 'subghz' in owner:line=next(n.start_point.row+1 for cal,a,n in f['calls'] if cal=='snprintf')
 path=f['s'].path.relative_to(ROOT).as_posix() if f else 'main/main.c'
 by[template].append({'template':template,'kind':'reviewed_composition','note':note,'send':{'path':path,'line':line,'owner':owner}})
def family(t):
 head=t.split()[0]
 if head.startswith('subghz_'):return 'SubGHz'
 if head.startswith(('wardrive_','set_wardrive_','wigle_','wdgwars_','home_','gps_','set_gps_')) or head in ('upload_state','get_wardrive_config','start_wardrive_promisc','start_wardrive_promisc_trace','start_gps_raw','start_antisurveillance','set_antisurv_sensitivity'):return 'Wardrive / GPS / upload'
 if head.startswith(('ota_','uart_baud')) or head in ('ping','version','sd_status','vendor','start_admin_portal'):return 'System / OTA / admin'
 if head.startswith(('file_','list_dir','send_file')) or head=='list_sd':return 'Pliki / transfer'
 if head.startswith(('zig_','start_zig')):return 'IoT / Zigbee'
 if head in ('scan_bt','scan_airtag','init_nrf24','start_jammer24'):return 'Bluetooth / NRF24'
 if head.startswith(('wifi_connect','wpasec_','capture_gateway','arp_','start_nmap')) or head=='list_hosts':return 'Net Tools / połączenie Wi-Fi'
 return 'Wi-Fi / skan / operacje / portal'
out['commands']=[{'id':f'CMD-{i:03d}','family':family(t),'template':t,'head':t.split()[0],'occurrences':rr} for i,(t,rr) in enumerate(sorted(by.items()),1)]
out['http_server']=[]
main=funcs['portal_root_handler']['s'];mt=main.data.decode('utf8')
for m in re.finditer(r'httpd_uri_t\s+(\w+)\s*=\s*\{\s*\.uri\s*=\s*"([^"]+)"\s*,\s*\.method\s*=\s*(HTTP_\w+)\s*,\s*\.handler\s*=\s*(\w+)\s*\}',mt):
 var,path,method,handler=m.groups();f=funcs[handler]
 out['http_server'].append({'method':method.removeprefix('HTTP_'),'path':path,'handler':handler,'source':'main/main.c','line':mt[:m.start()].count('\n')+1,'handler_line':f['node'].start_point.row+1,'request_operations':[{'function':cal,'arguments':[f['s'].text(x) for x in a],'line':n.start_point.row+1} for cal,a,n in f['calls'] if cal.startswith(('httpd_req_','httpd_query_'))]})
out['http_client']=[{'method':'GET','template':'{base_url}/api/list?path={url_encoded_directory}','default_base_url':'http://172.0.0.1','path':'components/janos_file_transfer/janos_file_transfer.c','line':305,'owner':'janos_file_transfer_list','arguments':['base_url','encoded_dir'],'contract':'JSON list; normalized remote dir; HTTP failures and incomplete body handled.'},{'method':'GET','template':'{base_url}/api/download?path={url_encoded_remote_path}','default_base_url':'http://172.0.0.1','path':'components/janos_file_transfer/janos_file_transfer.c','line':558,'owner':'janos_file_transfer_download','arguments':['config->base_url','encoded_path'],'headers':{'Range':'bytes={resume_from}- (only resume)'},'contract':'200 or 206; resume restart when 200; 404 missing; progress, cancellation, CRC/local writes.'}]
# Use actual function owners for HTTP evidence.
for e in out['http_client']:
 for f in funcs.values():
  if f['s'].path.relative_to(ROOT).as_posix()==e['path'] and f['node'].start_point.row+1<=e['line']<=f['node'].end_point.row+1:e['owner']=f['name']
out['other_network']=[{'protocol':'DNS UDP','address':'0.0.0.0:53','path':'main/main.c','line':38262,'owner':'dns_server_task','contract':'Captive portal DNS answers; recvfrom/sendto; separate from ten HTTP routes.'}]
out['external_services_without_local_http_endpoint']=['WiGLE: wigle_key read, wigle_upload [path|all] delegated to JanOS.','WDGWars: wdgwars_key read, wdgwars_upload [path|all] delegated to JanOS.','WPA-sec: wpasec_key read and wpasec_upload delegated to JanOS.','OTA: ota_* and wifi_connect ... ota delegated to JanOS.','http://172.0.0.1 in admin UI is navigation information; only the two API client routes are implemented locally.']
out['emulator_native_exports']=[]
for p in sorted((ROOT/'tools/ui_emulator/runtime').glob('*')):
 if p.suffix not in ('.c','.h'):continue
 text=p.read_text(encoding='utf8')
 for m in re.finditer(r'EMSCRIPTEN_KEEPALIVE\s+([^;{}]*?\b(emu_\w+)\s*\(([^)]*)\))',text):
  out['emulator_native_exports'].append({'name':m.group(2),'signature':m.group(1),'path':p.relative_to(ROOT).as_posix(),'line':text[:m.start()].count('\n')+1})
out['emulator_public_methods']=[]
for file,cls in [('bridge.mjs','BrowserDevice'),('device.mjs','SimulatedDevice')]:
 p=ROOT/'tools/ui_emulator/model'/file;text=p.read_text(encoding='utf8')
 for m in re.finditer(r'^  ([A-Za-z]\w*)\(([^\n]*?)\)\s*\{',text,re.M):
  out['emulator_public_methods'].append({'class':cls,'name':m.group(1),'parameters':m.group(2),'path':p.relative_to(ROOT).as_posix(),'line':text[:m.start()].count('\n')+1})
out['emulator_runtime_exports']={'functions':['_malloc','_free'],'runtime_methods':['ccall','cwrap','UTF8ToString','HEAPU8','HEAPU16','FS'],'path':'tools/ui_emulator/CMakeLists.txt','line':51}
out['coverage_limits']=['Inwentaryzacja statyczna wszystkich wywołań rozpoznanych TX oraz tras w 29 plikach C aplikacji/autorskich komponentów; nie test sprzętu ani dowód działania emulatora.','Konserwatywna suma zapisów nie odróżnia wszystkich gałęzi i żywotności zmiennej. Ten sam szablon może wystąpić przy wcześniejszym zapisie w tej samej funkcji.','%s, %d i dane użytkownika nie są wyliczone do wszystkich możliwych wartości. Parametry i builder_arguments w JSON zachowują wyrażenia C.','Sufiksy SubGHz, lista indeksów i lista cytowanych ścieżek mają osobne reviewed_composition. Bazowe wpisy select_networks/file_delete opisują składnik konstrukcji, nie gwarantowaną samodzielną poprawną komendę.','Odpowiedź na przychodzące tab_gps_read to %.6f,%.6f lub No GPS fix; nie komenda klienta. Brak twierdzenia o kompletności wszystkich formatów RX.','Zewnętrzne API JanOS/WiGLE/WDGWars/WPA-sec nie są zaimplementowane w tym repo; nie dopisano hipotetycznych tras.','Granice HTTP/DNS/tekstowe UART/USB plus protokół transferu ujęte; niskopoziomowe USB descriptor/control requests, sterowniki I2C/SPI/LCD i funkcje HAL nie są endpointami aplikacji.','Publiczne metody JS i eksporty wasm to wewnętrzny interfejs emulatora offline, nie endpointy sieciowe.']
out['counts']={'source_c_files':len(sources),'command_templates_including_reviewed_compositions':len(out['commands']),'command_heads':len({c['head'] for c in out['commands']}),'raw_tx_construction_occurrences':len(records),'transport_wrappers':len(sinks),'http_server_routes':len(out['http_server']),'http_client_routes':len(out['http_client']),'dns_services':1,'binary_protocol_symbols':3,'native_exports':len(out['emulator_native_exports']),'browser_device_methods':sum(x['class']=='BrowserDevice' for x in out['emulator_public_methods']),'simulated_device_methods':sum(x['class']=='SimulatedDevice' for x in out['emulator_public_methods']),'unresolved_tx_expressions':len(out['unresolved_occurrences']),'distinct_tx_call_sites':len({(x['send']['path'],x['send']['line'],x['send']['transport_call']) for x in records+framing+forwards+unresolved})}
Path(ROOT/'docs/ui-emulator/communication-endpoints.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def esc(x):return str(x).replace('|','\\|').replace('\n',' ').replace('\r','')
def src(ev):return f"`{ev.get('source',ev.get('path'))}:{ev['line']}`"
md=['# Pełny statyczny katalog komunikacji Tab5','', 'To kontrakt konsumenta **Tab5**, nie pełna dokumentacja API firmware JanOS. Inwentaryzacja z kodu, bez połączeń sieciowych i bez uruchamiania sprzętu. Dokładne argumenty, ślady konstrukcji, właściciele wywołań i SHA-256 źródeł: [communication-endpoints.json](communication-endpoints.json).','', '## Liczniki','', '| Kategoria | Liczba |','|---|---:|']
md += [f'| {k} | {v} |' for k,v in out['counts'].items()]
md += ['', '## Komendy UART / USB CDC','', 'CRLF usunięto z klucza szablonu; oryginalne bajty pozostają w JSON. Warianty printf zachowują argumenty C. Wpisy z `<...>`/`[...]` są ręcznie sprawdzonym opisem składania, a nie dosłowną komendą. Wspólny transport wybiera Grove, USB lub MBUS; nie każda komenda ma sens na każdym module.']
for fam in sorted({c['family'] for c in out['commands']}):
 md+=['',f'### {fam}','', '| ID | Szablon | Właściciel (przykład) | Źródło |','|---|---|---|---|']
 for c in out['commands']:
  if c['family']!=fam:continue
  e=c['occurrences'][0]['send'];md.append(f"| {c['id']} | `{esc(c['template'])}` | `{e['owner']}` | {src(e)} |")
md+=['','## HTTP serwera lokalnego captive portal','', '| Metoda | Trasa | Handler | Rejestracja |','|---|---|---|---|']
for e in out['http_server']:md.append(f"| {e['method']} | `{e['path']}` | `{e['handler']}` | {src(e)} |")
md+=['','## HTTP klienta transferu JanOS','', '| Metoda | Szablon URL | Właściciel | Źródło |','|---|---|---|---|']
for e in out['http_client']:md.append(f"| {e['method']} | `{e['template']}` | `{e['owner']}` | {src(e)} |")
md+=['','Baza w Tab5: `http://172.0.0.1`. `build_url` dokleja `?path=` po kodowaniu ścieżki. Pobieranie może wysłać `Range: bytes={resume_from}-`; obsługuje 200/206, błędy, anulowanie i wznowienie.','', 'DNS captive portal: **UDP 0.0.0.0:53**, `main/main.c:38262`. To osobna usługa, bez trasy HTTP.','', '## Odpowiedzi, framing i binarny transfer','', '`\\r\\n` to framing, nie endpoint. `wardrive_reply_tab_gps_read` odpowiada na żądanie modułu: `%.6f,%.6f\\r\\n` albo `No GPS fix\\r\\n` (`main/main.c:27386`).','', '| Symbol | Wartość | Znaczenie | Wysłanie |','|---|---|---|---|']
for e in out['binary_protocol']:md.append(f"| {e['name']} | `{e['value']}` | {e['meaning']} | {src(e)} |")
md+=['','## Wrappery transportu','', '| Wrapper | Indeks argumentu komendy (od 0) |','|---|---:|']
md += [f'| `{k}` | {v} |' for k,v in sorted(sinks.items())]
md+=['','Nie liczyć wrapperów jako dodatkowych komend. `forwarded_occurrences` pokazuje przekazanie parametru; `raw_construction_occurrences` zachowuje dokładne miejsca konstrukcji i TX.','', '## Techniczny interfejs emulatora','', 'Eksporty wasm są lokalnymi punktami wejścia; ich obecność nie dowodzi pokrycia wszystkich historii GUI.','', '| Eksport | Sygnatura | Źródło |','|---|---|---|']
for e in out['emulator_native_exports']:md.append(f"| `{e['name']}` | `{esc(e['signature'])}` | {src(e)} |")
for cls in ('BrowserDevice','SimulatedDevice'):
 md+=['',f'### {cls} — publiczne metody','', '| Metoda | Parametry | Źródło |','|---|---|---|']
 for e in out['emulator_public_methods']:
  if e['class']==cls:md.append(f"| `{e['name']}` | `{esc(e['parameters'])}` | {src(e)} |")
md+=['','Eksporty runtime Emscripten: `_malloc`, `_free`; `ccall`, `cwrap`, `UTF8ToString`, `HEAPU8`, `HEAPU16`, `FS` (`tools/ui_emulator/CMakeLists.txt:51`).','', '## Granice kompletności i zależności','']
md += ['- '+x for x in out['coverage_limits']]
md+=['']+['- '+x for x in out['external_services_without_local_http_endpoint']]
md+=['','## Odtworzenie','', 'Z katalogu repozytorium: `tools/ui_emulator/.venv/Scripts/python.exe tools/ui_emulator/inventory_communication.py`. Generator odczytuje źródła i zapisuje tylko oba dokumenty inwentaryzacji. Nie buduje emulatora ani firmware.','']
Path(ROOT/'docs/ui-emulator/communication-endpoints.md').write_text('\n'.join(md),encoding='utf8')
print(json.dumps(out['counts'],indent=2))


