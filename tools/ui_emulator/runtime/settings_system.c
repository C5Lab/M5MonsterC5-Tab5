static void app_system_close_page(void);
static lv_obj_t *app_system_page, *app_system_labels[4], *app_system_buttons[4];
static int app_system_jobs[4];
static char app_system_errors[4][160];

EM_JS(void,app_system_status_text,(int tab,int job,char *out,int size),{
 try {
  const id=emulatorDevice.module(tab),s=emulatorDevice.device.systemStatus(id);
  const j=job?emulatorDevice.device.job(job):null;
  const operation=job?(j?(j.error||(j.state==='running'?j.result?.stage:j.state)||j.state):'Operation reset'):(s.active?'Busy':'Idle');
  stringToUTF8(`${id.toUpperCase()} - ${s.connected?'Connected':'Disconnected'}\nBoard: ${s.boardName}\nFirmware: ${s.version}\nUptime: ${Math.floor(s.uptimeMs/1000)} s | Reboots: ${s.reboots}\nSD: ${s.sdPresent?'present':'missing'} | ${s.sdUsedBytes}/${s.sdCapacityBytes} bytes\nOperation: ${operation}`,out,size);
 }catch(e){stringToUTF8(e.message,out,size);}
});
EM_JS(int,app_system_reboot,(int tab,char *error,int size),{
 try{return emulatorDevice.device.systemStart(emulatorDevice.module(tab),'reboot');}
 catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(void,app_system_metadata,(int tab,int *values,char *version,int size),{
 const s=emulatorDevice.device.systemStatus(emulatorDevice.module(tab));
 HEAP32[values>>2]=s.connected?1:0;HEAP32[(values>>2)+1]=s.sdPresent?1:0;
 HEAP32[(values>>2)+2]=s.sdUsedBytes;HEAP32[(values>>2)+3]=s.sdCapacityBytes;
 stringToUTF8(s.version,version,size);
});
static void app_system_sync_metadata(void) {
 for(int tab=0;tab<=2;tab+=2){
  int v[4]={0};char version[48];app_system_metadata(tab,v,version,sizeof(version));
  tab_context_t *ctx=get_ctx_for_tab(tab);
  ctx->sd_card_present=v[0]&&v[1];ctx->home_sd_stats_valid=v[0]&&v[1];
  ctx->home_sd_total_bytes=(uint32_t)v[3];ctx->home_sd_free_bytes=(uint32_t)(v[3]-v[2]);
  snprintf(ctx->janos_version,sizeof(ctx->janos_version),"%s",version);
  snprintf(ctx->janos_app_version,sizeof(ctx->janos_app_version),"%s",version);
  snprintf(ctx->janos_app_project,sizeof(ctx->janos_app_project),"JanOS demo");
  ctx->home_meta_last_update_ms=(uint32_t)app_device_ms+1;
 }
}
static void app_system_close_page(void) {
 for(int tab=0;tab<4;tab++){
  if(app_system_jobs[tab]&&app_model_state(app_system_jobs[tab])==1)app_model_cancel(app_system_jobs[tab]);
  app_system_jobs[tab]=0;app_system_errors[tab][0]=0;
  app_system_labels[tab]=NULL;app_system_buttons[tab]=NULL;
 }
 if(app_system_page){lv_obj_del(app_system_page);app_system_page=NULL;}
}
static void app_system_back_cb(lv_event_t *e){(void)e;show_internal_tiles();}
static void app_system_reboot_cb(lv_event_t *e){
 int tab=(int)(intptr_t)lv_event_get_user_data(e);
 app_system_errors[tab][0]=0;
 int job=app_system_reboot(tab,app_system_errors[tab],sizeof(app_system_errors[tab]));
 if(job)app_system_jobs[tab]=job;
}
static void app_system_tick(void){
 app_system_sync_metadata();
 if(!app_system_page)return;
 for(int tab=0;tab<=2;tab+=2){
  char text[640];app_system_status_text(tab,app_system_jobs[tab],text,sizeof(text));
  if(app_system_errors[tab][0]){strncat(text,"\n",sizeof(text)-strlen(text)-1);strncat(text,app_system_errors[tab],sizeof(text)-strlen(text)-1);}
  lv_label_set_text(app_system_labels[tab],text);
  if(app_system_jobs[tab]&&app_model_state(app_system_jobs[tab])==1)lv_obj_add_state(app_system_buttons[tab],LV_STATE_DISABLED);
  else lv_obj_clear_state(app_system_buttons[tab],LV_STATE_DISABLED);
 }
}
static void app_system_show(void){
 app_system_close_page();
 if(internal_tiles)lv_obj_add_flag(internal_tiles,LV_OBJ_FLAG_HIDDEN);
 if(internal_settings_page)lv_obj_add_flag(internal_settings_page,LV_OBJ_FLAG_HIDDEN);
 app_system_page=lv_obj_create(internal_container);lv_obj_set_size(app_system_page,lv_pct(100),lv_pct(100));
 lv_obj_set_style_bg_color(app_system_page,ui_bg_color(),0);lv_obj_set_flex_flow(app_system_page,LV_FLEX_FLOW_COLUMN);
 lv_obj_set_style_pad_all(app_system_page,16,0);lv_obj_set_style_pad_row(app_system_page,12,0);
 lv_obj_t *back=lv_btn_create(app_system_page);lv_obj_set_size(back,140,52);style_back_nav_button(back);
 app_bind_adapter(back,app_system_back_cb,LV_EVENT_CLICKED,NULL,"emu.phase46.system.back");
 lv_obj_t *label=lv_label_create(back);lv_label_set_text(label,LV_SYMBOL_LEFT " Back");lv_obj_center(label);
 label=lv_label_create(app_system_page);lv_label_set_text(label,"Module Status - offline simulation");
 lv_obj_set_width(label,lv_pct(100));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
 lv_obj_set_style_text_font(label,&lv_font_montserrat_20,0);
 for(int tab=0;tab<=2;tab+=2){
  lv_obj_t *card=lv_obj_create(app_system_page);lv_obj_set_size(card,lv_pct(100),LV_SIZE_CONTENT);style_surface_panel(card,12);
  lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(card,14,0);
  app_system_labels[tab]=lv_label_create(card);lv_obj_set_width(app_system_labels[tab],lv_pct(100));
  lv_label_set_long_mode(app_system_labels[tab],LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(app_system_labels[tab],&lv_font_montserrat_16,0);
  lv_obj_t *button=lv_btn_create(card);app_system_buttons[tab]=button;lv_obj_set_size(button,220,48);
  app_bind_adapter(button,app_system_reboot_cb,LV_EVENT_CLICKED,(void *)(intptr_t)tab,tab==0?"emu.phase46.system.reboot.grove":"emu.phase46.system.reboot.mbus");
  label=lv_label_create(button);lv_label_set_text(label,"Simulate reboot");lv_obj_center(label);
 }
 app_system_tick();
}
static void internal_tile_event_cb(lv_event_t *e){
 const char *name=lv_event_get_user_data(e);if(!name)return;
 if(!strcmp(name,"Module Status"))app_system_show();
 else if(!strcmp(name,"Settings")){app_system_close_page();show_settings_page();}
 else if(!strcmp(name,"Ad Hoc Portal"))show_adhoc_portal_page();
}
/* Emulator-only module status, styled with the native INTERNAL tile/card helpers. */
static void show_internal_tiles(void)
{
    ESP_LOGI(TAG, "Showing INTERNAL tiles");

    if (!internal_container) {
        ESP_LOGE(TAG, "INTERNAL container not initialized!");
        return;
    }

    app_system_close_page();
    // Hide settings page if visible, show tiles
    if (internal_settings_page) lv_obj_add_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);

    if (internal_tiles) {
        // Already exists, just show it
        lv_obj_clear_flag(internal_tiles, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Create tiles inside internal container
    internal_tiles = lv_obj_create(internal_container);
    lv_obj_set_size(internal_tiles, lv_pct(100), lv_pct(100));
    lv_obj_align(internal_tiles, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(internal_tiles, ui_bg_color(), 0);
    lv_obj_set_style_border_width(internal_tiles, 0, 0);
    lv_obj_set_style_radius(internal_tiles, 0, 0);
    lv_obj_set_style_pad_all(internal_tiles, 12, 0);
    lv_obj_set_style_pad_gap(internal_tiles, 10, 0);
    lv_obj_set_flex_flow(internal_tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(internal_tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style_scrollable_tile_grid(internal_tiles);

    // Create 2 tiles for INTERNAL tab
    create_tile(internal_tiles, LV_SYMBOL_SETTINGS, "Settings", COLOR_MATERIAL_PURPLE, internal_tile_event_cb, "Settings");
    create_tile(internal_tiles, LV_SYMBOL_WIFI, "Ad Hoc\nPortal & Karma", COLOR_MATERIAL_ORANGE, internal_tile_event_cb, "Ad Hoc Portal");

    create_tile(internal_tiles, LV_SYMBOL_LIST, "Module\nStatus", COLOR_MATERIAL_CYAN, internal_tile_event_cb, "Module Status");

    // Ensure tiles are visible after creation (fixes initial display issue)
    lv_obj_clear_flag(internal_tiles, LV_OBJ_FLAG_HIDDEN);
}
