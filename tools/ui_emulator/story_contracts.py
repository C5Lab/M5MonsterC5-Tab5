"""Reviewed story-to-current-function mapping; no claim of runtime coverage."""
STEPS={
 'bluetooth-scan':['show_airtag_scan_page','show_bt_scan_page','show_bt_locator_page'],
 'global-attacks':['show_blackout_confirm_popup','show_blackout_active_popup','show_global_handshaker_active_popup','show_phishing_portal_popup','show_phishing_portal_active_popup','show_snifferdog_active_popup','show_wardrive_page','show_deauth_detector_page'],
 'karma-monsters-c5':['show_karma_page','karma_show_probes_cb','karma_html_select_cb','karma_monitor_task'],
 'network-observer-karma':['show_observer_page','network_row_click_cb','show_deauth_popup','karma2_fetch_probes','show_karma2_attack_popup','karma2_attack_background_cb','show_adhoc_portal_page','show_portal_data_page'],
 'scan-attacks':['show_scan_page','network_checkbox_event_cb','show_scan_deauth_popup','show_evil_twin_popup','show_sae_popup','show_handshaker_popup','show_arp_poison_page','show_rogue_ap_popup','rogue_ap_monitor_task','rogue_ap_monitor_task'],
 'settings-compromised-data':['show_red_team_settings_page','show_compromised_data_page','show_evil_twin_passwords_page','show_portal_data_page','show_handshakes_page','show_scan_time_popup'],
 'wpa-sec-upload':['wpasec_btn_event_cb','wpasec_upload_task','wpasec_network_row_click_cb','wpasec_connect_btn_cb','wpasec_upload_task'],
}
NOTES={
 'bluetooth-scan':'Three related workflows, not three mandatory consecutive actions. Locate requires a valid discovered-device index.',
 'global-attacks':'Independent operation examples, not one concurrent sequence. Simulated radio ownership must prevent conflicting jobs.',
 'karma-monsters-c5':'External JanOS workflow: probes, HTML choice, operation state and synthetic result. Distinguish this from the internal portal.',
 'network-observer-karma':'Crosses external observation and internal portal state. Captions referring to C6 must be checked against the current internal implementation; do not route every step to JanOS.',
 'scan-attacks':'Branching menu of operations. Security type, selected networks, available HTML, connected LAN and known password are different preconditions.',
 'settings-compromised-data':'Red Team affects dependent-page visibility. Saved-result pages need actual virtual contents. Scan time is per-module where the production setting supports it.',
 'wpa-sec-upload':'Key-file preparation is an external prerequisite; the final website check is external, not a Tab5 screen. The emulator should simulate the outcome locally and must not claim a real upload occurred.',
}
