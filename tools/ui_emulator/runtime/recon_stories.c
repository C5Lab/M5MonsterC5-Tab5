/* Read-only native evidence for the GROVE Observer and Mesh stories. */
EMSCRIPTEN_KEEPALIVE void emu_recon_story_state(int mesh) {
 tab_context_t *ctx=&grove_ctx;int tab=TAB_GROVE;
 bool page=current_tab==tab&&app_story_visible(mesh?ctx->iot_page:ctx->observer_page);
 bool home=current_tab==tab&&ctx->current_visible_page==ctx->tiles&&app_story_visible(ctx->tiles);
 const char *target="";
 if(ctx->popup_open&&ctx->popup_network_idx>=0&&ctx->popup_network_idx<ctx->observer_network_count)
  target=ctx->observer_networks[ctx->popup_network_idx].bssid;
 int capture=app_observer_capture_jobs[tab];
 EM_ASM({globalThis.emulatorReconEvidence=({tab:$0,page:!!$1,home:!!$2,run:$3,running:!!$4,stopped:$5,cleared:$6,networks:$7,nodes:$8,expanded:UTF8ToString($9),packets:$10});},
  current_tab,page,home,mesh?app_mesh_story_run[tab]:app_observer_story_run[tab],
  mesh?ctx->iot_recon_monitoring:ctx->observer_running,
  mesh?app_mesh_story_stopped[tab]:app_observer_story_stopped[tab],app_mesh_story_cleared[tab],
  mesh?ctx->iot_pan_count:ctx->observer_network_count,ctx->iot_node_count,ctx->iot_expanded_pan,
  mesh&&ctx->iot_packets_label?atoi(lv_label_get_text(ctx->iot_packets_label)):0);
 EM_ASM({const s=globalThis.emulatorReconEvidence;
  s.popup=!!$0;s.target=UTF8ToString($1);s.confirm=!!$2;s.captureVisible=!!$3;s.captureId=$4;
  const o=emulatorDevice.device.observer('grove');s.clients=o.networks.reduce((n,x)=>n+x.clients.length,0);
  if(!$5){s.packets=o.packets;s.running=s.running&&o.running;}
  const j=emulatorDevice.device.job(s.captureId);s.captureState=j?({running:1,completed:2,cancelled:3,failed:4})[j.state]:0;
  const f=emulatorDevice.device.files('grove').find(f=>f.path===j?.file);
  s.file=!!f;s.captureTarget=f?.networkId||'';
 },app_story_visible(ctx->network_popup),target,
  current_tab==tab&&app_story_visible(observer_exit_overlay)&&page,
  app_story_visible(ctx->mitm_popup_overlay),capture,mesh);
}
