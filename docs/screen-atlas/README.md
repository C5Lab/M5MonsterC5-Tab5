> Superseded: see the [actual LVGL render gallery](../ui-render/README.md) and [navigation flow maps](../ui-render/flow/README.md).

# Screen atlas - source analysis

These diagrams document functions that create or show UI. **They are not LVGL renders**.
One function may handle several states; several functions may build one screen.
Conditional paths are combined. Dimensions are source calls, not evaluated geometry.
Missing text means no direct string literal was found, not that the screen is empty.

Generator: `python tools/generate_screen_atlas.py`.

[Rotation flow and test procedure](../Screen_Rotation_Test_Report.md)

| Function / SVG diagram | Source |
|---|---|
| [show_scan_overlay](main--show_scan_overlay.svg) | [main/main.c:6388](../../main/main.c) |
| [show_evil_twin_loading_overlay](main--show_evil_twin_loading_overlay.svg) | [main/main.c:6421](../../main/main.c) |
| [show_splash_screen](main--show_splash_screen.svg) | [main/main.c:7255](../../main/main.c) |
| [show_hidden_ssid_popup](main--show_hidden_ssid_popup.svg) | [main/main.c:8839](../../main/main.c) |
| [show_scan_deauth_popup](main--show_scan_deauth_popup.svg) | [main/main.c:9441](../../main/main.c) |
| [show_sae_popup](main--show_sae_popup.svg) | [main/main.c:9563](../../main/main.c) |
| [show_mitm_popup](main--show_mitm_popup.svg) | [main/main.c:9889](../../main/main.c) |
| [show_gitm_exit_confirm](main--show_gitm_exit_confirm.svg) | [main/main.c:11601](../../main/main.c) |
| [show_gitm_page](main--show_gitm_page.svg) | [main/main.c:11961](../../main/main.c) |
| [show_gitm_page_from_scan](main--show_gitm_page_from_scan.svg) | [main/main.c:12279](../../main/main.c) |
| [show_rogue_gitm_popup](main--show_rogue_gitm_popup.svg) | [main/main.c:12622](../../main/main.c) |
| [show_handshaker_popup](main--show_handshaker_popup.svg) | [main/main.c:13202](../../main/main.c) |
| [nmap_show_scan_type_popup](main--nmap_show_scan_type_popup.svg) | [main/main.c:14374](../../main/main.c) |
| [show_nmap_page](main--show_nmap_page.svg) | [main/main.c:14892](../../main/main.c) |
| [show_arp_poison_page](main--show_arp_poison_page.svg) | [main/main.c:15237](../../main/main.c) |
| [show_karma_page](main--show_karma_page.svg) | [main/main.c:16310](../../main/main.c) |
| [show_evil_twin_popup](main--show_evil_twin_popup.svg) | [main/main.c:16792](../../main/main.c) |
| [show_uart1_tiles](main--show_uart1_tiles.svg) | [main/main.c:17460](../../main/main.c) |
| [show_mbus_tiles](main--show_mbus_tiles.svg) | [main/main.c:17488](../../main/main.c) |
| [show_internal_tiles](main--show_internal_tiles.svg) | [main/main.c:17529](../../main/main.c) |
| [show_main_tiles](main--show_main_tiles.svg) | [main/main.c:17569](../../main/main.c) |
| [show_scan_page](main--show_scan_page.svg) | [main/main.c:17647](../../main/main.c) |
| [show_network_popup](main--show_network_popup.svg) | [main/main.c:18463](../../main/main.c) |
| [show_deauth_popup](main--show_deauth_popup.svg) | [main/main.c:18960](../../main/main.c) |
| [show_observer_exit_confirm](main--show_observer_exit_confirm.svg) | [main/main.c:19915](../../main/main.c) |
| [show_observer_page](main--show_observer_page.svg) | [main/main.c:20269](../../main/main.c) |
| [show_esp_modem_page](main--show_esp_modem_page.svg) | [main/main.c:20758](../../main/main.c) |
| [show_blackout_confirm_popup](main--show_blackout_confirm_popup.svg) | [main/main.c:20980](../../main/main.c) |
| [show_blackout_active_popup](main--show_blackout_active_popup.svg) | [main/main.c:21069](../../main/main.c) |
| [show_sd_warning_popup](main--show_sd_warning_popup.svg) | [main/main.c:21258](../../main/main.c) |
| [show_snifferdog_confirm_popup](main--show_snifferdog_confirm_popup.svg) | [main/main.c:21371](../../main/main.c) |
| [show_snifferdog_active_popup](main--show_snifferdog_active_popup.svg) | [main/main.c:21460](../../main/main.c) |
| [show_global_handshaker_confirm_popup](main--show_global_handshaker_confirm_popup.svg) | [main/main.c:21958](../../main/main.c) |
| [show_global_handshaker_active_popup](main--show_global_handshaker_active_popup.svg) | [main/main.c:22047](../../main/main.c) |
| [show_phishing_portal_active_popup](main--show_phishing_portal_active_popup.svg) | [main/main.c:22446](../../main/main.c) |
| [show_phishing_portal_popup](main--show_phishing_portal_popup.svg) | [main/main.c:22615](../../main/main.c) |
| [show_wardrive_gps_overlay](main--show_wardrive_gps_overlay.svg) | [main/main.c:22769](../../main/main.c) |
| [show_wardrive_upload_menu](main--show_wardrive_upload_menu.svg) | [main/main.c:23261](../../main/main.c) |
| [show_wardrive_upload_popup](main--show_wardrive_upload_popup.svg) | [main/main.c:27031](../../main/main.c) |
| [show_home_mgmt_overlay](main--show_home_mgmt_overlay.svg) | [main/main.c:27951](../../main/main.c) |
| [show_wardrive_home_confirm](main--show_wardrive_home_confirm.svg) | [main/main.c:28569](../../main/main.c) |
| [show_wardrive_page](main--show_wardrive_page.svg) | [main/main.c:31151](../../main/main.c) |
| [show_antisurv_page](main--show_antisurv_page.svg) | [main/main.c:31589](../../main/main.c) |
| [show_zig_recon_page](main--show_zig_recon_page.svg) | [main/main.c:32967](../../main/main.c) |
| [show_compromised_cleanup_popup](main--show_compromised_cleanup_popup.svg) | [main/main.c:34113](../../main/main.c) |
| [show_compromised_file_page](main--show_compromised_file_page.svg) | [main/main.c:35312](../../main/main.c) |
| [show_compromised_delete_confirm](main--show_compromised_delete_confirm.svg) | [main/main.c:36056](../../main/main.c) |
| [show_espshark_page](main--show_espshark_page.svg) | [main/main.c:36293](../../main/main.c) |
| [show_compromised_data_page](main--show_compromised_data_page.svg) | [main/main.c:36434](../../main/main.c) |
| [show_evil_twin_passwords_page](main--show_evil_twin_passwords_page.svg) | [main/main.c:36522](../../main/main.c) |
| [show_evil_twin_connect_popup](main--show_evil_twin_connect_popup.svg) | [main/main.c:36729](../../main/main.c) |
| [show_rogue_ap_popup](main--show_rogue_ap_popup.svg) | [main/main.c:37164](../../main/main.c) |
| [show_rogue_ap_page](main--show_rogue_ap_page.svg) | [main/main.c:37231](../../main/main.c) |
| [show_karma2_html_popup](main--show_karma2_html_popup.svg) | [main/main.c:37816](../../main/main.c) |
| [show_karma2_attack_popup](main--show_karma2_attack_popup.svg) | [main/main.c:38574](../../main/main.c) |
| [show_adhoc_portal_page](main--show_adhoc_portal_page.svg) | [main/main.c:39151](../../main/main.c) |
| [show_portal_data_page](main--show_portal_data_page.svg) | [main/main.c:39337](../../main/main.c) |
| [show_wpasec_popup](main--show_wpasec_popup.svg) | [main/main.c:40354](../../main/main.c) |
| [show_handshakes_page](main--show_handshakes_page.svg) | [main/main.c:40442](../../main/main.c) |
| [show_wardrive_files_page](main--show_wardrive_files_page.svg) | [main/main.c:40447](../../main/main.c) |
| [show_pcap_captures_page](main--show_pcap_captures_page.svg) | [main/main.c:40452](../../main/main.c) |
| [show_pcap_viewer_page](main--show_pcap_viewer_page.svg) | [main/main.c:40850](../../main/main.c) |
| [pcap_viewer_show_summary_popup](main--pcap_viewer_show_summary_popup.svg) | [main/main.c:42068](../../main/main.c) |
| [show_deauth_detector_page](main--show_deauth_detector_page.svg) | [main/main.c:47132](../../main/main.c) |
| [show_bluetooth_menu_page](main--show_bluetooth_menu_page.svg) | [main/main.c:47316](../../main/main.c) |
| [show_airtag_scan_page](main--show_airtag_scan_page.svg) | [main/main.c:47534](../../main/main.c) |
| [show_jammer_page](main--show_jammer_page.svg) | [main/main.c:47959](../../main/main.c) |
| [show_bt_scan_page](main--show_bt_scan_page.svg) | [main/main.c:48191](../../main/main.c) |
| [show_ap_radar_page](main--show_ap_radar_page.svg) | [main/main.c:48672](../../main/main.c) |
| [show_bt_locator_page](main--show_bt_locator_page.svg) | [main/main.c:48938](../../main/main.c) |
| [show_beacon_ssids_page](main--show_beacon_ssids_page.svg) | [main/main.c:49778](../../main/main.c) |
| [show_beacon_spam_page](main--show_beacon_spam_page.svg) | [main/main.c:49912](../../main/main.c) |
| [show_global_attacks_page](main--show_global_attacks_page.svg) | [main/main.c:50035](../../main/main.c) |
| [show_version_mismatch_popup](main--show_version_mismatch_popup.svg) | [main/main.c:51282](../../main/main.c) |
| [show_no_board_popup](main--show_no_board_popup.svg) | [main/main.c:51465](../../main/main.c) |
| [show_scan_time_popup](main--show_scan_time_popup.svg) | [main/main.c:52003](../../main/main.c) |
| [show_red_team_disclaimer_popup](main--show_red_team_disclaimer_popup.svg) | [main/main.c:52265](../../main/main.c) |
| [show_red_team_settings_page](main--show_red_team_settings_page.svg) | [main/main.c:52402](../../main/main.c) |
| [show_screen_timeout_popup](main--show_screen_timeout_popup.svg) | [main/main.c:52583](../../main/main.c) |
| [show_screen_rotation_popup](main--show_screen_rotation_popup.svg) | [main/main.c:52721](../../main/main.c) |
| [show_screen_brightness_popup](main--show_screen_brightness_popup.svg) | [main/main.c:52863](../../main/main.c) |
| [show_time_popup](main--show_time_popup.svg) | [main/main.c:53205](../../main/main.c) |
| [show_theme_popup](main--show_theme_popup.svg) | [main/main.c:53329](../../main/main.c) |
| [show_screen_lock_popup](main--show_screen_lock_popup.svg) | [main/main.c:53523](../../main/main.c) |
| [show_ft_baud_popup](main--show_ft_baud_popup.svg) | [main/main.c:53653](../../main/main.c) |
| [show_ota_page](main--show_ota_page.svg) | [main/main.c:55650](../../main/main.c) |
| [show_settings_page](main--show_settings_page.svg) | [main/main.c:55917](../../main/main.c) |
| [show_sd_admin_page](main--show_sd_admin_page.svg) | [main/main.c:56427](../../main/main.c) |
| [compromised_transfer_show_popup](main--compromised_transfer_show_popup.svg) | [main/main.c:56809](../../main/main.c) |
| [show_action_popup](subghz_hunter_screen--show_action_popup.svg) | [main/screens/subghz_hunter_screen.c:506](../../main/screens/subghz_hunter_screen.c) |
| [show_leave_popup](subghz_hunter_screen--show_leave_popup.svg) | [main/screens/subghz_hunter_screen.c:593](../../main/screens/subghz_hunter_screen.c) |
| [show_subghz_hunter_page](subghz_hunter_screen--show_subghz_hunter_page.svg) | [main/screens/subghz_hunter_screen.c:813](../../main/screens/subghz_hunter_screen.c) |
| [show_subghz_hunter_page_resume](subghz_hunter_screen--show_subghz_hunter_page_resume.svg) | [main/screens/subghz_hunter_screen.c:815](../../main/screens/subghz_hunter_screen.c) |
| [show_subghz_hunter_settings_page](subghz_hunter_settings_screen--show_subghz_hunter_settings_page.svg) | [main/screens/subghz_hunter_settings_screen.c:143](../../main/screens/subghz_hunter_settings_screen.c) |
| [show_subghz_jammer_page](subghz_jammer_screen--show_subghz_jammer_page.svg) | [main/screens/subghz_jammer_screen.c:310](../../main/screens/subghz_jammer_screen.c) |
| [show_action_popup](subghz_listen_screen--show_action_popup.svg) | [main/screens/subghz_listen_screen.c:1283](../../main/screens/subghz_listen_screen.c) |
| [show_leave_popup](subghz_listen_screen--show_leave_popup.svg) | [main/screens/subghz_listen_screen.c:1384](../../main/screens/subghz_listen_screen.c) |
| [show_tx_warn_popup](subghz_listen_screen--show_tx_warn_popup.svg) | [main/screens/subghz_listen_screen.c:1471](../../main/screens/subghz_listen_screen.c) |
| [show_subghz_listen_page](subghz_listen_screen--show_subghz_listen_page.svg) | [main/screens/subghz_listen_screen.c:1521](../../main/screens/subghz_listen_screen.c) |
| [show_subghz_listen_page_at](subghz_listen_screen--show_subghz_listen_page_at.svg) | [main/screens/subghz_listen_screen.c:1773](../../main/screens/subghz_listen_screen.c) |
| [show_subghz_listen_settings_page](subghz_listen_settings_screen--show_subghz_listen_settings_page.svg) | [main/screens/subghz_listen_settings_screen.c:35](../../main/screens/subghz_listen_settings_screen.c) |
| [show_delete_confirm](subghz_manage_screen--show_delete_confirm.svg) | [main/screens/subghz_manage_screen.c:480](../../main/screens/subghz_manage_screen.c) |
| [show_tx_count_popup](subghz_manage_screen--show_tx_count_popup.svg) | [main/screens/subghz_manage_screen.c:642](../../main/screens/subghz_manage_screen.c) |
| [show_action_popup](subghz_manage_screen--show_action_popup.svg) | [main/screens/subghz_manage_screen.c:724](../../main/screens/subghz_manage_screen.c) |
| [show_subghz_manage_page](subghz_manage_screen--show_subghz_manage_page.svg) | [main/screens/subghz_manage_screen.c:894](../../main/screens/subghz_manage_screen.c) |
| [show_subghz_scanner_page](subghz_scanner_screen--show_subghz_scanner_page.svg) | [main/screens/subghz_scanner_screen.c:297](../../main/screens/subghz_scanner_screen.c) |
| [show_subghz_scanner_settings_page](subghz_scanner_settings_screen--show_subghz_scanner_settings_page.svg) | [main/screens/subghz_scanner_settings_screen.c:120](../../main/screens/subghz_scanner_settings_screen.c) |
| [show_subghz_page](subghz_screen--show_subghz_page.svg) | [main/screens/subghz_screen.c:325](../../main/screens/subghz_screen.c) |
| [show_subghz_settings_page](subghz_settings_screen--show_subghz_settings_page.svg) | [main/screens/subghz_settings_screen.c:183](../../main/screens/subghz_settings_screen.c) |
| [show_subghz_tesla_page](subghz_tesla_screen--show_subghz_tesla_page.svg) | [main/screens/subghz_tesla_screen.c:34](../../main/screens/subghz_tesla_screen.c) |
| [subghz_show_text_input_popup](subghz_text_input_popup--subghz_show_text_input_popup.svg) | [main/screens/subghz_text_input_popup.c:79](../../main/screens/subghz_text_input_popup.c) |
| [show_subghz_weather_page](subghz_weather_screen--show_subghz_weather_page.svg) | [main/screens/subghz_weather_screen.c:377](../../main/screens/subghz_weather_screen.c) |

## Existing photographs / illustrations

Historical material from `tab.stories`; it does not verify the current version or rotation.

- [bluetooth-scan / airtag detector](<../tab.stories/src/bluetooth-scan/airtag detector.jpeg>)
- [bluetooth-scan / BT list](<../tab.stories/src/bluetooth-scan/BT list.jpeg>)
- [bluetooth-scan / BT locate device](<../tab.stories/src/bluetooth-scan/BT locate device.jpeg>)
- [global-attacks / blackout in action](<../tab.stories/src/global-attacks/blackout in action.jpeg>)
- [global-attacks / c5 phising portal portal](<../tab.stories/src/global-attacks/c5 phising portal portal.jpeg>)
- [global-attacks / c5 portal waiting](<../tab.stories/src/global-attacks/c5 portal waiting.jpeg>)
- [global-attacks / Detector](<../tab.stories/src/global-attacks/Detector.jpeg>)
- [global-attacks / global attacks](<../tab.stories/src/global-attacks/global attacks.jpeg>)
- [global-attacks / handshaker in action](<../tab.stories/src/global-attacks/handshaker in action.jpeg>)
- [global-attacks / network observer probes list 4 karma](<../tab.stories/src/global-attacks/network observer probes list 4 karma.jpeg>)
- [global-attacks / snifferdog in action](<../tab.stories/src/global-attacks/snifferdog in action.jpeg>)
- [global-attacks / wardrive](<../tab.stories/src/global-attacks/wardrive.jpeg>)
- [karma-monsters-c5 / c5 karma attack](<../tab.stories/src/karma-monsters-c5/c5 karma attack.jpeg>)
- [karma-monsters-c5 / c5 karma password obtained](<../tab.stories/src/karma-monsters-c5/c5 karma password obtained.jpeg>)
- [karma-monsters-c5 / c5 karma select html](<../tab.stories/src/karma-monsters-c5/c5 karma select html.jpeg>)
- [karma-monsters-c5 / landing page](<../tab.stories/src/karma-monsters-c5/landing page.jpeg>)
- [middle-man / arp-mitm](<../tab.stories/src/middle-man/arp-mitm.jpeg>)
- [middle-man / rogue-gitm](<../tab.stories/src/middle-man/rogue-gitm.png>)
- [network-observer-karma / c6 karma active from network observer](<../tab.stories/src/network-observer-karma/c6 karma active from network observer.jpeg>)
- [network-observer-karma / c6 karma got password](<../tab.stories/src/network-observer-karma/c6 karma got password.jpeg>)
- [network-observer-karma / c6 karma in internal tab](<../tab.stories/src/network-observer-karma/c6 karma in internal tab.jpeg>)
- [network-observer-karma / deauth station](<../tab.stories/src/network-observer-karma/deauth station.jpeg>)
- [network-observer-karma / network observer list clients](<../tab.stories/src/network-observer-karma/network observer list clients.jpeg>)
- [network-observer-karma / network observer probes list 4 karma](<../tab.stories/src/network-observer-karma/network observer probes list 4 karma.jpeg>)
- [network-observer-karma / network observer start c6 karma](<../tab.stories/src/network-observer-karma/network observer start c6 karma.jpeg>)
- [network-observer-karma / network observer zoom on 1 network](<../tab.stories/src/network-observer-karma/network observer zoom on 1 network.jpeg>)
- [scan-attacks / arp poisoning active](<../tab.stories/src/scan-attacks/arp poisoning active.jpeg>)
- [scan-attacks / evil twin waiting for victim](<../tab.stories/src/scan-attacks/evil twin waiting for victim.jpeg>)
- [scan-attacks / handshaker active](<../tab.stories/src/scan-attacks/handshaker active.jpeg>)
- [scan-attacks / landing page](<../tab.stories/src/scan-attacks/landing page.jpeg>)
- [scan-attacks / rogue ap got pass](<../tab.stories/src/scan-attacks/rogue ap got pass.jpeg>)
- [scan-attacks / rogue ap witing for pass](<../tab.stories/src/scan-attacks/rogue ap witing for pass.jpeg>)
- [scan-attacks / rogueAP known pass](<../tab.stories/src/scan-attacks/rogueAP known pass.jpeg>)
- [scan-attacks / rogueAP known password](<../tab.stories/src/scan-attacks/rogueAP known password.jpeg>)
- [scan-attacks / sae overflow active](<../tab.stories/src/scan-attacks/sae overflow active.jpeg>)
- [scan-attacks / select target networks](<../tab.stories/src/scan-attacks/select target networks.jpeg>)
- [scan-attacks / simple deauth attack](<../tab.stories/src/scan-attacks/simple deauth attack.jpeg>)
- [settings-compromised-data / comp data evil twin](<../tab.stories/src/settings-compromised-data/comp data evil twin.jpeg>)
- [settings-compromised-data / comp data handshakes](<../tab.stories/src/settings-compromised-data/comp data handshakes.jpeg>)
- [settings-compromised-data / comp data menu](<../tab.stories/src/settings-compromised-data/comp data menu.jpeg>)
- [settings-compromised-data / comp data portal](<../tab.stories/src/settings-compromised-data/comp data portal.jpeg>)
- [settings-compromised-data / red team setting](<../tab.stories/src/settings-compromised-data/red team setting.jpeg>)
- [settings-compromised-data / Scan time](<../tab.stories/src/settings-compromised-data/Scan time.jpeg>)
- [wpa-sec-upload / 1](<../tab.stories/src/wpa-sec-upload/1.jpg>)
- [wpa-sec-upload / 2](<../tab.stories/src/wpa-sec-upload/2.jpg>)
- [wpa-sec-upload / 3](<../tab.stories/src/wpa-sec-upload/3.jpg>)
- [wpa-sec-upload / 4](<../tab.stories/src/wpa-sec-upload/4.jpg>)
- [wpa-sec-upload / 5](<../tab.stories/src/wpa-sec-upload/5.jpg>)
