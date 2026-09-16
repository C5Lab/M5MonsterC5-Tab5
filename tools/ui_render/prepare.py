"""Extract production UI builders for a host-only LVGL rendering harness."""
from pathlib import Path
import re
import json
import tree_sitter_c
from tree_sitter import Language, Parser

ROOT = Path('/repo')
WORK = Path('/work')
parser = Parser(Language(tree_sitter_c.language()))
src = (ROOT/'main/main.c').read_bytes()
tree = parser.parse(src)


def text(node):
    return src[node.start_byte:node.end_byte].decode()


def top_nodes(node):
    for child in node.named_children:
        if child.type.startswith('preproc_') and child.type not in ('preproc_def', 'preproc_function_def', 'preproc_include'):
            yield from top_nodes(child)
        else:
            yield child


def declarator_name(node):
    while node and node.type not in ('identifier', 'type_identifier'):
        node = node.child_by_field_name('declarator') or (node.named_children[0] if node.type == 'parenthesized_declarator' and node.named_children else None)
    return text(node) if node else None


nodes = list(top_nodes(tree.root_node))
funcs = {}
decls = []
macros = []
for node in nodes:
    if node.type == 'function_definition':
        name = declarator_name(node.child_by_field_name('declarator'))
        if name:
            funcs[name] = node
    elif node.type in ('declaration', 'type_definition', 'struct_specifier', 'enum_specifier'):
        # A declaration may be a forward function prototype; emit definitions later.
        if node.type == 'declaration' and any(c.type == 'function_declarator' for c in node.named_children):
            continue
        decls.append(node)
    elif node.type in ('preproc_def', 'preproc_function_def'):
        macros.append(node)


def words(value):
    return set(re.findall(r'\b[A-Za-z_]\w*\b', value))


def declarations_names(node):
    result = set()
    for c in node.named_children:
        if c.type in ('init_declarator', 'pointer_declarator', 'array_declarator', 'identifier', 'type_identifier'):
            n = declarator_name(c)
            if n: result.add(n)
            elif c.type == 'type_identifier': result.add(text(c))
    if node.type == 'type_definition':
        for c in node.named_children:
            n = declarator_name(c)
            if n: result.add(n)
    return result


roots = {n for n in funcs if n.startswith('show_')}
roots |= {'create_status_bar', 'create_tab_bar', 'create_uart_tiles_in_container'} & funcs.keys()
full = set(roots)
stubs = set()
scan_match=re.search(rb'// Update network list\r?\n    (if \(ctx->network_list\))',src)
scan_node=tree.root_node.descendant_for_byte_range(scan_match.start(1),scan_match.start(1)+2)
while scan_node.type != 'if_statement': scan_node=scan_node.parent
scan_body=text(scan_node)
used = words((WORK/'entry.c').read_text()) | words(scan_body) | {n for n in funcs if n.startswith('subghz_host_')}
selected = set()


def keep(name):
    body = text(funcs[name].child_by_field_name('body'))
    if name.startswith('scan_filter_'): return True
    if name.startswith('subghz_host_'): return 'uart_' not in name and 'free_state' not in name
    if name in {'tab_transport_name', 'current_tab_has_sd_card', 'wardrive_config_set_defaults', 'antisurv_sens_name', 'sd_admin_state_name', 'wardrive_upload_provider_label', 'compromised_cleanup_initial_log', 'compromised_cleanup_initial_status', 'compromised_cleanup_title_for_action'} or (name.startswith('compromised_') and name.endswith('_for_kind')): return True
    if name in {'get_current_ctx', 'get_ctx_for_tab', 'get_container_for_tab', 'tab_id_for_ctx', 'get_scan_view', 'gitm_ctx', 'pcap_viewer_get_state'} or name.startswith('tab_is_'): return True
    if name.endswith(('_task', '_cb', '_callback')): return False
    if any(x in name for x in ('uart_read', 'uart_download', 'uart_send', 'transport', 'audio_', 'nvs', 'wifi_connect')): return False
    return ('lv_' in body or name.startswith(('ui_', 'style_', 'get_current_tab', 'get_tab_', 'format_', 'popup_clamp', 'ssid_', 'create_', 'hide_all_', 'theme_')))


while True:
    old = (len(full), len(stubs), len(selected), len(used))
    for name in list(full):
        used |= words(text(funcs[name]))
    for name in used & funcs.keys():
        if name in full: continue
        if keep(name): full.add(name)
        else: stubs.add(name)
    for i,node in enumerate(decls):
        if declarations_names(node) & used:
            selected.add(i)
            used |= words(text(node))
    if old == (len(full), len(stubs), len(selected), len(used)): break
stubs -= full

parts = ['#include "host.h"', '#include "scan_filter.h"']
parts += [text(n) for n in macros]
parts += [text(n) + (';' if n.type in ('struct_specifier', 'enum_specifier') else '') for i,n in enumerate(decls) if i in selected]
for name in sorted(full | stubs):
    n = funcs[name]
    signature = src[n.start_byte:n.child_by_field_name('body').start_byte].decode().strip()
    parts.append(signature + ';')
for name in sorted(full | stubs):
    n = funcs[name]
    if name in full:
        parts.append(f'#line {n.start_point.row+1} "main/main.c"\n'+text(n))
    else:
        signature = src[n.start_byte:n.child_by_field_name('body').start_byte].decode().strip()
        typ = text(n.child_by_field_name('type'))
        if '*' in signature[:re.search(r'\b'+re.escape(name)+r'\s*\(', signature).start()]: typ += ' *'
        ret = '' if typ == 'void' else 'return ('+typ+'){0};'
        parts.append(signature + ' { '+ret+' }')
parts.append('static void host_scan_rows(tab_context_t *ctx) {\n'+scan_body+'\n}')
parts.append('#include "entry.c"')
(WORK/'generated.c').write_text('\n\n'.join(parts))
(WORK/'extraction.json').write_text(json.dumps({'full':sorted(full), 'stubbed': sorted(stubs), 'roots':sorted(roots)},indent=2))
dispatch = []
cases = []
arguments = {
    'show_hidden_ssid_popup':'NULL', 'show_sae_popup':'0',
    'show_rogue_gitm_popup':'get_current_ctx(), 0', 'show_network_popup':'0',
    'show_deauth_popup':'0, 0', 'show_sd_warning_popup':'NULL',
    'show_phishing_portal_active_popup':'get_current_ctx()',
    'show_wardrive_gps_overlay':'get_current_ctx()', 'show_wardrive_upload_menu':'get_current_ctx()',
    'show_wardrive_upload_popup':'get_current_ctx(), 1', 'show_home_mgmt_overlay':'get_current_ctx()',
    'show_wardrive_home_confirm':'get_current_ctx(), "LAB-NETWORK-1"',
    'show_compromised_cleanup_popup':'get_current_ctx(), 0, 0',
    'show_compromised_file_page':'0',
    'show_evil_twin_connect_popup':'"LAB-NETWORK-1", "demo-password"',
    'show_rogue_ap_popup':'get_current_ctx()', 'show_karma2_attack_popup':'"LAB-NETWORK-1"',
    'show_scan_filter_popup':'(show_scan_page(), get_current_ctx())',
    'show_ap_radar_page':'0', 'show_bt_locator_page':'0',
}
for name in sorted(roots):
    n = funcs[name]
    sig = src[n.start_byte:n.child_by_field_name('body').start_byte].decode()
    if re.search(r'\(\s*(?:void)?\s*\)', sig) and name.startswith('show_'):
        dispatch.append(f'if (!strcmp(screen,"{name}")) {{ {name}(); return true; }}')
        cases.append(name)
    elif name in arguments:
        dispatch.append(f'if (!strcmp(screen,"{name}")) {{ {name}({arguments[name]}); return true; }}')
        cases.append(name)
(WORK/'dispatch.h').write_text('\n'.join(dispatch)+'\nreturn false;\n')
subghz_cases = re.findall(r'void (show_subghz_\w+)\(void\);', (ROOT/'main/screens/subghz_host.h').read_text())
for name in subghz_cases:
    dispatch.append(f'if (!strcmp(screen,"{name}")) {{ {name}(); return true; }}')
cases += subghz_cases
(WORK/'dispatch.h').write_text('\n'.join(dispatch)+'\nreturn false;\n')
(WORK/'cases.json').write_text(json.dumps(cases,indent=2))
for name in ('freertos/FreeRTOS.h','freertos/task.h','freertos/semphr.h','esp_log.h','esp_heap_caps.h','esp_timer.h','nvs.h','bsp/m5stack_tab5.h'):
    path=WORK/'shims'/name; path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists(): path.write_text('#pragma once\n#include "host.h"\n')
conf = (ROOT/'lv_conf.h').read_text()
conf = re.sub(r'#define LV_MEM_SIZE[^\n]+', '#define LV_MEM_SIZE (128 * 1024 * 1024U)', conf)
conf = re.sub(r'#define LV_USE_SDL\s+\d+', '#define LV_USE_SDL 0', conf)
if not (WORK/'lv_conf.h').exists() or (WORK/'lv_conf.h').read_text() != conf:
    (WORK/'lv_conf.h').write_text(conf)
print(f'{len(full)} original function bodies; {len(stubs)} inert adapters; {len(selected)} declarations')
