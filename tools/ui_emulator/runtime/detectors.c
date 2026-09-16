/* Browser-only Deauth Detector boundary. Included after the model bridge in
 * application.c. The page comes byte-exact from main/main.c: the retained
 * show_deauth_detector_page() renders it, and its Start callback sends
 * "deauth_detector" and xTaskCreate()s an infinite UART reader task.
 *
 * That task is intercepted in xTaskCreate() (see application.c); instead of
 * running its UART loop we synthesize one detected frame per interval from the
 * scenario, format it as the firmware's "[DEAUTH] ..." line and let the
 * production parse_deauth_line() build the entry, so the retained
 * update_deauth_table() draws exactly what the device would. Stop/Back clear
 * ctx->deauth_detector_running and the tick deactivates. */

EM_JS(int, app_deauth_line_js, (int tab, int seq, char *out, int size), {
 return stringToUTF8(emulatorDevice.deauthDetectorLine(tab, seq), out, size);
});

static struct {
 bool active;
 tab_context_t *ctx;
 int seq;
 unsigned run;
 double last_ms;
} app_deauth[4];

/* Reached from the intercepted xTaskCreate(deauth_detector_task, ...). The
 * retained start callback has already set ctx->deauth_detector_running. */
static void app_deauth_start(tab_context_t *ctx) {
 if(!ctx)return;
 int tab=tab_id_for_ctx(ctx);
 if(tab<0||tab>=4)return;
 app_deauth[tab].ctx=ctx;
 app_deauth[tab].active=true;
 app_deauth[tab].seq=0;
 app_deauth[tab].run++;
 app_deauth[tab].last_ms=0;
}

/* Mirror the detector task's single job: append one synthesized deauth frame at
 * a steady cadence while running, newest first, capped like the firmware. */
static void app_deauth_tick(void) {
 for(int tab=0;tab<4;tab++) {
  if(!app_deauth[tab].active)continue;
  tab_context_t *ctx=app_deauth[tab].ctx;
  if(!ctx||!ctx->deauth_detector_running){app_deauth[tab].active=false;continue;}
  if(current_tab!=tab)continue;           /* the shared table belongs to the visible module */
  if(!deauth_table||!lv_obj_is_valid(deauth_table))continue;
  if(app_device_ms-app_deauth[tab].last_ms<1200)continue;
  app_deauth[tab].last_ms=app_device_ms;
  char line[256];
  if(app_deauth_line_js(tab,app_deauth[tab].seq++,line,sizeof(line))<=0)continue;
  deauth_entry_t entry;
  if(!parse_deauth_line(line,&entry))continue;
  if(deauth_entry_count<DEAUTH_DETECTOR_MAX_ENTRIES)deauth_entry_count++;
  memmove(&deauth_entries[1],&deauth_entries[0],(deauth_entry_count-1)*sizeof(deauth_entry_t));
  deauth_entries[0]=entry;
  update_deauth_table();
 }
}

/* ==========================================================================
 * Anti-surveillance (follower detection)
 *
 * The retained show_antisurv_page() renders byte-exact. Start sends
 * "start_antisurveillance" and xTaskCreate()s an infinite monitor task, which
 * is intercepted; app_antisurv_tick() flags followers from the scenario and
 * appends the red rows the task would. The retained stop callback sends "stop"
 * then synchronously reads a "Devices seen: D, followers flagged: F" summary
 * through transport_read_bytes_tab(); app_config_command() stages that line on
 * "stop" so the adapter below drains it. No GPS or radio is involved.
 * ======================================================================== */

EM_JS(int, app_antisurv_follower_js, (int tab, int seq, char *out, int size), {
 return stringToUTF8(emulatorDevice.antisurvFollowerLine(tab, seq), out, size);
});

static char app_as_buf[4][256];
static int app_as_len[4], app_as_read[4];
static struct { bool active; tab_context_t *ctx; int seq, followers, devices; unsigned run; double last_ms; } app_as[4];

/* Serves only the staged stop summary; a silent read reports no data. */
static int transport_read_bytes_tab(tab_id_t tab, uart_port_t port, void *data, size_t len, TickType_t ticks_to_wait) {
 (void)port; (void)ticks_to_wait;
 int i=(int)tab; if(i<0||i>=4)return 0;
 int avail=app_as_len[i]-app_as_read[i]; if(avail<=0)return 0;
 int n=(int)len<avail?(int)len:avail;
 memcpy(data, app_as_buf[i]+app_as_read[i], n); app_as_read[i]+=n; return n;
}

static void app_antisurv_start(tab_context_t *ctx) {
 if(!ctx)return; int tab=tab_id_for_ctx(ctx); if(tab<0||tab>=4)return;
 app_as[tab].ctx=ctx; app_as[tab].active=true;
 app_as[tab].run++;
 app_as[tab].seq=0; app_as[tab].followers=0; app_as[tab].devices=0; app_as[tab].last_ms=0;
}

/* Called from app_config_command() when a "stop" command arrives: stage the
 * summary line the retained stop reader expects, then go idle. */
static void app_antisurv_on_stop(int tab) {
 if(tab<0||tab>=4||!app_as[tab].active)return;
 app_as[tab].active=false;
 int n=snprintf(app_as_buf[tab], sizeof(app_as_buf[tab]),
   "Anti-surveillance stopped. Devices seen: %d, followers flagged: %d\n",
   app_as[tab].devices, app_as[tab].followers);
 app_as_len[tab]=n>0?n:0; app_as_read[tab]=0;
}

static void app_antisurv_tick(void) {
 for(int tab=0;tab<4;tab++){
  if(!app_as[tab].active)continue;
  tab_context_t *ctx=app_as[tab].ctx;
  if(!ctx||!ctx->antisurv_monitoring){app_as[tab].active=false;continue;}
  if(current_tab!=tab)continue;
  if(app_device_ms-app_as[tab].last_ms<1500)continue;
  app_as[tab].last_ms=app_device_ms;
  char line[256];
  if(app_antisurv_follower_js(tab,app_as[tab].seq++,line,sizeof(line))<=0)continue;
  char mac[24]="?", name[64]="";
  char *bar=strchr(line,'|');
  if(bar){*bar=0; snprintf(mac,sizeof(mac),"%s",line); snprintf(name,sizeof(name),"%s",bar+1);}
  else snprintf(mac,sizeof(mac),"%s",line);
  app_as[tab].devices++; app_as[tab].followers++;
  ctx->antisurv_follower_count=app_as[tab].followers;
  ctx->antisurv_device_count=app_as[tab].devices;
  if(ctx->antisurv_list && lv_obj_is_valid(ctx->antisurv_list)){
   while(lv_obj_get_child_cnt(ctx->antisurv_list)>60)lv_obj_del(lv_obj_get_child(ctx->antisurv_list,0));
   lv_obj_t *row=lv_obj_create(ctx->antisurv_list);
   lv_obj_set_size(row,lv_pct(100),LV_SIZE_CONTENT);
   lv_obj_set_style_bg_color(row,lv_color_hex(0x3A1020),0);
   lv_obj_set_style_border_color(row,COLOR_MATERIAL_RED,0);
   lv_obj_set_style_border_width(row,1,0);
   lv_obj_set_style_radius(row,6,0);
   lv_obj_set_style_pad_all(row,6,0);
   lv_obj_set_flex_flow(row,LV_FLEX_FLOW_COLUMN);
   lv_obj_clear_flag(row,LV_OBJ_FLAG_SCROLLABLE);
   lv_obj_t *mac_lbl=lv_label_create(row);
   lv_label_set_text_fmt(mac_lbl,"! FOLLOWER ! %s",mac);
   lv_obj_set_style_text_color(mac_lbl,COLOR_MATERIAL_RED,0);
   if(name[0]){lv_obj_t *nl=lv_label_create(row);lv_label_set_text(nl,name);lv_obj_set_style_text_color(nl,lv_color_hex(0xDDDDDD),0);}
   lv_obj_scroll_to_view(row,LV_ANIM_OFF);
  }
  if(ctx->antisurv_count_label && lv_obj_is_valid(ctx->antisurv_count_label)){
   lv_label_set_text_fmt(ctx->antisurv_count_label,"Followers: %d",app_as[tab].followers);
   lv_obj_set_style_text_color(ctx->antisurv_count_label,COLOR_MATERIAL_RED,0);
  }
  if(ctx->antisurv_status_label && lv_obj_is_valid(ctx->antisurv_status_label)){
   lv_label_set_text(ctx->antisurv_status_label,"! FOLLOWER DETECTED !");
   lv_obj_set_style_text_color(ctx->antisurv_status_label,COLOR_MATERIAL_RED,0);
  }
 }
}

/* ==========================================================================
 * Handshaker popup
 *
 * Reached from the "Handshaker" attack action (see attacks_routes.c). The
 * retained show_handshaker_popup() sends "start_handshake" and xTaskCreate()s
 * a monitor task; that task is intercepted and app_handshaker_tick() drives the
 * popup through the retained append_handshaker_log() and handshaker_set_done_state(),
 * ending in a captured handshake for the selected network. No radio is involved.
 * ======================================================================== */

EM_JS(int,app_hs_begin_js,(int tab,const char *targets),{
 try{return emulatorDevice.device.attackStart(emulatorDevice.module(tab),'handshake',{bssids:UTF8ToString(targets).split(',').filter(Boolean)});}catch(e){return 0;}
});
EM_JS(int,app_hs_state_js,(int id),{const j=emulatorDevice.device.job(id);return j?.state==='running'?1:j?.state==='completed'?2:0;});
EM_JS(void,app_hs_stop_js,(int id),{if(id)emulatorDevice.device.attackStop(id);});
EM_JS(int,app_hs_result_js,(int id,char *out,int size),{
 const rows=emulatorDevice.device.job(id)?.result?.handshakes||[];
 stringToUTF8(rows[0]?.ssid||"",out,size);return rows.length;
});
static struct { bool active; tab_context_t *ctx; int step,id,run,stopped; double last_ms; char target[64]; } app_hs[4];
static void app_handshaker_start(tab_context_t *ctx) {
 if(!ctx)return;int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=4)return;
 scan_view_t v=get_scan_view(ctx);char targets[2048]={0};
 for(int k=0;k<v.sel_count;k++){int i=v.sel_indices[k];if(i<0||i>=v.net_count)continue;if(targets[0])strncat(targets,",",sizeof(targets)-strlen(targets)-1);strncat(targets,v.nets[i].bssid,sizeof(targets)-strlen(targets)-1);}
 /* Native no-selection means all discovered networks. */
 if(!v.sel_count)for(int i=0;i<v.net_count;i++){if(targets[0])strncat(targets,",",sizeof(targets)-strlen(targets)-1);strncat(targets,v.nets[i].bssid,sizeof(targets)-strlen(targets)-1);}
 int id=app_hs_begin_js(tab,targets);app_hs[tab].ctx=ctx;
 if(!id){ctx->handshaker_monitoring=false;append_handshaker_log("Unavailable: check connection, selection and busy operation",HS_LOG_PROGRESS);return;}
 app_hs[tab].id=id;app_hs[tab].run=id;app_hs[tab].active=true;app_hs[tab].step=0;app_hs[tab].last_ms=app_device_ms;
 int index=v.sel_count?v.sel_indices[0]:0;
 snprintf(app_hs[tab].target,sizeof(app_hs[tab].target),"%s",index>=0&&index<v.net_count?v.nets[index].ssid:"target");
}
static void app_handshaker_on_stop(int tab){if(tab>=0&&tab<4){if(app_hs[tab].id&&app_hs_state_js(app_hs[tab].id)==1){app_hs_stop_js(app_hs[tab].id);app_hs[tab].stopped=app_hs[tab].run;}app_hs[tab].id=0;app_hs[tab].active=false;}}
static void app_handshaker_tick(void){
 for(int tab=0;tab<4;tab++){
  if(!app_hs[tab].id)continue;
  tab_context_t *ctx=app_hs[tab].ctx;
  if(app_hs_state_js(app_hs[tab].id)!=1){app_hs[tab].active=false;app_hs[tab].id=0;ctx->handshaker_monitoring=false;if(current_tab==tab&&ctx->handshaker_popup)append_handshaker_log("Simulation cancelled or disconnected",HS_LOG_PROGRESS);continue;}
  if(!app_hs[tab].active||current_tab!=tab)continue;
  if(app_device_ms-app_hs[tab].last_ms<800)continue;
  app_hs[tab].last_ms=app_device_ms;int step=app_hs[tab].step++;
  if(step==0)append_handshaker_log("Deauthenticating clients...",HS_LOG_PROGRESS);
  else if(step==1)append_handshaker_log("Waiting for handshake...",HS_LOG_PROGRESS);
  else if(step==2){char msg[128],ssid[64];
   if(app_hs_result_js(app_hs[tab].id,ssid,sizeof(ssid))>0){snprintf(msg,sizeof(msg),"Handshake captured: %s",ssid);append_handshaker_log(msg,HS_LOG_SUCCESS);ctx->handshaker_capture_success=true;}
   else {append_handshaker_log("No synthetic handshake available for this selection",HS_LOG_PROGRESS);ctx->handshaker_capture_success=false;}
  }
  else {handshaker_set_done_state(ctx,ctx->handshaker_capture_success);app_hs[tab].active=false;}
 }
}

/* Adapters for boundaries pulled in by the handshaker path. The emulator has no
 * USB serial and runs no real observer/UART tasks, so preparation is a no-op that
 * reports success and the idle wait returns immediately. */
static bool observer_handshaker_prepare_selection(tab_context_t *ctx, wifi_network_t *target) { (void)ctx;(void)target;return true; }
static void usb_flush_input(uint32_t max_ms) { (void)max_ms; }
static bool wait_for_observer_uart_idle(tab_context_t *ctx, uint32_t timeout_ms) { (void)ctx;(void)timeout_ms;return true; }

/* Firmware's grove-UART send; route it through the same command dispatch as the
 * per-tab and MBus variants so anti-surv/handshaker commands are recognized. */
static void uart_send_command(const char *cmd) { app_config_command(current_tab, cmd); }
