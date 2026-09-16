"""Reproducible UI scope, separate from emulator runtime/build artifacts."""
import ast
import hashlib
import json
import re
from collections import Counter
from pathlib import Path
from source_index import ROOT, Source, walk
import audit_coverage as audit

OUT=ROOT/'docs/ui-emulator'
coverage=json.loads((ROOT/'tools/ui_emulator/coverage.json').read_text())
manifest=json.loads((ROOT/'tools/ui_emulator/generated/manifest.json').read_text())
retained=set(manifest['retained']);boundaries=manifest['boundaries']
result=audit.compute()
contracts={c['source_registration']:c for c in json.loads((ROOT/'tools/ui_emulator/control-contracts.json').read_text())['controls']}
drift=[p for p,h in coverage['sources'].items() if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=h]
assert not drift, f'Refresh structural inventory before scope generation: {drift}'

def status(name):return 'retained' if name in retained else boundaries.get(name,'not-wired')
def strings(expr):
    return [ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"',expr)]
def cell(value):return str(value).replace('|','\\|').replace('\n',' / ').replace('\r','')
def source_link(file,line):return f'[{file}:{line}](../../{file}#L{line})'
def guards(node,owner):
    found=[];parent=node.parent
    while parent and parent!=owner:
        if parent.type=='if_statement':found.append(parent.child_by_field_name('condition'))
        parent=parent.parent
    return list(reversed(found))

sources={p:Source(ROOT/p) for p in coverage['sources']}
runtime_sources={p.relative_to(ROOT).as_posix():Source(p,mask_embedded_js=True) for p in sorted((ROOT/'tools/ui_emulator/runtime').glob('*.c'))}
tiles=[];seen=set();adapter_controls=[]
for file,src in {**sources,**runtime_sources}.items():
    runtime=file in runtime_sources
    for owner,node in src.functions.items():
        for n in walk(node):
            if n.type!='call_expression':continue
            fn=src.text(n.child_by_field_name('function'))
            args=[src.text(a) for a in n.child_by_field_name('arguments').named_children if a.type!='comment']
            if fn in ('create_tile','subghz_create_tile'):
                labels=strings(args[2]);action=args[5] if fn=='create_tile' else args[4]
                key=(owner,action)
                if key in seen:continue
                seen.add(key)
                tiles.append({'owner':owner,'labels':labels,'label_expression':args[2],
                    'action':action,'callback':args[4], 'file':file,'line':n.start_point.row+1,
                    'kind':'emulator-only' if runtime else 'firmware-tile',
                    'guards':[src.text(g) for g in guards(n,node)]})
            if runtime and fn=='app_bind_adapter' and len(args)>=6:
                adapter_controls.append({'owner':owner,'object':args[0],'callback':args[1],
                    'event':args[2],'user_data':args[3],'id_expression':args[4] if len(args)==5 else args[-1],
                    'file':file,'line':n.start_point.row+1})
            # Current app_bind_adapter takes five arguments.
            if runtime and fn=='app_bind_adapter' and len(args)==5:
                adapter_controls.append({'owner':owner,'object':args[0],'callback':args[1],
                    'event':args[2],'user_data':args[3],'id_expression':args[4],
                    'file':file,'line':n.start_point.row+1})

src=sources['main/main.c'];body=src.function('create_attack_action_bar')
base=src.functions['create_attack_action_bar'].start_point.row+1
bar={}
for offset,line in enumerate(body.splitlines()):
    if 'tiles[count++]' not in line:continue
    labels=strings(line)
    if len(labels)!=2:continue
    label,action=labels
    if action in bar:bar[action]['construction_lines'].append(base+offset);continue
    bar[action]={'owner':'create_attack_action_bar','labels':[label],'action':action,
        'callback':'callback / karma_callback','file':'main/main.c','line':base+offset,
        'construction_lines':[base+offset],'kind':'shared-action-tile',
        'guards':['Red Team, rodzaj paska Scan/Observer i przekazany karma_callback; patrz źródło']}
tiles+=list(bar.values())

controls=[]
for c in coverage['controls']:
    names=audit._handler_names(c,contracts)
    disposition=audit._disposition(names,retained,boundaries)
    endpoints=sorted(fid for fid in c.get('reachable_functions',[]) if fid.split('::')[-1].startswith('show_'))
    controls.append({k:c.get(k) for k in ['id','owner','object_expression','callback','event','user_data','line','construction_path','callback_candidates']}|
        {'disposition':disposition,'reachable_ui_functions':endpoints})
assert len(controls)==452
assert Counter(c['disposition'] for c in controls)==Counter({k:result['summary'][k] for k in ['retained','adapter','unsupported','not-wired']})
ui_endpoints=[{'id':fid,'name':f['name'],'file':f['file'],'line':f['line'],'disposition':status(f['name']),
    'controls':[c['id'] for c in controls if c['owner']==fid]}
    for fid,f in coverage['functions'].items() if f['name'].startswith('show_')]
constructors=[{'id':fid,'name':f['name'],'file':f['file'],'line':f['line'],'disposition':status(f['name'])}
    for fid,f in coverage['functions'].items() if f['role']=='screen_constructor' and not f['name'].startswith('show_')]
html=(ROOT/'tools/ui_emulator/web/index.html').read_text()
shell=[{'tag':m[0],'id':m[1]} for m in re.findall(r'<(button|select|input|canvas)[^>]*\bid="([^"]+)"',html)]
payload={'source_hashes_verified':coverage['sources'],'audit_summary':result['summary'],
    'counts':{'tile_definitions':len(tiles),'show_functions':len(ui_endpoints),'other_constructor_functions':len(constructors),
              'firmware_control_registrations':len(controls),'reviewed_navigation_edges':len(coverage['routes']),
              'emulator_adapter_registrations':len(adapter_controls),'shell_controls':len(shell)},
    'tiles':tiles,'ui_endpoints':ui_endpoints,'other_constructors':constructors,'controls':controls,
    'emulator_adapter_controls':adapter_controls,'shell_controls':shell,'navigation_routes':coverage['routes'],
    'deferred_controls':result['open_items'],
    'limitations':['Rejestracje i definicje, nie liczba przycisków po rozwinięciu dynamicznych list.',
      'Status funkcji/kontrolki nie dowodzi kompletności całego flow ani guided story.',
      '110 tras jest istniejącym ręcznie przeglądanym grafem; pełne wywołania/gałęzie są w coverage.json.',
      'SubGHz jest wymieniony dla pełnego scope, ale pozostaje wyłączony decyzją użytkownika.']}
(OUT/'ui-scope-inventory.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

lines=['# Pełny rejestr UI — źródła, ekrany, kontrolki','',
    'Wygenerowano przez `tools/ui_emulator/inventory_scope.py`. Zgodność hashy wszystkich 19 plików firmware potwierdzona.',
    '',f'**Liczby:** {len(tiles)} definicji kafli, {len(ui_endpoints)} funkcji `show_*`, {len(constructors)} pozostałych konstruktorów `create_*`, '
    f'{len(controls)} rejestracji firmware, {len(adapter_controls)} rejestracji dodatkowych adapterów emulatora, {len(shell)} kontrolek obudowy WWW.',
    '', 'Kafle i wiersze tworzone w pętlach są szablonami, nie zamkniętą liczbą instancji. '
    'Lista urządzeń, sieci, klientów, plików, częstotliwości i sensorów może tworzyć wiele elementów.',
    '', '**Status:** `retained` = kod zachowany; `adapter` = granica symulowana; `unsupported` = jawnie nieobsługiwane; '
    '`not-wired` = poza kompilowaną ścieżką. Sam status funkcji nie oznacza, że story jest gotowe.',
    '', '## Wszystkie definicje kafli','', '| # | Grupa / konstruktor | Etykieta | Akcja / callback | Warunek | Źródło |', '|---|---|---|---|---|---|']
for i,t in enumerate(tiles,1):
    lines.append(f'| T{i:03} | `{t["owner"]}` | {cell(" / ".join(t["labels"]))} | `{cell(t["action"])}` | {cell("; ".join(t["guards"])) or "—"} | {source_link(t["file"],t["line"])} |')
lines+=['','## Endpointy UI: wszystkie funkcje show_*','', '| Funkcja | Stan kompilacji | Kontrolki bezpośrednio w funkcji | Źródło |','|---|---|---|---|']
for e in ui_endpoints:lines.append(f'| `{e["name"]}` | {e["disposition"]} | {len(e["controls"])} | {source_link(e["file"],e["line"])} |')
lines+=['','## Pozostałe konstruktory create_*','', 'Pomocnicze konstruktory też są ujęte, ale nie należy liczyć ich automatycznie jako osobnych ekranów.', '', '| Funkcja | Stan | Źródło |','|---|---|---|']
for e in constructors:lines.append(f'| `{e["name"]}` | {e["disposition"]} | {source_link(e["file"],e["line"])} |')
lines+=['','## Wszystkie 452 punkty rejestracji kontrolek firmware','',
    'Identyfikator = plik + konstruktor + event-N. `user_data` rozróżnia akcje szablonów. '
    'Pełne warunki konstrukcji i osiągalne endpointy UI są w [JSON](ui-scope-inventory.json).', '',
    '| ID | Obiekt / dane | Callback | Event | Stan | Źródło |','|---|---|---|---|---|---|']
for c in controls:
    file=c['owner'].split('::')[0]
    lines.append(f'| `{cell(c["id"])}` | `{cell(c["object_expression"])} / {cell(c["user_data"])}` | `{cell(c["callback"])}` | {cell(c["event"])} | {c["disposition"]} | {source_link(file,c["line"])} |')
lines+=['','## Dodatkowe kontrolki adapterów emulatora','',
    'Nie są doliczane do 452 punktów firmware. Tu są między innymi generowanie fixture PCAP, lokalne pickery i symulowane moduły.', '',
    '| ID / wyrażenie | Callback | Konstruktor | Źródło |','|---|---|---|---|']
for c in adapter_controls:lines.append(f'| `{cell(c["id_expression"])}` | `{c["callback"]}` | `{c["owner"]}` | {source_link(c["file"],c["line"])} |')
lines+=['','## Kontrolki strony WWW','', '| ID | Element |','|---|---|']
for c in shell:lines.append(f'| `{c["id"]}` | {c["tag"]} |')
lines+=['','## Zidentyfikowane trasy nawigacji','',
    'Istniejący graf ma 110 przeglądanych krawędzi; to nie jest deklaracja kompletności wszystkich kombinacji nawigacji. '
    'Każda kontrolka i jej osiągalne funkcje pozostają osobno w JSON.', '',
    '| Graf | Z → do | Wyzwalacz | Funkcja |','|---|---|---|---|']
for r in coverage['routes']:lines.append(f'| {r["diagram"]} | {r["source"]} → {r["target"]} | {cell(r["label"])} | `{r["function"]}` |')
(OUT/'UI_Endpoints_and_Controls.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(json.dumps(payload['counts'],ensure_ascii=False));print('Source drift: 0; coverage control conservation: PASS')
