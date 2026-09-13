/* Native Tab5 upload menus retained; only virtual SD and transport boundaries. */
static tab_context_t *app_wdu_pending[4];
static double app_wdu_due[4];
static int app_wdu_ids[4][WARDRIVE_WIGLE_MAX_FILES];

EM_JS(int,app_wdu_sd,(int tab),{
 const s=emulatorDevice.device.snapshot(emulatorDevice.module(tab));return s.connected&&s.sdPresent?1:0;
});
EM_JS(int,app_wdu_count,(int tab),{
 return emulatorDevice.device.wardrive(emulatorDevice.module(tab)).sessions.filter(s=>!s.archived).length;
});
EM_JS(void,app_wdu_file,(int tab,int index,int *values,char *path,int size,char *wigle,char *wdg),{
 const s=emulatorDevice.device.wardrive(emulatorDevice.module(tab)).sessions.filter(s=>!s.archived)[index];
 if(!s)return;
 HEAP32[values>>2]=s.id;HEAP32[(values>>2)+1]=s.sizeBytes;
 HEAP32[(values>>2)+2]=s.wifiCount;HEAP32[(values>>2)+3]=s.btCount;
 stringToUTF8(s.path,path,size);
 stringToUTF8(s.uploadStatus?.wigle||(s.uploaded?'done':'pending'),wigle,16);
 stringToUTF8(s.uploadStatus?.wdgwars||(s.uploaded?'done':'pending'),wdg,16);
});
EM_JS(int,app_wdu_send,(int tab,int provider,int mode,const int *ids,int count,char *text,int size),{
 try {
  const options={provider:provider===2?'wdgwars':'wigle',mode:['selected','pending','all'][mode],
   sessionIds:Array.from({length:count},(_,i)=>HEAP32[(ids>>2)+i])};
  // Scenario override stays outside native Tab5 UI; never issue a network request.
  const result=emulatorDevice.device.wardriveUpload(emulatorDevice.module(tab),
   globalThis.emulatorWardriveUploadOutcome||'success',options);
  stringToUTF8((provider===2?'WDGWars':'WiGLE')+' sync finished.\nUploaded: '+result.uploaded+
   '  Skipped: 0  Failed: '+result.failed,text,size);return result.failed===0?1:0;
 }catch(e){stringToUTF8(e.message,text,size);return 0;}
});

static bool wardrive_tab_has_sd_card(tab_id_t tab){return (tab==TAB_GROVE||tab==TAB_MBUS)&&app_wdu_sd(tab)!=0;}

static void wardrive_wigle_load_file_list(tab_context_t *ctx,tab_id_t tab,uart_port_t uart_port) {
 (void)uart_port;if(!ctx)return;
 if(ctx->wardrive_wigle_list&&lv_obj_is_valid(ctx->wardrive_wigle_list))lv_obj_clean(ctx->wardrive_wigle_list);
 free(ctx->wardrive_wigle_files);ctx->wardrive_wigle_files=NULL;
 wardrive_upload_state_reset(ctx);
 ctx->wardrive_wigle_file_count=ctx->wardrive_wigle_selected_count=ctx->wardrive_wigle_page=0;
 memset(&ctx->wardrive_file_summary,0,sizeof(ctx->wardrive_file_summary));
 if(!app_wdu_sd(tab))return;
 int count=app_wdu_count(tab);if(count>WARDRIVE_WIGLE_MAX_FILES)count=WARDRIVE_WIGLE_MAX_FILES;
 ctx->wardrive_wigle_files=calloc(count?count:1,sizeof(wardrive_wigle_file_t));
 if(!ctx->wardrive_wigle_files)return;
 ctx->wardrive_upload_state_entries=calloc(WARDRIVE_UPLOAD_STATE_MAX,sizeof(wardrive_upload_state_entry_t));
 wardrive_file_summary_t *summary=&ctx->wardrive_file_summary;
 summary->valid=true;summary->files=summary->parsed=summary->announced=count;
 for(int i=0;i<count;i++) {
  wardrive_wigle_file_t *f=&ctx->wardrive_wigle_files[i];int values[4]={0};
  app_wdu_file(tab,i,values,f->path,sizeof(f->path),f->wigle_status,f->wdgwars_status);
  app_wdu_ids[tab][i]=values[0];f->size_bytes=values[1];f->wifi_rows=values[2];f->ble_rows=values[3];
  const char *base=strrchr(f->path,'/');snprintf(f->name,sizeof(f->name),"%s",base?base+1:f->path);
  snprintf(f->hash,sizeof(f->hash),"sim-%08x",values[0]);
  summary->bytes+=f->size_bytes;summary->wifi+=f->wifi_rows;summary->ble+=f->ble_rows;
  for(int svc=0;svc<2;svc++) {
   const char *status=svc?f->wdgwars_status:f->wigle_status;
   bool done=strcmp(status,"done")==0,failed=strcmp(status,"failed")==0;
   if(svc){summary->wdgwars_ok+=done;summary->wdgwars_failed+=failed;summary->wdgwars_pending+=!done&&!failed;}
   else {summary->wigle_ok+=done;summary->wigle_failed+=failed;summary->wigle_pending+=!done&&!failed;}
   if(strcmp(status,"pending")&&ctx->wardrive_upload_state_entries&&ctx->wardrive_upload_state_count<WARDRIVE_UPLOAD_STATE_MAX){
    wardrive_upload_state_entry_t *entry=&ctx->wardrive_upload_state_entries[ctx->wardrive_upload_state_count++];
    snprintf(entry->service,sizeof(entry->service),"%s",svc?"wdgwars":"wigle");
    snprintf(entry->filename,sizeof(entry->filename),"%s",f->name);snprintf(entry->status,sizeof(entry->status),"%s",status);
    snprintf(entry->hash,sizeof(entry->hash),"%s",f->hash);entry->size_bytes=f->size_bytes;entry->wifi_rows=f->wifi_rows;entry->ble_rows=f->ble_rows;
   }
  }
 }
 summary->rows=summary->devices=summary->wifi+summary->ble;ctx->wardrive_wigle_file_count=count;
 ctx->wardrive_upload_state_loaded=true;
}

static void wardrive_upload_menu_load_task(void *arg) {
 tab_context_t *ctx=arg;if(!ctx)return;
 tab_id_t tab=tab_id_for_ctx(ctx);wardrive_wigle_load_file_list(ctx,tab,uart_port_for_tab(tab));
 ctx->wardrive_upload_menu_loaded=true;ctx->wardrive_upload_menu_loading=false;ctx->wardrive_upload_menu_task=NULL;
 lv_async_call(wardrive_upload_menu_load_async_render,ctx);
}

static void wardrive_wigle_start_upload(tab_context_t *ctx,wardrive_upload_mode_t mode) {
 if(!ctx||ctx->wardrive_wigle_task_running)return;
 if(mode==WARDRIVE_UPLOAD_MODE_SELECTED&&ctx->wardrive_wigle_selected_count<=0)return;
 int tab=tab_id_for_ctx(ctx);ctx->wardrive_upload_mode=mode;ctx->wardrive_wigle_task_running=true;
 ctx->wardrive_wigle_upload_done=false;app_wdu_pending[tab]=ctx;app_wdu_due[tab]=app_device_ms+1500;
 lv_label_set_text_fmt(ctx->wardrive_wigle_status_label,"Preparing %s upload%s...",
  wardrive_upload_provider_label(ctx->wardrive_upload_provider),mode==WARDRIVE_UPLOAD_MODE_PENDING?" (pending)":mode==WARDRIVE_UPLOAD_MODE_ALL?" (all)":"");
 wardrive_wigle_update_send_btn(ctx);
 if(ctx->wardrive_wigle_send_btn)lv_obj_add_state(ctx->wardrive_wigle_send_btn,LV_STATE_DISABLED);
 if(ctx->wardrive_wigle_batch_row)lv_obj_add_flag(ctx->wardrive_wigle_batch_row,LV_OBJ_FLAG_HIDDEN);
 if(ctx->wardrive_wigle_stop_btn)lv_obj_clear_flag(ctx->wardrive_wigle_stop_btn,LV_OBJ_FLAG_HIDDEN);
 if(ctx->wardrive_wigle_spinner)lv_obj_clear_flag(ctx->wardrive_wigle_spinner,LV_OBJ_FLAG_HIDDEN);
}

static void app_wd_upload_tick(double now) {
 for(int tab=0;tab<4;tab++) {
  tab_context_t *ctx=app_wdu_pending[tab];if(!ctx)continue;
  if(!ctx->wardrive_wigle_task_running||!ctx->wardrive_wigle_popup_overlay){app_wdu_pending[tab]=NULL;continue;}
  if(!app_wdu_sd(tab)) {
   app_wdu_pending[tab]=NULL;ctx->wardrive_wigle_task_running=false;
   if(ctx->wardrive_wigle_status_label)lv_label_set_text(ctx->wardrive_wigle_status_label,"Module disconnected or SD card missing.");
   wardrive_wigle_update_send_btn(ctx);continue;
  }
  if(now<app_wdu_due[tab])continue;
  app_wdu_pending[tab]=NULL;int selected[WARDRIVE_WIGLE_MAX_FILES],count=0;
  for(int i=0;i<ctx->wardrive_wigle_file_count;i++)if(ctx->wardrive_wigle_files[i].selected)selected[count++]=app_wdu_ids[tab][i];
  char text[256];int okay=app_wdu_send(tab,ctx->wardrive_upload_provider,ctx->wardrive_upload_mode,selected,count,text,sizeof(text));
  ctx->wardrive_wigle_task_running=false;ctx->wardrive_wigle_task=NULL;
  if(ctx->wardrive_wigle_list)lv_obj_clean(ctx->wardrive_wigle_list);
  wardrive_wigle_load_file_list(ctx,tab,uart_port_for_tab(tab));wardrive_wigle_render_page(ctx);
  ctx->wardrive_wigle_upload_done=okay;wardrive_wigle_update_send_btn(ctx);
  if(ctx->wardrive_wigle_status_label)lv_label_set_text(ctx->wardrive_wigle_status_label,text);
 }
}

