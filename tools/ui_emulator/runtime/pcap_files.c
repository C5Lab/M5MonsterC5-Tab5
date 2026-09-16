/* Native file cards, confirmations and transfer/cleanup popups; virtual I/O only. */
static unsigned app_pf_story_copied[4],app_pf_story_deleted[4];
static char app_pf_story_copy_path[4][WARDRIVE_WIGLE_PATH_MAX],app_pf_story_delete_path[4][WARDRIVE_WIGLE_PATH_MAX];
EM_JS(int,app_pf_count,(int tab),{return emulatorDevice.device.files(emulatorDevice.module(tab)).filter(f=>/[.]pcap(?:ng)?$/i.test(f.path)).length;});
EM_JS(void,app_pf_file,(int tab,int index,char *path,int size,int *values),{
 const id=emulatorDevice.module(tab),f=emulatorDevice.device.files(id).filter(f=>/[.]pcap(?:ng)?$/i.test(f.path))[index];if(!f)return;
 stringToUTF8(f.path,path,size);HEAP32[values>>2]=f.sizeBytes||f.size||emulatorDevice.device.readFile(id,f.path).length;
 let synced=false;try{const bytes=emulatorDevice.device.readFile(id,f.path),local=FS.readFile(f.path);synced=local.length===bytes.length&&local.every((v,i)=>v===bytes[i]);}catch{}
 HEAP32[(values>>2)+1]=synced?1:0;
});
EM_JS(int,app_pf_copy,(int tab,const char *path,char *message,int size),{
 try{const p=UTF8ToString(path),bytes=emulatorDevice.device.readFile(emulatorDevice.module(tab),p);
 FS.mkdirTree(p.slice(0,p.lastIndexOf('/')));FS.writeFile(p,bytes);
 stringToUTF8('Copied '+bytes.length+' bytes to virtual Tab5 SD: '+p,message,size);return 1;
 }catch(e){stringToUTF8(e.message,message,size);return 0;}
});
EM_JS(int,app_pf_delete,(int tab,const char *path,int wardrive,char *message,int size),{
 try{const p=UTF8ToString(path),d=emulatorDevice.device,id=emulatorDevice.module(tab);
 if(wardrive)d.wardriveDeleteFiles(id,[p]);else d.deleteFile(id,p);
 try{FS.unlink(p);}catch(e){if(FS.analyzePath(p).exists)throw e;}
 stringToUTF8('Deleted virtual file: '+p,message,size);return 1;
 }catch(e){stringToUTF8(e.message,message,size);return 0;}
});
static void compromised_files_load_task(void *arg){
 tab_context_t *ctx=arg;if(!ctx)return;int tab=tab_id_for_ctx(ctx);
 if(ctx->compromised_files_load_kind==COMPROMISED_FILE_KIND_WARDRIVE)wardrive_wigle_load_file_list(ctx,tab,uart_port_for_tab(tab));
 else {
  free(ctx->wardrive_wigle_files);ctx->wardrive_wigle_files=NULL;ctx->wardrive_wigle_file_count=0;
  int count=app_wdu_sd(tab)?app_pf_count(tab):0;if(count>WARDRIVE_WIGLE_MAX_FILES)count=WARDRIVE_WIGLE_MAX_FILES;
  ctx->wardrive_wigle_files=calloc(count?count:1,sizeof(wardrive_wigle_file_t));
  if(ctx->wardrive_wigle_files)for(int i=0;i<count;i++){
   wardrive_wigle_file_t *f=&ctx->wardrive_wigle_files[i];int v[2]={0};app_pf_file(tab,i,f->path,sizeof(f->path),v);
   const char *base=strrchr(f->path,'/');snprintf(f->name,sizeof(f->name),"%s",base?base+1:f->path);f->size_bytes=v[0];f->tab5_synced=v[1];ctx->wardrive_wigle_file_count++;
  }
 }
 ctx->compromised_files_loaded=true;ctx->compromised_files_loading=false;ctx->compromised_files_load_task=NULL;
 lv_async_call(compromised_files_load_async_render,ctx);
}
static void app_pf_finish_cleanup(tab_context_t *ctx,const char *status,const char *detail){
 if(ctx->compromised_cleanup_spinner)lv_obj_add_flag(ctx->compromised_cleanup_spinner,LV_OBJ_FLAG_HIDDEN);
 if(ctx->compromised_cleanup_status_label)lv_label_set_text(ctx->compromised_cleanup_status_label,status);
 if(ctx->compromised_cleanup_log_label)lv_label_set_text(ctx->compromised_cleanup_log_label,detail);
 ctx->compromised_cleanup_running=false;ctx->compromised_cleanup_task=NULL;ctx->compromised_files_loaded=false;
}
static void compromised_cleanup_task(void *arg){
 compromised_cleanup_task_args_t *a=arg;if(!a)return;int ok=0;char line[320]="",status[100];
 for(int i=0;i<a->path_count;i++){
  int tab=tab_id_for_ctx(a->ctx),done=app_pf_delete(tab,a->paths[i],a->kind==COMPROMISED_FILE_KIND_WARDRIVE,line,sizeof(line));ok+=done;
  if(done){app_pf_story_deleted[tab]++;snprintf(app_pf_story_delete_path[tab],WARDRIVE_WIGLE_PATH_MAX,"%s",a->paths[i]);}
 }
 snprintf(status,sizeof(status),"Deleted: %d, failed: %d",ok,a->path_count-ok);app_pf_finish_cleanup(a->ctx,status,line);free(a);
}
static void wardrive_cleanup_close_cb(lv_event_t *e){
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)ctx=get_current_ctx();if(!ctx||ctx->compromised_cleanup_running)return;
 compromised_file_kind_t kind=ctx->compromised_cleanup_kind;lv_obj_t **slot=compromised_page_slot_for_kind(ctx,kind);
 bool visible=tab_id_for_ctx(ctx)==current_tab&&slot&&*slot&&ctx->current_visible_page==*slot;
 close_compromised_cleanup_popup(ctx);if(visible){ctx->compromised_files_loaded=false;show_compromised_file_page(kind);}
}
/* Transfer advances one file per tick so Cancel can stop before another write. */
static struct {bool pending;int tab,count,index;double due;char paths[WARDRIVE_WIGLE_MAX_FILES][WARDRIVE_WIGLE_PATH_MAX];} app_pf_transfer;
static void app_pf_start(lv_event_t *e,bool all){
 tab_context_t *ctx=get_current_ctx();if(!ctx||compromised_transfer_ui.active||!app_wdu_sd(current_tab))return;
 int index=compromised_index_from_user_data(lv_event_get_user_data(e));if(index<0||index>=ctx->wardrive_wigle_file_count)return;
 memset(&app_pf_transfer,0,sizeof(app_pf_transfer));app_pf_transfer.tab=current_tab;
 for(int i=0;i<ctx->wardrive_wigle_file_count&&app_pf_transfer.count<WARDRIVE_WIGLE_MAX_FILES;i++)if(all||i==index)
 snprintf(app_pf_transfer.paths[app_pf_transfer.count++],WARDRIVE_WIGLE_PATH_MAX,"%s",ctx->wardrive_wigle_files[i].path);
 compromised_transfer_ui.active=true;compromised_transfer_ui.cancel_requested=false;compromised_transfer_ui.sync_mode=all;
 compromised_transfer_show_popup(all?"Sync all virtual captures":ctx->wardrive_wigle_files[index].name);
 app_pf_transfer.pending=true;app_pf_transfer.due=app_device_ms+500;
}
static void compromised_file_copy_cb(lv_event_t *e){app_pf_start(e,false);}
static void compromised_files_sync_cb(lv_event_t *e){app_pf_start(e,true);}
static void app_pcap_files_tick(double now){
 if(!app_pf_transfer.pending||now<app_pf_transfer.due)return;
 char detail[360];bool ok=false;
 if(compromised_transfer_ui.cancel_requested)snprintf(detail,sizeof(detail),"Cancelled; completed copies remain on virtual Tab5 SD.");
 else if(!app_wdu_sd(app_pf_transfer.tab))snprintf(detail,sizeof(detail),"Module disconnected or SD card missing.");
 else{
  ok=app_pf_copy(app_pf_transfer.tab,app_pf_transfer.paths[app_pf_transfer.index],detail,sizeof(detail));
  if(ok){int tab=app_pf_transfer.tab;app_pf_story_copied[tab]++;snprintf(app_pf_story_copy_path[tab],WARDRIVE_WIGLE_PATH_MAX,"%s",app_pf_transfer.paths[app_pf_transfer.index]);}
  if(ok&&++app_pf_transfer.index<app_pf_transfer.count){app_pf_transfer.due=now+500;return;}
 }
 app_pf_transfer.pending=false;compromised_transfer_ui.task=NULL;compromised_transfer_finish_ui_unlocked(ok,detail);
}
EM_JS(int,app_pf_archive,(int tab,const char *service,const char *status,int move,char *message,int size),{
 try{const r=emulatorDevice.device.wardriveCleanup(emulatorDevice.module(tab),{service:UTF8ToString(service),status:UTF8ToString(status),move:!!move});
 stringToUTF8((move?'Archive move':'Dry-run')+': scanned '+r.scanned+', matched '+r.matched+', moved '+r.moved+'\n'+r.paths.join('\n'),message,size);return r.matched;
 }catch(e){stringToUTF8(e.message,message,size);return -1;}
});
static void wardrive_cleanup_task(void *arg){
 wardrive_cleanup_task_args_t *a=arg;if(!a)return;char detail[2048];int matched=app_pf_archive(tab_id_for_ctx(a->ctx),a->service,a->status,a->move,detail,sizeof(detail));
 app_pf_finish_cleanup(a->ctx,matched<0?"Cleanup failed":a->move?"Move complete":"Dry-run complete",detail);
 if(!a->move&&matched>0){a->ctx->wardrive_cleanup_dry_run_ready=true;if(a->ctx->compromised_cleanup_move_btn)lv_obj_clear_flag(a->ctx->compromised_cleanup_move_btn,LV_OBJ_FLAG_HIDDEN);}
 free(a);
}
static void wardrive_fix_task(void *arg){
 wardrive_fix_task_args_t *a=arg;if(!a)return;
 app_pf_finish_cleanup(a->ctx,"Fix unavailable for this fixture format","Virtual wardrive sessions contain JSON track/row data. Firmware wardrive_fix expects native CSV logs. No file was changed or created.");free(a);
}
