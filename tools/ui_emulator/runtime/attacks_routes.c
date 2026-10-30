/* Native action labels route to offline adapters; unrelated operations stay explicit. */
static void save_globals_to_tab_context(tab_context_t *ctx) {
 /* Firmware saves only legacy Karma globals here. Simulator Karma already owns
    its state in ctx; copying the stale globals would erase it on a tab switch. */
 (void)ctx;
}
static void pause_observer_for_attack(tab_context_t *ctx) {
 /* The passive scenario feed shares no radio; only native inspection must stop. */
 if(ctx){cancel_observer_inspect_task(ctx);ctx->observer_start_active=false;}
}
static void app_rogue_gitm_open(tab_context_t *ctx,int victim_index);
static void handle_selected_attack(const char *action) {
 tab_context_t *ctx=get_current_ctx();if(!ctx||!action)return;
 scan_view_t view=get_scan_view(ctx);
 if(!strcmp(action,"Pixie Dust")){
  if(ctx->observer_attack_override_active||ctx->current_visible_page!=ctx->scan_page||
     ctx->scan_in_progress||ctx->scan_last_timed_out||view.sel_count!=1||!view.nets||
     view.sel_indices[0]<0||view.sel_indices[0]>=view.net_count){
   if(ctx->scan_status_label)lv_label_set_text(ctx->scan_status_label,
    "Select exactly one AP from a finished scan for Pixie Dust");
   return;
  }
  if(g_pixie.active){
   if(ctx->scan_status_label)lv_label_set_text(ctx->scan_status_label,"A Pixie Dust session is already active");
   return;
  }
  show_pixie_page(ctx,&view.nets[view.sel_indices[0]]);return;
 }
 if(!strcmp(action,"Capture GW")){show_gitm_page_from_scan();return;}
 if(!strcmp(action,"Karma")){show_karma_page();return;}
 if(!strcmp(action,"Handshaker")){show_handshaker_popup();return;}
 if(!strcmp(action,"Rogue GITM")){
  if(ctx->observer_attack_override_active){
   if(ctx->observer_status_label)lv_label_set_text(ctx->observer_status_label,
    "Rogue GITM: open it from WiFi Scan & Attack (needs the full scan).");
   if(ctx->popup_title_label)lv_label_set_text(ctx->popup_title_label,
    "Rogue GITM: open it from WiFi Scan & Attack (needs the full scan).");
   return;
  }
  if(view.sel_count!=1){
   if(ctx->scan_status_label)lv_label_set_text(ctx->scan_status_label,
    "Select exactly 1 network for Rogue GITM");
   return;
  }
  app_rogue_gitm_open(ctx,view.sel_indices[0]);return;
 }
 bool multi=!strcmp(action,"Deauth")||!strcmp(action,"Evil Twin");
 if(view.sel_count<1||(!multi&&view.sel_count!=1)){
  if(!strcmp(action,"Radar")&&!ctx->popup_open){
   int tab=tab_id_for_ctx(ctx);app_radar_selection_errors[tab]++;
   app_radar_rejected_count[tab]=view.sel_count;
   for(int i=0;i<view.sel_count&&i<2;i++){
    int index=view.sel_indices[i];
    snprintf(app_radar_rejected_bssids[tab][i],18,"%s",index>=0&&index<view.net_count?view.nets[index].bssid:"");
   }
  }
  lv_obj_t *label=ctx->popup_open?ctx->observer_status_label:ctx->scan_status_label;
  if(label)lv_label_set_text_fmt(label,"Select %s network%s for %s",multi?"at least 1":"exactly 1",multi?"s":"",action);
  return;
 }
 int index=view.sel_indices[0];if(index<0||index>=view.net_count)return;
 wifi_network_t *network=&view.nets[index];
 if(!strcmp(action,"Radar")){show_ap_radar_page(index);return;}
 if(!strcmp(action,"SAE Overflow")){app_sae_open(ctx,index);return;}
 if(!strcmp(action,"Deauth")){show_scan_deauth_popup();return;}
 if(!strcmp(action,"Evil Twin")){show_evil_twin_popup();return;}
 if(!strcmp(action,"MITM")){show_mitm_popup();return;}
 if(!strcmp(action,"Rogue AP")){show_rogue_ap_page();return;}
 if(!strcmp(action,"ARP Poison")){
  snprintf(ctx->arp_target_ssid,sizeof(ctx->arp_target_ssid),"%s",network->ssid);
  snprintf(ctx->arp_target_security,sizeof(ctx->arp_target_security),"%s",network->security);
  ctx->arp_target_password[0]=0;show_arp_poison_page();return;
 }
 if(!strcmp(action,"Nmap")){
  snprintf(nmap_target_ssid,sizeof(nmap_target_ssid),"%s",network->ssid);
  snprintf(nmap_target_security,sizeof(nmap_target_security),"%s",network->security);
  show_nmap_page();return;
 }
 emu_unsupported(action);
}
static void attack_tile_event_cb(lv_event_t *e){handle_selected_attack(lv_event_get_user_data(e));}
static void observer_station_attack_tile_event_cb(lv_event_t *e){
 tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 if(ctx->deauth_network_idx<0||ctx->deauth_network_idx>=ctx->observer_network_count)return;
 const char *action=lv_event_get_user_data(e);
 int network_idx=ctx->deauth_network_idx;
 app_da_cancel(ctx);destroy_deauth_popup_ui();prepare_observer_attack_override(ctx,network_idx);
 ctx->observer_attack_return_to_observer=action&&(!strcmp(action,"ARP Poison")||!strcmp(action,"Rogue AP")||!strcmp(action,"Nmap"));
 handle_selected_attack(action);
}
