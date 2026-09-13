# Adapter binding stability after source changes

2026-09-13, local emulator regression fix.

After refreshing generated source metadata, native Evil Twin and Rogue AP Start buttons stopped responding. Opening Evil Twin produced three `Unenrolled production event registration` events; Start had no binding and created no model operation. The copied adapter constructors still supplied old numeric firmware line offsets to `app_bind`, while generated templates reflected the current source locations.

49 copied registration sites now resolve their permanent template ID through `app_template_line`. A source insertion can move an enrolled registration without changing its identity or disabling its callback. Unknown identities continue to fail closed through the existing unenrolled-registration check.

| Adapter file | Migrated sites |
| --- | ---: |
| attacks_deauth_arp.c | 15 |
| attacks_rogue_evil_mitm.c | 13 |
| nettools_nmap_iot.c | 17 |
| radar.c | 3 |
| dialogs.c | 1 |

Every mapping matches constructor, object, callback, event, user data and construction branch against the existing permanent identity registry. Explicit reviewed aliases cover module-owned `ctx->` fields, asynchronous ARP/Nmap host-list builders, and Radar's renderer/panel. Nmap's saved-password and unknown-password forms previously reused numeric IDs from the open-network branch; each now uses its own existing branch identity. No firmware source or identity enrollment changed.

`tests/test_emulator_adapter_sites.py` initially failed at all 49 sites in both checks, then passed both tests after migration. It rejects numeric offsets and verifies every copied binding against its intended semantic identity and source contract. Portal acceptance now checks for missing enrollment immediately after opening each form, before clicking Start.

Focused validation on the rebuilt artifact:

- `test_emulator_portal_demo.py`: **4 PASS, 18.022 s**, including Evil Twin/Rogue AP client-to-password demonstrations in all rotations and CHROME-CLINIC.
- `test_emulator_attacks_rogue_evil_mitm.py`: **4 PASS, 6.270 s**, including disconnect, reset, MITM cancel and file finalization.
- `Queue2NettoolsBrowser.test_s13_all_rotations` and `test_nmap_secured_connect_accessible_all_rotations`: **2 PASS, 93.549 s**, covering ARP/MITM/Nmap in all rotations and the corrected secured-network Nmap branch.

The final full-regression report remains authoritative for the complete emulator suite.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
