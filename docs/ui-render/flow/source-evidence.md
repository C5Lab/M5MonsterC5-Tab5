# UI navigation source evidence

Each row identifies the callback or lifecycle function that performs the route. Line numbers are regenerated from an anchor found inside that function. `condition` rows are intentionally drawn as dashed routes.

## UI navigation overview

| Route | Type | Trigger / condition | Source |
|---|---|---|---|
| Firmware boot → Boot intro | `auto` | automatic: initialize LVGL | `main/main.c:58893` — `app_main()` |
| Boot intro → No-board modal | `condition` | conditional: no Monster detected | `main/main.c:6805` — `boot_detection_done_async()` |
| Boot intro → Persistent tab shell | `auto` | automatic: board detected | `main/main.c:6808` — `boot_detection_done_async()` |
| Persistent tab shell → Grove / USB dashboard | `click` | tap Grove / USB tab | `main/main.c:8257` — `tab_click_cb()` |
| Persistent tab shell → M-BUS dashboard | `click` | tap M-BUS tab | `main/main.c:8245` — `tab_click_cb()` |
| Persistent tab shell → Internal tools | `click` | tap Internal tab | `main/main.c:8238` — `tab_click_cb()` |
| Grove / USB dashboard → Wi-Fi operations | `click` | tap WiFi / attacks / observer | `main/main.c:8671` — `main_tile_event_cb()` |
| Grove / USB dashboard → Compromised data / PCAP | `click` | tap Compromised Data | `main/main.c:8683` — `main_tile_event_cb()` |
| Grove / USB dashboard → Bluetooth | `click` | tap Bluetooth | `main/main.c:8687` — `main_tile_event_cb()` |
| Grove / USB dashboard → Sub-GHz | `click` | tap Sub-GHz | `main/main.c:8700` — `main_tile_event_cb()` |
| Internal tools → Device settings | `click` | tap Settings | `main/main.c:17523` — `internal_tile_event_cb()` |

## Wi-Fi operations and attack routes

| Route | Type | Trigger / condition | Source |
|---|---|---|---|
| Transport dashboard → WiFi Scan & Attack | `click` | tap WiFi Scan & Attack | `main/main.c:8671` — `main_tile_event_cb()` |
| Transport dashboard → Network Observer | `click` | tap Network Observer | `main/main.c:8675` — `main_tile_event_cb()` |
| Transport dashboard → Global WiFi Attacks | `click` | tap Global WiFi Attacks | `main/main.c:8673` — `main_tile_event_cb()` |
| Network Observer → Network action popup | `click` | tap network row | `main/main.c:18941` — `network_row_click_cb()` |
| Network Observer → Client deauth popup | `condition` | tap client row; Red Team required | `main/main.c:18957` — `client_row_click_cb()` |
| WiFi Scan & Attack → Deauth active popup | `condition` | tap Deauth; one or more selected | `main/main.c:9167` — `handle_selected_attack()` |
| WiFi Scan & Attack → SD-card warning | `condition` | Evil Twin without SD | `main/main.c:9173` — `handle_selected_attack()` |
| WiFi Scan & Attack → Evil Twin setup / active | `click` | tap Evil Twin | `main/main.c:9176` — `handle_selected_attack()` |
| WiFi Scan & Attack → SAE Overflow active | `condition` | tap SAE Overflow; exactly one AP | `main/main.c:9198` — `handle_selected_attack()` |
| WiFi Scan & Attack → Handshaker popup | `click` | tap Handshaker | `main/main.c:9203` — `handle_selected_attack()` |
| WiFi Scan & Attack → ARP Poison page | `condition` | tap ARP Poison; exactly one AP | `main/main.c:9231` — `handle_selected_attack()` |
| WiFi Scan & Attack → MITM setup / active | `condition` | tap MITM; exactly one AP | `main/main.c:9252` — `handle_selected_attack()` |
| WiFi Scan & Attack → Capture Gateway page | `click` | tap Capture GW | `main/main.c:9267` — `handle_selected_attack()` |
| WiFi Scan & Attack → Rogue GITM popup | `condition` | tap Rogue GITM; exactly one AP | `main/main.c:9293` — `handle_selected_attack()` |
| WiFi Scan & Attack → Nmap page / result popups | `condition` | tap Nmap; exactly one AP | `main/main.c:9315` — `handle_selected_attack()` |
| WiFi Scan & Attack → Rogue AP page / popup | `condition` | tap Rogue AP; exactly one AP | `main/main.c:9342` — `handle_selected_attack()` |
| WiFi Scan & Attack → AP Radar | `condition` | tap AP Radar; exactly one AP | `main/main.c:9366` — `handle_selected_attack()` |
| Global WiFi Attacks → Blackout confirm / active | `click` | tap Blackout | `main/main.c:49985` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → SnifferDog confirm / active | `click` | tap SnifferDog | `main/main.c:49991` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → Global Handshaker confirm / active | `click` | tap Handshaker | `main/main.c:49997` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → SD-card warning | `condition` | Portal / Wardrive without SD | `main/main.c:50005` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → Phishing Portal setup / active | `click` | tap Portal | `main/main.c:50008` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → Wardrive page / overlays | `click` | tap Wardrive | `main/main.c:50019` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → Beacon Spam menu | `click` | tap Beacon Spam | `main/main.c:50025` — `global_attack_tile_event_cb()` |
| Global WiFi Attacks → Capture Gateway page | `click` | tap GITM | `main/main.c:50031` — `global_attack_tile_event_cb()` |
| Beacon Spam menu → Beacon SSID list / editor | `click` | tap List SSIDs | `main/main.c:49897` — `beacon_spam_tile_event_cb()` |
| Beacon Spam menu → Global WiFi Attacks | `back` | Back | `main/main.c:49910` — `beacon_spam_back_btn_event_cb()` |

## Compromised data and PCAP analysis

| Route | Type | Trigger / condition | Source |
|---|---|---|---|
| Transport dashboard → Compromised Data | `click` | tap Compromised Data | `main/main.c:8683` — `main_tile_event_cb()` |
| Compromised Data → Evil Twin Passwords | `click` | tap Evil Twin Passwords | `main/main.c:33192` — `compromised_data_tile_event_cb()` |
| Compromised Data → Portal Data | `click` | tap Portal Data | `main/main.c:33194` — `compromised_data_tile_event_cb()` |
| Compromised Data → Handshake files | `click` | tap Handshakes | `main/main.c:33196` — `compromised_data_tile_event_cb()` |
| Compromised Data → Wardrive files | `click` | tap Wardrive Files | `main/main.c:33198` — `compromised_data_tile_event_cb()` |
| Compromised Data → ESPShark hub | `click` | tap ESPShark | `main/main.c:33200` — `compromised_data_tile_event_cb()` |
| ESPShark hub → Monster PCAP captures | `click` | tap Monster SD | `main/main.c:33161` — `espshark_action_btn_event_cb()` |
| ESPShark hub → Tab5 PCAP browser | `click` | tap Tab5 SD | `main/main.c:33163` — `espshark_action_btn_event_cb()` |
| Monster PCAP captures → Copy / sync progress popup | `click` | tap Copy / Sync | `main/main.c:58734` — `compromised_file_copy_cb()` |
| Monster PCAP captures → Delete / cleanup confirm | `click` | tap Delete / Clean | `main/main.c:35940` — `compromised_file_delete_cb()` |
| Tab5 PCAP browser → PCAP packet analysis | `click` | tap PCAP file | `main/main.c:41278` — `pcap_viewer_file_open_cb()` |
| PCAP packet analysis → Tab5 PCAP browser | `back` | Back releases capture | `main/main.c:40685` — `pcap_viewer_back_cb()` |
| Tab5 PCAP browser → ESPShark hub | `back` | Back with no capture open | `main/main.c:40687` — `pcap_viewer_back_cb()` |
| PCAP packet analysis → Packet detail | `click` | tap packet row | `main/main.c:41653` — `pcap_viewer_packet_detail_cb()` |
| PCAP packet analysis → Capture / DNS summaries | `click` | tap summary / DNS | `main/main.c:42159` — `pcap_viewer_summary_cb()` |
| PCAP packet analysis → Connections / protocols | `click` | tap Connections | `main/main.c:42845` — `pcap_viewer_connections_cb()` |
| PCAP packet analysis → Connections / protocols | `click` | tap Protocols | `main/main.c:42962` — `pcap_viewer_protocols_cb()` |
| PCAP packet analysis → Devices / remote endpoints | `click` | tap Devices | `main/main.c:43464` — `pcap_viewer_devices_cb()` |
| PCAP packet analysis → Devices / remote endpoints | `click` | tap Remote endpoints | `main/main.c:43470` — `pcap_viewer_remote_endpoints_cb()` |
| PCAP packet analysis → Investigation / capture health | `click` | tap Health / Investigation | `main/main.c:43752` — `pcap_viewer_health_cb()` |
| PCAP packet analysis → Network map | `click` | tap Map | `main/main.c:45347` — `pcap_viewer_map_cb()` |
| PCAP packet analysis → Tools popup | `click` | tap Tools | `main/main.c:45946` — `pcap_viewer_tools_cb()` |
| PCAP packet analysis → Extracted objects | `click` | tap Objects | `main/main.c:46409` — `pcap_viewer_objects_cb()` |

## Internal tools, settings, and Bluetooth

| Route | Type | Trigger / condition | Source |
|---|---|---|---|
| Persistent tab shell → Internal tools | `click` | tap Internal tab | `main/main.c:8238` — `tab_click_cb()` |
| Internal tools → Settings | `click` | tap Settings | `main/main.c:17523` — `internal_tile_event_cb()` |
| Internal tools → Ad Hoc Portal & Karma | `click` | tap Ad Hoc Portal & Karma | `main/main.c:17526` — `internal_tile_event_cb()` |
| Settings → Scan Setup popup | `click` | tap Scan Setup | `main/main.c:55894` — `settings_tile_event_cb()` |
| Settings → Red Team page / disclaimer | `click` | tap Red Team | `main/main.c:55896` — `settings_tile_event_cb()` |
| Settings → Screen Timeout popup | `click` | tap Screen Timeout | `main/main.c:55898` — `settings_tile_event_cb()` |
| Settings → Screen Brightness popup | `click` | tap Screen Brightness | `main/main.c:55900` — `settings_tile_event_cb()` |
| Settings → Screen Rotation popup | `click` | tap Screen Rotation | `main/main.c:55902` — `settings_tile_event_cb()` |
| Settings → Theme popup | `click` | tap Theme | `main/main.c:55904` — `settings_tile_event_cb()` |
| Settings → Time popup | `click` | tap Time | `main/main.c:55906` — `settings_tile_event_cb()` |
| Settings → Screen Lock popup | `click` | tap Screen Lock | `main/main.c:55908` — `settings_tile_event_cb()` |
| Settings → Monster OTA page / modals | `click` | tap Monster OTA | `main/main.c:55910` — `settings_tile_event_cb()` |
| Settings → Monster SD Admin / leave dialog | `click` | tap Monster SD Admin | `main/main.c:55912` — `settings_tile_event_cb()` |
| Settings → Transfer Speed popup | `click` | tap Transfer Speed | `main/main.c:55914` — `settings_tile_event_cb()` |
| Transport dashboard → Bluetooth menu | `click` | tap Bluetooth | `main/main.c:8687` — `main_tile_event_cb()` |
| Bluetooth menu → AirTag Scan | `click` | tap AirTag Scan | `main/main.c:47309` — `bt_menu_tile_event_cb()` |
| Bluetooth menu → BT Scan & Locate | `click` | tap BT Scan & Locate | `main/main.c:47311` — `bt_menu_tile_event_cb()` |
| Bluetooth menu → 2.4 GHz Jammer | `condition` | tap Jammer; Red Team tile only | `main/main.c:47313` — `bt_menu_tile_event_cb()` |
| BT Scan & Locate → BT Locator | `click` | tap discovered device | `main/main.c:48188` — `bt_scan_device_click_cb()` |
| BT Scan & Locate → BT Scan & Locate | `click` | tap Rescan | `main/main.c:48180` — `bt_scan_rescan_cb()` |

## Sub-GHz navigation and modal routes

| Route | Type | Trigger / condition | Source |
|---|---|---|---|
| Transport dashboard → Sub-GHz | `click` | tap Sub-GHz | `main/main.c:8700` — `main_tile_event_cb()` |
| Sub-GHz → Quick Scan | `click` | tap Quick Scan | `main/screens/subghz_screen.c:291` — `on_scanner()` |
| Sub-GHz → Hunter | `click` | tap Hunter | `main/screens/subghz_screen.c:292` — `on_hunter()` |
| Sub-GHz → Listen | `click` | tap Listen | `main/screens/subghz_screen.c:293` — `on_listen()` |
| Sub-GHz → SD Signals | `click` | tap SD Signals | `main/screens/subghz_screen.c:294` — `on_manage()` |
| Sub-GHz → Weather | `click` | tap Weather | `main/screens/subghz_screen.c:295` — `on_weather()` |
| Sub-GHz → Sub-GHz Jammer | `click` | tap Jammer | `main/screens/subghz_screen.c:296` — `on_jammer()` |
| Sub-GHz → Tesla | `click` | tap Tesla | `main/screens/subghz_screen.c:297` — `on_tesla()` |
| Sub-GHz → Sub-GHz Settings | `click` | tap Settings | `main/screens/subghz_screen.c:298` — `on_settings()` |
| Quick Scan → Scanner Settings | `click` | tap Settings | `main/screens/subghz_scanner_screen.c:295` — `on_settings()` |
| Scanner Settings → Quick Scan | `back` | Back | `main/screens/subghz_scanner_settings_screen.c:118` — `on_back()` |
| Quick Scan → Listen | `condition` | tap result; prefill + auto-start | `main/screens/subghz_scanner_screen.c:227` — `on_tile_clicked()` |
| Hunter → Hunter Settings | `click` | tap Settings | `main/screens/subghz_hunter_screen.c:710` — `on_settings()` |
| Hunter Settings → Hunter | `back` | Back; resume captures | `main/screens/subghz_hunter_settings_screen.c:141` — `on_back()` |
| Listen → Listen Settings | `click` | tap Settings | `main/screens/subghz_listen_screen.c:1188` — `on_settings()` |
| Listen Settings → Listen | `back` | Back | `main/screens/subghz_listen_settings_screen.c:33` — `on_back()` |
| Listen → Captured signal actions | `click` | tap captured signal | `main/screens/subghz_listen_screen.c:397` — `on_signal_row_clicked()` |
| Listen → Leave with captures? | `condition` | Back with unsaved captures | `main/screens/subghz_listen_screen.c:1176` — `on_back()` |
| Captured signal actions → Transmit warning | `click` | tap Transmit | `main/screens/subghz_listen_screen.c:1265` — `on_action_transmit()` |
| Hunter → Hunter capture actions | `click` | tap captured signal | `main/screens/subghz_hunter_screen.c:371` — `on_row_click()` |
| Hunter → Leave with captures? | `condition` | Back with captures | `main/screens/subghz_hunter_screen.c:698` — `on_back()` |
| SD Signals → Saved signal actions | `click` | tap saved signal | `main/screens/subghz_manage_screen.c:250` — `on_row_click()` |
| Saved signal actions → Rename text input | `click` | tap Rename | `main/screens/subghz_manage_screen.c:450` — `on_action_rename()` |
| Saved signal actions → Delete confirmation | `click` | tap Delete | `main/screens/subghz_manage_screen.c:558` — `on_action_delete()` |
| Saved signal actions → Transmit count | `click` | tap Transmit | `main/screens/subghz_manage_screen.c:570` — `on_action_transmit()` |
| Quick Scan → Sub-GHz | `back` | Back | `main/screens/subghz_scanner_screen.c:286` — `on_back()` |
| Hunter → Sub-GHz | `back` | Back / confirmed leave | `main/screens/subghz_hunter_screen.c:688` — `perform_back()` |
| Listen → Sub-GHz | `back` | Back / confirmed leave | `main/screens/subghz_listen_screen.c:1164` — `perform_back()` |
| SD Signals → Sub-GHz | `back` | Back | `main/screens/subghz_manage_screen.c:892` — `on_back()` |

