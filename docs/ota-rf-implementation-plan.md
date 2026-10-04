# Monster OTA — JanOS RF support (implementation plan)

Adds over-the-air updates for **Monster RF** (JanOS RF) alongside the existing
classic Monster flow, from the M5Stack Tab5 (ESP32-P4) remote. The Tab5 still
sends text commands over UART and parses the C5's log lines; all flashing stays
on the C5 (see [Monster_OTA.md](Monster_OTA.md)).

Source of truth for the commands and responses: `projectZero/ESP32C5/main/main.c`
(JanOS 1.7.5). The RF releases live at
`elpadrino26/janosrf-web-flasher`.

## 1. Three facts, kept independent

A shared version number (e.g. 1.7.5) does not identify the hardware variant, and
no single signal proves the others. The Tab5 tracks them separately:

| Fact | Trustworthy positive signal | What it does **not** prove |
|---|---|---|
| **Firmware variant** (RF vs classic) | `subghz_status` returns `[SUBGHZ_STATUS] ...` ⇒ RF firmware is running | Silence / `Unrecognized command` does **not** prove classic hardware — an RF board can carry classic firmware |
| **Command support** (updater understands `rf`) | `ota_info` prints `OTA RF source:` (and `OTA RF layout:`) | Does **not** prove RF hardware — a classic build with the new updater prints the same line. `ota_list rf` is **not** a safe probe: an older parser may ignore the extra argument |
| **Partition layout** (RF-compatible) | `ota_info` prints `OTA RF layout: compatible` | Absent line ⇒ **unknown**, never assume compatible. Classic table @`0x8000` is not compatible; RF requires @`0x10000` |

These map directly to `ota_info_supports_rf_commands()`, the `subghz_status`
probe already run by `check_subghz_status_for_tab()` (→ `ctx->has_subghz`), and
`ota_rf_layout_t` in [`main/ota_rf.h`](../main/ota_rf.h).

## 2. `ota_info` as the pre-connection probe

`ota_info` reads partitions only — it needs no WiFi — so the Tab5 runs it
**before connecting** for any update or list. The parsed report
(`ota_info_report_t`) yields:

- `has_rf_source` → the updater understands the RF commands;
- `layout` → `COMPATIBLE` / `INCOMPATIBLE` / `UNKNOWN`;
- `table_offset`, `boot`/`running`/`next`, and the two `APP[n]` slots.

Expected new lines from JanOS 1.7.5:

```
OTA default source: classic (C5Lab/projectZero), channel=main
OTA RF source: elpadrino26/janosrf-web-flasher (one-shot: ota_check rf <tag>)
OTA partition table offset: 0x10000
OTA RF layout: compatible (release files still require verification)
```

## 3. Automatic routing (with firmware-enforced gates)

Routing is automatic (`ota_rf_auto_should_use_rf()`):

- a **live `[SUBGHZ_STATUS]` reply** (the SUB-GHz tile is shown, `ctx->has_subghz`)
  is **sufficient on its own** to route RF. The `subghz` command exists only in
  RF firmware, and RF firmware can only have booted from the RF layout, so this
  holds even when the running firmware's `ota_info` omits the RF lines (as some
  `janosrf-web-flasher` builds do — see the field finding below). A
  subghz-confirmed device is treated as an override, so an UNKNOWN layout does
  not block it (the C5 firmware still verifies);
- otherwise the Tab5 routes RF only when `ota_info` positively proves it: the
  updater supports the rf commands **and** the layout is `compatible`;
- a missing subghz reply never forces the classic path (an RF board may carry
  classic firmware). A positively **INCOMPATIBLE** layout always blocks.

RF listing and install always carry the `rf` argument and **never fall back to
the classic repo** on error.

### Manual variant override + Force update

Field finding: some Monster RF firmware (e.g. a `janosrf-web-flasher` 1.7.5
build) does **not** print the `OTA RF source:` / `OTA RF layout:` lines in
`ota_info`, so automatic detection cannot see it and falls back to classic. The
setup page therefore has:

- **Firmware Variant** — `Auto-detect` (the routing above), `Classic`, or
  `RF (Monster RF)`. Choosing `RF` is a manual override that routes the RF
  source only when the shared route gate can verify it. Unknown legacy RF
  layouts require positive Sub-GHz identification and a recognized `ota_info`
  response. Manual selection never bypasses incompatible layout evidence.
- **Force update** opens the release list. The user selects and confirms an
  actual published tag, copied verbatim into `ota_check [rf] <tag>`. Both classic
  and RF rows are selectable. Neither the detected JanOS version nor APP
  descriptor metadata is used to guess a tag. A fresh `ota_info` precheck runs
  again after confirmation, before the install command is issued.
- The detected JanOS version and APP descriptor version remain separate. A
  mismatch is shown in Info; APP metadata cannot overwrite the detected JanOS
  version. The selected classic main/dev channel is sent to the target module
  again before listing or updating.

RF is a per-operation choice, not a saved channel (`ota_channel` stays classic).

### Single-source RF firmware (no `rf` keyword)

The janosrf 1.7.5 build answers `ota_check rf 1.7.5` with
`Usage: ota_check [latest|<tag>]` — it has **no `rf` argument**. It is a
*single-source* build: its own plain `ota_check` already targets the RF repo it
was flashed from. The `rf` keyword is a newer dual-source feature it lacks.

So the Tab5 sends the `rf` keyword **only** when the firmware advertises it
(`ota_info_supports_rf_commands`, i.e. the `OTA RF source:` line). When RF is
wanted but the firmware has no `rf` keyword (e.g. a subghz-confirmed janosrf
build), the Tab5 sends the **plain** `ota_check` / `ota_list`, which on RF
firmware hits its own RF repo. This is not "falling back to the classic source":
on a firmware that *does* support `rf`, the keyword is never dropped (dropping it
there would wrongly select the classic repo). Force reinstall on such a device is
plain `ota_check <selected_release_tag>` after the release-list confirmation.

### Shared route gate

`ota_install_route_allowed()` runs before every route, including commands
without the `rf` keyword. It blocks RF on incompatible/non-RF offsets, classic
on positively identified RF firmware or RF layouts, and empty precheck responses.
The legacy classic path accepts a recognized running APP report when older
firmware omits offset/source metadata. This preserves old classic updates; it
does not prove physical hardware identity when old firmware cannot report it.
JanOS is the final writer and must enforce its own image/layout checks.

## 4. Install decision (`ota_rf_decide_install`)

For an RF install the module returns one decision:

| Condition | Decision | UI |
|---|---|---|
| Updater lacks rf commands | `BLOCK_NO_CAPABILITY` | explain: install newer classic JanOS first |
| Layout incompatible | `BLOCK_INCOMPATIBLE` | explain: restore RF layout over USB |
| Layout unknown | `BLOCK_UNKNOWN_LAYOUT` | explain: read Info / update first — never assumed |
| Latest, compatible | `ALLOW_LATEST` | proceed (`ota_check rf`) — firmware skips if not newer |
| Explicit tag, newer | `ALLOW_TAG` | proceed (`ota_check rf <tag>`) |
| Explicit tag, same version | `CONFIRM_REINSTALL` | confirm dialog, then proceed |
| Explicit tag, older | `CONFIRM_DOWNGRADE` | confirm dialog, then proceed |
| Explicit tag, not a version | `BLOCK_BAD_TAG` | explain |

Version parsing/comparison (`ota_rf_parse_version` / `ota_rf_compare_versions`)
is ported verbatim from JanOS so the Tab5 accepts/rejects exactly what the
firmware will, accepting both `1.7.5` and `v1.7.5` and **preserving the original
tag** in the install command. Reinstall and downgrade are never started
automatically — the automatic "Download & Flash" only requests *latest*; an
explicit tag comes from tapping a release row and is always confirmed first.

The firmware re-verifies everything regardless: `ota_rf_layout_compatible()`
plus a byte-compare of `partition-table.bin`/`bootloader.bin`; it never migrates
partitions. The Tab5 never migrates partitions or bypasses these checks.

## 5. Commands (P4 → C5)

| Purpose | Classic | RF |
|---|---|---|
| Partition/source info | `ota_info` | `ota_info` (same command; reports both sources + layout) |
| List releases | `ota_list` | `ota_list rf` |
| Install latest-if-newer | `ota_check` | `ota_check rf` / `ota_check rf latest` |
| Install explicit tag (reinstall/downgrade) | `ota_check <tag>` | `ota_check rf <tag>` |
| Channel (classic only) | `ota_channel main\|dev` | — (RF is a per-operation choice, not a saved channel) |

The RF flow connects with a plain `wifi_connect` (no `ota` flag) and then sends
the routed command after the C5 reports an IP. The classic flow is also routed
through a post-IP `ota_check` for a single code path.

## 6. Async UART handling

- Line framing uses the host-tested reassembler `ota_line_asm_*` (fragmentation,
  bare CR / bare LF / CRLF, dropped empties, over-long lines discarded not
  truncated). Command echo, the prompt, and interleaved foreign log lines pass
  through as ordinary lines; the parser decides which to keep.
- The stages are separated: **precheck** (`ota_info`) → **connect** → **list or
  install** → **reboot wait**. Sending a command or observing a restart is never
  treated as success; only the explicit terminal markers are (see Monster_OTA.md).
- The precheck has no terminator of its own, so the monitor task decides once its
  output has been quiet for ~1.5 s.
- On device change the per-device cache is cleared (`ota_rf_state_bind_device`);
  the page also resets it on open. Parallel operations are refused
  (`ota_rf_state_begin_op`). After the C5 reboots, the normal detection cycle
  re-reads the version and `subghz_status`.

## 7. Module boundary

The pure logic lives in [`main/ota_rf.c`](../main/ota_rf.c) /
[`main/ota_rf.h`](../main/ota_rf.h) — no LVGL or ESP-IDF — so the tests exercise
the real parser and decision code the firmware links, not a copy. `main.c` holds
only the LVGL glue (the precheck phase in `ota_monitor_task` /
`ota_precheck_decide`, routing in the button callbacks, and the confirm dialog).

See [ota-test-coverage.md](ota-test-coverage.md) for the test-to-requirement map.
