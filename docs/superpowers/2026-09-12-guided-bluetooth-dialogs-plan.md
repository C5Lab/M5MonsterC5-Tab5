# Guided Bluetooth and remaining dialog implementation

Authorized scope: user requested TODO items 1 and 2. Keep virtual SD and story
progress volatile. No publishing, firmware changes, Nmap layout work or durable SD.

1. Guided Bluetooth: add a scenario definition, deterministic state-driven
   progress, and accessible guide beside the native emulator. Steps: open Grove
   Bluetooth scan, locate a scenario device, observe changing RSSI, stop/return.
   Free exploration remains available; restarting a guide resets only guide
   progress, not the user's device state. Do not fake completion with a Next button.
2. Native dialogs: implement the eight currently open coverage controls, including
   Wardrive home detection confirmation, missing board and SD warnings, version
   mismatch, and OTA slot details. Match real entry conditions; expose offline
   module conditions in the simulator so users can exercise the branches.
3. Add focused model/native browser tests, including all rotations and cancellation.
   Rebuild once integration is ready, update coverage only for verified controls,
   run the focused gate then cumulative acceptance. Record actual failures.
4. Review both parts and update handoff/TODO with evidence and limitations.

Execution: delegate native dialogs and their tests; root implements guided story
and shell integration. Coordinate shared application/runtime/build changes.
Ruling: work in the existing dirty workspace because the emulator is largely
untracked and already user-tested here; preserve all unrelated files.

## Execution ledger

- Guided story implemented and native browser tests pass in four orientations.
- Native dialogs implemented; the audit delta is exactly eight controls, with no
  removed coverage and no change to the 93 deferred controls.
- Task and integrated code reviews found no blockers. Fixed review findings:
  preserve guide in fullscreen, verify background tracking cannot advance,
  commit OTA slot only after successful reboot, use per-slot callback identities,
  and accurately label the offline no-board condition.
- Ruling: add a two-second viewing window to the three distinct RSSI readings;
  default 10x timing otherwise made the signal instruction disappear too quickly.
- Final focused integrated gate passed 18 cases / 4 scripts in 39.13 s.
- Full gate attempted: 22 scripts passed, then a pre-existing localhost Wasm
  startup reset/timeout stopped test_emulator_browser.py. Later suites did not
  run; no full PASS claimed. Both authorized feature tasks are implemented.
