# Screen Rotation

Documentation for the **screen orientation** feature on the **M5Stack Tab5 (ESP32-P4)** —
what shipped, what is still open, and the traps worth knowing before touching it again.

## Summary

The Tab5 panel is natively **720×1280 (portrait)**. The app can now run rotated to 90°, 180°
or 270°, selected from **Settings → Screen Rotation** and applied on the next boot.

Rotation is done **in software** by `esp_lvgl_port`: the panel keeps scanning in its native
720×1280 frame and the pixels are rotated on flush. The MIPI-DSI panel is never reconfigured
(`lvgl_port_disp_rotation_update()` returns early when `sw_rotate` is set), so there is no
dependency on `esp_lcd_panel_swap_xy()`, which this panel does not support anyway.

Rotation is **manual and explicit**. Automatic rotation from the BMI270 was considered and
**dropped** — see [Not planned](#not-planned).

## How it works today

| Piece | Where |
|---|---|
| Setting UI | `show_screen_rotation_popup()` in `main/main.c` |
| Persistence | NVS key `scr_rot` in namespace `settings`, values `0..3` = `LV_DISPLAY_ROTATION_*` |
| Applied at boot | `app_main()`, immediately after `bsp_display_start()` and **before any widget exists** |
| Rotation call | `bsp_display_rotate()` → `lv_disp_set_rotation()` |
| Pixel rotation | `lvgl_port_flush_callback()` in `components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c` |

### Why a reboot is required

`main/main.c` caches **158 `static lv_obj_t *` page and popup pointers** and builds each page
once — see `show_settings_page()`, which only clears `LV_OBJ_FLAG_HIDDEN` when the page already
exists. Changing the resolution on a live tree re-runs flex and percentage layouts, but every
already-built page keeps its pixel-sized widgets. Rebuilding on the fly means making 163
`show_*` pages resolution-reactive; rebooting costs a few seconds and is correct by
construction.

The restart path is deliberately quiet: `audio_silence_for_restart()` mutes the codec,
`set_brightness_gamma(0)` kills the backlight, then 60 ms of settling before `esp_restart()`.
The boot that follows plays a two-note chime in place of the startup melody, keyed on
`esp_reset_reason() == ESP_RST_SW`.

## Done

- Settings tile, dropdown, NVS persistence, apply-at-boot, restart button.
- Clean restart: codec muted and backlight off before the reset.
- **A two-note chime instead of the startup melody** on that reboot (`play_rotation_chime()`,
  E5→A5). The full tune turns a settings change into a jingle, but plain silence reads as a
  fault: a tablet you rebooted on purpose should say something when it comes back. It honours
  Boot sound = OFF like the melody does. Audio is not stuck either way —
  `audio_silence_for_restart()` mutes the ES8388 over I²C and the chip keeps its registers
  across a software reset, but `bsp_codec_init()` ends with `set_volume(80)`, which unmutes.
- **Fixed a latent driver bug**: the 180° branch of the software rotation called
  `lv_draw_sw_rotate()` with the source width and height transposed and the stride taken from
  the other axis, corrupting any non-square flush block. It now goes through the PPA, like 90°.
- Landscape layout work:
  - tile grids scroll vertically instead of clipping — `style_scrollable_tile_grid()` at 7 sites
    in `main.c`, `subghz_grid_scrollable()` at 3 more in `main/screens/`;
  - oversized popups clamped to the display: `popup_clamp_h()` for the tall ones, and
    `popup_clamp_w()` for two PCAP cards that were wider than the panel in portrait — see
    [Pass A](#pass-a--provably-broken-no-hardware-needed--done);
  - the home dashboard footer collapses from two rows to one.
- **`popup_clamp_h()` measures the tab container, not the display.** Every popup it guards is
  parented to the tab container, which starts `UI_TOP_OFFSET` (130 px) down the screen. Clamping
  to the full 720 px gave the Network Observer popup a 680 px card inside 590 px of container:
  centred, so half its close button sat behind the tab bar and the attack bar at the bottom was
  clipped away entirely. The budget is now `ver_res - UI_TOP_OFFSET - 40` — unchanged in
  portrait (1110 px, above every popup in the file), 550 px in landscape.
- **Network rows run on one line on a wide screen.** The scan list and the Network Observer
  stacked BSSID/channel/band/security/RSSI, then MFP + uptime, then vendor under the SSID — three
  lines drawn for a 704 px wide list. In a 90/270 orientation that list is 1264 px wide, so the
  run now sits *beside* the SSID on a single line, and the row is one line instead of three.
  `format_network_info()` builds the text (it replaced six drifting copies of the same format
  string) and `style_network_row_text()` does the layout, with the SSID in a fixed 240 px column
  in the scan list and 300 px in the Observer, dotted when it is too long.
- **ESPShark in landscape.** The scrolling strip generalised into `create_button_strip()`
  (fixed-size children, centred when they fit, sideways scroll when they do not) and applied
  where the wide screen was stretching things:
  - the hub's two source cards were `lv_pct(49)` — 620 px each in landscape, a billboard for two
    lines of text. Fixed at 460 px and centred in a strip, which also means a third source
    scrolls instead of wrapping into a row the page cannot afford;
  - the PCAP viewer's nine analysis buttons were three stacked full-width rows, 138 px of a
    590 px page, each button stretched to ~300 px. One 50 px strip of 152 px buttons, so the
    packet list below gains 88 px. `analysis_row_top/bottom/third` alias the same strip there,
    which leaves the nine call sites saying which row they belong to.
  - **Portrait keeps both layouts as they were** — the stacked rows show all nine buttons at
    once on a narrow screen, and a strip would hide five of them behind a scroll for nothing.
- **The attack action bar is one horizontally scrolling strip in every orientation.** It started
  as a landscape-only fix and turned out to be the better layout on the tall screen too, so the
  stacked 3 + 3 + rest grid (plus the full-width Rogue GITM row) is gone. It cost 314 px of
  portrait height to show the same eleven tiles; the strip costs 82 px. Only the tile size still
  varies with the screen — 126×74 on the tall panel, 118×72 in landscape, matching what
  `create_small_tile()` picks for itself. The height it gives back goes to:
  - **Scan page** — the network list already has `flex_grow`, so it just gets ~230 px taller.
  - **Network Observer popup** — the client list now grows into the free space instead of
    leaving a hole (`lv_obj_set_flex_grow()` on `popup_clients_container`).
  - **Deauth Station popup** — was a hand-tuned 620×520 drawn around the four-row bar; now
    `LV_SIZE_CONTENT` in height, so it shrinks to about 360 px and fits the landscape screen
    without clamping.

## Open work

The order to do it in — the passes are defined in [Screen audit](#screen-audit):

~~**Pass A**~~ (done — the eight statically provable breakages) → **Pass B** (hardware smoke
test: portrait, 90°, 180°, 270°) → **Pass C** (the screen-by-screen walk-through) → **Pass D**
(fix what C found, then the polish items 2 and 3 below) → **Pass E** (portrait regression pass).

The numbered items below are the individual pieces those passes pick up.

### 1. Verify on hardware — Pass B

Cheapest item, and the one gating everything else.

- **180°** — never run since the PPA reroute; a new code path, so this is the real risk.
- **270°** — still on the CPU path; expected to work, but slower.
- **The one-row attack bar in portrait** — the scan list, the Network Observer popup (its client
  list now takes the freed height) and the Deauth Station popup (now `LV_SIZE_CONTENT`).

### 2. Route 270° through the PPA

90° and 180° are hardware-rotated; 270° still calls `lv_draw_sw_rotate()` on every flush block.
The arguments there are correct, so it is a performance item, not a bug. `rotate_copy_pixel()`
already handles every angle — following the existing convention, 270° passes `90`.

### 3. Reconcile the two `large` heuristics

`create_uart_tiles_in_container()` and `create_small_tile()` decide `large` from
`ver_res >= 1000`, while `create_tile()` uses `(ver_res >= 1000) || (hor_res >= 960)`. In
landscape they disagree: the page picks compact gaps and a small footer while the tiles pick
their large size. The combination happens to suit a short wide screen, so this is cosmetic — but
it is an accident, not a decision. `ver_res >= 1000` is open-coded in five places by now
(`create_small_tile`, `create_uart_tiles_in_container`, the two `tall_layout` sites, and
`create_attack_action_bar`, which needs it only to size the strip tiles to match what
`create_small_tile()` chose for itself); one `ui_layout_is_large()` helper would settle all of
them, `create_tile()` included.

### 4. Audit the remaining screens

Only the screens actually exercised so far have been checked, out of **164 `show_*`/popup
functions**. The full inventory, what static analysis already proves broken, and the order to
work through it are in [Screen audit](#screen-audit) below.

### 5. Regressions to watch from the layout changes

- **Rogue GITM** has no dedicated full-width row any more, in either orientation; it is the last
  tile in the strip, distinguished only by its magenta colour. Revisit if that reads badly — a
  wider tile or a separator before it are both cheap.
- **The strip hides tiles off-screen.** In portrait eleven tiles need 1466 px against ~600 of
  content width in the Observer popup, so most of the bar is behind a sideways scroll with only
  an auto-hiding scrollbar to advertise it. Worth watching whether people find Nmap and the two
  GITM entries at all; a fade or arrow on the right edge is the fix if not.
- **The analysis strip overflows by ~190 px.** Nine 152 px buttons plus gaps come to 1432 px
  against 1244 px of page: seven and a half are visible, OBJECTS at the end needs a nudge. Drop
  `PCAP_ANALYSIS_BTN_W` to 136 if that reads as a bug rather than a scroll.
- **The one-line network row dots long SSIDs.** 240 px in the scan list, 300 px in the Observer,
  at font 18 — roughly 20 and 26 characters. Portrait is untouched (the SSID still gets the full
  row width), and the numbers are two arguments to `style_network_row_text()` if they read short.
- **The merged run can still wrap.** Everything after the SSID is one label with `LONG_WRAP` and
  `flex_grow`, so a long vendor string plus a long uptime falls to a second line rather than
  being cut. Rows are `LV_SIZE_CONTENT`, so the list stays correct — it just loses some of the
  density the merge bought.
- **LAST NET** shows an SSID in a card that is ~175 px wide in landscape instead of ~315 px, so
  long names clip sooner.
- The **FILES** card is the tightest in the one-row dashboard: two ~60 px columns plus gap and
  padding land at roughly 142 px inside 159 px of content width.

### 6. Startup melody stutter

If the melody breaks up on a **cold** boot in landscape (not the silent rotation reboot), the
cause is PSRAM bandwidth, not audio: code runs XIP from PSRAM and competes with the LVGL boot
intro — the comment above `audio_play_notes()` already documents this — and rotation adds a PPA
pass to every flush. The fix would be raising the melody task priority or pre-rendering more of
the tune up front.

## Screen audit

Everything below was measured, not guessed: a script walked `main/main.c` and
`main/screens/*.c`, split them into functions, and collected every `lv_obj_set_size()`,
`lv_obj_set_height()` and `lv_obj_set_width()` with a **literal** argument, plus every
`LV_FLEX_FLOW_ROW_WRAP` grid, then cross-checked each against `popup_clamp_h()` and
`style_scrollable_tile_grid()`. Computed sizes (`lv_pct`, `LV_SIZE_CONTENT`,
`ver_res - UI_TOP_OFFSET`) were ignored — they follow the display on their own.

Out of **164 `show_*`/popup functions, 21 carry a fixed dimension over the line.** To redo the
sweep after edits — the line numbers below age fast:

```sh
grep -nE 'lv_obj_set_(size|height|width)\([^,]+, *[0-9]{3,}' main/main.c main/screens/*.c
grep -n 'LV_FLEX_FLOW_ROW_WRAP' main/main.c main/screens/*.c
```

What to measure against:

| Orientation | Usable | What breaks |
|---|---|---|
| Portrait `720×1280` | 720 wide | anything **wider than ~700 px** — and this is the *default* orientation, so a breach here is a bug today |
| Landscape `1280×720` | 720 tall, minus the 40 px `popup_clamp_h()` margin | anything **taller than 680 px** |

### Pass A — provably broken, no hardware needed — **done**

Deterministic, so it went first: the hardware walk-through now only reports things the script
could not see. All eight compile clean against the flags from `build/compile_commands.json`;
none of them has been looked at on the device yet, which is what Pass B is for.

| # | Where | Was | Problem | What was done |
|---|---|---|---|---|
| A1 | `pcap_viewer_tools_cb`, `main.c:45823` | `900×570` | **180 px wider than the portrait screen.** Centred, so ~90 px was cut off each side and two of the eleven tool buttons were unreachable — a bug today, not a rotation regression | new `popup_clamp_w()`; the card scrolls vertically once narrowed, because the 210 px buttons then wrap two per row instead of three |
| A2 | `pcap_viewer_start_extraction`, `main.c:45941` | `860×460` | same, 140 px over | `popup_clamp_w()` + `popup_clamp_h()`; it only holds a status line, a spinner and CLOSE, so it cannot overflow |
| A3 | `subghz_screen.c:357` (Sub-GHz menu, 8 tiles) | grid | `flex_grow` + `ROW_WRAP` + `SCROLLABLE` cleared — the exact pattern that lost the tile rows in landscape | new `subghz_grid_scrollable()` in `subghz_screen.c`, declared in `subghz_internal.h` (`style_scrollable_tile_grid()` is `static` in `main.c` and not reachable from there) |
| A4 | `subghz_scanner_screen.c:362` (frequency tiles) | grid | same | same helper |
| A5 | `subghz_weather_screen.c:435` (sensor tiles) | grid | same | same helper |
| A6 | `show_time_popup`, `main.c:53019` | `600×640` | the tallest unclamped card in the file: fits 680 px with nothing to spare | `popup_clamp_h()`, and it scrolls if the clamp bites |
| A7 | `subghz_text_input_popup.c:109` | `720×240` | exactly the portrait screen width — border and shadow on the screen edge | `hor_res - 40`, capped at the original 720 so landscape does not grow it |
| A8 | `compromised_transfer_show_popup`, `main.c:56497` | `680×340` | 20 px a side in portrait, shadow inside that | `popup_clamp_w()` |

### Pass B — hardware smoke test

Nothing below is worth doing until this passes.

- [ ] **Portrait** — the layout changed here today (one-row attack bar, Observer client list,
      Deauth popup): Scan, Network Observer + its popups, Deauth Station. The network rows must
      still be **three stacked lines** here — the one-line form is landscape-only.
- [ ] **90°** — same three, plus Home and Settings. Specifically: the Observer popup fits between
      the tab bar and the bottom edge with its close button and attack strip both reachable, and
      the scan rows read as one line.
- [ ] **180°** — the PPA reroute has never run. If it corrupts, stop and fix the driver first.
- [ ] **270°** — CPU path, so look at the frame rate, not just the picture.
- [ ] The rotation chime plays on each of those reboots; the full melody plays on a cold boot.

### Pass C — the walk-through

One orientation at a time, tile by tile. For each screen: does anything fall off the bottom, can
every control still be reached, does any list lose its scrollbar. **Home tab** (11 tiles):

- [ ] WiFi Scan & Attack — `show_scan_page`, `main.c:17523` *(changed today)*
- [ ] Global WiFi Attacks — `show_global_attacks_page`, `main.c:49874`
- [ ] Compromised Data — `show_compromised_data_page`, `main.c:36308` → Evil Twin passwords,
      Portal data, Handshakes, Wardrive files, **ESPshark hub (card strip)**, PCAP captures,
      **PCAP viewer — analysis strip, and the Tools / Extract cards (A1/A2)**
- [ ] Deauth Detector — `show_deauth_detector_page`, `main.c:46971`
- [ ] Bluetooth — `show_bluetooth_menu_page`, `main.c:47155` → AirTag scan, BT scan, BT locator,
      Jammer
- [ ] Network Observer — `show_observer_page`, `main.c:20154` *(popup changed today)* → AP radar
- [ ] Karma — `show_karma_page`, `main.c:16186`
- [ ] Wardrive — `show_wardrive_page`, `main.c:31036` (setup popup already clamped)
- [ ] Anti-Surv — `show_antisurv_page`, `main.c:31473`
- [ ] Mesh Recon — `show_zig_recon_page`, `main.c:32851`
- [ ] Sub-GHz — `main/screens/subghz_screen.c` **(A3, now scrolls)** → Quick Scan **(A4)**,
      Hunter, Listen (`WATERFALL_W` is a fixed 800 px, wider than the portrait screen — it only
      ever fitted by scrolling), SD Signals (rename uses the text-input popup, **A7**), Weather
      **(A5)**, Jammer, Tesla, Settings

**INTERNAL tab** (2 tiles):

- [ ] Settings — `show_settings_page`, `main.c:55575` → Scan Setup, Red Team, Screen Timeout,
      Screen Brightness, Screen Rotation, Theme, Time **(A6)**, Screen Lock, Monster OTA,
      Monster SD Admin
- [ ] Ad Hoc Portal & Karma — `show_adhoc_portal_page`, `main.c:39024`

**Popups whose frame fits 680 px but whose contents were drawn for the tall screen.** These are
the ones to open deliberately, because nothing about them looks wrong until you do:

| Popup | Size | Line | Landscape margin |
|---|---|---|---|
| Wardrive blacklist scan | `620×560` | `main.c:30736` | 120 px |
| Handshaker | `550×560` | `main.c:13160` | 120 px |
| Home management overlay | `600×560` | `main.c:27852` | 120 px |
| Evil Twin | `600×550` | `main.c:16705` | 130 px |
| Rogue GITM | `600×540` | `main.c:12561` | 140 px |
| Wardrive blacklist | `560×540` | `main.c:30930` | 140 px |
| OTA scan box | `600×520` | `main.c:55237` | 160 px |
| Theme | `430×500` | `main.c:53111` | 180 px |
| Phishing portal | `600×480` | `main.c:22535` | 200 px |
| Global Handshaker active | `520×470` | `main.c:21971` | 210 px |
| Scan deauth / Rogue AP | `550×450` | `main.c:9337`, `:37057` | 230 px |
| Karma2 / Ad-hoc probes | `400×450` | `main.c:37520`, `:38798` | 230 px |

### Pass D — fix what C found, then the polish

Pass C produces the real work list; do it before the cosmetic items (270° through the PPA, the
`ui_layout_is_large()` cleanup, the melody stutter), because those do not change what the user
sees on a screen that is broken.

### Pass E — portrait regression pass

Repeat Pass C in portrait once. The attack-bar strip and the two popup changes landed in
**both** orientations, so portrait is no longer the "known good" baseline it was.

## Not planned

### Automatic rotation from the BMI270

**Dropped, not deferred.** The sensor is the easy half; what kills it is the reboot.
Auto-rotation would mean the tablet deciding to reboot itself because it was picked up or put
down — mid-scan, mid-attack, taking the UART session to the C5 and every page's state with it.
That is worse than the problem it solves, and hysteresis does not fix it: the cost is not a wrong
decision, it is a *correct* decision landing at a bad moment.

It only becomes a sensible feature after live re-layout works, i.e. after the 158 cached page
pointers and the ~223 fixed-pixel `lv_obj_set_size()` calls are gone. Rewriting 163 `show_*`
pages for that is not on the table either — the reboot costs a few seconds and is correct by
construction. If it ever happens, the IMU is a small addition on top and these notes still hold:

- Sensor at **0x68** on the existing I²C bus (`BSP_I2C_NUM 0`, SDA `GPIO31`, SCL `GPIO32`); the
  INA226 code in `main/main.c` is the pattern to copy for `i2c_master_dev_handle_t`.
- The BMI270 needs a **~8 KB configuration blob** uploaded after power-on before it leaves
  configuration mode — either the Bosch `BMI270_SensorAPI` (~30 KB of flash) or a hand-rolled
  init with the blob in an array.
- Accelerometer-only at 25–50 Hz, low-pass filtered, with hysteresis and a dead zone for the
  tablet lying flat. The sensor axes relative to the panel have to be calibrated on the device.

## Traps

Things that cost time once and should not cost it twice.

- **A popup's ceiling is its container, not the screen.** Popups are parented to the tab
  container, which is `UI_TOP_OFFSET` = 130 px shorter than the display. Size one against
  `ver_res` and in landscape it is centred in a box 90 px smaller than itself: LVGL clips the
  overflow at both ends, so the title bar and the bottom row of controls simply are not there.
  Nothing logs a warning. Use `popup_clamp_h()`.
- **Do not rotate touch coordinates in the BSP.** LVGL already transforms every pointer event in
  `indev_pointer_proc()` (`lv_indev.c`) using the display rotation, and its formulas match the
  inverse of `lvgl_port_rotate_area()` exactly for all four angles. Adding a mapping in
  `lvgl_read_cb()` applies it twice and pushes every touch off-screen — the symptom is a display
  that rotates correctly while nothing at all responds to taps. The touch controllers are
  configured with `x_max`/`y_max` at the native panel size and no swap or mirror; leave them.
- **`flex_grow` overrides `LV_SIZE_CONTENT`.** `lv_flex.c` calls `area_set_main_size()` and sets
  `h_layout = 1` on grown items, so a container with both is sized by the parent, not by its
  content. Its overflow is therefore clipped and needs `LV_OBJ_FLAG_SCROLLABLE` to be reachable —
  this is exactly why the tile grids lost their bottom rows in landscape.
- **The boot side never flashed.** `bsp_display_brightness_init()` configures the LEDC channel
  with `duty = 0`, and `app_main()` paints the base screen dark and flushes it before raising the
  backlight. Only the restart side needed handling.
- **Built-in Montserrat faces are ASCII plus LVGL symbols.** `U+00B0` renders as a placeholder
  box, which is why the rotation dropdown says `Portrait (0)` rather than `0°`.
- **Verify with a real object compile.** The project builds with `-Werror`; `-fsyntax-only` skips
  the optimizer-dependent warnings. Compile the changed file to an object using the flags from
  `build/compile_commands.json`, writing the output outside the build tree.
