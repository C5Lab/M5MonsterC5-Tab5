/* Native UI from main/main.c; cooperative synthetic transport only.
 * app_bind identifiers preserve original firmware source line numbers. */
EM_JS(int,app_da_start_js,(int tab,const char *kind,const char *bssids,const char *mac,char *error,int size),{
 try{return emulatorDevice.device.attackStart(emulatorDevice.module(tab),UTF8ToString(kind),{bssids:UTF8ToString(bssids).split(',').filter(Boolean),targetMac:UTF8ToString(mac)||undefined});}
 catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(void,app_da_stop_js,(int id),{try{emulatorDevice.device.attackStop(id);}catch(e){}});
EM_JS(int,app_da_poll_js,(int id,char *out,int size),{
 const j=emulatorDevice.device.job(id);if(!j){stringToUTF8('Simulation reset',out,size);return -1;}
 if(j.state==='cancelled'||j.state==='failed'){stringToUTF8(j.error||j.state,out,size);return -1;}
 const r=j.result||{};stringToUTF8((j.state==='completed'?'Stopped':'Simulated')+' | '+(r.packets||0)+' packets | '+Math.floor((r.elapsedMs||0)/1000)+'s',out,size);
 return j.state==='completed'?1:0;
});
static int app_arp_story_jobs[4][4];
static struct {int id,prep,action;lv_obj_t *owner,*status,*page,*attack_page;} app_da[4];
static void app_da_cancel(tab_context_t *ctx){
 int t=tab_id_for_ctx(ctx);if(app_da[t].id)app_model_cancel(app_da[t].id);if(app_da[t].prep)app_model_cancel(app_da[t].prep);
 app_da[t].id=app_da[t].prep=0;ctx->deauth_active=false;
}
static void app_da_stop(tab_context_t *ctx){int t=tab_id_for_ctx(ctx);if(app_da[t].id)app_da_stop_js(app_da[t].id);app_da[t].id=0;ctx->deauth_active=false;}
static void app_da_deleted(lv_event_t *e){
 lv_obj_t *o=lv_event_get_target(e);tab_context_t *ctx=lv_event_get_user_data(e);int t=tab_id_for_ctx(ctx);
 if(o==app_da[t].owner){app_da_cancel(ctx);app_da[t].owner=app_da[t].status=app_da[t].attack_page=NULL;}
 if(o==app_da[t].page){app_da_cancel(ctx);app_da[t].page=NULL;ctx->arp_wifi_connected=false;ctx->arp_host_count=0;
 ctx->arp_poison_page=ctx->arp_status_label=ctx->arp_hosts_container=ctx->arp_connect_btn=ctx->arp_list_hosts_btn=ctx->arp_password_input=ctx->arp_keyboard=NULL;}
 if(o==ctx->scan_deauth_overlay)ctx->scan_deauth_overlay=ctx->scan_deauth_popup=NULL;
 if(o==ctx->arp_attack_popup_overlay)ctx->arp_attack_popup_overlay=ctx->arp_attack_popup=NULL;
 if(o==ctx->deauth_popup){ctx->deauth_popup=ctx->deauth_btn=ctx->deauth_btn_label=NULL;ctx->deauth_active=false;}
}
static bool app_da_begin(tab_context_t *ctx,const char *kind,const char *mac,const char *single){
 char bssids[2048]={0},error[192]={0};int t=tab_id_for_ctx(ctx);
 if(app_da[t].id||app_da[t].prep)return false;
 if(single&&single[0])snprintf(bssids,sizeof(bssids),"%s",single);
 else {scan_view_t v=get_scan_view(ctx);for(int i=0;i<v.sel_count;i++){int n=v.sel_indices[i];if(n>=0&&n<v.net_count){if(bssids[0])strncat(bssids,",",sizeof(bssids)-strlen(bssids)-1);strncat(bssids,v.nets[n].bssid,sizeof(bssids)-strlen(bssids)-1);}}}
 app_da[t].id=app_da_start_js(t,kind,bssids,mac?mac:"",error,sizeof(error));
 if(!strcmp(kind,"arp"))app_arp_story_jobs[t][3]=app_da[t].id;
 if(!app_da[t].id){lv_obj_t *label=!strcmp(kind,"arp")?ctx->arp_status_label:ctx->popup_open?ctx->observer_status_label:ctx->scan_status_label;if(label)lv_label_set_text(label,error);return false;}return true;
}
static void app_da_watch(tab_context_t *ctx,lv_obj_t *owner,lv_obj_t *status){int t=tab_id_for_ctx(ctx);app_da[t].owner=owner;app_da[t].status=status;app_da[t].attack_page=ctx->current_visible_page;lv_obj_add_event_cb(owner,app_da_deleted,LV_EVENT_DELETE,ctx);}
static void show_scan_deauth_popup(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    if (ctx->scan_deauth_popup != NULL) return;  // Already showing in this tab

    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    if (!app_da_begin(ctx,"deauth","",NULL)) return;
    // Create modal overlay (fills container, semi-transparent, blocks input behind)
    ctx->scan_deauth_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->scan_deauth_overlay);
    lv_obj_set_size(ctx->scan_deauth_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->scan_deauth_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->scan_deauth_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->scan_deauth_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->scan_deauth_overlay, LV_OBJ_FLAG_CLICKABLE);  // Capture clicks

    // Create popup as child of overlay
    ctx->scan_deauth_popup = lv_obj_create(ctx->scan_deauth_overlay);
    lv_obj_set_size(ctx->scan_deauth_popup, 550, 450);
    lv_obj_center(ctx->scan_deauth_popup);
    lv_obj_set_style_bg_color(ctx->scan_deauth_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->scan_deauth_popup, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(ctx->scan_deauth_popup, 2, 0);
    lv_obj_set_style_radius(ctx->scan_deauth_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->scan_deauth_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->scan_deauth_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->scan_deauth_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->scan_deauth_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->scan_deauth_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->scan_deauth_popup, 12, 0);

    // Title
    lv_obj_t *title = lv_label_create(ctx->scan_deauth_popup);
    lv_label_set_text(title, "Attacking networks:");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);

    // Scrollable container for network list
    lv_obj_t *list_cont = lv_obj_create(ctx->scan_deauth_popup);
    lv_obj_set_size(list_cont, lv_pct(100), 280);
    lv_obj_set_style_bg_color(list_cont, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_style_radius(list_cont, 8, 0);
    lv_obj_set_style_pad_all(list_cont, 12, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_cont, 8, 0);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Add each selected network to the list
    scan_view_t v = get_scan_view(ctx);
    for (int i = 0; i < v.sel_count; i++) {
        int idx = v.sel_indices[i];
        if (idx >= 0 && idx < v.net_count) {
            wifi_network_t *net = &v.nets[idx];

            // Network item container
            lv_obj_t *item = lv_obj_create(list_cont);
            lv_obj_set_size(item, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_radius(item, 6, 0);
            lv_obj_set_style_pad_all(item, 10, 0);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(item, 4, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            // SSID
            const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
            lv_obj_t *ssid_label = lv_label_create(item);
            lv_label_set_text_fmt(ssid_label, "%s %s", LV_SYMBOL_WIFI, ssid_display);
            lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);

            // BSSID, Band, Security and vendor
            lv_obj_t *info_label = lv_label_create(item);
            const char *vendor_display = strlen(net->vendor) > 0 ? net->vendor : "-";
            lv_label_set_text_fmt(info_label, "BSSID: %s | %s | %s\nVendor: %s",
                                  net->bssid, net->band, net->security, vendor_display);
            lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(info_label, lv_color_hex(0xAAAAAA), 0);
        }
    }

    // STOP button
    lv_obj_t *stop_btn = lv_btn_create(ctx->scan_deauth_popup);
    lv_obj_set_size(stop_btn, lv_pct(100), 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xCC0000), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    app_bind(stop_btn, scan_deauth_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_scan_deauth_popup.stop_btn.clicked.d63e08472e49"));

    lv_obj_t *btn_label = lv_label_create(stop_btn);
    lv_label_set_text(btn_label, "STOP ATTACK");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_18, 0);
    lv_obj_center(btn_label);
    app_da_watch(ctx,ctx->scan_deauth_overlay,title);
}

static void scan_deauth_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Deauth popup closed - sending stop command");

    // Send stop command to current tab's UART
    

    // Delete overlay (popup is child, will be deleted too)
    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->scan_deauth_overlay) {
        app_da_stop(ctx);
        lv_obj_del(ctx->scan_deauth_overlay);
        ctx->scan_deauth_overlay = NULL;
        ctx->scan_deauth_popup = NULL;
        ctx->observer_attack_return_to_observer = false;
        clear_observer_attack_override(ctx);
    }
}

static void show_deauth_popup(int network_idx, int client_idx)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;

    // Block deauth if Red Team mode is disabled
    if (!enable_red_team) {
        ESP_LOGW(TAG, "Deauth blocked - Red Team mode disabled");
        if (ctx->observer_status_label) {
            lv_label_set_text(ctx->observer_status_label, "Deauth requires Red Team mode");
            lv_obj_set_style_text_color(ctx->observer_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }

    if (client_idx < 0 || client_idx >= MAX_CLIENTS_PER_NETWORK) return;
    if (network_idx < 0 || network_idx >= ctx->observer_network_count) return;
    if (ctx->deauth_popup != NULL) return;  // Already showing a popup

    observer_network_t *net = &ctx->observer_networks[network_idx];
    if (net->clients[client_idx][0] == '\0') return;

    const char *client_mac = net->clients[client_idx];
    ESP_LOGI(TAG, "Opening deauth popup for client: %s on network: %s", client_mac, net->ssid);

    ctx->deauth_network_idx = network_idx;
    ctx->deauth_client_idx = client_idx;
    ctx->deauth_active = false;  // Not yet deauthing

    // Stop main observer timer for this context
    if (ctx->observer_timer != NULL) {
        xTimerStop(ctx->observer_timer, 0);
        ESP_LOGI(TAG, "Stopped observer timer for deauth popup");
    }

    // Create popup overlay
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;
    ctx->deauth_popup = lv_obj_create(container);
    // 520 px was drawn around a four-row attack bar. With the bar down to one
    // row the card would carry ~150 px of empty space, so let it take its
    // content instead: every child below has a fixed or content-sized height,
    // and the total lands near 360 px - inside the short landscape screen too.
    lv_obj_set_size(ctx->deauth_popup, 620, LV_SIZE_CONTENT);
    lv_obj_center(ctx->deauth_popup);
    lv_obj_set_style_bg_color(ctx->deauth_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->deauth_popup, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_border_width(ctx->deauth_popup, 2, 0);
    lv_obj_set_style_radius(ctx->deauth_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->deauth_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->deauth_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->deauth_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->deauth_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->deauth_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->deauth_popup, 12, 0);

    // Header with title and close button
    lv_obj_t *header = lv_obj_create(ctx->deauth_popup);
    lv_obj_set_size(header, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Deauth Station");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_RED, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Close button (X)
    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x555555), LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    app_bind(close_btn, deauth_btn_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)1, app_template_line("ui.show_deauth_popup.close_btn.clicked.f8a6c12f3f60"));  // 1 = close button

    lv_obj_t *close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(close_icon);

    // Network info section
    lv_obj_t *info_container = lv_obj_create(ctx->deauth_popup);
    lv_obj_set_size(info_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x0A0A1A), 0);
    lv_obj_set_style_border_width(info_container, 0, 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_set_style_pad_all(info_container, 12, 0);
    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info_container, 4, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);

    // SSID
    const char *ssid_display = strlen(net->ssid) > 0 ? net->ssid : "(Hidden)";
    lv_obj_t *ssid_label = lv_label_create(info_container);
    lv_label_set_text_fmt(ssid_label, "Network: %s", ssid_display);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xFFFFFF), 0);

    // BSSID + Channel
    lv_obj_t *bssid_label = lv_label_create(info_container);
    lv_label_set_text_fmt(bssid_label, "BSSID: %s  |  CH%d", net->bssid, net->channel);
    lv_obj_set_style_text_font(bssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bssid_label, lv_color_hex(0xAAAAAA), 0);

    lv_obj_t *vendor_label = lv_label_create(info_container);
    lv_label_set_text_fmt(vendor_label, "Vendor: %s", strlen(net->vendor) > 0 ? net->vendor : "-");
    lv_obj_set_style_text_font(vendor_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(vendor_label, lv_color_hex(0xAAAAAA), 0);

    // Client MAC (highlighted)
    lv_obj_t *client_label = lv_label_create(info_container);
    lv_label_set_text_fmt(client_label, "Station: %s", client_mac);
    lv_obj_set_style_text_font(client_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(client_label, COLOR_MATERIAL_RED, 0);

    // Deauth button (red)
    ctx->deauth_btn = lv_btn_create(ctx->deauth_popup);
    lv_obj_set_size(ctx->deauth_btn, lv_pct(100), 60);
    lv_obj_set_style_bg_color(ctx->deauth_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(ctx->deauth_btn, lv_color_lighten(COLOR_MATERIAL_RED, 30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ctx->deauth_btn, 12, 0);
    app_bind(ctx->deauth_btn, deauth_btn_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)0, app_template_line("ui.show_deauth_popup.deauth_btn.clicked.3d53e9fc0cfa"));  // 0 = deauth/stop button

    ctx->deauth_btn_label = lv_label_create(ctx->deauth_btn);
    lv_label_set_text(ctx->deauth_btn_label, "Deauth Station");
    lv_obj_set_style_text_font(ctx->deauth_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->deauth_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ctx->deauth_btn_label);

    create_attack_action_bar(ctx->deauth_popup, observer_station_attack_tile_event_cb, NULL);
    app_da_watch(ctx,ctx->deauth_popup,ctx->deauth_btn_label);
}

static void destroy_deauth_popup_ui(void)
{
    tab_context_t *ctx = get_current_ctx(); if (!ctx) return;
    if (ctx->deauth_popup != NULL) {
        lv_obj_del(ctx->deauth_popup);
        ctx->deauth_popup = NULL;
    }

    ctx->deauth_btn = NULL;
    ctx->deauth_btn_label = NULL;
    ctx->deauth_active = false;
    ctx->deauth_network_idx = -1;
    ctx->deauth_client_idx = -1;
}

static void stop_and_close_deauth_popup(bool resume_observer){tab_context_t *ctx=get_current_ctx();if(!ctx)return;app_da_stop(ctx);destroy_deauth_popup_ui();(void)resume_observer;}
static void close_deauth_popup(void){stop_and_close_deauth_popup(true);}
static void deauth_btn_click_cb(lv_event_t *e){tab_context_t *ctx=get_current_ctx();if(!ctx)return;if((intptr_t)lv_event_get_user_data(e)==1||ctx->deauth_active){close_deauth_popup();return;}int n=ctx->deauth_network_idx,c=ctx->deauth_client_idx;if(n<0||n>=ctx->observer_network_count||c<0||c>=MAX_CLIENTS_PER_NETWORK)return;observer_network_t *net=&ctx->observer_networks[n];if(app_da_begin(ctx,"deauth",net->clients[c],net->bssid)){ctx->deauth_active=true;lv_label_set_text(ctx->deauth_btn_label,"STOP");}}


static void arp_keyboard_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx(); if (!ctx) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void arp_password_input_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx(); if (!ctx) return;
    (void)e;
    if (ctx->arp_keyboard) {
        lv_obj_clear_flag(ctx->arp_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ctx->arp_keyboard, ctx->arp_password_input);
    }
}

static void arp_poison_back_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "ARP Poison: back button pressed");

    tab_context_t *ctx = get_current_ctx();
    if(!ctx)return;
    app_da_cancel(ctx);
    if(ctx->arp_attack_popup_overlay)lv_obj_del(ctx->arp_attack_popup_overlay);
    bool return_to_observer = ctx && ctx->observer_attack_return_to_observer;
    bool return_to_evil_twin_passwords = ctx && ctx->arp_return_to_evil_twin_passwords;

    // Reset state
    ctx->arp_wifi_connected = false;
    ctx->arp_host_count = 0;
    memset(ctx->arp_target_ssid, 0, sizeof(ctx->arp_target_ssid));
    memset(ctx->arp_target_security, 0, sizeof(ctx->arp_target_security));
    memset(ctx->arp_our_ip, 0, sizeof(ctx->arp_our_ip));
    memset(ctx->arp_target_password, 0, sizeof(ctx->arp_target_password));
    ctx->arp_auto_mode = false;

    if (ctx->arp_poison_page) {
        lv_obj_del(ctx->arp_poison_page);
        ctx->arp_poison_page = NULL;
        if (ctx) {
            ctx->arp_poison_page = NULL;
        }
        ctx->arp_password_input = NULL;
        ctx->arp_keyboard = NULL;
        ctx->arp_connect_btn = NULL;
        ctx->arp_status_label = NULL;
        ctx->arp_hosts_container = NULL;
        ctx->arp_list_hosts_btn = NULL;
    }

    if (ctx) {
        ctx->observer_attack_return_to_observer = false;
        ctx->arp_return_to_evil_twin_passwords = false;
        clear_observer_attack_override(ctx);
    }

    if (return_to_observer) {
        show_observer_page();
        return;
    }

    if (return_to_evil_twin_passwords) {
        if (ctx && ctx->evil_twin_passwords_page) {
            lv_obj_clear_flag(ctx->evil_twin_passwords_page, LV_OBJ_FLAG_HIDDEN);
            ctx->current_visible_page = ctx->evil_twin_passwords_page;
        } else {
            show_evil_twin_passwords_page();
        }
        return;
    }

    show_scan_page();
}

static void arp_host_click_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx(); if(!ctx)return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ctx->arp_host_count) return;



    // Block ARP poisoning if Red Team mode is disabled
    if (!enable_red_team) {
        ESP_LOGW(TAG, "ARP Poisoning blocked - Red Team mode disabled");
        if (ctx->arp_status_label) {
            lv_label_set_text(ctx->arp_status_label, "ARP Poisoning requires Red Team mode");
            lv_obj_set_style_text_color(ctx->arp_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }

    arp_host_t *host = &ctx->arp_hosts[idx];
    ESP_LOGI(TAG, "ARP Poison: Starting attack on %s (%s)", host->ip, host->mac);

    if (!app_da_begin(ctx,"arp",host->mac,NULL)) return;

    // Create attack popup
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    ctx->arp_attack_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->arp_attack_popup_overlay);
    lv_obj_set_size(ctx->arp_attack_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->arp_attack_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->arp_attack_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->arp_attack_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->arp_attack_popup_overlay, LV_OBJ_FLAG_CLICKABLE);

    ctx->arp_attack_popup = lv_obj_create(ctx->arp_attack_popup_overlay);
    lv_obj_set_size(ctx->arp_attack_popup, 400, 250);
    lv_obj_center(ctx->arp_attack_popup);
    lv_obj_set_style_bg_color(ctx->arp_attack_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->arp_attack_popup, COLOR_MATERIAL_PURPLE, 0);
    lv_obj_set_style_border_width(ctx->arp_attack_popup, 3, 0);
    lv_obj_set_style_radius(ctx->arp_attack_popup, 16, 0);
    lv_obj_set_style_pad_all(ctx->arp_attack_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->arp_attack_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->arp_attack_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->arp_attack_popup, 15, 0);
    lv_obj_clear_flag(ctx->arp_attack_popup, LV_OBJ_FLAG_SCROLLABLE);

    // Icon
    lv_obj_t *icon = lv_label_create(ctx->arp_attack_popup);
    lv_label_set_text(icon, LV_SYMBOL_SHUFFLE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(icon, COLOR_MATERIAL_PURPLE, 0);

    // Title
    lv_obj_t *title = lv_label_create(ctx->arp_attack_popup);
    lv_label_set_text_fmt(title, "ARP Poisoning %s", host->ip);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);

    // Status
    lv_obj_t *status = lv_label_create(ctx->arp_attack_popup);
    lv_label_set_text(status, "Attack in Progress...");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xCCCCCC), 0);

    // Stop button
    lv_obj_t *stop_btn = lv_btn_create(ctx->arp_attack_popup);
    lv_obj_set_size(stop_btn, 140, 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 10, 0);
    app_bind(stop_btn, arp_attack_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.arp_host_click_cb.stop_btn.clicked.b9a9df920c40"));

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);
    app_da_watch(ctx,ctx->arp_attack_popup_overlay,status);
}

static void arp_attack_popup_close_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "ARP Poison: Stopping attack");

    // Send stop command to current tab's UART
    

    // Close popup
    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->arp_attack_popup_overlay) {
        app_da_stop(ctx);
        lv_obj_del(ctx->arp_attack_popup_overlay);
        ctx->arp_attack_popup_overlay = NULL;
        ctx->arp_attack_popup = NULL;
    }
}

static void app_arp_hosts_done(tab_context_t *ctx,char *rx_buffer){
    // Parse response
    ctx->arp_host_count = 0;
    memset(ctx->arp_our_ip, 0, sizeof(ctx->arp_our_ip));

    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL && ctx->arp_host_count < ARP_MAX_HOSTS) {
        // Look for "Our IP: X.X.X.X, Netmask: X.X.X.X"
        if (strstr(line, "Our IP:") != NULL) {
            char *ip_start = strstr(line, "Our IP:") + 7;
            while (*ip_start == ' ') ip_start++;
            char *comma = strchr(ip_start, ',');
            if (comma) {
                int len = comma - ip_start;
                if (len > 0 && len < (int)sizeof(ctx->arp_our_ip)) {
                    strncpy(ctx->arp_our_ip, ip_start, len);
                    ctx->arp_our_ip[len] = '\0';
                }
            }
        }
        // Look for host entries: "  IP  ->  MAC"
        else if (strstr(line, "->") != NULL) {
            char ip[20] = {0};
            char mac[18] = {0};

            // Parse: "  192.168.3.61  ->  C4:2B:44:12:29:15"
            char *arrow = strstr(line, "->");
            if (arrow) {
                // Get IP (before arrow)
                char *p = line;
                while (*p == ' ') p++;
                int ip_len = 0;
                while (*p && *p != ' ' && ip_len < 19) {
                    ip[ip_len++] = *p++;
                }
                ip[ip_len] = '\0';

                // Get MAC (after arrow)
                p = arrow + 2;
                while (*p == ' ') p++;
                int mac_len = 0;
                while (*p && *p != ' ' && *p != '\n' && mac_len < 17) {
                    mac[mac_len++] = *p++;
                }
                mac[mac_len] = '\0';

                // Validate and store
                if (strlen(ip) >= 7 && strlen(mac) == 17) {
                    strncpy(ctx->arp_hosts[ctx->arp_host_count].ip, ip, sizeof(ctx->arp_hosts[0].ip) - 1);
                    strncpy(ctx->arp_hosts[ctx->arp_host_count].mac, mac, sizeof(ctx->arp_hosts[0].mac) - 1);
                    ctx->arp_host_count++;
                    ESP_LOGI(TAG, "ARP host %d: %s -> %s", ctx->arp_host_count, ip, mac);
                }
            }
        }
        line = strtok(NULL, "\n\r");
    }

    bsp_display_lock(0);

    // Update status
    if (ctx->arp_status_label) {
        if (ctx->arp_host_count > 0) {
            lv_label_set_text_fmt(ctx->arp_status_label, "Our IP: %s | Found %d hosts", ctx->arp_our_ip, ctx->arp_host_count);
            lv_obj_set_style_text_color(ctx->arp_status_label, COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text(ctx->arp_status_label, "No hosts found");
            lv_obj_set_style_text_color(ctx->arp_status_label, COLOR_MATERIAL_RED, 0);
        }
    }

    // Display hosts in container
    if (ctx->arp_hosts_container) {
        lv_obj_clean(ctx->arp_hosts_container);

        for (int i = 0; i < ctx->arp_host_count; i++) {
            lv_obj_t *row = lv_obj_create(ctx->arp_hosts_container);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 10, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            app_bind(row, arp_host_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i, app_template_line("ui.arp_list_hosts_cb.row.clicked.783e7c246bca"));

            // IP
            lv_obj_t *ip_lbl = lv_label_create(row);
            lv_label_set_text(ip_lbl, ctx->arp_hosts[i].ip);
            lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ip_lbl, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_width(ip_lbl, 150);

            // MAC
            lv_obj_t *mac_lbl = lv_label_create(row);
            lv_label_set_text(mac_lbl, ctx->arp_hosts[i].mac);
            lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x888888), 0);
        }
    }
}

static void show_arp_poison_page(void)
{


    tab_context_t *ctx = get_current_ctx();
    if (ctx && ctx->observer_attack_return_to_observer) {
        if (ctx->observer_running) {
            pause_observer_for_attack(ctx);
        }
        if (ctx->popup_open) {
            destroy_network_popup_ui(ctx);
        }
        if (ctx->deauth_popup != NULL) {
            destroy_deauth_popup_ui();
        }
    }

    if(!ctx)return;
    if(!ctx->arp_hosts)ctx->arp_hosts=calloc(ARP_MAX_HOSTS,sizeof(arp_host_t));
    if(!ctx->arp_hosts)return;
    // Reset state
    ctx->arp_wifi_connected = false;
    ctx->arp_host_count = 0;
    memset(ctx->arp_our_ip, 0, sizeof(ctx->arp_our_ip));

    // Hide scan page
    if (ctx && ctx->scan_page) {
        lv_obj_add_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    if(ctx->arp_poison_page)lv_obj_del(ctx->arp_poison_page);

    ctx->arp_poison_page = lv_obj_create(container);
    if (ctx) {
        ctx->arp_poison_page = ctx->arp_poison_page;
    }
    lv_obj_set_size(ctx->arp_poison_page, lv_pct(100), lv_pct(100));
    lv_obj_align(ctx->arp_poison_page, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->arp_poison_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->arp_poison_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->arp_poison_page, 15, 0);
    lv_obj_set_flex_flow(ctx->arp_poison_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->arp_poison_page, 10, 0);

    // Header
    lv_obj_t *header = lv_obj_create(ctx->arp_poison_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    app_bind(back_btn, arp_poison_back_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.back_btn.clicked.f2c3f92c2c4e"));

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    // Title - use "Tests" instead of "Attacks" when Red Team is disabled
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, enable_red_team ? "Internal WiFi Attacks" : "Internal WiFi Tests");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_PURPLE, 0);

    // Target network info
    lv_obj_t *target_label = lv_label_create(ctx->arp_poison_page);
    lv_label_set_text_fmt(target_label, "Target: %s", ctx->arp_target_ssid);
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xCCCCCC), 0);

    bool is_open_network = wifi_network_security_is_open(ctx->arp_target_security);

    // Check if password is known from Evil Twin database (only in manual mode, non-open)
    bool password_known = false;
    password_known = ctx->arp_target_password[0] != 0;
    // Password section (only shown in manual mode)
    lv_obj_t *pass_section = NULL;

    if (!ctx->arp_auto_mode) {
        pass_section = lv_obj_create(ctx->arp_poison_page);
        lv_obj_set_size(pass_section, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(pass_section, lv_color_hex(0x252525), 0);
        lv_obj_set_style_border_width(pass_section, 0, 0);
        lv_obj_set_style_radius(pass_section, 8, 0);
        lv_obj_set_style_pad_all(pass_section, 15, 0);
        lv_obj_set_style_pad_row(pass_section, 10, 0);
        lv_obj_clear_flag(pass_section, LV_OBJ_FLAG_SCROLLABLE);

        if (is_open_network) {
            lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);

            lv_obj_t *open_label = lv_label_create(pass_section);
            lv_label_set_text(open_label, LV_SYMBOL_WARNING "  Open Network (no password required)");
            lv_obj_set_style_text_font(open_label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(open_label, COLOR_MATERIAL_AMBER, 0);

            lv_obj_t *btn_row = lv_obj_create(pass_section);
            lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn_row, 0, 0);
            lv_obj_set_style_pad_all(btn_row, 0, 0);
            lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn_row, 15, 0);
            lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

            ctx->arp_connect_btn = lv_btn_create(btn_row);
            lv_obj_set_size(ctx->arp_connect_btn, 120, 40);
            lv_obj_set_style_bg_color(ctx->arp_connect_btn, COLOR_MATERIAL_GREEN, 0);
            lv_obj_set_style_radius(ctx->arp_connect_btn, 8, 0);
            app_bind(ctx->arp_connect_btn, arp_connect_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_connect_btn.clicked.ba08d327a983"));

            lv_obj_t *connect_label = lv_label_create(ctx->arp_connect_btn);
            lv_label_set_text(connect_label, "Connect");
            lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
            lv_obj_center(connect_label);

            ctx->arp_list_hosts_btn = lv_btn_create(btn_row);
            lv_obj_set_size(ctx->arp_list_hosts_btn, 120, 40);
            lv_obj_set_style_bg_color(ctx->arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_style_radius(ctx->arp_list_hosts_btn, 8, 0);
            app_bind(ctx->arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_list_hosts_btn.clicked.15832a445891"));
            lv_obj_add_flag(ctx->arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);

            lv_obj_t *list_hosts_label = lv_label_create(ctx->arp_list_hosts_btn);
            lv_label_set_text(list_hosts_label, "List Hosts");
            lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
            lv_obj_center(list_hosts_label);
        } else if (password_known) {
            // Saved password exists on the module; let firmware resolve it via --saved.
            lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);

            lv_obj_t *pass_title = lv_label_create(pass_section);
            lv_label_set_text(pass_title, "Saved password available");
            lv_obj_set_style_text_font(pass_title, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(pass_title, lv_color_hex(0xFFFFFF), 0);

            lv_obj_t *pass_value = lv_label_create(pass_section);
            lv_label_set_text(pass_value, "Firmware will use --saved");
            lv_obj_set_style_text_font(pass_value, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(pass_value, COLOR_MATERIAL_GREEN, 0);

            // Buttons row
            lv_obj_t *btn_row = lv_obj_create(pass_section);
            lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn_row, 0, 0);
            lv_obj_set_style_pad_all(btn_row, 0, 0);
            lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(btn_row, 15, 0);
            lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

            // Connect button
            ctx->arp_connect_btn = lv_btn_create(btn_row);
            lv_obj_set_size(ctx->arp_connect_btn, 120, 40);
            lv_obj_set_style_bg_color(ctx->arp_connect_btn, COLOR_MATERIAL_GREEN, 0);
            lv_obj_set_style_radius(ctx->arp_connect_btn, 8, 0);
            app_bind(ctx->arp_connect_btn, arp_connect_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_connect_btn.clicked.8fdd3ab06217"));

            lv_obj_t *connect_label = lv_label_create(ctx->arp_connect_btn);
            lv_label_set_text(connect_label, "Connect");
            lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
            lv_obj_center(connect_label);

            // List Hosts button (hidden initially)
            ctx->arp_list_hosts_btn = lv_btn_create(btn_row);
            lv_obj_set_size(ctx->arp_list_hosts_btn, 120, 40);
            lv_obj_set_style_bg_color(ctx->arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_style_radius(ctx->arp_list_hosts_btn, 8, 0);
            app_bind(ctx->arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_list_hosts_btn.clicked.3074e7c62ae8"));
            lv_obj_add_flag(ctx->arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);

            lv_obj_t *list_hosts_label = lv_label_create(ctx->arp_list_hosts_btn);
            lv_label_set_text(list_hosts_label, "List Hosts");
            lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
            lv_obj_center(list_hosts_label);
        } else {
            // Show password input
            lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(pass_section, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(pass_section, 15, 0);

            // Password label + input
            lv_obj_t *pass_left = lv_obj_create(pass_section);
            lv_obj_set_size(pass_left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(pass_left, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(pass_left, 0, 0);
            lv_obj_set_style_pad_all(pass_left, 0, 0);
            lv_obj_set_flex_flow(pass_left, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(pass_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(pass_left, 10, 0);
            lv_obj_clear_flag(pass_left, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *pass_label = lv_label_create(pass_left);
            lv_label_set_text(pass_label, "Password:");
            lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);

            ctx->arp_password_input = lv_textarea_create(pass_left);
            lv_obj_set_size(ctx->arp_password_input, 300, 40);
            lv_textarea_set_one_line(ctx->arp_password_input, true);
            lv_textarea_set_placeholder_text(ctx->arp_password_input, "WiFi password");
            lv_obj_set_style_bg_color(ctx->arp_password_input, lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_border_color(ctx->arp_password_input, COLOR_MATERIAL_PURPLE, 0);
            lv_obj_set_style_border_width(ctx->arp_password_input, 1, 0);
            lv_obj_set_style_text_color(ctx->arp_password_input, lv_color_hex(0xFFFFFF), 0);
            app_bind(ctx->arp_password_input, arp_password_input_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_password_input.clicked.8e5e9a6ce46b"));

            // Connect button
            ctx->arp_connect_btn = lv_btn_create(pass_section);
            lv_obj_set_size(ctx->arp_connect_btn, 120, 40);
            lv_obj_set_style_bg_color(ctx->arp_connect_btn, COLOR_MATERIAL_GREEN, 0);
            lv_obj_set_style_radius(ctx->arp_connect_btn, 8, 0);
            app_bind(ctx->arp_connect_btn, arp_connect_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_connect_btn.clicked.d9a00d069249"));

            lv_obj_t *connect_label = lv_label_create(ctx->arp_connect_btn);
            lv_label_set_text(connect_label, "Connect");
            lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
            lv_obj_center(connect_label);

            // List Hosts button (hidden initially)
            ctx->arp_list_hosts_btn = lv_btn_create(pass_section);
            lv_obj_set_size(ctx->arp_list_hosts_btn, 120, 40);
            lv_obj_set_style_bg_color(ctx->arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
            lv_obj_set_style_radius(ctx->arp_list_hosts_btn, 8, 0);
            app_bind(ctx->arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_list_hosts_btn.clicked.f4b20e3915c2"));
            lv_obj_add_flag(ctx->arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);

            lv_obj_t *list_hosts_label = lv_label_create(ctx->arp_list_hosts_btn);
            lv_label_set_text(list_hosts_label, "List Hosts");
            lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
            lv_obj_center(list_hosts_label);
        }
    }

    // In auto mode, create List Hosts button here (since pass_section is not created)
    if (ctx->arp_auto_mode) {
        lv_obj_t *btn_container = lv_obj_create(ctx->arp_poison_page);
        lv_obj_set_size(btn_container, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_container, 0, 0);
        lv_obj_set_style_pad_all(btn_container, 0, 0);
        lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

        ctx->arp_list_hosts_btn = lv_btn_create(btn_container);
        lv_obj_set_size(ctx->arp_list_hosts_btn, 150, 45);
        lv_obj_set_style_bg_color(ctx->arp_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_radius(ctx->arp_list_hosts_btn, 8, 0);
        app_bind(ctx->arp_list_hosts_btn, arp_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_arp_poison_page.arp_list_hosts_btn.clicked.a2e7a8dacdcd"));
        lv_obj_add_flag(ctx->arp_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);  // Hidden until connected

        lv_obj_t *list_hosts_label = lv_label_create(ctx->arp_list_hosts_btn);
        lv_label_set_text(list_hosts_label, LV_SYMBOL_REFRESH " List Hosts");
        lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
        lv_obj_center(list_hosts_label);
    }

    // Status label
    ctx->arp_status_label = lv_label_create(ctx->arp_poison_page);
    if (ctx->arp_auto_mode) {
        lv_label_set_text_fmt(ctx->arp_status_label, "Auto-connecting to %s...", ctx->arp_target_ssid);
        lv_obj_set_style_text_color(ctx->arp_status_label, COLOR_MATERIAL_AMBER, 0);
    } else if (is_open_network) {
        lv_label_set_text(ctx->arp_status_label, "Press Connect to join open network");
        lv_obj_set_style_text_color(ctx->arp_status_label, lv_color_hex(0x888888), 0);
    } else {
        lv_label_set_text(ctx->arp_status_label, "Enter WiFi password to connect");
        lv_obj_set_style_text_color(ctx->arp_status_label, lv_color_hex(0x888888), 0);
    }
    lv_obj_set_style_text_font(ctx->arp_status_label, &lv_font_montserrat_14, 0);

    // Hosts container (scrollable)
    ctx->arp_hosts_container = lv_obj_create(ctx->arp_poison_page);
    lv_obj_set_size(ctx->arp_hosts_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ctx->arp_hosts_container, 1);
    lv_obj_set_style_bg_color(ctx->arp_hosts_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(ctx->arp_hosts_container, 0, 0);
    lv_obj_set_style_radius(ctx->arp_hosts_container, 8, 0);
    lv_obj_set_style_pad_all(ctx->arp_hosts_container, 8, 0);
    lv_obj_set_flex_flow(ctx->arp_hosts_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->arp_hosts_container, 6, 0);

    lv_obj_t *placeholder = lv_label_create(ctx->arp_hosts_container);
    if (ctx->arp_auto_mode) {
        lv_label_set_text(placeholder, "Connecting to network...");
    } else {
        lv_label_set_text(placeholder, "Connect to WiFi and click 'List Hosts' to scan network");
    }
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x666666), 0);

    // Create keyboard (hidden initially, only in manual mode)
    if (!ctx->arp_auto_mode) {
        ctx->arp_keyboard = lv_keyboard_create(container);  // On parent container, not flex page
        lv_obj_set_size(ctx->arp_keyboard, lv_pct(100), 260);  // Larger keys
        lv_obj_align(ctx->arp_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);  // Pin to bottom
        style_on_screen_keyboard(ctx->arp_keyboard);
        lv_keyboard_set_textarea(ctx->arp_keyboard, ctx->arp_password_input);
        app_bind(ctx->arp_keyboard, arp_keyboard_cb, LV_EVENT_ALL, NULL, app_template_line("ui.show_arp_poison_page.arp_keyboard.all.730320bbc476"));
        lv_obj_add_flag(ctx->arp_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    // In auto mode, start connection immediately
    if (ctx->arp_auto_mode) {
        // Use a small delay to let UI render first, then trigger auto connect
        arp_connect_cb(NULL);
    }
    app_da[tab_id_for_ctx(ctx)].page=ctx->arp_poison_page;
    lv_obj_add_event_cb(ctx->arp_poison_page,app_da_deleted,LV_EVENT_DELETE,ctx);
    ctx->current_visible_page=ctx->arp_poison_page;
}

EM_JS(int,app_arp_prep_js,(int tab,int action,const char *ssid,const char *password,char *error,int size),{
 try{return emulatorDevice.device.toolStart(emulatorDevice.module(tab),'nmap',{action:action===1?'connect':'hosts',ssid:UTF8ToString(ssid),password:UTF8ToString(password)});}catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(int,app_arp_reply_js,(int id,char *out,int size),{
 const j=emulatorDevice.device.job(id);if(!j){stringToUTF8('Simulation reset',out,size);return -1;}
 if(j.state==='cancelled'||j.state==='failed'){stringToUTF8(j.error||j.state,out,size);return -1;}
 if(j.state==='completed'){stringToUTF8(j.result?.text||"",out,size);return 1;}return 0;
});
static void app_arp_prep(tab_context_t *ctx,int action){
 int t=tab_id_for_ctx(ctx);char error[192]={0};if(app_da[t].id||app_da[t].prep)return;
 const char *password=ctx->arp_target_password[0]?ctx->arp_target_password:ctx->arp_password_input?lv_textarea_get_text(ctx->arp_password_input):"";
 if(action==1&&!wifi_network_security_is_open(ctx->arp_target_security)&&!password[0]){lv_label_set_text(ctx->arp_status_label,"Enter password first");return;}
 if(action==2&&!ctx->arp_wifi_connected){lv_label_set_text(ctx->arp_status_label,"Connect first");return;}
 app_da[t].prep=app_arp_prep_js(t,action,ctx->arp_target_ssid,password,error,sizeof(error));app_da[t].action=action;app_arp_story_jobs[t][action]=app_da[t].prep;
 lv_label_set_text(ctx->arp_status_label,app_da[t].prep?(action==1?"Connecting...":"Scanning network hosts..."):error);
 if(app_da[t].prep){if(ctx->arp_connect_btn)lv_obj_add_state(ctx->arp_connect_btn,LV_STATE_DISABLED);if(ctx->arp_list_hosts_btn)lv_obj_add_state(ctx->arp_list_hosts_btn,LV_STATE_DISABLED);}
 if(ctx->arp_keyboard)lv_obj_add_flag(ctx->arp_keyboard,LV_OBJ_FLAG_HIDDEN);
}
static void arp_connect_cb(lv_event_t *e){(void)e;tab_context_t *ctx=get_current_ctx();if(ctx)app_arp_prep(ctx,1);}
static void arp_list_hosts_cb(lv_event_t *e){(void)e;tab_context_t *ctx=get_current_ctx();if(ctx)app_arp_prep(ctx,2);}
static void arp_auto_connect_timer_cb(lv_timer_t *timer){lv_timer_del(timer);arp_connect_cb(NULL);}
static void app_attacks_deauth_arp_tick(void){
 char reply[4096];
 for(int t=0;t<4;t++){
  tab_context_t *ctx=get_ctx_for_tab(t);
  /* Check this operation's own surfaces, never ancestor visibility: changing
     module tabs hides their parent containers and must not cancel background jobs. */
  lv_obj_t *owner=app_da[t].owner,*attack_page=app_da[t].attack_page,*page=app_da[t].page;
  bool attack_hidden=app_da[t].id&&(!owner||!lv_obj_is_valid(owner)||lv_obj_has_flag(owner,LV_OBJ_FLAG_HIDDEN)||
      (attack_page&&(!lv_obj_is_valid(attack_page)||lv_obj_has_flag(attack_page,LV_OBJ_FLAG_HIDDEN))));
  bool prep_hidden=app_da[t].prep&&(!page||!lv_obj_is_valid(page)||lv_obj_has_flag(page,LV_OBJ_FLAG_HIDDEN));
  if(attack_hidden||prep_hidden){app_da_cancel(ctx);
   if(ctx->arp_status_label&&lv_obj_is_valid(ctx->arp_status_label))lv_label_set_text(ctx->arp_status_label,"Simulation cancelled");
   if(ctx->arp_connect_btn&&lv_obj_is_valid(ctx->arp_connect_btn))lv_obj_clear_state(ctx->arp_connect_btn,LV_STATE_DISABLED);
   if(ctx->arp_list_hosts_btn&&lv_obj_is_valid(ctx->arp_list_hosts_btn))lv_obj_clear_state(ctx->arp_list_hosts_btn,LV_STATE_DISABLED);
   if(ctx->arp_keyboard&&lv_obj_is_valid(ctx->arp_keyboard))lv_obj_add_flag(ctx->arp_keyboard,LV_OBJ_FLAG_HIDDEN);
   if(ctx->deauth_btn_label&&lv_obj_is_valid(ctx->deauth_btn_label))lv_label_set_text(ctx->deauth_btn_label,"Deauth Station");
   continue;
  }
  if(app_da[t].id){int state=app_da_poll_js(app_da[t].id,reply,sizeof(reply));
   if(app_da[t].status&&lv_obj_is_valid(app_da[t].status)&&app_da[t].status!=ctx->deauth_btn_label)lv_label_set_text(app_da[t].status,reply);
   if(state){app_da[t].id=0;ctx->deauth_active=false;if(ctx->deauth_btn_label)lv_label_set_text(ctx->deauth_btn_label,state<0?reply:"Deauth Station");}
  }
  if(app_da[t].prep){int state=app_arp_reply_js(app_da[t].prep,reply,sizeof(reply));if(!state)continue;int action=app_da[t].action;app_da[t].prep=0;
   if(!app_da[t].page||!lv_obj_is_valid(app_da[t].page))continue;
   if(ctx->arp_connect_btn)lv_obj_clear_state(ctx->arp_connect_btn,LV_STATE_DISABLED);if(ctx->arp_list_hosts_btn)lv_obj_clear_state(ctx->arp_list_hosts_btn,LV_STATE_DISABLED);
   if(state<0){ctx->arp_wifi_connected=false;lv_label_set_text(ctx->arp_status_label,reply);continue;}
   if(action==1){ctx->arp_wifi_connected=strstr(reply,"SUCCESS")!=NULL;lv_label_set_text(ctx->arp_status_label,ctx->arp_wifi_connected?"Connected (synthetic)":"Connection failed");
    if(ctx->arp_wifi_connected){if(ctx->arp_list_hosts_btn)lv_obj_clear_flag(ctx->arp_list_hosts_btn,LV_OBJ_FLAG_HIDDEN);if(ctx->arp_connect_btn)lv_obj_add_state(ctx->arp_connect_btn,LV_STATE_DISABLED);if(ctx->arp_auto_mode)app_arp_prep(ctx,2);}
   }else app_arp_hosts_done(ctx,reply);
  }
 }
}
