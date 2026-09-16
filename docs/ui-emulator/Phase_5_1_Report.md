# Phase 5.1 - Settings persistence and reset

First bounded implementation slice of Phase 5. Guided stories, scenario selection
and optional persistent virtual SD remain open.

## Behavior

- Native settings use one versioned `tab5-emulator:settings` JSON record.
  Existing `tab5-emulator-v1:*` numeric settings migrate once; invalid values,
  corrupt records and unrecognized schema versions recover to validated defaults.
- Internal settings and the existing Wardrive autoupload adapter keys retain
  their supported numeric ranges. Wardrive startup restoration is not added by
  this slice; its existing adapter writes remain compatible.
- Successful writes survive reload and the existing native rotation/restart
  flow. When localStorage refuses access or writes, sessionStorage preserves
  edits across reload in the same tab. If both are unavailable, edits remain in
  memory until reload. A visible status explains the active storage mode.
- Reset demo restores default settings and orientation and reconstructs the
  seeded device, discarding transient operations and virtual files. It preserves
  unrelated origin storage. An empty versioned record prevents legacy settings
  from returning after reset.
- If both storage tiers refuse writes but old settings remain readable, the
  reset URL retains `reset=1` so reload cannot resurrect those old settings.
  Once settings can be saved again, startup removes that marker.
- Module reboot behavior from Phase 4.6 is unchanged: module state restarts
  while its virtual SD is retained. Full page reload still recreates transient
  virtual SD; optional persistent SD is separate work.
- Interrupted WebAssembly downloads are retried at most three times, with a
  visible retry message and a recoverable loading error after exhaustion.
  Compilation starts only after the binary has downloaded successfully.

## Verification

**Latest result (2026-09-12):** Build passed. All seven Node persistence cases
and six Chromium lifecycle/loading cases passed (browser suite: 10.883 s).
The cumulative run is **failed**, with 13 scripts passed and one failed:
`test_emulator_lifecycle.py` timed out in startup for all four cases, before
their workflow actions. Later domain gates did not run. See
[run summary](test-runs/20260912-125312-25a13f/summary.json) and
[aggregate report](phase5-settings-verification.json). The download retry handles
the injected transient-failure cases but does not resolve every local transfer
failure in this environment. Full acceptance remains open.

Build with `tools/ui_emulator/build.ps1`, then run:

```powershell
tools/ui_emulator/.venv/Scripts/python.exe tests/verify_emulator_phase5_settings.py
```

The gate runs seven persistence model tests, six Chromium lifecycle/loading
tests and the full Phase 4 roll-up, including the 4.7 coverage audit. It records
artifact hashes in `phase5-settings-verification.json` and writes the usual
per-script logs and cumulative summary under `test-runs/`.

Test-first checks reproduced missing persistence/migration, rejection of the
existing Wardrive keys, failure to carry a fallback snapshot across restart,
the old reload-only reset, and resurrection of stale settings when writes fail.

Initial full Phase 4 verification encountered intermittent startup timeouts
in Phase 2 before reaching the detector/audit gates. A browser network probe
isolated `ERR_CONNECTION_RESET` during the local WebAssembly response; the same
failure occurred with a direct fetch without compilation. The bounded download
retry addresses this recoverable transport failure. Browser route tests inject
two interrupted downloads and verify recovery, then interrupt all three and
verify a visible terminal error. Final acceptance is recorded in the gate report.

No commit, push, deployment or physical hardware validation is part of this slice.
