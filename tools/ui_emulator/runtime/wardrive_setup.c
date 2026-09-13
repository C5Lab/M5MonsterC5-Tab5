/* Original Wardrive Setup/GPS UI is retained; only device I/O is substituted. */
static unsigned app_wd_story_applied[4],app_wd_story_gps_samples[4];
static bool wardrive_load_config(tab_context_t *ctx) {
 if(!ctx)return false;if(!ctx->wardrive_config.loaded)wardrive_config_set_defaults(&ctx->wardrive_config);
 ctx->wardrive_config.loaded=true;return true;
}
static bool wardrive_load_gps_module(tab_context_t *ctx){return ctx!=NULL;}
static void save_wd_autoupload_to_nvs(void) {
 app_store("wd_auto",g_wd_autoupload_enabled);app_store("wd_wigle",g_wd_autoupload_wigle);
 app_store("wd_wars",g_wd_autoupload_wdgwars);app_store("wd_archive",g_wd_autoupload_archive);app_store("wd_power",g_wd_autoupload_poweroff);
}
static void wardrive_apply_task(void *arg) {
 tab_context_t *ctx=arg;if(!ctx)return;
 app_wd_story_applied[tab_id_for_ctx(ctx)]++;
 ctx->wardrive_config.loaded=true;ctx->wardrive_setup_gps_dirty=false;
 ctx->wardrive_setup_applying=false;ctx->wardrive_setup_task=NULL;
 if(ctx->wardrive_setup_load_btn)lv_obj_remove_state(ctx->wardrive_setup_load_btn,LV_STATE_DISABLED);
 if(ctx->wardrive_setup_apply_btn)lv_obj_remove_state(ctx->wardrive_setup_apply_btn,LV_STATE_DISABLED);
 if(ctx->wardrive_setup_close_btn)lv_obj_remove_state(ctx->wardrive_setup_close_btn,LV_STATE_DISABLED);
 if(ctx->wardrive_setup_status_label)lv_label_set_text(ctx->wardrive_setup_status_label,"Applied");
}
static void wardrive_gps_debug_send_command(tab_context_t *ctx,const char *cmd) {
 if(ctx&&!strcmp(cmd,"stop")){ctx->wardrive_gps_debug_running=false;ctx->wardrive_gps_debug_task=NULL;wardrive_gps_debug_set_running_controls(ctx,false);}
}
EM_JS(void,app_wd_gps_values,(int tab,double *values),{
 const d=emulatorDevice.device,m=emulatorDevice.module(tab),s=d.wardrive(m),connected=d.snapshot(m).connected;
 HEAPF64[values>>3]=connected&&s.gps.fix?1:0;HEAPF64[(values>>3)+1]=s.gps.lat;
 HEAPF64[(values>>3)+2]=s.gps.lon;HEAPF64[(values>>3)+3]=s.gps.satellites;HEAPF64[(values>>3)+4]=connected?1:0;
});
static void app_wd_gps_tick(tab_context_t *ctx) {
 if(!ctx||!ctx->wardrive_gps_debug_running||!ctx->wardrive_gps_debug_overlay)return;
 app_wd_story_gps_samples[tab_id_for_ctx(ctx)]++;
 double data[5];app_wd_gps_values(tab_id_for_ctx(ctx),data);
 bool fix=data[0]!=0;char text[256];
 lv_label_set_text(ctx->wardrive_gps_debug_fix_label,fix?"Fix: valid":"Fix: waiting");
 lv_label_set_text_fmt(ctx->wardrive_gps_debug_sat_label,"Satellites: %d",(int)data[3]);
 lv_label_set_text(ctx->wardrive_gps_debug_hdop_label,fix?"HDOP: 1.0":"HDOP: -");
 lv_label_set_text(ctx->wardrive_gps_debug_gps_presence_label,data[4]?"GPS: detected":"GPS: not detected");
 lv_label_set_text(ctx->wardrive_gps_debug_antenna_label,data[4]?"Antenna: OK":"Antenna: unknown");
 if(fix)snprintf(text,sizeof(text),"Coordinates: %.6f, %.6f",data[1],data[2]);
 else snprintf(text,sizeof(text),"Coordinates: waiting for fix");
 lv_label_set_text(ctx->wardrive_gps_debug_coord_label,text);
 /* Deterministic NMEA fixture with a valid XOR checksum, never hardware data. */
 double lat=fabs(data[1]),lon=fabs(data[2]);int ld=(int)lat,od=(int)lon;
 char body[180];snprintf(body,sizeof(body),"GPGGA,120000,%02d%07.4f,%c,%03d%07.4f,%c,%d,%02d,1.0,0.0,M,0.0,M,,",ld,(lat-ld)*60,data[1]<0?'S':'N',od,(lon-od)*60,data[2]<0?'W':'E',fix?1:0,(int)data[3]);
 unsigned checksum=0;for(char *p=body;*p;p++)checksum^=(unsigned char)*p;
 snprintf(text,sizeof(text),"$%s*%02X",body,checksum);wardrive_gps_debug_append_line(ctx,text,false);
}
