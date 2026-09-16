/* S02/S03 read-only native evidence. Always reports GROVE plus current tab. */
EMSCRIPTEN_KEEPALIVE void emu_wifi_story_state(void) {
 tab_context_t *ctx=&grove_ctx;app_job_t *scan=&app_jobs[TAB_GROVE];app_radar_t *radar=&app_radars[TAB_GROVE];
 int visible=current_tab==TAB_GROVE;
 EM_ASM({globalThis.emulatorWifiEvidence=({tab:$0,scanId:$1,scanState:$2,count:$3,scanVisible:!!$4,homeVisible:!!$5,selected:[],radarId:$6,radarState:$7,radarVisible:!!$8,rssi:$9,radarTarget:UTF8ToString($10),rejections:$11,redTeam:!!$12});},
  current_tab,scan->model_id,scan->state,ctx->network_count,
  visible&&ctx->current_visible_page==ctx->scan_page&&app_story_visible(ctx->scan_page),
  visible&&ctx->current_visible_page==ctx->tiles&&app_story_visible(ctx->tiles),
  radar->story_job,radar->story_job?app_model_state(radar->story_job):0,
  visible&&app_story_visible(radar->page),radar->rssi,radar->bssid,app_radar_selection_errors[TAB_GROVE],enable_red_team);
 EM_ASM({emulatorWifiEvidence.rejectedCount=$0;emulatorWifiEvidence.rejectedSelected=[];},app_radar_rejected_count[TAB_GROVE]);
 for(int i=0;i<app_radar_rejected_count[TAB_GROVE]&&i<2;i++)
  EM_ASM({emulatorWifiEvidence.rejectedSelected.push(UTF8ToString($0));},app_radar_rejected_bssids[TAB_GROVE][i]);
 for(int i=0;i<ctx->selected_count;i++){
  int index=ctx->selected_indices[i];if(index<0||index>=ctx->network_count)continue;
  EM_ASM({emulatorWifiEvidence.selected.push(UTF8ToString($0));},ctx->networks[index].bssid);
 }
}
