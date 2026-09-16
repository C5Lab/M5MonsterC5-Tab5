/* Native confirmation/active screens with exclusively offline model jobs. */
static struct { int id,kind; tab_context_t *ctx; lv_obj_t *owner; } app_global[4];

EM_JS(int,app_global_start_js,(int tab,int kind,char *error,int size),{
 try{return emulatorDevice.device.attackStart(emulatorDevice.module(tab),['blackout','snifferdog','global_handshaker'][kind]);}
 catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(int,app_global_handshakes_js,(int id,char *last,int size),{
 const rows=emulatorDevice.device.job(id)?.result?.handshakes||[];
 stringToUTF8(rows.at(-1)?.ssid||"",last,size);return rows.length;
});

static lv_obj_t *app_global_popup(tab_context_t *ctx,int kind) {
 return kind==0?ctx->blackout_popup:kind==1?ctx->snifferdog_popup:ctx->global_handshaker_popup;
}
static lv_obj_t *app_global_overlay(tab_context_t *ctx,int kind) {
 return kind==0?ctx->blackout_popup_overlay:kind==1?ctx->snifferdog_popup_overlay:ctx->global_handshaker_popup_overlay;
}
static void app_global_idle(tab_context_t *ctx,int kind) {
 if(kind==0)ctx->blackout_running=false;
 else if(kind==1)ctx->snifferdog_running=false;
 else {ctx->global_handshaker_monitoring=false;ctx->global_handshaker_task=NULL;}
}
static void app_global_close(tab_context_t *ctx,int kind) {
 if(kind==0)close_blackout_popup_ctx(ctx);
 else if(kind==1)close_snifferdog_popup_ctx(ctx);
 else close_global_handshaker_popup_ctx(ctx);
}
static void app_global_stop(int tab) {
 if(tab<0||tab>=4||!app_global[tab].id)return;
 app_da_stop_js(app_global[tab].id);app_global[tab].id=0;
 app_global_idle(app_global[tab].ctx,app_global[tab].kind);
}
static void app_global_deleted(lv_event_t *event) {
 tab_context_t *ctx=lv_event_get_user_data(event);int tab=tab_id_for_ctx(ctx);
 if(tab<0||tab>=4||lv_event_get_target(event)!=app_global[tab].owner)return;
 if(app_global[tab].id)app_model_cancel(app_global[tab].id);
 int kind=app_global[tab].kind;app_global_idle(ctx,kind);
 if(kind==0)ctx->blackout_popup_overlay=ctx->blackout_popup=NULL;
 else if(kind==1)ctx->snifferdog_popup_overlay=ctx->snifferdog_popup=NULL;
 else {
  ctx->global_handshaker_popup_overlay=ctx->global_handshaker_popup=NULL;
  ctx->global_handshaker_log_container=ctx->global_handshaker_status_label=ctx->global_handshaker_stats_label=NULL;
 }
 app_global[tab].id=0;app_global[tab].ctx=NULL;app_global[tab].owner=NULL;
}

static void app_global_confirm(lv_event_t *event,int kind) {
 tab_context_t *ctx=lv_event_get_user_data(event);if(!ctx)ctx=get_current_ctx();
 if(!ctx)return;
 int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=4||app_global[tab].id)return;
 char error[192]={0};int id=app_global_start_js(tab,kind,error,sizeof(error));
 if(!id){
  lv_obj_t *popup=app_global_popup(ctx,kind);
  if(popup)lv_label_set_text(lv_obj_get_child(popup,2),error);
  return;
 }
 app_global_close(ctx,kind);
 app_global[tab].id=id;app_global[tab].ctx=ctx;app_global[tab].kind=kind;
 if(kind==0)show_blackout_active_popup();
 else if(kind==1)show_snifferdog_active_popup();
 else show_global_handshaker_active_popup();
 app_global[tab].owner=app_global_overlay(ctx,kind);
 if(!app_global[tab].owner){app_model_cancel(id);app_global[tab].id=0;app_global_idle(ctx,kind);return;}
 lv_obj_add_event_cb(app_global[tab].owner,app_global_deleted,LV_EVENT_DELETE,ctx);
}
static void blackout_confirm_yes_cb(lv_event_t *e){app_global_confirm(e,0);}
static void snifferdog_confirm_yes_cb(lv_event_t *e){app_global_confirm(e,1);}
static void global_handshaker_confirm_yes_cb(lv_event_t *e){app_global_confirm(e,2);}
static void global_handshaker_monitor_task(void *arg){(void)arg;/* app_global_tick owns monitoring. */}

static void app_global_tick(void) {
 for(int tab=0;tab<4;tab++){
  int id=app_global[tab].id;if(!id)continue;
  tab_context_t *ctx=app_global[tab].ctx;int kind=app_global[tab].kind;
  lv_obj_t *popup=app_global_popup(ctx,kind);if(!popup)continue;
  char state[192];int status=app_da_poll_js(id,state,sizeof(state));
  if(kind==2){
   char last[64];int total=app_global_handshakes_js(id,last,sizeof(last));
   if(ctx->global_handshaker_total_captured!=total){
    ctx->global_handshaker_total_captured=total;
    snprintf(ctx->global_handshaker_last_ssid,sizeof(ctx->global_handshaker_last_ssid),"%s",last);
    update_global_handshaker_stats_ctx(ctx);
    lv_label_set_text_fmt(ctx->global_handshaker_status_label,"Simulated handshake: %s",last[0]?last:"(Hidden)");
   }
  } else lv_label_set_text(lv_obj_get_child(popup,2),state);
  if(status!=0){
   lv_label_set_text(lv_obj_get_child(popup,1),"Simulation stopped");
   if(kind==2&&ctx->global_handshaker_status_label)lv_label_set_text(ctx->global_handshaker_status_label,state);
   app_global_idle(ctx,kind);app_global[tab].id=0;
  }
 }
}
