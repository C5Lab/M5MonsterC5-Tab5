/* Read-only S13/S14 evidence. Job IDs are retained after native completion. */
EMSCRIPTEN_KEEPALIVE void emu_queue2_nettools_state(int kind) {
 tab_context_t *ctx=&grove_ctx;int t=TAB_GROVE;gitm_ctx_t *g=ctx->gitm;
 lv_obj_t *page=kind==0?ctx->arp_poison_page:kind==1?ctx->mitm_popup_overlay:kind==2?ctx->nmap_page:g?g->page:NULL;
 int connect=kind==0?app_arp_story_jobs[t][1]:kind==2?app_nmap_story_jobs[t][1]:kind>=3?app_gitm_story_jobs[t][1]:0;
 int hosts=kind==0?app_arp_story_jobs[t][2]:kind==2?app_nmap_story_jobs[t][2]:kind>=3?app_gitm_story_jobs[t][0]:0;
 int run=kind==0?app_arp_story_jobs[t][3]:kind==1?app_mitm_story_jobs[t]:kind==2?app_nmap_story_jobs[t][3]:app_gitm_story_jobs[t][2];
 const char *target=kind==0?ctx->arp_target_ssid:kind==2?ctx->nmap_target_ssid:kind>=3&&g?g->up_ssid:"";
 scan_view_t v=get_scan_view(ctx);if(kind==1&&v.sel_count==1&&v.sel_indices[0]>=0&&v.sel_indices[0]<v.net_count)target=v.nets[v.sel_indices[0]].ssid;
 EM_ASM({globalThis.emulatorQueue2NettoolsEvidence=({tab:$0,page:!!$1,scan:!!$2,home:!!$3,connectId:$4,hostsId:$5,run:$6,target:UTF8ToString($7),hosts:$8,connected:!!$9,resultPopup:!!$10,confirm:!!$11,entry:$12});},
 current_tab,current_tab==t&&app_story_visible(page),app_story_visible(ctx->scan_page),app_story_visible(ctx->tiles),connect,hosts,run,target,
 kind==0?ctx->arp_host_count:kind==2?ctx->nmap_host_count:g?g->net_count:0,
 kind==0?ctx->arp_wifi_connected:kind==2?ctx->nmap_wifi_connected:kind>=3&&g?app_story_visible(g->step2):0,
 kind==0?app_story_visible(ctx->arp_attack_popup_overlay):kind==2?app_story_visible(ctx->nmap_results_popup_overlay):0,
 kind>=3&&app_story_visible(gitm_exit_overlay),app_gitm_story_entry[t]);
 EM_ASM({const s=globalThis.emulatorQueue2NettoolsEvidence;const d=emulatorDevice.device;
 s.job=d.job(s.run);s.connectJob=d.job(s.connectId);s.hostsJob=d.job(s.hostsId);
 s.running=s.job?.state==='running';s.stopped=s.job?.state==='completed';s.packets=s.job?.result?.packets||0;
 s.file=!!d.files('grove').find(f=>f.path===s.job?.file);
 s.moduleConnected=d.snapshot('grove').connected;
 s.apSsid=UTF8ToString($0);
 },g?g->ap_ssid:"");
}
