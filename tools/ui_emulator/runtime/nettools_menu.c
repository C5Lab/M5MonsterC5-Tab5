/* Retain the native Global WiFi menu; other simulations belong to Phase 4.5. */
static void global_attack_tile_event_cb(lv_event_t *e) {
 const char *action=lv_event_get_user_data(e);
 if(action&&!strcmp(action,"Capture GW")){show_gitm_page();return;}
 if(action&&!strcmp(action,"Beacon Spam")){show_beacon_spam_page();return;}
 if(action&&!strcmp(action,"Blackout")){show_blackout_confirm_popup();return;}
 if(action&&!strcmp(action,"Handshakes")){show_global_handshaker_confirm_popup();return;}
 if(action&&!strcmp(action,"Snifferdog")){show_snifferdog_confirm_popup();return;}
 emu_unsupported(action?action:"Global WiFi tool");
}
