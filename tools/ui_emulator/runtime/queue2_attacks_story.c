/* Physical speaker is absent; retain native success UI without an unavailable error. */
static void alert_chime_play(alert_tone_t tone){(void)tone;}
/* Read-only evidence: native surfaces and offline operation handles. */
EMSCRIPTEN_KEEPALIVE void emu_queue2_attacks_state(void){
 tab_context_t *c=&grove_ctx;int t=0;
 EM_ASM({globalThis.emulatorQueue2AttackEvidence=({tab:$0,home:!!$1,scan:!!$2,karma:!!$3,karmaPopup:!!$4,karmaConfig:!!$5,beacon:!!$6,beaconList:!!$7,beaconPopup:!!$8,ssidCount:$9});},current_tab,
 app_story_visible(c->tiles),app_story_visible(c->scan_page),app_story_visible(c->karma_page),app_story_visible(c->karma_attack_popup),app_story_visible(c->karma_html_popup),app_story_visible(c->beacon_spam_page),app_story_visible(c->beacon_ssids_page),app_story_visible(c->beacon_spam_active_popup),c->beacon_spam_ssid_count);
 EM_ASM({Object.assign(globalThis.emulatorQueue2AttackEvidence,{karmaId:$0,beaconId:$1,deauthId:$2,saeId:$3,globalId:$4,hsId:$5,hsRun:$6,hsStopped:$7,hsStep:$8,hsPopup:!!$9,hsSuccess:!!$10,deauthPopup:!!$11,saePopup:!!$12});},app_kb_jobs[t][0],app_kb_jobs[t][1],app_da[t].id,app_sae[t].id,app_global[t].id,app_hs[t].id,app_hs[t].run,app_hs[t].stopped,app_hs[t].step,app_story_visible(c->handshaker_popup),c->handshaker_capture_success,app_story_visible(c->scan_deauth_popup),app_story_visible(c->sae_popup));
}
