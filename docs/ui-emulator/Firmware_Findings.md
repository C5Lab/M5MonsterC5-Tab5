# Firmware defects found by the emulator

## 1. `%f` passed to LVGL's built-in printf, which has no float support

**Status:** fixed 2026-09-11 in `main/main.c`. Found 2026-09-10 while wiring the
Phase 3 local PCAP analysis screens. Affected shipped firmware, not only the
emulator.

**Resolution:** all seven call sites now format the float with `snprintf` into a
local buffer and pass it as `%s`, exactly the suggested fix below. The frozen
slice inputs (`coverage.json`, `control-contracts.json`, `control-identities.json`,
`adapter-decisions.json`, `dependency-registry.json`, `slice-policy.json`) were
regenerated against the fixed source (`main/main.c` sha256 `900e1adf…`), the
emulator was rebuilt, and `tests/verify_emulator_phase3.py` passes. The capture
summary page now renders and is asserted against the model.

`lv_conf.h` sets `LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN` (line 45) and
`LV_USE_FLOAT 0` (line 455). In LVGL's builtin formatter,
`PRINTF_DISABLE_SUPPORT_FLOAT` is then `1`, so `case 'f'` is compiled out
(`managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c:776`).
An unknown conversion falls through to `default:`, which prints the literal
character **and does not consume the argument**.

Every conversion after a `%f` therefore reads the wrong `va_arg`. Where a `%s`
follows, that is a wild pointer dereference.

### Call sites in `main/main.c`

| Line | Format | Consequence |
| --- | --- | --- |
| 22835 | `... SAT: %d %.2f km` | trailing value only; distance prints as `f` |
| 22844 | `... SAT: %d %.2f km` | same |
| 30334 | `Coordinates: %.6f, %.6f` | wardrive GPS debug shows `f, f`, never coordinates |
| 40943 | `%.0f KB/s  -  %s left` | `%s` reads the double's low half as a pointer |
| 40977 | `%.0f pkt/s  -  %s left` | same |
| 43129 | `%.3f s span
TX %llu B \| RX %llu B` | byte counters read shifted arguments |
| 46447 | `%.3f s  \|  %s  \|  snaplen %lu%s%s%s` | four `%s` read shifted arguments |

### Evidence

Opening a capture on the emulator's ESPShark TAB5 SD page traps immediately:

```
RuntimeError: memory access out of bounds
    at emulator.wasm.lv_vsnprintf_inner
    at emulator.wasm.lv_label_set_text_fmt
    at emulator.wasm.pcap_viewer_render_capture_page
    at emulator.wasm.pcap_viewer_render_load_result_async
```

WebAssembly bounds-checks linear memory, so the bad pointer traps here. On the
device the same read lands somewhere in a flat address space: usually garbage
text, sometimes a crash. The defect is identical.

### Suggested fix

Format the value with the C library first, then pass it as `%s`:

```c
char span_text[16];
snprintf(span_text, sizeof(span_text), "%.3f", duration);
lv_label_set_text_fmt(label, "... %s s ...", span_text, ...);
```

`snprintf` is already used throughout `main.c` and does support floats. The
alternative, setting `LV_USE_FLOAT 1`, grows every build and changes LVGL's own
arithmetic; it is not the smaller change.

### Regeneration performed with the fix

`tests/test_lvgl_float_format.py` compiles the affected production label fragments
against LVGL's actual builtin formatter under ASan/UBSan through WSL `cc`.
It verifies decimal values and arguments following them; the regression passes.
The browser workflow also verifies the rendered capture summary. Full ESP-IDF
firmware compilation and physical Tab5 validation were not performed in this
emulator checkpoint.

Editing `main/main.c` invalidated `slice-policy.json`'s `source_sha256` and the
frozen control contracts. These were regenerated deliberately against the fixed
source and the emulator was rebuilt, so the summary page now renders. Fixing the
`%f` sites also shifted line numbers, which exposed a separate emulator-tooling
off-by-one (multi-line `lv_obj_add_event_cb(...)` enrollment); that is recorded
in the [Phase 3 report](Phase_3_Report.md), not here, because it is not a firmware
defect.
