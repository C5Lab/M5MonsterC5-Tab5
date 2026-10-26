/* Native LVGL shell around deterministic, offline WPA audit fixtures. */
static lv_obj_t *app_wpa_page, *app_wpa_body, *app_wpa_tile, *app_wpa_confirm, *app_wpa_progress;
static lv_obj_t *app_wpa_handshakes_buttons[4], *app_wpa_handshake_notices[4];
static bool app_wpa_selected[4], app_wpa_dirty;
static int app_wpa_wordlist, app_wpa_filter, app_wpa_state_filter, app_wpa_revision_seen;
static char app_wpa_notice[192], app_wpa_confirm_id[48];
static int app_wpa_confirm_action;

EM_JS(int,app_wpa_revision_js,(),{return emulatorDevice.wpa.revision();});
EM_JS(void,app_wpa_advance_js,(double ms),{emulatorDevice.wpa.advance(ms);});
EM_JS(int,app_wpa_capture_count_js,(),{return emulatorDevice.wpa.catalog().length;});
EM_JS(int,app_wpa_capture_js,(int index,char *id,char *ssid,char *name,char *format,char *validation,char *reason,char *sources,int *flags),{
 const f=emulatorDevice.wpa.catalog()[index];if(!f)return 0;
 stringToUTF8(f.id,id,128);stringToUTF8(f.ssid,ssid,128);stringToUTF8(f.name,name,128);
 stringToUTF8(f.format,format,128);stringToUTF8(f.validation,validation,128);
 stringToUTF8(f.reason,reason,128);stringToUTF8(f.sources.join(' + ').toUpperCase(),sources,128);
 HEAP32[flags>>2]=f.local?1:0;HEAP32[(flags>>2)+1]=f.available?1:0;return 1;
});
EM_JS(int,app_wpa_session_count_js,(),{return emulatorDevice.wpa.sessions().length;});
EM_JS(int,app_wpa_session_js,(int index,char *id,char *ssid,char *name,char *wordlist,int *values),{
 const s=emulatorDevice.wpa.sessions()[index];if(!s)return 0;
 stringToUTF8(s.id,id,128);stringToUTF8(s.ssid,ssid,128);stringToUTF8(s.name,name,128);
 stringToUTF8(s.wordlist,wordlist,128);
 HEAP32[values>>2]=s.tried;HEAP32[(values>>2)+1]=s.total;
 HEAP32[(values>>2)+2]=s.workers;HEAP32[(values>>2)+3]=s.state==='running'?1:0;return 1;
});
EM_JS(int,app_wpa_queue_count_js,(),{return emulatorDevice.wpa.queue().length;});
EM_JS(int,app_wpa_queue_pending_js,(),{
 return emulatorDevice.wpa.queue().some(item=>item.state==='pending')?1:0;
});
EM_JS(int,app_wpa_queue_paused_js,(),{
 return emulatorDevice.wpa.queue().some(item=>item.state==='paused')?1:0;
});
EM_JS(int,app_wpa_queue_cancelable_js,(),{
 return emulatorDevice.wpa.queue().some(item=>['pending','running','paused'].includes(item.state))?1:0;
});
EM_JS(int,app_wpa_batch_active_js,(),{
 const active=emulatorDevice.wpa.active();
 return emulatorDevice.wpa.batchRunning()&&active?.queueIndex>=0?1:0;
});
EM_JS(int,app_wpa_queue_running_js,(int index),{
 return emulatorDevice.wpa.queue()[index]?.state==='running'?1:0;
});
EM_JS(int,app_wpa_queue_contains_js,(const char *id),{
 return emulatorDevice.wpa.queue().some(item=>item.captureId===UTF8ToString(id))?1:0;
});
EM_JS(int,app_wpa_queue_editable_js,(),{
 const a=emulatorDevice.wpa,q=a.queue();
 return q.length&&!a.active()&&!a.batchRunning()&&q.every(item=>item.state==='pending')?1:0;
});
EM_JS(void,app_wpa_queue_js,(int index,char *out,int size),{
 const q=emulatorDevice.wpa.queue()[index],c=emulatorDevice.wpa.catalog().find(c=>c.id===q.captureId);
 stringToUTF8((c?.ssid||q.captureId)+' | '+q.state.replace('_',' ').toUpperCase()+' | '+q.tried+' tried | '+q.wordlist,out,size);
});
EM_JS(int,app_wpa_history_count_js,(),{return emulatorDevice.wpa.history().length;});
EM_JS(void,app_wpa_history_js,(int index,char *out,int size),{
 const h=emulatorDevice.wpa.history()[index];
 stringToUTF8(h.ssid+' | '+h.outcome+' | '+h.tried+' tried | '+h.workers+' workers | '+h.wordlist,out,size);
});
EM_JS(int,app_wpa_recovered_count_js,(),{return emulatorDevice.wpa.recovered().length;});
EM_JS(void,app_wpa_recovered_js,(int index,char *out,int size),{
 const r=emulatorDevice.wpa.recovered()[index];
 stringToUTF8(r.ssid+' | '+r.credential+' | '+r.source,out,size);
});
EM_JS(int,app_wpa_capture_state_js,(const char *id),{
 const state=emulatorDevice.wpa.catalogState(UTF8ToString(id));
 return {'New':1,'Resumable':2,'Found':3,'Not found':4,'Error':5,'Invalid':6}[state]||0;
});
EM_JS(void,app_wpa_progress_js,(char *out,int size),{
 const a=emulatorDevice.wpa.active();
  stringToUTF8(a?'Audit running: '+a.ssid+' | '+a.tried+'/'+a.total+' tried | '+a.workers+' virtual workers':String(),out,size);
});
EM_JS(int,app_wpa_action_js,(const char *action,const char *id,int wordlist,char *out,int size),{
 const a=emulatorDevice.wpa,op=UTF8ToString(action),key=UTF8ToString(id);
 const list=a.wordlists()[wordlist]||a.wordlists()[0];
 try{
  let message=String();
  if(op==='audit'){a.start(key,list);message='Audit started with '+list+'.';}
  else if(op==='sync'){message=a.syncCapture(key)?'1 copied in the synthetic catalogue.':'Already local in the synthetic catalogue.';}
  else if(op==='sync-all'){const r=a.syncAll();message=r.copied+' copied in the synthetic catalogue, '+r.alreadyLocal+' already local, '+r.skippedInvalid+' invalid skipped.';}
  else if(op==='pause'){a.pause();message='Audit paused at a safe candidate offset.';}
  else if(op==='resume'){a.resume(key);message='Audit resumed from saved progress.';}
  else if(op==='start-over'){a.startOver(key);message='Audit restarted from zero.';}
  else if(op==='delete'){a.deleteAudit(key);message='Audit checkpoint deleted; capture kept.';}
  else if(op==='queue-add'){const q=a.addToQueue(key.split(',').filter(Boolean),list);message=q.length+' queued with '+list+'.';}
  else if(op==='queue-clear'){a.clearQueue();message='Batch queue cleared.';}
  else if(op==='batch-start'){a.startBatch();message='Batch started.';}
  else if(op==='batch-resume'){a.resumeBatch();message='Batch resumed.';}
  else if(op==='skip-current'){a.skipCurrent();message='Current batch item skipped; checkpoint retained.';}
  else if(op==='queue-remove'){a.removeQueueItem(Number(key));message='Queue row removed; checkpoints retained.';}
  else if(op==='batch-cancel'){a.cancelBatch();message='Batch cancelled. Captures and checkpoints were kept.';}
  else if(op==='delete-local'){a.deleteLocalCapture(key);message='Deleted local Tab5 copy. Worker copies were kept.';}
  else if(op==='queue-wordlist'){a.setQueueWordlist(list);message='Candidate list updated for every queued capture.';}
  else if(op==='batch-new'){a.newBatch();message='New batch ready; audit checkpoints were kept.';}
  else throw new Error('Unknown audit action');
  stringToUTF8(message,out,size);return 1;
 }catch(e){stringToUTF8(e.message,out,size);return 0;}
});
EM_JS(int,app_wpa_capture_for_ssid_js,(const char *ssid,char *out,int size),{
 const capture=emulatorDevice.wpa.catalog().find(c=>c.ssid===UTF8ToString(ssid)&&c.validation==='ready');
 if(!capture)return 0;stringToUTF8(capture.id,out,size);return 1;
});
EM_JS(int,app_wpa_capture_for_file_js,(int tab,const char *path,char *id,int id_size,
                                       char *ssid,int ssid_size,char *reason,int reason_size),{
 try{
  const module=emulatorDevice.module(tab),name=UTF8ToString(path);
   const bytes=emulatorDevice.device.readFile(module,name);
   const capture=emulatorDevice.wpa.captureForFile(module,name,bytes);
  if(!capture)return 0;
  stringToUTF8(capture.id,id,id_size);stringToUTF8(capture.ssid,ssid,ssid_size);
  stringToUTF8(capture.reason,reason,reason_size);
  return capture.validation==='ready'?1:-1;
 }catch{return 0;}
});

static lv_obj_t *app_wpa_label(lv_obj_t *parent,const char *text,const lv_font_t *font,lv_color_t color){
 lv_obj_t *label=lv_label_create(parent);lv_label_set_text(label,text);
 lv_obj_set_width(label,lv_pct(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
 lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,color,0);return label;
}
static lv_obj_t *app_wpa_card(lv_obj_t *parent){
 lv_obj_t *card=lv_obj_create(parent);lv_obj_set_width(card,lv_pct(100));
 lv_obj_set_height(card,LV_SIZE_CONTENT);style_surface_panel(card,12);
 lv_obj_set_style_pad_all(card,14,0);lv_obj_set_style_pad_row(card,9,0);
 lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE);
 return card;
}
static lv_obj_t *app_wpa_actions(lv_obj_t *parent){
 lv_obj_t *row=lv_obj_create(parent);lv_obj_set_width(row,lv_pct(100));
 lv_obj_set_height(row,LV_SIZE_CONTENT);lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(row,0,0);lv_obj_set_style_pad_all(row,0,0);
 lv_obj_set_style_pad_gap(row,8,0);lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW_WRAP);
 lv_obj_clear_flag(row,LV_OBJ_FLAG_SCROLLABLE);return row;
}
static lv_obj_t *app_wpa_button(lv_obj_t *parent,const char *label,const char *binding,
                                lv_event_cb_t cb,void *data,bool disabled){
 lv_obj_t *button=lv_btn_create(parent);lv_obj_set_size(button,LV_SIZE_CONTENT,44);
 lv_obj_set_style_min_width(button,100,0);lv_obj_set_style_pad_hor(button,13,0);
 lv_obj_set_style_bg_color(button,COLOR_MATERIAL_PURPLE,0);
 lv_obj_set_style_radius(button,10,0);lv_obj_set_style_shadow_width(button,0,0);
 if(disabled)lv_obj_add_state(button,LV_STATE_DISABLED);
 else app_bind_adapter(button,cb,LV_EVENT_CLICKED,data,binding);
 lv_obj_t *text=lv_label_create(button);lv_label_set_text(text,label);
 lv_obj_set_style_text_font(text,&lv_font_montserrat_14,0);
 lv_obj_set_style_text_color(text,lv_color_white(),0);lv_obj_center(text);return button;
}
static void app_wpa_close(void){
 if(app_wpa_confirm){lv_obj_del(app_wpa_confirm);app_wpa_confirm=NULL;}
 if(app_wpa_page){lv_obj_del(app_wpa_page);app_wpa_page=NULL;}
 app_wpa_body=NULL;app_wpa_progress=NULL;
}
static void app_wpa_back_cb(lv_event_t *e){(void)e;app_wpa_close();show_internal_tiles();}
static bool app_wpa_apply(const char *action,const char *id){
 char message[192]={0};
 bool ok=app_wpa_action_js(action,id?id:"",app_wpa_wordlist,message,sizeof(message))==1;
 snprintf(app_wpa_notice,sizeof(app_wpa_notice),"%s",message);
 app_wpa_dirty=true;return ok;
}
static void app_wpa_confirm_cancel_cb(lv_event_t *e){
 (void)e;if(app_wpa_confirm){lv_obj_del(app_wpa_confirm);app_wpa_confirm=NULL;}
}
static void app_wpa_confirm_yes_cb(lv_event_t *e){
 (void)e;const char *actions[]={"","start-over","delete","delete-local","batch-cancel"};
 if(app_wpa_apply(actions[app_wpa_confirm_action],app_wpa_confirm_id)&&
    app_wpa_confirm_action==3)memset(app_wpa_selected,0,sizeof(app_wpa_selected));
 app_wpa_confirm_cancel_cb(NULL);
}
static void app_wpa_ask(int action,const char *id){
 if(app_wpa_confirm||!app_wpa_page)return;
 app_wpa_confirm_action=action;snprintf(app_wpa_confirm_id,sizeof(app_wpa_confirm_id),"%s",id);
 app_wpa_confirm=lv_obj_create(internal_container);
 lv_obj_set_size(app_wpa_confirm,lv_pct(100),lv_pct(100));
 lv_obj_set_style_bg_color(app_wpa_confirm,lv_color_hex(0x10131C),0);
 lv_obj_set_style_bg_opa(app_wpa_confirm,LV_OPA_80,0);
 lv_obj_set_style_border_width(app_wpa_confirm,0,0);
 lv_obj_set_flex_flow(app_wpa_confirm,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_flex_align(app_wpa_confirm,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER,LV_FLEX_ALIGN_CENTER);
 lv_obj_t *card=app_wpa_card(app_wpa_confirm);lv_obj_set_width(card,lv_pct(78));
 const char *titles[]={"","Start this audit over from zero?","Delete this saved audit?",
                       "Delete local capture?","Cancel entire batch?"};
 const char *details[]={"","The capture and other audit sessions remain available.",
   "The capture and other audit sessions remain available.",
   "Remove the Tab5 copy. Copies on virtual workers and saved audits are kept.",
   "Queued and paused rows become cancelled. Capture files and audit checkpoints are kept."};
 app_wpa_label(card,titles[action],&lv_font_montserrat_18,ui_text_color());
 app_wpa_label(card,details[action],
               &lv_font_montserrat_14,ui_muted_color());
 lv_obj_t *buttons=app_wpa_actions(card);
 app_wpa_button(buttons,"Cancel","emu.wpa.confirm.cancel",app_wpa_confirm_cancel_cb,NULL,false);
 const char *confirm[]={"","Start over","Delete audit","Delete from Tab5","Cancel batch"};
 app_wpa_button(buttons,confirm[action],"emu.wpa.confirm.yes",
                app_wpa_confirm_yes_cb,NULL,false);
}
static void app_wpa_session_cb(lv_event_t *e){
 intptr_t packed=(intptr_t)lv_event_get_user_data(e);
 int index=(int)(packed/10),action=(int)(packed%10);
 char id[128],ssid[128],name[128],wordlist[128];int values[4];
 if(!app_wpa_session_js(index,id,ssid,name,wordlist,values))return;
 if(action==1)app_wpa_apply("resume",id);
 else if(action==2)app_wpa_ask(1,id);
 else if(action==3)app_wpa_ask(2,id);
}
static void app_wpa_capture_cb(lv_event_t *e){
 intptr_t packed=(intptr_t)lv_event_get_user_data(e);
 int index=(int)(packed/10),action=(int)(packed%10);
 char id[128],ssid[128],name[128],format[128],validation[128],reason[128],sources[128];int flags[2];
 if(!app_wpa_capture_js(index,id,ssid,name,format,validation,reason,sources,flags))return;
 if(action==1&&!app_wpa_queue_contains_js(id)){
  app_wpa_selected[index]=!app_wpa_selected[index];app_wpa_dirty=true;
 }
 else if(action==2)app_wpa_apply("audit",id);
 else if(action==3)app_wpa_apply("sync",id);
 else if(action==4)app_wpa_ask(3,id);
}
static void app_wpa_queue_cb(lv_event_t *e){
 int index=(int)(intptr_t)lv_event_get_user_data(e);char row[12];
 snprintf(row,sizeof(row),"%d",index);app_wpa_apply("queue-remove",row);
}
static bool app_wpa_visible_source(const char *sources,int local);
static void app_wpa_simple_cb(lv_event_t *e){
 intptr_t action=(intptr_t)lv_event_get_user_data(e);
 if(action==1)app_wpa_apply("sync-all","");
 else if(action==2)app_wpa_apply("pause","");
 else if(action==3){
  char ids[192]={0};int count=app_wpa_capture_count_js();
  for(int i=0;i<count&&i<4;i++)if(app_wpa_selected[i]){
   char id[128],ssid[128],name[128],format[128],validation[128],reason[128],sources[128];int flags[2];
   if(!app_wpa_capture_js(i,id,ssid,name,format,validation,reason,sources,flags))continue;
   if(ids[0])strncat(ids,",",sizeof(ids)-strlen(ids)-1);
   strncat(ids,id,sizeof(ids)-strlen(ids)-1);
  }
  if(app_wpa_apply("queue-add",ids))memset(app_wpa_selected,0,sizeof(app_wpa_selected));
 }else if(action==4)app_wpa_apply("queue-clear","");
 else if(action==5)app_wpa_apply("batch-start","");
 else if(action==6)app_wpa_apply("batch-resume","");
 else if(action==7)app_wpa_apply("skip-current","");
 else if(action==8)app_wpa_ask(4,"");
 else if(action==11)app_wpa_apply("batch-new","");
 else if(action==9){
  int count=app_wpa_capture_count_js();
  for(int i=0;i<count&&i<4;i++){
   char id[128],ssid[128],name[128],format[128],validation[128],reason[128],sources[128];
   int flags[2];
   if(!app_wpa_capture_js(i,id,ssid,name,format,validation,reason,sources,flags))continue;
   if(!strcmp(validation,"ready")&&flags[1]&&!app_wpa_queue_contains_js(id)&&
      app_wpa_visible_source(sources,flags[0])&&
      (!app_wpa_state_filter||app_wpa_capture_state_js(id)==app_wpa_state_filter))
      app_wpa_selected[i]=true;
  }
  app_wpa_dirty=true;
 }else if(action==10){memset(app_wpa_selected,0,sizeof(app_wpa_selected));app_wpa_dirty=true;}
}
static void app_wpa_wordlist_cb(lv_event_t *e){
 app_wpa_wordlist=(int)lv_dropdown_get_selected(lv_event_get_target(e));
 if(app_wpa_queue_editable_js())app_wpa_apply("queue-wordlist","");
 else app_wpa_dirty=true;
}
static void app_wpa_filter_cb(lv_event_t *e){
 app_wpa_filter=(int)lv_dropdown_get_selected(lv_event_get_target(e));app_wpa_dirty=true;
}
static void app_wpa_state_filter_cb(lv_event_t *e){
 app_wpa_state_filter=(int)lv_dropdown_get_selected(lv_event_get_target(e));app_wpa_dirty=true;
}
static bool app_wpa_visible_source(const char *sources,int local){
 static const char *labels[]={"","LOCAL","GROVE","USB","MBUS"};
 if(app_wpa_filter==0)return true;
 if(app_wpa_filter==1)return local!=0;
 return strstr(sources,labels[app_wpa_filter])!=NULL;
}
static void app_wpa_render(void){
 if(current_tab!=TAB_INTERNAL||!internal_container)return;
 int32_t saved_scroll=app_wpa_body?lv_obj_get_scroll_y(app_wpa_body):0;
 app_wpa_close();if(internal_tiles)lv_obj_add_flag(internal_tiles,LV_OBJ_FLAG_HIDDEN);
 if(internal_settings_page)lv_obj_add_flag(internal_settings_page,LV_OBJ_FLAG_HIDDEN);
 app_wpa_page=lv_obj_create(internal_container);
 lv_obj_set_size(app_wpa_page,lv_pct(100),lv_pct(100));
 lv_obj_set_style_bg_color(app_wpa_page,ui_bg_color(),0);
 lv_obj_set_style_border_width(app_wpa_page,0,0);lv_obj_set_style_radius(app_wpa_page,0,0);
 lv_obj_set_style_pad_all(app_wpa_page,12,0);lv_obj_set_style_pad_row(app_wpa_page,10,0);
 lv_obj_set_flex_flow(app_wpa_page,LV_FLEX_FLOW_COLUMN);
 lv_obj_clear_flag(app_wpa_page,LV_OBJ_FLAG_SCROLLABLE);

 lv_obj_t *header=app_wpa_actions(app_wpa_page);
 app_wpa_button(header,LV_SYMBOL_LEFT " Back","emu.wpa.back",app_wpa_back_cb,NULL,false);
 lv_obj_t *heading=lv_obj_create(header);lv_obj_set_flex_grow(heading,1);
 lv_obj_set_height(heading,LV_SIZE_CONTENT);lv_obj_set_style_bg_opa(heading,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(heading,0,0);lv_obj_set_style_pad_all(heading,0,0);
 lv_obj_set_flex_flow(heading,LV_FLEX_FLOW_COLUMN);lv_obj_clear_flag(heading,LV_OBJ_FLAG_SCROLLABLE);
 app_wpa_label(heading,"WPA PSK Auditor",&lv_font_montserrat_22,ui_text_color());
 app_wpa_label(heading,"Capture readiness, durable resume and audit history",
               &lv_font_montserrat_12,ui_muted_color());

 lv_obj_t *body=lv_obj_create(app_wpa_page);lv_obj_set_size(body,lv_pct(100),0);
 app_wpa_body=body;
 lv_obj_set_flex_grow(body,1);lv_obj_set_style_bg_opa(body,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(body,0,0);lv_obj_set_style_pad_all(body,0,0);
 lv_obj_set_style_pad_row(body,10,0);lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);
 lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(body,LV_DIR_VER);
 app_wpa_label(body,"Offline fixture audit. No radio traffic, packet parsing or key derivation.",
               &lv_font_montserrat_14,COLOR_MATERIAL_AMBER);
 if(app_wpa_notice[0])app_wpa_label(body,app_wpa_notice,&lv_font_montserrat_14,COLOR_MATERIAL_CYAN);

 lv_obj_t *batch=app_wpa_card(body);
 app_wpa_label(batch,"Batch queue",&lv_font_montserrat_18,ui_text_color());
 int queue_count=app_wpa_queue_count_js();char info[256];
 snprintf(info,sizeof(info),"%d queued | one virtual audit at a time",queue_count);
 app_wpa_label(batch,info,&lv_font_montserrat_14,ui_muted_color());
 if(queue_count==0)app_wpa_label(batch,"Select ready captures below, then add them to the queue.",
                                  &lv_font_montserrat_14,ui_muted_color());
 else for(int i=0;i<queue_count;i++){
  app_wpa_queue_js(i,info,sizeof(info));lv_obj_t *row=app_wpa_actions(batch);
  lv_obj_t *label=app_wpa_label(row,info,&lv_font_montserrat_14,ui_text_color());
  lv_obj_set_flex_grow(label,1);char binding[64];
  snprintf(binding,sizeof(binding),"emu.wpa.batch.row.%d.remove",i);
  app_wpa_button(row,"Remove",binding,app_wpa_queue_cb,(void *)(intptr_t)i,
                 app_wpa_queue_running_js(i));
 }
 lv_obj_t *batch_actions=app_wpa_actions(batch);
 bool active=false;int session_count=app_wpa_session_count_js();
 for(int i=0;i<session_count;i++){
  char id[128],ssid[128],name[128],wordlist[128];int values[4];
  if(app_wpa_session_js(i,id,ssid,name,wordlist,values))active|=values[3]!=0;
 }
 bool has_selection=false;
 for(int i=0;i<4;i++)has_selection|=app_wpa_selected[i];
 app_wpa_button(batch_actions,"Add selected","emu.wpa.batch.add",app_wpa_simple_cb,(void *)3,
                !has_selection||active);
 app_wpa_button(batch_actions,"Clear queue","emu.wpa.batch.clear",app_wpa_simple_cb,(void *)4,
                queue_count==0||app_wpa_queue_paused_js());
 app_wpa_button(batch_actions,"New batch","emu.wpa.batch.new",app_wpa_simple_cb,(void *)11,
                queue_count==0||active);
 app_wpa_button(batch_actions,"Start batch","emu.wpa.batch.start",app_wpa_simple_cb,(void *)5,
                !app_wpa_queue_pending_js()||app_wpa_queue_paused_js()||active);
 app_wpa_button(batch_actions,"Resume batch","emu.wpa.batch.resume",app_wpa_simple_cb,(void *)6,
                !app_wpa_queue_paused_js()||active);
 app_wpa_button(batch_actions,"Skip current","emu.wpa.batch.skip",app_wpa_simple_cb,(void *)7,
                !app_wpa_batch_active_js());
 app_wpa_button(batch_actions,"Cancel batch","emu.wpa.batch.cancel",app_wpa_simple_cb,(void *)8,
                !app_wpa_queue_cancelable_js());

 lv_obj_t *sessions=app_wpa_card(body);
 app_wpa_label(sessions,"Resumable audits",&lv_font_montserrat_18,ui_text_color());
 if(session_count==0)app_wpa_label(sessions,"No resumable audit",&lv_font_montserrat_14,ui_muted_color());
 for(int i=0;i<session_count;i++){
  char id[128],ssid[128],name[128],wordlist[128];int values[4];
  if(!app_wpa_session_js(i,id,ssid,name,wordlist,values))continue;
  snprintf(info,sizeof(info),"%s | %s\n%s | %d/%d tried | %d virtual workers",
           ssid,values[3]?"Audit running":"Paused",wordlist,values[0],values[1],values[2]);
  app_wpa_label(sessions,info,&lv_font_montserrat_14,ui_text_color());
  lv_obj_t *actions=app_wpa_actions(sessions);
  if(values[3]){
   if(!app_wpa_progress)app_wpa_progress=app_wpa_label(sessions,"",&lv_font_montserrat_14,COLOR_MATERIAL_CYAN);
   app_wpa_button(actions,"Pause","emu.wpa.audit.pause",app_wpa_simple_cb,(void *)2,false);
  }else{
   char binding[100];snprintf(binding,sizeof(binding),"emu.wpa.session.%s.resume",id);
   app_wpa_button(actions,"Resume",binding,app_wpa_session_cb,(void *)(intptr_t)(i*10+1),active);
   snprintf(binding,sizeof(binding),"emu.wpa.session.%s.start-over",id);
   app_wpa_button(actions,"Start over",binding,app_wpa_session_cb,(void *)(intptr_t)(i*10+2),active);
   snprintf(binding,sizeof(binding),"emu.wpa.session.%s.delete",id);
   app_wpa_button(actions,"Delete audit",binding,app_wpa_session_cb,(void *)(intptr_t)(i*10+3),active);
  }
 }
 if(app_wpa_progress){app_wpa_progress_js(info,sizeof(info));lv_label_set_text(app_wpa_progress,info);}

 lv_obj_t *catalog=app_wpa_card(body);
 app_wpa_label(catalog,"Capture catalog",&lv_font_montserrat_18,ui_text_color());
 lv_obj_t *controls=app_wpa_actions(catalog);
 lv_obj_t *source=lv_dropdown_create(controls);lv_dropdown_set_options(source,"ALL\nLOCAL\nGROVE\nUSB\nMBUS");
 lv_dropdown_set_selected(source,app_wpa_filter);lv_obj_set_size(source,165,44);
 app_bind_adapter(source,app_wpa_filter_cb,LV_EVENT_VALUE_CHANGED,NULL,"emu.wpa.source");
 lv_obj_t *state=lv_dropdown_create(controls);
 lv_dropdown_set_options(state,"All states\nNew\nResumable\nFound\nNot found\nError\nInvalid");
 lv_dropdown_set_selected(state,app_wpa_state_filter);lv_obj_set_size(state,175,44);
 app_bind_adapter(state,app_wpa_state_filter_cb,LV_EVENT_VALUE_CHANGED,NULL,"emu.wpa.state");
 lv_obj_t *wordlist=lv_dropdown_create(controls);
 lv_dropdown_set_options(wordlist,"Demo shortlist\nExpanded demo");
 lv_dropdown_set_selected(wordlist,app_wpa_wordlist);lv_obj_set_size(wordlist,210,44);
 app_bind_adapter(wordlist,app_wpa_wordlist_cb,LV_EVENT_VALUE_CHANGED,NULL,"emu.wpa.wordlist");
 app_wpa_button(controls,"Sync all to Tab5","emu.wpa.sync-all",app_wpa_simple_cb,(void *)1,false);
 app_wpa_button(controls,"Select visible","emu.wpa.select-visible",app_wpa_simple_cb,(void *)9,active);
 app_wpa_button(controls,"Clear selection","emu.wpa.clear-selection",app_wpa_simple_cb,(void *)10,
                !has_selection);
 app_wpa_label(catalog,"Merged sources share one row. Invalid captures remain visible but cannot run.",
               &lv_font_montserrat_12,ui_muted_color());
 int capture_count=app_wpa_capture_count_js();
 for(int i=0;i<capture_count&&i<4;i++){
  char id[128],ssid[128],name[128],format[128],validation[128],reason[128],sources[128];int flags[2];
  if(!app_wpa_capture_js(i,id,ssid,name,format,validation,reason,sources,flags)||
     !app_wpa_visible_source(sources,flags[0])||
     (app_wpa_state_filter&&app_wpa_capture_state_js(id)!=app_wpa_state_filter))continue;
  lv_obj_t *row=app_wpa_card(catalog);
  snprintf(info,sizeof(info),"%s | %s | %s",ssid,format,
           !strcmp(validation,"ready")?"READY":"NEEDS INVESTIGATION");
  app_wpa_label(row,info,&lv_font_montserrat_16,
                !strcmp(validation,"ready")?ui_text_color():COLOR_MATERIAL_AMBER);
  snprintf(info,sizeof(info),"%s\n%s\nSources: %s",name,reason,sources);
  app_wpa_label(row,info,&lv_font_montserrat_12,ui_muted_color());
  lv_obj_t *actions=app_wpa_actions(row);bool ready=!strcmp(validation,"ready")&&flags[1];
  char binding[100];snprintf(binding,sizeof(binding),"emu.wpa.capture.%s.select",id);
  app_wpa_button(actions,app_wpa_selected[i]?"Selected":"Select",binding,
                 app_wpa_capture_cb,(void *)(intptr_t)(i*10+1),
                 !ready||active||app_wpa_queue_contains_js(id));
  snprintf(binding,sizeof(binding),"emu.wpa.capture.%s.audit",id);
  app_wpa_button(actions,"Audit",binding,app_wpa_capture_cb,(void *)(intptr_t)(i*10+2),!ready||active);
  if(!flags[0]&&ready){
   snprintf(binding,sizeof(binding),"emu.wpa.capture.%s.sync",id);
   app_wpa_button(actions,"Sync to Tab5",binding,app_wpa_capture_cb,
                  (void *)(intptr_t)(i*10+3),false);
  }
  if(flags[0]){
   snprintf(binding,sizeof(binding),"emu.wpa.capture.%s.delete-local",id);
   app_wpa_button(actions,"Delete from Tab5",binding,app_wpa_capture_cb,
                  (void *)(intptr_t)(i*10+4),active);
  }
 }

 lv_obj_t *history=app_wpa_card(body);
 app_wpa_label(history,"Recent history",&lv_font_montserrat_18,ui_text_color());
 int history_count=app_wpa_history_count_js();
 if(!history_count)app_wpa_label(history,"No audit history yet",&lv_font_montserrat_14,ui_muted_color());
 for(int i=0;i<history_count;i++){app_wpa_history_js(i,info,sizeof(info));
  app_wpa_label(history,info,&lv_font_montserrat_14,ui_muted_color());}
 lv_obj_t *recovered=app_wpa_card(body);
 app_wpa_label(recovered,"Recovered demo credentials",&lv_font_montserrat_18,ui_text_color());
 int recovered_count=app_wpa_recovered_count_js();
 if(!recovered_count)app_wpa_label(recovered,"No synthetic credential found yet.",
                                    &lv_font_montserrat_14,ui_muted_color());
 for(int i=0;i<recovered_count;i++){app_wpa_recovered_js(i,info,sizeof(info));
  app_wpa_label(recovered,info,&lv_font_montserrat_14,COLOR_MATERIAL_GREEN);}
 lv_obj_update_layout(body);
 if(saved_scroll>0)lv_obj_scroll_to_y(body,saved_scroll,LV_ANIM_OFF);
 app_wpa_dirty=false;app_wpa_revision_seen=app_wpa_revision_js();
}
static void app_wpa_show(void){app_wpa_render();}
static void app_wpa_tile_clicked(lv_event_t *e){(void)e;app_wpa_show();}
static void app_wpa_tile_deleted(lv_event_t *e){(void)e;app_wpa_tile=NULL;}
static void app_wpa_handshakes_button_deleted(lv_event_t *e){
 int tab=(int)(intptr_t)lv_event_get_user_data(e);
 if(tab>=0&&tab<4)app_wpa_handshakes_buttons[tab]=NULL;
}
static void app_wpa_handshake_notice_deleted(lv_event_t *e){
 int tab=(int)(intptr_t)lv_event_get_user_data(e);
 if(tab>=0&&tab<4)app_wpa_handshake_notices[tab]=NULL;
}
static void app_wpa_from_handshakes_cb(lv_event_t *e){
 (void)e;emu_show(0,TAB_INTERNAL);
 snprintf(app_wpa_notice,sizeof(app_wpa_notice),
          "Compromised Data: Handshakes links to the synthetic catalog for offline audits.");
 app_wpa_show();
}
static void app_wpa_handshake_status(tab_context_t *ctx,int tab,const char *message){
 if(!ctx||!ctx->handshakes_page||tab<0||tab>=4)return;
 lv_obj_t *label=app_wpa_handshake_notices[tab];
 if(!label||!lv_obj_is_valid(label)){
  label=lv_label_create(ctx->handshakes_page);app_wpa_handshake_notices[tab]=label;
  lv_obj_set_width(label,lv_pct(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(label,COLOR_MATERIAL_AMBER,0);
  lv_obj_move_to_index(label,2);
  lv_obj_add_event_cb(label,app_wpa_handshake_notice_deleted,LV_EVENT_DELETE,(void *)(intptr_t)tab);
 }
 lv_label_set_text(label,message);
}
static void app_wpa_tick(double ms){
 app_wpa_advance_js(ms);
 if(internal_tiles&&!app_wpa_tile){
  app_wpa_tile=create_tile(internal_tiles,LV_SYMBOL_EYE_OPEN,"WPA PSK\nAuditor",
                           COLOR_MATERIAL_AMBER,internal_tile_event_cb,"WPA PSK Auditor");
  lv_obj_add_event_cb(app_wpa_tile,app_wpa_tile_clicked,LV_EVENT_CLICKED,NULL);
  lv_obj_add_event_cb(app_wpa_tile,app_wpa_tile_deleted,LV_EVENT_DELETE,NULL);
  apply_tool_order(internal_tiles,TO_INTERNAL,internal_tile_event_cb);
 }
 tab_context_t *ctx=get_current_ctx();
 int tab=ctx?tab_id_for_ctx(ctx):-1;
 if(ctx&&tab>=0&&tab<4&&ctx->handshakes_page&&
    ctx->current_visible_page==ctx->handshakes_page&&
    !lv_obj_has_flag(ctx->handshakes_page,LV_OBJ_FLAG_HIDDEN)&&!app_wpa_handshakes_buttons[tab]){
  app_wpa_handshakes_buttons[tab]=app_wpa_button(ctx->handshakes_page,"Open WPA Auditor",
                     "emu.wpa.handshakes.open",app_wpa_from_handshakes_cb,NULL,false);
  lv_obj_move_to_index(app_wpa_handshakes_buttons[tab],1);
  lv_obj_add_event_cb(app_wpa_handshakes_buttons[tab],app_wpa_handshakes_button_deleted,
                      LV_EVENT_DELETE,(void *)(intptr_t)tab);
 }
 if(app_wpa_page&&current_tab!=TAB_INTERNAL){app_wpa_close();return;}
 if(app_wpa_page&&(app_wpa_dirty||app_wpa_revision_seen!=app_wpa_revision_js()))app_wpa_render();
 else if(app_wpa_progress){
  char progress[256];app_wpa_progress_js(progress,sizeof(progress));lv_label_set_text(app_wpa_progress,progress);
 }
}
/* Legacy Handshaker's result route opens the matching synthetic catalog row. */
static void handshaker_crack_cb(lv_event_t *e){
 tab_context_t *ctx=lv_event_get_user_data(e);
 if(!ctx||!ctx->handshaker_capture_success)return;
 int tab=tab_id_for_ctx(ctx);char capture[64]={0};
 if(tab<0||tab>=4||!app_wpa_capture_for_ssid_js(app_hs[tab].target,capture,sizeof(capture))){
  emu_unavailable("a matching synthetic WPA capture for Crack latest");return;
 }
 handshaker_popup_close_cb(NULL);
 emu_show(0,TAB_INTERNAL);
 snprintf(app_wpa_notice,sizeof(app_wpa_notice),"Crack latest: %s is ready in the synthetic catalog.",capture);
 app_wpa_show();
}
/* The Handshakes row carries its selected file index, not a network SSID. */
static void hs_crack_file_cb(lv_event_t *e){
 tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 void *data=lv_event_get_user_data(e);
 int tab=tab_id_for_ctx(ctx),index=compromised_index_from_user_data(data);
 if(tab<0||tab>=3||compromised_kind_from_user_data(data)!=COMPROMISED_FILE_KIND_HANDSHAKE||
    index<0||index>=ctx->wardrive_wigle_file_count||!ctx->wardrive_wigle_files)return;
 wardrive_wigle_file_t *file=&ctx->wardrive_wigle_files[index];
 char capture[64]={0},ssid[64]={0},reason[128]={0},status[240],file_name[128];
 snprintf(file_name,sizeof(file_name),"%s",file->name);
 int matched=app_wpa_capture_for_file_js(tab,file->path,capture,sizeof(capture),
                                          ssid,sizeof(ssid),reason,sizeof(reason));
 if(matched<=0){
  if(matched<0)snprintf(status,sizeof(status),
    "%s needs investigation: %s. Inspect the offline WPA Auditor catalog.",file_name,reason);
  else snprintf(status,sizeof(status),
    "No matching synthetic WPA capture for %s. Inspect the offline WPA Auditor catalog.",file_name);
  app_wpa_handshake_status(ctx,tab,status);return;
 }
 emu_show(0,TAB_INTERNAL);
 snprintf(app_wpa_notice,sizeof(app_wpa_notice),
          "CRACK %s: %s is ready in the synthetic catalog.",file_name,capture);
 app_wpa_show();
}
