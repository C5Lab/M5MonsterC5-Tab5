# WiGLE upload Wi-Fi prompt audit

The browser emulator retains the Tab5 Wardrive upload menu and its WiGLE
**Send**, **Sync Pending**, and **Force All** controls. It completes uploads
through a deterministic local model. The firmware's Wi-Fi scan, network picker,
SSID/password fields, show-password checkbox, Connect, and Rescan controls are
unreachable from this emulator route.

## Call path

| Build | Reachable path |
| --- | --- |
| Firmware | [`wardrive_upload_wigle_btn_cb`](../../main/main.c#L25190) opens [`show_wardrive_upload_popup`](../../main/main.c#L29048). Its Send, Sync Pending, and Force All callbacks call [`wardrive_wigle_start_upload`](../../main/main.c#L26852), which creates `wardrive_wigle_upload_task` with `xTaskCreate` (line 26895 onward). The task calls [`wardrive_wigle_ensure_wifi_connected`](../../main/main.c#L28297). That function scans with `scan_networks` (line 27272), shows network rows (line 27470), and can create the credentials prompt (lines 27548, 27654, 27800). The prompt wires Connect and show-password callbacks (lines 27183, 27195); the scan flow wires Rescan (line 27830). An already-connected firmware Wi-Fi session skips the prompt (line 27236). |
| Emulator | The [slice policy](../../tools/ui_emulator/slice-phase4-wardrive-upload.json) retains the same popup and button callbacks but maps `wardrive_wigle_start_upload` to an adapter. The [adapter](../../tools/ui_emulator/runtime/wardrive_upload.c#L80) schedules an upload after 1500 ms. [`app_wd_upload_tick`](../../tools/ui_emulator/runtime/wardrive_upload.c#L94), called by the [app tick](../../tools/ui_emulator/runtime/application.c#L297), invokes `app_wdu_send` directly. That bridge calls the local [`wardriveUpload` model](../../tools/ui_emulator/model/device.mjs#L552), which updates saved session statuses for a selected success, failure, or partial outcome. No network request or Wi-Fi credential check occurs. |

The generated browser C has the three retained upload callbacks and adapter
entry point, but no definition of `wardrive_wigle_upload_task`,
`wardrive_wigle_ensure_wifi_connected`, or
`wardrive_wigle_create_credentials_prompt`. Its Wi-Fi prompt names appear only
in the control identity table at the top of
[`generated/browser.c`](../../tools/ui_emulator/generated/browser.c#L179);
that table does not construct controls. The firmware prompt cannot be reached
by clicking a retained upload control, so adding a separate local credential
screen would create a second upload flow rather than complete this route.

## Automatic upload result

The Wardrive Home confirmation uses the same local upload model. After **Yes**,
the emulator shows an auto-upload result overlay. Failure offers **Retry**,
**Resume** and **Close**; Retry applies the current synthetic outcome to pending
sessions. Success offers Resume and Close. If the Wardrive **Power off** option
is enabled, a ten-second virtual countdown offers **Stay on**. Expiry reports a
simulated power-off state in the browser; no physical power action occurs.
With **Archive** enabled, a successful automatic upload runs the model's
`all done move` cleanup: only sessions uploaded to both providers move into
`/uploaded/`. Failed automatic uploads leave previously completed sessions
in place.
The result-button factory is an emulator adapter for the firmware's
`wardrive_autoup_make_btn` registration. The WiGLE network and credential
prompt remains outside the offline route.

## Verification boundary

[`test_record_save_upload_and_gps`](../../tests/test_emulator_phase4_wardrive.py#L50)
clicks WiGLE Sync Pending through the native popup and verifies simulated
failure, partial, and success results on saved sessions. It requires no Wi-Fi
picker or credentials. This is browser/model behavior only; it does not test
firmware Wi-Fi connection or a WiGLE service upload.

[`test_emulator_wardrive_autoup.py`](../../tests/test_emulator_wardrive_autoup.py)
drives the Home result overlay through native Canvas controls, including retry,
resume, Stay on, countdown expiry, Archive and Close in three rotations.
