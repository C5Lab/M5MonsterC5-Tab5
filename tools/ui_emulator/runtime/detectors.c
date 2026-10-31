/* Browser-only JanOS detector transport. The retained Tab5 controls write to
 * their captured tab and port; the model keeps scan rows, command replies and
 * selected BSSIDs separate for each simulated module. */

EM_JS(int,app_deauth_read_js,(int tab,char *out,int size),{
 const line=emulatorDevice.detectorRead(tab);
 if(line===null)return 0;
 stringToUTF8(line+'\n',out,size);
 return 1;
});
EM_JS(int,app_deauth_connected_js,(int tab),{
 return emulatorDevice.device.systemStatus(emulatorDevice.module(tab)).connected?1:0;
});

static char app_deauth_tx[4][512];
static size_t app_deauth_tx_len[4];
static char app_deauth_rx[4][1024];
static size_t app_deauth_rx_len[4],app_deauth_rx_pos[4];

/* The browser runs all retained callbacks on one thread. The detector's
 * context and task flags still own its console lease, and the synthetic
 * FreeRTOS mutex below is initialized for each external tab in app_init(). */
static bool janos_console_occupied(tab_id_t tab) {
 if(tab<TAB_GROVE||tab>TAB_MBUS)return true;
 tab_context_t *ctx=get_ctx_for_tab(tab);
 return ctx && ctx->deauth_detector_console_claimed;
}

/* The firmware can write the command and CRLF separately. Commit a full line
 * only at a terminator, preserving the module chosen when the job began. */
static int transport_write_bytes_tab(tab_id_t tab,uart_port_t port,const char *data,size_t len) {
 (void)port;
 int i=(int)tab;
 if(i<0||i>=4||!data)return 0;
 if(i<=TAB_MBUS&&!app_deauth_connected_js(i))return 0;
 for(size_t n=0;n<len;n++){
  char ch=data[n];
  if(ch=='\r'||ch=='\n') {
   if(app_deauth_tx_len[i]) {
    app_deauth_tx[i][app_deauth_tx_len[i]]='\0';
    if(!strcmp(app_deauth_tx[i],"scan_networks"))
     atomic_fetch_add(&detector_scan_epoch[i],1);
    app_config_command(i,app_deauth_tx[i]);
    app_deauth_tx_len[i]=0;
   }
   continue;
  }
  if(app_deauth_tx_len[i]+1>=sizeof(app_deauth_tx[i])){
   app_deauth_tx_len[i]=0;
   emu_unsupported("UART command exceeds emulator buffer");
   return 0;
  }
  app_deauth_tx[i][app_deauth_tx_len[i]++]=ch;
 }
 return (int)len;
}

EM_JS(int, app_deauth_line_js, (int tab, int seq, char *out, int size), {
 return stringToUTF8(emulatorDevice.deauthDetectorLine(tab, seq), out, size);
});
EM_JS(int, app_deauth_pending_js, (int tab), {
 return emulatorDevice.detectorHasPendingObservation(tab) ? 1 : 0;
});

static struct {
 bool active;
 tab_context_t *ctx;
 int seq;
 unsigned run;
 double last_ms;
} app_deauth[4];

/* Reached from the intercepted scan or monitor task. Poll helpers below run
 * the retained state machine without blocking the browser event loop. */
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

/* Drive the real Tab5 scan/start/stop parsers with per-module JanOS lines.
 * Synthetic observations enter the same production parser after start ACK. */
static void app_deauth_tick(void) {
 for(int tab=0;tab<4;tab++) {
  /* Hardware discovery keeps these flags current outside a detector task on
   * Tab5. Mirror that here so reconnect + bound Stop retry is testable. */
  if(tab==TAB_GROVE)grove_detected=app_deauth_connected_js(tab);
  if(tab==TAB_MBUS)mbus_detected=app_deauth_connected_js(tab);
  if(!app_deauth[tab].active)continue;
  tab_context_t *ctx=app_deauth[tab].ctx;
  if(!ctx){app_deauth[tab].active=false;continue;}
  if(ctx->deauth_detector_scan_active)deauth_detector_scan_poll(ctx);
  if(ctx->deauth_detector_task_active)deauth_detector_poll(ctx);
  bool task_released=false;
  if(!ctx->deauth_detector_scan_active){
   task_released=ctx->deauth_detector_scan_task!=NULL;
   ctx->deauth_detector_scan_task=NULL;
   app_deauth_scan_session[tab]=false;
  }
  if(!ctx->deauth_detector_task_active){
   task_released=task_released||ctx->deauth_detector_task!=NULL;
   ctx->deauth_detector_task=NULL;
  }
  if(task_released)deauth_detector_render_status(ctx);
  if(!ctx->deauth_detector_scan_active&&!ctx->deauth_detector_task_active){
   app_deauth[tab].active=false;continue;
  }
  if(!ctx->deauth_detector_running||!ctx->deauth_entries||
     !ctx->deauth_detector_table||!lv_obj_is_valid(ctx->deauth_detector_table))continue;
  /* JanOS publishes Trace armed before it can report the first confirming
   * marker. Do not let the synthetic event source overtake that queued status. */
  if(ctx->detector_watch.trace_requested &&
     (ctx->detector_watch.trace_state==DETECTOR_TRACE_WAITING_ARM ||
      ctx->detector_watch.trace_state==DETECTOR_TRACE_RELEASED))continue;
  if(app_device_ms-app_deauth[tab].last_ms<1200)continue;
  app_deauth[tab].last_ms=app_device_ms;
  char line[256];
  if(app_deauth_line_js(tab,app_deauth[tab].seq++,line,sizeof(line))<=0)continue;
  deauth_detector_process_line(ctx,line);
  /* The bridge queues the triggering V2 record behind its confirming marker.
   * Drain that one pending record in the same tick so the 1000 ms deadline
   * cannot overtake the observation that opened the confirmation window. */
  if(!strncmp(line,"[DETECTOR_TRACE] confirming_bssid=",
              sizeof("[DETECTOR_TRACE] confirming_bssid=")-1) &&
     app_deauth_pending_js(tab) &&
     app_deauth_line_js(tab,app_deauth[tab].seq,line,sizeof(line))>0)
   deauth_detector_process_line(ctx,line);
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
 if(app_deauth_rx_pos[i]>=app_deauth_rx_len[i]){
  app_deauth_rx_pos[i]=app_deauth_rx_len[i]=0;
  if(app_deauth_read_js(i,app_deauth_rx[i],sizeof(app_deauth_rx[i])))
   app_deauth_rx_len[i]=strlen(app_deauth_rx[i]);
 }
 if(app_deauth_rx_len[i]>app_deauth_rx_pos[i]){
  size_t avail=app_deauth_rx_len[i]-app_deauth_rx_pos[i];
  size_t count=len<avail?len:avail;
  memcpy(data,app_deauth_rx[i]+app_deauth_rx_pos[i],count);
  app_deauth_rx_pos[i]+=count;
  return (int)count;
 }
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
 app_hs[tab].target[0]='\0';
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
   if(app_hs_result_js(app_hs[tab].id,ssid,sizeof(ssid))>0){
    snprintf(app_hs[tab].target,sizeof(app_hs[tab].target),"%s",ssid);
    snprintf(msg,sizeof(msg),"Handshake captured: %s",ssid);append_handshaker_log(msg,HS_LOG_SUCCESS);ctx->handshaker_capture_success=true;
   }
   else {app_hs[tab].target[0]='\0';append_handshaker_log("No synthetic handshake available for this selection",HS_LOG_PROGRESS);ctx->handshaker_capture_success=false;}
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
