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
    if(!ctx->observer_networks){emu_unsupported("Observer allocation failed");return;}
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
        for(int c=0;c<MAX_CLIENTS_PER_NETWORK;c++) {
            if(!app_observer_client_mac(tab,i,c,net->clients[c],sizeof(net->clients[c])))break;
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
  if(ctx->observer_start_btn)lv_obj_remove_state(ctx->observer_start_btn,LV_STATE_DISABLED);
  if(ctx->observer_stop_btn)lv_obj_add_state(ctx->observer_stop_btn,LV_STATE_DISABLED);
  if(ctx->observer_status_label)lv_label_set_text(ctx->observer_status_label,"Simulation stopped or reset; cached rows retained.");
  return;
 }
 for(int i=0;i<ctx->observer_network_count;i++){
  observer_network_t *net=&ctx->observer_networks[i];net->rssi=app_observer_live_rssi(tab,net->bssid,net->rssi);
  lv_obj_t *label=ctx->observer_inspect_info_labels&&i<ctx->observer_inspect_label_count?ctx->observer_inspect_info_labels[i]:NULL;
  if(label&&lv_obj_is_valid(label)){char text[NETWORK_INFO_BUF];format_network_info(text,sizeof(text),net->bssid,net->channel,net->band,NULL,net->rssi,creds_badge(ctx,net->ssid),NULL,net->uptime,net->vendor);lv_label_set_text(label,text);}
 }
 for(int i=0;i<1024;i++){
  lv_obj_t *row=app_bindings[i].object;const char *id=app_bindings[i].id;
  size_t len=strlen(id);if(!row||len<7||strcmp(id+len-7,"/client")||!app_descendant(row,ctx->observer_table))continue;
  lv_obj_t *label=lv_obj_get_child(row,0);if(!label||!lv_obj_check_type(label,&lv_label_class))continue;
  bool active=app_observer_live_client(tab,lv_label_get_text(label));
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
