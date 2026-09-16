# SAE Overflow emulator follow-up

The firmware already contained an SAE popup, but the emulator's action router
did not open it and its command adapter accepted `sae_overflow` without an
effect. This follow-up wires the native popup to an offline model lifecycle.

## Behavior

- Select exactly one scenario network, then choose **SAE Overflow**. The native
  popup displays that network's identity and **SAE Overflow Active**.
- The simulated operation reserves only its own module. STOP completes it and
  removes the popup; reopening starts a new operation ID.
- Disconnect and the shell's Cancel action cancel the operation. The popup title
  changes to **SAE Overflow stopped**, and STOP closes it. Owner deletion also
  cancels any remaining job and clears the native popup pointers.
- Grove and MBus can run independently; stopping one leaves the other running.
- Demo reset discards the runtime and any pending SAE operation. No real radio
  traffic is generated and no output capture is written.

## Files and verification

**Verified 2026-09-12:** Build passed; focused gate passed all eight Node cases
and three Chromium cases (11.87 s total). The browser cases include all four
rotations, invalid selection, disconnect/reset and simultaneous Grove/MBus
operations. Evidence: [focused summary](test-runs/20260912-145145-fe6dcd/summary.json)
and [artifact-matched gate report](sae-verification.json). All five coverage-audit
assertions also pass.

The separately attempted earlier attack-browser regression passed seven of
eleven cases. Four Evil Twin/MITM/Rogue cases timed out in `setUp` before emulator
initialization; see [regression run](test-runs/20260912-144833-9fe5b9/summary.json).
This failure remains recorded and is not converted into a focused-gate pass.

`slice-phase4-sae.json` retains the production popup and close handler;
`runtime/sae.c` owns the cooperative lifecycle. The route and per-tick/STOP
dispatch are connected in `attacks_routes.c` and `application.c`. The existing
shared attack model validates SAE's single target and owns cancellation.

```powershell
.\tools\ui_emulator\build.ps1
.\tools\ui_emulator\.venv\Scripts\python.exe tests/verify_emulator_sae.py
```

The focused gate includes two SAE model cases, six existing attack-model
regressions, and three browser cases (including four rotations and simultaneous
Grove/MBus operations). It is also included in `verify_emulator_phase4.py`, so the
full `verify_emulator_phase5_settings.py` command reaches it after the earlier
cumulative gates. Earlier attack browser coverage remains in that cumulative
chain rather than being repeated by the focused SAE gate.

The reviewed audit delta is exactly `show_sae_popup::event-0`: not-wired to
retained. Totals: 452 controls, 319 covered, 6 explicit unsupported boundaries,
127 catalogued not-wired controls; zero uncategorized controls or uncovered back
routes. The baseline was regenerated after inspecting this single-item delta.

Global Handshaker was subsequently implemented; see [Global Attacks](Global_Attacks_Report.md). Other catalogued open/deferred flows remain separate.
Full Chromium regression is subject to the previously recorded local startup
connection-reset failures; a focused result does not close that broader gate.
