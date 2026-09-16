# Phase 2 verification

Reviewed: 2026-09-10. Phase 2 covers the Home/Settings/Scan feasibility slice.
The authoritative result is [the seven-suite gate](phase2-verification.json),
which rejects failed tests, stale reports and static build changes during testing.

## Reproduction

From Windows Command Prompt in the repository root:

~~~bat
powershell -ExecutionPolicy Bypass -File tools\ui_emulator\build.ps1 -Jobs 8
tools\ui_emulator\.venv\Scripts\python.exe tests\verify_emulator_phase2.py
~~~

The build needs Docker and the managed LVGL component. Browser tests need the
emulator Python environment and Playwright Chromium. Run the interpreter
directly: prefixing its executable path with py makes Python interpret the binary
as source. The pinned Docker image's pip does not support
--break-system-packages; installing packages in Windows does not change it.

For a manual preview:

~~~bat
cd /d C:\Users\mati\Documents\GitHub\M5MonsterC5-Tab5
tools\ui_emulator\.venv\Scripts\python.exe -m http.server 8765 --bind 127.0.0.1 --protocol HTTP/1.1 --directory tools/ui_emulator/dist
~~~

Keep the terminal open and visit http://127.0.0.1:8765/. Stop with Ctrl+C.
If occupied, change the port in both places. Rebuild after source changes and
refresh the page. HTTP/1.1 keep-alive avoids the intermittent local Wasm response
stall observed with the earlier HTTP/1.0 test server.

## Verified scope

- Home, Scan and Internal Settings navigation using real Canvas input.
- Four rotations at desktop and narrow viewports; mouse and emulated touch,
  landscape wheel/drag scrolling, pointer cancellation without accidental clicks.
- Simulated scan, cancellation, late-response suppression, rescan, selection and
  independent Grove/MBus results. Active scan overlays block Canvas navigation
  as production firmware intends; rendering continues and navigation resumes
  after completion. The shell provides cancellation.
- Scan Setup numeric editing, saved values, Enter commit, Escape cancellation,
  range clamping and production minimum/maximum validation. Numeric typing
  replaces a draft; Enter or the next pointer press commits through the Spinbox
  API before the production Save handler reads its value.
- Theme and retained sound settings, screen timeout value, visible brightness
  including 1%, rotation and restart persistence. A bare URL restores saved
  rotation; an explicit rotation query overrides it.
- Runtime control identities, unique generations and popup cleanup. Enumeration
  covers every live registration, including hidden objects and multiple events
  on the brightness slider, against frozen control templates.
- Production extraction and contracts: 132 byte-preserved functions and 44
  explicit boundaries. This is a retained slice, not whole-firmware coverage.

Evidence: [browser cases](phase2-browser-report.json),
[interaction acceptance](phase2-acceptance-report.json), and
[performance profiles](phase2-performance-report.json). The gate also runs
settings regression, input, slice and contract suites. Reports identify
the exact static files by SHA-256.

## Performance and boundaries

Budgets define a 1.2 MB total static download, desktop and throttled-mobile
startup/render limits, and a 96 MiB observed linear-memory limit. The mobile
profile uses Chromium with 4x CPU throttling and a 9 Mbit/s, 150 ms-latency link.
It is not physical phone testing. Render callback measurements include framebuffer
conversion and putImageData, but exclude browser paint/compositing. Wasm memory
is committed linear memory rather than live allocations and can grow. Exact
measurements are in the performance report.

Time, Screen Lock, Transfer Speed, OTA and other excluded destinations retain
explicit unsupported boundaries. Sound configuration persistence does not claim
hardware playback; storing screen timeout does not claim physical display
blanking. SubGHz remains deferred. Firefox, WebKit and physical mobile validation
belong to Phase 6. No production firmware source, hardware or deployment is
changed by this prototype.

Phase 3 now has a [standalone model foundation](Phase_3_Report.md); its complete
browser workflow remains open.

**Gate freshness (2026-09-10):** The recorded gate describes the build made
before the Phase 3 Observer slice was wired. That change widens the retained
slice, so the artifact hashes in these reports no longer match `dist/`. Re-run
`tests\verify_emulator_phase2.py` after the next build; the gate is designed to
reject stale reports, so it will say so itself.
