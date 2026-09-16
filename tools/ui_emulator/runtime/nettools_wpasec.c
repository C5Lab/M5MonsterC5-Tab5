/* Retained native popup and credential controls; offline asynchronous task boundary.
 * Networks come from the module simulation. Typed passwords remain in native memory
 * only; neither passwords nor SSIDs are passed to JavaScript or transport. */
static tab_context_t *app_wpasec_pending[4];
static int app_wpasec_jobs[4];
static int app_wpasec_stage[4];
static wifi_network_t app_wpasec_networks[4][MAX_NETWORKS];

EM_JS(int, app_wpasec_scan_start, (int tab, char *error, int size), {
    try { return emulatorDevice.device.scan(emulatorDevice.module(tab)); }
    catch(e) { stringToUTF8(String(e.message), error, size); return 0; }
});
EM_JS(int, app_wpasec_scan_state, (int id), {
    const j=emulatorDevice.device.job(id);
    return j?.state==='running'?0:j?.state==='completed'?1:-1;
});
EM_JS(int, app_wpasec_scan_count, (int tab), {
    return emulatorDevice.device.snapshot(emulatorDevice.module(tab)).networks.length;
});
EM_JS(void, app_wpasec_scan_row, (int tab,int index,char *out,int size), {
    const n=emulatorDevice.device.snapshot(emulatorDevice.module(tab)).networks[index];
    const quote=v=>'"'+String(v==null?"":v).replaceAll('"','""')+'"';
    stringToUTF8(n?[n.index,n.ssid,n.vendor,n.bssid,n.channel,n.security,n.rssi,n.band].map(quote).join(','):"",out,size);
});

EM_JS(int, app_wpasec_start, (int tab, char *error, int size), {
    try {
        return emulatorDevice.device.toolStart(emulatorDevice.module(tab), 'wpasec',
            {outcome: globalThis.emulatorWpasecOutcome || 'success'});
    } catch (e) { stringToUTF8(String(e.message), error, size); return 0; }
});

EM_JS(void, app_wpasec_cancel, (int id), {
    emulatorDevice.device.cancel(id);
});

EM_JS(int, app_wpasec_poll, (int id, char *text, int size), {
    const job = emulatorDevice.device.job(id);
    if (!job) { stringToUTF8('Offline simulation: operation reset.', text, size); return 1; }
    if (job.state === 'running') {
        stringToUTF8('Offline simulation: uploading demo files... ' +
            Math.round(job.progress * 100) + '%\nNo files sent.', text, size);
        return 0;
    }
    const r = job.result || {};
    let message = job.state === 'completed' ? 'Simulated upload complete!' :
        'Simulated upload ' + job.state + (job.error ? ': ' + job.error : "");
    message += '\nUploaded: ' + (r.uploaded || 0) + '  Skipped: ' + (r.skipped || 0) +
        '  Failed: ' + (r.failed || 0);
    for (const file of (r.files || []).slice(0, 4))
        message += '\n' + (file.name || file.path || 'Demo capture') + ': ' + file.status;
    stringToUTF8(message + '\nOffline simulation. No files sent.', text, size);
    return 1;
});

/* Native hidden-SSID dialog with only its hardware lookup removed. */
static void show_hidden_ssid_popup(hidden_ssid_callback_t callback)
{
    if (hidden_ssid_popup_overlay) {
        close_hidden_ssid_popup();
    }
    hidden_ssid_on_confirm = callback;

    evil_twin_entry_count = 0;
    memset(evil_twin_entries, 0, sizeof(evil_twin_entries));

    /* Offline adapter: no saved-password lookup or transport reads. */
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    hidden_ssid_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(hidden_ssid_popup_overlay);
    lv_obj_set_size(hidden_ssid_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(hidden_ssid_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(hidden_ssid_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(hidden_ssid_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hidden_ssid_popup_overlay, LV_OBJ_FLAG_CLICKABLE);

    hidden_ssid_popup = lv_obj_create(hidden_ssid_popup_overlay);
    lv_obj_set_size(hidden_ssid_popup, 500, LV_SIZE_CONTENT);
    lv_obj_center(hidden_ssid_popup);
    lv_obj_set_style_bg_color(hidden_ssid_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(hidden_ssid_popup, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(hidden_ssid_popup, 2, 0);
    lv_obj_set_style_radius(hidden_ssid_popup, 16, 0);
    lv_obj_set_style_shadow_width(hidden_ssid_popup, 30, 0);
    lv_obj_set_style_shadow_color(hidden_ssid_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(hidden_ssid_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(hidden_ssid_popup, 20, 0);
    lv_obj_set_flex_flow(hidden_ssid_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hidden_ssid_popup, 12, 0);
    lv_obj_clear_flag(hidden_ssid_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hidden_ssid_popup);
    lv_label_set_text(title, LV_SYMBOL_EYE_CLOSE " Hidden Network");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);

    lv_obj_t *subtitle = lv_label_create(hidden_ssid_popup);
    lv_label_set_text(subtitle, "Enter or select SSID:");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xCCCCCC), 0);

    if (evil_twin_entry_count > 0) {
        char dropdown_opts[1024] = {0};
        int pos = 0;
        for (int i = 0; i < evil_twin_entry_count; i++) {
            if (i > 0) {
                dropdown_opts[pos++] = '\n';
            }
            int remain = (int)sizeof(dropdown_opts) - pos - 1;
            if (remain <= 0) break;
            int written = snprintf(dropdown_opts + pos, remain, "%s", evil_twin_entries[i].ssid);
            if (written > 0) pos += written;
        }

        hidden_ssid_dropdown = lv_dropdown_create(hidden_ssid_popup);
        lv_obj_set_width(hidden_ssid_dropdown, lv_pct(100));
        lv_dropdown_set_options(hidden_ssid_dropdown, dropdown_opts);
        lv_obj_set_style_bg_color(hidden_ssid_dropdown, lv_color_hex(0x2D2D2D), 0);
        lv_obj_set_style_text_color(hidden_ssid_dropdown, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(hidden_ssid_dropdown, COLOR_MATERIAL_ORANGE, 0);
        lv_obj_set_style_border_width(hidden_ssid_dropdown, 1, 0);
        lv_obj_set_style_text_font(hidden_ssid_dropdown, &lv_font_montserrat_16, 0);

        lv_obj_t *list = lv_dropdown_get_list(hidden_ssid_dropdown);
        if (list) {
            lv_obj_set_style_bg_color(list, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_text_color(list, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_border_color(list, COLOR_MATERIAL_ORANGE, 0);
            lv_obj_set_style_text_font(list, &lv_font_montserrat_16, 0);
        }

        app_bind_adapter(hidden_ssid_dropdown, hidden_ssid_dropdown_changed_cb, LV_EVENT_VALUE_CHANGED, NULL, "emu.phase4.hidden_ssid.hidden_ssid_dropdown.lv_event_value_changed");
    }

    hidden_ssid_textarea = lv_textarea_create(hidden_ssid_popup);
    lv_obj_set_size(hidden_ssid_textarea, lv_pct(100), 44);
    lv_textarea_set_one_line(hidden_ssid_textarea, true);
    lv_textarea_set_max_length(hidden_ssid_textarea, 32);
    lv_textarea_set_placeholder_text(hidden_ssid_textarea, "Type SSID here...");
    lv_obj_set_style_bg_color(hidden_ssid_textarea, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(hidden_ssid_textarea, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width(hidden_ssid_textarea, 1, 0);
    lv_obj_set_style_text_color(hidden_ssid_textarea, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(hidden_ssid_textarea, &lv_font_montserrat_16, 0);
    app_bind_adapter(hidden_ssid_textarea, hidden_ssid_textarea_focus_cb, LV_EVENT_CLICKED, NULL, "emu.phase4.hidden_ssid.hidden_ssid_textarea.lv_event_clicked");
    app_bind_adapter(hidden_ssid_textarea, hidden_ssid_textarea_focus_cb, LV_EVENT_FOCUSED, NULL, "emu.phase4.hidden_ssid.hidden_ssid_textarea.lv_event_focused");

    if (evil_twin_entry_count > 0) {
        char first_ssid[64];
        lv_dropdown_get_selected_str(hidden_ssid_dropdown, first_ssid, sizeof(first_ssid));
        lv_textarea_set_text(hidden_ssid_textarea, first_ssid);
    }

    lv_obj_t *btn_row = lv_obj_create(hidden_ssid_popup);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 15, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 120, 44);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    app_bind_adapter(cancel_btn, hidden_ssid_cancel_btn_cb, LV_EVENT_CLICKED, NULL, "emu.phase4.hidden_ssid.cancel_btn.lv_event_clicked");
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_lbl);

    lv_obj_t *confirm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(confirm_btn, 120, 44);
    lv_obj_set_style_bg_color(confirm_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(confirm_btn, 8, 0);
    app_bind_adapter(confirm_btn, hidden_ssid_confirm_btn_cb, LV_EVENT_CLICKED, NULL, "emu.phase4.hidden_ssid.confirm_btn.lv_event_clicked");
    lv_obj_t *confirm_lbl = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_lbl, "Confirm");
    lv_obj_set_style_text_font(confirm_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(confirm_lbl);

    hidden_ssid_keyboard = lv_keyboard_create(hidden_ssid_popup_overlay);
    lv_obj_set_size(hidden_ssid_keyboard, lv_pct(100), 260);
    lv_obj_align(hidden_ssid_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    style_on_screen_keyboard(hidden_ssid_keyboard);
    lv_keyboard_set_textarea(hidden_ssid_keyboard, hidden_ssid_textarea);
    lv_obj_add_flag(hidden_ssid_keyboard, LV_OBJ_FLAG_HIDDEN);
    app_bind_adapter(hidden_ssid_keyboard, hidden_ssid_keyboard_ready_cb, LV_EVENT_READY, NULL, "emu.phase4.hidden_ssid.hidden_ssid_keyboard.lv_event_ready");
    app_bind_adapter(hidden_ssid_keyboard, hidden_ssid_keyboard_ready_cb, LV_EVENT_CANCEL, NULL, "emu.phase4.hidden_ssid.hidden_ssid_keyboard.lv_event_cancel");
}

/* UI-only extraction of the network list from main/main.c wpasec_upload_task. */
static void app_wpasec_network_list(tab_context_t *ctx, int tab, int wpasec_net_count)
{
    wifi_network_t *wpasec_nets = app_wpasec_networks[tab];
        // Create scrollable network list inside popup
        if (ctx->wpasec_popup) {
            ctx->wpasec_network_list = lv_obj_create(ctx->wpasec_popup);
            lv_obj_set_size(ctx->wpasec_network_list, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(ctx->wpasec_network_list, 1);
            lv_obj_set_style_bg_color(ctx->wpasec_network_list, lv_color_hex(0x111122), 0);
            lv_obj_set_style_border_width(ctx->wpasec_network_list, 0, 0);
            lv_obj_set_style_radius(ctx->wpasec_network_list, 8, 0);
            lv_obj_set_style_pad_all(ctx->wpasec_network_list, 6, 0);
            lv_obj_set_flex_flow(ctx->wpasec_network_list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(ctx->wpasec_network_list, 4, 0);

            // Move network list before the close button (close button is last child)
            lv_obj_move_to_index(ctx->wpasec_network_list, -2);

            for (int i = 0; i < wpasec_net_count; i++) {
                wifi_network_t *net = &wpasec_nets[i];

                lv_obj_t *row = lv_obj_create(ctx->wpasec_network_list);
                lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
                lv_obj_set_style_pad_all(row, 8, 0);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_radius(row, 8, 0);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_style_pad_row(row, 2, 0);
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

                // SSID label
                lv_obj_t *ssid_lbl = lv_label_create(row);
                if (strlen(net->ssid) > 0) {
                    lv_label_set_text(ssid_lbl, net->ssid);
                } else {
                    lv_label_set_text(ssid_lbl, "(Hidden)");
                }
                lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);

                // Info line
                lv_obj_t *info_lbl = lv_label_create(row);
                lv_label_set_text_fmt(info_lbl, "%s | %s | %s | %d dBm",
                                      net->bssid, net->band, net->security, net->rssi);
                lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);

                // Store network pointer in the static array for callback
                char binding[100];
                snprintf(binding, sizeof(binding), "emu.phase4.wpasec.network.%d.%d.pick", tab, i);
                app_bind_adapter(row, wpasec_network_row_click_cb, LV_EVENT_CLICKED,
                                 (void *)&wpasec_nets[i], binding);
            }

            // Manual entry option at the bottom
            lv_obj_t *other_row = lv_obj_create(ctx->wpasec_network_list);
            lv_obj_set_size(other_row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(other_row, 8, 0);
            lv_obj_set_style_bg_color(other_row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(other_row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(other_row, 0, 0);
            lv_obj_set_style_radius(other_row, 8, 0);
            lv_obj_set_flex_flow(other_row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(other_row, 2, 0);
            lv_obj_add_flag(other_row, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t *other_ssid_lbl = lv_label_create(other_row);
            lv_label_set_text(other_ssid_lbl, "Other");
            lv_obj_set_style_text_font(other_ssid_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(other_ssid_lbl, lv_color_hex(0xFFFFFF), 0);

            lv_obj_t *other_info_lbl = lv_label_create(other_row);
            lv_label_set_text(other_info_lbl, "Enter SSID and password manually");
            lv_obj_set_style_text_font(other_info_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(other_info_lbl, lv_color_hex(0x888888), 0);

            app_bind_adapter(other_row, wpasec_network_row_click_cb, LV_EVENT_CLICKED,
                (void *)wpasec_other_ssid_user_data, "emu.phase4.wpasec.other.pick");
        }
}

static void wpasec_upload_task(void *arg)
{
    tab_context_t *ctx = arg;
    if (!ctx || !ctx->wpasec_popup) return;
    int tab = tab_id_for_ctx(ctx);
    if (app_wpasec_jobs[tab]) app_wpasec_cancel(app_wpasec_jobs[tab]);
    app_wpasec_jobs[tab] = 0;
    app_wpasec_pending[tab] = ctx;
    memset(app_wpasec_networks[tab], 0, sizeof(app_wpasec_networks[tab]));
    memset(ctx->wpasec_selected_password, 0, sizeof(ctx->wpasec_selected_password));
    ctx->wpasec_task = NULL;
    char error[160] = {0};
    app_wpasec_jobs[tab] = app_wpasec_scan_start(tab, error, sizeof(error));
    app_wpasec_stage[tab] = 0;
    lv_label_set_text_fmt(ctx->wpasec_status_label, "Offline simulation: %s",
        app_wpasec_jobs[tab] ? "Scanning networks..." : error);
    if (!app_wpasec_jobs[tab]) { ctx->wpasec_task_running = false; app_wpasec_pending[tab] = NULL; }

}

static void app_wpasec_tick(double now)
{
    (void)now;
    for (int tab = 0; tab < 4; tab++) {
        tab_context_t *ctx = app_wpasec_pending[tab];
        if (!ctx) continue;
        if (!ctx->wpasec_task_running || !ctx->wpasec_popup_overlay ||
            !lv_obj_is_valid(ctx->wpasec_popup_overlay)) {
            if (app_wpasec_jobs[tab]) app_wpasec_cancel(app_wpasec_jobs[tab]);
            app_wpasec_jobs[tab] = 0;
            app_wpasec_pending[tab] = NULL;
            memset(ctx->wpasec_selected_password, 0, sizeof(ctx->wpasec_selected_password));
            continue;
        }
        char status[768] = {0};
        if (app_wpasec_stage[tab] == 0) {
            int state = app_wpasec_scan_state(app_wpasec_jobs[tab]);
            if (!state) continue;
            app_wpasec_jobs[tab] = 0;
            if (state < 0) {
                lv_label_set_text(ctx->wpasec_status_label, "Offline simulation: scan cancelled or failed. Close and retry.");
                ctx->wpasec_task_running = false;
                app_wpasec_pending[tab] = NULL;
                continue;
            }
            int count = app_wpasec_scan_count(tab), parsed = 0;
            if (count > MAX_NETWORKS) count = MAX_NETWORKS;
            for (int i = 0; i < count; i++) {
                char line[512]; app_wpasec_scan_row(tab, i, line, sizeof(line));
                if (parse_network_line(line, &app_wpasec_networks[tab][parsed])) parsed++;
            }
            app_wpasec_network_list(ctx, tab, parsed);
            lv_label_set_text_fmt(ctx->wpasec_status_label,
                "Offline simulation: found %d networks - select one.\nDemo key available. No files sent.", parsed);
            app_wpasec_stage[tab] = 1;
            continue;
        }
        if (app_wpasec_stage[tab] == 1) {
            if (!ctx->wpasec_selected_ssid[0]) continue;
            bool manual = strcmp(ctx->wpasec_selected_ssid, WPASEC_OTHER_SSID) == 0;
            if (ctx->wpasec_network_list) { lv_obj_del(ctx->wpasec_network_list); ctx->wpasec_network_list = NULL; }
            if (manual) ctx->wpasec_selected_ssid[0] = '\0';
            lv_label_set_text_fmt(ctx->wpasec_status_label,
                "Offline simulation: %s\nEnter optional demo password and Connect.\nCredentials stay in memory; no files sent.",
                manual ? "Enter SSID" : ctx->wpasec_selected_ssid);
            wpasec_create_credentials_prompt(ctx, manual);
            app_wpasec_stage[tab] = 2;
            continue;
        }
        if (!app_wpasec_jobs[tab]) {
            if (!ctx->wpasec_connect_ready) continue;
            ctx->wpasec_connect_ready = false;
            /* Secrets are never needed, including when manual SSID validation fails. */
            memset(ctx->wpasec_selected_password, 0, sizeof(ctx->wpasec_selected_password));
            if (ctx->wpasec_password_input) lv_textarea_set_text(ctx->wpasec_password_input, "");
            if (!ctx->wpasec_selected_ssid[0]) {
                lv_label_set_text(ctx->wpasec_status_label, "Offline simulation: enter SSID.");
                continue;
            }
            app_wpasec_jobs[tab] = app_wpasec_start(tab, status, sizeof(status));
            if (!app_wpasec_jobs[tab]) {
                lv_label_set_text_fmt(ctx->wpasec_status_label, "Offline simulation: %s\nRetry with Connect.", status);
                continue;
            }
            if (ctx->wpasec_connect_btn) lv_obj_add_state(ctx->wpasec_connect_btn, LV_STATE_DISABLED);
            if (ctx->wpasec_password_input) lv_obj_add_state(ctx->wpasec_password_input, LV_STATE_DISABLED);
        }
        if (app_wpasec_poll(app_wpasec_jobs[tab], status, sizeof(status))) {
            ctx->wpasec_task_running = false;
            ctx->wpasec_task = NULL;
            app_wpasec_jobs[tab] = 0;
            app_wpasec_pending[tab] = NULL;
        }
        if (ctx->wpasec_status_label) lv_label_set_text(ctx->wpasec_status_label, status);
    }
}
