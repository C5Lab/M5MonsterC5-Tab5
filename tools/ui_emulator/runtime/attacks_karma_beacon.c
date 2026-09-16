/* Native UI with synthetic lifecycle only. Never performs radio or transport I/O. */
static int app_kb_jobs[4][2];
static int app_kb_completed_karma[4];
static lv_obj_t *app_kb_labels[4][2];
static tab_context_t *app_kb_contexts[4];
static bool app_kb_seeded[4];
static lv_obj_t *app_kb_empty_status[4];
EM_JS(int,app_kb_start_js,(int tab,int kind,const char *ssids,char *err,int size),{
 try {const names=UTF8ToString(ssids).split('\n').filter(Boolean);
 return emulatorDevice.device.attackStart(emulatorDevice.module(tab),kind?'beacon':'karma',(kind||names.length)?{customSSIDs:names}:{});
 }catch(e){stringToUTF8('Offline simulation: '+e.message,err,size);return 0;}
});
EM_JS(void,app_kb_end_js,(int id,int cancel),{
 if(cancel) emulatorDevice.device.cancel(id); else emulatorDevice.device.attackStop(id);
});
EM_JS(int,app_kb_poll_js,(int id,char *out,int size),{
 const j=emulatorDevice.device.job(id),r=j?.result||{};
 stringToUTF8('Offline simulation: '+(j?.state||'reset')+'\nPackets: '+(r.packets||0)+'  Clients: '+(r.clients||[]).length+'\n'+(r.clients||[]).slice(0,2).map(c=>c.mac).join('\n')+(j?.error?'\n'+j.error:""),out,size);
 return j?.state==='running'?0:1;
});
EM_JS(int,app_kb_probe_count_js,(int id),{
 const j=emulatorDevice.device.job(id);
 return j?.state==='completed'&&j.result?.elapsedMs>=1000?(j.result.ssids||[]).length:0;
});
EM_JS(void,app_kb_probe_row_js,(int id,int index,char *out,int size),{
 const j=emulatorDevice.device.job(id);
 stringToUTF8(j?.state==='completed'&&j.result?.elapsedMs>=1000?(j.result.ssids||[])[index]||"":"",out,size);
});
static void app_kb_finish(tab_context_t *ctx,int kind,bool cancel){
 int tab=tab_id_for_ctx(ctx); if(tab<0||tab>=4)return;
 if(app_kb_jobs[tab][kind])app_kb_end_js(app_kb_jobs[tab][kind],cancel);
 if(!kind)app_kb_completed_karma[tab]=cancel?0:app_kb_jobs[tab][kind];
}
static bool app_kb_begin(tab_context_t *ctx,int kind,const char *ssids){
 int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=4)return false;
 char err[256]={0};int id=app_kb_start_js(tab,kind,ssids,err,sizeof(err));
 if(!id){lv_obj_t *label=kind?ctx->beacon_ssids_status_label:ctx->karma_status_label;
 if(!label&&kind&&ctx->beacon_spam_page){label=lv_label_create(ctx->beacon_spam_page);ctx->beacon_ssids_status_label=label;}
 if(label)lv_label_set_text(label,err);return false;}
 if(!kind)app_kb_completed_karma[tab]=0;
 app_kb_jobs[tab][kind]=id;app_kb_contexts[tab]=ctx;return true;
}
static void app_attacks_karma_beacon_cancel(tab_context_t *ctx){
 int tab=tab_id_for_ctx(ctx);if(tab<0||tab>=4)return;
 for(int k=0;k<2;k++){app_kb_finish(ctx,k,true);app_kb_jobs[tab][k]=0;app_kb_labels[tab][k]=NULL;}
 ctx->karma_monitoring=false;ctx->karma_sniffer_running=false;
}
static void app_attacks_karma_beacon_tick(void){
 char text[512];
 for(int tab=0;tab<4;tab++)for(int k=0;k<2;k++)if(app_kb_jobs[tab][k]){
  tab_context_t *ctx=app_kb_contexts[tab];
  lv_obj_t *owner=k?ctx->beacon_spam_page:ctx->karma_page;
  if(!owner||!lv_obj_is_valid(owner)||lv_obj_has_flag(owner,LV_OBJ_FLAG_HIDDEN)){
   app_kb_finish(ctx,k,true);app_kb_jobs[tab][k]=0;app_kb_labels[tab][k]=NULL;continue;
  }
  lv_obj_t *label=app_kb_labels[tab][k];
  int done=app_kb_poll_js(app_kb_jobs[tab][k],text,sizeof(text));
  if(label&&lv_obj_is_valid(label))lv_label_set_text(label,text);
  else if(!k&&ctx->karma_status_label&&lv_obj_is_valid(ctx->karma_status_label))lv_label_set_text(ctx->karma_status_label,text);
  if(done){if(!k)app_kb_completed_karma[tab]=app_kb_jobs[tab][k];app_kb_jobs[tab][k]=0;if(!k){ctx->karma_sniffer_running=false;
   if(ctx->karma_start_btn)lv_obj_clear_state(ctx->karma_start_btn,LV_STATE_DISABLED);
   if(ctx->karma_stop_btn)lv_obj_add_state(ctx->karma_stop_btn,LV_STATE_DISABLED);
  }}
 }
}
static void beacon_spam_refresh_ssids(tab_context_t *ctx){
 int tab=tab_id_for_ctx(ctx);if(!app_kb_seeded[tab]){
 app_kb_seeded[tab]=true;ctx->beacon_spam_ssid_count=2;
 for(int i=0;i<2;i++){ctx->beacon_spam_ssids[i].index=i+1;snprintf(ctx->beacon_spam_ssids[i].ssid,sizeof(ctx->beacon_spam_ssids[i].ssid),"Demo Beacon %d",i+1);}}
 if(ctx->beacon_ssids_status_label)lv_label_set_text_fmt(ctx->beacon_ssids_status_label,"Offline simulation: %d SSIDs",ctx->beacon_spam_ssid_count);
}
static void beacon_ssids_delete_cb(lv_event_t *e){
 tab_context_t *ctx=get_current_ctx();int n=(int)(intptr_t)lv_event_get_user_data(e)-1;
 if(n<0||n>=ctx->beacon_spam_ssid_count)return;
 for(int i=n;i<ctx->beacon_spam_ssid_count-1;i++){ctx->beacon_spam_ssids[i]=ctx->beacon_spam_ssids[i+1];ctx->beacon_spam_ssids[i].index=i+1;}
 ctx->beacon_spam_ssid_count--;beacon_spam_refresh_ssids(ctx);beacon_spam_rebuild_ssid_grid(ctx);
}
static void close_beacon_spam_active_popup(tab_context_t *ctx,bool send_stop){
 if(send_stop)app_kb_finish(ctx,1,false);
 app_kb_labels[tab_id_for_ctx(ctx)][1]=NULL;
 if(ctx->beacon_spam_active_overlay)lv_obj_del(ctx->beacon_spam_active_overlay);
 ctx->beacon_spam_active_overlay=NULL;ctx->beacon_spam_active_popup=NULL;
}
static void karma_fetch_html_files(void){(get_current_ctx()->karma_html_count)=1;snprintf((get_current_ctx()->karma_html_files)[0],64,"demo-portal.html");}
static void karma_html_select_cb(lv_event_t *e){(void)e;do_karma_attack_start();}
static void karma_attack_popup_close_cb(lv_event_t *e){
 (void)e;app_kb_finish(get_current_ctx(),0,false);app_kb_labels[current_tab][0]=NULL;
 if((get_current_ctx()->karma_attack_popup_overlay))lv_obj_del((get_current_ctx()->karma_attack_popup_overlay));
 (get_current_ctx()->karma_attack_popup_overlay)=NULL;(get_current_ctx()->karma_attack_popup)=NULL;
 (get_current_ctx()->karma_attack_ssid_label)=NULL;(get_current_ctx()->karma_attack_mac_label)=NULL;(get_current_ctx()->karma_attack_password_label)=NULL;
}
static void karma_back_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();app_attacks_karma_beacon_cancel(ctx);
 karma_attack_popup_close_cb(NULL);karma_html_popup_close_cb(NULL);
 hide_all_pages(ctx);if(ctx->tiles){lv_obj_clear_flag(ctx->tiles,LV_OBJ_FLAG_HIDDEN);ctx->current_visible_page=ctx->tiles;}
}

static void beacon_spam_start_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();
    if (!ctx || !container) return;

    beacon_spam_refresh_ssids(ctx);
    if (ctx->beacon_spam_ssid_count <= 0) {
        int tab=tab_id_for_ctx(ctx);
        lv_obj_t *label=app_kb_empty_status[tab];
        if(!label||!lv_obj_is_valid(label)||lv_obj_get_parent(label)!=ctx->beacon_spam_page){
            label=lv_label_create(ctx->beacon_spam_page);app_kb_empty_status[tab]=label;
            lv_obj_set_width(label,lv_pct(90));lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);
        }
        lv_obj_clear_flag(label,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label,"No SSIDs configured. Add at least one.");
        lv_obj_set_style_text_color(label,COLOR_MATERIAL_RED,0);
        return;
    }
    if(app_kb_empty_status[current_tab]&&lv_obj_is_valid(app_kb_empty_status[current_tab]))lv_obj_add_flag(app_kb_empty_status[current_tab],LV_OBJ_FLAG_HIDDEN);

    char ssids[8192] = {0};
    for(int i=0;i<ctx->beacon_spam_ssid_count;i++) {
        if(i) strncat(ssids,"\n",sizeof(ssids)-strlen(ssids)-1);
        strncat(ssids,ctx->beacon_spam_ssids[i].ssid,sizeof(ssids)-strlen(ssids)-1);
    }
    if(!app_kb_begin(ctx,1,ssids)) return;
    if (ctx->beacon_spam_active_overlay) {
        close_beacon_spam_active_popup(ctx, false);
    }

    ctx->beacon_spam_active_overlay = lv_obj_create(container);
    style_modal_overlay(ctx->beacon_spam_active_overlay, LV_OPA_70);

    ctx->beacon_spam_active_popup = lv_obj_create(ctx->beacon_spam_active_overlay);
    lv_obj_set_size(ctx->beacon_spam_active_popup, 520, 320);
    lv_obj_center(ctx->beacon_spam_active_popup);
    style_popup_card(ctx->beacon_spam_active_popup, 12, COLOR_MATERIAL_CYAN);
    lv_obj_set_style_pad_all(ctx->beacon_spam_active_popup, 18, 0);
    lv_obj_set_flex_flow(ctx->beacon_spam_active_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->beacon_spam_active_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ctx->beacon_spam_active_popup, 14, 0);
    lv_obj_clear_flag(ctx->beacon_spam_active_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(ctx->beacon_spam_active_popup);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_44, 0);
    lv_obj_set_style_text_color(icon, COLOR_MATERIAL_CYAN, 0);

    lv_obj_t *title = lv_label_create(ctx->beacon_spam_active_popup);
    lv_label_set_text(title, "Beacon Spam");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_CYAN, 0);

    lv_obj_t *msg = lv_label_create(ctx->beacon_spam_active_popup);
    app_kb_labels[current_tab][1]=msg;
    lv_label_set_text(msg,"Offline simulation: starting Beacon Spam");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(msg, ui_text_color(), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *stop_btn = lv_btn_create(ctx->beacon_spam_active_popup);
    lv_obj_set_size(stop_btn, 190, 52);
    style_danger_button(stop_btn);
    app_bind_adapter(stop_btn,beacon_spam_active_close_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.beacon_spam_start_cb.beacon_spam_active_close_cb.LV_EVENT_CLICKED");

    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(stop_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(stop_lbl);
}

static void beacon_ssids_add_popup_save_cb(lv_event_t *e)
{
    (void)e;
    tab_context_t *ctx = get_current_ctx();
    if (!ctx || !ctx->beacon_ssids_add_textarea) return;

    char ssid[BEACON_SPAM_SSID_MAX_LEN + 1];
    snprintf(ssid, sizeof(ssid), "%s", lv_textarea_get_text(ctx->beacon_ssids_add_textarea));
    beacon_spam_trim_whitespace(ssid);
    if (ssid[0] == '\0') {
        if (ctx->beacon_ssids_status_label) {
            lv_label_set_text(ctx->beacon_ssids_status_label, "SSID cannot be empty.");
            lv_obj_set_style_text_color(ctx->beacon_ssids_status_label, COLOR_MATERIAL_RED, 0);
        }
        return;
    }

    if(ctx->beacon_spam_ssid_count >= BEACON_SPAM_MAX_SSIDS) return;
    int n=ctx->beacon_spam_ssid_count++;
    ctx->beacon_spam_ssids[n].index=n+1;
    snprintf(ctx->beacon_spam_ssids[n].ssid,sizeof(ctx->beacon_spam_ssids[n].ssid),"%s",ssid);
    close_beacon_ssids_add_popup(ctx);
    beacon_spam_refresh_ssids(ctx);
    beacon_spam_rebuild_ssid_grid(ctx);
}

static void karma_show_probes_cb(lv_event_t *e)
{
    (void)e;
    int id=app_kb_completed_karma[current_tab];
    int count=app_kb_probe_count_js(id);
    (get_current_ctx()->karma_probe_count)=count<KARMA_MAX_PROBES?count:KARMA_MAX_PROBES;
    for(int i=0;i<(get_current_ctx()->karma_probe_count);i++){
        (get_current_ctx()->karma_probes)[i].index=i+1;
        app_kb_probe_row_js(id,i,(get_current_ctx()->karma_probes)[i].ssid,sizeof((get_current_ctx()->karma_probes)[i].ssid));
    }
    // Update status
    if ((get_current_ctx()->karma_status_label)) {
        if ((get_current_ctx()->karma_probe_count) > 0) {
            lv_label_set_text_fmt((get_current_ctx()->karma_status_label), "Found %d probes - click to attack", (get_current_ctx()->karma_probe_count));
            lv_obj_set_style_text_color((get_current_ctx()->karma_status_label), COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text((get_current_ctx()->karma_status_label), "No probes found - run sniffer first");
            lv_obj_set_style_text_color((get_current_ctx()->karma_status_label), COLOR_MATERIAL_RED, 0);
        }
    }

    // Display probes in container
    if ((get_current_ctx()->karma_probes_container)) {
        lv_obj_clean((get_current_ctx()->karma_probes_container));

        for (int i = 0; i < (get_current_ctx()->karma_probe_count); i++) {
            lv_obj_t *row = lv_obj_create((get_current_ctx()->karma_probes_container));
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2D2D2D), 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x3D3D3D), LV_STATE_PRESSED);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 12, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 10, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            char binding_id[100];
            snprintf(binding_id,sizeof(binding_id),"emu.phase45.karma.probe.%d.%d",current_tab,i);
            app_bind_adapter(row,karma_probe_click_cb,LV_EVENT_CLICKED,(void*)(intptr_t)i,binding_id);

            // Index
            lv_obj_t *idx_lbl = lv_label_create(row);
            lv_label_set_text_fmt(idx_lbl, "%d.", (get_current_ctx()->karma_probes)[i].index);
            lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(idx_lbl, lv_color_hex(0x888888), 0);
            lv_obj_set_width(idx_lbl, 30);

            // SSID
            lv_obj_t *ssid_lbl = lv_label_create(row);
            lv_label_set_text(ssid_lbl, (get_current_ctx()->karma_probes)[i].ssid);
            lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ssid_lbl, COLOR_MATERIAL_ORANGE, 0);
        }
    }
}

static void do_karma_attack_start(void)
{
    if (!(get_current_ctx()->karma_html_dropdown) || (get_current_ctx()->karma_selected_probe_idx) < 0) return;

    int html_idx = lv_dropdown_get_selected((get_current_ctx()->karma_html_dropdown));
    int probe_idx = (get_current_ctx()->karma_probes)[(get_current_ctx()->karma_selected_probe_idx)].index;

    ESP_LOGI(TAG, "Karma: Starting attack - probe %d, html %d", probe_idx, html_idx);

    // Close HTML popup
    if ((get_current_ctx()->karma_html_popup_overlay)) {
        lv_obj_del((get_current_ctx()->karma_html_popup_overlay));
        (get_current_ctx()->karma_html_popup_overlay) = NULL;
        (get_current_ctx()->karma_html_popup) = NULL;
        (get_current_ctx()->karma_html_dropdown) = NULL;
    }

    if(!app_kb_begin(get_current_ctx(),0,(get_current_ctx()->karma_probes)[(get_current_ctx()->karma_selected_probe_idx)].ssid)) return;
    // Create attack popup
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    (get_current_ctx()->karma_attack_popup_overlay) = lv_obj_create(container);
    lv_obj_remove_style_all((get_current_ctx()->karma_attack_popup_overlay));
    lv_obj_set_size((get_current_ctx()->karma_attack_popup_overlay), lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color((get_current_ctx()->karma_attack_popup_overlay), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa((get_current_ctx()->karma_attack_popup_overlay), LV_OPA_50, 0);
    lv_obj_clear_flag((get_current_ctx()->karma_attack_popup_overlay), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag((get_current_ctx()->karma_attack_popup_overlay), LV_OBJ_FLAG_CLICKABLE);

    (get_current_ctx()->karma_attack_popup) = lv_obj_create((get_current_ctx()->karma_attack_popup_overlay));
    lv_obj_set_size((get_current_ctx()->karma_attack_popup), 500, 350);
    lv_obj_center((get_current_ctx()->karma_attack_popup));
    lv_obj_set_style_bg_color((get_current_ctx()->karma_attack_popup), lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color((get_current_ctx()->karma_attack_popup), COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width((get_current_ctx()->karma_attack_popup), 3, 0);
    lv_obj_set_style_radius((get_current_ctx()->karma_attack_popup), 16, 0);
    lv_obj_set_style_pad_all((get_current_ctx()->karma_attack_popup), 25, 0);
    lv_obj_set_flex_flow((get_current_ctx()->karma_attack_popup), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align((get_current_ctx()->karma_attack_popup), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row((get_current_ctx()->karma_attack_popup), 12, 0);
    lv_obj_clear_flag((get_current_ctx()->karma_attack_popup), LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create((get_current_ctx()->karma_attack_popup));
    lv_label_set_text(title, LV_SYMBOL_WIFI " Karma Attack Active");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);

    // SSID label
    (get_current_ctx()->karma_attack_ssid_label) = lv_label_create((get_current_ctx()->karma_attack_popup));
    lv_label_set_text((get_current_ctx()->karma_attack_ssid_label), "Starting portal...");
    lv_obj_set_style_text_font((get_current_ctx()->karma_attack_ssid_label), &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color((get_current_ctx()->karma_attack_ssid_label), lv_color_hex(0xCCCCCC), 0);

    // MAC label
    (get_current_ctx()->karma_attack_mac_label) = lv_label_create((get_current_ctx()->karma_attack_popup));
    lv_label_set_text((get_current_ctx()->karma_attack_mac_label), "Waiting for clients...");
    lv_obj_set_style_text_font((get_current_ctx()->karma_attack_mac_label), &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color((get_current_ctx()->karma_attack_mac_label), lv_color_hex(0x888888), 0);

    // Password label
    (get_current_ctx()->karma_attack_password_label) = lv_label_create((get_current_ctx()->karma_attack_popup));
    lv_label_set_text((get_current_ctx()->karma_attack_password_label), "");
    lv_obj_set_style_text_font((get_current_ctx()->karma_attack_password_label), &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color((get_current_ctx()->karma_attack_password_label), COLOR_MATERIAL_GREEN, 0);

    // Stop button
    lv_obj_t *stop_btn = lv_btn_create((get_current_ctx()->karma_attack_popup));
    lv_obj_set_size(stop_btn, 140, 50);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 10, 0);
    app_bind_adapter(stop_btn,karma_attack_popup_close_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.do_karma_attack_start.karma_attack_popup_close_cb.LV_EVENT_CLICKED");

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_18, 0);
    lv_obj_center(stop_label);

    app_kb_labels[current_tab][0]=(get_current_ctx()->karma_attack_mac_label);
    lv_label_set_text((get_current_ctx()->karma_attack_ssid_label),"Offline simulation");
}


static void karma_start_sniffer_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: Starting sniffer");

    if(!app_kb_begin(get_current_ctx(),0,"")) return;
    (get_current_ctx()->karma_sniffer_running) = true;

    // Update button states
    if ((get_current_ctx()->karma_start_btn)) {
        lv_obj_add_state((get_current_ctx()->karma_start_btn), LV_STATE_DISABLED);
    }
    if ((get_current_ctx()->karma_stop_btn)) {
        lv_obj_clear_state((get_current_ctx()->karma_stop_btn), LV_STATE_DISABLED);
    }

    if ((get_current_ctx()->karma_status_label)) {
        lv_label_set_text((get_current_ctx()->karma_status_label), "Sniffer started - collecting probes...");
        lv_obj_set_style_text_color((get_current_ctx()->karma_status_label), COLOR_MATERIAL_GREEN, 0);
    }
}

static void karma_stop_sniffer_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Karma: Stopping sniffer");

    app_kb_finish(get_current_ctx(),0,false);
    (get_current_ctx()->karma_sniffer_running) = false;

    // Update button states
    if ((get_current_ctx()->karma_start_btn)) {
        lv_obj_clear_state((get_current_ctx()->karma_start_btn), LV_STATE_DISABLED);
    }
    if ((get_current_ctx()->karma_stop_btn)) {
        lv_obj_add_state((get_current_ctx()->karma_stop_btn), LV_STATE_DISABLED);
    }

    if ((get_current_ctx()->karma_status_label)) {
        lv_label_set_text((get_current_ctx()->karma_status_label), "Sniffer stopped - fetching probes...");
        lv_obj_set_style_text_color((get_current_ctx()->karma_status_label), COLOR_MATERIAL_AMBER, 0);
    }

    // Auto-fetch probes after stopping
    karma_show_probes_cb(NULL);
}


static void beacon_spam_back_btn_event_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 app_attacks_karma_beacon_cancel(ctx);close_beacon_ssids_add_popup(ctx);
 close_beacon_spam_active_popup(ctx,false);show_global_attacks_page();
}

static void show_karma_page(void)
{
    ESP_LOGI(TAG, "Showing Karma page");

    // Get current tab's data and container
    tab_context_t *ctx = get_current_ctx();
    lv_obj_t *container = get_current_tab_container();

    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for tab %d", current_tab);
        return;
    }

    // Hide all other pages
    hide_all_pages(ctx);

    // If page already exists for this tab, just show it
    if (ctx->karma_page) {
        lv_obj_clear_flag(ctx->karma_page, LV_OBJ_FLAG_HIDDEN);
        ctx->current_visible_page = ctx->karma_page;
        (get_current_ctx()->karma_page) = ctx->karma_page;  // Update legacy reference
        (get_current_ctx()->karma_probes_container) = ctx->karma_probes_container;
        (get_current_ctx()->karma_start_btn) = ctx->karma_start_btn;
        (get_current_ctx()->karma_stop_btn)  = ctx->karma_stop_btn;
        (get_current_ctx()->karma_status_label)      = ctx->karma_status_label;
        ESP_LOGI(TAG, "Showing existing karma page for tab %d", current_tab);
        return;
    }

    ESP_LOGI(TAG, "Creating new karma page for tab %d", current_tab);

    // Fresh page - reset state for this tab
    (get_current_ctx()->karma_probe_count) = 0;
    (get_current_ctx()->karma_selected_probe_idx) = -1;

    // Create karma page container inside tab container
    ctx->karma_page = lv_obj_create(container);
    (get_current_ctx()->karma_page) = ctx->karma_page;  // Keep legacy reference
    lv_obj_set_size((get_current_ctx()->karma_page), lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color((get_current_ctx()->karma_page), ui_bg_color(), 0);
    lv_obj_set_style_border_width((get_current_ctx()->karma_page), 0, 0);
    lv_obj_set_style_pad_all((get_current_ctx()->karma_page), 15, 0);
    lv_obj_set_flex_flow((get_current_ctx()->karma_page), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row((get_current_ctx()->karma_page), 10, 0);

    // Header
    lv_obj_t *header = lv_obj_create((get_current_ctx()->karma_page));
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
    style_back_nav_button(back_btn);
    app_bind_adapter(back_btn,karma_back_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.show_karma_page.back_btn.LV_EVENT_CLICKED");

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Karma Attack");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, dark_mode_enabled ? COLOR_LAB5_MAGENTA : ui_tab_icon_color(), 0);

    // Button bar
    lv_obj_t *btn_bar = lv_obj_create((get_current_ctx()->karma_page));
    lv_obj_set_size(btn_bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_bar, 0, 0);
    lv_obj_set_style_pad_all(btn_bar, 0, 0);
    lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_bar, 15, 0);
    lv_obj_clear_flag(btn_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Start Sniffer button
    (get_current_ctx()->karma_start_btn) = lv_btn_create(btn_bar);
    lv_obj_set_size((get_current_ctx()->karma_start_btn), 130, 45);
    lv_obj_set_style_bg_color((get_current_ctx()->karma_start_btn), COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_bg_color((get_current_ctx()->karma_start_btn), lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius((get_current_ctx()->karma_start_btn), 8, 0);
    app_bind_adapter((get_current_ctx()->karma_start_btn),karma_start_sniffer_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.show_karma_page.karma_start_sniffer_btn.LV_EVENT_CLICKED");

    lv_obj_t *start_label = lv_label_create((get_current_ctx()->karma_start_btn));
    lv_label_set_text(start_label, "Start Sniffer");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_14, 0);
    lv_obj_center(start_label);

    // Stop Sniffer button
    (get_current_ctx()->karma_stop_btn) = lv_btn_create(btn_bar);
    lv_obj_set_size((get_current_ctx()->karma_stop_btn), 130, 45);
    lv_obj_set_style_bg_color((get_current_ctx()->karma_stop_btn), COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color((get_current_ctx()->karma_stop_btn), lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_radius((get_current_ctx()->karma_stop_btn), 8, 0);
    app_bind_adapter((get_current_ctx()->karma_stop_btn),karma_stop_sniffer_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.show_karma_page.karma_stop_sniffer_btn.LV_EVENT_CLICKED");

    lv_obj_t *stop_label = lv_label_create((get_current_ctx()->karma_stop_btn));
    lv_label_set_text(stop_label, "Stop Sniffer");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_label);

    // Initially: Start enabled, Stop disabled (sniffer not running)
    (get_current_ctx()->karma_sniffer_running) = false;
    lv_obj_add_state((get_current_ctx()->karma_stop_btn), LV_STATE_DISABLED);

    // Status label
    (get_current_ctx()->karma_status_label) = lv_label_create((get_current_ctx()->karma_page));
    lv_label_set_text((get_current_ctx()->karma_status_label), "Start sniffer to collect probe requests");
    lv_obj_set_style_text_font((get_current_ctx()->karma_status_label), &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color((get_current_ctx()->karma_status_label), ui_muted_color(), 0);

    // Probes container (scrollable)
    (get_current_ctx()->karma_probes_container) = lv_obj_create((get_current_ctx()->karma_page));
    lv_obj_set_size((get_current_ctx()->karma_probes_container), lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow((get_current_ctx()->karma_probes_container), 1);
    style_surface_panel((get_current_ctx()->karma_probes_container), 8);
    lv_obj_set_style_pad_all((get_current_ctx()->karma_probes_container), 10, 0);
    lv_obj_set_flex_flow((get_current_ctx()->karma_probes_container), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row((get_current_ctx()->karma_probes_container), 6, 0);

    lv_obj_t *placeholder = lv_label_create((get_current_ctx()->karma_probes_container));
    lv_label_set_text(placeholder, "Wait a moment - the longer the better. Click Stop to see collected Probes.");
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(placeholder, ui_muted_color(), 0);

    // Mirror widget pointers into ctx so tab-switch save/restore works correctly
    ctx->karma_probes_container      = (get_current_ctx()->karma_probes_container);
    ctx->karma_start_btn             = (get_current_ctx()->karma_start_btn);
    ctx->karma_stop_btn              = (get_current_ctx()->karma_stop_btn);
    ctx->karma_status_label          = (get_current_ctx()->karma_status_label);
    ctx->karma_html_popup_overlay    = (get_current_ctx()->karma_html_popup_overlay);
    ctx->karma_html_popup            = (get_current_ctx()->karma_html_popup);
    ctx->karma_html_dropdown         = (get_current_ctx()->karma_html_dropdown);
    ctx->karma_attack_popup_overlay  = (get_current_ctx()->karma_attack_popup_overlay);
    ctx->karma_attack_popup          = (get_current_ctx()->karma_attack_popup);
    ctx->karma_attack_ssid_label     = (get_current_ctx()->karma_attack_ssid_label);
    ctx->karma_attack_mac_label      = (get_current_ctx()->karma_attack_mac_label);
    ctx->karma_attack_password_label = (get_current_ctx()->karma_attack_password_label);

    // Set current visible page
    ctx->current_visible_page = ctx->karma_page;
}

static void karma_probe_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (get_current_ctx()->karma_probe_count)) return;

    (get_current_ctx()->karma_selected_probe_idx) = idx;
    ESP_LOGI(TAG, "Karma: Selected probe %d: %s", idx, (get_current_ctx()->karma_probes)[idx].ssid);

    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    // Create overlay
    (get_current_ctx()->karma_html_popup_overlay) = lv_obj_create(container);
    lv_obj_remove_style_all((get_current_ctx()->karma_html_popup_overlay));
    lv_obj_set_size((get_current_ctx()->karma_html_popup_overlay), lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color((get_current_ctx()->karma_html_popup_overlay), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa((get_current_ctx()->karma_html_popup_overlay), LV_OPA_50, 0);
    lv_obj_clear_flag((get_current_ctx()->karma_html_popup_overlay), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag((get_current_ctx()->karma_html_popup_overlay), LV_OBJ_FLAG_CLICKABLE);

    // Create popup
    (get_current_ctx()->karma_html_popup) = lv_obj_create((get_current_ctx()->karma_html_popup_overlay));
    lv_obj_set_size((get_current_ctx()->karma_html_popup), 450, 280);
    lv_obj_center((get_current_ctx()->karma_html_popup));
    lv_obj_set_style_bg_color((get_current_ctx()->karma_html_popup), lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color((get_current_ctx()->karma_html_popup), COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_border_width((get_current_ctx()->karma_html_popup), 3, 0);
    lv_obj_set_style_radius((get_current_ctx()->karma_html_popup), 16, 0);
    lv_obj_set_style_pad_all((get_current_ctx()->karma_html_popup), 20, 0);
    lv_obj_set_flex_flow((get_current_ctx()->karma_html_popup), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align((get_current_ctx()->karma_html_popup), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row((get_current_ctx()->karma_html_popup), 15, 0);
    lv_obj_clear_flag((get_current_ctx()->karma_html_popup), LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create((get_current_ctx()->karma_html_popup));
    lv_label_set_text(title, "Select HTML Portal File");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_ORANGE, 0);

    // Loading spinner
    lv_obj_t *spinner = lv_spinner_create((get_current_ctx()->karma_html_popup));
    lv_obj_set_size(spinner, 50, 50);
    lv_spinner_set_anim_params(spinner, 1000, 200);

    lv_obj_t *loading_label = lv_label_create((get_current_ctx()->karma_html_popup));
    lv_label_set_text(loading_label, "Loading HTML files...");
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(loading_label, lv_color_hex(0x888888), 0);

    // Force refresh to show loading state
    lv_refr_now(NULL);
    bsp_display_unlock();

    // Fetch HTML files
    karma_fetch_html_files();

    bsp_display_lock(0);

    // Remove loading elements
    lv_obj_del(spinner);
    lv_obj_del(loading_label);

    if ((get_current_ctx()->karma_html_count) == 0) {
        lv_obj_t *error_label = lv_label_create((get_current_ctx()->karma_html_popup));
        lv_label_set_text(error_label, "No HTML files found on SD card");
        lv_obj_set_style_text_font(error_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(error_label, COLOR_MATERIAL_RED, 0);

        // Close button
        lv_obj_t *close_btn = lv_btn_create((get_current_ctx()->karma_html_popup));
        lv_obj_set_size(close_btn, 100, 40);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(close_btn, 8, 0);
        app_bind_adapter(close_btn,karma_html_popup_close_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.karma_probe_click_cb.close_btn.LV_EVENT_CLICKED");

        lv_obj_t *close_label = lv_label_create(close_btn);
        lv_label_set_text(close_label, "Close");
        lv_obj_center(close_label);
        return;
    }

    // SSID info
    lv_obj_t *ssid_label = lv_label_create((get_current_ctx()->karma_html_popup));
    lv_label_set_text_fmt(ssid_label, "Target: %s", (get_current_ctx()->karma_probes)[idx].ssid);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xCCCCCC), 0);

    // HTML dropdown
    (get_current_ctx()->karma_html_dropdown) = lv_dropdown_create((get_current_ctx()->karma_html_popup));
    lv_obj_set_width((get_current_ctx()->karma_html_dropdown), 350);

    char options[2048] = "";
    for (int i = 0; i < (get_current_ctx()->karma_html_count); i++) {
        if (i > 0) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
        strncat(options, (get_current_ctx()->karma_html_files)[i], sizeof(options) - strlen(options) - 1);
    }
    lv_dropdown_set_options((get_current_ctx()->karma_html_dropdown), options);
    lv_obj_set_style_bg_color((get_current_ctx()->karma_html_dropdown), lv_color_hex(0x252525), 0);
    lv_obj_set_style_text_color((get_current_ctx()->karma_html_dropdown), lv_color_hex(0xFFFFFF), 0);

    // Buttons row
    lv_obj_t *btn_row = lv_obj_create((get_current_ctx()->karma_html_popup));
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 20, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    // Cancel button
    lv_obj_t *cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 100, 40);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    app_bind_adapter(cancel_btn,karma_html_popup_close_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.karma_probe_click_cb.cancel_btn.LV_EVENT_CLICKED");

    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    // Start button
    lv_obj_t *start_btn = lv_btn_create(btn_row);
    lv_obj_set_size(start_btn, 120, 40);
    lv_obj_set_style_bg_color(start_btn, COLOR_MATERIAL_ORANGE, 0);
    lv_obj_set_style_radius(start_btn, 8, 0);
    app_bind_adapter(start_btn,karma_html_select_cb,LV_EVENT_CLICKED,NULL,"emu.phase45.karma_probe_click_cb.start_btn.LV_EVENT_CLICKED");

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "Start Karma");
    lv_obj_center(start_label);
}

static void karma_html_popup_close_cb(lv_event_t *e)
{
    (void)e;
    if ((get_current_ctx()->karma_html_popup_overlay)) {
        lv_obj_del((get_current_ctx()->karma_html_popup_overlay));
        (get_current_ctx()->karma_html_popup_overlay) = NULL;
        (get_current_ctx()->karma_html_popup) = NULL;
        (get_current_ctx()->karma_html_dropdown) = NULL;
    }
}
