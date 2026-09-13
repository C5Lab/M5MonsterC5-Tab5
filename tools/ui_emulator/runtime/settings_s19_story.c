/* Read-only evidence of native display preferences and popup lifecycle. */
EMSCRIPTEN_KEEPALIVE void emu_settings_s19_state(void){
 EM_ASM({globalThis.emulatorSettingsS19=({timeout:$0,brightness:$1,rotation:$2,activeRotation:$3,dark:!!$4,timeoutOpen:!!$5,brightnessOpen:!!$6,rotationOpen:!!$7,themeOpen:!!$8,settings:!!$9});},
 screen_timeout_setting,screen_brightness_setting,screen_rotation_setting,screen_rotation_active(),dark_mode_enabled,
 app_story_visible(screen_timeout_popup_overlay),app_story_visible(screen_brightness_popup_overlay),app_story_visible(screen_rotation_popup_overlay),app_story_visible(theme_popup_overlay),app_story_visible(internal_settings_page));
 EM_ASM({emulatorSettingsS19.dashboard=!!$0;emulatorSettingsS19.boot=$1;emulatorSettingsS19.alert=!!$2;},dashboard_enabled,boot_sound_mode,alert_sound_enabled);
}
