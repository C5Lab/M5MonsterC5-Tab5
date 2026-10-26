/* Wardrive simulator boundary: original LVGL pages are retained.
 * Cooperative actions supply scenario data without radio or network I/O. */
static lv_obj_t *app_wd_page[4];
static int app_wd_rows[4]={-1,-1,-1,-1};
static double app_wd_last;
static void app_wd_gps_tick(tab_context_t *ctx);
static void app_wd_lists_sync(int tab);
static void app_wd_home_arm(tab_context_t *ctx);
static void app_wd_home_check(tab_context_t *ctx);
static char app_wd_error[4][256];
static int app_wd_story_run[4];
static double app_wd_power_due[4];
static void app_wd_power_tick(void);
static void app_wd_auto_close(tab_context_t *ctx);
static void app_wd_auto_retry_cb(lv_event_t *e);
EM_JS(int,app_wd_action,(int tab,int action,char *error,int size),{
 try {
  const d=emulatorDevice.device,m=emulatorDevice.module(tab);
  let result=1;if(action===0)result=d.wardriveStart(m);
  if(action===1)d.wardriveStop(m);
  if(action===2)d.wardriveSetGps(m,!d.wardrive(m).gps.fix);
  if(action===6&&d.wardrive(m).running)d.cancel(d.snapshot(m).active);
  if(action>=3&&action<=5)d.wardriveUpload(m,['success','failure','partial'][action-3]);
  stringToUTF8("",error,size);return result;
 } catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(int,app_wd_snapshot,(int tab,int *metrics,char *status,int size),{
 const s=emulatorDevice.device.wardrive(emulatorDevice.module(tab));
 HEAP32[metrics>>2]=s.wifiCount;HEAP32[(metrics>>2)+1]=s.btCount;
 HEAP32[(metrics>>2)+2]=s.gps.satellites||0;HEAP32[(metrics>>2)+3]=s.rows.length;HEAP32[(metrics>>2)+4]=Math.round(s.gps.distanceM||0);
 stringToUTF8(s.error||(s.running?(s.gps.fix?'GPS Fix Acquired - Scanning...':'Waiting for GPS fix...'):(s.sessions.length?'Wardrive stopped':'Press Start to begin wardrive')),status,size);
 return s.running?1:0;
});
EM_JS(void,app_wd_csv,(int tab,int index,char *out,int size),{
 const s=emulatorDevice.device.wardrive(emulatorDevice.module(tab)),r=s.rows[index];
 const safe=v=>String(v??"").replaceAll(',',' ');
 stringToUTF8(r?[r.bssid,r.ssid,r.security,0,0,0,r.lat,r.lon,0,0,r.kind==='bluetooth'?'BLE':'WIFI'].map(safe).join(','):"",out,size);
});
static void app_wd_page_deleted(lv_event_t *e) {
 int tab=(int)(intptr_t)lv_event_get_user_data(e);char error[256];
 app_wd_action(tab,6,error,sizeof(error));app_wd_page[tab]=NULL;app_wd_rows[tab]=-1;app_wd_error[tab][0]=0;
 app_wd_power_due[tab]=0;
 tab_context_t *ctx=get_ctx_for_tab(tab);if(ctx){ctx->wardrive_autoup_overlay=NULL;ctx->wardrive_autoup_status_label=NULL;ctx->wardrive_autoup_btn_row=NULL;}
}
static void app_wd_sync(tab_context_t *ctx) {
 if(!ctx||!ctx->wardrive_page||!lv_obj_is_valid(ctx->wardrive_page))return;
 int tab=tab_id_for_ctx(ctx);if(tab!=TAB_GROVE&&tab!=TAB_MBUS)return;
 if(app_wd_page[tab]!=ctx->wardrive_page){app_wd_page[tab]=ctx->wardrive_page;app_wd_rows[tab]=-1;lv_obj_add_event_cb(ctx->wardrive_page,app_wd_page_deleted,LV_EVENT_DELETE,(void*)(intptr_t)tab);}
 int data[5];char status[256];bool running=app_wd_snapshot(tab,data,status,sizeof(status));
 ctx->wardrive_monitoring=running;
 if(running){lv_obj_add_state(ctx->wardrive_start_btn,LV_STATE_DISABLED);lv_obj_remove_state(ctx->wardrive_stop_btn,LV_STATE_DISABLED);lv_obj_add_state(ctx->wardrive_upload_btn,LV_STATE_DISABLED);lv_obj_add_state(ctx->wardrive_setup_btn,LV_STATE_DISABLED);}
 else {lv_obj_remove_state(ctx->wardrive_start_btn,LV_STATE_DISABLED);lv_obj_add_state(ctx->wardrive_stop_btn,LV_STATE_DISABLED);lv_obj_remove_state(ctx->wardrive_upload_btn,LV_STATE_DISABLED);lv_obj_remove_state(ctx->wardrive_setup_btn,LV_STATE_DISABLED);}
 lv_label_set_text(ctx->wardrive_status_label,app_wd_error[tab][0]?app_wd_error[tab]:status);
 if(app_wd_rows[tab]!=data[3]) {
  ctx->wardrive_net_count=ctx->wardrive_net_head=ctx->wardrive_wifi_count=ctx->wardrive_bt_count=0;
  for(int i=0;i<data[3];i++){char line[512];app_wd_csv(tab,i,line,sizeof(line));parse_wardrive_network_line(ctx,line);}
  app_wd_rows[tab]=data[3];
  if(running)app_wd_home_check(ctx);update_wardrive_table(ctx);
 }
 ctx->wardrive_wifi_count=data[0];ctx->wardrive_bt_count=data[1];ctx->wardrive_sat_count=data[2];ctx->wardrive_distance_m=data[4];
 update_wardrive_count_label(ctx);
}
static void app_wd_do(int action) {
 tab_context_t *ctx=get_current_ctx();int tab=current_tab;char error[256];
 if(!ctx)return;
 if(action==0){app_wd_rows[tab]=-1;app_wd_lists_sync(tab);app_wd_home_arm(ctx);}
 int ok=app_wd_action(tab,action,error,sizeof(error));if(action==0&&ok)app_wd_story_run[tab]=ok;
 snprintf(app_wd_error[tab],256,"%s",error);app_wd_sync(ctx);
 if(!ok&&ctx->wardrive_status_label)lv_label_set_text(ctx->wardrive_status_label,error);
}
static void wardrive_start_cb(lv_event_t *e){(void)e;app_wd_do(0);}
static void wardrive_stop_cb(lv_event_t *e){(void)e;app_wd_do(1);}
static void wardrive_back_cb(lv_event_t *e) {
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 close_wardrive_home_confirm(ctx);app_wd_auto_close(ctx);
 if(ctx->wardrive_monitoring)app_wd_do(1);
 wardrive_setup_close_cb(NULL);close_wardrive_upload_menu(ctx);close_wardrive_wigle_popup_ctx(ctx);lv_obj_add_flag(ctx->wardrive_page,LV_OBJ_FLAG_HIDDEN);show_main_tiles();
}
static void app_wardrive_tick(void) {
 if(app_device_ms-app_wd_last<300)return;app_wd_last=app_device_ms;
 app_wd_sync(get_ctx_for_tab(TAB_GROVE));app_wd_sync(get_ctx_for_tab(TAB_MBUS));
 app_wd_gps_tick(get_ctx_for_tab(TAB_GROVE));app_wd_gps_tick(get_ctx_for_tab(TAB_MBUS));
 app_wd_power_tick();
}

EM_JS(int,app_wd_auto_upload,(int tab,int wigle,int wars,int archive,char *error,int size),{
 try {
  const d=emulatorDevice.device,m=emulatorDevice.module(tab),outcome=globalThis.emulatorWardriveUploadOutcome||'success';
  let failed=0;for(const provider of [...(wigle?['wigle']:[]),...(wars?['wdgwars']:[])])failed+=d.wardriveUpload(m,outcome,{provider,mode:'pending'}).failed;
  // Firmware runs `wardrive_cleanup all done move` only after a successful
  // automatic upload. The model uses the same all-services-done filter.
  if(!failed&&archive&&(wigle||wars))d.wardriveCleanup(m,{service:'all',status:'done',move:true});
  stringToUTF8(failed?'Simulated auto-upload failed':"",error,size);return failed?0:1;
 }catch(e){stringToUTF8(e.message,error,size);return 0;}
});
/* The firmware helper creates the result buttons; the browser keeps that
 * control and substitutes local actions for the network and PMIC work. */
static lv_obj_t *wardrive_autoup_make_btn(lv_obj_t *row,const char *label,lv_color_t color,
                                          lv_event_cb_t cb,void *data) {
 lv_obj_t *btn=lv_btn_create(row);lv_obj_set_size(btn,145,48);
 lv_obj_set_style_bg_color(btn,color,0);lv_obj_set_style_radius(btn,8,0);
 const char *id=!strcmp(label,"Retry")?"emu.wardrive.autoup.retry":
                !strcmp(label,"Resume")?"emu.wardrive.autoup.resume":
                !strcmp(label,"Stay on")?"emu.wardrive.autoup.stay":"emu.wardrive.autoup.close";
 app_bind_adapter(btn,cb,LV_EVENT_CLICKED,data,id);
 lv_obj_t *text=lv_label_create(btn);lv_label_set_text(text,label);lv_obj_center(text);
 return btn;
}
static void app_wd_auto_close(tab_context_t *ctx) {
 if(!ctx)return;
 int tab=tab_id_for_ctx(ctx);app_wd_power_due[tab]=0;
 if(ctx->wardrive_autoup_overlay&&lv_obj_is_valid(ctx->wardrive_autoup_overlay))
  lv_obj_del(ctx->wardrive_autoup_overlay);
 ctx->wardrive_autoup_overlay=NULL;ctx->wardrive_autoup_status_label=NULL;
 ctx->wardrive_autoup_btn_row=NULL;
}
static void app_wd_auto_close_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);app_wd_auto_close(ctx);
}
static void app_wd_auto_resume_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)return;
 app_wd_auto_close(ctx);app_wd_do(0);
}
static void app_wd_auto_actions(tab_context_t *ctx,bool failed) {
 if(!ctx||!ctx->wardrive_autoup_btn_row)return;
 lv_obj_clean(ctx->wardrive_autoup_btn_row);
 if(failed)wardrive_autoup_make_btn(ctx->wardrive_autoup_btn_row,"Retry",COLOR_MATERIAL_BLUE,app_wd_auto_retry_cb,ctx);
 wardrive_autoup_make_btn(ctx->wardrive_autoup_btn_row,"Resume",COLOR_MATERIAL_GREEN,app_wd_auto_resume_cb,ctx);
 wardrive_autoup_make_btn(ctx->wardrive_autoup_btn_row,"Close",lv_color_hex(0x444444),app_wd_auto_close_cb,ctx);
}
static void app_wd_auto_stay_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)return;
 app_wd_power_due[tab_id_for_ctx(ctx)]=0;
 lv_label_set_text(ctx->wardrive_autoup_status_label,"Power off cancelled");
 app_wd_auto_actions(ctx,false);
}
static void app_wd_auto_result(tab_context_t *ctx,bool ok,const char *error) {
 if(!ctx||!ctx->wardrive_page)return;
 app_wd_auto_close(ctx);
 lv_obj_t *overlay=lv_obj_create(ctx->wardrive_page);ctx->wardrive_autoup_overlay=overlay;
 lv_obj_remove_style_all(overlay);lv_obj_set_size(overlay,lv_pct(100),lv_pct(100));
 lv_obj_set_style_bg_color(overlay,lv_color_hex(0x000000),0);
 lv_obj_set_style_bg_opa(overlay,LV_OPA_70,0);
 lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_t *box=lv_obj_create(overlay);lv_obj_set_size(box,lv_pct(92),250);lv_obj_center(box);
 lv_obj_set_style_bg_color(box,lv_color_hex(0x242424),0);
 lv_obj_set_style_border_color(box,ok?COLOR_MATERIAL_GREEN:COLOR_MATERIAL_RED,0);
 lv_obj_set_style_border_width(box,2,0);lv_obj_set_style_radius(box,12,0);
 lv_obj_set_style_pad_all(box,18,0);lv_obj_set_flex_flow(box,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_flex_align(box,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 lv_obj_t *title=lv_label_create(box);lv_label_set_text(title,"Wardrive auto-upload");
 lv_obj_set_style_text_color(title,lv_color_hex(0xFFFFFF),0);
 ctx->wardrive_autoup_status_label=lv_label_create(box);
 lv_obj_set_width(ctx->wardrive_autoup_status_label,lv_pct(100));
 lv_label_set_long_mode(ctx->wardrive_autoup_status_label,LV_LABEL_LONG_WRAP);
 lv_obj_set_style_text_align(ctx->wardrive_autoup_status_label,LV_TEXT_ALIGN_CENTER,0);
 lv_obj_set_style_text_color(ctx->wardrive_autoup_status_label,ok?COLOR_MATERIAL_GREEN:COLOR_MATERIAL_RED,0);
 ctx->wardrive_autoup_btn_row=lv_obj_create(box);
 lv_obj_set_size(ctx->wardrive_autoup_btn_row,lv_pct(100),LV_SIZE_CONTENT);
 lv_obj_set_style_bg_opa(ctx->wardrive_autoup_btn_row,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(ctx->wardrive_autoup_btn_row,0,0);
 lv_obj_set_style_pad_all(ctx->wardrive_autoup_btn_row,0,0);
 lv_obj_set_flex_flow(ctx->wardrive_autoup_btn_row,LV_FLEX_FLOW_ROW);
 lv_obj_set_flex_align(ctx->wardrive_autoup_btn_row,LV_FLEX_ALIGN_SPACE_EVENLY,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 if(!ok){
  lv_label_set_text_fmt(ctx->wardrive_autoup_status_label,"Auto-upload failed\n%s",error&&*error?error:"Local upload unavailable");
  app_wd_auto_actions(ctx,true);
 }else if(g_wd_autoupload_poweroff){
  app_wd_power_due[tab_id_for_ctx(ctx)]=app_device_ms+10000;
  lv_label_set_text(ctx->wardrive_autoup_status_label,"Auto-upload complete\nPowering off in 10s (demo)");
  wardrive_autoup_make_btn(ctx->wardrive_autoup_btn_row,"Stay on",COLOR_MATERIAL_RED,app_wd_auto_stay_cb,ctx);
 }else{
  lv_label_set_text(ctx->wardrive_autoup_status_label,"Auto-upload complete");
  app_wd_auto_actions(ctx,false);
 }
}
static void app_wd_auto_retry_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)return;
 int tab=tab_id_for_ctx(ctx);char error[256]={0};
 bool ok=app_wd_auto_upload(tab,g_wd_autoupload_wigle,g_wd_autoupload_wdgwars,g_wd_autoupload_archive,error,sizeof(error));
 snprintf(app_wd_error[tab],sizeof(app_wd_error[tab]),"%s",ok?"Home network detected - simulated auto-upload finished":error);
 app_wd_sync(ctx);app_wd_auto_result(ctx,ok,error);
}
static void app_wd_power_tick(void) {
 for(int tab=0;tab<4;tab++)if(app_wd_power_due[tab]>0){
  tab_context_t *ctx=get_ctx_for_tab(tab);
  if(!ctx||!ctx->wardrive_autoup_status_label){app_wd_power_due[tab]=0;continue;}
  double remaining=app_wd_power_due[tab]-app_device_ms;
  if(remaining<=0){
   app_wd_power_due[tab]=0;
   lv_label_set_text(ctx->wardrive_autoup_status_label,"Simulated power off\nClose to continue the demo");
   lv_obj_clean(ctx->wardrive_autoup_btn_row);
   wardrive_autoup_make_btn(ctx->wardrive_autoup_btn_row,"Close",lv_color_hex(0x444444),app_wd_auto_close_cb,ctx);
  }else lv_label_set_text_fmt(ctx->wardrive_autoup_status_label,
   "Auto-upload complete\nPowering off in %ds (demo)",(int)ceil(remaining/1000));
 }
}
static void wardrive_home_confirm_yes_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)ctx=get_current_ctx();if(!ctx)return;
 close_wardrive_home_confirm(ctx);int tab=tab_id_for_ctx(ctx);char error[256];
 app_wd_action(tab,1,error,sizeof(error));
 /* Armed providers are offline fixtures; this uses the same session-upload model as manual sync. */
 bool ok=app_wd_auto_upload(tab,g_wd_autoupload_wigle,g_wd_autoupload_wdgwars,g_wd_autoupload_archive,error,sizeof(error));
 snprintf(app_wd_error[tab],256,"%s",ok?"Home network detected - simulated auto-upload finished":error);
 app_wd_sync(ctx);app_wd_auto_result(ctx,ok,error);
}
