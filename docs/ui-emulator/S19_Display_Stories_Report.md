# S19 — Display preferences guided stories

Local implementation, 2026-09-13. Four entries in **More guided stories** use the native INTERNAL → Settings controls:

| Story | Native acceptance |
| --- | --- |
| Screen Timeout | Change the timeout, close and reopen, inspect the retained value, restore the starting preference and close. |
| Screen Brightness | Move and release the slider, require the saved native value and matching canvas brightness filter, close and reopen, restore the starting brightness and close. |
| Theme | Toggle Dark mode, observe the native rebuild and restore it. Change Dashboard, Boot sound and Alert sound, close and reopen to inspect retained values, then restore all original preferences, reopen and close. Sound controls save preferences only; the emulator stays silent. |
| Screen Rotation | Save a different orientation, close and reopen while the original orientation remains active, press native Restart, reopen after reload and verify the new orientation is active, then close. |

Guide predicates read native preferences and popup visibility through `emu_settings_s19_state`. They do not click controls or modify preferences. Wrong module, hidden page and stale guide epoch cannot advance a step. Starting again captures a new baseline; merely opening and closing an unchanged preference is insufficient.

Rotation uses a one-use URL fragment handed off only from the active rotation story at its native Restart step. The new page consumes and removes it immediately. Actual active orientation and the saved native preference must agree before the guide resumes its final inspection. Subsequent reloads return to the regular menu. This does not persist story progress in localStorage or sessionStorage.

Screen Timeout verifies the preference only. Browser inactivity does not simulate physical panel sleep. Brightness uses the existing CSS canvas adapter; rotation uses the existing local preview reboot. Settings retain the emulator's existing preference storage behavior; this change adds no persistent demonstration data.

## Verification

- `node --test tests/test_emulator_settings_s19.mjs`: **5 tests PASS**, including wrong-tab/hidden/stale-epoch rejection, saved brightness plus matching filter, Theme restoration and invalid/replayed rotation handoffs.
- `.\tools\ui_emulator\.venv\Scripts\python.exe tests/test_emulator_settings_s19.py`: **5 tests PASS in 35.517 s** on the final native build. Covers **16 native story paths** (four stories × four starting orientations), plus close-without-change, guide restart, wrong-tab and one-shot reload cleanup.
- Browser paths launch the real guide picker and operate actual canvas controls. Each path rejects JavaScript errors and unsupported emulator events. Theme includes all four controls and compares the actual canvas before/after palette rebuild; brightness requires the real saved value and CSS filter; rotation checks native canvas dimensions and Currently active after the native Restart.
- No deployment or physical Tab5 claim.

Final integrated verification (2026-09-13): **72 scripts PASS, 0 failures**, including this group on the final build. [Full result](test-runs/20260913-201748-ac7773/summary.json), [integration report](S18_S20_Integration_Report.md).
