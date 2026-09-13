# Phase 4.5 implementation

Approved scope: native deauth, ARP, Karma, Beacon, Rogue AP, Evil Twin and MITM
screens backed only by deterministic offline simulation, plus Observer activity.
Edit current working tree; no commits, real operations or external services.
Full build/browser checks are left to the user, as requested.

1. Shared attack lifecycle and passive Observer activity model; lightweight tests.
2. Native deauth/ARP adapters and browser acceptance.
3. Native Karma/Beacon adapters and browser acceptance.
4. Native Rogue AP/Evil Twin/MITM adapters and browser acceptance.
5. Connect menus, bindings, cooperative ticks and Observer updates.
6. Cheap syntax/model checks, review, verification gate and documentation handoff.

Ruling: preserve the Phase 3 synthetic-capture button and tests as a separate
workflow when enabling the native MITM screen. Observer is a synthetic passive
activity feed, so it can display events while another simulated job owns a module.
Prior status: user confirmed 4.4 build succeeds; final browser results not supplied.

Progress: implementation and inexpensive checks complete. Full build/browser
acceptance remains with the user. See Phase_4_5_Report.md for evidence/commands.
Review resolved station binding/return routes, uncaught template errors and
legacy Karma state clobbering on tab switches. Native Nmap scan entry is now wired.
