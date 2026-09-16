/* Native source forms below preserve firmware geometry and event identities.
 * All outcomes are local scenario data; this adapter performs no radio I/O. */
typedef struct { tab_context_t *ctx; int id,kind; lv_obj_t *surface,*page,*label; } app_rem_job_t;
static app_rem_job_t app_rem_jobs[4][3];
static int app_mitm_story_jobs[4];
static char app_rem_portal_paths[4][20][192];
static char app_rem_portal_errors[4][192];
EM_JS(int,app_rem_template,(int tab,int row,char *name,char *path,char *error),{
 try {const item=emulatorDevice.device.attackTemplates(emulatorDevice.module(tab))[row];if(!item)return 0;
 stringToUTF8(item.name||item.path.split('/').pop(),name,64);stringToUTF8(item.path,path,192);return 1;}
 catch(e){stringToUTF8(e.message||"Portal templates unavailable.",error,192);return 0;}
});
static void app_rem_portals(tab_context_t *ctx) {
 int tab=tab_id_for_ctx(ctx);ctx->evil_twin_html_count=0;app_rem_portal_errors[tab][0]=0;
 for(int i=0;i<20;i++) {if(!app_rem_template(tab,i,ctx->evil_twin_html_files[i],app_rem_portal_paths[tab][i],app_rem_portal_errors[tab]))break;ctx->evil_twin_html_count++;}
}
EM_JS(int,app_rem_begin,(int tab,int kind,const char *bssids,const char *ssid,const char *portal,int saved,char *error),{
 try{return emulatorDevice.device.attackStart(emulatorDevice.module(tab),['rogue_ap','evil_twin','mitm'][kind],
 {bssids:UTF8ToString(bssids).split('\n').filter(Boolean),ssid:UTF8ToString(ssid),portal:UTF8ToString(portal),savedPassword:!!saved});}
 catch(e){stringToUTF8(e.message,error,192);return 0;}
});
EM_JS(int,app_rem_poll,(int id,char *message,int size),{
 const j=emulatorDevice.device.job(id);
 if(!j){stringToUTF8('Operation reset.',message,size);return -1;}
 if(j.state!=='running'&&j.state!=='completed'){stringToUTF8(j.error||'Operation cancelled.',message,size);return -1;}
 const r=j.result||{};
 if(r.demo){stringToUTF8(['Simulated '+j.state,...r.demo.lines].join('\n'),message,size);return j.state==='running'?1:2;}
 const text=['Simulated '+j.state, 'Packets: '+(r.packets||0)+' | Clients: '+(r.clients||[]).length,
 ...(r.clients||[]).slice(0,4).map(c=>[c.mac,c.ip,c.vendor].filter(Boolean).join(' | ')),
 ...(r.events||[]).slice(-2),j.file?'File: '+j.file:"", 'Offline scenario; no credentials acquired.'].filter(Boolean).join('\n');
 stringToUTF8(text,message,size);return j.state==='running'?1:2;
});
EM_JS(void,app_rem_stop,(int id),{try{emulatorDevice.device.attackStop(id);}catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));}});
static lv_obj_t *app_rem_label(tab_context_t *ctx,int kind) {return kind==0?ctx->rogue_ap_status_label:kind==1?ctx->evil_twin_status_label:ctx->mitm_status_label;}
static void app_rem_cancel(tab_context_t *ctx,int kind) {
 app_rem_job_t *j=&app_rem_jobs[tab_id_for_ctx(ctx)][kind];if(j->id)app_model_cancel(j->id);j->id=0;
 if(kind==0){ctx->rogue_ap_monitoring=false;ctx->rogue_ap_task=NULL;}
 if(kind==1){ctx->evil_twin_monitoring=false;ctx->evil_twin_task=NULL;}
}
static void app_rem_deleted(lv_event_t *event) {
 app_rem_job_t *j=lv_event_get_user_data(event);if(!j||!j->ctx)return;tab_context_t *ctx=j->ctx;
 lv_obj_t *obj=lv_event_get_target(event);
 /* A deleted old modal must not clear a replacement modal in the shared slot. */
 if(j->kind==0&&obj!=ctx->rogue_ap_page&&obj!=ctx->rogue_ap_popup_overlay)return;
 if(j->kind==1&&obj!=ctx->evil_twin_overlay)return;
 if(j->kind==2&&obj!=ctx->mitm_popup_overlay)return;
 app_rem_cancel(ctx,j->kind);
 if(j->kind==0){
  if(obj==ctx->rogue_ap_page){ctx->rogue_ap_page=NULL;ctx->rogue_ap_password_input=NULL;ctx->rogue_ap_start_btn=NULL;ctx->rogue_ap_html_dropdown=NULL;
   if(ctx->rogue_ap_keyboard){lv_obj_del(ctx->rogue_ap_keyboard);ctx->rogue_ap_keyboard=NULL;}}
  if(obj==ctx->rogue_ap_popup_overlay){ctx->rogue_ap_popup_overlay=NULL;ctx->rogue_ap_popup=NULL;ctx->rogue_ap_status_label=NULL;}
 }else if(j->kind==1){ctx->evil_twin_overlay=NULL;ctx->evil_twin_popup=NULL;ctx->evil_twin_network_dropdown=NULL;ctx->evil_twin_html_dropdown=NULL;ctx->evil_twin_status_label=NULL;}
 else {ctx->mitm_popup_overlay=NULL;ctx->mitm_popup=NULL;ctx->mitm_status_label=NULL;ctx->mitm_password_input=NULL;ctx->mitm_keyboard=NULL;ctx->mitm_connect_btn=NULL;ctx->mitm_stop_btn=NULL;ctx->mitm_pass_row=NULL;ctx->mitm_btn_row=NULL;ctx->mitm_use_saved_password=false;}
}
static void app_rem_watch(tab_context_t *ctx,int kind,lv_obj_t *obj) {
 app_rem_job_t *j=&app_rem_jobs[tab_id_for_ctx(ctx)][kind];j->ctx=ctx;j->kind=kind;
 (lv_obj_add_event_cb)(obj,app_rem_deleted,LV_EVENT_DELETE,j);
}
static bool app_rem_start(tab_context_t *ctx,int kind,int primary) {
 app_rem_job_t *j=&app_rem_jobs[tab_id_for_ctx(ctx)][kind];if(j->id)return false;
 scan_view_t v=get_scan_view(ctx);lv_obj_t *label=app_rem_label(ctx,kind);
 if(v.sel_count<1||primary<0||primary>=v.sel_count||(kind==0&&v.sel_count!=1)) {
  if(label)lv_label_set_text(label,"Select exactly one target network.");return false;
 }
 int first=v.sel_indices[primary];if(first<0||first>=v.net_count)return false;
 char bssids[1024]={0};snprintf(bssids,sizeof(bssids),"%s",v.nets[first].bssid);
 if(kind==1)for(int i=0;i<v.sel_count;i++){int n=v.sel_indices[i];if(i==primary||n<0||n>=v.net_count)continue;
  strncat(bssids,"\n",sizeof(bssids)-strlen(bssids)-1);strncat(bssids,v.nets[n].bssid,sizeof(bssids)-strlen(bssids)-1);}
 const char *portal="";if(kind!=2){lv_obj_t *dropdown=kind==0?ctx->rogue_ap_html_dropdown:ctx->evil_twin_html_dropdown;
  int selected=dropdown?lv_dropdown_get_selected(dropdown):-1;
  if(selected<0||selected>=ctx->evil_twin_html_count){if(label)lv_label_set_text(label,"Select a scenario portal template.");return false;}
  portal=app_rem_portal_paths[tab_id_for_ctx(ctx)][selected];}
 j->surface=kind==0?ctx->rogue_ap_popup_overlay:kind==1?ctx->evil_twin_overlay:ctx->mitm_popup_overlay;
 j->page=kind==0?ctx->rogue_ap_page:NULL;j->label=label;
 char error[192]={0};j->ctx=ctx;j->kind=kind;j->id=app_rem_begin(tab_id_for_ctx(ctx),kind,bssids,v.nets[first].ssid,portal,ctx->mitm_use_saved_password,error);
 if(kind==2)app_mitm_story_jobs[tab_id_for_ctx(ctx)]=j->id;
 if(!j->id){if(label)lv_label_set_text(label,error[0]?error:"Could not start simulated operation.");return false;}
 if(label)lv_label_set_text(label,"Starting simulated operation...");
 if(kind==0)ctx->rogue_ap_monitoring=true;if(kind==1)ctx->evil_twin_monitoring=true;return true;
}
static void app_rogue_evil_mitm_tick(void) {
 for(int tab=0;tab<4;tab++)for(int kind=0;kind<3;kind++){
  app_rem_job_t *j=&app_rem_jobs[tab][kind];if(!j->id||!j->ctx)continue;
  tab_context_t *ctx=j->ctx;lv_obj_t *label=app_rem_label(ctx,kind);
  lv_obj_t *surface=kind==0?ctx->rogue_ap_popup_overlay:kind==1?ctx->evil_twin_overlay:ctx->mitm_popup_overlay;
  if(!label||label!=j->label||!lv_obj_is_valid(label)||surface!=j->surface||
     !surface||!lv_obj_is_valid(surface)||lv_obj_has_flag(surface,LV_OBJ_FLAG_HIDDEN)||
     (kind==0&&(ctx->rogue_ap_page!=j->page||!j->page||!lv_obj_is_valid(j->page)||lv_obj_has_flag(j->page,LV_OBJ_FLAG_HIDDEN)))){
   app_rem_cancel(ctx,kind);continue;
  }
  char message[1024]={0};int state=app_rem_poll(j->id,message,sizeof(message));lv_label_set_text(label,message);
  lv_obj_set_style_text_color(label,state<0?COLOR_MATERIAL_RED:COLOR_MATERIAL_GREEN,0);
  if(state==1)continue;j->id=0;
  if(kind==0){ctx->rogue_ap_monitoring=false;ctx->rogue_ap_task=NULL;}
  if(kind==1){ctx->evil_twin_monitoring=false;ctx->evil_twin_task=NULL;}
  if(kind==2){
   if(ctx->mitm_stop_btn)lv_obj_add_flag(ctx->mitm_stop_btn,LV_OBJ_FLAG_HIDDEN);
   if(ctx->mitm_btn_row)lv_obj_clear_flag(ctx->mitm_btn_row,LV_OBJ_FLAG_HIDDEN);
   if(state<0&&ctx->mitm_pass_row)lv_obj_clear_flag(ctx->mitm_pass_row,LV_OBJ_FLAG_HIDDEN);
  }
 }
}
static void do_evil_twin_start(void){tab_context_t *ctx=get_current_ctx();if(ctx&&ctx->evil_twin_network_dropdown)app_rem_start(ctx,1,lv_dropdown_get_selected(ctx->evil_twin_network_dropdown));}
static void evil_twin_close_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 app_rem_job_t *j=&app_rem_jobs[tab_id_for_ctx(ctx)][1];if(j->id)app_rem_stop(j->id);
 if(ctx->evil_twin_overlay)lv_obj_del(ctx->evil_twin_overlay);
 ctx->observer_attack_return_to_observer=false;clear_observer_attack_override(ctx);
}
static void rogue_ap_popup_close_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 app_rem_job_t *j=&app_rem_jobs[tab_id_for_ctx(ctx)][0];if(j->id)app_rem_stop(j->id);
 if(ctx->rogue_ap_popup_overlay)lv_obj_del(ctx->rogue_ap_popup_overlay);
}
static void rogue_ap_back_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;bool observer=ctx->observer_attack_return_to_observer;
 app_rem_cancel(ctx,0);if(ctx->rogue_ap_popup_overlay)lv_obj_del(ctx->rogue_ap_popup_overlay);
 if(ctx->rogue_ap_page)lv_obj_del(ctx->rogue_ap_page);rogue_ap_page=NULL;
 ctx->observer_attack_return_to_observer=false;clear_observer_attack_override(ctx);
 if(observer)show_observer_page();else show_scan_page();
}
static void app_rem_rogue_keyboard_cb(lv_event_t *e){
 lv_event_code_t code=lv_event_get_code(e);
 if(code==LV_EVENT_READY||code==LV_EVENT_CANCEL)lv_obj_add_flag(lv_event_get_target(e),LV_OBJ_FLAG_HIDDEN);
}
static void rogue_ap_start_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 if(ctx->rogue_ap_keyboard)lv_obj_add_flag(ctx->rogue_ap_keyboard,LV_OBJ_FLAG_HIDDEN);
 show_rogue_ap_popup(ctx);app_rem_start(ctx,0,0);
}
static void mitm_connect_and_start_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 if(ctx->mitm_keyboard)lv_obj_add_flag(ctx->mitm_keyboard,LV_OBJ_FLAG_HIDDEN);
 if(!app_rem_start(ctx,2,0))return;
 if(ctx->mitm_pass_row)lv_obj_add_flag(ctx->mitm_pass_row,LV_OBJ_FLAG_HIDDEN);
 if(ctx->mitm_btn_row)lv_obj_add_flag(ctx->mitm_btn_row,LV_OBJ_FLAG_HIDDEN);
 if(ctx->mitm_stop_btn)lv_obj_clear_flag(ctx->mitm_stop_btn,LV_OBJ_FLAG_HIDDEN);
}
static void mitm_popup_close_cb(lv_event_t *e){
 tab_context_t *ctx=get_current_ctx();if(!ctx)return;app_rem_job_t *j=&app_rem_jobs[tab_id_for_ctx(ctx)][2];
 if(e&&lv_event_get_target(e)==ctx->mitm_stop_btn&&j->id){app_rem_stop(j->id);app_rogue_evil_mitm_tick();return;}
 app_rem_cancel(ctx,2);if(ctx->mitm_popup_overlay)lv_obj_del(ctx->mitm_popup_overlay);
 ctx->observer_attack_return_to_observer=false;clear_observer_attack_override(ctx);
}
static void show_mitm_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->mitm_popup != NULL) return;

    scan_view_t v = get_scan_view(ctx);
    if (v.sel_count == 0) return;
    int idx = v.sel_indices[0];
    if (idx < 0 || idx >= v.net_count) return;

    wifi_network_t *net = &v.nets[idx];
    const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
    bool is_open = wifi_network_security_is_open(net->security);

    char mitm_found_password[65] = {0};
    bool mitm_password_known = false;
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    ctx->mitm_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->mitm_popup_overlay);
    lv_obj_set_size(ctx->mitm_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->mitm_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->mitm_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->mitm_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->mitm_popup_overlay, LV_OBJ_FLAG_CLICKABLE);

    ctx->mitm_popup = lv_obj_create(ctx->mitm_popup_overlay);
    lv_obj_set_size(ctx->mitm_popup, 550, 380);
    lv_obj_center(ctx->mitm_popup);
    lv_obj_set_style_bg_color(ctx->mitm_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->mitm_popup, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(ctx->mitm_popup, 2, 0);
    lv_obj_set_style_radius(ctx->mitm_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->mitm_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->mitm_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->mitm_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->mitm_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->mitm_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->mitm_popup, 12, 0);
    lv_obj_set_flex_align(ctx->mitm_popup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(ctx->mitm_popup);
    lv_label_set_text_fmt(title, LV_SYMBOL_EYE_OPEN "  MITM Capture - %s", ssid_display);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_TEAL, 0);

    lv_obj_t *info_label = lv_label_create(ctx->mitm_popup);
    const char *mitm_vendor_display = strlen(net->vendor) > 0 ? net->vendor : "-";
    lv_label_set_text_fmt(info_label, "BSSID: %s | %s | %s\nVendor: %s",
                          net->bssid, net->band, net->security, mitm_vendor_display);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);

    ctx->mitm_pass_row = lv_obj_create(ctx->mitm_popup);
    lv_obj_set_size(ctx->mitm_pass_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctx->mitm_pass_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->mitm_pass_row, 0, 0);
    lv_obj_set_style_pad_all(ctx->mitm_pass_row, 0, 0);
    lv_obj_set_flex_flow(ctx->mitm_pass_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->mitm_pass_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->mitm_pass_row, 10, 0);
    lv_obj_clear_flag(ctx->mitm_pass_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pass_label = lv_label_create(ctx->mitm_pass_row);
    lv_label_set_text(pass_label, "Password:");
    lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);

    ctx->mitm_password_input = lv_textarea_create(ctx->mitm_pass_row);
    lv_obj_set_size(ctx->mitm_password_input, 320, 40);
    lv_textarea_set_one_line(ctx->mitm_password_input, true);
    lv_textarea_set_placeholder_text(ctx->mitm_password_input, "WiFi password");
    lv_textarea_set_password_mode(ctx->mitm_password_input, true);
    lv_obj_set_style_bg_color(ctx->mitm_password_input, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(ctx->mitm_password_input, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_border_width(ctx->mitm_password_input, 1, 0);
    lv_obj_set_style_text_color(ctx->mitm_password_input, lv_color_hex(0xFFFFFF), 0);
    app_bind(ctx->mitm_password_input, mitm_password_input_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_mitm_popup.ctx--mitm_password_input.clicked.1c928f645df5"));
    app_bind(ctx->mitm_password_input, mitm_password_input_cb, LV_EVENT_VALUE_CHANGED, NULL, app_template_line("ui.show_mitm_popup.ctx--mitm_password_input.value_changed.fac43ecf2ab9"));

    if (mitm_password_known) {
        lv_textarea_set_text(ctx->mitm_password_input, mitm_found_password);
    } else if (!is_open) {
        lv_textarea_set_placeholder_text(ctx->mitm_password_input,
                                         "Saved password on JanOS");
    } else {
        lv_obj_add_flag(ctx->mitm_pass_row, LV_OBJ_FLAG_HIDDEN);
    }
    // For every secured network, let JanOS resolve --saved against eviltwin.txt,
    // portals.txt and home.txt. If it fails, the callback switches to manual input.
    ctx->mitm_use_saved_password = mitm_password_known || !is_open;

    ctx->mitm_status_label = lv_label_create(ctx->mitm_popup);
    if (mitm_password_known) {
        lv_label_set_text(ctx->mitm_status_label,
                          "Known password found. Press Connect & Start.");
    } else if (!is_open) {
        lv_label_set_text(ctx->mitm_status_label,
                          "Simulated connection; saved-password mode selected.\n"
                          "No credentials are acquired by this emulator.");
    } else {
        lv_label_set_text(ctx->mitm_status_label,
                          "Open network. Press Connect & Start.");
    }
    lv_obj_set_style_text_font(ctx->mitm_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->mitm_status_label,
        (mitm_password_known || !is_open) ? COLOR_MATERIAL_GREEN
                                         : lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(ctx->mitm_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ctx->mitm_status_label, lv_pct(100));

    ctx->mitm_btn_row = lv_obj_create(ctx->mitm_popup);
    lv_obj_set_size(ctx->mitm_btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctx->mitm_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->mitm_btn_row, 0, 0);
    lv_obj_set_style_pad_all(ctx->mitm_btn_row, 0, 0);
    lv_obj_set_flex_flow(ctx->mitm_btn_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->mitm_btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->mitm_btn_row, 8, 0);
    lv_obj_clear_flag(ctx->mitm_btn_row, LV_OBJ_FLAG_SCROLLABLE);

    ctx->mitm_connect_btn = lv_btn_create(ctx->mitm_btn_row);
    lv_obj_set_size(ctx->mitm_connect_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(ctx->mitm_connect_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(ctx->mitm_connect_btn, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ctx->mitm_connect_btn, 8, 0);
    app_bind(ctx->mitm_connect_btn, mitm_connect_and_start_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_mitm_popup.ctx--mitm_connect_btn.clicked.5ba18a07e62c"));

    lv_obj_t *connect_label = lv_label_create(ctx->mitm_connect_btn);
    lv_label_set_text(connect_label, "Connect & Start");
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
    lv_obj_center(connect_label);

    lv_obj_t *mitm_cancel_btn = lv_btn_create(ctx->mitm_btn_row);
    lv_obj_set_size(mitm_cancel_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(mitm_cancel_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_color(mitm_cancel_btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(mitm_cancel_btn, 8, 0);
    app_bind(mitm_cancel_btn, mitm_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_mitm_popup.mitm_cancel_btn.clicked.33ecd9a04152"));

    lv_obj_t *cancel_label = lv_label_create(mitm_cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_label);

    ctx->mitm_stop_btn = lv_btn_create(ctx->mitm_popup);
    lv_obj_set_size(ctx->mitm_stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(ctx->mitm_stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(ctx->mitm_stop_btn, lv_color_hex(0xCC0000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ctx->mitm_stop_btn, 8, 0);
    app_bind(ctx->mitm_stop_btn, mitm_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_mitm_popup.ctx--mitm_stop_btn.clicked.043496b1091e"));
    lv_obj_add_flag(ctx->mitm_stop_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *stop_label2 = lv_label_create(ctx->mitm_stop_btn);
    lv_label_set_text(stop_label2, "STOP CAPTURE");
    lv_obj_set_style_text_font(stop_label2, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label2);

    app_rem_watch(ctx, 2, ctx->mitm_popup_overlay);

    ctx->mitm_keyboard = lv_keyboard_create(ctx->mitm_popup_overlay);
    lv_obj_set_size(ctx->mitm_keyboard, lv_pct(100), 260);
    lv_obj_align(ctx->mitm_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    style_on_screen_keyboard(ctx->mitm_keyboard);
    lv_keyboard_set_textarea(ctx->mitm_keyboard, ctx->mitm_password_input);
    app_bind(ctx->mitm_keyboard, mitm_keyboard_cb, LV_EVENT_ALL, NULL, app_template_line("ui.show_mitm_popup.ctx--mitm_keyboard.all.b3b9b2b0cf01"));
    lv_obj_add_flag(ctx->mitm_keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void show_evil_twin_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->evil_twin_popup != NULL) return;  // Already showing in this tab

    app_rem_portals(ctx);
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->evil_twin_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->evil_twin_overlay);
    lv_obj_set_size(ctx->evil_twin_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->evil_twin_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->evil_twin_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->evil_twin_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->evil_twin_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks

    // Create popup as child of overlay
    ctx->evil_twin_popup = lv_obj_create(ctx->evil_twin_overlay);
    lv_obj_set_size(ctx->evil_twin_popup, 600, 550);
    lv_obj_center(ctx->evil_twin_popup);
    lv_obj_set_style_bg_color(ctx->evil_twin_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->evil_twin_popup, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(ctx->evil_twin_popup, 2, 0);
    lv_obj_set_style_radius(ctx->evil_twin_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->evil_twin_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->evil_twin_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->evil_twin_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->evil_twin_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->evil_twin_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->evil_twin_popup, 12, 0);

    // Title
    lv_obj_t *title = lv_label_create(ctx->evil_twin_popup);
    lv_label_set_text(title, "Evil Twin Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);

    // Network dropdown container
    lv_obj_t *net_cont = lv_obj_create(ctx->evil_twin_popup);
    lv_obj_set_size(net_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(net_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(net_cont, 0, 0);
    lv_obj_set_style_pad_all(net_cont, 0, 0);
    lv_obj_set_flex_flow(net_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(net_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(net_cont, 10, 0);
    lv_obj_clear_flag(net_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *net_label = lv_label_create(net_cont);
    lv_label_set_text(net_label, "Evil Twin Network:");
    lv_obj_set_style_text_font(net_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(net_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(net_label, 180);

    ctx->evil_twin_network_dropdown = lv_dropdown_create(net_cont);
    lv_obj_set_width(ctx->evil_twin_network_dropdown, 350);
    lv_obj_set_style_bg_color(ctx->evil_twin_network_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->evil_twin_network_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(ctx->evil_twin_network_dropdown, lv_color_hex(0x555555), 0);

    // Build network dropdown options from selected networks
    char network_options[1024] = "";
    {
        scan_view_t v = get_scan_view(ctx);
        for (int i = 0; i < v.sel_count; i++) {
            int idx = v.sel_indices[i];
            if (idx >= 0 && idx < v.net_count) {
                const char *ssid = strlen(v.nets[idx].ssid) > 0 ? v.nets[idx].ssid : "(Hidden)";
                if (i > 0) strncat(network_options, "\n", sizeof(network_options) - strlen(network_options) - 1);
                strncat(network_options, ssid, sizeof(network_options) - strlen(network_options) - 1);
            }
        }
    }
    lv_dropdown_set_options(ctx->evil_twin_network_dropdown, network_options);

    // Style dropdown list (dark background when opened)
    lv_obj_t *net_list = lv_dropdown_get_list(ctx->evil_twin_network_dropdown);
    if (net_list) {
        lv_obj_set_style_bg_color(net_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(net_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(net_list, lv_color_hex(0x555555), 0);
    }

    // HTML dropdown container
    lv_obj_t *html_cont = lv_obj_create(ctx->evil_twin_popup);
    lv_obj_set_size(html_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(html_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(html_cont, 0, 0);
    lv_obj_set_style_pad_all(html_cont, 0, 0);
    lv_obj_set_flex_flow(html_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(html_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(html_cont, 10, 0);
    lv_obj_clear_flag(html_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *html_label = lv_label_create(html_cont);
    lv_label_set_text(html_label, "Portal HTML:");
    lv_obj_set_style_text_font(html_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(html_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(html_label, 180);

    ctx->evil_twin_html_dropdown = lv_dropdown_create(html_cont);
    lv_obj_set_width(ctx->evil_twin_html_dropdown, 350);
    lv_obj_set_style_bg_color(ctx->evil_twin_html_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->evil_twin_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(ctx->evil_twin_html_dropdown, lv_color_hex(0x555555), 0);

    // Build HTML dropdown options
    char html_options[2048] = "";
    for (int i = 0; i < ctx->evil_twin_html_count; i++) {
        if (i > 0) strncat(html_options, "\n", sizeof(html_options) - strlen(html_options) - 1);
        strncat(html_options, ctx->evil_twin_html_files[i], sizeof(html_options) - strlen(html_options) - 1);
    }
    lv_dropdown_set_options(ctx->evil_twin_html_dropdown, html_options);

    // Style dropdown list (dark background when opened)
    lv_obj_t *html_list = lv_dropdown_get_list(ctx->evil_twin_html_dropdown);
    if (html_list) {
        lv_obj_set_style_bg_color(html_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(html_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(html_list, lv_color_hex(0x555555), 0);
    }

    // START ATTACK button
    lv_obj_t *start_btn = lv_btn_create(ctx->evil_twin_popup);
    lv_obj_set_size(start_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(start_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0xCC7000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(start_btn, 8, 0);
    app_bind(start_btn, evil_twin_start_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_evil_twin_popup.start_btn.clicked.d6dd0f7a2da2"));

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START ATTACK");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_18, 0);
    lv_obj_center(start_label);

    // Status label (scrollable area)
    lv_obj_t *status_cont = lv_obj_create(ctx->evil_twin_popup);
    lv_obj_set_size(status_cont, lv_pct(100), 200);
    lv_obj_set_style_bg_color(status_cont, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(status_cont, 0, 0);
    lv_obj_set_style_radius(status_cont, 8, 0);
    lv_obj_set_style_pad_all(status_cont, 12, 0);
    lv_obj_add_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);

    ctx->evil_twin_status_label = lv_label_create(status_cont);
    lv_label_set_text(ctx->evil_twin_status_label, "Select network and portal, then click START ATTACK");
    lv_obj_set_style_text_font(ctx->evil_twin_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->evil_twin_status_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_width(ctx->evil_twin_status_label, lv_pct(100));
    lv_label_set_long_mode(ctx->evil_twin_status_label, LV_LABEL_LONG_WRAP);

    // CLOSE button (hidden initially, shown when password captured)
    lv_obj_t *close_btn = lv_btn_create(ctx->evil_twin_popup);
    lv_obj_set_size(close_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    app_bind(close_btn, evil_twin_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_evil_twin_popup.close_btn.clicked.9fb1b503e09d"));
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_HIDDEN);  // Hidden initially

    lv_obj_t *et_close_label = lv_label_create(close_btn);
    lv_label_set_text(et_close_label, "CLOSE");
    lv_obj_set_style_text_font(et_close_label, &lv_font_montserrat_18, 0);
    lv_obj_center(et_close_label);

    // STOP button (always visible - sends stop command and closes popup)
    lv_obj_t *stop_btn = lv_btn_create(ctx->evil_twin_popup);  // Use ctx->evil_twin_popup!
    lv_obj_set_size(stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xB71C1C), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    app_bind(stop_btn, evil_twin_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_evil_twin_popup.stop_btn.clicked.ddc99face1ee"));

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
    if(!ctx->evil_twin_html_count){
        lv_label_set_text(ctx->evil_twin_status_label,app_rem_portal_errors[tab_id_for_ctx(ctx)][0]?app_rem_portal_errors[tab_id_for_ctx(ctx)]:"No portal templates available. Close and retry.");
        lv_obj_add_state(start_btn,LV_STATE_DISABLED);
    }
    app_rem_watch(ctx, 1, ctx->evil_twin_overlay);
}
static void show_rogue_ap_page(void)
{
    ESP_LOGI(TAG, "Showing Rogue AP page");

    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();

    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }

    if (ctx->observer_attack_return_to_observer) {
        if (ctx->observer_running) {
            pause_observer_for_attack(ctx);
        }
        if (ctx->popup_open) {
            destroy_network_popup_ui(ctx);
        }
        if (deauth_popup_obj != NULL) {
            destroy_deauth_popup_ui();
        }
    }

    hide_all_pages(ctx);

    // If page already exists for this tab, just show it
    if (ctx->rogue_ap_page) {
        lv_obj_clear_flag(ctx->rogue_ap_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->rogue_ap_page;
        rogue_ap_page = ctx->rogue_ap_page;
        ESP_LOGI(TAG, "Showing existing rogue AP page for tab %d", current_tab);
        return;
    }

    ESP_LOGI(TAG, "Creating new rogue AP page for tab %d", current_tab);

    // Create rogue AP page
    ctx->rogue_ap_page = lv_obj_create(container);
    rogue_ap_page = ctx->rogue_ap_page;
    lv_obj_set_size(ctx->rogue_ap_page, lv_pct(100), lv_pct(100));
    lv_obj_align(ctx->rogue_ap_page, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->rogue_ap_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->rogue_ap_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->rogue_ap_page, 15, 0);
    lv_obj_set_flex_flow(ctx->rogue_ap_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->rogue_ap_page, 10, 0);

    // Header
    lv_obj_t *header = lv_obj_create(ctx->rogue_ap_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    app_bind(back_btn, rogue_ap_back_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_rogue_ap_page.back_btn.clicked.962520e8c7c5"));

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Rogue AP Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);

    // Get selected network SSID and try to find known password
    {
        scan_view_t v = get_scan_view(ctx);
        if (v.sel_count > 0) {
            int idx = v.sel_indices[0];
            if (idx >= 0 && idx < v.net_count) {
                strncpy(ctx->rogue_ap_ssid, v.nets[idx].ssid, sizeof(ctx->rogue_ap_ssid) - 1);
                ctx->rogue_ap_ssid[sizeof(ctx->rogue_ap_ssid) - 1] = '\0';
            }
        }
    }
    memset(ctx->rogue_ap_password, 0, sizeof(ctx->rogue_ap_password));

    evil_twin_entry_count = 0;
    // Target network info
    lv_obj_t *target_label = lv_label_create(ctx->rogue_ap_page);
    lv_label_set_text_fmt(target_label, "Target SSID: %s", ctx->rogue_ap_ssid);
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xCCCCCC), 0);

    // Password section
    lv_obj_t *pass_section = lv_obj_create(ctx->rogue_ap_page);
    lv_obj_set_size(pass_section, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pass_section, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(pass_section, 0, 0);
    lv_obj_set_style_radius(pass_section, 8, 0);
    lv_obj_set_style_pad_all(pass_section, 15, 0);
    lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pass_section, 10, 0);
    lv_obj_clear_flag(pass_section, LV_OBJ_FLAG_SCROLLABLE);

    // Check if password is known
    bool password_known = false;
    for (int i = 0; i < evil_twin_entry_count; i++) {
        if (strcmp(evil_twin_entries[i].ssid, ctx->rogue_ap_ssid) == 0) {
            strncpy(ctx->rogue_ap_password, evil_twin_entries[i].password, sizeof(ctx->rogue_ap_password) - 1);
            ctx->rogue_ap_password[sizeof(ctx->rogue_ap_password) - 1] = '\0';
            password_known = true;
            break;
        }
    }

    if (password_known) {
        // Show known password as label
        lv_obj_t *pass_label = lv_label_create(pass_section);
        lv_label_set_text(pass_label, "Known Password:");
        lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t *pass_value = lv_label_create(pass_section);
        lv_label_set_text_fmt(pass_value, "%s", ctx->rogue_ap_password);
        lv_obj_set_style_text_font(pass_value, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pass_value, COLOR_MATERIAL_GREEN, 0);
    } else {
        // Show password input
        lv_obj_t *pass_label = lv_label_create(pass_section);
        lv_label_set_text(pass_label, "Enter WiFi Password:");
        lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);

        ctx->rogue_ap_password_input = lv_textarea_create(pass_section);
        lv_obj_set_size(ctx->rogue_ap_password_input, lv_pct(100), 45);
        lv_textarea_set_one_line(ctx->rogue_ap_password_input, true);
        lv_textarea_set_placeholder_text(ctx->rogue_ap_password_input, "WiFi password");
        lv_obj_set_style_bg_color(ctx->rogue_ap_password_input, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(ctx->rogue_ap_password_input, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_border_width(ctx->rogue_ap_password_input, 1, 0);
        lv_obj_set_style_text_color(ctx->rogue_ap_password_input, lv_color_hex(0xFFFFFF), 0);

        // Keyboard (hidden, activated on click)
        ctx->rogue_ap_keyboard = lv_keyboard_create(container);
        lv_obj_set_size(ctx->rogue_ap_keyboard, lv_pct(100), 260);
        lv_obj_align(ctx->rogue_ap_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        style_on_screen_keyboard(ctx->rogue_ap_keyboard);
        lv_keyboard_set_textarea(ctx->rogue_ap_keyboard, ctx->rogue_ap_password_input);
        lv_obj_add_flag(ctx->rogue_ap_keyboard, LV_OBJ_FLAG_HIDDEN);

        // Add event handler to show keyboard when textarea is clicked
        app_bind(ctx->rogue_ap_password_input, rogue_ap_password_focus_cb, LV_EVENT_FOCUSED, NULL, app_template_line("ui.show_rogue_ap_page.ctx--rogue_ap_password_input.focused.b4af443dc164"));
    }

    // Fetch HTML files
    app_rem_portals(ctx);
    // HTML dropdown
    lv_obj_t *html_label = lv_label_create(ctx->rogue_ap_page);
    lv_label_set_text(html_label, "Select Portal HTML:");
    lv_obj_set_style_text_font(html_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(html_label, lv_color_hex(0xFFFFFF), 0);

    ctx->rogue_ap_html_dropdown = lv_dropdown_create(ctx->rogue_ap_page);
    lv_obj_set_width(ctx->rogue_ap_html_dropdown, lv_pct(100));
    lv_obj_set_height(ctx->rogue_ap_html_dropdown, 45);
    lv_obj_set_style_bg_color(ctx->rogue_ap_html_dropdown, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_text_color(ctx->rogue_ap_html_dropdown, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(ctx->rogue_ap_html_dropdown, lv_color_hex(0x555555), 0);

    // Build HTML dropdown options
    char html_options[2048] = "";
    for (int i = 0; i < ctx->evil_twin_html_count; i++) {
        if (i > 0) strncat(html_options, "\n", sizeof(html_options) - strlen(html_options) - 1);
        strncat(html_options, ctx->evil_twin_html_files[i], sizeof(html_options) - strlen(html_options) - 1);
    }
    lv_dropdown_set_options(ctx->rogue_ap_html_dropdown, html_options);

    // Style dropdown list
    lv_obj_t *html_list = lv_dropdown_get_list(ctx->rogue_ap_html_dropdown);
    if (html_list) {
        lv_obj_set_style_bg_color(html_list, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(html_list, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(html_list, lv_color_hex(0x555555), 0);
    }

    // Start Rogue AP button
    ctx->rogue_ap_start_btn = lv_btn_create(ctx->rogue_ap_page);
    lv_obj_set_size(ctx->rogue_ap_start_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(ctx->rogue_ap_start_btn, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_bg_color(ctx->rogue_ap_start_btn, lv_color_lighten(COLOR_MATERIAL_CYAN, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ctx->rogue_ap_start_btn, 8, 0);
    app_bind(ctx->rogue_ap_start_btn, rogue_ap_start_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_rogue_ap_page.ctx--rogue_ap_start_btn.clicked.bdcb6faf6d61"));

    lv_obj_t *btn_label = lv_label_create(ctx->rogue_ap_start_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_POWER " Start Rogue AP");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0x000000), 0);
    lv_obj_center(btn_label);

    if(!ctx->evil_twin_html_count){
        lv_obj_t *error_label=lv_label_create(ctx->rogue_ap_page);
        lv_label_set_text(error_label,app_rem_portal_errors[tab_id_for_ctx(ctx)][0]?app_rem_portal_errors[tab_id_for_ctx(ctx)]:"No portal templates available. Go back and retry.");
        lv_obj_set_width(error_label,lv_pct(100));lv_obj_set_style_text_color(error_label,COLOR_MATERIAL_RED,0);
        lv_obj_add_state(ctx->rogue_ap_start_btn,LV_STATE_DISABLED);
    }
    app_rem_watch(ctx, 0, ctx->rogue_ap_page);
    if(ctx->rogue_ap_keyboard) app_bind_adapter(ctx->rogue_ap_keyboard, app_rem_rogue_keyboard_cb, LV_EVENT_ALL, ctx, "rogue-ap/keyboard");
    // Set current visible page
    ctx->current_visible_page = ctx->rogue_ap_page;
}
static void show_rogue_ap_popup(tab_context_t *ctx)
{
    if (!ctx) return;
    if (ctx->rogue_ap_popup_overlay != NULL) return;  // Already showing in this tab

    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    // Create modal overlay
    ctx->rogue_ap_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->rogue_ap_popup_overlay);
    lv_obj_set_size(ctx->rogue_ap_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->rogue_ap_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->rogue_ap_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->rogue_ap_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->rogue_ap_popup_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks

    // Create popup as child of overlay
    ctx->rogue_ap_popup = lv_obj_create(ctx->rogue_ap_popup_overlay);
    lv_obj_set_size(ctx->rogue_ap_popup, 550, 450);
    lv_obj_center(ctx->rogue_ap_popup);
    lv_obj_set_style_bg_color(ctx->rogue_ap_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->rogue_ap_popup, COLOR_MATERIAL_CYAN, 0);
    lv_obj_set_style_border_width(ctx->rogue_ap_popup, 2, 0);
    lv_obj_set_style_radius(ctx->rogue_ap_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->rogue_ap_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->rogue_ap_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->rogue_ap_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->rogue_ap_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->rogue_ap_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->rogue_ap_popup, 12, 0);

    // Title
    lv_obj_t *title = lv_label_create(ctx->rogue_ap_popup);
    lv_label_set_text(title, "Rogue AP Running");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);

    // Status label (scrollable)
    ctx->rogue_ap_status_label = lv_label_create(ctx->rogue_ap_popup);
    lv_label_set_text(ctx->rogue_ap_status_label, "Starting Rogue AP...");
    lv_obj_set_style_text_font(ctx->rogue_ap_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->rogue_ap_status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(ctx->rogue_ap_status_label, lv_pct(100));
    lv_label_set_long_mode(ctx->rogue_ap_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(ctx->rogue_ap_status_label, 1);

    // Close button
    lv_obj_t *close_btn = lv_btn_create(ctx->rogue_ap_popup);
    lv_obj_set_size(close_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    app_bind(close_btn, rogue_ap_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_rogue_ap_popup.close_btn.clicked.1ef5919230a3"));

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Stop Rogue AP");
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(close_label);

    // Start monitoring task
    app_rem_watch(ctx, 0, ctx->rogue_ap_popup_overlay);
}
