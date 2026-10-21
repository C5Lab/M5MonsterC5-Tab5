# Distributed Crack Workers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Tab5 distribute dictionary cracking across its local cores and every compatible JanOS Monster while keeping Tab5 as primary storage.

**Architecture:** A portable `hs_crack_remote_core` module owns response parsing, hexadecimal decoding, and deterministic byte shards. `main.c` owns UART transports, reverse FTB upload, worker discovery/lifecycle, and integration with the existing local crack coordinator. Full wordlist CRC values are cached against the existing fast wordlist fingerprint.

**Tech Stack:** ESP-IDF, FreeRTOS, UART/USB CDC transports, FATFS, LVGL, portable C host tests.

**Spec:** `docs/superpowers/specs/2026-09-18-distributed-crack-workers.md`

## Global Constraints

- Tab5 is the only source of truth; JanOS copies are caches.
- The wire prefix remains `CRACK/1`, capability protocol 2 provides
  `safe_offset`, and binary framing is `FTB\x01`.
- Grove, USB, and M-BUS workers are optional and auto-discovered.
- Generic guesses stay local; only SD wordlists are sharded.
- No commits and no full firmware build by Codex.

---

### Task 1: Portable protocol and shard core

**Files:**
- Create: `main/hs_crack_remote_core.h`
- Create: `main/hs_crack_remote_core.c`
- Create: `tests/hs_crack_remote_core_test.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Produces: `hs_remote_parse_line()`, `hs_remote_hex_decode()`,
  `hs_remote_make_shards()`, and remote lease accounting helpers.
- Consumes: `[CRACK/1]` response lines and a wordlist byte size.

- [ ] Write tests for capability, READY, STATUS, DONE/found parsing, malformed lines, HEX decoding, and gap-free shards.
- [ ] Run the test without the module and confirm compilation fails.
- [ ] Implement the minimal portable module.
- [ ] Run with `-Wall -Wextra -Werror -fsanitize=address,undefined` and require PASS.

### Task 2: Reverse file transfer and CRC cache

**Files:**
- Create: `main/hs_crack_remote.h`
- Create: `main/hs_crack_remote.c`
- Modify: `main/hs_crack_cache.h`
- Modify: `main/hs_crack_cache.c`
- Modify: `tests/hs_crack_cache_test.c`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Produces: persistent `hs_crack_sync_crc_lookup/store()` and transport-neutral upload state helpers.
- Consumes: the existing wordlist fingerprint, local file stream, and JanOS READY fields.

- [ ] Add failing tests proving full CRC is reused only for an exact wordlist fingerprint.
- [ ] Implement atomic CRC-cache persistence.
- [ ] Implement probe/receive/reset and FTB sender callbacks with resume-prefix verification.
- [ ] Re-run cache and remote-core host tests.

### Task 3: Multi-worker orchestration

**Files:**
- Modify: `main/main.c`

**Interfaces:**
- Consumes: detected Grove/USB/M-BUS flags, transport functions, extracted HCCAPX records, full wordlist CRC, and the remote protocol module.
- Produces: automatic worker discovery, synchronization, start/status/cancel lifecycle, first-hit propagation, and complete-shard accounting.

- [ ] Serialize extracted records to `/sdcard/lab/handshakes/_crack_worker.hccapx`.
- [ ] Probe capabilities and synchronize capture/wordlist on each detected worker.
- [ ] Divide the remaining wordlist among local and accepted remote workers without line gaps or duplicates.
- [ ] Poll remote statuses while feeding local workers; decode a remote found password and cancel peers.
- [ ] Require three consecutive missed polls before losing a worker lease and
  reset the miss counter on every valid status.
- [ ] On cancellation/error, cancel remotes and locally resume an incomplete
  shard from its confirmed `safe_offset`.
- [ ] Show active, successfully finished, and fallback worker counts separately.

### Task 4: Verification and handoff

**Files:**
- Modify: `tests/README.md`

**Interfaces:**
- Consumes: all new host tests and source changes.
- Produces: exact manual CLI/device test sequence for the user.

- [ ] Run all new host tests and CMake registration checks.
- [ ] Run ESP compiler `-fsyntax-only` for new ESP-bound modules.
- [ ] Review the complete diff for protocol compatibility and cancellation cleanup.
- [ ] Report that the full firmware build remains for the user.
