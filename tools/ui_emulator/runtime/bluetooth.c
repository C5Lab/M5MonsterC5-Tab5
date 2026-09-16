/* Browser-only Bluetooth boundaries. Included after the model bridge in
 * application.c. The menu, scan and locator pages come byte-exact from
 * main/main.c; this file only feeds them from the scenario model.
 *
 * Discovery: the retained show_bt_scan_page() sends "scan_bt" and reads UART with
 * transport_read_bytes() until a "Summary:" line. We stage the model's device
 * text on the command and drain it here, so the production parse_bt_device_line()
 * builds the list. Locator: the retained tracking task is an infinite UART loop,
 * so it is intercepted in xTaskCreate(); app_bt_tick() drives the RSSI label from
 * the model instead. */

EM_JS(int,app_bt_scan_text,(int tab,char *out,int size),{
 return stringToUTF8(emulatorDevice.bluetoothScanText(tab),out,size);
});
EM_JS(int,app_bt_locator_rssi_js,(int tab,const char *mac),{
 return emulatorDevice.bluetoothLocatorRssi(tab,UTF8ToString(mac));
});
/* Guide evidence follows native discovery, locator samples and Back. No UI
 * introspection or synthetic Next clicks are used to advance a story. */
EM_JS(int,app_bt_guide_epoch,(void),{
 return globalThis.emulatorGuideEpoch||0;
});
EM_JS(void,app_bt_guide_event,(int tab,const char *type,const char *mac,int rssi,int epoch),{
 globalThis.dispatchEvent(new CustomEvent('emulator-bluetooth',{detail:{tab,type:UTF8ToString(type),mac:UTF8ToString(mac),rssi,epoch}}));
});
EM_JS(void,app_bt_guide_scan,(int tab,const char *macs,int epoch),{
 globalThis.dispatchEvent(new CustomEvent('emulator-bluetooth',{detail:{tab,type:'scan',devices:UTF8ToString(macs).split(',').filter(Boolean),epoch}}));
});

static char app_bt_scan_buf[4096];
static int app_bt_scan_len, app_bt_scan_read;
/* A deferred Rescan belongs to the guide session that requested it. */
static int app_bt_scan_epoch_override=-1;
static struct {
 bool active,rescan_pending;
 int guide_epoch,rescan_epoch;
 tab_context_t *ctx;
 lv_obj_t *label;
 char mac[18];
 double last_ms;
 bt_device_t devices[BT_MAX_DEVICES];
 int count;
} app_bt_modules[4];

static void app_bt_stage_scan(int tab) {
 app_bt_modules[tab].guide_epoch=app_bt_scan_epoch_override>=0?app_bt_scan_epoch_override:app_bt_guide_epoch();
 app_bt_scan_len=app_bt_scan_text(tab,app_bt_scan_buf,sizeof(app_bt_scan_buf));
 app_bt_scan_read=0;
 /* Keep each displayed list's snapshot: the firmware parser globals are shared
    and another module can replace them before a cached row is clicked. */
 app_bt_modules[tab].count=0;
 char copy[sizeof(app_bt_scan_buf)];memcpy(copy,app_bt_scan_buf,sizeof(copy));
 char *save=NULL;
 for(char *line=strtok_r(copy,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)) {
  int i=app_bt_modules[tab].count;
  if(i<BT_MAX_DEVICES && parse_bt_device_line(line,&app_bt_modules[tab].devices[i]))app_bt_modules[tab].count++;
 }
 char macs[BT_MAX_DEVICES*19+1]="";
 for(int i=0;i<app_bt_modules[tab].count;i++){if(i)strcat(macs,",");strcat(macs,app_bt_modules[tab].devices[i].mac);}
 app_bt_guide_scan(tab,macs,app_bt_modules[tab].guide_epoch);
}

static void bt_scan_rescan_cb(lv_event_t *event) {
 (void)event;int tab=current_tab;
 /* Rebuild on the next tick, after the clicked object's event has returned. */
 app_bt_modules[tab].ctx=get_current_ctx();app_bt_modules[tab].rescan_pending=true;
 app_bt_modules[tab].rescan_epoch=app_bt_guide_epoch();
}
static void bt_scan_device_click_cb(lv_event_t *event) {
 int tab=current_tab,index=(int)(intptr_t)lv_event_get_user_data(event);
 if(index<0||index>=app_bt_modules[tab].count)return;
 bt_device_count=app_bt_modules[tab].count;
 memcpy(bt_devices,app_bt_modules[tab].devices,sizeof(bt_device_t)*bt_device_count);
 show_bt_locator_page(index);
}

/* Retained scan/locator code reads UART through this adapter. Only the staged
 * discovery response is served; other reads report no data, as a silent line
 * would on the device. */
static int transport_read_bytes(uart_port_t port, void *data, size_t len, TickType_t ticks_to_wait) {
 (void)port;(void)ticks_to_wait;
 int avail=app_bt_scan_len-app_bt_scan_read;
 if(avail<=0)return 0;
 int n=(int)len<avail?(int)len:avail;
 memcpy(data,app_bt_scan_buf+app_bt_scan_read,n);
 app_bt_scan_read+=n;
 return n;
}

static void app_bt_locator_start(tab_context_t *ctx) {
 if(!ctx)return;
 int tab=tab_id_for_ctx(ctx);
 app_bt_modules[tab].ctx=ctx;app_bt_modules[tab].label=bt_locator_rssi_label;
 snprintf(app_bt_modules[tab].mac,sizeof(app_bt_modules[tab].mac),"%s",bt_locator_target_mac);
 app_bt_modules[tab].active=true;app_bt_modules[tab].last_ms=0;
 app_bt_guide_event(tab,"locate",app_bt_modules[tab].mac,0,app_bt_modules[tab].guide_epoch);
}
static void app_bt_locator_stop(int tab) {
 app_bt_modules[tab].active=false;
 if(app_bt_modules[tab].ctx)app_bt_modules[tab].ctx->bt_locator_tracking=false;
}
static void bt_locator_tracking_back_btn_event_cb(lv_event_t *event) {
 (void)event;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 app_bt_locator_stop(current_tab);bt_locator_tracking=false;bt_locator_task_handle=NULL;
 if(ctx->bt_locator_page)lv_obj_add_flag(ctx->bt_locator_page,LV_OBJ_FLAG_HIDDEN);
 if(ctx->bt_scan_page){lv_obj_remove_flag(ctx->bt_scan_page,LV_OBJ_FLAG_HIDDEN);ctx->current_visible_page=ctx->bt_scan_page;}
 app_bt_guide_event(current_tab,"back",app_bt_modules[current_tab].mac,0,app_bt_modules[current_tab].guide_epoch);
}

/* Mirror the tracking task's single job: update the RSSI label from the model.
 * The retained back button clears ctx->bt_locator_tracking and sends "stop". */
static void app_bt_tick(void) {
 for(int tab=0;tab<4;tab++) {
  tab_context_t *ctx=app_bt_modules[tab].ctx;
  if(app_bt_modules[tab].rescan_pending && current_tab==tab) {
   app_bt_modules[tab].rescan_pending=false;app_bt_locator_stop(tab);
   if(ctx && ctx->bt_scan_page){lv_obj_del(ctx->bt_scan_page);ctx->bt_scan_page=NULL;}
   app_bt_scan_epoch_override=app_bt_modules[tab].rescan_epoch;
   show_bt_scan_page();
   app_bt_scan_epoch_override=-1;
  }
  if(!app_bt_modules[tab].active)continue;
  if(!ctx||!ctx->bt_locator_tracking){app_bt_modules[tab].active=false;continue;}
  if(app_device_ms-app_bt_modules[tab].last_ms<300)continue;
  app_bt_modules[tab].last_ms=app_device_ms;
  lv_obj_t *label=app_bt_modules[tab].label;
  if(!label||!lv_obj_is_valid(label)){app_bt_locator_stop(tab);continue;}
  int rssi=app_bt_locator_rssi_js(tab,app_bt_modules[tab].mac);
  if(current_tab==tab&&ctx->current_visible_page==ctx->bt_locator_page)app_bt_guide_event(tab,"sample",app_bt_modules[tab].mac,rssi,app_bt_modules[tab].guide_epoch);
  if(rssi<=-100)lv_label_set_text(label,"No signal");
  else lv_label_set_text_fmt(label,"%d dBm",rssi);
  lv_color_t col=rssi>-50?COLOR_MATERIAL_GREEN:rssi>-70?COLOR_MATERIAL_AMBER:COLOR_MATERIAL_RED;
  lv_obj_set_style_text_color(label,col,0);
 }
}
