/* Native SD Admin form adapted for offline simulation; credentials remain in C. */
static int app_sd_admin_job;
static int app_queue2_sd_job,app_queue2_sd_stopped;
EM_JS(int, app_sd_admin_connected, (int tab), {
 if(tab!==0&&tab!==2)return 0;
 return emulatorDevice.device.snapshot(emulatorDevice.module(tab)).connected ? 1 : 0;
});
EM_JS(int, app_sd_admin_start_js, (int tab, char *error, int size), {
 try { return emulatorDevice.device.systemStart(emulatorDevice.module(tab),'sd_admin',{}); }
 catch(e) { stringToUTF8(e.message,error,size); return 0; }
});
EM_JS(int, app_sd_admin_poll_js, (int id, char *error, int size), {
 const j=emulatorDevice.device.job(id);
 if(!j) { stringToUTF8('Simulation reset.',error,size); return 0; }
 if(j.state!=='running') { stringToUTF8(j.error||'Simulation stopped.',error,size); return 0; }
 return 1;
});
static tab_id_t sd_admin_resolve_target_tab(void) {
 if(!tab_is_internal(sd_admin_target_tab)&&app_sd_admin_connected(sd_admin_target_tab)) return sd_admin_target_tab;
 if(app_sd_admin_connected(TAB_GROVE))return TAB_GROVE;
 if(app_sd_admin_connected(TAB_USB))return TAB_USB;
 if(app_sd_admin_connected(TAB_MBUS))return TAB_MBUS;
 return TAB_INTERNAL;
}
static void sd_admin_stop_cb(lv_event_t *e) {
 (void)e;
 if(app_sd_admin_job)app_model_cancel(app_sd_admin_job);
 if(app_sd_admin_job)app_queue2_sd_stopped=app_sd_admin_job;
 app_sd_admin_job=0;
 tab_context_t *ctx=&internal_ctx;
 ctx->sd_admin_task=NULL;
 memset(ctx->sd_admin_active_password,0,sizeof(ctx->sd_admin_active_password));
 if(ctx->sd_admin_password_input&&lv_obj_is_valid(ctx->sd_admin_password_input))lv_textarea_set_text(ctx->sd_admin_password_input, "");
 if(ctx->sd_admin_keyboard&&lv_obj_is_valid(ctx->sd_admin_keyboard))lv_obj_add_flag(ctx->sd_admin_keyboard,LV_OBJ_FLAG_HIDDEN);
 sd_admin_set_status(ctx,SD_ADMIN_STOPPED,"Offline simulation stopped. No network service is active.");
 sd_admin_hide_qr(ctx);sd_admin_refresh_ui(ctx);
}
static void app_sd_admin_deleted(lv_event_t *e) {
 (void)e;
 if(app_sd_admin_job)app_model_cancel(app_sd_admin_job);
 app_sd_admin_job=0;
 tab_context_t *ctx=&internal_ctx;
 ctx->sd_admin_task=NULL;ctx->sd_admin_state=SD_ADMIN_STOPPED;
 ctx->sd_admin_status[0]=0;
 memset(ctx->sd_admin_active_password,0,sizeof(ctx->sd_admin_active_password));
 ctx->sd_admin_page=ctx->sd_admin_password_input=ctx->sd_admin_keyboard=NULL;
 ctx->sd_admin_start_btn=ctx->sd_admin_quick_start_btn=ctx->sd_admin_stop_btn=NULL;
 ctx->sd_admin_status_label=ctx->sd_admin_command_label=ctx->sd_admin_qr_section=ctx->sd_admin_qr=NULL;
 if(ctx->sd_admin_leave_overlay&&lv_obj_is_valid(ctx->sd_admin_leave_overlay))lv_obj_del(ctx->sd_admin_leave_overlay);
 ctx->sd_admin_leave_overlay=NULL;
}
static void sd_admin_start_with_password(tab_context_t *ctx,const char *password,bool save_to_nvs) {
 (void)save_to_nvs;
 if(!ctx||ctx->sd_admin_state!=SD_ADMIN_STOPPED||!password)return;
 size_t n=strlen(password);
 if(n<8||n>63||strchr(password,' ')||strchr(password,'\r')||strchr(password,'\n')) {
  sd_admin_set_status(ctx,SD_ADMIN_STOPPED,"Enter a WPA2 password with 8-63 characters and no spaces.");
  sd_admin_refresh_ui(ctx);return;
 }
 tab_id_t tab=sd_admin_resolve_target_tab();char error[160]={0};
 if(tab_is_internal(tab))snprintf(error,sizeof(error),"No simulated JanOS device is connected.");
 else app_sd_admin_job=app_sd_admin_start_js(tab,error,sizeof(error));
 if(!app_sd_admin_job) {
  sd_admin_set_status(ctx,SD_ADMIN_STOPPED,error);sd_admin_refresh_ui(ctx);return;
 }
 app_queue2_sd_job=app_sd_admin_job;
 if(ctx->sd_admin_keyboard)lv_obj_add_flag(ctx->sd_admin_keyboard,LV_OBJ_FLAG_HIDDEN);
 if(ctx->sd_admin_password_input) {
  lv_textarea_set_text(ctx->sd_admin_password_input,"");
 }
 sd_admin_hide_qr(ctx);
 sd_admin_set_status(ctx,SD_ADMIN_RUNNING,"Offline simulation running. No AP, HTTP service or QR connection exists.");
 sd_admin_refresh_ui(ctx);
}
static void sd_admin_leave_keep_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)return;
 if(ctx->sd_admin_leave_overlay)lv_obj_del(ctx->sd_admin_leave_overlay);
 ctx->sd_admin_leave_overlay=NULL;
}
static void sd_admin_leave_stop_cb(lv_event_t *e) {
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)return;
 if(ctx->sd_admin_leave_overlay)lv_obj_del(ctx->sd_admin_leave_overlay);
 ctx->sd_admin_leave_overlay=NULL;
 sd_admin_stop_cb(NULL);sd_admin_return_to_settings(ctx);
}
static void app_sd_admin_tick(void) {
 if(!app_sd_admin_job)return;
 tab_context_t *ctx=&internal_ctx;char error[160]={0};
 if(!ctx->sd_admin_page||!lv_obj_is_valid(ctx->sd_admin_page)) {
  app_model_cancel(app_sd_admin_job);app_sd_admin_job=0;return;
 }
 if(app_sd_admin_poll_js(app_sd_admin_job,error,sizeof(error)))return;
 app_sd_admin_job=0;ctx->sd_admin_task=NULL;
 sd_admin_set_status(ctx,SD_ADMIN_STOPPED,error);sd_admin_hide_qr(ctx);sd_admin_refresh_ui(ctx);
 if(ctx->sd_admin_leave_overlay)lv_obj_del(ctx->sd_admin_leave_overlay);
 ctx->sd_admin_leave_overlay=NULL;
}

static void sd_admin_back_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = &internal_ctx;
    if (!ctx) return;
    if (ctx->sd_admin_state != SD_ADMIN_RUNNING) {
        sd_admin_stop_cb(NULL);
        sd_admin_return_to_settings(ctx);
        return;
    }
    if (ctx->sd_admin_leave_overlay) return;
    lv_obj_t *container = internal_container;
    ctx->sd_admin_leave_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->sd_admin_leave_overlay);
    lv_obj_set_size(ctx->sd_admin_leave_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->sd_admin_leave_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->sd_admin_leave_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->sd_admin_leave_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dialog = lv_obj_create(ctx->sd_admin_leave_overlay);
    lv_obj_set_size(dialog, 520, 260);
    lv_obj_center(dialog);
    style_surface_panel(dialog, 14);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dialog, 22, 0);
    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, "SD Admin simulation is running");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_t *text = lv_label_create(dialog);
    lv_label_set_text(text, "Stay here or stop the simulation and go back?");
    lv_obj_set_style_text_color(text, ui_muted_color(), 0);
    lv_obj_t *row = lv_obj_create(dialog);
    lv_obj_set_size(row, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_t *keep = lv_btn_create(row);
    lv_obj_set_size(keep, 210, 48);
    lv_obj_set_style_bg_color(keep, COLOR_MATERIAL_GREEN, 0);
    app_bind_adapter(keep,sd_admin_leave_keep_cb,LV_EVENT_CLICKED,ctx, "emu.phase4.sd_admin_back_cb.keep.lv_event_clicked");
    lv_obj_t *keep_label = lv_label_create(keep);
    lv_label_set_text(keep_label, "Stay here");
    lv_obj_center(keep_label);
    lv_obj_t *stop = lv_btn_create(row);
    lv_obj_set_size(stop, 150, 48);
    style_danger_button(stop);
    app_bind_adapter(stop,sd_admin_leave_stop_cb,LV_EVENT_CLICKED,ctx, "emu.phase4.sd_admin_back_cb.stop.lv_event_clicked");
    lv_obj_t *stop_label = lv_label_create(stop);
    lv_label_set_text(stop_label, "Stop and back");
    lv_obj_center(stop_label);
}

static void show_sd_admin_page(void)
{
    tab_context_t *ctx = &internal_ctx;
    lv_obj_t *container = internal_container;
    if (!ctx || !container) return;
    if (internal_settings_page) lv_obj_add_flag(internal_settings_page, LV_OBJ_FLAG_HIDDEN);
    hide_all_pages(ctx);
    if (ctx->sd_admin_page) {
        lv_obj_clear_flag(ctx->sd_admin_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->sd_admin_page;
        sd_admin_refresh_ui(ctx);
        return;
    }

    ctx->sd_admin_page = lv_obj_create(container);
    lv_obj_set_size(ctx->sd_admin_page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->sd_admin_page, ui_bg_color(), 0);
    lv_obj_set_style_border_width(ctx->sd_admin_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->sd_admin_page, 18, 0);
    lv_obj_set_flex_flow(ctx->sd_admin_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->sd_admin_page, 14, 0);

    lv_obj_t *header = lv_obj_create(ctx->sd_admin_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 14, 0);
    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_size(back, 72, 60);
    style_back_nav_button(back);
    app_bind_adapter(back,sd_admin_back_cb,LV_EVENT_CLICKED,NULL, "emu.phase4.show_sd_admin_page.back.lv_event_clicked");
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "SD Card Admin / Simulation");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_GREEN, 0);

    lv_obj_t *info = lv_obj_create(ctx->sd_admin_page);
    lv_obj_set_size(info, lv_pct(100), LV_SIZE_CONTENT);
    style_surface_panel(info, 10);
    lv_obj_set_style_pad_all(info, 14, 0);
    lv_obj_t *info_text = lv_label_create(info);
    lv_label_set_text(info_text,
        "Offline simulation: SD Admin lifecycle only. No Wi-Fi AP or HTTP service is created.\n"
        "Enter a temporary password or use Quick Start. Passwords are never saved or sent.");
    lv_label_set_long_mode(info_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_text, lv_pct(100));
    lv_obj_set_style_text_font(info_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_text, ui_text_color(), 0);

    lv_obj_t *form = lv_obj_create(ctx->sd_admin_page);
    lv_obj_set_size(form, lv_pct(100), LV_SIZE_CONTENT);
    style_surface_panel(form, 10);
    lv_obj_set_style_pad_all(form, 14, 0);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(form, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(form, 8, 0);
    ctx->sd_admin_password_input = lv_textarea_create(form);
    lv_obj_set_size(ctx->sd_admin_password_input, 520, 48);
    lv_textarea_set_one_line(ctx->sd_admin_password_input, true);
    lv_textarea_set_max_length(ctx->sd_admin_password_input, 63);
    lv_textarea_set_password_mode(ctx->sd_admin_password_input, true);
    lv_textarea_set_password_show_time(ctx->sd_admin_password_input, WPASEC_PASSWORD_SHOW_MS);
    lv_textarea_set_placeholder_text(ctx->sd_admin_password_input, "Enter WPA2 password");
    app_bind_adapter(ctx->sd_admin_password_input,sd_admin_password_focus_cb,LV_EVENT_CLICKED,NULL, "emu.phase4.show_sd_admin_page.sd_admin_password_input.lv_event_clicked");
    app_bind_adapter(ctx->sd_admin_password_input,sd_admin_password_focus_cb,LV_EVENT_FOCUSED,NULL, "emu.phase4.show_sd_admin_page.sd_admin_password_input.lv_event_focused");
    app_bind_adapter(ctx->sd_admin_password_input,sd_admin_password_insert_cb,LV_EVENT_INSERT,NULL, "emu.phase4.show_sd_admin_page.sd_admin_password_input.lv_event_insert");
    lv_obj_t *show_password_checkbox = lv_checkbox_create(form);
    lv_checkbox_set_text(show_password_checkbox, "Show password");
    lv_obj_set_style_text_font(show_password_checkbox, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(show_password_checkbox, ui_text_color(), 0);
    app_bind_adapter(show_password_checkbox,sd_admin_show_password_toggle_cb,LV_EVENT_VALUE_CHANGED,ctx->sd_admin_password_input, "emu.phase4.show_sd_admin_page.show_password_checkbox.lv_event_value_changed");

    lv_obj_t *actions = lv_obj_create(ctx->sd_admin_page);
    lv_obj_set_size(actions, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 14, 0);
    ctx->sd_admin_start_btn = lv_btn_create(actions);
    lv_obj_set_size(ctx->sd_admin_start_btn, 220, 52);
    lv_obj_set_style_bg_color(ctx->sd_admin_start_btn, COLOR_MATERIAL_GREEN, 0);
    app_bind_adapter(ctx->sd_admin_start_btn,sd_admin_start_cb,LV_EVENT_CLICKED,NULL, "emu.phase4.show_sd_admin_page.sd_admin_start_btn.lv_event_clicked");
    lv_obj_t *start_label = lv_label_create(ctx->sd_admin_start_btn);
    lv_label_set_text(start_label, "Start Admin Portal");
    lv_obj_center(start_label);
    ctx->sd_admin_quick_start_btn = lv_btn_create(actions);
    lv_obj_set_size(ctx->sd_admin_quick_start_btn, 150, 52);
    lv_obj_set_style_bg_color(ctx->sd_admin_quick_start_btn, COLOR_MATERIAL_TEAL, 0);
    lv_obj_set_style_bg_color(ctx->sd_admin_quick_start_btn, lv_color_hex(0x00796B), LV_STATE_PRESSED);
    app_bind_adapter(ctx->sd_admin_quick_start_btn,sd_admin_quick_start_cb,LV_EVENT_CLICKED,NULL, "emu.phase4.show_sd_admin_page.sd_admin_quick_start_btn.lv_event_clicked");
    lv_obj_t *quick_start_label = lv_label_create(ctx->sd_admin_quick_start_btn);
    lv_label_set_text(quick_start_label, "Quick Start");
    lv_obj_center(quick_start_label);
    ctx->sd_admin_stop_btn = lv_btn_create(actions);
    lv_obj_set_size(ctx->sd_admin_stop_btn, 160, 52);
    style_danger_button(ctx->sd_admin_stop_btn);
    app_bind_adapter(ctx->sd_admin_stop_btn,sd_admin_stop_cb,LV_EVENT_CLICKED,NULL, "emu.phase4.show_sd_admin_page.sd_admin_stop_btn.lv_event_clicked");
    lv_obj_t *stop_label = lv_label_create(ctx->sd_admin_stop_btn);
    lv_label_set_text(stop_label, "Stop Portal");
    lv_obj_center(stop_label);
    lv_obj_t *quick_start_info = lv_label_create(ctx->sd_admin_page);
    lv_label_set_text(quick_start_info, "Quick Start uses a simulated session.");
    lv_obj_set_style_text_font(quick_start_info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(quick_start_info, COLOR_MATERIAL_TEAL, 0);

    lv_obj_t *status_panel = lv_obj_create(ctx->sd_admin_page);
    lv_obj_set_size(status_panel, lv_pct(100), LV_SIZE_CONTENT);
    style_surface_panel(status_panel, 10);
    lv_obj_set_style_pad_all(status_panel, 12, 0);
    lv_obj_set_flex_flow(status_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(status_panel, 4, 0);
    ctx->sd_admin_status_label = lv_label_create(status_panel);
    lv_label_set_long_mode(ctx->sd_admin_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->sd_admin_status_label, lv_pct(100));
    lv_obj_set_style_text_font(ctx->sd_admin_status_label, &lv_font_montserrat_16, 0);
    ctx->sd_admin_command_label = lv_label_create(status_panel);
    lv_label_set_long_mode(ctx->sd_admin_command_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->sd_admin_command_label, lv_pct(100));
    lv_obj_set_style_text_font(ctx->sd_admin_command_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->sd_admin_command_label, ui_muted_color(), 0);
    lv_obj_add_flag(ctx->sd_admin_command_label, LV_OBJ_FLAG_HIDDEN);
    if (!ctx->sd_admin_status[0]) sd_admin_set_status(ctx, SD_ADMIN_STOPPED,
                                                       "Enter a WPA2 password or use Quick Start.");

    ctx->sd_admin_qr_section = lv_obj_create(ctx->sd_admin_page);
    lv_obj_set_size(ctx->sd_admin_qr_section, lv_pct(100), 264);
    style_surface_panel(ctx->sd_admin_qr_section, 12);
    lv_obj_set_style_pad_all(ctx->sd_admin_qr_section, 10, 0);
    lv_obj_clear_flag(ctx->sd_admin_qr_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *qr_heading = lv_label_create(ctx->sd_admin_qr_section);
    lv_label_set_text(qr_heading, "Offline simulation");
    lv_obj_set_style_text_font(qr_heading, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(qr_heading, COLOR_MATERIAL_GREEN, 0);
    lv_obj_align(qr_heading, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *qr_label = lv_label_create(ctx->sd_admin_qr_section);
    lv_label_set_text(qr_label, "No network connection or QR is available.");
    lv_obj_set_style_text_font(qr_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qr_label, ui_muted_color(), 0);
    lv_obj_align(qr_label, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_add_flag(ctx->sd_admin_qr_section, LV_OBJ_FLAG_HIDDEN);

    ctx->sd_admin_keyboard = lv_keyboard_create(ctx->sd_admin_page);
    lv_obj_set_size(ctx->sd_admin_keyboard, lv_pct(100), 240);
    lv_obj_add_flag(ctx->sd_admin_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ctx->sd_admin_keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    style_on_screen_keyboard(ctx->sd_admin_keyboard);
    lv_keyboard_set_textarea(ctx->sd_admin_keyboard, ctx->sd_admin_password_input);
    app_bind_adapter(ctx->sd_admin_keyboard,sd_admin_keyboard_cb,LV_EVENT_ALL,NULL, "emu.phase4.show_sd_admin_page.sd_admin_keyboard.lv_event_all");
    lv_obj_add_flag(ctx->sd_admin_keyboard, LV_OBJ_FLAG_HIDDEN);
    ctx->current_visible_page = ctx->sd_admin_page;
    app_bind_adapter(ctx->sd_admin_page,app_sd_admin_deleted,LV_EVENT_DELETE,NULL, "emu.phase4.show_sd_admin_page.sd_admin_page.lv_event_delete");
    sd_admin_refresh_ui(ctx);
}
