/* Native condition dialogs; only hardware retry scheduling is removed. */
static void show_no_board_popup(void)
{
    ESP_LOGI(TAG, "Showing 'No Board Detected' popup");

    board_detection_popup_open = true;

    lv_obj_t *scr = lv_scr_act();

    // Create modal overlay
    board_detect_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(board_detect_overlay);
    lv_obj_set_size(board_detect_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(board_detect_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(board_detect_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(board_detect_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board_detect_overlay, LV_OBJ_FLAG_CLICKABLE);

    // Create popup container
    board_detect_popup = lv_obj_create(board_detect_overlay);
    lv_obj_set_size(board_detect_popup, 400, 280);
    lv_obj_center(board_detect_popup);
    lv_obj_set_style_bg_color(board_detect_popup, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_color(board_detect_popup, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_border_width(board_detect_popup, 3, 0);
    lv_obj_set_style_radius(board_detect_popup, 16, 0);
    lv_obj_set_style_pad_all(board_detect_popup, 24, 0);
    lv_obj_set_flex_flow(board_detect_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(board_detect_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(board_detect_popup, 16, 0);
    lv_obj_clear_flag(board_detect_popup, LV_OBJ_FLAG_SCROLLABLE);

    // Warning icon
    lv_obj_t *icon = lv_label_create(board_detect_popup);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon, COLOR_MATERIAL_AMBER, 0);

    // Title
    lv_obj_t *title = lv_label_create(board_detect_popup);
    lv_label_set_text(title, "No Board Detected");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    // Subtitle
    lv_obj_t *subtitle = lv_label_create(board_detect_popup);
    lv_label_set_text(subtitle, "Offline demo: both modules disconnected");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);

    // Status label (shows retry status)
    lv_obj_t *status = lv_label_create(board_detect_popup);
    lv_label_set_text(status, "Choose Normal in Module condition to reconnect.");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x888888), 0);

    // Close button
    lv_obj_t *close_btn = lv_btn_create(board_detect_popup);
    lv_obj_set_size(close_btn, 160, 50);
    lv_obj_set_style_bg_color(close_btn, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    app_bind(close_btn, board_detect_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_no_board_popup.close_btn.clicked.7a25cc051fc0"));

    lv_obj_t *btn_label = lv_label_create(close_btn);
    lv_label_set_text(btn_label, "Continue Anyway");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_label);

    // Shell condition changes reload this offline demo.
    board_detect_retry_stop = true;
}

EM_JS(int,app_dialog_condition,(),{
 const value=new URL(location.href).searchParams.get('moduleCondition');
 if(value==='missing-board'||value==='missing-sd')for(const tab of [0,2])
  emulatorDevice.device.setModule(emulatorDevice.module(tab),value==='missing-board'?{connected:false}:{sdPresent:false});
 return ['normal','missing-board','missing-sd','version-mismatch'].indexOf(value);
});
static void app_dialog_init(void) {
 int condition=app_dialog_condition();app_system_sync_metadata();
 if(condition==1){grove_detected=uart1_detected=mbus_detected=false;show_no_board_popup();}
 if(condition==3){grove_ctx.janos_version_mismatch=true;mbus_ctx.janos_version_mismatch=true;show_version_mismatch_popup();}
}
