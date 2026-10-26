/* Global Portal and Observer Probes & Karma UI for the local scenario only.
 * This file creates LVGL controls but no AP, DNS, HTTP server, radio or UART. */
typedef struct {
 int tab,job,mode,probe_count,template_count;
 lv_obj_t *overlay,*card,*status,*ssid_input,*template_dropdown,*keyboard;
 char network_id[64],ssid[33],template_paths[12][192],error[192];
} app_portal_flow_t;
static app_portal_flow_t app_portal_flows[4];

static void app_portal_flow_render(app_portal_flow_t *flow);
static void app_portal_flow_close(app_portal_flow_t *flow) {
 if(!flow||!flow->overlay)return;
 lv_obj_t *overlay=flow->overlay;
 flow->overlay=flow->card=flow->status=flow->ssid_input=flow->template_dropdown=flow->keyboard=NULL;
 if(lv_obj_is_valid(overlay))lv_obj_del(overlay);
}
static lv_obj_t *app_portal_flow_label(lv_obj_t *parent,const char *message,const lv_font_t *font,lv_color_t color) {
 lv_obj_t *label=lv_label_create(parent);lv_label_set_text(label,message);
 lv_obj_set_width(label,lv_pct(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
 lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,color,0);
 return label;
}
static lv_obj_t *app_portal_flow_button(lv_obj_t *parent,const char *title,const char *binding,lv_event_cb_t callback,app_portal_flow_t *flow) {
 lv_obj_t *button=lv_btn_create(parent);lv_obj_set_size(button,LV_SIZE_CONTENT,44);
 lv_obj_set_style_min_width(button,110,0);lv_obj_set_style_pad_hor(button,14,0);
 lv_obj_set_style_bg_color(button,COLOR_MATERIAL_ORANGE,0);
 lv_obj_set_style_radius(button,9,0);lv_obj_set_style_shadow_width(button,0,0);
 app_bind_adapter(button,callback,LV_EVENT_CLICKED,flow,binding);
 lv_obj_t *label=lv_label_create(button);lv_label_set_text(label,title);
 lv_obj_set_style_text_font(label,&lv_font_montserrat_14,0);
 lv_obj_set_style_text_color(label,lv_color_black(),0);lv_obj_center(label);
 return button;
}
static lv_obj_t *app_portal_flow_actions(lv_obj_t *parent) {
 lv_obj_t *row=lv_obj_create(parent);lv_obj_set_width(row,lv_pct(100));
 lv_obj_set_height(row,LV_SIZE_CONTENT);lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(row,0,0);lv_obj_set_style_pad_all(row,0,0);
 lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW_WRAP);lv_obj_set_style_pad_gap(row,10,0);
 lv_obj_clear_flag(row,LV_OBJ_FLAG_SCROLLABLE);return row;
}
static void app_portal_flow_cancel_cb(lv_event_t *event) {
 app_portal_flow_close(lv_event_get_user_data(event));
}
static void app_portal_flow_background_cb(lv_event_t *event) {
 app_portal_flow_close(lv_event_get_user_data(event));
}
static void app_portal_flow_stop_cb(lv_event_t *event) {
 app_portal_flow_t *flow=lv_event_get_user_data(event);
 if(flow->job)app_rem_stop(flow->job);
 flow->job=0;app_portal_flow_close(flow);
}
static void app_portal_flow_input_cb(lv_event_t *event) {
 app_portal_flow_t *flow=lv_event_get_user_data(event);
 if(!flow->keyboard)return;
 if(lv_event_get_code(event)==LV_EVENT_FOCUSED||lv_event_get_code(event)==LV_EVENT_CLICKED){
  lv_keyboard_set_textarea(flow->keyboard,flow->ssid_input);
  lv_obj_remove_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
 }else if(lv_event_get_code(event)==LV_EVENT_DEFOCUSED)lv_obj_add_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
}
static void app_portal_flow_keyboard_cb(lv_event_t *event) {
 app_portal_flow_t *flow=lv_event_get_user_data(event);
 if(flow->keyboard&&(lv_event_get_code(event)==LV_EVENT_READY||lv_event_get_code(event)==LV_EVENT_CANCEL))
  lv_obj_add_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
}
EM_JS(int,app_portal_flow_begin_js,(int tab,const char *network,const char *ssid,const char *portal,char *error,int size),{
 try{
  const options={portal:UTF8ToString(portal)},id=UTF8ToString(network);
  if(id)options.networkIds=[id];else options.ssid=UTF8ToString(ssid);
  return emulatorDevice.device.attackStart(emulatorDevice.module(tab),'portal_demo',options);
 }catch(e){stringToUTF8(e.message,error,size);return 0;}
});
static void app_portal_flow_start_cb(lv_event_t *event) {
 app_portal_flow_t *flow=lv_event_get_user_data(event);
 if(flow->job||!flow->template_count)return;
 const char *ssid=flow->mode==0&&flow->ssid_input?lv_textarea_get_text(flow->ssid_input):flow->ssid;
 unsigned selected=flow->template_dropdown?lv_dropdown_get_selected(flow->template_dropdown):0;
 if(selected>=(unsigned)flow->template_count)selected=0;
 flow->error[0]=0;
 int job=app_portal_flow_begin_js(flow->tab,flow->mode?flow->network_id:"",ssid,
                                  flow->template_paths[selected],flow->error,sizeof(flow->error));
 if(!job){if(flow->status)lv_label_set_text(flow->status,flow->error);return;}
 flow->job=job;snprintf(flow->ssid,sizeof(flow->ssid),"%s",ssid);
 app_portal_flow_render(flow);
}
static void app_portal_flow_probe_cb(lv_event_t *event) {
 app_portal_flow_t *flow=&app_portal_flows[tab_id_for_ctx(get_current_ctx())];
 int index=(int)(intptr_t)lv_event_get_user_data(event);
 if(index<0||index>=flow->probe_count)return;
 app_observer_field(flow->tab,index,"id",flow->network_id,sizeof(flow->network_id));
 app_observer_field(flow->tab,index,"ssid",flow->ssid,sizeof(flow->ssid));
 flow->mode=2;app_portal_flow_render(flow);
}
static void app_portal_flow_render(app_portal_flow_t *flow) {
 app_portal_flow_close(flow);
 tab_context_t *ctx=get_ctx_for_tab(flow->tab);
 if(!ctx||!ctx->container)return;
 flow->overlay=lv_obj_create(ctx->container);
 lv_obj_set_size(flow->overlay,lv_pct(100),lv_pct(100));style_modal_overlay(flow->overlay,LV_OPA_70);
 flow->card=lv_obj_create(flow->overlay);lv_obj_set_size(flow->card,600,510);
 lv_obj_center(flow->card);style_popup_card(flow->card,14,COLOR_MATERIAL_ORANGE);
 lv_obj_set_style_pad_all(flow->card,18,0);lv_obj_set_style_pad_row(flow->card,13,0);
 lv_obj_set_flex_flow(flow->card,LV_FLEX_FLOW_COLUMN);
 app_portal_flow_label(flow->card,flow->mode==0?"Offline Portal":"Probes & Karma",
                       &lv_font_montserrat_22,COLOR_MATERIAL_ORANGE);
 app_portal_flow_label(flow->card,"LOCAL SIMULATION  /  No AP, radio traffic, web server or real credentials",
                       &lv_font_montserrat_12,ui_muted_color());
 if(flow->job){
  char message[1024]={0};app_rem_poll(flow->job,message,sizeof(message));
  flow->status=app_portal_flow_label(flow->card,message,&lv_font_montserrat_16,ui_text_color());
  lv_obj_t *actions=app_portal_flow_actions(flow->card);
  app_portal_flow_button(actions,"Background",flow->mode==0?"emu.portal.background":"emu.karma2.background",
                         app_portal_flow_background_cb,flow);
  app_portal_flow_button(actions,"Stop Portal",flow->mode==0?"emu.portal.stop":"emu.karma2.stop",
                         app_portal_flow_stop_cb,flow);
  return;
 }
 if(flow->mode==1){
  app_portal_flow_label(flow->card,"Synthetic probe catalogue from the running Observer",
                        &lv_font_montserrat_14,ui_text_color());
  flow->probe_count=app_observer_network_count(flow->tab);
  if(!flow->probe_count)app_portal_flow_label(flow->card,"No probes found. Start Observer first.",
                                              &lv_font_montserrat_14,COLOR_MATERIAL_AMBER);
  for(int i=0;i<flow->probe_count&&i<12;i++){
   char ssid[64],binding[64],caption[96];
   app_observer_field(flow->tab,i,"ssid",ssid,sizeof(ssid));
   snprintf(caption,sizeof(caption),"%d. %s",i+1,ssid[0]?ssid:"(Hidden)");
   snprintf(binding,sizeof(binding),"emu.karma2.probe.%d",i);
   lv_obj_t *row=lv_btn_create(flow->card);lv_obj_set_width(row,lv_pct(95));
   lv_obj_set_height(row,42);lv_obj_set_style_bg_color(row,ui_card_color(),0);
   app_bind_adapter(row,app_portal_flow_probe_cb,LV_EVENT_CLICKED,(void *)(intptr_t)i,binding);
   lv_obj_t *label=lv_label_create(row);lv_label_set_text(label,caption);lv_obj_center(label);
  }
  app_portal_flow_button(app_portal_flow_actions(flow->card),"Close","emu.karma2.cancel",
                         app_portal_flow_cancel_cb,flow);
  return;
 }
 if(flow->mode==0){
  app_portal_flow_label(flow->card,"Enter SSID:",&lv_font_montserrat_16,ui_text_color());
  flow->ssid_input=lv_textarea_create(flow->card);lv_obj_set_size(flow->ssid_input,lv_pct(90),45);
  lv_textarea_set_one_line(flow->ssid_input,true);lv_textarea_set_max_length(flow->ssid_input,32);
  lv_textarea_set_text(flow->ssid_input,flow->ssid[0]?flow->ssid:"LAB5 Offline Portal");
  app_bind_adapter(flow->ssid_input,app_portal_flow_input_cb,LV_EVENT_ALL,flow,"emu.portal.ssid");
 }else{
  char message[64];snprintf(message,sizeof(message),"SSID: %s",flow->ssid);
  app_portal_flow_label(flow->card,message,&lv_font_montserrat_16,ui_text_color());
 }
 app_portal_flow_label(flow->card,"Select Portal HTML:",&lv_font_montserrat_16,ui_text_color());
 char options[900]={0};flow->template_count=0;flow->error[0]=0;
 for(int i=0;i<12;i++){
  char name[64]={0};
  if(!app_rem_template(flow->tab,i,name,flow->template_paths[i],flow->error))break;
  if(i)strncat(options,"\n",sizeof(options)-strlen(options)-1);
  strncat(options,name,sizeof(options)-strlen(options)-1);flow->template_count++;
 }
 flow->template_dropdown=lv_dropdown_create(flow->card);
 lv_obj_set_width(flow->template_dropdown,lv_pct(90));
 lv_dropdown_set_options(flow->template_dropdown,flow->template_count?options:"No local demo templates");
 flow->status=app_portal_flow_label(flow->card,flow->template_count?
   "All visits and submissions are synthetic fixtures.":flow->error[0]?flow->error:"No local demo templates available.",
   &lv_font_montserrat_14,flow->template_count?ui_muted_color():COLOR_MATERIAL_AMBER);
 lv_obj_t *actions=app_portal_flow_actions(flow->card);
 app_portal_flow_button(actions,"Cancel",flow->mode==0?"emu.portal.cancel":"emu.karma2.cancel",
                        app_portal_flow_cancel_cb,flow);
 if(flow->template_count)app_portal_flow_button(actions,"Start Portal",flow->mode==0?"emu.portal.start":"emu.karma2.start",
                                               app_portal_flow_start_cb,flow);
 if(flow->mode==0){
  flow->keyboard=lv_keyboard_create(flow->overlay);
  lv_obj_set_size(flow->keyboard,lv_pct(100),260);lv_obj_align(flow->keyboard,LV_ALIGN_BOTTOM_MID,0,0);
  style_on_screen_keyboard(flow->keyboard);lv_keyboard_set_textarea(flow->keyboard,flow->ssid_input);
  lv_obj_add_flag(flow->keyboard,LV_OBJ_FLAG_HIDDEN);
  app_bind_adapter(flow->keyboard,app_portal_flow_keyboard_cb,LV_EVENT_ALL,flow,"emu.portal.keyboard");
 }
}
static void app_portal_flow_open(int mode) {
 tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=3)return;
 app_portal_flow_t *flow=&app_portal_flows[tab];
 flow->tab=tab;
 if(!flow->job){flow->mode=mode;flow->network_id[0]=0;
  if(mode==0&&!flow->ssid[0])snprintf(flow->ssid,sizeof(flow->ssid),"LAB5 Offline Portal");}
 app_portal_flow_render(flow);
}
static void app_global_portal_show(void){app_portal_flow_open(0);}
static void observer_karma_btn_cb(lv_event_t *event){(void)event;app_portal_flow_open(1);}
static void app_portal_flow_tick(void) {
 for(int tab=0;tab<3;tab++){
  app_portal_flow_t *flow=&app_portal_flows[tab];
  if(flow->overlay&&!lv_obj_is_valid(flow->overlay)){
   flow->overlay=flow->card=flow->status=flow->ssid_input=flow->template_dropdown=flow->keyboard=NULL;
  }
  if(!flow->job)continue;
  char message[1024]={0};int state=app_rem_poll(flow->job,message,sizeof(message));
  if(flow->status&&lv_obj_is_valid(flow->status))lv_label_set_text(flow->status,message);
  if(state!=1)flow->job=0;
 }
}
