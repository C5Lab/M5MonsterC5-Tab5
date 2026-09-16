/* Offline lifecycle around the retained firmware popup; no radio transport. */
static struct { int id; tab_context_t *ctx; lv_obj_t *owner; } app_sae[4];

static void app_sae_stop(int tab) {
 if(tab<0||tab>=4||!app_sae[tab].id)return;
 app_da_stop_js(app_sae[tab].id);
 app_sae[tab].id=0;
}

static void app_sae_deleted(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);int tab=tab_id_for_ctx(ctx);
 if(tab<0||tab>=4||lv_event_get_target(e)!=app_sae[tab].owner)return;
 if(app_sae[tab].id)app_model_cancel(app_sae[tab].id);
 app_sae[tab].id=0;app_sae[tab].owner=NULL;app_sae[tab].ctx=NULL;
 ctx->sae_popup_overlay=ctx->sae_popup=NULL;
 ctx->observer_attack_return_to_observer=false;
 clear_observer_attack_override(ctx);
}

static void app_sae_open(tab_context_t *ctx,int index) {
 int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=4||ctx->sae_popup)return;
 scan_view_t view=get_scan_view(ctx);
 if(index<0||index>=view.net_count)return;
 char error[192]={0};
 int id=app_da_start_js(tab,"sae_overflow",view.nets[index].bssid,"",error,sizeof(error));
 if(!id){
  lv_obj_t *label=ctx->popup_open?ctx->observer_status_label:ctx->scan_status_label;
  if(label)lv_label_set_text(label,error);
  return;
 }
 show_sae_popup(index);
 if(!ctx->sae_popup_overlay){app_model_cancel(id);return;}
 app_sae[tab].id=id;app_sae[tab].ctx=ctx;app_sae[tab].owner=ctx->sae_popup_overlay;
 lv_obj_add_event_cb(app_sae[tab].owner,app_sae_deleted,LV_EVENT_DELETE,ctx);
}

static void app_sae_tick(void) {
 for(int tab=0;tab<4;tab++){
  if(!app_sae[tab].id)continue;
  char state[192];int result=app_da_poll_js(app_sae[tab].id,state,sizeof(state));
  if(result==0)continue;
  tab_context_t *ctx=app_sae[tab].ctx;
  if(ctx&&ctx->sae_popup&&lv_obj_is_valid(ctx->sae_popup)){
   lv_obj_t *title=lv_obj_get_child(ctx->sae_popup,0);
   lv_label_set_text(title,"SAE Overflow stopped");
  }
  app_sae[tab].id=0;
 }
}
