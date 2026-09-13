/* S18 observes real native dialogs and portable module settings. */
EMSCRIPTEN_KEEPALIVE void emu_settings_s18_state(void){
 EM_ASM({globalThis.emulatorSettingsS18=({scanPopup:!!$0,redPage:!!$1,disclaimer:!!$2,redTeam:!!$3,min:$4,max:$5,vendor:!!$6,minInput:$7,maxInput:$8});},
 app_story_visible(scan_time_popup_obj),app_story_visible(red_team_page),app_story_visible(red_team_disclaimer_popup),enable_red_team,
 app_min[TAB_GROVE],app_max[TAB_GROVE],app_vendor[TAB_GROVE],
 scan_time_grove_min_spinbox?lv_spinbox_get_value(scan_time_grove_min_spinbox):-1,
 scan_time_grove_max_spinbox?lv_spinbox_get_value(scan_time_grove_max_spinbox):-1);
}
