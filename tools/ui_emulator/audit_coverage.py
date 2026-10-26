"""Phase 4.7 coverage-ledger audit.

Static, deterministic closeout for Phase 4. It reconciles every control and
back route in the coverage ledger (``coverage.json``) against what the last
emulator build actually compiled (``generated/manifest.json``) and the
per-control preservation contracts (``control-contracts.json``).

Each control resolves to one disposition:

* ``retained``  - the firmware handler is compiled into the emulator.
* ``adapter``   - the handler is a boundary served by a runtime adapter.
* ``unsupported`` - the handler is a boundary deliberately left inert.
* ``replaced`` - the firmware registration was omitted and an emulator-owned
  workflow exists; its individual actions still require behavioral review.
* ``not-wired`` - the handler was sliced out; the control is inert in the
  emulator and MUST be catalogued as an open item.

The audit does not decide policy. Every ``not-wired`` control and every
``not-wired`` back route is matched against the category rules below so that
the demo can name each open item and its reason. A control that matches no
rule is reported ``UNCATEGORIZED`` and fails the gate: it forces a human
triage decision instead of silently shipping a dead control.

Run directly to print the summary; pass ``--write-baseline`` to (re)generate
``docs/ui-emulator/phase4-coverage-baseline.json``.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from inventory_inputs import inventory_source_paths

ROOT = Path(__file__).resolve().parents[2]
EMU = ROOT / 'tools/ui_emulator'
COVERAGE = EMU / 'coverage.json'
MANIFEST = EMU / 'generated/manifest.json'
CONTRACTS = EMU / 'control-contracts.json'
BASELINE = ROOT / 'docs/ui-emulator/phase4-coverage-baseline.json'

# Ordered category rules for controls whose firmware handler is sliced out.
# A rule matches when the owner's source file contains ``file`` (when given)
# and the owner function name is in ``owners`` (when given). The first match
# wins. ``open`` distinguishes deliberately-deferred scope (False) from
# dialogs inside otherwise-covered flows that are the next candidates to wire
# (True); both are permitted by the gate but reported separately.
CATEGORY_RULES = [
    # Boot and sleep chrome are tied to the physical display/power manager.
    {'category': 'device-chrome', 'owners': {
        'app_main', 'screen_timeout_timer_cb', 'show_splash_screen'},
     'open': False,
     'reason': 'Boot splash and physical display sleep are not simulated.'},

    # ESP-modem serial tool drives real hardware.
    {'category': 'esp-modem-not-simulated', 'owners': {'show_esp_modem_page'},
     'open': False,
     'reason': 'ESP modem serial tool drives real hardware; not simulated.'},

    # WiGLE upload needs a real Wi-Fi join and credential entry, out of scope
    # for the offline upload-outcome simulation wired in Phase 4.2.
    {'category': 'wardrive-wigle-wifi', 'owners': {
        'wardrive_wigle_create_credentials_prompt', 'wardrive_wigle_ensure_wifi_connected'},
     'open': False,
     'reason': 'WiGLE credential entry and real Wi-Fi join are out of the offline upload scope.'},

    # --- Open candidates: reachable dialogs inside otherwise-covered flows.
    # Permitted by the gate but flagged as the next controls to wire.
    {'category': 'dialog-open-candidate', 'owners': {
        'show_observer_exit_confirm', 'show_wardrive_home_confirm'},
     'open': True,
     'reason': 'Exit/leave confirm dialog inside a covered flow; candidate to wire next.'},

    {'category': 'settings-open-candidate', 'owners': {
        'show_time_popup', 'time_add_switch_row', 'show_version_mismatch_popup',
        'ota_create_info_slot_card'},
     'open': True,
     'reason': 'Settings/clock/version dialog inside a covered flow; candidate to wire next.'},

    {'category': 'utility-dialog-open-candidate', 'owners': {
        'show_no_board_popup', 'show_sd_warning_popup', 'show_ft_baud_popup'},
     'open': True,
     'reason': 'Shared utility dialog reachable from covered flows; candidate to wire next.'},
]

# These firmware constructors are replaced by emulator-owned, offline native
# screens. The original registrations are absent by design; the named tests
# verify a workflow, not necessarily every action of each original control.
REPLACEMENT_RULES = [
    {'owners': {'ota_release_list_add_line', 'ota_rf_confirm_install'},
     'runtime': 'tools/ui_emulator/runtime/settings_ota.c',
     'test': 'tests/test_emulator_ota_variant.py'},
    {'owners': {'hs_crack_show_popup', 'wpa_auditor_button_data',
                'wpa_auditor_apply_saved_config', 'wpa_auditor_catalog_render_locked',
                'wpa_auditor_show_loading_page', 'wpa_auditor_batch_render_card',
                'show_wpa_psk_auditor_page'},
     'runtime': 'tools/ui_emulator/runtime/wpa_auditor.c',
     'test': 'tests/test_emulator_wpa_auditor.py'},
    {'owners': {'karma2_fetch_probes', 'show_karma2_attack_popup',
                'show_karma2_html_popup', 'show_phishing_portal_popup',
                'show_phishing_portal_active_popup'},
     'runtime': 'tools/ui_emulator/runtime/portal_flows.c',
     'test': 'tests/test_emulator_portal_flows.py'},
    {'owners': {'rogue_gitm_pass_row', 'show_rogue_gitm_popup'},
     'runtime': 'tools/ui_emulator/runtime/rogue_gitm.c',
     'test': 'tests/test_emulator_rogue_gitm.py'},
]


def _short(name):
    return name.split('::')[-1] if name else name


def _load():
    return (json.loads(COVERAGE.read_text(encoding='utf-8')),
            json.loads(MANIFEST.read_text(encoding='utf-8')),
            json.loads(CONTRACTS.read_text(encoding='utf-8')))


def _check_inventory_freshness(coverage, manifest, contracts):
    """Reject source drift hidden by an old inventory or generated manifest."""
    sources = coverage.get('sources')
    if not isinstance(sources, dict) or 'main/main.c' not in sources:
        raise ValueError('Coverage inventory has no main/main.c source hash')
    current_paths = {path.relative_to(ROOT).as_posix()
                     for path in inventory_source_paths(ROOT)}
    recorded_paths = set(sources)
    if current_paths != recorded_paths:
        missing = sorted(current_paths - recorded_paths)
        removed = sorted(recorded_paths - current_paths)
        raise ValueError('Stale coverage source set: '
                         f'missing {missing}; removed {removed}; regenerate inventory')
    for path, recorded in sources.items():
        try:
            # The inventory hashes LF bytes; Windows Git checkouts may use CRLF.
            data = (ROOT / path).read_bytes().replace(b'\r\n', b'\n')
        except OSError as error:
            raise ValueError(f'Missing coverage source: {path}') from error
        if hashlib.sha256(data).hexdigest() != recorded:
            raise ValueError(f'Stale coverage source hash: {path}; regenerate inventory')
    if contracts.get('sources') != sources:
        raise ValueError('Stale control contracts source hashes; regenerate contracts')
    if manifest.get('source_sha256') != sources['main/main.c']:
        raise ValueError('Stale generated manifest source hash; rebuild emulator')


def _handler_names(control, contracts_by_reg):
    """Resolve source-qualified firmware handler names for a control."""
    contract = contracts_by_reg.get(control['id'])
    names = []
    if contract:
        for handler in contract['contract'].get('handlers', []):
            fn = handler.get('function')
            if fn:
                names.append(fn)
    if not names and control.get('callback'):
        names.append(control['owner'].split('::')[0] + '::' + _short(control['callback']))
    return names


def _compiled_sources(source_paths):
    """Production sources linked directly or included into the Wasm target."""
    cmake = (EMU / 'CMakeLists.txt').read_text(encoding='utf-8')
    sources = set(re.findall(r'"\$\{REPO_ROOT\}/([^"\n]+\.c)"', cmake))
    for pattern in re.findall(r'file\(GLOB\s+\w+\s+"\$\{REPO_ROOT\}/([^"\n]+\.c)"', cmake):
        sources.update(path.relative_to(ROOT).as_posix() for path in ROOT.glob(pattern))
    browser = (EMU / 'generated/browser.c').read_text(encoding='utf-8')
    for path in source_paths:
        if path.endswith('.inc') and f'#include "{Path(path).name}"' in browser:
            sources.add(path)
    return sources


def _disposition(names, retained, boundaries, compiled_sources):
    if not names:
        return 'not-wired'
    seen = {('retained' if n.split('::')[0] in compiled_sources or _short(n) in retained
             else boundaries.get(_short(n), 'not-wired')) for n in names}
    for kind in ('retained', 'adapter', 'unsupported'):
        if kind in seen:
            return kind
    return 'not-wired'


def _owner_adapter_disposition(control, disposition, boundaries):
    """Resolve generic callback factories through their compiled adapter."""
    owner_name = _short(control['owner'])
    if (owner_name == 'wardrive_autoup_make_btn'
            and _short(control.get('callback')) == 'cb'
            and boundaries.get(owner_name) == 'adapter'):
        return 'adapter'
    return disposition


def _categorize(owner_file, owner_name):
    for rule in CATEGORY_RULES:
        if rule.get('file') and rule['file'] not in owner_file:
            continue
        if rule.get('owners') is not None and owner_name not in rule['owners']:
            continue
        if rule.get('file') is None and rule.get('owners') is None:
            continue
        return rule
    return None


def _replacement(owner_name):
    for rule in REPLACEMENT_RULES:
        if owner_name in rule['owners']:
            for evidence in ('runtime', 'test'):
                if not (ROOT / rule[evidence]).is_file():
                    raise FileNotFoundError(rule[evidence])
            return {'runtime': rule['runtime'], 'test': rule['test']}
    return None


def compute():
    """Return the full audit result without touching disk beyond reads."""
    coverage, manifest, contracts = _load()
    _check_inventory_freshness(coverage, manifest, contracts)
    retained = set(manifest['retained'])
    boundaries = manifest['boundaries']
    compiled_sources = _compiled_sources(coverage['sources'])
    by_reg = {c['source_registration']: c for c in contracts['controls']}

    controls = {'retained': [], 'adapter': [], 'replaced': [],
                'unsupported': [], 'not-wired': []}
    replacements = []
    open_items, uncategorized = [], []
    for control in coverage['controls']:
        owner_file = control['owner'].split('::')[0]
        owner_name = _short(control['owner'])
        names = _handler_names(control, by_reg)
        disposition = _disposition(names, retained, boundaries, compiled_sources)
        # This source helper accepts a generic callback parameter named ``cb``;
        # its emulator adapter constructs the same result controls.
        disposition = _owner_adapter_disposition(control, disposition, boundaries)
        replacement = _replacement(owner_name) if disposition == 'not-wired' else None
        if replacement:
            disposition = 'replaced'
            replacements.append({'id': control['id'], **replacement})
        controls[disposition].append(control['id'])
        if disposition == 'not-wired':
            rule = _categorize(owner_file, owner_name)
            entry = {'id': control['id'], 'owner': owner_name,
                     'callback': _short(control.get('callback'))}
            if rule is None:
                uncategorized.append(entry)
            else:
                entry.update(category=rule['category'], open=rule['open'],
                             reason=rule['reason'])
                open_items.append(entry)

    back_routes = []
    for route in coverage['routes']:
        if route.get('kind') != 'back':
            continue
        fn = _short(route.get('function'))
        disposition = ('retained' if fn in retained or route.get('file') in compiled_sources
                       else boundaries.get(fn, 'not-wired'))
        deferred = False
        back_routes.append({
            'id': route['id'], 'source': route.get('source'),
            'target': route.get('target'), 'function': fn,
            'disposition': disposition,
            'covered': disposition in ('retained', 'adapter'),
            'deferred': deferred})

    bad_back = [r for r in back_routes
                if not r['covered'] and not r['deferred']]

    summary = {kind: len(ids) for kind, ids in controls.items()}
    summary['total'] = sum(summary.values())
    summary['covered'] = summary['retained'] + summary['adapter']
    summary['replaced'] = len(replacements)
    summary['open'] = sum(1 for i in open_items if i['open'])
    summary['deferred'] = sum(1 for i in open_items if not i['open'])
    summary['uncategorized'] = len(uncategorized)
    summary['back_routes'] = len(back_routes)
    summary['back_routes_covered'] = sum(1 for r in back_routes if r['covered'])
    summary['back_routes_uncovered'] = len(bad_back)

    return {
        'source_sha256': manifest.get('source_sha256'),
        'summary': summary,
        'not_wired': sorted(controls['not-wired']),
        'unsupported': sorted(controls['unsupported']),
        'replaced': sorted(controls['replaced']),
        'replacements': sorted(replacements, key=lambda entry: entry['id']),
        'open_items': sorted(open_items, key=lambda e: (e['category'], e['id'])),
        'uncategorized': uncategorized,
        'back_routes': sorted(back_routes, key=lambda r: r['id']),
        'back_routes_uncovered': bad_back,
    }


def load_baseline():
    if not BASELINE.exists():
        return None
    return json.loads(BASELINE.read_text(encoding='utf-8'))


def write_baseline(result):
    payload = {
        'note': 'Phase 4.7 coverage-ledger baseline. Regenerate with '
                'python tools/ui_emulator/audit_coverage.py --write-baseline '
                'after a deliberate slice-policy change.',
        'summary': result['summary'],
        'not_wired': result['not_wired'],
        'unsupported': result['unsupported'],
        'replaced': result['replaced'],
        'replacements': result['replacements'],
        'open_items': result['open_items'],
    }
    BASELINE.write_text(json.dumps(payload, indent=2) + '\n', encoding='utf-8')


def _print(result):
    s = result['summary']
    print('Phase 4.7 coverage audit')
    print(f"  controls: {s['total']}  covered={s['covered']} "
          f"(retained={s['retained']} adapter={s['adapter']}) "
         f"replaced={s['replaced']} unsupported={s['unsupported']} not-wired={s['not-wired']}")
    print(f"  workflow replacements pending control-level review: {s['replaced']}")
    print(f"  open items: {s['open']} candidate + {s['deferred']} deferred; "
          f"uncategorized={s['uncategorized']}")
    print(f"  back routes: {s['back_routes']} "
          f"covered={s['back_routes_covered']} uncovered={s['back_routes_uncovered']}")
    if result['uncategorized']:
        print('  UNCATEGORIZED (triage required):')
        for e in result['uncategorized']:
            print(f"    {e['id']}")
    if result['back_routes_uncovered']:
        print('  UNCOVERED back routes:')
        for r in result['back_routes_uncovered']:
            print(f"    {r['id']} {r['source']}->{r['target']} ({r['function']})")


if __name__ == '__main__':
    import sys
    outcome = compute()
    _print(outcome)
    if '--write-baseline' in sys.argv:
        write_baseline(outcome)
        print(f'  wrote {BASELINE.relative_to(ROOT)}')
