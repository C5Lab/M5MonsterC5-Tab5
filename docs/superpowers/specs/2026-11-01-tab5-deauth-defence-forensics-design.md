# Tab5 Deauth Defence and Forensics Design

**Status:** Approved for implementation on 2026-11-01
**Scope:** Tab5 production model/UI, native emulator, JS device/bridge, browser acceptance, and documentation
**Excluded:** JanOS, Cardputer ADV, firmware builds, flashing, publishing, commits, and pushes

## Goal

Implement the complete Tab5 controller contract for JanOS autonomous Defence,
then add an optional versioned forensic evidence stream without changing the
existing DEAUTH V2, Trace, CSA, history, CSV, latest-card, sound, or alarm
contracts.

## Architecture

`detector_watch_session_t` remains the authority for detector command, start,
Trace, Defence, Stop, and remote-uncertainty state. Defence markers are parsed
strictly before event parsing and can never become observations.

Forensics uses a separate `detector_forensic_session_t` and a fixed-capacity
PSRAM row array owned by each Tab5 context. Strict `[DEAUTH_FORENSIC/1]` rows
are accepted only while the captured detector session is active on its captured
tab, UART port, transport identity, USB generation, command generation, and
selected BSSID set. Valid forensic rows never call `detector_history_add`,
never increment normal counters, and never reach the sound/alarm path.

The JS device and bridge simulate deterministic protocol records, not RF
physics. Native browser callbacks continue to run the production C parser and
UI. All implementations use fixed capacities and saturating counters.

## Defence contract

- Defence requires selected networks and emits
  `deauth_detector_v2 --only <indices> trace defence`.
- PMKID Lab is a separate confirmed opt-in and appends `pmkid-decoy`.
- Defence never falls back to the active legacy detector. Exact
  `Unrecognized command` or explicit option rejection becomes
  `Defence unsupported`.
- Trigger, provisional, suppression, release, and stopped records are evidence
  or state only. Only exact `tx ... result=ok` increments response totals.
- Exact `All operations stopped.` is the only final Stop acknowledgement.
  Missing ACK, timeout, write failure, disconnect, partial stop text, or stale
  generation preserves visible uncertainty and the last state.
- Every scan, start, reply, stop, retry, and uncertainty transition remains on
  the tab, port, transport, USB generation, command generation, and selected
  BSSIDs captured at Start.

## Forensic wire contract

The only accepted field order is:

```text
[DEAUTH_FORENSIC/1] FRAME=<deauth|disassoc> CH=<1..233|-> BSSID=<mac> TX=<mac> DA=<mac> RSSI=<-127..0|-> REASON=<0..65535|-> SEQ=<0..4095|-> RETRY=<0|1|-> PROTECTED=<0|1|-> FC=<0x0000..0xFFFF|-> TS_US=<0..UINT64_MAX>
```

The parser rejects substring matches, missing/duplicate/reordered/extra fields,
bad or repeated prompt prefixes, truncation, oversize input, invalid MACs,
numeric overflow, and values outside the specified ranges. `-` remains unknown
and is never inferred from another record.

## Bounded forensic model

- Capacity: 128 retained rows per Tab5 detector context, allocated in PSRAM.
- Evict the oldest row on overflow and saturating-increment `local_dropped`.
- Saturating `accepted`, `burst_count`, and `largest_burst` cover the full run.
- Unique BSSID/DA, reason distribution, RSSI min/rounded-average/max, protected,
  unprotected, retry, and unknown counts are computed from retained rows.
- Bursts use consecutive `(FRAME, BSSID, TX, DA)` identity. A key change or a
  gap above 20,000,000 us opens a new burst; exactly 20,000,000 us does not.
- Sequence comparison is per stream over retained rows, modulo 4096. Equal
  sequence plus retry is `Retransmission`; equal sequence without retry is
  `Repeated sequence`; fewer than two comparable known values is
  `Insufficient sequence data`.
- A new run resets only after the production model confirms Start. Stop pending
  and every uncertain terminal condition preserve the summary. Exact final
  `All operations stopped.` freezes the summary; late records cannot revive it.

## UI

The existing DEAUTH/CSA history card layout remains unchanged. The detector
screen adds Defence and PMKID Lab controls, Defence state, and a compact
Forensics summary card. The card always displays
`Evidence only — not transmitter attribution` and opens a bounded evidence
view inside the detector screen. The evidence view provides BSSID, recipient,
reason, and frame filters, neutral row labels, and distinct unsupported,
no-observation, and all-filtered empty states. Unknown or suspicious evidence
does not receive danger styling solely for being unprotected, repeated, or
discontinuous.

## Validation boundary

Production extraction tests prove parser/model/state/transport behavior. JS and
browser tests prove emulator parity and rendered UI behavior. They do not prove
an ESP-IDF build, flashed firmware, RF behavior, JanOS emission of the new
marker, physical UART routing, timing, SD-card writes, or device layout.
