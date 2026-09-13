/* Native GITM setup, live view and exit modal; cooperative virtual device jobs. */
typedef struct { tab_context_t *ctx; int id,kind; bool leaving; } app_gitm_job_t;
static app_gitm_job_t app_gitm_jobs[4];
static int app_gitm_story_jobs[4][3],app_gitm_story_entry[4];
static lv_obj_t *app_gitm_pages[4];
static void app_gitm_deleted(lv_event_t *event) {
 tab_context_t *ctx=lv_event_get_user_data(event);if(!ctx)return;int tab=tab_id_for_ctx(ctx);
 app_gitm_job_t *job=&app_gitm_jobs[tab];if(job->id)app_model_cancel(job->id);
 memset(job,0,sizeof(*job));app_gitm_pages[tab]=NULL;
 if(ctx->gitm){ctx->gitm->session_active=false;ctx->gitm->session_task=NULL;ctx->gitm->page=NULL;}
}
EM_JS(int,app_gitm_begin,(int tab,int kind,const char *ssid,const char *pass,const char *up,const char *prefix),{
 try {return emulatorDevice.device.toolStart(emulatorDevice.module(tab),['gitm_scan','gitm_connect','gitm'][kind],
  {ssid:UTF8ToString(ssid),password:UTF8ToString(pass),upstreamSsid:UTF8ToString(up),prefix:UTF8ToString(prefix)});
 }catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));return 0;}
});
EM_JS(int,app_gitm_poll,(int id,int *values,char *file,int size,char *error,int error_size),{
 const j=emulatorDevice.device.job(id);if(!j){stringToUTF8('Operation reset.',error,error_size);return -1;}
 const r=j.result||{};HEAP32[values>>2]=r.packets||0;HEAP32[(values>>2)+1]=r.bytes||0;
 HEAP32[(values>>2)+2]=(r.networks||[]).length;HEAP32[(values>>2)+3]=(r.clients||[]).length;
 stringToUTF8(j.file||r.file||"",file,size);stringToUTF8(j.error||'Operation cancelled.',error,error_size);
 return j.state==='completed'?2:j.state==='running'?1:-1;
});
EM_JS(void,app_gitm_network,(int id,int row,char *ssid,char *bssid,char *security,int *values),{
 const n=emulatorDevice.device.job(id)?.result?.networks?.[row];if(!n)return;
 stringToUTF8(n.ssid||"",ssid,33);stringToUTF8(n.bssid||"",bssid,18);stringToUTF8(n.security||n.auth||'OPEN',security,32);
 HEAP32[values>>2]=n.channel||1;HEAP32[(values>>2)+1]=n.rssi||-50;
});
EM_JS(void,app_gitm_client,(int id,int row,char *mac,char *ip),{
 const c=emulatorDevice.device.job(id)?.result?.clients?.[row];if(!c)return;
 stringToUTF8(c.mac||"",mac,18);stringToUTF8(c.ip||"",ip,16);
});
EM_JS(void,app_gitm_finish,(int id),{
 try{emulatorDevice.device.toolStop(id);}catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));}
});
static void app_gitm_stage(tab_context_t *ctx,int kind) {
 if(!ctx||!ctx->gitm)return;
 gitm_ctx_t *g=ctx->gitm;int tab=tab_id_for_ctx(ctx);
 app_gitm_jobs[tab]=(app_gitm_job_t){.ctx=ctx,.kind=kind};
 app_gitm_jobs[tab].id=app_gitm_begin(tab,kind,kind==1?g->up_ssid:g->ap_ssid,
  kind==1?g->up_pass:g->ap_pass,g->up_ssid,g->pcap_basename);
 app_gitm_story_jobs[tab][kind]=app_gitm_jobs[tab].id;
 if(kind==0){g->net_count=0;g->up_index=-1;}
 if(kind==2)gitm_set_state(ctx,GITM_STARTING,"Starting simulated capture...",COLOR_MATERIAL_AMBER);
}
static void gitm_scan_task(void *arg){app_gitm_stage(arg,0);}
static void gitm_connect_task(void *arg){app_gitm_stage(arg,1);}
static void gitm_session_task(void *arg){app_gitm_stage(arg,2);}
static void gitm_attach_task(void *arg) {
 tab_context_t *ctx=arg;if(!ctx||!ctx->gitm)return;
 int tab=tab_id_for_ctx(ctx);
 app_gitm_story_entry[tab]=ctx->gitm->preselect_ssid[0]?1:2;
 if(app_gitm_pages[tab]!=ctx->gitm->page){app_gitm_pages[tab]=ctx->gitm->page;(lv_obj_add_event_cb)(ctx->gitm->page,app_gitm_deleted,LV_EVENT_DELETE,ctx);}
 gitm_set_hint(ctx,"Simulated JanOS is idle. SCAN to choose an upstream.",lv_color_hex(0xCCCCCC));
 if(ctx->gitm->scan_btn)lv_obj_clear_state(ctx->gitm->scan_btn,LV_STATE_DISABLED);
}
static void gitm_exit_wait_task(void *arg) {
 tab_context_t *ctx=arg;if(!ctx||!ctx->gitm)return;
 app_gitm_jobs[tab_id_for_ctx(ctx)].leaving=true;
}
static void gitm_leave_page(tab_context_t *ctx) {
 if(!ctx||!ctx->gitm)return;
 app_gitm_job_t *job=&app_gitm_jobs[tab_id_for_ctx(ctx)];
 if(job->ctx){if(job->id)app_model_cancel(job->id);memset(job,0,sizeof(*job));}
 ctx->gitm->session_active=false;ctx->gitm->session_task=NULL;
 ctx->gitm->preselect_ssid[0]=0;
 if(ctx->gitm->keyboard)lv_obj_add_flag(ctx->gitm->keyboard,LV_OBJ_FLAG_HIDDEN);
 if(ctx->gitm->page)lv_obj_add_flag(ctx->gitm->page,LV_OBJ_FLAG_HIDDEN);
 ctx->observer_attack_return_to_observer=false;clear_observer_attack_override(ctx);
 if(ctx->tiles){lv_obj_clear_flag(ctx->tiles,LV_OBJ_FLAG_HIDDEN);ctx->current_visible_page=ctx->tiles;}
}
static void app_gitm_tick(void) {
 for(int tab=0;tab<4;tab++) {
  app_gitm_job_t *job=&app_gitm_jobs[tab];tab_context_t *ctx=job->ctx;if(!ctx||!ctx->gitm)continue;
  gitm_ctx_t *g=ctx->gitm;
  if(!g->page||!lv_obj_is_valid(g->page)){if(job->id)app_model_cancel(job->id);memset(job,0,sizeof(*job));continue;}
  if(job->kind==2&&g->stop_request)app_gitm_finish(job->id);
  int data[4]={0};char file[128]={0},error[180]={0};
  int state=job->id?app_gitm_poll(job->id,data,file,sizeof(file),error,sizeof(error)):-1;
  if(job->kind==2&&state>0) {
   cgw_snapshot_t *s=&g->snap;s->complete=true;s->active=s->capture_active=state==1;s->upstream=true;s->napt=true;s->dns_proxy=true;s->channel=6;
   snprintf(s->ssid,sizeof(s->ssid),"%s",g->ap_ssid);snprintf(s->security,sizeof(s->security),"%s",g->ap_wpa2?"wpa2":"open");
   snprintf(s->upstream_ssid,sizeof(s->upstream_ssid),"%s",g->up_ssid);
   snprintf(s->sta_ip,sizeof(s->sta_ip),"192.0.2.2");snprintf(s->dns,sizeof(s->dns),"192.0.2.1");
   snprintf(s->client_isolation,sizeof(s->client_isolation),"off");snprintf(s->file,sizeof(s->file),"%s",file);
   s->packets=data[0];s->file_bytes=data[1];s->reported_clients=data[3];s->client_count=data[3]>CGW_MAX_CLIENTS?CGW_MAX_CLIENTS:data[3];s->clients_truncated=data[3]>CGW_MAX_CLIENTS;
   for(int i=0;i<s->client_count;i++)app_gitm_client(job->id,i,s->clients[i].mac,s->clients[i].ip);
   s->queue_capacity=64;s->rate_limit_kbps=s->rate_effective_kbps=1000;gitm_render_live(ctx);
   if(state==1)gitm_set_state(ctx,GITM_RUNNING,"Simulated capture running. STOP to save the PCAP.",COLOR_MATERIAL_GREEN);
  }
  if(state==1)continue;
  g->session_active=false;g->session_task=NULL;
  if(g->scan_btn)lv_obj_clear_state(g->scan_btn,LV_STATE_DISABLED);
  if(g->connect_btn)lv_obj_clear_state(g->connect_btn,LV_STATE_DISABLED);
  if(state<0) {
   gitm_set_state(ctx,GITM_IDLE,error[0]?error:"Could not start simulated operation.",COLOR_MATERIAL_RED);
   if(job->kind==2){g->snap.active=g->snap.capture_active=false;gitm_restore_setup(ctx);}
  }else if(job->kind==0) {
   g->net_count=data[2]>MAX_NETWORKS?MAX_NETWORKS:data[2];
   for(int i=0;i<g->net_count;i++) {
    wifi_network_t *n=&g->nets[i];memset(n,0,sizeof(*n));int values[2]={0};char security[32]={0};
    app_gitm_network(job->id,i,n->ssid,n->bssid,security,values);
    snprintf(n->security,sizeof(n->security),"%s",security);n->channel=values[0];n->rssi=values[1];n->index=i+1;snprintf(n->band,sizeof(n->band),"%s",n->channel>14?"5GHz":"2.4GHz");
   }
   int selected=-1;for(int i=0;i<g->net_count;i++)if(g->preselect_ssid[0]&&!strcmp(g->nets[i].ssid,g->preselect_ssid))selected=i;
   g->preselect_ssid[0]=0;if(selected>=0)gitm_apply_upstream_pick(ctx,selected);else gitm_render_net_list(ctx);
   gitm_set_hint(ctx,"Pick the network that provides Internet, then CONNECT.",lv_color_hex(0xCCCCCC));
  }else if(job->kind==1) {
   gitm_collapse_upstream(ctx);if(g->step2)lv_obj_clear_flag(g->step2,LV_OBJ_FLAG_HIDDEN);
   g->s2_collapsed=false;gitm_set_collapsed(g->s2_content,g->s2_chev,false);gitm_update_name_preview(ctx);
   gitm_set_state(ctx,GITM_IDLE,"Name your GITM access point, then START.",lv_color_hex(0xCCCCCC));
  }else {
   g->final.valid=file[0]!=0;g->final.frames=data[0];snprintf(g->final.file,sizeof(g->final.file),"%s",file);
   if(g->stop_btn)lv_obj_add_flag(g->stop_btn,LV_OBJ_FLAG_HIDDEN);
   if(g->copy_btn&&g->final.valid)lv_obj_clear_flag(g->copy_btn,LV_OBJ_FLAG_HIDDEN);
   app_model_sync_files(tab);gitm_set_state(ctx,GITM_IDLE,"Simulated PCAP finalized. COPY TO TAB5 opens ESPShark.",COLOR_MATERIAL_GREEN);
  }
  bool leaving=job->leaving;memset(job,0,sizeof(*job));
  if(leaving){close_gitm_exit_confirm();gitm_leave_page(ctx);}
 }
}


static void gitm_copy_cb(lv_event_t *event) {
 (void)event;tab_context_t *ctx=get_current_ctx();if(!ctx||!ctx->gitm)return;
 if(!ctx->gitm->final.valid||!ctx->gitm->final.file[0]){gitm_set_hint(ctx,"No finalized capture path to copy.",COLOR_MATERIAL_RED);return;}
 if(app_model_sync_files(tab_id_for_ctx(ctx))<0){gitm_set_hint(ctx,"Could not copy simulated capture.",COLOR_MATERIAL_RED);return;}
 show_pcap_viewer_page();pcap_viewer_state_t *state=pcap_viewer_get_state(ctx,false);
 const char *path=ctx->gitm->final.file;const char *base=strrchr(path,'/');
 if(state)pcap_viewer_start_file_load(state,path,base?base+1:path,false);
}
