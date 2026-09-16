/* Read-only evidence for Time / Transfer Speed guided stories. */
EM_JS(void,app_s20_publish,(int open,int baud_open,int h24,int dst,int show,int baud,double now,const char *status,const char *clock,const char *rollers,int visible),{
 globalThis.emulatorSettingsS20=({timeOpen:!!open,baudOpen:!!baud_open,h24:!!h24,dst:!!dst,show:!!show,baud:baud,now:now,status:UTF8ToString(status),clock:UTF8ToString(clock),rollers:UTF8ToString(rollers),clockVisible:!!visible,offset:emulatorSettings.get("rtc_offset",-new Date().getTimezoneOffset()*60000)});
});
EMSCRIPTEN_KEEPALIVE void emu_settings_s20_state(void){
 char rollers[80]="";
 if(time_popup_overlay&&time_year_roller)snprintf(rollers,sizeof(rollers),"%d,%d,%d,%d,%d",RTC_PICKER_YEAR_MIN+lv_roller_get_selected(time_year_roller),1+lv_roller_get_selected(time_month_roller),1+lv_roller_get_selected(time_day_roller),lv_roller_get_selected(time_hour_roller),lv_roller_get_selected(time_min_roller));
 app_s20_publish(time_popup_overlay!=NULL,ft_baud_popup_overlay!=NULL,clock_24h,clock_dst,clock_show,janos_ft_baud,app_clock_now(),time_popup_status?lv_label_get_text(time_popup_status):"",status_clock_label?lv_label_get_text(status_clock_label):"",rollers,status_clock_label&&!lv_obj_has_flag(status_clock_label,LV_OBJ_FLAG_HIDDEN));
}
