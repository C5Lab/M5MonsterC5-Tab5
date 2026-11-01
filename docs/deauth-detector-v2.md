# Deauth Detector V2 on Tab5

The Deauth Detector screen supports JanOS detailed deauthentication records while
continuing to accept the original `[DEAUTH]` records. The existing CSA record
format and view are unchanged.

## Start and connection ownership

**All networks** starts with `deauth_detector_v2`. **Selected networks**
first runs `scan_networks` on the chosen Grove, USB, or MBus connection. The
picker shows SSID, BSSID, and channel. Its one-based indices come only from that
JanOS scan after `Scan results printed.`; an unfinished scan, empty selection,
new scan, or lost connection invalidates the selection. The selected command is
`deauth_detector_v2 --only <indices>`, with at most 64 BSSIDs.

**Channel trace (passive)** is off by default and can be enabled for either
mode. Its value is captured for the session and cannot be changed while the
session is active or its remote state is uncertain. Trace adds one final token:

```text
deauth_detector_v2 trace
deauth_detector_v2 --only <fresh one-based indices> trace
```

Tab5 never sends `--trace`, never inserts `trace` between indices, and never
adds it to a legacy command. With the control off, the commands above remain
exactly the original commands without the final token.

The scan, start command, replies, `[DETECTOR_WATCH]` checks, Stop, and any retry
stay on the captured tab ID, UART port, and transport even if another tab becomes
visible. The screen waits for JanOS to confirm the start and, for detailed mode,
`[DETECTOR_INFO] format=deauth_v2` before showing V2 as running. In selected
mode, running additionally requires every strict `[DETECTOR_WATCH]` proof: the
count/order, channel, complete MAC, and mandatory `SSID=` field are checked
against the fresh scan. The five-second WATCH window starts once the start ACK
and required V2 format marker are both present, so their own latency cannot
consume the WATCH verification time; duplicate markers do not extend it. A
strict WATCH proof may arrive before the start ACK, as it does in current JanOS
Defence startup. Tab5 retains that proof for the same captured session but still
does not report running until ACK, V2 format, and the complete WATCH set all
agree.

An older JanOS may reply with the exact line `Unrecognized command` to
`deauth_detector_v2`. Only that explicit rejection permits a single retry on
the same connection using `deauth_detector` (all networks) or
`deauth_detector --only <indices>` (selected networks). A timeout, disconnect,
ambiguous response, or missing format confirmation does not trigger a retry;
the screen shows that the remote state is uncertain and offers Stop on the
bound connection. The status distinguishes confirmed V2, confirmed legacy
fallback, and an uncertain remote state. The legacy `deauth_detector` command,
including its `--only` form, and the legacy event parser remain unchanged.

Trace status is independent of V2 start confirmation. After the V2 format is
confirmed, the normal JanOS sequence is:

```text
[DETECTOR_INFO] format=deauth_v2
[DETECTOR_TRACE] armed
[DETECTOR_TRACE] confirming_bssid=<mac> first_channel=<channel> window_ms=1000
[DEAUTH_V2] ...
[DEAUTH_V2] ...
[DETECTOR_TRACE] locked_channel=<channel>
[DETECTOR_TRACE] released_channel=<channel> reason=idle idle_ms=20000
[DETECTOR_TRACE] armed
```

The final tune can instead report:

```text
[DETECTOR_TRACE] lock_failed_channel=<channel> error=<name>
```

The screen distinguishes waiting for JanOS to arm Trace, armed, confirming a
specific BSSID, locked, released and rearmed after idle, failed and ready for
retry, unsupported, and dropped for legacy fallback. `confirming_bssid` is
accepted only with a valid MAC, `first_channel` in 1..255, and
`window_ms=1000`. Established UART/log prefixes are accepted. An idle release
is accepted only as the exact
`released_channel=<1..255> reason=idle idle_ms=20000` record. Every
`DETECTOR_TRACE` line is consumed before event parsing; malformed, late, or
out-of-session lines remain non-events and cannot revive a stopped or uncertain
session.

Trace status never enters history, counters, CSV, sound, alarm output, or the
latest-observation rows. V2 observations continue through the normal event path
during confirmation, and Tab5 never infers a lock from their `CH` field. A
failed tune does not stop monitoring; JanOS returns to the armed/retry state and
a later eligible DEAUTH may start a new confirmation. A confirmed Stop clears
all Trace state. If Stop is not confirmed, Tab5 preserves the remote-state
uncertainty and the last visible Trace state. CSA records and records rejected
by the selected-BSSID filter cannot start or score confirmation.

If the bound UART link is lost during an active or already uncertain session,
Tab5 invalidates the scan selection but preserves the last visible Trace state
and labels it as remotely uncertain. Reconnecting does not silently claim that
the remote detector stopped; a confirmed Stop is still required to clear it.
After Grove/MBus reconnect, Stop retry reclaims the captured tab and port. USB
retry additionally requires the captured connection generation, so a newly
enumerated/different USB connection is not treated as the original remote
session and the uncertainty remains visible.

While locked, only an eligible DEAUTH for the locked BSSID refreshes Trace
activity. After 20000 ms without one, JanOS reports the released channel and
rearms Trace in that order. Tab5 clears the active lock/confirmation, resumes
showing the armed/hopping state, and retains the released channel as context
until the next confirmation begins. Other BSSIDs, CSA records, and filtered
records do not postpone the idle release. The following `armed` marker is
idempotent, and firmware that implements only the older `locked_channel` or
`lock_failed_channel` markers remains compatible.

Only the exact `Unrecognized command` response permits the existing one-time
legacy fallback, which is sent without trace and labeled accordingly. Older V2
firmware may parse the final `trace` token as an invalid detector/watch index;
Tab5 reports **Trace unsupported** and does not pretend the detector is running
or retry legacy in that case.

## Defence

**Defence** is an opt-in controller mode for selected networks only. It forces
Trace on and locks that control until Defence is disabled, then sends this exact
command on the captured connection:

```text
deauth_detector_v2 --only <fresh one-based indices> trace defence
```

**PMKID Lab** is a separate, explicitly confirmed lab opt-in and appends the
single final token `pmkid-decoy`. It cannot be enabled without Defence. All
networks mode, an empty/stale selection, or an unconfirmed PMKID opt-in is
rejected locally.

Defence has no active legacy fallback. Exact `Unrecognized command` and explicit
Defence-option rejection produce **Defence unsupported by this JanOS; no passive
fallback started**. A timeout, disconnect, write failure, partial Stop text, or
generation mismatch remains visibly uncertain; none of these responses permits
another start command on a different tab or transport.

Strict `[DETECTOR_DEFENCE]` records describe armed, trigger, provisional,
transmit result, suppression, release, and stopped state. Fields must appear in
their defined order with valid values and no extras. The selected BSSID set is
checked for BSSID-bearing records. These records are always consumed before
event parsing and never enter V2/CSA history, CSV, sound, alarms, or normal
counters. Trigger/provisional state is not proof of transmission. Only an exact
`tx=... result=ok ... error=none` record increments confirmed responses; an
exact error record increments errors. A transmit result must follow a matching
trigger and same-BSSID provisional record; it consumes that decision. Release
must then match the accepted response and BSSID. Duplicate, stale, cross-BSSID,
wrong-stage, or unauthorized PMKID sequences are consumed without changing
Defence state or counters.

A monitor-only `decision=observe` record remains telemetry. After a confirmed
decoy TX or its idle release, it does not replace the last successful campaign
state; only a later `decoy_4way` or authorized `decoy_pmkid` decision begins a
new response campaign. This matches JanOS runs where a monitored PMKID probe is
reported between two independent four-way decoy responses.

The session owns the captured `tab_id_t`, UART port, Grove/USB/MBus transport,
USB link generation, command generation, scan generation, and selected BSSIDs.
Only exact `All operations stopped.` finalizes Stop and clears active Defence
state. `Deauth Detector stopped.` is partial information only. If final Stop is
missing, Tab5 preserves the last Defence state and labels the remote state as
uncertain.

## Wire record and interpretation

```text
[DEAUTH_V2] CH: <ch> | BSSID: <mac> | TX: <mac> | DA: <mac> | RSSI: <rssi> | REASON: <0..65535 or '-'> | SSID: <text>
```

JanOS sends one `DEAUTH_V2` record per received deauthentication frame in V2
mode, without a duplicate legacy `DEAUTH` record. Tab5 parses the
fields in that order and treats SSID as the final text field, so parentheses in
an SSID remain part of the name. `Output:` examples, status lines, and malformed
records do not become observations. Conversely, status-looking text inside the
final SSID field remains ordinary SSID text and cannot confirm Start, WATCH, or
Stop.

| Field | Meaning |
| --- | --- |
| `BSSID` | Network identity reported for the frame. Selection and grouping use the MAC address, not SSID alone. |
| `TX` | Claimed transmitter address in the received frame. It can be spoofed; it is not a verified attacker MAC. |
| `DA` | Frame destination/recipient address. `FF:FF:FF:FF:FF:FF` is broadcast and does not identify one victim. |
| `REASON` | Numeric reason code carried by an unprotected frame. `-` means the protected/encrypted unicast reason is unavailable; Tab5 displays it as unknown and does not invent code `0`. It is not proof that a station disconnected. |
| `CH`, `RSSI` | Channel and signal level observed by the receiver. |
| `SSID` | Display name associated with the BSSID; it is not a unique network identity. |

Each V2 row shows **Claimed TX**, **Frame recipient (DA)**, and **Reason** below
the existing channel, network, BSSID, and RSSI columns. CSA rows remain
observed channel-change announcements; they do not prove an attack or a channel
change and do not trigger the DEAUTH sound.

Channel trace is passive. The first eligible DEAUTH starts a 1000 ms confirmation
window for its claimed BSSID. Only observations for that BSSID are scored, while
other accepted V2 observations are still displayed normally. For 2.4 GHz, JanOS
checks the first receive channel plus or minus two channels, clamped to 1..14.
For 5 GHz it checks the previous, current, and next entries in the JanOS detector
channel list. The winner is selected by frame count and then average RSSI; an
exact tie prefers the BSSID's scan-cache channel, then the first receive channel.
Only JanOS's final `locked_channel` confirms the lock.

Once confirmed, Trace stops only detector-initiated channel hopping; connected
Wi-Fi may still control the physical channel. A trace lock, a claimed TX
address, or a DEAUTH observation is not proof of an attacker or a client
disconnection. An idle release resumes detector-initiated hopping and permits a
later eligible DEAUTH to begin a fresh confirmation and lock cycle.

## Optional forensic evidence stream

Tab5 accepts the following versioned record in exactly this order:

```text
[DEAUTH_FORENSIC/1] FRAME=<deauth|disassoc> CH=<1..233|-> BSSID=<mac> TX=<mac> DA=<mac> RSSI=<-127..0|-> REASON=<0..65535|-> SEQ=<0..4095|-> RETRY=<0|1|-> PROTECTED=<0|1|-> FC=<0x0000..0xFFFF|-> TS_US=<0..UINT64_MAX>
```

Raw records are accepted after the detector's existing normalization: an
optional single `I JanOS: ` log envelope followed by an optional single `> `
serial-prompt prefix. The parser rejects substring matches,
duplicate/reordered/missing/extra fields, repeated or different prefixes,
structurally truncated records, lines of 512 bytes or more, invalid MAC
addresses, numeric overflow, and out-of-range values. `-` means unknown and is
never inferred from another record.

Forensics is a separate, bounded evidence model. A context retains at most 128
rows in PSRAM; overflow evicts the oldest row and increments `local_dropped`.
Accepted and burst totals saturate rather than wrap. A burst keeps consecutive
`(FRAME, BSSID, TX, DA)` identity and ends on an identity change or a gap greater
than 20,000,000 microseconds; exactly 20,000,000 remains in the current burst.
Sequence deltas are modulo 4096 and per stream. Equal sequence with `RETRY=1` is
shown as retransmission, equal sequence with `RETRY=0` as repeated sequence, and
missing or non-comparable values as insufficient sequence data. Retained-row
statistics also track reason-code distribution plus retry/protection unknowns;
they never manufacture values for `-` fields.

The summary reports duration, accepted/retained rows, bursts and largest burst,
unique BSSID/recipient counts, retained-row RSSI min/rounded-average/max, retry,
protected/unprotected/unknown counts, and local drops. **View evidence** provides
BSSID, recipient, reason, and frame filters. It always states **Evidence only —
not transmitter attribution** and uses neutral labels such as **Claimed TX** and
**Frame recipient**. The data does not prove an attacker, disconnection, or a
successful Defence action. If bounded-row eviction removes the selected BSSID,
recipient, or reason, that vanished filter returns to **All** instead of leaving
an invisible stale predicate active.

Rows are accepted only for the confirmed active detector session and its
captured tab, port, transport/link generation, command generation, scan
generation, and allowed BSSID set. They bypass normal history, CSV, V2/CSA
counters, latest groups, sound, and alarm paths. A new confirmed Start resets
the forensic session. Stop pending or an uncertain Stop preserves it; exact
`All operations stopped.` freezes it and late markers cannot revive it.

Current JanOS does not yet emit `[DEAUTH_FORENSIC/1]`. Until it does, the screen
continues to operate normally and shows **Forensics unavailable for this
firmware** after an inactive/finished session with no accepted marker. During a
confirmed active run it shows **No forensic observations**. Host and emulator
tests use deterministic fixture records to exercise this optional path.

## History and SD export

V2 observations group by BSSID, TX, DA, and reason identity. Numeric reason
codes and the explicit unknown/encrypted reason form different groups. Legacy DEAUTH records
form separate groups because they lack TX, DA, and reason; CSA uses its own
grouping. The screen keeps the latest 200 groups, their first/last seen times
and occurrence counts, plus separate DEAUTH and CSA event totals. Stop preserves
history. A new confirmed Start clears it; **Clear** also clears it on demand.

**Save CSV** writes a snapshot to `/sdcard/lab/detector/` on the Tab5 SD card.
Existing column positions remain intact. V2 fills the existing `claimed_tx`
column and the appended `frame_da,reason_code` columns. The final
`reason_status` column is `known` for a numeric code and `unknown_encrypted`
when JanOS sent `REASON: -`; the numeric CSV cell is empty in the latter case.
Legacy DEAUTH leaves these fields empty; CSA keeps its existing claimed TX value
and leaves DA and reason empty. Saving does not clear the on-screen history.

## Validation boundary

The host parser, history, watch-state, transport-binding, Defence, Forensics,
and emulator tests can check the wire contract, bounded statistics, UI state
transitions, isolation, and CSV serialization. Browser tests exercise the native
Tab5 C/LVGL slice through deterministic JS device/bridge streams. These checks
do not establish ESP-IDF firmware build success, JanOS emission of the forensic
marker, radio behavior, physical UART routing/timing, SD-card writes on hardware,
or behavior on a flashed device.
