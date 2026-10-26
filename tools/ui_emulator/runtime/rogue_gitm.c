/* Rogue GITM setup reuses the retained Capture Gateway live view. The model
 * accepts only scenario network IDs and keeps no entered passwords. */
typedef struct {
 tab_context_t *ctx;int tab,victim_index,uplink_count,uplinks[MAX_NETWORKS];
 lv_obj_t *overlay,*card,*uplink,*home_row,*home,*mirror,*status,*keyboard;
} app_rogue_gitm_t;
static app_rogue_gitm_t app_rogue_gitm[4];

static void app_rogue_gitm_close(app_rogue_gitm_t *flow){
 if(!flow||!flow->overlay)return;
 if(flow->home)lv_textarea_set_text(flow->home,"");
 if(flow->mirror)lv_textarea_set_text(flow->mirror,"");
 lv_obj_t *overlay=flow->overlay;
 flow->overlay=flow->card=flow->uplink=flow->home_row=flow->home=
  flow->mirror=flow->status=flow->keyboard=NULL;
 if(lv_obj_is_valid(overlay))lv_obj_del(overlay);
}
static void app_rogue_gitm_cancel_cb(lv_event_t *event){
 app_rogue_gitm_close(lv_event_get_user_data(event));
}
static void app_rogue_gitm_input_cb(lv_event_t *event){
 app_rogue_gitm_t *flow=lv_event_get_user_data(event);
 if(!flow->keyboard)return;
 if(lv_event_get_code(event)==LV_EVENT_FOCUSED||lv_event_get_code(event)==LV_EVENT_CLICKED){
  lv_keyboard_set_textarea(flow->keyboard,lv_event_get_target(event));
  lv_obj_remove_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
 }
}
static void app_rogue_gitm_keyboard_cb(lv_event_t *event){
 app_rogue_gitm_t *flow=lv_event_get_user_data(event);
 if(flow->keyboard&&(lv_event_get_code(event)==LV_EVENT_READY||lv_event_get_code(event)==LV_EVENT_CANCEL))
  lv_obj_add_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
}
static void app_rogue_gitm_sync_uplink(app_rogue_gitm_t *flow){
 if(!flow||!flow->uplink)return;
 int selected=lv_dropdown_get_selected(flow->uplink);
 if(selected<0||selected>=flow->uplink_count)return;
 scan_view_t view=get_scan_view(flow->ctx);
 int index=flow->uplinks[selected];if(index<0||index>=view.net_count)return;
 bool open=wifi_network_security_is_open(view.nets[index].security);
 if(open){lv_textarea_set_text(flow->home,"");lv_obj_add_flag(flow->home_row,LV_OBJ_FLAG_HIDDEN);}
 else lv_obj_remove_flag(flow->home_row,LV_OBJ_FLAG_HIDDEN);
}
static void app_rogue_gitm_uplink_cb(lv_event_t *event){
 app_rogue_gitm_sync_uplink(lv_event_get_user_data(event));
}
EM_JS(int,app_rogue_gitm_begin_js,(int tab,const char *victim,const char *resolved,const char *uplink,
                                    const char *mirror,const char *home,char *error,int size),{
 try{return emulatorDevice.device.toolStart(emulatorDevice.module(tab),'rogue_gitm',{
  victimId:UTF8ToString(victim),resolvedVictimSsid:UTF8ToString(resolved),
  upstreamId:UTF8ToString(uplink),mirrorPass:UTF8ToString(mirror),upstreamPass:UTF8ToString(home)});}
 catch(e){stringToUTF8(e.message,error,size);return 0;}
});
static void app_rogue_gitm_start_cb(lv_event_t *event){
 app_rogue_gitm_t *flow=lv_event_get_user_data(event);if(!flow||!flow->ctx)return;
 scan_view_t view=get_scan_view(flow->ctx);
 if(flow->victim_index<0||flow->victim_index>=view.net_count)return;
 int selected=lv_dropdown_get_selected(flow->uplink);
 if(selected<0||selected>=flow->uplink_count)return;
 int up_index=flow->uplinks[selected];if(up_index<0||up_index>=view.net_count)return;
 wifi_network_t *victim=&view.nets[flow->victim_index],*upstream=&view.nets[up_index];
 const char *mirror=lv_textarea_get_text(flow->mirror);
 const char *home=wifi_network_security_is_open(upstream->security)?"":lv_textarea_get_text(flow->home);
 if(!gitm_ctx(flow->ctx)){lv_label_set_text(flow->status,"Could not open the offline live view.");return;}
 char error[192]={0};
 int job=app_rogue_gitm_begin_js(flow->tab,victim->bssid,victim->ssid,upstream->bssid,
                                  mirror?mirror:"",home?home:"",error,sizeof(error));
 if(!job){lv_label_set_text(flow->status,error);return;}
 tab_context_t *ctx=flow->ctx;int tab=flow->tab;
 char victim_ssid[33],victim_bssid[18],upstream_ssid[33];int channel=victim->channel;
 snprintf(victim_ssid,sizeof(victim_ssid),"%s",victim->ssid);
 snprintf(victim_bssid,sizeof(victim_bssid),"%s",victim->bssid);
 snprintf(upstream_ssid,sizeof(upstream_ssid),"%s",upstream->ssid);
 app_rogue_gitm_close(flow);
 gitm_ctx_t *g=ctx->gitm;g->session_active=true;
 show_gitm_page();
 if(!g->page){app_model_cancel(job);g->session_active=false;return;}
 g->rogue=true;g->victim_channel=channel;
 snprintf(g->victim_ssid,sizeof(g->victim_ssid),"%s",victim_ssid);
 snprintf(g->victim_bssid,sizeof(g->victim_bssid),"%s",victim_bssid);
 snprintf(g->ap_ssid,sizeof(g->ap_ssid),"%s",victim_ssid);
 snprintf(g->up_ssid,sizeof(g->up_ssid),"%s",upstream_ssid);
 g->ap_wpa2=true;g->stop_request=false;
 g->mirror_pass[0]=0;g->up_pass[0]=0;
 cgw_snapshot_reset(&g->snap);memset(&g->final,0,sizeof(g->final));
 if(g->step1)lv_obj_add_flag(g->step1,LV_OBJ_FLAG_HIDDEN);
 if(g->step2)lv_obj_add_flag(g->step2,LV_OBJ_FLAG_HIDDEN);
 if(g->live)lv_obj_remove_flag(g->live,LV_OBJ_FLAG_HIDDEN);
 if(g->stop_btn){lv_obj_remove_flag(g->stop_btn,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_state(g->stop_btn,LV_STATE_DISABLED);}
 if(g->copy_btn)lv_obj_add_flag(g->copy_btn,LV_OBJ_FLAG_HIDDEN);
 app_gitm_jobs[tab]=(app_gitm_job_t){.ctx=ctx,.id=job,.kind=2};
 app_gitm_story_jobs[tab][2]=job;
 if(app_gitm_pages[tab]!=g->page){app_gitm_pages[tab]=g->page;
  lv_obj_add_event_cb(g->page,app_gitm_deleted,LV_EVENT_DELETE,ctx);}
 gitm_set_state(ctx,GITM_STARTING,"Simulated Rogue GITM: connecting the uplink...",COLOR_MATERIAL_AMBER);
}
static lv_obj_t *app_rogue_gitm_pass_row(app_rogue_gitm_t *flow,const char *label,const char *binding,lv_obj_t **input){
 lv_obj_t *row=lv_obj_create(flow->card);lv_obj_set_width(row,lv_pct(100));
 lv_obj_set_height(row,LV_SIZE_CONTENT);lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(row,0,0);lv_obj_set_style_pad_all(row,0,0);
 lv_obj_set_flex_flow(row,LV_FLEX_FLOW_COLUMN);
 lv_obj_t *heading=lv_label_create(row);lv_label_set_text(heading,label);
 lv_obj_set_style_text_font(heading,&lv_font_montserrat_14,0);
 *input=lv_textarea_create(row);lv_obj_set_size(*input,lv_pct(100),44);
 lv_textarea_set_one_line(*input,true);lv_textarea_set_max_length(*input,63);
 app_bind_adapter(*input,app_rogue_gitm_input_cb,LV_EVENT_CLICKED,flow,binding);
 app_bind_adapter(*input,app_rogue_gitm_input_cb,LV_EVENT_FOCUSED,flow,
                  !strcmp(binding,"emu.rogue-gitm.home")?"emu.rogue-gitm.home.focus":"emu.rogue-gitm.mirror.focus");
 return row;
}
static void app_rogue_gitm_hidden_confirm(const char *ssid){
 tab_context_t *ctx=get_current_ctx();if(!ctx||!ssid||!ssid[0])return;
 scan_view_t view=get_scan_view(ctx);
 if(view.sel_count!=1)return;
 int index=view.sel_indices[0];if(index<0||index>=view.net_count)return;
 snprintf(view.nets[index].ssid,sizeof(view.nets[index].ssid),"%s",ssid);
 app_rogue_gitm_open(ctx,index);
}
static void app_rogue_gitm_open(tab_context_t *ctx,int victim_index){
 if(!ctx)return;
 scan_view_t view=get_scan_view(ctx);
 if(victim_index<0||victim_index>=view.net_count)return;
 wifi_network_t *victim=&view.nets[victim_index];
 if(!victim->ssid[0]){show_hidden_ssid_popup(app_rogue_gitm_hidden_confirm);return;}
 int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=3)return;
 app_rogue_gitm_t *flow=&app_rogue_gitm[tab];app_rogue_gitm_close(flow);
 flow->ctx=ctx;flow->tab=tab;flow->victim_index=victim_index;flow->uplink_count=0;
 char options[2048]={0};
 for(int i=0;i<view.net_count&&flow->uplink_count<MAX_NETWORKS;i++){
  wifi_network_t *candidate=&view.nets[i];
  if(i==victim_index||!candidate->ssid[0]||candidate->channel!=victim->channel||
     !strcmp(candidate->bssid,victim->bssid))continue;
  char line[110];snprintf(line,sizeof(line),"%s  ch%d  %s",candidate->ssid,candidate->channel,candidate->security);
  if(strlen(options)+strlen(line)+2>=sizeof(options))break;
  if(options[0])strcat(options,"\n");
  strcat(options,line);
  flow->uplinks[flow->uplink_count++]=i;
 }
 if(!flow->uplink_count){
  if(ctx->scan_status_label)lv_label_set_text_fmt(ctx->scan_status_label,
   "No other network on ch %d for the uplink",victim->channel);
  return;
 }
 flow->overlay=lv_obj_create(ctx->container);lv_obj_set_size(flow->overlay,lv_pct(100),lv_pct(100));
 style_modal_overlay(flow->overlay,LV_OPA_70);
 flow->card=lv_obj_create(flow->overlay);lv_obj_set_size(flow->card,600,540);
 lv_obj_align(flow->card,LV_ALIGN_TOP_MID,0,40);
 style_popup_card(flow->card,16,COLOR_LAB5_MAGENTA);
 lv_obj_set_style_pad_all(flow->card,18,0);lv_obj_set_style_pad_row(flow->card,10,0);
 lv_obj_set_flex_flow(flow->card,LV_FLEX_FLOW_COLUMN);
 lv_obj_t *title=lv_label_create(flow->card);lv_label_set_text_fmt(title,"Rogue GITM - %s",victim->ssid);
 lv_obj_set_style_text_font(title,&lv_font_montserrat_20,0);
 lv_obj_set_style_text_color(title,COLOR_LAB5_MAGENTA,0);
 lv_obj_t *summary=lv_label_create(flow->card);
 lv_label_set_text_fmt(summary,"Victim / mirror SSID: %s\nBSSID %s   ch %d   %s",
                       victim->ssid,victim->bssid,victim->channel,victim->security);
 lv_obj_set_width(summary,lv_pct(100));lv_label_set_long_mode(summary,LV_LABEL_LONG_WRAP);
 lv_obj_t *uplink_label=lv_label_create(flow->card);
 lv_label_set_text_fmt(uplink_label,"Uplink (Internet, ch %d):",victim->channel);
 flow->uplink=lv_dropdown_create(flow->card);lv_obj_set_width(flow->uplink,lv_pct(100));
 lv_dropdown_set_options(flow->uplink,options);
 app_bind_adapter(flow->uplink,app_rogue_gitm_uplink_cb,LV_EVENT_VALUE_CHANGED,flow,"emu.rogue-gitm.uplink");
 flow->home_row=app_rogue_gitm_pass_row(flow,"Demo uplink pass (optional):","emu.rogue-gitm.home",&flow->home);
 app_rogue_gitm_pass_row(flow,"Demo mirror pass (8-63 chars):","emu.rogue-gitm.mirror",&flow->mirror);
 lv_textarea_set_text(flow->mirror,"DEMO-only-2026!");
 flow->status=lv_label_create(flow->card);lv_obj_set_width(flow->status,lv_pct(100));
 lv_label_set_long_mode(flow->status,LV_LABEL_LONG_WRAP);
 lv_label_set_text(flow->status,"OFFLINE SIMULATION: no deauth, mirror AP, Internet uplink or real capture.");
 lv_obj_t *actions=lv_obj_create(flow->card);lv_obj_set_width(actions,lv_pct(100));
 lv_obj_set_height(actions,LV_SIZE_CONTENT);lv_obj_set_style_bg_opa(actions,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(actions,0,0);lv_obj_set_style_pad_all(actions,0,0);
 lv_obj_set_flex_flow(actions,LV_FLEX_FLOW_ROW);lv_obj_clear_flag(actions,LV_OBJ_FLAG_SCROLLABLE);
 const char *names[]={"Cancel","Start"};lv_event_cb_t callbacks[]={app_rogue_gitm_cancel_cb,app_rogue_gitm_start_cb};
 const char *bindings[]={"emu.rogue-gitm.cancel","emu.rogue-gitm.start"};
 for(int i=0;i<2;i++){
  lv_obj_t *button=lv_btn_create(actions);lv_obj_set_size(button,230,48);
  lv_obj_set_style_bg_color(button,i?COLOR_MATERIAL_GREEN:ui_card_color(),0);
  app_bind_adapter(button,callbacks[i],LV_EVENT_CLICKED,flow,bindings[i]);
  lv_obj_t *label=lv_label_create(button);lv_label_set_text(label,names[i]);lv_obj_center(label);
 }
 flow->keyboard=lv_keyboard_create(flow->overlay);
 lv_obj_set_size(flow->keyboard,lv_pct(100),260);lv_obj_align(flow->keyboard,LV_ALIGN_BOTTOM_MID,0,0);
 style_on_screen_keyboard(flow->keyboard);lv_obj_add_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
 app_bind_adapter(flow->keyboard,app_rogue_gitm_keyboard_cb,LV_EVENT_ALL,flow,"emu.rogue-gitm.keyboard");
 app_rogue_gitm_sync_uplink(flow);
}
