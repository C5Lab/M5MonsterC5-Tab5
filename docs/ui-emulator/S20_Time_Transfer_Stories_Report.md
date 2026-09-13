# S20 — Time and Transfer Speed stories

Local implementation, 2026-09-13. Choose a variant in **More guided stories**, press **Start selected story**, then use the native INTERNAL → Settings controls.

Seven variants cover setting a different valid date/time and observing the running virtual RTC; rejecting February 31 without changing the RTC and then correcting it; closing an unsaved edit and reopening the unchanged clock; 12/24-hour format with matching visible clock; DST with a one-hour clock shift; clock visibility; and choosing/reopening a saved console baud preference.

Stories read the actual native popup, rollers, status label, top-bar clock and preferences through the read-only `emu_settings_s20_state` export. Starting or restarting while a popup is already open requires closing and reopening it. A previously saved preference does not satisfy a fresh change. Hidden pages, another module and old guide epochs do not advance steps. Completion returns to the story menu without restarting the demo.

Transfer Speed only demonstrates the console baud preference. It does not measure a physical cable or radio transfer rate. The RTC is local virtual civil time; no operating-system clock or hardware is changed. Existing settings persistence remains separate from volatile story progress and demo files.

Validation: `node --test tests/test_emulator_settings_s20.mjs` — **5 tests PASS**. `tests/test_emulator_settings_s20.py` — **4 tests PASS in 43.513 s**, all seven variants in four rotations (28 paths), plus restarting a partially completed settings story. The browser tests launch from the actual story picker and operate native canvas controls; all paths return to the picker and assert no unavailable-control events or browser errors. Calendar tests verify that an invalid write preserves the RTC offset before correcting the date. The consolidated full-regression result is recorded separately in the current emulator test report.

Files: `tools/ui_emulator/web/settings-s20.mjs`, `tools/ui_emulator/runtime/settings_s20_story.c`, `tests/test_emulator_settings_s20.mjs`, `tests/test_emulator_settings_s20.py`.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
