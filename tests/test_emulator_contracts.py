"""Meaningful stability and fail-closed checks for the Phase 1 contract tooling."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ui_emulator'))
from control_contracts import identity_key, instance_id, reconcile_identities, build_contract

class Contracts(unittest.TestCase):
    def setUp(self):
        self.control={'id':'old::event-0','owner':'main/main.c::show_scan_page',
                      'object_expression':'back_btn','callback':'back_cb',
                      'event':'LV_EVENT_CLICKED','user_data':'NULL','line':100,
                      'callback_candidates':['main/main.c::back_cb'],
                      'reachable_functions':['main/main.c::back_cb']}

    def test_ids_survive_line_changes_and_reordering(self):
        a=self.control;b=copy.deepcopy(a);b['object_expression']='scan_btn';b['callback']='scan_cb'
        registry=reconcile_identities([a,b],{},enroll=True)
        moved=copy.deepcopy(a);moved.update(line=999,id='old::event-17')
        self.assertEqual(reconcile_identities([b,moved],registry),registry)
        self.assertEqual(identity_key(a),identity_key(moved))

    def test_new_or_ambiguous_binding_requires_explicit_review(self):
        a=self.control;registry=reconcile_identities([a],{},enroll=True)
        changed=copy.deepcopy(a);changed['callback']='other_cb'
        with self.assertRaises(ValueError):reconcile_identities([changed],registry)
        with self.assertRaises(ValueError):reconcile_identities([a,a],{},enroll=True)

    def test_action_label_whitespace_is_not_discarded(self):
        a=copy.deepcopy(self.control);b=copy.deepcopy(a)
        a['user_data']='"two  spaces"';b['user_data']='"two spaces"'
        self.assertNotEqual(identity_key(a),identity_key(b))

    def test_different_constructor_branches_have_different_ids(self):
        a=copy.deepcopy(self.control);b=copy.deepcopy(a)
        a['construction_path']=[{'condition':'(password_known)','branch':'then'}]
        b['construction_path']=[{'condition':'(password_known)','branch':'else'}]
        self.assertNotEqual(identity_key(a),identity_key(b))

    def test_instances_use_entities_and_semantic_slots_not_row_positions(self):
        a=instance_id('ui.scan.row','scan','grove','net-1','select')
        self.assertEqual(a,instance_id('ui.scan.row','scan','grove','net-1','select'))
        self.assertNotEqual(a,instance_id('ui.scan.row','scan','mbus','net-1','select'))
        self.assertNotEqual(a,instance_id('ui.scan.row','scan','grove','net-2','select'))
        with self.assertRaises(ValueError):instance_id('ui.scan.row','scan','grove','','select')

    def test_contract_keeps_guards_and_values_and_forbids_silent_stubs(self):
        functions={'main/main.c::back_cb':{'guards':['(ctx == NULL)'],'assignments':[{'target':'ctx->scan_active','value':'false','line':12}], 'calls':[{'name':'uart_send_command_for_tab','args':['"stop"'],'line':13}]}}
        contract=build_contract(self.control,functions)
        self.assertEqual(contract['handler_policy'],'retain_original_body_and_branch_order')
        self.assertEqual(contract['handlers'][0]['guards'],['(ctx == NULL)'])
        self.assertEqual(contract['handlers'][0]['assignments'][0]['value'],'false')
        self.assertFalse(contract['allow_noop_substitution'])
        self.assertEqual(contract['state_owner'],'originating_module_context_and_ui_session')

if __name__=='__main__':unittest.main()
