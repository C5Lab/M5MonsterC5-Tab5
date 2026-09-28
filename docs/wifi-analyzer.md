# Wi-Fi Analyzer for Tab5

Wi-Fi Analyzer displays complete JanOS WFA/1 scan snapshots on the Tab5. It is a
separate screen and reader: the existing **WiFi Scan & Attack** screen, legacy
scan result buffers, network selections, and Flipper-compatible commands keep
their existing paths.

The implementation is present in source. Firmware compilation, emulator builds,
flashing, serial hardware tests, C test-harness execution, target runtime tests,
and rendered UI validation were **not performed** during this implementation.
The device acceptance checklist below remains pending.

## Open and scan

Select an external **Grove**, **USB**, or **MBus** port, then open the
**WiFi Analyzer** home tile. The analyzer is not available on the **INTERNAL**
tab. Connect a MonsterC5 running JanOS with WFA/1 support.

1. Open **Filters / Setup** if you want to change acquisition settings.
2. Choose **Scan**. Tab5 checks capabilities and requests a fresh snapshot.
3. Read the channel/RSSI view and access-point list after a complete scan arrives.
4. Choose **Stop** to cancel acquisition and repeat scheduling. **Back** requests
   the same cancellation and leaves after transport ownership is released.

The header stays visible while the body scrolls. The settings/filter area expands
above the full-width chart in both portrait and landscape; its controls wrap to
the available width. Scroll farther down for the sortable AP list and requested
channel scope. The AP list also has its own scroll area.

Each external port retains its acquisition settings, local filters and last
complete result independently for the current app session. These are not saved
to NVS. Switching away stops acquisition. Returning does not silently restart a
scan: choose **Scan** to acquire a fresh result after reconnection.

## Acquisition settings

Acquisition settings are copied when **Scan** starts and are disabled while the
worker is active, including the interval between repeat scans. Use **Stop** before
changing them.

| Setting | Choices and behavior |
| --- | --- |
| Scan band | 2.4 GHz, 5 GHz, or both; default both |
| Profile | Quick, Detailed, or Passive; default Quick |
| AP record limit | 64 by default, or 128 maximum |
| Channels CSV | Empty uses the driver's candidate plan; an explicit list such as `1,6,11` restricts the requested channels |
| Repeat after completion | Off by default, or 2, 5, or 10 seconds |

The channel editor accepts up to 159 characters. Explicit channels must be valid,
unique, separated by commas without spaces, and belong to the selected scan band.
The driver may further restrict the requested plan. Invalid requests are rejected
without replacing the last good result.

Repeat scans are scheduled by Tab5, not JanOS. Tab5 waits for the previous scan's
terminal response, a short release delay and an idle acknowledgement before
waiting the chosen interval. A failed scan stops the repeat loop. Scans do not
overlap, so the interval is not a fixed wall-clock sampling period.

The record limit bounds returned APs, not the total APs found by JanOS. A result
can report, for example, 64 returned out of more than 255 found. The UI shows the
full found count and marks a truncated result explicitly.

## Local display filters

These controls only change the current view. They do not send commands or modify
the committed scan snapshot:

- Band: all, 2.4 GHz or 5 GHz.
- Primary channel: all or one supported channel.
- Width: all, Unknown, 20, 40, 80, 160 or 80+80 MHz.
- Minimum RSSI: slider from -100 to -20 dBm.
- Security: all or a canonical authentication label, including Unknown.
- SSID/BSSID search: case-insensitive ASCII substring matching.

**Reset filters** restores all bands/channels/widths/security, clears the search,
and sets the RSSI threshold to -100 dBm. It leaves acquisition settings alone.
The summary always distinguishes filtered APs, returned APs and the total found.

Tap **RSSI**, **Channel** or **Name** above the AP list to sort. Tap the same
control again to reverse the order; the arrow shows the current direction.
RSSI defaults to strongest first. AP identity and color come from BSSID, so an
SSID change or different row order does not reassign its identity.

Tap a row or a network footprint on the chart to highlight that AP and open the
same details panel **below the AP list**. The list retains its 320 px viewport;
selection updates borders in place instead of deleting and recreating rows.
Details expand/collapse over 220 ms into a bounded 248 px card, with a fixed
header/Close button and independently scrollable text. The page does not jump
when selecting an AP. The selection button under the chart shows its SSID,
BSSID and RSSI; tap **Details below** there to scroll explicitly to the panel.
Close collapses the panel while retaining the selected AP highlight.

Chart taps use the current band, filters, zoom window and animated trace height.
Tap inside a footprint or near its top edge; unknown geometry is selectable near
its vertical primary-channel marker. Nearby overlapping traces cycle in the
current list order on repeated taps. The selected trace draws last, and its row
is revealed by scrolling only the list, leaving the chart viewport unchanged.
Dragging to scroll and tapping empty chart space do not change selection.

Details include SSID, original
SSID bytes, BSSID, raw RSSI, primary channel/frequency, band, security, PHY flags,
width, secondary-channel relationship and center frequencies. A hidden SSID is
labelled explicitly. AP selection does not call legacy network-selection,
credential, enrichment or attack actions.

Text editors use the existing app keyboard. A connected physical keyboard uses
the same text-input and navigation integration: Escape activates the appropriate
Cancel or Back control, and the AP list supports row browsing/activation.

## Propose a channel

The **WiFi Analyzer** home tile is the last tool tile on each external-port
dashboard, after optional Sub-GHz and NFC tools. Inside the analyzer, choose
the chart band and tap **Propose channel** beside the zoom controls. This opens
a local advisory panel; it does not start a scan or configure an AP/router.

The advisor compares **20 MHz router settings only**:

- 2.4 GHz: channels **1, 6 and 11**.
- 5 GHz: channels **36, 40, 44 and 48** by default. The optional **36-165
  (incl. DFS)** pool compares the 25 conventional 20 MHz channels in that range
  and labels DFS candidates. This pool is not a country-specific allowed list.

Use only channels offered by the router under its configured country and
operating conditions. A 40/80/160 MHz router configuration needs a different
comparison; the current advisor does not recommend a bonded channel block.
Wide *neighbor* APs are included using their reported full footprint, including
both disjoint 80+80 MHz segments.

The top three candidates show a relative score, known overlapping AP count and
strongest known overlapping RSSI. Lower scores indicate less observed overlap.
Equal best scores are explicitly reported as ties; channel-number ordering
does not mean the first tied candidate is better. An empty scan reports no
observed APs and no inferred preference, rather than claiming a free channel.

All records in the selected band of the committed snapshot are considered,
regardless of display filters or zoom. **Own access point → Exclude selected
AP** removes only the explicitly selected BSSID. First select your own AP from
the current band; other BSSIDs, including virtual SSIDs from the same physical
router, remain included. The default is **Include all APs**. Results update
when a new snapshot, band, candidate pool or selected own AP changes. Closing
the panel retains the plot and selection.

The advisor declines to rank a missing, truncated, stale or older-than-five-minute
snapshot. It also requires the requested scan plan to contain every channel
1-13 for 2.4 GHz, or all 28 WFA 5 GHz channels 36-177 for 5 GHz. This deliberately
rejects a scan of only 1/6/11 or 36-48: adjacent primary channels and wide APs
can affect the candidate frequencies. Recovery is to clear **Channels CSV**,
scan the chosen band or Both bands, and use 128 records if the result was
truncated. A still-truncated scan remains insufficient.

Some JanOS versions intersect the requested plan with configured country or
manual driver channel limits before emitting `begin`. A full default scan in
a 1-11 country, or a restricted 5 GHz plan, therefore still fails this advisor's
conservative coverage gate. The UI explicitly reports this limitation: clearing
CSV only helps a custom scan restriction, and repeating a country-limited scan
does not resolve it. WFA/1 has no authoritative complete-permitted-plan marker,
so the advisor cannot safely distinguish that case from a user-chosen partial
scan. Do not change country settings merely to obtain a recommendation.

These checks verify the *requested* channel plan only. WFA/1 does not provide
actual per-channel dwell/coverage, a router regulatory domain, noise, traffic
load, channel utilization or an airtime measurement. Driver filtering and
missed weak APs remain possible. Consequently every result is a starting point
to verify near the intended router/client location, not a speed guarantee.

### Scoring contract

For each candidate's nominal 20 MHz interval, each known-width AP contributes:

```text
weight = (clamp(RSSI, -100, -20) + 101)^2
contribution = weight * overlapping_MHz
score = sum(contribution)
```

Overlap is the intersection with the reported AP segment(s), capped at 20 MHz
per AP. This is an intentionally simple RSSI-weighted overlap heuristic, **not
linear RF power, measured interference, a percentage or airtime**. Each BSSID
counts once. A width/center-unknown AP adds `weight * 20` equally to every
candidate; it is counted separately from known overlaps, and the ranking is
labelled provisional. It cannot make one channel appear better solely because
its unknown footprint was silently ignored. Scores sort ascending, then by
channel number; tie reporting remains mandatory. At 128 APs the maximum score
is 16,796,160, well within a 32-bit unsigned integer. The C model allocates no
memory and consumes the existing PSRAM snapshot.

The candidate policy draws on the conventional 2.4 GHz non-overlapping plan
described in the [Cisco RF reference](https://www.cisco.com/c/en/us/td/docs/wireless/controller/9800/technical-reference/wireless-rf-reference-guide.html).
Scan/country limitations are documented by [Espressif for ESP32-C5](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c5/api-guides/wifi.html).
Neither source endorses this app's heuristic or verifies an individual capture.

### Advisor validation

Compiler-free checks on 2026-09-28: **81 analyzer checks** (41 reader-reference,
12 advisor-reference, 15 integration-source and 13 UI-source checks), and **34 C
grammar checks**, including the modified dashboard function. The independent
Python advisor reference covers partial/truncated input, empty ties, adjacent
channels, RSSI weighting, wide/disjoint footprints, unknown geometry, explicit
BSSID exclusion, band isolation and DFS labels. Source contracts guard full
snapshot use, no radio commands, stale handling and the last home tile.

Production C cases were added to `tests/wifi_analyzer_model_test.c` but were
**not compiled or executed**. Passing the Python reference and source checks
does not prove C/runtime equivalence, rendering, touch behavior or RF accuracy.
After the user's build, compare advisor rankings with captured WFA data; test
both bands, both pools, own-AP exclusion, filters/zoom independence, ties,
unknown-width APs, stale/expired/truncated/partial scans, and reopening the
screen. Check that the analyzer tile stays last with and without optional
Sub-GHz/NFC hardware.

## Read the chart correctly

The chart defaults to **2.4 GHz**. Its selector switches to **5 GHz** independently
of acquisition and local band filters. Selecting an AP switches the chart to that
AP's band. A both-band scan therefore retains both bands while displaying one
band's geometry at a time. **6 GHz is not supported.**

For **5 GHz**, the **5 GHz range** selector defaults to **Auto**. Auto fits the
APs matching the display filters, includes complete known-width footprints (both
80+80 segments) and unknown-width primary markers, and adds frequency padding.
The view spans at least 200 MHz so a single AP retains channel context. With APs
only around channels 36 through 64, the range is 5150-5350 MHz instead of squeezing
them into the left edge of a 5150-5900 MHz chart. **Full band** restores that full
range. Empty filtered views also use the full range. The axis caption always
shows its actual MHz range; tick labels thin out when needed to avoid collisions.
The setting is local display state and does not initiate a scan.

**Zoom** offers 1x, 2x and 4x in both bands. It magnifies the current base range
(Auto/Full band on 5 GHz, 2400-2500 MHz on 2.4 GHz). The left/right buttons pan
by half a viewport, stop at the base-range edges, and disable when further
movement is unavailable. **Fit** returns to 1x. Changing the plot band or the
Auto/Full band choice also resets zoom. Selecting an AP from the list recenters
an already zoomed view on that AP's primary channel; chart selection preserves
the current window so repeated taps can cycle overlapping APs. The caption reports the actual MHz
window and zoom factor; footprints crossing its edges are clipped, and primary
markers outside it are not drawn in the margins.

Zoom does not filter the AP list or change primary-channel counts. Use the local
SSID/BSSID, RSSI, channel or width filters when many APs share the same block and
you want fewer overlapping traces. Filters affect the 5 GHz Auto base range;
zoom and panning then operate inside that range. No display control sends a scan
command or changes a stored AP measurement.

**Filters / Setup → Chart animation → Grow / Off** controls a 480 ms ease-out
reveal. With Grow enabled (default), footprints rise from the chart baseline to
the measured RSSI when a new successful snapshot arrives, the chart band is
changed, or a retained page is reopened. There is no continuous wobble and no
overshoot. List/detail RSSI values remain exact throughout. Status/age updates do
not restart the animation. Animation is canceled on hide/delete or when the
chart moves offscreen. The same Off setting disables details-panel transitions;
neither animation changes AP row heights.

Device validation after the user's build: test 2x/4x, pan at both edges, Fit,
band/range changes, filters, and selecting an AP while zoomed. Check Grow/Off,
repeated scans, Back/tab switching during animation, and 128 APs for frame time
and memory stability. This session does not compile or render the target UI.

The details/selection follow-up passed **67 compiler-free analyzer checks**
(41 reference-reader tests, 13 integration source contracts, 13 UI source
contracts) and **33 C grammar checks**. Three new source contracts were observed
failing before implementation and passing afterward. These check code structure
and API availability, not LVGL rendering or touch behavior. Device acceptance:

- Select a row near both ends of the list, change selection, close/reopen details
  and inspect long SSIDs/raw SSID bytes. The list height must stay unchanged and
  all details must be reachable within the panel.
- Tap overlapping 80 MHz footprints and an unknown-width marker at 1x/2x/4x;
  confirm BSSID identity agrees across the chart, selection button, row and
  details. Repeat taps should cycle close traces without moving the plot window.
- Tap blank space and drag the chart; neither should select an AP. Apply filters
  and verify hidden APs cannot be selected from the chart.
- Switch Grow/Off, rapidly open/close the panel, leave/delete/reopen the page
  mid-transition, and receive a new scan while an AP is selected. Verify no
  partial final panel, stale callback, unintended page jump or changed radio state.

The 2.4 GHz view remains fixed at 2400-2500 MHz. The frequency scale is linear in
both modes, including gaps between channel groups. Multiple APs on the same
80 MHz block correctly share their horizontal footprint even when their primary
channels differ; color and primary ticks distinguish their records.

The range follow-up passed 62 compiler-free analyzer checks and 33 C grammar
checks. The authored C range cases cover the photographed low-channel cluster,
full-band override, unchanged 2.4 GHz, disjoint segments, filters, the upper band
edge and empty data. They were not compiled/executed. After the user's Tab5 build,
compare Auto/Full band on the same snapshot, toggle display filters, and check
portrait/landscape label spacing. The supplied device photos predate this change.

Horizontal positions use frequency in MHz, with channel numbers as ticks. This
preserves 2.4 GHz overlap and the gaps between 5 GHz channel groups. The vertical
axis is RSSI from -100 to -20 dBm; values outside that range are clamped only for
drawing, while the list/details retain the reported RSSI.

When complete width and center metadata is available, a colored footprint spans
the reported frequency range and reaches the AP's RSSI. An 80+80 MHz AP has two
separate segments. A primary-channel tick marks its position. Unknown or
incomplete geometry is drawn as a primary marker; it is not assumed to be 20 MHz.

Width and center metadata are SDK derived and **unverified on target**. A plausible
footprint is not proof that the SDK metadata is accurate for that AP. The channel
summary counts observed AP primary channels in the filtered, selected-band view.
It is not spectrum power, interference strength, measured airtime, or a utilization
percentage. No own-AP marker or historical time range is inferred.

The 160 MHz reader requires a full-channel center of 5250, 5570 or 5815 MHz in
the supported 5 GHz plan. It rejects an old JanOS record claiming 160 MHz with
the primary-80 center 5290 MHz. Updated JanOS publishes ambiguous geometry as
all-null, preserving the AP as an unknown-width primary marker. Build the updated
JanOS before validating this Tab5 version. The raw SDK cause remains unconfirmed.
The follow-up passed 61 analyzer Python/reference/source checks (41 contract,
13 integration, 7 UI) and 33 C grammar checks. C harness execution and the updated
firmware/device checks remain pending. The earlier verification totals below
describe the initial implementation, before this regression was added.

The requested channel scope comes from the snapshot's `begin.channels`. It is a
requested, driver-filtered plan, not evidence that every frequency was visited or
that the scan measured complete RF coverage.

## Status and recovery

Only a valid successful end publishes a new snapshot. A successful empty scan
replaces the previous result with an empty result. Cancellation, timeout, parser
failure or disconnect retains the last complete result and marks it stale as
appropriate. The status line includes its age.

| Status | Action |
| --- | --- |
| WFA/1 unavailable | Update JanOS to a version supporting the analyzer. The legacy scanner remains available; no empty result is inferred from the capability timeout. |
| Transport busy / disconnected | Stop the conflicting operation or restore the connection, then choose Scan again. |
| `no_psram` | Analyzer bulk storage was unavailable. Stop other memory-heavy work and reopen the screen before retrying. There is no internal-RAM fallback. |
| Invalid/incomplete data or timeout | Keep the stale result for reference and retry after the worker finishes recovery. |
| JanOS restarted | The previous result remains stale. Choose Scan to establish a fresh connection and scan. |
| JanOS did not confirm idle / radio fault | Restart or reconnect JanOS as directed. The worker keeps exclusive receive ownership while the remote scan may still be active. |
| `legacy_radio_uncertain` | Restart JanOS before attempting analyzer admission again. |

Back may remain in a stopping state until JanOS confirms idle or the connection
is lost. The application does not force-delete a worker that may still own the
transport. Other physical ports remain separate.

## Implementation boundaries

`main/wifi_analyzer_model.c` implements a standalone bounded WFA/1 reader and
portable AP model. Snapshot lines use `[WFA1]`; capability, status and error
traffic use the separate `[WFACTL1]` control prefix. The reader preserves partial
reads, rejects malformed or overlong frames, checks transaction identities and
record order, and commits only a complete successful snapshot.

The parser limits a complete line to 1024 bytes including its prefix, excluding
CR/LF. Overflow discards through the next line boundary. AP sequence numbers are
transaction-local and never become legacy `select_networks` indices.

`main/wifi_analyzer.c` provides one lazily allocated session per external port.
Its worker owns transport acquisition, capability probing, scan requests,
deadlines, cancellation, idle synchronization and repeat scheduling. Host hooks
join existing console and exclusive-operation ownership rather than introducing
a second independent reader on the same port.

Session/reader/working/committed bulk storage and the separate UI snapshot use
PSRAM only. The UI copies a view under a nonblocking lock on a 250 ms LVGL timer;
it skips a busy copy instead of blocking the UI. All LVGL updates occur on the UI
thread. Deleting a screen releases its timer and snapshot and requests a
cooperative stop; it does not delete a running worker or free its session.

The screen implementation is in `main/screens/wifi_analyzer_screen.c`.
`main/main.c` owns the external-port tile, navigation and transport integration.
Legacy parser/result paths remain separate.

## Protocol commands

The commands below describe the JanOS interface. They were not sent to hardware
as part of this implementation. Do not run a second serial consumer while the
Tab5 analyzer owns that port.

```text
wifi_analyzer caps
wifi_analyzer scan --band both --profile quick --limit 64
wifi_analyzer scan --band 2.4 --channels 1,6,11 --profile detailed --limit 128
wifi_analyzer scan --band 5 --profile passive --limit 64
wifi_analyzer status
wifi_analyzer stop
wifi_analyzer clear
```

`stop` cancels only analyzer work. `clear` releases analyzer cache resources while
idle and leaves legacy results alone; the current Tab5 screen does not expose a
Clear action. There is no v1 cached-results replay command. See the sibling JanOS
repository's `ESP32C5/docs/wifi-analyzer-protocol.md` for the full wire contract.

## Compiler-free checks

From the repository root:

```text
python -m unittest discover -s tests -p "test_wifi_analyzer*.py"
python tools/wifi_analyzer_contract.py tests/fixtures/wifi_analyzer_v1.ndjson
python tools/check_wifi_analyzer_syntax.py
```

The grammar checker additionally requires Python packages `tree-sitter==0.26.0`
and `tree-sitter-c==0.24.2` available to that interpreter. The other commands use
the Python standard library. To repeat the two legacy source-check suites, run
`python tests/test_scan_network_layout_contract.py` and
`python tests/test_observer_incremental_ui_contract.py`. Avoid unrestricted test
discovery when compilation is prohibited: other test scripts compile C harnesses.

The Python contract tests exercise a **Python reference reader**, including
fragmented streams, rejected frames and transactional snapshots. They do not
execute the production C reader, controller, JanOS firmware, Flipper parser, or
ESP-IDF radio driver. Integration/UI tests inspect source contracts, ownership
boundaries, lifecycle rules and installed LVGL API names; they do not render the
screen or prove runtime behavior.

Compiler-free verification recorded on **2026-09-28**:

- 39 analyzer Python contract tests, 12 integration source contracts and 7 UI
  source contracts passed.
- 2 existing scan-layout checks and 5 Observer checks passed: 65 checks total
  across these suites.
- The synthetic capture produced 1 committed snapshot, 2 terminal records and
  0 errors in the Python reference validator.
- The source grammar checker parsed 33 files/fragments with 0 errors. This is
  grammar validation, not C compilation, linking, type checking or execution.

The three existing USB test scripts that generate and compile C were updated
for integration stubs but deliberately not run under the no-compilation
constraint.

`tests/wifi_analyzer_model_test.c` is an authored portable C harness for later
execution. It was not compiled or executed. Passing Python checks must not be
reported as passing C, firmware, emulator, rendering or hardware tests.

## Pending device acceptance

After an explicitly authorized build and device test session, verify:

1. Open the analyzer on Grove, USB and MBus. Confirm it is absent on INTERNAL,
   and that settings/results do not cross ports. Check every supported rotation:
   header access, wrapped controls, chart labels, AP rows, keyboard and editors.
2. Run a default both-band, Quick, 64-record scan. Compare a 2.4 GHz-only scan, a
   5 GHz-only scan, both other profiles and an explicit channel list. Confirm
   the 128-record choice and malformed/cross-band channel rejection.
3. Change each local filter and reset it. Confirm no scan command is sent. Check
   empty success, no filter matches, hidden SSIDs, non-ASCII/display replacement,
   raw SSID bytes, stable BSSID colors, all three sorts and selected AP details.
4. Compare known 20/40/80/160 MHz and 80+80 MHz AP metadata against an independent
   source. Confirm unknown geometry remains a marker and the 5 GHz gaps remain
   proportional to frequency. Do not mark SDK width verified without this test.
5. Exercise a truncated result with more than 255 APs found using an appropriate
   controlled capture or test setup. Confirm filtered/returned/found counts and
   the truncation notice remain distinct.
6. Run each repeat interval; confirm a new request follows terminal completion
   and idle acknowledgement. Stop during scanning, publication and repeat wait.
   Back must wait for safe release; leaving must stop future repeats.
7. Unplug/reconnect USB and restart JanOS during acquisition. Confirm old data
   remains visibly stale, a new Scan uses a fresh connection, and delayed output
   never becomes a legacy scan result.
8. Check unsupported firmware, memory allocation failure, fragmented/malformed
   frames, timeout, missing idle acknowledgement and busy/radio-fault responses
   with a controlled test setup. Confirm bounded parsing, visible recovery and
   continued UI responsiveness.
9. Try conflicting legacy scan/capture/console operations on the same port and
   an independent operation on another port. Check ownership and release. Then
   exercise the old WiFi Scan & Attack, network selection, enrichment and
   Flipper-compatible workflows to verify their existing behavior.
10. Repeatedly enter/leave/delete the page during and after scans. Check task and
    memory usage for leaks, stale pointers, callbacks navigating the wrong tab,
    and accidental worker deletion.

These checks are an acceptance plan, not a report of executed tests.
