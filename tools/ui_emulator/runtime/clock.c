/* Virtual wall clock: UTC fields encode user-selected civil time. No host clock writes. */
EM_JS(double,app_clock_now,(void),{
 const fallback=-new Date().getTimezoneOffset()*60000;
 return Math.floor((Date.now()+emulatorSettings.get("rtc_offset",fallback))/1000);
});
EM_JS(void,app_clock_set,(double epoch),{
 emulatorSettings.set("rtc_offset",Math.round(epoch*1000-Date.now()));
});
static esp_err_t rx8130_read_time(struct tm *out,bool *power_lost) {
 if(!out)return ESP_FAIL;
 time_t epoch=(time_t)app_clock_now();gmtime_r(&epoch,out);
 if(power_lost)*power_lost=false;
 return ESP_OK;
}
static esp_err_t rx8130_set_time(const struct tm *value) {
 if(!value)return ESP_FAIL;
 struct tm input=*value,check;
 time_t epoch=timegm(&input);gmtime_r(&epoch,&check);
 if(check.tm_year!=value->tm_year||check.tm_mon!=value->tm_mon||check.tm_mday!=value->tm_mday||
    check.tm_hour!=value->tm_hour||check.tm_min!=value->tm_min||check.tm_sec!=value->tm_sec)return ESP_FAIL;
 app_clock_set((double)epoch);return ESP_OK;
}
static void update_status_clock(void) {
 if(!status_clock_label)return;
 if(!clock_show){lv_obj_add_flag(status_clock_label,LV_OBJ_FLAG_HIDDEN);return;}
 lv_obj_clear_flag(status_clock_label,LV_OBJ_FLAG_HIDDEN);
 struct tm now;rx8130_read_time(&now,NULL);
 int hour=clock_24h?now.tm_hour:(now.tm_hour%12?now.tm_hour%12:12);
 lv_label_set_text_fmt(status_clock_label,"%02d:%02d",hour,now.tm_min);
}
static void rx8130_sync_system_from_rtc(void) {update_status_clock();}
static void time_dst_switch_cb(lv_event_t *e) {
 bool on=lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED);
 if(on==clock_dst)return;
 app_clock_set(app_clock_now()+(on?3600:-3600));
 clock_dst=on;save_clock_settings_to_nvs();update_status_clock();time_popup_refresh_from_rtc();
}
