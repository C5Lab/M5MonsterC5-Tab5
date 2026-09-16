/* Explicit, cooperative simulator boundaries. This file never opens hardware. */
#include "runtime.h"
static double app_device_ms;
static void app_system_sync_metadata(void);
static void app_system_tick(void);
static void app_dialog_init(void);
static void app_settings_ota_tick(double now);
static void app_sd_admin_tick(void);
static void app_portal_tick(void);
static void app_radar_tick(double ms);
static double app_speed=10;
static int app_restart;
static unsigned app_generation=1;
typedef struct { tab_context_t *ctx; unsigned generation; int model_id,next_row,state; } app_job_t;
static app_job_t app_jobs[4];
static lv_obj_t *app_scan_pages[4];
static int app_min[4]={100,100,100,100},app_max[4]={300,300,300,300};
static bool app_vendor[4]={true,true,true,true};
static uint8_t app_brightness=80;
static bool app_audio_muted;
EM_JS(int,app_model_scan,(int tab),{
 try{return emulatorDevice.scan(tab);}catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));return 0;}
});
EM_JS(int,app_model_cancel,(int id),{return emulatorDevice.cancel(id);});
EM_JS(int,app_model_state,(int id),{return emulatorDevice.state(id);});
EM_JS(void,app_model_advance,(double ms),{emulatorDevice.advance(ms);});
EM_JS(int,app_model_rows,(int tab),{return emulatorDevice.rows(tab).length;});
EM_JS(void,app_model_network,(int tab,int row,char *out,int size),{stringToUTF8(emulatorDevice.rows(tab)[row]||"",out,size);});
EM_JS(int,app_model_select,(int tab,const char *bssid),{
 try{emulatorDevice.select(tab,UTF8ToString(bssid));return 1;}catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));return 0;}
});
EM_JS(int,app_model_capture,(int tab),{
 try{return emulatorDevice.device.capture(emulatorDevice.module(tab));}catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));return 0;}
});
EM_JS(int,app_model_client_count,(int tab),{return emulatorDevice.device.clients(emulatorDevice.module(tab)).length;});
EM_JS(void,app_model_client_mac,(int tab,int index,char *out,int size),{
 stringToUTF8(emulatorDevice.device.clients(emulatorDevice.module(tab))[index]?.mac||"",out,size);
});
EM_JS(void,app_model_error,(int id,char *out,int size),{stringToUTF8(emulatorDevice.device.job(id)?.error||"",out,size);});
EM_JS(void,app_model_capture_file,(int id,char *out,int size),{stringToUTF8(emulatorDevice.device.job(id)?.file||"",out,size);});
/* Mirror the module's virtual SD into the in-memory filesystem so the production
   analysis code opens a real file at the same path the model records. */
EM_JS(int,app_model_sync_files,(int tab),{
 try{
  const bridge=emulatorDevice,module=bridge.module(tab);let written=0;
  for(const entry of bridge.device.files(module)){
   const bytes=bridge.device.readFile(module,entry.path);
   FS.mkdirTree(entry.path.slice(0,entry.path.lastIndexOf('/')));
   FS.writeFile(entry.path,bytes);written++;
  }
  return written;
 }catch(e){dispatchEvent(new CustomEvent('emulator-model-error',{detail:e.message}));return -1;}
});
static void emu_unsupported(const char *name) { emu_unavailable(name); }
static TickType_t xTaskGetTickCount(void) { return (TickType_t)app_device_ms; }
static int64_t esp_timer_get_time(void) { return (int64_t)(app_device_ms*1000); }
/* These delays only separate already synchronous configuration writes. Scan jobs
   never call this adapter: their consumer and reply clock is app_device_ms. */
static void vTaskDelay(TickType_t ticks) { (void)ticks; }
/* Retained tasks run inline here, so "delete the running task" is a return. */
static void vTaskDelete(TaskHandle_t task) { (void)task; }
static void esp_restart(void) { app_restart=(screen_rotation_setting & 3)+1; }
/* One virtual timer slot per tab context. Nothing schedules them: the retained
   popup poll they used to drive is an explicit boundary in this build. */
typedef struct { void *id; void (*cb)(TimerHandle_t); bool running; } app_timer_t;
static app_timer_t app_timers[8];
static int app_timer_count;
static TimerHandle_t xTimerCreate(const char *name,TickType_t period,BaseType_t reload,void *id,void (*cb)(TimerHandle_t)) {
 (void)name;(void)period;(void)reload;
 if(app_timer_count>=8){emu_unsupported("Virtual timer capacity exceeded");return NULL;}
 app_timers[app_timer_count].id=id;app_timers[app_timer_count].cb=cb;app_timers[app_timer_count].running=false;
 return &app_timers[app_timer_count++];
}
static BaseType_t xTimerStop(TimerHandle_t timer,TickType_t wait) {
 (void)wait;if(!timer)return pdFALSE;((app_timer_t *)timer)->running=false;return pdPASS;
}
static void vTimerSetTimerID(TimerHandle_t timer,void *id) { if(timer)((app_timer_t *)timer)->id=id; }
EMSCRIPTEN_KEEPALIVE int emu_restart_requested(void) { return app_restart; }
EM_JS(int,app_store,(const char *key,int value),{
 try { emulatorSettings.set(UTF8ToString(key),value); return 0; } catch(e) { return -1; }
});
EM_JS(int,app_load,(const char *key,int fallback),{
 return emulatorSettings.get(UTF8ToString(key),fallback);
});
static int app_load_range(const char *key,int fallback,int min,int max) {int value=app_load(key,fallback);return value<min||value>max?fallback:value;}
static void save_janos_ft_baud_to_nvs(uint32_t rate) {app_store(NVS_KEY_FT_BAUD,(int)rate);}
static char app_nvs_key[16][64]; static uint8_t app_nvs_value[16]; static int app_nvs_count;
static int nvs_open(const char *ns,int mode,nvs_handle_t *h) { (void)ns;(void)mode; if(!h)return ESP_FAIL; *h=1;app_nvs_count=0;return ESP_OK; }
static int nvs_set_u8(nvs_handle_t h,const char *key,uint8_t val) { if(h!=1 || app_nvs_count>=16)return ESP_FAIL; snprintf(app_nvs_key[app_nvs_count],64,"%s",key);app_nvs_value[app_nvs_count++]=val;return ESP_OK; }
static int nvs_commit(nvs_handle_t h) { if(h!=1)return ESP_FAIL;for(int i=0;i<app_nvs_count;i++)if(app_store(app_nvs_key[i],app_nvs_value[i])){emu_unsupported("Virtual settings persistence failed");return ESP_FAIL;}return ESP_OK; }
static void nvs_close(nvs_handle_t h) {(void)h;app_nvs_count=0;}
static void app_bt_stage_scan(int tab);
static void app_bt_locator_stop(int tab);
static void app_bt_locator_start(tab_context_t *ctx);
static void app_bt_tick(void);
static void app_deauth_start(tab_context_t *ctx);
static void app_deauth_tick(void);
static void app_antisurv_start(tab_context_t *ctx);
static void app_antisurv_tick(void);
static void app_antisurv_on_stop(int tab);
static void app_handshaker_start(tab_context_t *ctx);
static void app_handshaker_tick(void);
static void app_handshaker_on_stop(int tab);
static void app_sae_stop(int tab);
static void app_sae_tick(void);
static void app_global_stop(int tab);
static void app_global_tick(void);
static void app_wardrive_tick(void);
static void app_wd_upload_tick(double now);
static void app_config_command(int tab,const char *cmd) {
 int value;if(sscanf(cmd,"channel_time set min %d",&value)==1)app_min[tab]=value;
 else if(sscanf(cmd,"channel_time set max %d",&value)==1)app_max[tab]=value;
 else if(strcmp(cmd,"scan_bt")==0)app_bt_stage_scan(tab);
 else if(strncmp(cmd,"scan_bt ",8)==0){/* locator start is handled in xTaskCreate */}
 else if(strcmp(cmd,"stop")==0){app_bt_locator_stop(tab);app_antisurv_on_stop(tab);app_handshaker_on_stop(tab);app_sae_stop(tab);app_global_stop(tab);}
 else if(strcmp(cmd,"deauth_detector")==0){/* detection driven from xTaskCreate + app_deauth_tick */}
 else if(strcmp(cmd,"start_antisurveillance")==0){/* driven from xTaskCreate + app_antisurv_tick */}
 else if(strncmp(cmd,"set_antisurv_sensitivity",24)==0){/* sensitivity is UI-only in the emulator */}
 else if(strcmp(cmd,"start_handshake")==0){/* driven from xTaskCreate + app_handshaker_tick */}
 else if(!strcmp(cmd,"start_blackout")||!strcmp(cmd,"start_sniffer_dog")||!strcmp(cmd,"unselect_networks")){/* global confirmation owns the offline job; no hardware I/O */}
 else if(strncmp(cmd,"select_networks",15)==0){/* selection is implicit in the model */}
 else if(cmd[0]!='\r' && cmd[0]!='\n')emu_unsupported(cmd);
}
static int uart_flush(uart_port_t port) {(void)port;return ESP_OK;}
static int uart_write_bytes(uart_port_t port,const void *bytes,size_t count) {char cmd[128];if(count>=sizeof(cmd))return -1;memcpy(cmd,bytes,count);cmd[count]=0;app_config_command(port==UART_NUM_2?TAB_MBUS:TAB_GROVE,cmd);return count;}
static void uart2_send_command(const char *cmd) {app_config_command(TAB_MBUS,cmd);}
static void uart_send_command_for_tab(const char *cmd) {app_config_command(current_tab,cmd);}
static int read_channel_time_from_uart(uart_port_t port,const char *param) {int tab=port==UART_NUM_2?TAB_MBUS:TAB_GROVE;return strcmp(param,"min")==0?app_min[tab]:app_max[tab];}
static bool scan_setup_vendor_read_from_target(tab_id_t tab,uart_port_t port,bool *enabled) {(void)port;if(!enabled)return false;*enabled=app_vendor[tab];return true;}
static bool scan_setup_vendor_set_on_target(tab_id_t tab,uart_port_t port,bool enabled) {(void)port;app_vendor[tab]=enabled;return true;}
static void cancel_inspect_task(tab_context_t *ctx) {if(ctx){ctx->inspect_active=false;ctx->inspect_task=NULL;}}
static void app_scan_page_deleted(lv_event_t *event) {
 tab_context_t *ctx=lv_event_get_user_data(event);int tab=tab_id_for_ctx(ctx);
 if(app_jobs[tab].state==1){app_model_cancel(app_jobs[tab].model_id);app_jobs[tab].state=3;ctx->scan_in_progress=false;if(current_tab==tab)hide_scan_overlay();}
 app_jobs[tab].generation=0;app_scan_pages[tab]=NULL;
}
static void wifi_scan_task(void *arg) {
 int tab=(int)(uintptr_t)arg; if(tab<0 || tab>3)return;
 app_jobs[tab]=(app_job_t){.ctx=get_ctx_for_tab(tab),.generation=app_generation,.state=1};
 app_jobs[tab].model_id=app_model_scan(tab);
 /* Keep a rejected start pending until the next tick can dismiss the modal
    overlay safely, outside the production click callback. */
 app_jobs[tab].ctx->network_count=0;
 if(app_jobs[tab].ctx->scan_page && app_scan_pages[tab]!=app_jobs[tab].ctx->scan_page){app_scan_pages[tab]=app_jobs[tab].ctx->scan_page;(lv_obj_add_event_cb)(app_scan_pages[tab],app_scan_page_deleted,LV_EVENT_DELETE,app_jobs[tab].ctx);}
}
static void *app_pending_load;
static pcap_viewer_state_t *app_pending_artifact[4];
static void app_pcap_artifact_run(pcap_viewer_state_t *state);
static void app_pcap_files_tick(double now);
static void app_gitm_tick(void);
static void app_wpasec_tick(double now);
static void app_nettools_nmap_iot_tick(void);
static void app_attacks_deauth_arp_tick(void);
static void app_attacks_karma_beacon_tick(void);
static void app_rogue_evil_mitm_tick(void);
static BaseType_t xTaskCreate(TaskFunction_t fn,const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle) {
 (void)stack;(void)priority;
 /* The two tasks with an explicit simulator implementation run inline on the
    browser thread; anything else stays an advertised unsupported boundary. */
 if(fn==wifi_scan_task){wifi_scan_task(arg);if(handle)*handle=&app_jobs[(int)(uintptr_t)arg];return pdPASS;}
 if(fn==popup_focus_task){if(handle)*handle=NULL;popup_focus_task(arg);return pdPASS;}
 /* The analysis task must not run inside the click that started it: the retained
    loader replaces the page the event target lives on. Run it on the next tick,
    which is what a real task does. */
 if(fn==pcap_viewer_load_task){if(handle)*handle=NULL;app_pending_load=arg;return pdPASS;}
 if(fn==pcap_viewer_artifact_task){pcap_viewer_state_t *state=arg;app_pending_artifact[tab_id_for_ctx(state->ctx)]=state;if(handle)*handle=NULL;return pdPASS;}
 if(fn==compromised_files_load_task||fn==compromised_cleanup_task||fn==wardrive_cleanup_task||fn==wardrive_fix_task){fn(arg);if(handle)*handle=NULL;return pdPASS;}
 /* The BT locator task is an infinite UART loop; drive its RSSI label from
    app_bt_tick() instead of running the body. */
 if(fn==bt_locator_tracking_task){app_bt_locator_start((tab_context_t *)arg);if(handle)*handle=NULL;return pdPASS;}
 /* The deauth detector task is an infinite UART loop; drive its table from
    app_deauth_tick() instead of running the body. */
 if(fn==deauth_detector_task){app_deauth_start((tab_context_t *)arg);if(handle)*handle=NULL;return pdPASS;}
 /* Anti-surveillance and handshaker monitor tasks are infinite UART loops;
    drive their UI from app_antisurv_tick()/app_handshaker_tick() instead. */
 if(fn==antisurv_monitor_task){app_antisurv_start((tab_context_t *)arg);if(handle)*handle=NULL;return pdPASS;}
 if(fn==handshaker_monitor_task){app_handshaker_start((tab_context_t *)arg);if(handle)*handle=NULL;return pdPASS;}
 if(fn==global_handshaker_monitor_task){if(handle)*handle=NULL;return pdPASS;}
 if(fn==wardrive_apply_task){wardrive_apply_task(arg);if(handle)*handle=NULL;return pdPASS;}
 if(fn==wardrive_gps_debug_task){if(handle)*handle=NULL;return pdPASS;}
 if(fn==wardrive_upload_menu_load_task){wardrive_upload_menu_load_task(arg);if(handle)*handle=NULL;return pdPASS;}
 if(fn==gitm_attach_task||fn==gitm_scan_task||fn==gitm_connect_task||fn==gitm_session_task||fn==gitm_exit_wait_task||fn==wpasec_upload_task){fn(arg);if(handle)*handle=NULL;return pdPASS;}
 emu_unsupported(name);if(handle)*handle=NULL;return pdFALSE;
}
static void app_run_pending_task(void) {
 void *arg=app_pending_load;app_pending_load=NULL;if(arg)pcap_viewer_load_task(arg);
 for(int tab=0;tab<4;tab++){pcap_viewer_state_t *state=app_pending_artifact[tab];if(state){app_pending_artifact[tab]=NULL;app_pcap_artifact_run(state);}}
}
static void app_scan_finish(int tab,bool cancelled) {
 app_job_t *job=&app_jobs[tab];tab_context_t *ctx=job->ctx;
 if(!ctx)return;job->state=cancelled?3:2;ctx->scan_in_progress=false;
 if(ctx->scan_btn)lv_obj_remove_state(ctx->scan_btn,LV_STATE_DISABLED);
 if(ctx->spinner)lv_obj_add_flag(ctx->spinner,LV_OBJ_FLAG_HIDDEN);
 if(current_tab==tab)hide_scan_overlay();
 if(cancelled){ctx->network_count=0; if(ctx->network_list)lv_obj_clean(ctx->network_list);}
 else app_scan_rows(ctx);
 if(ctx->scan_status_label){if(cancelled)lv_label_set_text(ctx->scan_status_label,"Simulator: scan cancelled");else lv_label_set_text_fmt(ctx->scan_status_label,"Found %d networks",ctx->network_count);}
}
EMSCRIPTEN_KEEPALIVE int emu_scan_cancel(int tab) {if(tab<0||tab>3||app_jobs[tab].state!=1)return 0;app_model_cancel(app_jobs[tab].model_id);app_scan_finish(tab,true);return 1;}
EM_JS(int,app_model_busy,(void),{return [0,2,3].some(tab=>emulatorDevice.snapshot(tab).active!==null);});
EMSCRIPTEN_KEEPALIVE int emu_set_timing(int realistic) {if(app_model_busy())return 0;app_speed=realistic?1:10;return 1;}
EMSCRIPTEN_KEEPALIVE int emu_scan_state(int tab) {return tab>=0&&tab<4?app_jobs[tab].state:-1;}
EMSCRIPTEN_KEEPALIVE int emu_network_count(int tab) {return tab>=0&&tab<4?get_ctx_for_tab(tab)->network_count:-1;}
EMSCRIPTEN_KEEPALIVE int emu_selected_count(int tab) {return tab>=0&&tab<4?get_ctx_for_tab(tab)->selected_count:-1;}
EMSCRIPTEN_KEEPALIVE int emu_current_tab(void) {return current_tab;}
EMSCRIPTEN_KEEPALIVE void emu_show(int page,int tab) {
 if(tab!=TAB_GROVE&&tab!=TAB_MBUS&&tab!=TAB_INTERNAL)return;
 lv_obj_t *old=get_current_tab_container();if(old)lv_obj_add_flag(old,LV_OBJ_FLAG_HIDDEN);
 current_tab=tab;lv_obj_t *container=get_current_tab_container();if(container)lv_obj_remove_flag(container,LV_OBJ_FLAG_HIDDEN);update_tab_styles();
 if(page==1){switch_to_internal_settings_page();}
 else if(page==2)show_scan_page();else show_main_tiles();
}
static void init_tab_context(tab_context_t *ctx) { if(!ctx->networks)ctx->networks=calloc(MAX_NETWORKS,sizeof(wifi_network_t));if(!ctx->networks)abort();ctx->sd_card_present=true;ctx->has_subghz=false; }
static void trigger_home_meta_refresh(tab_context_t *ctx,bool force) {(void)force;if(ctx)app_system_sync_metadata();}
static uint32_t home_quote_rand_u32(void) {static uint32_t seed=0xC5AB1234;seed=seed*1664525u+1013904223u;return seed;}
static bool usb_gps_get_fix(double *lat,double *lon,uint32_t wait) {(void)wait;if(lat)*lat=52.23;if(lon)*lon=21.01;return true;}
static void set_brightness_gamma(uint8_t percent) {app_brightness=percent;}
EMSCRIPTEN_KEEPALIVE int emu_brightness(void) {return app_brightness;}
static void audio_silence_for_restart(void) {app_audio_muted=true;}
static esp_err_t ina226_init(void) {ina226_initialized=true;return ESP_OK;}
static void update_battery_status(void) {current_battery_voltage=4.0f;if(battery_voltage_label)lv_label_set_text(battery_voltage_label,"4.00V (SIM)");}
static void battery_status_timer_cb(lv_timer_t *timer) {(void)timer;update_battery_status();}
void app_init(void) {
 janos_ft_baud=JANOS_FT_BAUD_CHOICES[janos_ft_baud_index((uint32_t)app_load(NVS_KEY_FT_BAUD,JANOS_FT_BAUD_DEFAULT))];
 rx8130_present=true;
 clock_24h=app_load_range("clock_24h",1,0,1);clock_show=app_load_range("clock_show",1,0,1);clock_dst=app_load_range("clock_dst",0,0,1);
 grove_detected=true;uart1_detected=true;mbus_detected=true;uart2_initialized=true;internal_sd_present=true;usb_detected=false;
 enable_red_team=app_load_range(NVS_KEY_RED_TEAM,1,0,1);dark_mode_enabled=app_load_range(NVS_KEY_DARK_MODE,1,0,1);dashboard_enabled=app_load_range(NVS_KEY_DASHBOARD,1,0,1);
 screen_rotation_setting=lv_display_get_rotation(NULL);screen_brightness_setting=app_load_range(NVS_KEY_SCREEN_BRIGHT,80,1,100);screen_timeout_setting=app_load_range(NVS_KEY_SCREEN_TIMEOUT,4,0,4);
 boot_sound_mode=(boot_sound_mode_t)app_load_range(NVS_KEY_BOOT_SOUND,BOOT_SOUND_MODE_NOKIA,0,BOOT_SOUND_MODE_STAR_WARS);
 alert_sound_enabled=app_load_range(NVS_KEY_ALERT_SOUND,1,0,1);
 set_brightness_gamma(screen_brightness_setting);
 init_tab_context(&grove_ctx);init_tab_context(&mbus_ctx);init_tab_context(&internal_ctx);init_tab_context(&usb_ctx);
 create_tab_containers();grove_ctx.container=grove_container;mbus_ctx.container=mbus_container;internal_ctx.container=internal_container;
 show_main_tiles();update_tab_styles();update_battery_status();app_dialog_init();
}
static void app_observer_tick(void);
void app_tick(double elapsed_ms) {
 update_status_clock();
 if(elapsed_ms<0)return;double delta=elapsed_ms*app_speed;app_device_ms+=delta;
 app_model_advance(delta);
 app_run_pending_task();
 app_observer_tick();
 app_bt_tick();
 app_deauth_tick();
 app_antisurv_tick();
 app_handshaker_tick();
 app_sae_tick();
 app_global_tick();
 app_wardrive_tick();
 app_wd_upload_tick(app_device_ms);
 app_pcap_files_tick(app_device_ms);
 app_gitm_tick();
 app_wpasec_tick(app_device_ms);
 app_nettools_nmap_iot_tick();
 app_attacks_deauth_arp_tick();
 app_attacks_karma_beacon_tick();
 app_rogue_evil_mitm_tick();
 app_system_tick();
 app_settings_ota_tick(app_device_ms);
 app_sd_admin_tick();
 app_portal_tick();
 app_radar_tick(elapsed_ms);
 for(int tab=0;tab<4;tab++) {
  app_job_t *job=&app_jobs[tab];if(job->state!=1)continue;
  if(job->generation!=app_generation){app_model_cancel(job->model_id);app_scan_finish(tab,true);continue;}
  int state=app_model_state(job->model_id);
  if(state==3||state==4||state==0){
   app_scan_finish(tab,true);
   if(state!=3){job->state=4;if(job->ctx->scan_status_label)lv_label_set_text(job->ctx->scan_status_label,"Simulator: scan could not start or failed");}
   continue;
  }
  while(job->next_row<MAX_NETWORKS && job->next_row<app_model_rows(tab)) {
   int row=job->next_row++;char line[256];app_model_network(tab,row,line,sizeof(line));
   tab_context_t *ctx=job->ctx;wifi_network_t *net=&ctx->networks[ctx->network_count];memset(net,0,sizeof(*net));if(parse_network_line(line,net))ctx->network_count++;
  }
  if(state==2)app_scan_finish(tab,false);
 }
}
/* Registration metadata wraps registration only; original callback, event
   filter, and user data are passed unmodified to LVGL. */
#undef lv_obj_add_event_cb
static struct {lv_obj_t *object;char id[320];unsigned generation;} app_bindings[1024];
static unsigned app_binding_generation;
static bool app_descendant(lv_obj_t *obj,lv_obj_t *parent) {if(!parent)return false;for(;obj;obj=lv_obj_get_parent(obj))if(obj==parent)return true;return false;}
static void app_binding_delete(lv_event_t *event) {lv_obj_t *object=lv_event_get_target(event);for(int i=0;i<1024;i++)if(app_bindings[i].object==object)app_bindings[i].object=NULL;}
/* Hand-adapted constructors retain enrolled identities across firmware line
   shifts. Generated production registrations can still use their live line. */
static int app_template_line(const char *id) {
 for(unsigned i=0;i<sizeof(app_templates)/sizeof(app_templates[0]);i++)
  if(strcmp(app_templates[i].id,id)==0)return app_templates[i].line;
 return -1; /* app_bind reports an unenrolled registration rather than guessing. */
}
static lv_event_dsc_t *app_bind(lv_obj_t *obj,lv_event_cb_t cb,lv_event_code_t event,void *data,int line) {
 const char *tid=NULL;for(unsigned i=0;i<sizeof(app_templates)/sizeof(app_templates[0]);i++)if(app_templates[i].line==line){tid=app_templates[i].id;break;}
 if(!tid){emu_unsupported("Unenrolled production event registration");return NULL;}
 /* Destructors belong to each object, not to the interactive control registry.
    Repeated map edges must all release their own point buffers. */
 if(event==LV_EVENT_DELETE)return lv_obj_add_event_cb(obj,cb,event,data);
 tab_context_t *ctx=get_current_ctx();for(int tab=0;tab<4;tab++){tab_context_t *candidate=get_ctx_for_tab(tab);if(app_descendant(obj,candidate->container)){ctx=candidate;break;}}
 const char *screen="home",*entity="static",*slot="control";char entity_key[160];
 if(app_descendant(obj,ctx->scan_page))screen="scan";
 if(app_descendant(obj,ctx->observer_page)||app_descendant(obj,ctx->network_popup))screen="observer";
 if(app_descendant(obj,internal_settings_page)||current_tab==TAB_INTERNAL)screen="settings";
 if(app_descendant(obj,ctx->pcap_viewer_page))screen="analysis";
 /* File pages share constructors but remain alive when hidden. Their controls
    must be distinct so a hidden page cannot reserve another page's Back ID. */
 if(app_descendant(obj,ctx->handshakes_page))screen="handshakes";
 if(app_descendant(obj,ctx->pcap_captures_page))screen="pcap-captures";
 if(app_descendant(obj,ctx->wardrive_files_page))screen="wardrive-files";
 if(app_descendant(obj,ctx->evil_twin_passwords_page))screen="saved-passwords";
 if(app_descendant(obj,ctx->portal_data_page))screen="portal-data";
 if(app_descendant(obj,ctx->espshark_page))screen="espshark";
 if(app_descendant(obj,ctx->bt_scan_page)||app_descendant(obj,ctx->bt_locator_page))screen="bluetooth";
 if(app_descendant(obj,ctx->wardrive_page)||app_descendant(obj,ctx->wardrive_wigle_popup_overlay))screen="wardrive";
 if(app_descendant(obj,ctx->nmap_page))screen="nmap";
 if(app_descendant(obj,ctx->iot_page))screen="iot";
 if(ctx->gitm&&app_descendant(obj,ctx->gitm->page))screen="gitm";
 if(app_descendant(obj,ctx->wpasec_popup_overlay))screen="wpasec";
 if(app_descendant(obj,ctx->arp_poison_page))screen="arp";
 if(app_descendant(obj,ctx->mitm_popup_overlay))screen="mitm";
 if(app_descendant(obj,ctx->evil_twin_overlay))screen="evil-twin";
 if(app_descendant(obj,ctx->rogue_ap_page)||app_descendant(obj,ctx->rogue_ap_popup_overlay))screen="rogue-ap";
 if(app_descendant(obj,ctx->karma_page))screen="karma";
 if(app_descendant(obj,ctx->beacon_spam_page)||app_descendant(obj,ctx->beacon_ssids_page))screen="beacon";
 if(cb==ota_ta_focus_cb){entity=lv_textarea_get_placeholder_text(obj);slot="field";screen="ota";}
 if(cb==arp_host_click_cb){int i=(int)(intptr_t)data;if(ctx->arp_hosts&&i>=0&&i<ctx->arp_host_count)entity=ctx->arp_hosts[i].ip;slot="host";}
 if(cb==beacon_ssids_delete_cb){snprintf(entity_key,sizeof(entity_key),"ssid-%u",(unsigned)(uintptr_t)data);entity=entity_key;slot="delete";}
 if(cb==nmap_host_click_cb){int i=(int)(intptr_t)data;if(i>=0&&i<ctx->nmap_host_count)entity=ctx->nmap_hosts[i].ip;slot="host";}
 if(cb==adhoc_probe_click_cb){snprintf(entity_key,sizeof(entity_key),"probe-%u",(unsigned)(uintptr_t)data);entity=entity_key;slot="portal-demo";}
 if(cb==time_show_switch_cb)slot="clock-show";
 if(cb==time_24h_switch_cb)slot="clock-format";
 if(cb==time_dst_switch_cb)slot="clock-dst";
 if(cb==wardrive_files_select_uploaded_cb)slot="select-uploaded";
 if(cb==wardrive_files_select_all_cb)slot="select-all";
 if(cb==wardrive_files_select_none_cb)slot="select-none";
 if(cb==nmap_scan_type_cb){if(data)entity=data;slot="scan-level";}
 if(cb==iot_recon_pan_click_cb){iot_recon_pan_t *pan=data;if(pan)entity=pan->pan_id;slot="pan";}
 if(cb==iot_recon_node_click_cb){iot_recon_node_t *node=data;if(node){snprintf(entity_key,sizeof(entity_key),"%s:%s",node->pan_id,node->addr);entity=entity_key;}slot="node";}
 if(cb==gitm_net_row_cb){int i=(int)(intptr_t)data;if(ctx->gitm&&i>=0&&i<ctx->gitm->net_count)entity=ctx->gitm->nets[i].bssid;slot="network";}
 if(cb==gitm_toggle_section_cb){snprintf(entity_key,sizeof(entity_key),"%lu",(unsigned long)(uintptr_t)data);entity=entity_key;slot="section";}
 if(cb==gitm_focus_cb){lv_obj_t *row=lv_obj_get_parent(obj);lv_obj_t *label=row?lv_obj_get_child(row,0):NULL;if(label&&lv_obj_check_type(label,&lv_label_class)){snprintf(entity_key,sizeof(entity_key),"%s:%s",ctx->gitm&&app_descendant(obj,ctx->gitm->s1_content)?"upstream":"ap",lv_label_get_text(label));entity=entity_key;}slot="field";}
 if(cb==gitm_scan_cb)slot="scan";
 if(cb==gitm_connect_cb)slot="connect";
 if(cb==gitm_start_cb)slot="start";
 if(cb==gitm_stop_cb)slot="stop";
 if(cb==gitm_copy_cb)slot="copy";
 if(cb==ota_slot_activate_cb){entity=data?(const char *)data:"slot";slot="activate";}
 if(cb==network_checkbox_event_cb){int i=(int)(intptr_t)data;if(i>=0&&i<ctx->network_count)entity=ctx->networks[i].bssid;slot="select";}
 else if(cb==network_item_event_cb){int index=lv_obj_get_index(obj);if(index>=0&&index<ctx->network_count)entity=ctx->networks[index].bssid;slot="toggle";}
 else if(cb==pcap_viewer_file_open_cb){pcap_viewer_file_t *file=data;if(file)entity=file->path;slot="open";}
 else if(cb==wardrive_wigle_file_checkbox_cb){wardrive_wigle_file_t *file=data;if(file)entity=file->path;slot="select";}
 else if(cb==home_mgmt_delete_cb){int i=(int)(intptr_t)data;if(i>=0&&i<ctx->wardrive_home_count)entity=ctx->wardrive_home[i].ssid;slot="remove";}
 else if(cb==wardrive_blacklist_scan_pick_cb||cb==wardrive_blacklist_scan_pick_del_cb){wardrive_bl_scan_pick_t *pick=data;if(pick)entity=pick->mac;slot=cb==wardrive_blacklist_scan_pick_cb?"pick":"delete";}
 else if(cb==wardrive_fix_file_cb){snprintf(entity_key,sizeof(entity_key),"file-%u",(unsigned)(uintptr_t)data);entity=entity_key;slot="fix";}
 else if(cb==pcap_viewer_filter_cb){snprintf(entity_key,sizeof(entity_key),"filter-%u",(unsigned)(uintptr_t)data);entity=entity_key;slot="filter";}
 else if(cb==pcap_viewer_packet_detail_cb){snprintf(entity_key,sizeof(entity_key),"%s:packet-%u",ctx->pcap_viewer?ctx->pcap_viewer->selected_path:"",(unsigned)(uintptr_t)data);entity=entity_key;slot="packet";}
 /* Observer rows repeat one template per cached network and client; without an
    entity every row would claim the same identity and be rejected as a duplicate. */
 else if(cb==network_row_click_cb){int i=(int)(intptr_t)data;if(ctx->observer_networks&&i>=0&&i<ctx->observer_network_count)entity=ctx->observer_networks[i].bssid;slot="observe";}
 else if(cb==client_row_click_cb){intptr_t packed=(intptr_t)data;int i=(int)(packed>>16),j=(int)(packed&0xFFFF);
  if(ctx->observer_networks&&i>=0&&i<ctx->observer_network_count&&j>=0&&j<MAX_CLIENTS_PER_NETWORK)entity=ctx->observer_networks[i].clients[j];slot="client";}
 else if(cb==main_tile_event_cb||cb==internal_tile_event_cb||cb==settings_tile_event_cb||cb==attack_tile_event_cb||cb==observer_attack_tile_event_cb||cb==observer_station_attack_tile_event_cb||cb==bt_menu_tile_event_cb||cb==global_attack_tile_event_cb||cb==compromised_data_tile_event_cb||cb==beacon_spam_tile_event_cb)slot=data?(const char *)data:"tile";
 /* BT scan rows repeat one template per discovered device; key on the MAC so
    each row is a distinct binding rather than a rejected duplicate. */
 else if(cb==bt_scan_device_click_cb){int i=(int)(intptr_t)data;if(i>=0&&i<bt_device_count)entity=bt_devices[i].mac;slot="locate";}
 else if(cb==spinbox_increment_event_cb||cb==spinbox_decrement_event_cb){
  lv_obj_t *row=lv_obj_get_parent(lv_obj_get_parent(obj));lv_obj_t *label=lv_obj_get_child(row,0);
  slot=lv_label_get_text(label);lv_obj_t *popup=lv_obj_get_parent(row);
  for(int index=lv_obj_get_index(row)-1;index>=0;index--){lv_obj_t *prior=lv_obj_get_child(popup,index);if(!lv_obj_check_type(prior,&lv_label_class))continue;const char *heading=lv_label_get_text(prior);if(!strcmp(heading,"Grove")){ctx=&grove_ctx;break;}if(!strcmp(heading,"MBus")){ctx=&mbus_ctx;break;}if(!strcmp(heading,"USB")){ctx=&usb_ctx;break;}}
 }
 else if(cb==espshark_action_btn_event_cb)slot=data?(const char *)data:"action";
 else if(cb==tab_click_cb){ctx=get_ctx_for_tab((tab_id_t)(uintptr_t)data);slot="tab";screen="navigation";}
 static const struct {lv_event_cb_t cb;const char *slot;} deep_actions[]={{pcap_viewer_detail_action_cb,"pcap_viewer_detail_action_cb"},{pcap_viewer_device_filter_cb,"pcap_viewer_device_filter_cb"},{pcap_viewer_device_profile_cb,"pcap_viewer_device_profile_cb"},{pcap_viewer_investigation_finding_filter_cb,"pcap_viewer_investigation_finding_filter_cb"},{pcap_viewer_investigation_mode_cb,"pcap_viewer_investigation_mode_cb"},{pcap_viewer_application_filter_cb,"pcap_viewer_application_filter_cb"},{pcap_map_mode_cb,"pcap_map_mode_cb"},{pcap_map_node_click_cb,"pcap_map_node_click_cb"},{pcap_map_node_quick_dossier_cb,"pcap_map_node_quick_dossier_cb"},{pcap_map_node_quick_filter_cb,"pcap_map_node_quick_filter_cb"},{pcap_viewer_connection_filter_cb,"pcap_viewer_connection_filter_cb"},{pcap_viewer_connection_follow_cb,"pcap_viewer_connection_follow_cb"},{pcap_viewer_connection_hex_cb,"pcap_viewer_connection_hex_cb"},{pcap_viewer_object_preview_cb,"pcap_viewer_object_preview_cb"},{pcap_viewer_object_filter_cb,"pcap_viewer_object_filter_cb"},{pcap_viewer_object_delete_cb,"pcap_viewer_object_delete_cb"},{pcap_viewer_tool_action_cb,"pcap_viewer_tool_action_cb"}};
 for(unsigned i=0;i<sizeof(deep_actions)/sizeof(deep_actions[0]);i++)if(cb==deep_actions[i].cb){snprintf(entity_key,sizeof(entity_key),"%lu:%.130s",(unsigned long)(uintptr_t)data,ctx->pcap_viewer?ctx->pcap_viewer->selected_path:"");entity=entity_key;slot=deep_actions[i].slot;screen="analysis";}
 if(cb==pcap_viewer_devices_cb)slot="local";else if(cb==pcap_viewer_remote_endpoints_cb)slot="remote";
 if(cb==wardrive_files_checkbox_cb){wardrive_wigle_file_t *file=data;if(file)entity=file->path;slot="select";}
 if(cb==compromised_file_copy_cb||cb==compromised_file_delete_cb||cb==wardrive_fix_file_cb){int i=compromised_index_from_user_data(data);if(i>=0&&i<ctx->wardrive_wigle_file_count)entity=ctx->wardrive_wigle_files[i].path;slot=cb==compromised_file_copy_cb?"copy":cb==compromised_file_delete_cb?"delete":"fix";}
 char id[320];snprintf(id,sizeof(id),"%s/%s/%s/%s/%s",tid,screen,tab_transport_name(tab_id_for_ctx(ctx)),entity,slot);
 for(int i=0;i<1024;i++)if(app_bindings[i].object&&!strcmp(app_bindings[i].id,id)){emu_unsupported("Duplicate active control binding");return NULL;}
 for(int i=0;i<1024;i++)if(!app_bindings[i].object){app_bindings[i].object=obj;snprintf(app_bindings[i].id,sizeof(app_bindings[i].id),"%s",id);app_bindings[i].generation=++app_binding_generation;lv_obj_add_event_cb(obj,app_binding_delete,LV_EVENT_DELETE,NULL);return lv_obj_add_event_cb(obj,cb,event,data);}
 emu_unsupported("Control binding capacity exceeded");return NULL;
}
EMSCRIPTEN_KEEPALIVE const char *emu_binding_id(uintptr_t object) {for(int i=0;i<1024;i++)if(app_bindings[i].object==(lv_obj_t *)object)return app_bindings[i].id;return "";}
EMSCRIPTEN_KEEPALIVE unsigned emu_binding_generation(uintptr_t object) {for(int i=0;i<1024;i++)if(app_bindings[i].object==(lv_obj_t *)object)return app_bindings[i].generation;return 0;}
/* Simulator-owned affordances are labelled with an explicit emu. prefix instead
   of borrowing a frozen firmware template ID. The tab suffix keeps one popup per
   module distinguishable, matching the production ID layout. */
static lv_event_dsc_t *app_bind_adapter(lv_obj_t *obj,lv_event_cb_t cb,lv_event_code_t event,void *data,const char *id) {
 tab_context_t *owner=get_current_ctx();for(int tab=0;tab<4;tab++){tab_context_t *candidate=get_ctx_for_tab(tab);if(app_descendant(obj,candidate->container)){owner=candidate;break;}}
 char full[320];snprintf(full,sizeof(full),"%s/%s",id,tab_transport_name(tab_id_for_ctx(owner)));
 for(int i=0;i<1024;i++)if(app_bindings[i].object&&!strcmp(app_bindings[i].id,full)){emu_unsupported("Duplicate active control binding");return NULL;}
 for(int i=0;i<1024;i++)if(!app_bindings[i].object){app_bindings[i].object=obj;snprintf(app_bindings[i].id,sizeof(app_bindings[i].id),"%s",full);app_bindings[i].generation=++app_binding_generation;lv_obj_add_event_cb(obj,app_binding_delete,LV_EVENT_DELETE,NULL);return lv_obj_add_event_cb(obj,cb,event,data);}
 emu_unsupported("Control binding capacity exceeded");return NULL;
}
EMSCRIPTEN_KEEPALIVE void emu_inspect_bindings(void) {
 EM_ASM({globalThis.emulatorBindings=[];});
 for(int i=0;i<1024;i++)if(app_bindings[i].object) {
  EM_ASM({globalThis.emulatorBindings.push({object:$0,binding:UTF8ToString($1),generation:$2});},
         (uintptr_t)app_bindings[i].object,app_bindings[i].id,app_bindings[i].generation);
 }
}
/* Observer boundaries come last: they use app_bind_adapter and the real
   lv_obj_add_event_cb, both of which are only available past the #undef above. */
#include "observer.c"
#include "bluetooth.c"
#include "detectors.c"
#include "wardrive.c"
#include "wardrive_setup.c"
#include "wardrive_upload.c"
#include "wardrive_lists.c"
#include "pcap_artifacts.c"
#include "pcap_files.c"
#include "clock.c"
#include "settings_system.c"
#include "settings_ota.c"
#include "settings_sd_admin.c"
#include "nettools_gitm.c"
#include "nettools_wpasec.c"
#include "nettools_nmap_iot.c"
#include "nettools_menu.c"
#include "attacks_deauth_arp.c"
#include "attacks_karma_beacon.c"
#include "attacks_rogue_evil_mitm.c"
#include "portal_demo.c"
#include "sae.c"
#include "global_attacks.c"
#include "radar.c"
#include "attacks_routes.c"

#include "dialogs.c"
#include "startup_story.c"
#include "wifi_stories.c"
#include "recon_stories.c"
#include "queue2_attacks_story.c"
#include "queue2_nettools_story.c"
#include "queue2_wardrive_story.c"
#include "queue2_system_story.c"
#include "settings_s18_story.c"
#include "settings_s19_story.c"
#include "settings_s20_story.c"
