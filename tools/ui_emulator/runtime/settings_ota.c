/* Native Monster OTA form and monitor layout. Monitor/scan layout transcribed
 * from main.c only to remove UART, task and partition-command boundaries.
 * Credentials stay in LVGL memory. All firmware actions are offline model jobs. */
static int app_ota_job, app_ota_scan_job;
static int app_ota_slot_job;
static int app_queue2_ota_update,app_queue2_ota_slot;
static void app_ota_slots_refresh(void);
static char app_ota_networks[MAX_NETWORKS][33];
EM_JS(int, app_ota_connected, (int tab), {
 return emulatorDevice.device.systemStatus(emulatorDevice.module(tab)).connected?1:0;
});
EM_JS(void, app_ota_network_ssid, (int tab,int index,char *out,int size), {
 const rows=emulatorDevice.device.snapshot(emulatorDevice.module(tab)).networks;
 stringToUTF8(rows[index]?.ssid||"",out,size);
});
static tab_id_t ota_pick_target_tab(void) {
 if(app_ota_connected(TAB_GROVE))return TAB_GROVE;
 if(app_ota_connected(TAB_MBUS))return TAB_MBUS;
 return TAB_INTERNAL;
}
EM_JS(void, app_ota_cancel, (int id), { emulatorDevice.device.cancel(id); });
EM_JS(int, app_ota_start, (int tab,int dev,char *error,int size), {
 try { return emulatorDevice.device.systemStart(emulatorDevice.module(tab),'update',
  {channel:dev?'dev':'main',outcome:globalThis.emulatorSystemOutcome||'success'}); }
 catch(e){stringToUTF8('Simulated update: '+e.message,error,size);return 0;}
});
EM_JS(int, app_ota_poll, (int id,char *text,int size,int *progress), {
 const j=emulatorDevice.device.job(id);
 HEAP32[progress>>2]=Math.round((j?.progress||0)*100);
 stringToUTF8('Simulated OTA: '+(j?(j.state==='running'?j.result?.stage:j.state):'reset')+
   (j?.error?' - '+j.error:""),text,size);
 return j?.state==='running'?0:1;
});
EM_JS(void, app_ota_info, (int tab,char *text,int size), {
 const s=emulatorDevice.device.systemStatus(emulatorDevice.module(tab));
 stringToUTF8('Offline simulation\nBoard: '+s.boardName+'\nVersion: '+s.version+
 '\nConnected: '+s.connected+'\nReboots: '+s.reboots,text,size);
});
static void ota_close_monitor(void)
{
    if (app_ota_slot_job) app_ota_cancel(app_ota_slot_job);
    app_ota_slot_job=0;
    if (app_ota_job) app_ota_cancel(app_ota_job);
    app_ota_job = 0;
    g_ota.monitoring = false;
    g_ota.task = NULL;
    if (g_ota.mon_overlay) {
        if(lv_obj_is_valid(g_ota.mon_overlay)) lv_obj_del(g_ota.mon_overlay);
        g_ota.mon_overlay = NULL;
    }
    g_ota.mon_wifi = NULL;
    g_ota.mon_ip = NULL;
    g_ota.mon_ota = NULL;
    g_ota.mon_phase = NULL;
    g_ota.mon_detail = NULL;
    g_ota.mon_progress = NULL;
    for (int i = 0; i < 4; i++) g_ota.mon_steps[i] = NULL;
    g_ota.mon_release_list = NULL;
    g_ota.mon_log_container = NULL;
    g_ota.mon_log_label = NULL;
    g_ota.mon_close_btn = NULL;
    g_ota.mon_close_label = NULL;
    g_ota.mon_release_count = 0;
    g_ota.info_summary = NULL;
    for (int i = 0; i < 2; i++) {
        g_ota.info_slots[i].card = NULL;
        g_ota.info_slots[i].title_label = NULL;
        g_ota.info_slots[i].role_label = NULL;
        g_ota.info_slots[i].meta_label = NULL;
        g_ota.info_slots[i].version_label = NULL;
        g_ota.info_slots[i].build_label = NULL;
        g_ota.info_slots[i].activate_btn = NULL;
        g_ota.info_slots[i].activate_label = NULL;
    }
    g_ota.install_started = false;
    g_ota.reboot_wait_shown = false;
    g_ota.flash_progress_seen = false;
    g_ota.byte_progress_seen = false;
    g_ota.terminal_status_seen = false;
    g_ota.screen_timeout_suspended = false;
    g_ota.list_after_connect = false;
    g_ota.list_sent_after_connect = false;
    g_ota.release_list_waiting = false;
    g_ota.mon_release_count = 0;
    g_ota.view = OTA_VIEW_NONE;
}
static void ota_open_monitor(ota_view_t view, const char *title)
{
    if (!internal_container) return;
    ota_close_monitor();   // ensure only one overlay/task at a time

    g_ota.log_buf[0] = '\0';
    g_ota.view = view;
    g_ota.install_started = false;
    g_ota.reboot_wait_shown = false;
    g_ota.flash_progress_seen = false;
    g_ota.byte_progress_seen = false;
    g_ota.terminal_status_seen = false;
    g_ota.screen_timeout_suspended = false;
    g_ota.list_after_connect = false;
    g_ota.list_sent_after_connect = false;
    g_ota.release_list_waiting = false;


    g_ota.mon_overlay = lv_obj_create(internal_container);
    lv_obj_remove_style_all(g_ota.mon_overlay);
    lv_obj_set_size(g_ota.mon_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ota.mon_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_ota.mon_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(g_ota.mon_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ota.mon_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(g_ota.mon_overlay);
    lv_obj_set_size(box, view == OTA_VIEW_INFO ? 640 : 640,
                    view == OTA_VIEW_INFO ? 560 : 620);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, ui_card_color(), 0);
    lv_obj_set_style_border_color(box, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_pad_all(box, view == OTA_VIEW_INFO ? 12 : 16, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ttl = lv_label_create(box);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ttl, COLOR_MATERIAL_ORANGE, 0);

    if (view == OTA_VIEW_STATUS) {
        lv_obj_t *status_card = lv_obj_create(box);
        lv_obj_set_size(status_card, lv_pct(100), 166);
        lv_obj_set_style_bg_color(status_card, ui_bg_color(), 0);
        lv_obj_set_style_border_width(status_card, 1, 0);
        lv_obj_set_style_border_color(status_card, ui_border_color(), 0);
        lv_obj_set_style_radius(status_card, 8, 0);
        lv_obj_set_style_pad_all(status_card, 12, 0);
        lv_obj_set_style_pad_row(status_card, 6, 0);
        lv_obj_set_flex_flow(status_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

        g_ota.mon_phase = lv_label_create(status_card);
        lv_label_set_text(g_ota.mon_phase, LV_SYMBOL_WIFI " Connecting WiFi");
        lv_obj_set_style_text_font(g_ota.mon_phase, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(g_ota.mon_phase, COLOR_MATERIAL_ORANGE, 0);
        lv_obj_set_width(g_ota.mon_phase, lv_pct(100));
        lv_label_set_long_mode(g_ota.mon_phase, LV_LABEL_LONG_DOT);

        g_ota.mon_detail = lv_label_create(status_card);
        lv_label_set_text(g_ota.mon_detail, "Waiting for JanOS network status...");
        lv_obj_set_style_text_font(g_ota.mon_detail, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(g_ota.mon_detail, ui_text_color(), 0);
        lv_obj_set_width(g_ota.mon_detail, lv_pct(100));
        lv_label_set_long_mode(g_ota.mon_detail, LV_LABEL_LONG_WRAP);

        g_ota.mon_progress = lv_bar_create(status_card);
        lv_obj_set_size(g_ota.mon_progress, lv_pct(100), 8);
        lv_bar_set_range(g_ota.mon_progress, 0, 100);
        lv_bar_set_value(g_ota.mon_progress, 0, LV_ANIM_OFF);

        lv_obj_t *steps = lv_obj_create(status_card);
        lv_obj_set_size(steps, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(steps, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(steps, 0, 0);
        lv_obj_set_style_pad_all(steps, 0, 0);
        lv_obj_set_style_pad_column(steps, 10, 0);
        lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(steps, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < 4; i++) {
            g_ota.mon_steps[i] = lv_label_create(steps);
            lv_obj_set_flex_grow(g_ota.mon_steps[i], 1);
            lv_obj_set_style_text_font(g_ota.mon_steps[i], &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(g_ota.mon_steps[i], ui_muted_color(), 0);
            lv_label_set_long_mode(g_ota.mon_steps[i], LV_LABEL_LONG_DOT);
        }
        ota_status_set_step(0, 0);

        g_ota.mon_wifi = lv_label_create(box);
        lv_label_set_text(g_ota.mon_wifi, "WiFi: connecting...");
        lv_obj_set_style_text_font(g_ota.mon_wifi, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_ota.mon_wifi, ui_text_color(), 0);

        g_ota.mon_ip = lv_label_create(box);
        lv_label_set_text(g_ota.mon_ip, "IP: ...");
        lv_obj_set_style_text_font(g_ota.mon_ip, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_ota.mon_ip, ui_text_color(), 0);

        g_ota.mon_ota = lv_label_create(box);
        lv_label_set_text(g_ota.mon_ota, "OTA: waiting for the C5...");
        lv_obj_set_style_text_font(g_ota.mon_ota, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_ota.mon_ota, ui_muted_color(), 0);
        lv_obj_set_width(g_ota.mon_ota, lv_pct(100));
        lv_label_set_long_mode(g_ota.mon_ota, LV_LABEL_LONG_WRAP);

        g_ota.mon_release_list = lv_obj_create(box);
        lv_obj_set_size(g_ota.mon_release_list, lv_pct(100), 220);
        lv_obj_set_style_bg_color(g_ota.mon_release_list, ui_card_color(), 0);
        lv_obj_set_style_border_width(g_ota.mon_release_list, 1, 0);
        lv_obj_set_style_border_color(g_ota.mon_release_list, ui_border_color(), 0);
        lv_obj_set_style_radius(g_ota.mon_release_list, 8, 0);
        lv_obj_set_style_pad_all(g_ota.mon_release_list, 8, 0);
        lv_obj_set_style_pad_row(g_ota.mon_release_list, 8, 0);
        lv_obj_set_flex_flow(g_ota.mon_release_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(g_ota.mon_release_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(g_ota.mon_release_list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_add_flag(g_ota.mon_release_list, LV_OBJ_FLAG_HIDDEN);
    }

    g_ota.mon_log_container = lv_obj_create(box);
    lv_obj_set_width(g_ota.mon_log_container, lv_pct(100));
    lv_obj_set_flex_grow(g_ota.mon_log_container, 1);
    lv_obj_set_style_bg_color(g_ota.mon_log_container, ui_bg_color(), 0);
    lv_obj_set_style_border_width(g_ota.mon_log_container, 1, 0);
    lv_obj_set_style_border_color(g_ota.mon_log_container, ui_border_color(), 0);
    lv_obj_set_style_radius(g_ota.mon_log_container, 8, 0);
    lv_obj_set_style_pad_all(g_ota.mon_log_container, 8, 0);
    lv_obj_set_scroll_dir(g_ota.mon_log_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_ota.mon_log_container, LV_SCROLLBAR_MODE_AUTO);

    g_ota.mon_log_label = lv_label_create(g_ota.mon_log_container);
    lv_label_set_text(g_ota.mon_log_label, "");
    lv_obj_set_width(g_ota.mon_log_label, lv_pct(100));
    lv_label_set_long_mode(g_ota.mon_log_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_ota.mon_log_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_ota.mon_log_label, ui_muted_color(), 0);

    g_ota.mon_close_btn = lv_btn_create(box);
    lv_obj_set_size(g_ota.mon_close_btn, lv_pct(100), 46);
    lv_obj_set_style_radius(g_ota.mon_close_btn, 8, 0);
    app_bind_adapter(g_ota.mon_close_btn, ota_monitor_close_cb, LV_EVENT_CLICKED, NULL,"emu.phase46.ota.monitor.close");
    g_ota.mon_close_label = lv_label_create(g_ota.mon_close_btn);
    lv_obj_set_style_text_font(g_ota.mon_close_label, &lv_font_montserrat_18, 0);
    ota_monitor_set_close_state(true, COLOR_MATERIAL_ORANGE, "Close");

    if (g_ota.mon_wifi) lv_label_set_text(g_ota.mon_wifi, "WiFi: simulated offline");
    if (g_ota.mon_ip) lv_label_set_text(g_ota.mon_ip, "Network access: disabled");
    if (g_ota.mon_ota) lv_label_set_text(g_ota.mon_ota, "Demo firmware only; no hardware flashed.");
}

static void ota_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!internal_container) return;
    if (g_ota.target_tab == TAB_INTERNAL) {
        if (g_ota.page_status) lv_label_set_text(g_ota.page_status, "No C5 detected");
        return;
    }
    if (g_ota.scan_running || g_ota.scan_overlay) return;

    g_ota.sel_ssid[0] = '\0';
    g_ota.scan_selected = false;
    g_ota.selected_saved_password = false;

    g_ota.scan_overlay = lv_obj_create(internal_container);
    lv_obj_remove_style_all(g_ota.scan_overlay);
    lv_obj_set_size(g_ota.scan_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_ota.scan_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_ota.scan_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(g_ota.scan_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_ota.scan_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *box = lv_obj_create(g_ota.scan_overlay);
    lv_obj_set_size(box, 600, 520);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, ui_card_color(), 0);
    lv_obj_set_style_border_color(box, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ttl = lv_label_create(box);
    lv_label_set_text(ttl, LV_SYMBOL_WIFI " Select WiFi network");
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ttl, COLOR_MATERIAL_ORANGE, 0);

    g_ota.scan_status = lv_label_create(box);
    lv_label_set_text(g_ota.scan_status, "Scanning WiFi networks...");
    lv_obj_set_style_text_font(g_ota.scan_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_ota.scan_status, ui_muted_color(), 0);
    lv_obj_set_width(g_ota.scan_status, lv_pct(100));
    lv_label_set_long_mode(g_ota.scan_status, LV_LABEL_LONG_WRAP);

    g_ota.scan_list = lv_obj_create(box);
    lv_obj_set_width(g_ota.scan_list, lv_pct(100));
    lv_obj_set_flex_grow(g_ota.scan_list, 1);
    lv_obj_set_style_bg_color(g_ota.scan_list, ui_bg_color(), 0);
    lv_obj_set_style_border_width(g_ota.scan_list, 0, 0);
    lv_obj_set_style_radius(g_ota.scan_list, 8, 0);
    lv_obj_set_style_pad_all(g_ota.scan_list, 6, 0);
    lv_obj_set_flex_flow(g_ota.scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_ota.scan_list, 4, 0);
    lv_obj_set_scroll_dir(g_ota.scan_list, LV_DIR_VER);

    lv_obj_t *close_btn = lv_btn_create(box);
    lv_obj_set_size(close_btn, lv_pct(100), 46);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    app_bind_adapter(close_btn, ota_scan_close_cb, LV_EVENT_CLICKED, NULL,"emu.phase46.ota.scan.close");
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_center(cl);

    g_ota.scan_running = true;
    lv_label_set_text(g_ota.scan_status, "Scanning simulated scenario networks...");
    app_ota_scan_job=app_model_scan(g_ota.target_tab);
    if(!app_ota_scan_job){g_ota.scan_running=false;lv_label_set_text(g_ota.scan_status,"Simulated scan unavailable; close and retry.");}

}
static void ota_scan_close(void) {
 if(app_ota_scan_job)app_model_cancel(app_ota_scan_job);
 app_ota_scan_job=0;
 g_ota.scan_running=false;g_ota.scan_task=NULL;
 if(g_ota.scan_overlay && lv_obj_is_valid(g_ota.scan_overlay))lv_obj_del(g_ota.scan_overlay);
 g_ota.scan_overlay=g_ota.scan_status=g_ota.scan_list=NULL;
}
static void ota_scan_row_click_cb(lv_event_t *e) {
 const char *ssid=lv_event_get_user_data(e);
 if(g_ota.ssid_ta)lv_textarea_set_text(g_ota.ssid_ta,ssid);
 if(g_ota.pass_ta)lv_textarea_set_text(g_ota.pass_ta,"");
 ota_scan_close();
 if(g_ota.page_status)lv_label_set_text(g_ota.page_status,"Simulated network selected; password is not used.");
}
static void ota_channel_changed_cb(lv_event_t *e) {
 (void)e;snprintf(g_ota.channel,sizeof(g_ota.channel),"%s",lv_dropdown_get_selected(g_ota.channel_dd)?"dev":"main");
 if(g_ota.page_status)lv_label_set_text_fmt(g_ota.page_status,"Simulated channel: %s",g_ota.channel);
}
static void ota_check_btn_cb(lv_event_t *e) {
 (void)e;
 if(g_ota.target_tab==TAB_INTERNAL){lv_label_set_text(g_ota.page_status,"No simulated module connected");return;}
 ota_open_monitor(OTA_VIEW_STATUS,"Monster OTA - Offline simulation");
 char error[256]={0};app_ota_job=app_ota_start(g_ota.target_tab,!strcmp(g_ota.channel,"dev"),error,sizeof(error));
 if(!app_ota_job){ota_status_set_phase("Simulated update unavailable",error,COLOR_MATERIAL_RED,0,0);return;}
 app_queue2_ota_update=app_ota_job;
 ota_status_set_phase("Simulated update starting","Demo firmware only; no network or hardware access.",COLOR_MATERIAL_ORANGE,0,0);
 ota_monitor_set_close_state(true,COLOR_MATERIAL_ORANGE,"Cancel simulated update");
}
static void ota_list_btn_cb(lv_event_t *e) {
 (void)e;ota_open_monitor(OTA_VIEW_LIST,"Monster OTA - Simulated releases");
 ota_append_log("Offline demo releases\nmain: demo-main-2\ndev: demo-dev-2\nClose and use Download & Flash to simulate an update.");
}
static void ota_info_btn_cb(lv_event_t *e) {
 (void)e;if(g_ota.target_tab==TAB_INTERNAL){lv_label_set_text(g_ota.page_status,"No simulated module connected");return;}
 ota_open_monitor(OTA_VIEW_INFO,"Monster OTA - Simulated device info");
 lv_obj_set_flex_flow(g_ota.mon_log_container,LV_FLEX_FLOW_COLUMN);
 lv_obj_t *row=lv_obj_create(g_ota.mon_log_container);lv_obj_set_size(row,lv_pct(100),270);
 lv_obj_move_to_index(row,0);
 lv_obj_set_flex_flow(row,LV_FLEX_FLOW_ROW);
 ota_create_info_slot_card(row,0);ota_create_info_slot_card(row,1);app_ota_slots_refresh();
 char text[384];app_ota_info(g_ota.target_tab,text,sizeof(text));ota_append_log(text);
}
static void app_settings_ota_tick(double now) {
 if(app_ota_slot_job && app_model_state(app_ota_slot_job)!=1) {
  int state=app_model_state(app_ota_slot_job);app_ota_slot_job=0;
  if(g_ota.info_slots[0].card){app_ota_slots_refresh();ota_append_log(state==2?"Simulated boot slot activated.":"Slot activation cancelled; running image unchanged.");}
 }

 (void)now;
 if(g_ota.page && !lv_obj_is_valid(g_ota.page)) {
  ota_close_monitor();ota_scan_close();memset(&g_ota,0,sizeof(g_ota));return;
 }
 if(app_ota_scan_job && g_ota.scan_list && app_model_state(app_ota_scan_job)!=1) {
  int state=app_model_state(app_ota_scan_job);app_ota_scan_job=0;g_ota.scan_running=false;
  int count=state==2?app_model_rows(g_ota.target_tab):0;if(count>MAX_NETWORKS)count=MAX_NETWORKS;
  lv_label_set_text(g_ota.scan_status,count?"Simulated scenario networks - credentials are not used.":"No simulated networks found; close and retry.");
  for(int i=0;i<count;i++) {
   app_ota_network_ssid(g_ota.target_tab,i,app_ota_networks[i],sizeof(app_ota_networks[i]));
   lv_obj_t *row=lv_btn_create(g_ota.scan_list);lv_obj_set_size(row,lv_pct(100),60);
   lv_obj_t *label=lv_label_create(row);lv_label_set_text(label,app_ota_networks[i][0]?app_ota_networks[i]:"(Hidden simulated network)");lv_obj_center(label);
   char id[80];snprintf(id,sizeof(id),"emu.phase46.ota.scan.network.%d",i);
   app_bind_adapter(row,ota_scan_row_click_cb,LV_EVENT_CLICKED,app_ota_networks[i],id);
  }
 }
 if(g_ota.page_status && !app_ota_job && !lv_label_get_text(g_ota.page_status)[0])
  lv_label_set_text(g_ota.page_status,"Offline simulation: demo firmware; credentials are not used.");
 if(!app_ota_job)return;
 if(!g_ota.mon_overlay){app_ota_cancel(app_ota_job);app_ota_job=0;return;}
 char text[256];int pct=0;int done=app_ota_poll(app_ota_job,text,sizeof(text),&pct);
 ota_status_set_phase(text,"Offline simulation. No hardware flashed.",COLOR_MATERIAL_ORANGE,done?4:pct/25,pct);
 if(done){app_ota_job=0;ota_monitor_set_close_state(true,COLOR_MATERIAL_ORANGE,"Close");
  char info[384];app_ota_info(g_ota.target_tab,info,sizeof(info));ota_append_log(info);}
}

EM_JS(int,app_ota_slot_snapshot,(int tab,int index,char *out,int size),{
 const s=emulatorDevice.device.systemStatus(emulatorDevice.module(tab));if(out)stringToUTF8(s.slots[index],out,size);return s.activeSlot;
});
EM_JS(int,app_ota_slot_start,(int tab,int slot,char *error,int size),{
 try{return emulatorDevice.device.systemActivateSlot(emulatorDevice.module(tab),slot);}catch(e){stringToUTF8(e.message,error,size);return 0;}
});
static void app_ota_slots_refresh(void) {
 int active=app_ota_slot_snapshot(g_ota.target_tab,-1,NULL,0);
 for(int i=0;i<2;i++) {
  ota_slot_info_t *s=&g_ota.info_slots[i];if(!s->card)return;
  lv_label_set_text_fmt(s->title_label,"Slot ota_%d",i);
  lv_label_set_text(s->role_label,i==active?"Running / boot slot":"Standby image");
  lv_label_set_text(s->meta_label,"Offline simulated partition");
  char version[48];app_ota_slot_snapshot(g_ota.target_tab,i,version,sizeof(version));lv_label_set_text(s->version_label,version);
  if(app_ota_slot_job)lv_obj_add_state(s->activate_btn,LV_STATE_DISABLED);else lv_obj_remove_state(s->activate_btn,LV_STATE_DISABLED);
  lv_label_set_text(s->build_label,"Demo firmware only");
  if(i==active)lv_obj_add_flag(s->activate_btn,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(s->activate_btn,LV_OBJ_FLAG_HIDDEN);
 }
}
static void ota_slot_activate_cb(lv_event_t *e) {
 const char *slot=lv_event_get_user_data(e);if(!slot||g_ota.target_tab==TAB_INTERNAL)return;
 char error[160]={0};int job=app_ota_slot_start(g_ota.target_tab,!strcmp(slot,"ota_1"),error,sizeof(error));
 if(!job){ota_append_log(error);return;}
 app_ota_slot_job=job;app_queue2_ota_slot=job;app_ota_slots_refresh();
 ota_append_log("Simulated slot activation pending reboot completion.");
}
