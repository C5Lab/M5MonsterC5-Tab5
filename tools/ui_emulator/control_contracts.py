"""Freeze binding identities and define source-preserving UI contracts.

This produces an implementation specification, not browser event bindings.
Enrollment is explicit; normal runs fail on identity drift or ambiguous sites.
"""
import argparse
import hashlib
import json
import re
from urllib.parse import quote
from source_index import ROOT

def normalized(value):
    # Do not normalize inside string literals: action labels can be meaningful.
    return re.sub(r'"(?:\\.|[^"\\])*"|\s+',lambda m: m[0] if m[0].startswith('"') else ' ',value or '').strip()

def identity_key(c):
    fields=[normalized(c[k]) for k in ['owner','object_expression','callback','event','user_data']]
    fields.append([(normalized(p['condition']),p['branch']) for p in c.get('construction_path',[])])
    return json.dumps(fields,separators=(',',':'))

def reconcile_identities(controls,registry,enroll=False):
    keys=[identity_key(c) for c in controls]
    if len(keys)!=len(set(keys)):
        raise ValueError('Ambiguous registration identity; add an explicit reviewed source discriminator')
    result=dict(registry)
    for c,key in zip(controls,keys):
        if key not in result:
            if not enroll:raise ValueError('Unregistered control identity: '+key)
            stem=c['owner'].split('::')[-1]+'.'+c['object_expression']+'.'+c['event'].removeprefix('LV_EVENT_').lower()
            stem=re.sub(r'[^a-zA-Z0-9_.-]+','-',stem).strip('-')
            result[key]='ui.'+stem+'.'+hashlib.sha256(key.encode()).hexdigest()[:12]
    missing=set(result)-set(keys)
    if missing:raise ValueError('Retired/changed identities need explicit migration review: '+str(len(missing)))
    if len(set(result.values()))!=len(result):raise ValueError('Duplicate permanent ID')
    return result

def instance_id(template,screen,module,entity,slot):
    values=[template,screen,module,entity,slot]
    if not all(isinstance(x,str) and x.strip() for x in values):
        raise ValueError('Explicit template, screen, module, entity and semantic slot are required')
    return '/'.join(quote(x,safe='') for x in values)

def build_contract(c,functions):
    deferred='subghz' in c['owner'].lower()
    handlers=[]
    for fid in c['callback_candidates']:
        f=functions[fid]
        handlers.append({'function':fid,'guards':f['guards'],'assignments':f.get('assignments',[]),'calls':f['calls']})
    if not handlers:raise ValueError('No resolved handler: '+c['owner'])
    event=c['event']
    lifecycle=('Destroy only this UI binding and its owned timers/user data; preserve original delete callback order.'
               if event=='LV_EVENT_DELETE' else
               'Dispatch only while the binding and its screen generation are alive; queued device results carry their own operation generation.')
    return {'scope':'deferred_subghz' if deferred else 'current',
            'preconditions':['The concrete object/input device belongs to the live UI session.',
                             'The event matches the original registration filter; LV_EVENT_ALL retains its internal dispatch.',
                             'Retain original constructor visibility/enable conditions and handler guard order.',
                             'SubGHz actions are not exposed in this milestone.'],
            'event':event,'callback_expression':c['callback'],'user_data_expression':c['user_data'],
            'construction_path':c.get('construction_path',[]),
            'handler_policy':'retain_original_body_and_branch_order','allow_noop_substitution':False,
            'handlers':handlers,'reachable_function_evidence':c['reachable_functions'],
            'expected_outcome':'Execute the selected original handler including branch guards, state writes, navigation and scheduled work; device calls receive responses from their explicit adapter contracts.',
            'state_owner':'originating_module_context_and_ui_session',
            'state_resolution':'Preserve original get_current_ctx/get_ctx_for_tab and global-variable semantics. Module-scoped state is isolated; existing shared globals belong to the UI session, not automatically duplicated per device.',
            'lifecycle':lifecycle,
            'instance_binding':{'fields':['template_id','screen_id','module_id','entity_id','slot'],
                                'fixed_widget_entity':'static','repeated_widget_entity':'canonical scenario ID resolved from the current index at binding time',
                                'helper_slot':'explicit action/role supplied by the caller; no pointer, source line or allocation-order ordinal'},
            'validation_phase':'browser implementation must test outcomes; Phase 1 specifies them without claiming runtime coverage'}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--enroll',action='store_true');args=ap.parse_args()
    x=json.loads((ROOT/'tools/ui_emulator/coverage.json').read_text(encoding='utf-8'))
    path=ROOT/'tools/ui_emulator/control-identities.json'
    saved=json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    old=saved.get('identities',{})
    if not args.enroll and saved.get('reviewed_sources')!=x['sources']:
        raise ValueError('Source changed since contract review; review then explicitly enroll the new snapshot')
    registry=reconcile_identities(x['controls'],old,args.enroll)
    if args.enroll:
        path.write_text(json.dumps({'version':1,'reviewed_sources':x['sources'],'identities':registry},indent=2)+'\n',encoding='utf-8')
    result=[]
    for c in x['controls']:
        result.append({'template_id':registry[identity_key(c)],'source_registration':c['id'],
                       'source_line':c['line'],'source_end_line':c.get('end_line',c['line']),'source_owner':c['owner'],
                       'contract':build_contract(c,x['functions'])})
    out={'status':'phase-1-source-preservation-contracts','sources':x['sources'],'controls':result,
         'scope_rule':'Defer SubGHz constructors and filter SubGHz dashboard actions; shared helpers remain available for other actions.'}
    (ROOT/'tools/ui_emulator/control-contracts.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
    print(f'{len(result)} permanent templates: '+str(sum(c['contract']['scope']=='current' for c in result))+' current, '+str(sum(c['contract']['scope']!='current' for c in result))+' deferred')

if __name__=='__main__':main()
