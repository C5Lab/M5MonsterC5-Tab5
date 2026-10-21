/* Browser-only Observer boundaries. Included after the model bridge in application.c.
 * The page, network rows and client popup come byte-exact from main/main.c.
 * Capture controls below are explicitly simulator UI, not firmware authentication. */
static int app_observer_capture_jobs[4];
static unsigned app_observer_story_run[4],app_observer_story_stopped[4];
static lv_obj_t *app_observer_analyze_btn[4];
static void app_observer_analyze_cb(lv_event_t *e);
static double app_observer_live_last[4];
EM_JS(int,app_observer_live_start,(int tab,const char *targets,char *error,int size),{
 try{emulatorDevice.device.observerStart(emulatorDevice.module(tab));return 1;}
 catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(void,app_observer_live_stop,(int tab),{emulatorDevice.device.observerStop(emulatorDevice.module(tab));});
EM_JS(int,app_observer_live_state,(int tab,int *values),{
 const s=emulatorDevice.device.observer(emulatorDevice.module(tab));
 const clients=s.networks.flatMap(n=>n.clients);
 HEAP32[values>>2]=s.packets;HEAP32[(values>>2)+1]=clients.filter(c=>c.active).length;HEAP32[(values>>2)+2]=clients.length;
 return s.running?1:0;
});
EM_JS(int,app_observer_live_rssi,(int tab,const char *bssid,int fallback),{
 return emulatorDevice.device.observer(emulatorDevice.module(tab)).networks.find(n=>n.bssid===UTF8ToString(bssid))?.rssi??fallback;
});
EM_JS(int,app_observer_live_client,(int tab,const char *mac),{
 return emulatorDevice.device.observer(emulatorDevice.module(tab)).networks.some(n=>n.clients.some(c=>c.mac===UTF8ToString(mac)&&c.active))?1:0;
});

/* Read Observer-owned inventory without selecting or populating WiFi Scan. */
EM_JS(int,app_observer_network_count,(int tab),{
 return emulatorDevice.device.observer(emulatorDevice.module(tab)).networks.length;
});
EM_JS(void,app_observer_field,(int tab,int index,const char *key,char *out,int size),{
 const n=emulatorDevice.device.observer(emulatorDevice.module(tab)).networks[index];
 stringToUTF8(String(n?.[UTF8ToString(key)]??''),out,size);
});
EM_JS(int,app_observer_client_mac,(int tab,int index,int client,char *out,int size),{
 const c=emulatorDevice.device.observer(emulatorDevice.module(tab)).networks[index]?.clients[client];
 stringToUTF8(c?.mac||'',out,size);return c?1:0;
});

static void cancel_observer_inspect_task(tab_context_t *ctx) {
    if (ctx) { ctx->observer_inspect_task=NULL; ctx->observer_inspect_active=false; }
}
static void close_network_popup(void) {
    tab_context_t *ctx=get_current_ctx();
    if (ctx) { destroy_network_popup_ui(ctx); ctx->popup_focus_active=false; ctx->popup_focus_task=NULL; }
}
static void observer_stop_btn_cb(lv_event_t *e) {
    (void)e; tab_context_t *ctx=get_current_ctx(); if (!ctx) return;
    ctx->observer_running=false; ctx->observer_start_active=false;
    for(int i=0;i<ctx->observer_network_count;i++) {
        ctx->observer_networks[i].snapshot_clock_ms=observer_clock_ms();
        ctx->observer_networks[i].snapshot_live=false;
    }
    app_observer_live_stop(tab_id_for_ctx(ctx));
    close_network_popup();
    if(ctx->observer_start_btn)lv_obj_remove_state(ctx->observer_start_btn,LV_STATE_DISABLED);
    if(ctx->observer_stop_btn)lv_obj_add_state(ctx->observer_stop_btn,LV_STATE_DISABLED);
    if(ctx->observer_status_label)lv_label_set_text(ctx->observer_status_label,"Simulation stopped; cached clients retained.");
}
static void observer_back_btn_event_cb(lv_event_t *e) {
    tab_context_t *ctx=get_current_ctx(); if(!ctx)return;
    if(ctx->observer_running||ctx->observer_start_active){
        close_observer_exit_confirm();show_observer_exit_confirm();return;
    }
    observer_stop_btn_cb(e); ctx->observer_page_visible=false;
    show_main_tiles();
}
static void observer_exit_confirm_cb(lv_event_t *e) {
    tab_context_t *ctx=get_current_ctx();
    if(ctx)app_observer_story_stopped[tab_id_for_ctx(ctx)]=app_observer_story_run[tab_id_for_ctx(ctx)];
    close_observer_exit_confirm();observer_stop_btn_cb(e);
    observer_back_btn_event_cb(e);
}
static void observer_start_btn_cb(lv_event_t *e) {
    (void)e; tab_context_t *ctx=get_current_ctx(); if(!ctx)return;
    int tab=tab_id_for_ctx(ctx);
    if(!ctx->observer_networks)ctx->observer_networks=calloc(MAX_OBSERVER_NETWORKS,sizeof(observer_network_t));
    if(!ctx->observer_network_ui)ctx->observer_network_ui=calloc(MAX_OBSERVER_NETWORKS,sizeof(observer_network_ui_t));
    if(!ctx->observer_networks||!ctx->observer_network_ui){emu_unsupported("Observer allocation failed");return;}
    observer_options_load();
    char error[160]="";
    if(!app_observer_live_start(tab,"",error,sizeof(error))){if(ctx->observer_status_label)lv_label_set_text(ctx->observer_status_label,error);return;}
    app_observer_story_run[tab]++;
    app_observer_live_last[tab]=0;
    close_network_popup();
    ctx->observer_network_count=0;
    int count=app_observer_network_count(tab);
    for(int i=0;i<count && ctx->observer_network_count<MAX_OBSERVER_NETWORKS;i++) {
        observer_network_t *net=&ctx->observer_networks[ctx->observer_network_count++];
        memset(net,0,sizeof(*net));
        char value[24];
        app_observer_field(tab,i,"index",value,sizeof(value));net->scan_index=atoi(value);
        app_observer_field(tab,i,"channel",value,sizeof(value));net->channel=atoi(value);
        app_observer_field(tab,i,"rssi",value,sizeof(value));net->rssi=atoi(value);
        app_observer_field(tab,i,"ssid",net->ssid,sizeof(net->ssid));
        app_observer_field(tab,i,"bssid",net->bssid,sizeof(net->bssid));
        app_observer_field(tab,i,"band",net->band,sizeof(net->band));
        app_observer_field(tab,i,"security",net->security,sizeof(net->security));
        app_observer_field(tab,i,"vendor",net->vendor,sizeof(net->vendor));
        /* Deterministic protocol fixtures exercise advertised/absent/unknown
         * security and WPS states without suggesting real RF observations. */
        char extended[1600];
        snprintf(extended,sizeof(extended),
            "fixture | ext_ver=1 | bssid=%s | rssi=%d | age_ms=%d | hidden=%d | resolved_ssid_hex=%s | ssid_source=%s | profile_source=beacon | frame_status=valid | privacy=1 | rsn_status=valid | rsn_group=000FAC04 | rsn_pairwise=000FAC04 | rsn_akm=000FAC02,000FAC08 | pmf_capable=1 | pmf_required=%d | wpa_status=absent | wps_status=%s | wps_present=%s | wps_state=2 | wps_config_methods=128 | wps_setup_locked=0 | wps_selected_registrar=0 | wps_manufacturer_hex=53696D756C61746564 | wps_model_name_hex=54657374204150 | wps_device_name_hex=4F627365727665722066697874757265",
            net->bssid,net->rssi,170+i*123,i==0,
            i==0?"53696D204C6162":"unknown",i==0?"probe_response":"unknown",i%2,
            i%3==0?"valid":i%3==1?"absent":"unknown",
            i%3==0?"1":i%3==1?"0":"unknown");
        observer_ext_parse(extended,&net->extended);
        net->extended_received_ms=observer_clock_ms();
        net->snapshot_clock_ms=net->extended_received_ms;net->snapshot_live=true;
        snprintf(net->uptime,sizeof(net->uptime),"%dh %dm",i+1,i*7);
        for(int c=0;c<MAX_CLIENTS_PER_NETWORK;c++) {
            if(!app_observer_client_mac(tab,i,c,net->clients[c],sizeof(net->clients[c])))break;
            snprintf(net->client_vendors[c],sizeof(net->client_vendors[c]),"Simulation vendor %d",c+1);
            net->client_rssi[c]=-45-c*6;net->client_rssi_known[c]=true;
            net->client_age_ms[c]=287+c*180;net->client_age_known[c]=true;
            net->client_received_ms[c]=observer_clock_ms();
            net->client_count++;
        }
    }
    ctx->observer_running=true;ctx->observer_start_active=false;
    update_observer_table(ctx);
    if(ctx->observer_status_label)lv_label_set_text_fmt(ctx->observer_status_label,
        "Observing %d scenario networks and their clients independently of WiFi Scan.",ctx->observer_network_count);
    if(ctx->observer_start_btn && ctx->observer_running)lv_obj_add_state(ctx->observer_start_btn,LV_STATE_DISABLED);
    if(ctx->observer_stop_btn && ctx->observer_running)lv_obj_remove_state(ctx->observer_stop_btn,LV_STATE_DISABLED);
}
static void app_capture_close_cb(lv_event_t *e) {
    (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
    int tab=tab_id_for_ctx(ctx);
    if(app_observer_capture_jobs[tab] && app_model_state(app_observer_capture_jobs[tab])==1)app_model_cancel(app_observer_capture_jobs[tab]);
    app_observer_capture_jobs[tab]=0;app_observer_analyze_btn[tab]=NULL;
    if(ctx->mitm_popup_overlay)lv_obj_del(ctx->mitm_popup_overlay);
    ctx->mitm_popup_overlay=NULL;ctx->mitm_popup=NULL;ctx->mitm_status_label=NULL;
    ctx->mitm_connect_btn=NULL;ctx->mitm_stop_btn=NULL;
    clear_observer_attack_override(ctx);
}
static void app_capture_start_cb(lv_event_t *e) {
    (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
    scan_view_t view=get_scan_view(ctx);if(view.sel_count!=1)return;
    int index=view.sel_indices[0];if(index<0 || index>=view.net_count)return;
    int tab=tab_id_for_ctx(ctx);
    if(app_observer_capture_jobs[tab])return;
    app_model_select(tab,view.nets[index].bssid);
    app_observer_capture_jobs[tab]=app_model_capture(tab);
    if(ctx->mitm_status_label)lv_label_set_text(ctx->mitm_status_label,app_observer_capture_jobs[tab]
        ? "Generating synthetic Ethernet/ARP PCAP. No WiFi connection or authentication."
        : "Synthetic capture could not start.");
    if(app_observer_capture_jobs[tab] && ctx->mitm_connect_btn)lv_obj_add_state(ctx->mitm_connect_btn,LV_STATE_DISABLED);
}
static void app_capture_show(void) {
    tab_context_t *ctx=get_current_ctx();if(!ctx || ctx->mitm_popup_overlay)return;
    scan_view_t view=get_scan_view(ctx);if(view.sel_count!=1){emu_unsupported("Select one network for synthetic capture");return;}
    lv_obj_t *parent=get_current_tab_container();if(!parent)return;
    ctx->mitm_popup_overlay=lv_obj_create(parent);lv_obj_set_size(ctx->mitm_popup_overlay,lv_pct(100),lv_pct(100));
    style_modal_overlay(ctx->mitm_popup_overlay,LV_OPA_70);
    ctx->mitm_popup=lv_obj_create(ctx->mitm_popup_overlay);lv_obj_set_size(ctx->mitm_popup,540,320);lv_obj_center(ctx->mitm_popup);
    lv_obj_set_flex_flow(ctx->mitm_popup,LV_FLEX_FLOW_COLUMN);
    lv_obj_t *title=lv_label_create(ctx->mitm_popup);lv_label_set_text(title,"SIMULATION - Synthetic PCAP");
    ctx->mitm_status_label=lv_label_create(ctx->mitm_popup);lv_obj_set_width(ctx->mitm_status_label,lv_pct(100));
    lv_label_set_long_mode(ctx->mitm_status_label,LV_LABEL_LONG_WRAP);
    lv_label_set_text(ctx->mitm_status_label,"Generate an Ethernet/ARP fixture for local file analysis. No MITM, handshake or authentication is performed.");
    ctx->mitm_connect_btn=lv_btn_create(ctx->mitm_popup);
    app_bind_adapter(ctx->mitm_connect_btn,app_capture_start_cb,LV_EVENT_CLICKED,NULL,"emu.phase3.capture.generate");
    lv_obj_t *label=lv_label_create(ctx->mitm_connect_btn);lv_label_set_text(label,"Generate synthetic PCAP");
    ctx->mitm_stop_btn=lv_btn_create(ctx->mitm_popup);
    app_bind_adapter(ctx->mitm_stop_btn,app_capture_close_cb,LV_EVENT_CLICKED,NULL,"emu.phase3.capture.close");
    label=lv_label_create(ctx->mitm_stop_btn);lv_label_set_text(label,"Cancel / Close");
    /* The saved fixture is only worth offering once it exists, so this stays
       disabled until the model reports the operation completed. */
    lv_obj_t *analyze=lv_btn_create(ctx->mitm_popup);
    app_bind_adapter(analyze,app_observer_analyze_cb,LV_EVENT_CLICKED,NULL,"emu.phase3.capture.analyze");
    lv_obj_add_state(analyze,LV_STATE_DISABLED);
    label=lv_label_create(analyze);lv_label_set_text(label,"Open local file analysis");
    app_observer_analyze_btn[tab_id_for_ctx(ctx)]=analyze;
}
/* Hand the saved fixture to the production local-analysis page. */
static void app_observer_analyze_cb(lv_event_t *e) {
    app_capture_close_cb(e);
    close_network_popup();
    show_pcap_viewer_page();
}
static void app_observer_capture_cb(lv_event_t *e) {
    (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx || !ctx->popup_open)return;
    prepare_observer_attack_override(ctx,ctx->popup_network_idx);app_capture_show();
}
static void popup_focus_task(void *arg) {
    tab_context_t *ctx=arg;if(!ctx || !ctx->popup_open)return;
    ctx->popup_focus_active=false;ctx->popup_focus_ready=true;ctx->popup_focus_task=NULL;
    /* Explicit simulator affordance; original production action callbacks remain identifiable. */
    lv_obj_t *btn=lv_btn_create(ctx->network_popup);
    app_bind_adapter(btn,app_observer_capture_cb,LV_EVENT_CLICKED,NULL,"emu.phase3.observer.synthetic_capture");
    lv_obj_t *label=lv_label_create(btn);lv_label_set_text(label,"Simulation: generate PCAP");
}
static void popup_timer_callback(TimerHandle_t timer) {(void)timer;}
static void observer_attack_tile_event_cb(lv_event_t *e) {
    const char *action=lv_event_get_user_data(e);
    tab_context_t *ctx=get_current_ctx();if(!ctx||!ctx->popup_open)return;
    prepare_observer_attack_override(ctx,ctx->popup_network_idx);
    ctx->observer_attack_return_to_observer=action&&(!strcmp(action,"ARP Poison")||!strcmp(action,"Rogue AP")||!strcmp(action,"Nmap"));
    handle_selected_attack(action);
}
static void app_observer_live_tick(tab_context_t *ctx,int tab) {
 if(!ctx->observer_running)return;
 if(!ctx->observer_page||!lv_obj_is_valid(ctx->observer_page)){app_observer_live_stop(tab);ctx->observer_running=false;return;}
 if(app_device_ms-app_observer_live_last[tab]<1000)return;
 app_observer_live_last[tab]=app_device_ms;int values[3]={0};
 if(!app_observer_live_state(tab,values)){
  ctx->observer_running=false;
  for(int i=0;i<ctx->observer_network_count;i++) {
   ctx->observer_networks[i].snapshot_clock_ms=observer_clock_ms();
   ctx->observer_networks[i].snapshot_live=false;
  }
  if(ctx->observer_start_btn)lv_obj_remove_state(ctx->observer_start_btn,LV_STATE_DISABLED);
  if(ctx->observer_stop_btn)lv_obj_add_state(ctx->observer_stop_btn,LV_STATE_DISABLED);
  if(ctx->observer_status_label)lv_label_set_text(ctx->observer_status_label,"Simulation stopped or reset; cached rows retained.");
  return;
 }
 for(int i=0;i<ctx->observer_network_count;i++){
  observer_network_t *net=&ctx->observer_networks[i];net->rssi=app_observer_live_rssi(tab,net->bssid,net->rssi);
  lv_obj_t *label=ctx->observer_inspect_info_labels&&i<ctx->observer_inspect_label_count?ctx->observer_inspect_info_labels[i]:NULL;
  if(label&&lv_obj_is_valid(label)){char text[NETWORK_INFO_BUF];observer_format_fields(net,text,sizeof(text));lv_label_set_text(label,text);}
 }
 for(int i=0;i<1024;i++){
  lv_obj_t *row=app_bindings[i].object;const char *id=app_bindings[i].id;
  size_t len=strlen(id);if(!row||len<7||strcmp(id+len-7,"/client")||!app_descendant(row,ctx->observer_table))continue;
  lv_obj_t *label=lv_obj_get_child(row,0);if(!label||!lv_obj_check_type(label,&lv_label_class))continue;
  char mac[18];snprintf(mac,sizeof(mac),"%.17s",lv_label_get_text(label));
  bool active=app_observer_live_client(tab,mac);
  lv_obj_set_style_text_color(label,active?COLOR_MATERIAL_TEAL:lv_color_hex(0x888888),0);
 }
 if(ctx->observer_status_label)lv_label_set_text_fmt(ctx->observer_status_label,"Simulation: %d packets | %d/%d clients active",values[0],values[1],values[2]);
}
/* Rewrite the popup only when the model state actually changes: an unconditional
   per-frame lv_label_set_text invalidates the card on every rendered frame. */
static int app_observer_capture_shown[4];
static void app_observer_tick(void) {
    for(int tab=0;tab<4;tab++) {
        tab_context_t *ctx=get_ctx_for_tab(tab);
        if(ctx&&(tab==TAB_GROVE||tab==TAB_MBUS))app_observer_live_tick(ctx,tab);
        if(!ctx || !ctx->mitm_status_label || !app_observer_capture_jobs[tab]) { app_observer_capture_shown[tab]=0; continue; }
        int state=app_model_state(app_observer_capture_jobs[tab]);
        if(state==app_observer_capture_shown[tab])continue;
        app_observer_capture_shown[tab]=state;
        const char *status=state==1 ? "Generating synthetic Ethernet/ARP capture..." :
            state==2 ? "Synthetic Ethernet/ARP PCAP saved. Close this popup and open local file analysis." :
            state==3 ? "Synthetic capture cancelled." : "Synthetic capture failed or expired.";
        if(state==2) {
            app_model_sync_files(tab);
            if(app_observer_analyze_btn[tab])lv_obj_remove_state(app_observer_analyze_btn[tab],LV_STATE_DISABLED);
        }
        char detail[96];app_model_error(app_observer_capture_jobs[tab],detail,sizeof(detail));
        lv_label_set_text_fmt(ctx->mitm_status_label,"%s%s%s\nNo WiFi connection or authentication performed.",
            status,detail[0]?" Reason: ":"",detail);
    }
}

/* Test-only bounded inventory exercising the actual retained LVGL refresh.
 * Native child counts avoid the inspector/interactive registry limits. */
EMSCRIPTEN_KEEPALIVE double emu_observer_stress(int count,int clients,int step,int during_scroll) {
 tab_context_t *ctx=get_current_ctx();
 if(!ctx || !ctx->observer_networks || !ctx->observer_table ||
    count<1 || count>MAX_OBSERVER_NETWORKS || clients<0 || clients>MAX_CLIENTS_PER_NETWORK) return -1;
 int old=ctx->observer_network_count;
 observer_network_t seed=ctx->observer_networks[0];
 for(int i=0;i<count;i++) {
  observer_network_t *n=&ctx->observer_networks[i];
  if(i>=old) *n=seed;
  snprintf(n->ssid,sizeof(n->ssid),"Stress hidden AP %03d generation %d",i,step);
  snprintf(n->bssid,sizeof(n->bssid),"02:11:22:33:%02X:%02X",i/256,i%256);
  n->extended.hidden=i%2;n->extended.present=true;n->extended.rssi_known=true;
  snprintf(n->extended.resolved_ssid,sizeof(n->extended.resolved_ssid),"Resolved AP %03d update %d",i,step);
  n->rssi=-40-(i+step)%50;n->client_count=clients;
  for(int j=0;j<clients;j++) {
   snprintf(n->clients[j],sizeof(n->clients[j]),"02:22:%02X:%02X:%02X:%02X",i/256,i%256,j,step%256);
   snprintf(n->client_vendors[j],sizeof(n->client_vendors[j]),"Changed vendor %03d generation %d",j,step);
  }
 }
 ctx->observer_network_count=count;
 ctx->observer_has_new_networks=count!=old;
 double t=emscripten_get_now();
 if(during_scroll) {
  lv_obj_scroll_to_y(ctx->observer_table,2000,LV_ANIM_OFF);
  ctx->observer_ui_refresh_pending=true;
 }
 observer_sync_changed_tiles(ctx);
 double mutation=emscripten_get_now()-t;
 lv_obj_update_layout(ctx->observer_table);
 double total=emscripten_get_now()-t;
 int total_client_rows=0;
 for(int i=0;i<count;++i) total_client_rows+=(int)lv_obj_get_child_count(ctx->observer_network_ui[i].client_container);
 EM_ASM({globalThis.observerStress=({mutation:$0,total:$1,count:$2,clients:$3,step:$4,tiles:$5,expanded:$6,scrollY:$7,clientRows:$8,totalClientRows:$9,firstTile:$10});},mutation,total,count,clients,step,(int)lv_obj_get_child_count(ctx->observer_table),ctx->observer_network_ui[0].clients_expanded,(int)lv_obj_get_scroll_y(ctx->observer_table),(int)lv_obj_get_child_count(ctx->observer_network_ui[0].client_container),total_client_rows,(uintptr_t)ctx->observer_network_ui[0].tile);
 return total;
}
