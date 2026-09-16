# Power Management Reference

This document describes the power-management behavior of the Tab5 firmware
running on ESP-IDF 5.4.1. It is a behavioral reference for development,
testing, and power-consumption measurements.

## Scope

The implementation reduces display and UI power consumption when the screen is
off while keeping Wardrive collection and touch wake-up active. WPA cracking
always takes priority and runs at full CPU performance.

The implementation does not enable automatic light sleep, tear down the MIPI
DSI display driver, remap the Power button, or change Wi-Fi/radio behavior. It
also does not switch the DSI video source or lower the CPU clock while the panel
driver is initialized. Hardware testing showed that doing so can cause an
unrecoverable DPI underrun and leave the screen blank after wake-up.

## Power profiles

| Profile | Screen | CPU maximum | CPU minimum | Automatic light sleep | Purpose |
|---|---:|---:|---:|---:|---|
| `interactive` | On | 360 MHz | 40 MHz | Disabled | Normal UI operation |
| `screen_off` | Off | 360 MHz | 40 MHz | Disabled | Display dark with expensive UI refreshes deferred |
| `performance` | On or off | 360 MHz | 40 MHz | Disabled | WPA cracking and protected high-load work |

Profile priority is:

1. `performance` claim
2. `screen_off`
3. `interactive`

The `performance` profile therefore remains active even if the display turns
off during WPA cracking.

## ESP32-P4 clock behavior

The project uses the pre-revision-3 ESP32-P4 clock tree selected in the project
configuration. The supported CPU operating points are 360, 180, and 90 MHz.

ESP-IDF power management is enabled, but the MIPI DSI driver holds an
`ESP_PM_CPU_FREQ_MAX` lock while the display driver exists. Consequently, the
configured maximum frequency becomes the effective operating frequency:

- `interactive`, `screen_off`, and `performance`: 360 MHz

Although the configured minimum is 40 MHz, the active DSI lock prevents the CPU
from dynamically dropping below 360 MHz while the panel driver remains
initialized. Hardware testing confirmed that lowering the configured maximum
or replacing the framebuffer stream with the DSI pattern generator can produce
an underrun that leaves the display blank after wake-up. All three logical
profiles therefore retain a 360 MHz maximum with the current display driver.

## Display-off transition

When the firmware turns the display off:

1. The backlight is set to zero.
2. The screen state is recorded as off.
3. The logical profile changes to `screen_off`, but the CPU remains at 360 MHz
   to protect the active MIPI-DPI framebuffer stream.
4. Touch input, automatic UI locking, Wardrive capture, UART parsing, storage,
   auto-upload, and low-battery handling continue to operate.
5. Expensive Wardrive LVGL table refreshes are deferred until the display is
   visible again.

The panel, DSI host, framebuffer, and touch controller remain initialized. This
is a backlight-off state with reduced UI work, not system sleep or panel
teardown.

## Touch wake-up transition

The existing first-touch wake behavior is preserved:

1. The first touch is consumed by the wake overlay and is not forwarded to the
   underlying UI control.
2. The logical profile returns to `interactive`.
3. Any deferred Wardrive table update is applied once.
4. The backlight is restored.

The Power button behavior is unchanged. Touch remains the supported way to wake
the dark display.

## WPA cracking

WPA cracking acquires a `performance` claim before starting the cracking
workflow. The firmware remains at 360 MHz during handshake preparation,
counting, download, worker execution, and cleanup.

The claim is released only after workers have stopped and temporary resources
have been freed. If the display is still off at that point, the firmware returns
to the `screen_off` profile; otherwise it returns to `interactive`.

## Wardrive behavior while the screen is off

Turning off the display does not pause Wardrive operation. Network records
continue to be received, parsed, stored, and uploaded according to the existing
configuration.

Only the costly visible table reconstruction is deferred. A pending-refresh
flag is set when data changes while the screen is off. The table is refreshed
once after wake-up or after returning to the Wardrive screen. While visible,
normal refresh throttling still limits updates to at most once per second.

## Failure behavior

The requested screen state remains authoritative even if an ESP-IDF
power-management call fails. This keeps touch wake-up, deferred UI refreshes,
and the telemetry `screen` field consistent with what the user sees. The active
MIPI-DPI path is never modified during a screen timeout or touch wake-up.

## INA226 telemetry

The existing INA226 device is sampled every two seconds. Valid samples feed a
30-sample rolling window, representing approximately 60 seconds. The rolling
window is cleared after an invalid sample so averages never bridge a
measurement gap.

A CSV record is written to the serial log every ten seconds:

```text
POWER_CSV,time_ms,profile,screen,cpu_max_mhz,voltage_v,current_ma,power_mw,avg60_power_mw,energy_mwh
```

The firmware also prints a `POWER_CSV_HEADER` line at startup. Field meanings
are:

| Field | Meaning |
|---|---|
| `time_ms` | Monotonic time since boot in milliseconds |
| `profile` | Applied profile: `interactive`, `screen_off`, or `performance` |
| `screen` | `on` or `off` according to the requested display state |
| `cpu_max_mhz` | Maximum CPU frequency associated with the applied profile |
| `voltage_v` | INA226 bus voltage |
| `current_ma` | Signed current; positive means discharge, negative means charging |
| `power_mw` | Battery discharge power; clamped to zero while charging |
| `avg60_power_mw` | Average discharge power over the valid rolling window |
| `energy_mwh` | Integrated discharge energy since telemetry initialization |

Energy integration uses elapsed monotonic time between valid samples. Charging
current does not subtract from the accumulated discharge-energy value.

## Log analysis

Capture the serial output to a file and run:

```powershell
python tools/analyze_power_log.py power.log
```

The analyzer accepts plain and ANSI-colored ESP-IDF logs, extracts `POWER_CSV`
records, and reports sample count, duration, average voltage, average current,
average power, P95 power, and integrated energy for the complete capture. Record
each scenario in a separate file when comparing profiles.

## Measurement procedure

For comparable measurements:

1. Measure on battery power. A connected USB supply may produce negative
   current because the battery is charging.
2. Start each scenario from a comparable battery state and radio workload.
3. Allow at least 60 seconds for the rolling average to stabilize.
4. Record at least 10 minutes for each scenario:
   - interactive screen-on Wardrive
   - screen-off Wardrive
   - WPA cracking
5. Compare average power and integrated energy rather than isolated current
   samples.

The onboard INA226 is suitable for firmware-level comparisons and long-running
monitoring. Use an external power analyzer such as a Joulescope or Nordic Power
Profiler Kit when startup peaks, short radio bursts, or absolute calibration
accuracy matter.

## Safety boundaries and non-goals

- No automatic light sleep is enabled.
- The MIPI DSI panel is not torn down or reinitialized during wake-up.
- The CPU maximum remains 360 MHz while the MIPI-DPI panel is initialized.
- The framebuffer and DSI video source are not switched during screen-off or
  wake-up transitions.
- The touch controller stays active while the display is dark.
- The Power button behavior is not changed.
- WPA cracking is not frequency-limited.
- Wi-Fi scanning, Wardrive collection, storage, upload, and low-battery
  protection continue normally.

## Implementation map

- `main/app_power_manager.c` and `.h`: profile selection and performance claims
- `main/app_power_telemetry.c` and `.h`: INA226 sampling, rolling average,
  energy integration, and CSV logging
- `main/main.c`: display transitions, Wardrive refresh deferral, and WPA
  integration
- `sdkconfig` and `sdkconfig.defaults`: ESP-IDF power-management and ESP32-P4
  clock configuration
- `tools/analyze_power_log.py`: offline serial-log analysis
- `tests/test_power_management_contract.py`: integration contract checks
- `tests/test_power_log_analyzer.py`: analyzer tests
