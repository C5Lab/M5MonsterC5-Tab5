# Monster OTA / JanOS RF — test coverage

All tests exercise the real parser and decision logic in
[`main/ota_rf.c`](../main/ota_rf.c) (host-compiled, no copy). The Python
contract tests compile that exact source with a small C harness
(`tests/_ota_rf_harness.py`); the C test links it directly.

## Running

```sh
# C unit test
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I main -o /tmp/ota_rf_test tests/ota_rf_test.c main/ota_rf.c
/tmp/ota_rf_test

# Python contract tests
python3 tests/test_ota_metadata.py
python3 tests/test_ota_flow.py
python3 tests/test_ota_http.py
```

On a Windows workstation without a host compiler, run the same commands inside
WSL against the `/mnt/c/...` checkout (see [../tests/README.md](../tests/README.md)).

## Requirement → test map

| Requirement | Covered by |
|---|---|
| Classic firmware with the new updater (capability, not RF hardware) | `test_ota_metadata` (classic+new-updater), `ota_rf_test` (`test_info_classic_new_updater`) |
| RF firmware with the new commands (compatible layout) | `test_ota_metadata` (rf compatible), `ota_rf_test` (`test_info_rf_compatible`) |
| Older RF / firmware without the new commands | `test_ota_metadata` (old updater), `test_ota_flow` (no-capability block), `ota_rf_test` (`test_info_old_updater_no_rf_lines`) |
| Unknown variant / identification timeout | `test_ota_flow` (unknown layout, subghz-without-capability), `test_ota_http` (`Unrecognized command`) |
| RF repo in `ota_info` ≠ RF hardware detected | `test_ota_metadata`, `test_ota_flow` (independence), `ota_rf_test` |
| Command routing carries `rf`; no fallback to classic repo | `test_ota_flow` (routing), `ota_rf_test` (`test_command_builders`) |
| Tags with / without `v`; equal / older / newer versions | `ota_rf_test` (`test_versions`), `test_ota_flow`, `test_ota_metadata` |
| Reinstall / downgrade never automatic | `test_ota_flow` (CONFIRM_*), `ota_rf_test` (`test_decisions`) |
| Incompatible / unknown partition layout blocks install | `test_ota_flow`, `ota_rf_test` (`test_decisions`) |
| Empty list and fetch/connectivity errors | `test_ota_http` (empty list, fetch error, not-connected) |
| UART fragmentation / echo / prompt / foreign logs / restart | `ota_rf_test` (`test_line_asm`), `test_ota_http` (fragmented list, truncated tail), `test_ota_metadata` (interleaved noise) |
| Re-detect after restart | `test_ota_flow` (re-detect after reboot) |
| Device change clears cache; parallel operations refused | `test_ota_flow` (state), `ota_rf_test` (`test_state`) |

## Firmware compile check (no full build, no flash)

Per the project rule that firmware is compiled by the maintainer, the change was
verified by compiling the touched translation units to objects with the real
flags from `build/compile_commands.json` (so `-Werror`, including
`format-truncation`, applies):

- `main/main.c` → object, clean;
- `main/ota_rf.c` → object, clean.

The initial validation above compiled objects only. The 2026-10-04 follow-up
also completed a full ESP-IDF 5.4.1 build for ESP32-P4. No device flashing was run.

## Force/tag and variant regression follow-up (2026-10-04)

- `tests/test_ota_tab5_regression.py` compiles the actual Tab5 functions from
  `main.c` with transport/UI boundary stubs and the real `ota_rf.c`. It checks
  separate JanOS/APP versions, exact classic tags, dev channel commands, Force
  opening the list, fresh prechecks after selection, cross-variant blocks and
  empty-response rejection.
- `tests/ota_route_test.c` checks incompatible layouts/offsets, both directions
  of cross-variant blocking, legacy RF, absent metadata, tag preservation,
  command injection rejection and overlong release-tag rejection.
- Existing flow, HTTP, metadata and RF C suites continue to pass.
- The user confirmed the earlier RF 1.7.5 reinstall on hardware. The corrected
  Force/list and shared gate changes still require device acceptance testing.

## Hardware acceptance (to be run on a device — not yet performed)

See [ota-rf-implementation-plan.md](ota-rf-implementation-plan.md). The
end-to-end scenario is listed in the session notes; it must be run on real
classic and RF Monsters before claiming hardware confirmation.
