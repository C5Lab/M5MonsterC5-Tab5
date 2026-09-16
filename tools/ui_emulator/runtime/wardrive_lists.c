/* Native Home Networks and MAC Blacklist dialogs. Data stays in per-tab
 * simulator memory; no credentials or commands leave the browser. */
static struct {
    wardrive_home_entry_t home[WARDRIVE_HOME_MAX];
    int home_count;
    char black[WARDRIVE_BLACKLIST_MAX][18];
    int black_count;
    wifi_network_t scan[MAX_NETWORKS];
} app_wd_lists[4];

EM_JS(void,app_wd_blacklist_sync_js,(int tab,const char *macs,int count),{
 const list=Array.from({length:count},(_,i)=>UTF8ToString(macs+i*18));
 emulatorDevice.device.wardriveSetBlacklist(emulatorDevice.module(tab),list);
});
static void app_wd_lists_sync(int tab) {
    app_wd_blacklist_sync_js(tab,(const char *)app_wd_lists[tab].black,app_wd_lists[tab].black_count);
}

EM_JS(int,app_wd_home_scan_count,(int tab),{
 return emulatorDevice.device.nearbyNetworks(emulatorDevice.module(tab)).length;
});
EM_JS(void,app_wd_home_scan_row,(int tab,int index,char *out,int size),{
 const n=emulatorDevice.device.nearbyNetworks(emulatorDevice.module(tab))[index];
 const quote=v=>'"'+String(v).replaceAll('"'," ")+'"';
 const line=n?[n.index,n.ssid,n.vendor,n.bssid,n.channel,n.security,n.rssi,n.band].map(quote).join(','):"";
 stringToUTF8(line,out,size);
});

static int wardrive_home_fetch(tab_context_t *ctx,char entries[][33],char bssids[][18],int max_entries) {
    if(!ctx || !entries || !bssids || max_entries<=0)return 0;
    int tab=tab_id_for_ctx(ctx),count=app_wd_lists[tab].home_count;
    if(count>max_entries)count=max_entries;
    for(int i=0;i<count;i++) {
        snprintf(entries[i],33,"%s",app_wd_lists[tab].home[i].ssid);
        snprintf(bssids[i],18,"%s",app_wd_lists[tab].home[i].bssid);
    }
    return count;
}

/* Decode the firmware's escaped quoted arguments without storing passwords. */
static const char *app_wd_quoted(const char *p,char *out,size_t size) {
    while(*p==' ')p++;
    if(*p++!='"')return NULL;
    size_t n=0;
    while(*p && *p!='"') {
        if(*p=='\\' && p[1])p++;
        if(n+1>=size)return NULL;
        out[n++]=*p++;
    }
    if(*p!='"')return NULL;
    out[n]=0;return p+1;
}
static bool wardrive_home_send_and_wait(tab_context_t *ctx,const char *cmd,char *resp,size_t resp_sz) {
    if(resp && resp_sz)resp[0]=0;
    if(!ctx || !cmd)return false;
    int tab=tab_id_for_ctx(ctx);char ssid[33],pass[65],bssid[18]="";
    const char *result="length",*p=NULL;
    bool add=strncmp(cmd,"home_add ",9)==0,remove=strncmp(cmd,"home_remove ",12)==0;
    if(add || remove)p=app_wd_quoted(cmd+(add?9:12),ssid,sizeof(ssid));
    if(p && ssid[0]) {
        if(add) {
            p=app_wd_quoted(p,pass,sizeof(pass));
            if(p) {while(*p==' ')p++;if(*p)p=app_wd_quoted(p,bssid,sizeof(bssid));}
        }
        if(p) {
            int i=0;while(i<app_wd_lists[tab].home_count && strcmp(app_wd_lists[tab].home[i].ssid,ssid))i++;
            if(remove) {
                if(i<app_wd_lists[tab].home_count) {
                    memmove(&app_wd_lists[tab].home[i],&app_wd_lists[tab].home[i+1],
                        (app_wd_lists[tab].home_count-i-1)*sizeof(wardrive_home_entry_t));
                    app_wd_lists[tab].home_count--;
                }
                result="Removed (simulation)";
            } else if(i>=WARDRIVE_HOME_MAX)result="full";
            else {
                if(i==app_wd_lists[tab].home_count)app_wd_lists[tab].home_count++;
                snprintf(app_wd_lists[tab].home[i].ssid,33,"%s",ssid);
                snprintf(app_wd_lists[tab].home[i].bssid,18,"%s",bssid);
                result="Saved (simulation)";
            }
        }
    }
    memset(pass,0,sizeof(pass));
    if(resp && resp_sz)snprintf(resp,resp_sz,"%s",result);
    return p!=NULL;
}

/* Called only for wardrive_blacklist commands by the settings boundary. */
static bool app_wd_lists_command(int tab,const char *cmd,char *resp,size_t size) {
    const char *result="Blacklist command rejected";
    bool ok=false;
    if(!strcmp(cmd,"wardrive_blacklist clear")) {app_wd_lists[tab].black_count=0;ok=true;}
    else {
        const char add_prefix[]="wardrive_blacklist add ";
        const char remove_prefix[]="wardrive_blacklist remove ";
        bool add=!strncmp(cmd,add_prefix,sizeof(add_prefix)-1);
        bool remove=!strncmp(cmd,remove_prefix,sizeof(remove_prefix)-1);
        if(add || remove) {
            const char *mac=cmd+(add?sizeof(add_prefix)-1:sizeof(remove_prefix)-1);
            bool valid=strlen(mac)==17;
            for(int i=0;i<17 && valid;i++) {
                bool hex=(mac[i]>='0'&&mac[i]<='9')||(mac[i]>='a'&&mac[i]<='f')||(mac[i]>='A'&&mac[i]<='F');
                valid=(i%3==2)?mac[i]==':':hex;
            }
            if(valid) {
                int i=0;while(i<app_wd_lists[tab].black_count && strcasecmp(mac,app_wd_lists[tab].black[i]))i++;
                if(remove) {
                    if(i<app_wd_lists[tab].black_count) {
                        memmove(app_wd_lists[tab].black[i],app_wd_lists[tab].black[i+1],(app_wd_lists[tab].black_count-i-1)*18);
                        app_wd_lists[tab].black_count--;
                    }
                    ok=true;
                } else if(i<app_wd_lists[tab].black_count)ok=true;
                else if(i<WARDRIVE_BLACKLIST_MAX) {
                    snprintf(app_wd_lists[tab].black[i],18,"%s",mac);app_wd_lists[tab].black_count++;ok=true;
                } else result="Blacklist full";
            } else result="Invalid MAC address";
        }
    }
    if(ok) {app_wd_lists_sync(tab);result="Blacklist updated (simulation)";}
    if(resp && size)snprintf(resp,size,"%s",result);
    if(!ok)emu_unsupported(result);
    return ok;
}

static bool wardrive_send_set_command(tab_context_t *ctx,const char *cmd,
                                      const char *ack_substr,char *resp_out,size_t resp_sz) {
    (void)ack_substr;
    if(resp_out && resp_sz)resp_out[0]=0;
    if(!ctx || !cmd)return false;
    if(!strncmp(cmd,"wardrive_blacklist ",19))
        return app_wd_lists_command(tab_id_for_ctx(ctx),cmd,resp_out,resp_sz);
    if(resp_out && resp_sz)snprintf(resp_out,resp_sz,"Unsupported simulator command");
    emu_unsupported(cmd);return false;
}

static void wardrive_blacklist_refresh(tab_context_t *ctx)
{
    if (!ctx || !ctx->wardrive_blacklist_list) return;
    lv_obj_clean(ctx->wardrive_blacklist_list);

    int tab = tab_id_for_ctx(ctx);
    char rx_buffer[WARDRIVE_BLACKLIST_MAX * 19 + 32] = {0};
    char parse_buffer[sizeof(rx_buffer)];
    for (int i=0;i<app_wd_lists[tab].black_count;i++) {
        strcat(rx_buffer,app_wd_lists[tab].black[i]);strcat(rx_buffer,"\n");
    }
    int count = 0;
    snprintf(parse_buffer, sizeof(parse_buffer), "%s", rx_buffer);
    char *saveptr = NULL;
    char *line = strtok_r(parse_buffer, "\r\n", &saveptr);
    while (line) {
        while (*line == ' ') line++;
        // A MAC line looks like AA:BB:CC:DD:EE:FF
        if (strlen(line) >= 17 && line[2] == ':' && line[5] == ':') {
            lv_obj_t *row = lv_obj_create(ctx->wardrive_blacklist_list);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2A3A), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_all(row, 6, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            char mac[18] = {0};
            snprintf(mac, sizeof(mac), "%.17s", line);
            lv_obj_t *mac_lbl = lv_label_create(row);
            lv_label_set_text(mac_lbl, mac);
            lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0xDDDDDD), 0);

            lv_obj_t *del_btn = lv_btn_create(row);
            lv_obj_set_size(del_btn, 80, 36);
            lv_obj_set_style_bg_color(del_btn, COLOR_MATERIAL_RED, 0);
            lv_obj_set_style_radius(del_btn, 6, 0);
            char *mac_copy = strdup(mac);
            char binding[100];snprintf(binding,sizeof(binding),"emu.phase4.wardrive.blacklist.%s.remove",mac);
            app_bind_adapter(del_btn, wardrive_blacklist_remove_cb, LV_EVENT_CLICKED, mac_copy,binding);
            (lv_obj_add_event_cb)(del_btn, wardrive_blacklist_row_del_cb, LV_EVENT_DELETE, mac_copy);
            lv_obj_t *del_lbl = lv_label_create(del_btn);
            lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
            lv_obj_center(del_lbl);
            count++;
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }

    if (ctx->wardrive_blacklist_status_label) {
        lv_label_set_text_fmt(ctx->wardrive_blacklist_status_label, "%d/%d entries", count, WARDRIVE_BLACKLIST_MAX);
        lv_obj_set_style_text_color(ctx->wardrive_blacklist_status_label, COLOR_MATERIAL_TEAL, 0);
    }
}

static void home_mgmt_scan_task(void *arg)
{
    tab_context_t *ctx = (tab_context_t *)arg;
    if (!ctx) { vTaskDelete(NULL); return; }
    int tab = tab_id_for_ctx(ctx);
    int home_mgmt_scan_count = 0;
    wifi_network_t *home_mgmt_scan_nets = app_wd_lists[tab].scan;
    int count = app_wd_home_scan_count(tab);
    for (int i=0;i<count && home_mgmt_scan_count<MAX_NETWORKS;i++) {
        char line[512];app_wd_home_scan_row(tab,i,line,sizeof(line));
        if(parse_network_line(line,&home_mgmt_scan_nets[home_mgmt_scan_count]))home_mgmt_scan_count++;
    }
    // Render the picker into the list (LVGL thread ownership via the lock).
    if (ctx->home_mgmt_overlay && ctx->home_mgmt_list) {
        lv_obj_clean(ctx->home_mgmt_list);
        if (home_mgmt_scan_count == 0) {
            lv_obj_t *lbl = lv_label_create(ctx->home_mgmt_list);
            lv_label_set_text(lbl, "No networks found. Tap Scan to retry.");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        } else {
            for (int i = 0; i < home_mgmt_scan_count; i++) {
                lv_obj_t *row = lv_obj_create(ctx->home_mgmt_list);
                lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
                lv_obj_set_style_pad_all(row, 6, 0);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x263238), 0);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x37474F), LV_STATE_PRESSED);
                lv_obj_set_style_border_width(row, 0, 0);
                lv_obj_set_style_radius(row, 6, 0);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_style_pad_row(row, 2, 0);
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

                lv_obj_t *s = lv_label_create(row);
                lv_label_set_text(s, home_mgmt_scan_nets[i].ssid[0] ? home_mgmt_scan_nets[i].ssid : "(hidden)");
                lv_obj_set_style_text_color(s, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_font(s, &lv_font_montserrat_16, 0);

                lv_obj_t *d = lv_label_create(row);
                lv_label_set_text_fmt(d, "%s | %s | %d dBm", home_mgmt_scan_nets[i].bssid,
                                      home_mgmt_scan_nets[i].band, home_mgmt_scan_nets[i].rssi);
                lv_obj_set_style_text_color(d, lv_color_hex(0x888888), 0);
                lv_obj_set_style_text_font(d, &lv_font_montserrat_12, 0);

                char binding[100];snprintf(binding,sizeof(binding),"emu.phase4.wardrive.home.%s.pick",home_mgmt_scan_nets[i].bssid);
                app_bind_adapter(row, home_mgmt_scan_pick_cb, LV_EVENT_CLICKED,
                                    (void *)&home_mgmt_scan_nets[i],binding);
            }
        }
        if (ctx->home_mgmt_status_label) {
            lv_label_set_text(ctx->home_mgmt_status_label,
                              home_mgmt_scan_count > 0 ? "Tap a network to fill SSID/BSSID."
                                                       : "No networks found.");
        }
    }

    ctx->home_mgmt_busy = false;
    ctx->home_mgmt_scan_task = NULL;
}

static void home_mgmt_scan_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    if (!ctx || ctx->home_mgmt_busy) return;
    ctx->home_mgmt_busy = true;
    if (ctx->home_mgmt_list) lv_obj_clean(ctx->home_mgmt_list);
    if (ctx->home_mgmt_status_label) lv_label_set_text(ctx->home_mgmt_status_label, "Scanning WiFi...");
    lv_refr_now(NULL);
    home_mgmt_scan_task(ctx);
}

static void app_wd_home_arm(tab_context_t *ctx) {
 int tab=tab_id_for_ctx(ctx);ctx->wardrive_home_count=app_wd_lists[tab].home_count;
 memcpy(ctx->wardrive_home,app_wd_lists[tab].home,sizeof(app_wd_lists[tab].home));
 for(int i=0;i<ctx->wardrive_home_count;i++)ctx->wardrive_home[i].prompted=false;
 ctx->wardrive_autoupload_ready=g_wd_autoupload_wigle||g_wd_autoupload_wdgwars;
}
static void app_wd_home_check(tab_context_t *ctx) {
 if(!g_wd_autoupload_enabled||!ctx->wardrive_autoupload_ready||ctx->wardrive_home_confirm_overlay)return;
 for(int i=0;i<ctx->wardrive_home_count;i++)for(int j=0;j<ctx->wardrive_net_count;j++) {
  wardrive_home_entry_t *h=&ctx->wardrive_home[i];wardrive_network_t *n=&ctx->wardrive_networks[j];
  if(!h->prompted&&!strcmp(n->kind,"WIFI")&&(!strcmp(h->ssid,n->ssid)||(h->bssid[0]&&!strcasecmp(h->bssid,n->bssid)))) {
   h->prompted=true;show_wardrive_home_confirm(ctx,h->ssid);return;
  }
 }
}
