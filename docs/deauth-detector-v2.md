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
consume the WATCH verification time; duplicate markers do not extend it.

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

The host parser, history, watch-state, transport-binding, and emulator tests can
check the wire contract, UI state transitions, and CSV serialization. Browser
tests exercise the Tab5 emulator. These checks do not establish firmware build
success, radio behavior, physical UART operation, SD-card writes on hardware,
or behavior on a flashed device.
