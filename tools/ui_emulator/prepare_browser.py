from pathlib import Path
import json,re,sys,hashlib
from source_index import Source,ROOT,walk
BASE=Path(__file__).resolve().parent

def top_nodes(node):
    for c in node.named_children:
        if c.type.startswith('preproc_') and c.type not in ('preproc_def','preproc_function_def','preproc_include'):
            yield from top_nodes(c)
        else: yield c

def words(s):
    s=re.sub(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", ' ', s, flags=re.S)
    return set(re.findall(r'\b[A-Za-z_]\w*\b',s))
def name(src,n):
    while n and n.type not in ('identifier','type_identifier'):
        n=n.child_by_field_name('declarator') or (n.named_children[0] if n.type=='parenthesized_declarator' and n.named_children else None)
    return src.text(n) if n else None

def write_changed(path,text):
    if not path.exists() or path.read_text(encoding="utf-8")!=text:
        path.write_text(text,encoding="utf-8")

def generate():
    src=Source(ROOT/'main/main.c'); policy=json.loads((BASE/'slice-policy.json').read_text())
    if hashlib.sha256(src.data).hexdigest()!=policy['source_sha256']:
        raise ValueError('Production source changed: review and refresh slice policy')
    # Phase overlays widen the slice; each still names an explicit decision per function.
    for overlay in sorted(BASE.glob('slice-*-*.json')):
        policy['functions'].update(json.loads(overlay.read_text()))
    nodes=list(top_nodes(src.tree.root_node)); decls=[]; macros=[]
    for n in nodes:
        if n.type in ('declaration','type_definition','struct_specifier','enum_specifier'):
            if n.type=='declaration' and any(c.type=='function_declarator' for c in n.named_children): continue
            decls.append(n)
        elif n.type in ('preproc_def','preproc_function_def'): macros.append(n)
    names=[]
    for n in decls:
        names.append({name(src,c) for c in n.named_children if c.type in ('init_declarator','pointer_declarator','array_declarator','identifier','type_identifier')} - {None})
        if n.type=='type_definition':
            names[-1].add(name(src,n.child_by_field_name('declarator')))
        # Anonymous enums are referenced through their enumerators, not a type name.
        for child in walk(n):
            if child.type == 'enumerator':
                identifier = child.child_by_field_name('name')
                if identifier is not None: names[-1].add(src.text(identifier))
    match=re.search(rb'// Update network list\r?\n    (if \(ctx->network_list\))',src.data)
    row=src.tree.root_node.descendant_for_byte_range(match.start(1),match.start(1)+2)
    while row.type!='if_statement': row=row.parent
    runtime=''.join(p.read_text() for p in sorted((BASE/'runtime').glob('*.c')) if p.name!='runtime.c')
    used=words(runtime) | words(src.text(row)) | set(policy['roots'])
    full=set(); boundary=set(); selected=set()
    while True:
        old=(len(full),len(boundary),len(selected),len(used))
        for macro in macros:
            if src.text(macro).split()[1].split('(')[0] in used: used |= words(src.text(macro))
        for fn in sorted(used & src.functions.keys()):
            decision=policy['functions'].get(fn)
            if decision is None: raise ValueError('Missing slice decision: '+fn)
            if decision=='retain': full.add(fn); used |= words(src.function(fn))
            else: boundary.add(fn); used |= words(src.text(src.functions[fn]).split('{',1)[0])
        for i,n in enumerate(decls):
            if names[i] & used: selected.add(i); used |= words(src.text(n))
        if old==(len(full),len(boundary),len(selected),len(used)): break
    out=BASE/'generated'; out.mkdir(exist_ok=True)
    parts=['#include "host.h"']; records=[]
    contracts=json.loads((BASE/'control-contracts.json').read_text())
    if contracts['sources']['main/main.c']!=policy['source_sha256']:
        raise ValueError('Frozen control contracts do not match slice source')
    bindings=[c for c in contracts['controls'] if c['source_owner'].startswith('main/main.c::')]
    # The interposing macro in host.h passes __LINE__, which Clang resolves to the
    # closing line of a multi-line lv_obj_add_event_cb(...) call. Key the template
    # table on that end line so multi-line registrations match; single-line calls
    # have source_end_line == source_line.
    table='static const app_template_t app_templates[] = {\n'+''.join('{'+str(c.get('source_end_line',c['source_line']))+','+json.dumps(c['template_id'])+'},\n' for c in bindings)+'};'
    parts.append(table)
    def emit(n,kind):
        parts.append(f'#line {n.start_point.row+1} "main/main.c"\n'+src.text(n)+(';' if n.type in ('struct_specifier','enum_specifier') else ''))
        records.append({'kind':kind,'line':n.start_point.row+1,'end_line':n.end_point.row+1,'sha256':hashlib.sha256(src.data[n.start_byte:n.end_byte]).hexdigest()})
    for n in macros:
        if src.text(n).split()[1].split('(')[0] in used: emit(n,'macro')
    for i,n in enumerate(decls):
        if i in selected: emit(n,'declaration')
    for fn in sorted(full|boundary):
        n=src.functions[fn]; parts.append(src.data[n.start_byte:n.child_by_field_name('body').start_byte].decode().strip()+';')
    for fn in sorted(full): emit(src.functions[fn],'function:'+fn)
    for fn in sorted(boundary):
        if policy['functions'][fn]=='adapter': continue
        n=src.functions[fn]; sig=src.data[n.start_byte:n.child_by_field_name('body').start_byte].decode().strip()
        typ=src.text(n.child_by_field_name('type'))
        if '*' in sig[:re.search(r'\b'+fn+r'\s*\(',sig).start()]: typ+=' *'
        ret='' if typ=='void' else 'return ('+typ+'){0};'
        parts.append(sig+' { emu_unsupported("'+fn+'"); '+ret+' }')
    parts.append(f'#line {row.start_point.row} "main/main.c"\nstatic void app_scan_rows(tab_context_t *ctx) {{\n'+src.text(row)+'\n}')
    records.append({'kind':'scan-row-block','line':row.start_point.row+1,'end_line':row.end_point.row+1,'sha256':hashlib.sha256(src.data[row.start_byte:row.end_byte]).hexdigest()})
    parts.append('#include "application.c"')
    write_changed(out/'browser.c','\n\n'.join(parts))
    write_changed(out/'manifest.json',json.dumps({'source_sha256':hashlib.sha256(src.data).hexdigest(),'retained':sorted(full),'boundaries':{f:policy['functions'][f] for f in sorted(boundary)},'provenance':records},indent=2))
    conf=(ROOT/'lv_conf.h').read_text(); conf=re.sub(r'#define LV_MEM_SIZE[^\n]+','#define LV_MEM_SIZE (32 * 1024 * 1024U)',conf); conf=re.sub(r'#define LV_USE_SDL\s+\d+','#define LV_USE_SDL 0',conf)
    conf=conf.replace('LV_FS_DEFAULT_DRIVE_LETTER','LV_FS_DEFAULT_DRIVER_LETTER')
    write_changed(out/'lv_conf.h',conf)
    for f in ('freertos/FreeRTOS.h','freertos/task.h','freertos/semphr.h','esp_log.h','esp_heap_caps.h','esp_timer.h','nvs.h','bsp/m5stack_tab5.h'):
        p=out/'shims'/f; p.parent.mkdir(parents=True,exist_ok=True); write_changed(p,'#pragma once\n#include "host.h"\n')
    print(f'{len(full)} retained production functions; {len(boundary)} explicit boundaries')
if __name__=='__main__': generate()
