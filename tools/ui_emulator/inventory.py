"""Generate a source-anchored inventory; inferred contracts are not runtime proof."""
import hashlib
import json
import re
from collections import Counter
from source_index import ROOT, Source, walk
from story_contracts import STEPS, NOTES

OUT=ROOT/'docs/ui-emulator'
paths=[ROOT/'main/main.c',*sorted((ROOT/'main/screens').glob('*.c'))]
sources=[Source(p) for p in paths if not p.name.startswith('lv_font_')]
functions={}
function_nodes={}
controls=[]
boundaries=[]
globals_=[]
errors=[]

def category(name):
    if re.search(r'^(lv_|ui_|style_)',name): return 'shared_ui'
    if re.search(r'^(uart_|transport_|usbh_|usb_|esp_wifi|esp_http|esp_restart|bsp_|gpio_|i2c_|spi_|nvs_|audio_|m5_|esp_lcd|esp_codec|esp_sntp)',name): return 'device_adapter'
    if re.search(r'^(xTask|vTask|xTimer|xSemaphore|vSemaphore|esp_timer|xQueue|vQueue)',name): return 'scheduler_adapter'
    if re.search(r'^(fopen|fclose|fread|fwrite|fseek|ftell|opendir|readdir|closedir|stat|unlink|rename|mkdir|remove)$',name): return 'storage_adapter'
    return 'shared_logic_or_dependency'

for s in sources:
    rel=s.path.relative_to(ROOT).as_posix()
    for node in walk(s.tree.root_node):
        if node.type=='ERROR' or node.is_missing:
            errors.append({'file':rel,'line':node.start_point.row+1,'kind':node.type,'text':s.text(node)[:120]})
        if node.type=='declaration':
            parent=node.parent
            if parent.type=='translation_unit' or parent.type.startswith('preproc_'):
                globals_.append({'file':rel,'line':node.start_point.row+1,'declaration':s.text(node)[:1200], 'lifetime':'translation-unit; reset/persistence ownership requires review'})
    for name,node in s.functions.items():
        fid=rel+'::'+name
        function_nodes[fid]=(s,node)
        calls=[]; guards=[]; writes=[]; assignments=[]
        for n in walk(node.child_by_field_name('body')):
            if n.type=='call_expression':
                fn=s.text(n.child_by_field_name('function'))
                args=[s.text(a) for a in n.child_by_field_name('arguments').named_children if a.type!='comment']
                call={'name':fn,'args':args,'line':n.start_point.row+1}
                calls.append(call)
                if category(fn).endswith('adapter'):
                    boundaries.append({'owner':fid,'category':category(fn),**call})
                if fn in {'lv_obj_add_event_cb','lv_indev_add_event_cb'} and len(args)>=3:
                    ordinal=sum(1 for c in controls if c['owner']==fid)
                    path=[];parent=n.parent
                    while parent and parent!=node:
                        if parent.type=='if_statement':
                            consequence=parent.child_by_field_name('consequence')
                            path.append({'condition':s.text(parent.child_by_field_name('condition')),'branch':'then' if consequence.start_byte<=n.start_byte<consequence.end_byte else 'else'})
                        parent=parent.parent
                    controls.append({'id':fid+'::event-'+str(ordinal),'owner':fid,'object_expression':args[0], 'callback':args[1],'event':args[2],'user_data':args[3] if len(args)>3 else None,'line':n.start_point.row+1,'end_line':n.end_point.row+1,'construction_path':list(reversed(path)),'evidence_kind':'registration site; loops may create multiple controls'})
            elif n.type=='if_statement':
                guards.append(s.text(n.child_by_field_name('condition')))
            elif n.type=='assignment_expression':
                writes.append(s.text(n.child_by_field_name('left')))
                assignments.append({'target':s.text(n.child_by_field_name('left')),'value':s.text(n.child_by_field_name('right')),'line':n.start_point.row+1})
        functions[fid]={'name':name,'file':rel,'line':node.start_point.row+1,'calls':calls,'guards':guards,'writes':sorted(set(writes)),'assignments':assignments, 'role': 'parser' if 'parse' in name else 'screen_constructor' if name.startswith(('show_','create_')) else 'logic'}

byname={}
for fid,f in functions.items():byname.setdefault(f['name'],[]).append(fid)
def resolve(name,owner):
    local=owner.split('::')[0]+'::'+name
    if local in functions:return local
    ids=byname.get(name,[])
    return ids[0] if len(ids)==1 else None

def callback_candidates(expression,owner,seen=None):
    seen=set() if seen is None else seen
    key=(expression,owner)
    if key in seen:return set()
    seen.add(key)
    direct={fid for word in re.findall(r'\b\w+\b',expression) if (fid:=resolve(word,owner))}
    if direct:return direct
    s,node=function_nodes[owner]
    # Reviewed alias: create_attack_action_bar builds tiles from exactly these
    # two callback parameters, then passes t->cb through create_small_tile.
    if expression=='t->cb' and functions[owner]['name']=='create_attack_action_bar':
        body=s.text(node)
        assert 'const bar_tile_t *t = &tiles[i]' in body and 't->cb' in body
        return callback_candidates('callback',owner,seen)|callback_candidates('karma_callback',owner,seen)
    # Callback supplied as a helper parameter: inspect every source call site.
    params=next((n for n in walk(node.child_by_field_name('declarator')) if n.type=='parameter_list'),None)
    if params:
        for index,param in enumerate(params.named_children):
            if any(n.type=='identifier' and s.text(n)==expression for n in walk(param)):
                for caller,f in functions.items():
                    for call in f['calls']:
                        if resolve(call['name'],caller)==owner and len(call['args'])>index:
                            direct|=callback_candidates(call['args'][index],caller,seen)
    # Local table of callback pointers: retain all possible functions and the
    # original expression so the runtime choice is not represented as fixed.
    array=re.match(r'(\w+)\[',expression)
    if array:
        for n in walk(node):
            if n.type=='init_declarator' and re.match(r'\b'+array[1]+r'\b',s.text(n.child_by_field_name('declarator'))):
                value=n.child_by_field_name('value')
                if value:direct|=callback_candidates(s.text(value),owner,seen)
    return direct

for c in controls:
    target=resolve(c['callback'],c['owner'])
    candidates=callback_candidates(c['callback'],c['owner'])
    reached=set();pending=list(candidates)
    while pending:
        fid=pending.pop()
        if fid in reached:continue
        reached.add(fid)
        for call in functions[fid]['calls']:
            callee=resolve(call['name'],fid)
            if callee and callee not in reached:pending.append(callee)
    c['callback_function']=target
    c['callback_candidates']=sorted(candidates)
    c['reachable_functions']=sorted(reached)
    c['device_and_scheduler_dependencies']=sorted({call['name'] for fid in reached for call in functions[fid]['calls'] if category(call['name']).endswith('adapter')})
    c['contract']={'precondition_evidence':sorted(candidates),'construction_guard_evidence':c['owner'],'action':c['callback']+'('+c['event']+')','expected_change_evidence':sorted(reached),'data_owner_evidence':sorted(reached),'status':'static contract; guards, writes and calls in referenced function records; runtime validation belongs to later phases'}
    c['contract']['callback_guards']={fid:functions[fid]['guards'] for fid in sorted(candidates)}
    c['contract']['callback_assignments']={fid:functions[fid]['assignments'] for fid in sorted(candidates)}
    c['contract']['callback_calls']={fid:functions[fid]['calls'] for fid in sorted(candidates)}
    c['contract']['instance_key_rule']='Bind the concrete object plus its current user_data at construction; index user_data must resolve to the scenario entity ID. Do not use source ordinal as persistent identity.'

# Import already anchored flow routes rather than retyping the diagram definitions.
import sys
sys.path.insert(0,str(ROOT/'tools/ui_flow'))
from generate_ui_flow import DIAGRAMS
routes=[{'id':d.slug+'::'+str(i),'diagram':d.slug,'source':e.source,'target':e.target,'label':e.label,'kind':e.kind,'file':e.file,'function':e.function,'anchor':e.anchor} for d in DIAGRAMS for i,e in enumerate(d.edges)]
topic_targets={
 'bluetooth-scan':['show_bt_scan_page','show_bt_menu_page'],
 'global-attacks':['show_global_attacks_page'],
 'karma-monsters-c5':['show_karma_page'],
 'network-observer-karma':['show_observer_page'],
 'scan-attacks':['show_scan_page'],
 'settings-compromised-data':['show_settings_page'],
 'wpa-sec-upload':['wpasec'],
 'middle-man':['gitm','arp']}
stories=[]
for p in sorted((ROOT/'docs/tab.stories/src').glob('*.html')):
    entries=re.findall(r'\{\s*file:\s*"([^"]+)",\s*text:\s*"([^"]+)"',p.read_text(encoding='utf-8'))
    targets=topic_targets[p.stem]
    candidates=[fid for fid,f in functions.items() if any(t==f['name'] or (t in ('wpasec','gitm','arp') and t in f['name']) for t in targets)]
    steps=[]
    if entries:
        assert len(STEPS[p.stem])==len(entries),p
        for i,(image,caption) in enumerate(entries):
            target='main/main.c::'+STEPS[p.stem][i]
            assert target in functions,target
            steps.append({'id':p.stem+'::'+str(i+1),'image':image,'instruction':caption,'function':target,'status':'source-mapped; scenario execution not implemented'})
    stories.append({'id':p.stem,'file':p.relative_to(ROOT).as_posix(),'candidate_functions':candidates,'steps':steps,'notes':NOTES.get(p.stem,'Conceptual sections: Introduction, ARP MITM, GITM and Rogue GITM. Map to show_arp_poison_page, show_gitm_page and show_rogue_gitm_popup; no numbered-step array.'),'status':'source-mapped with branch notes' if entries else 'narrative mapped'})

summary={'source_files':len(sources),'functions':len(functions),'event_registration_sites':len(controls),'resolved_callback_sites':sum(bool(c['callback_candidates']) for c in controls),'boundary_call_sites':len(boundaries),'global_declarations':len(globals_),'routes':len(routes),'story_steps':sum(len(s['steps']) for s in stories),'syntax_diagnostics':len(errors)}
data={'schema_version':1,'status':'structural-inventory-not-semantic-signoff','summary':summary,'sources':{s.path.relative_to(ROOT).as_posix():hashlib.sha256(s.data).hexdigest() for s in sources},'controls':controls,'functions':functions,'boundaries':boundaries,'global_declarations':globals_,'routes':routes,'stories':stories,'syntax_diagnostics':errors,'limitations':['Registration sites are not unique runtime widget counts.','Function-pointer aliases, callback arrays and helper-created controls need semantic resolution.','Boundary categories are name-based suggestions, not an approved runtime stub allowlist.','Source guards and writes do not alone establish complete control contracts.']}
(ROOT/'tools/ui_emulator/coverage.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
(OUT/'inventory-summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
