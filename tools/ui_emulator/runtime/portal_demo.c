/* Offline INTERNAL portal demonstration. No HTTP server or submitted user data. */
static int app_portal_job;
static char app_portal_ids[KARMA2_MAX_PROBES*2][64];
static char app_portal_ssid[33];
EM_JS(int,app_portal_network,(int index,char *id,char *ssid),{
 const n=emulatorDevice.device.nearbyNetworks('internal')[index];if(!n)return 0;
 stringToUTF8(n.id,id,64);stringToUTF8(n.ssid||'(Hidden)',ssid,33);return 1;
});
static void adhoc_fetch_probes_from_all_uarts(void) {
 adhoc_probe_count=0;
 for(int i=0;i<KARMA2_MAX_PROBES*2;i++){
  if(!app_portal_network(i,app_portal_ids[i],adhoc_probes[i]))break;
  adhoc_probe_count++;
 }
}
static void karma2_fetch_html_files(void) {
 karma2_html_count=1;snprintf(karma2_html_files[0],sizeof(karma2_html_files[0]),"Offline demo (synthetic)");
}
EM_JS(int,app_portal_start_js,(const char *network,char *error),{
 try{return emulatorDevice.device.attackStart('internal','portal_demo',{networkIds:[UTF8ToString(network)]});}
 catch(e){stringToUTF8(e.message,error,192);return 0;}
});
static void app_portal_rebuild(void) {
 if(adhoc_portal_page){lv_obj_del(adhoc_portal_page);adhoc_portal_page=NULL;}
 adhoc_portal_data_label=NULL;adhoc_portal_status_label=NULL;karma2_attack_status_label=NULL;
 show_adhoc_portal_page();
 if(!portal_active&&adhoc_portal_status_label)
  lv_label_set_text(adhoc_portal_status_label,"Offline demo: choose a synthetic scenario network in Show Probes.");
}
static void adhoc_html_select_cb(lv_event_t *e) {
 if(adhoc_selected_probe_idx<0||adhoc_selected_probe_idx>=adhoc_probe_count)return;
 char error[192]={0};int id=app_portal_start_js(app_portal_ids[adhoc_selected_probe_idx],error);
 if(!id){
  if(adhoc_html_popup_obj){lv_obj_t *label=lv_label_create(adhoc_html_popup_obj);lv_label_set_text(label,error);}
  return;
 }
 app_portal_job=id;snprintf(app_portal_ssid,sizeof(app_portal_ssid),"%s",adhoc_probes[adhoc_selected_probe_idx]);
 portal_ssid=app_portal_ssid;portal_active=true;portal_started_by_uart=0;
 snprintf(portal_selected_html,sizeof(portal_selected_html),"Offline demo (synthetic)");
 adhoc_html_popup_close_cb(e);app_portal_rebuild();
}
static void adhoc_portal_stop_cb(lv_event_t *e) {
 (void)e;if(app_portal_job)app_rem_stop(app_portal_job);
 app_portal_job=0;portal_active=false;portal_ssid=NULL;app_portal_rebuild();
}
static void app_portal_tick(void) {
 if(!app_portal_job)return;
 char message[1024];int state=app_rem_poll(app_portal_job,message,sizeof(message));
 if(adhoc_portal_data_label&&lv_obj_is_valid(adhoc_portal_data_label))lv_label_set_text(adhoc_portal_data_label,message);
 if(state!=1){app_portal_job=0;portal_active=false;}
}
