/* Browser-only cooperative nmap/IoT adapter. UI and parser bodies are
 * transcribed from main/main.c; only transport, scheduling and context storage
 * differ. All replies are synthetic model text; no hardware calls occur. */
EM_JS(int,app_ni_start_js,(int tab,const char *kind,const char *action,const char *ssid,const char *password,const char *target,const char *level,char *error,int size),{
 try {return emulatorDevice.device.toolStart(emulatorDevice.module(tab),UTF8ToString(kind),{action:UTF8ToString(action),ssid:UTF8ToString(ssid),password:UTF8ToString(password),target:UTF8ToString(target),level:UTF8ToString(level)});}
 catch(e){stringToUTF8(e.message,error,size);return 0;}
});
EM_JS(int,app_ni_poll_js,(int id,int sample,char *out,int size),{
 const j=emulatorDevice.device.job(id);if(!j){stringToUTF8('Job reset',out,size);return -1;}
 if(j.state==='running'&&j.tool==='iot'&&j.result&&j.result.sample!==sample){stringToUTF8(j.result.text,out,size);return j.result.sample+2;}
 if(j.state==='completed'){stringToUTF8(j.result?.text||"",out,size);return 1;}
 if(j.state==='failed'||j.state==='cancelled'){stringToUTF8(j.error||j.state,out,size);return -1;}
 return 0;
});
EM_JS(int,app_ni_progress_js,(int id),{return Math.round((emulatorDevice.device.job(id)?.progress||0)*100);});
static struct {int id,action,sample;tab_context_t *ctx;lv_obj_t *owner;} app_ni_jobs[4][2];
static int app_nmap_story_jobs[4][4];
static int app_mesh_story_run[4],app_mesh_story_stopped[4],app_mesh_story_cleared[4];
static void app_ni_cancel(tab_context_t *ctx,int kind) {
 int tab=tab_id_for_ctx(ctx);if(app_ni_jobs[tab][kind].id)app_model_cancel(app_ni_jobs[tab][kind].id);
 app_ni_jobs[tab][kind].id=0;
 if(kind){ctx->iot_recon_monitoring=false;ctx->iot_recon_task=NULL;}
 else {ctx->nmap_scanning=false;ctx->nmap_scan_task=NULL;}
}
static void app_ni_deleted(lv_event_t *e) {
 lv_obj_t *owner=lv_event_get_target(e);
 for(int t=0;t<4;t++)for(int k=0;k<2;k++)if(app_ni_jobs[t][k].owner==owner){
  tab_context_t *ctx=app_ni_jobs[t][k].ctx;app_ni_cancel(ctx,k);app_ni_jobs[t][k].owner=NULL;
  if(k){ctx->iot_page=NULL;ctx->iot_status_label=NULL;ctx->iot_channel_label=NULL;ctx->iot_packets_label=NULL;ctx->iot_networks_label=NULL;ctx->iot_dropped_label=NULL;ctx->iot_pan_list=NULL;ctx->iot_start_btn=NULL;ctx->iot_stop_btn=NULL;ctx->iot_clear_btn=NULL;}
  else {ctx->nmap_page=NULL;ctx->nmap_status_label=NULL;ctx->nmap_keyboard=NULL;ctx->nmap_password_input=NULL;ctx->nmap_connect_btn=NULL;ctx->nmap_list_hosts_btn=NULL;ctx->nmap_all_hosts_btn=NULL;ctx->nmap_hosts_container=NULL;ctx->nmap_wifi_connected=false;}
 }
}
static bool app_ni_begin(tab_context_t *ctx,int kind,int action,const char *ssid,const char *password,const char *target,const char *level) {
 int tab=tab_id_for_ctx(ctx);char error[192]={0};lv_obj_t *label=kind?ctx->iot_status_label:ctx->nmap_status_label;
 if(app_ni_jobs[tab][kind].id){if(label)lv_label_set_text(label,"Module busy");return false;}
 int id=app_ni_start_js(tab,kind?"iot":"nmap",action==1?"connect":action==2?"hosts":"scan",ssid,password,target,level,error,sizeof(error));
 if(!id){if(label)lv_label_set_text(label,error[0]?error:"Unable to start simulation");return false;}
 if(!kind)app_nmap_story_jobs[tab][action]=id;
 app_ni_jobs[tab][kind].id=id;app_ni_jobs[tab][kind].action=action;app_ni_jobs[tab][kind].ctx=ctx;
 app_ni_jobs[tab][kind].sample=-1;
 lv_obj_t *owner=kind?ctx->iot_page:ctx->nmap_page;
 if(app_ni_jobs[tab][kind].owner!=owner){app_ni_jobs[tab][kind].owner=owner;lv_obj_add_event_cb(owner,app_ni_deleted,LV_EVENT_DELETE,NULL);}
 if(label)lv_label_set_text(label,"Running synthetic simulation...");return true;
}


static void app_nmap_hosts_done(char *rx_buffer)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    ctx->nmap_host_count = 0;
    memset(ctx->nmap_our_ip, 0, sizeof(ctx->nmap_our_ip));

    char *line = strtok(rx_buffer, "\n\r");
    while (line != NULL && ctx->nmap_host_count < NMAP_MAX_HOSTS) {
        if (strstr(line, "Our IP:") != NULL) {
            char *ip_start = strstr(line, "Our IP:") + 7;
            while (*ip_start == ' ') ip_start++;
            char *comma = strchr(ip_start, ',');
            if (comma) {
                int len = comma - ip_start;
                if (len > 0 && len < (int)sizeof(ctx->nmap_our_ip)) {
                    strncpy(ctx->nmap_our_ip, ip_start, len);
                    ctx->nmap_our_ip[len] = '\0';
                }
            }
        } else if (strstr(line, "->") != NULL) {
            char ip[20] = {0};
            char mac[18] = {0};
            char *arrow = strstr(line, "->");
            if (arrow) {
                char *p = line;
                while (*p == ' ') p++;
                int ip_len = 0;
                while (*p && *p != ' ' && ip_len < 19) {
                    ip[ip_len++] = *p++;
                }
                ip[ip_len] = '\0';

                p = arrow + 2;
                while (*p == ' ') p++;
                int mac_len = 0;
                while (*p && *p != ' ' && *p != '\n' && mac_len < 17) {
                    mac[mac_len++] = *p++;
                }
                mac[mac_len] = '\0';

                if (strlen(ip) >= 7 && strlen(mac) == 17) {
                    snprintf(ctx->nmap_hosts[ctx->nmap_host_count].ip, sizeof(ctx->nmap_hosts[0].ip), "%s", ip);
                    snprintf(ctx->nmap_hosts[ctx->nmap_host_count].mac, sizeof(ctx->nmap_hosts[0].mac), "%s", mac);
                    ctx->nmap_hosts[ctx->nmap_host_count].port_count = 0;
                    ctx->nmap_hosts[ctx->nmap_host_count].no_open_ports = false;
                    ctx->nmap_host_count++;
                    ESP_LOGI(TAG, "Nmap host %d: %s -> %s", ctx->nmap_host_count, ip, mac);
                }
            }
        }
        line = strtok(NULL, "\n\r");
    }

    bsp_display_lock(0);

    if (ctx->nmap_status_label) {
        if (ctx->nmap_host_count > 0) {
            lv_label_set_text_fmt(ctx->nmap_status_label, "Our IP: %s | Found %d hosts - select target or scan all", ctx->nmap_our_ip, ctx->nmap_host_count);
            lv_obj_set_style_text_color(ctx->nmap_status_label, COLOR_MATERIAL_GREEN, 0);
        } else {
            lv_label_set_text(ctx->nmap_status_label, "No hosts found");
            lv_obj_set_style_text_color(ctx->nmap_status_label, COLOR_MATERIAL_RED, 0);
        }
    }

    if (ctx->nmap_hosts_container) {
        lv_obj_clean(ctx->nmap_hosts_container);

        for (int i = 0; i < ctx->nmap_host_count; i++) {
            lv_obj_t *row = lv_obj_create(ctx->nmap_hosts_container);
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
            app_bind(row, nmap_host_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i, app_template_line("ui.nmap_list_hosts_cb.row.clicked.8bd9da28d94b"));

            lv_obj_t *ip_lbl = lv_label_create(row);
            lv_label_set_text(ip_lbl, ctx->nmap_hosts[i].ip);
            lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ip_lbl, COLOR_MATERIAL_GREEN, 0);
            lv_obj_set_width(ip_lbl, 150);

            lv_obj_t *mac_lbl = lv_label_create(row);
            lv_label_set_text(mac_lbl, ctx->nmap_hosts[i].mac);
            lv_obj_set_style_text_font(mac_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(mac_lbl, lv_color_hex(0x888888), 0);

            lv_obj_t *scan_lbl = lv_label_create(row);
            lv_label_set_text(scan_lbl, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(scan_lbl, COLOR_MATERIAL_GREEN, 0);
        }

        if (ctx->nmap_host_count > 0) {
            ctx->nmap_all_hosts_btn = lv_btn_create(ctx->nmap_hosts_container);
            lv_obj_set_size(ctx->nmap_all_hosts_btn, lv_pct(100), 45);
            lv_obj_set_style_bg_color(ctx->nmap_all_hosts_btn, COLOR_MATERIAL_TEAL, 0);
            lv_obj_set_style_radius(ctx->nmap_all_hosts_btn, 8, 0);
            app_bind(ctx->nmap_all_hosts_btn, nmap_all_hosts_click_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.nmap_list_hosts_cb.nmap_all_hosts_btn.clicked.c7e6dfdb9ed5"));

            lv_obj_t *all_label = lv_label_create(ctx->nmap_all_hosts_btn);
            lv_label_set_text(all_label, LV_SYMBOL_LIST "  Nmap All Hosts");
            lv_obj_set_style_text_font(all_label, &lv_font_montserrat_16, 0);
            lv_obj_center(all_label);
        }
    }
}

static void app_nmap_scan_done(char *rx_buffer)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
 int len=strlen(rx_buffer),line_pos=0,total_ports=0,total_hosts_expected=0,current_host_idx=-1,hosts_completed=0,result_count=0;
 char line_buffer[512]; nmap_host_t *results=calloc(NMAP_MAX_HOSTS,sizeof(nmap_host_t));
 if(!results){lv_label_set_text(ctx->nmap_progress_label,"Not enough memory");return;}
 ctx->nmap_scanning=true;
            for (int i = 0; i < len && ctx->nmap_scanning; i++) {
                char c = rx_buffer[i];
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        char *trimmed = line_buffer;
                        while (*trimmed == ' ') trimmed++;

                        // Scan level: extract total ports
                        if (strstr(line_buffer, "Scan level:") != NULL) {
                            char *paren = strchr(line_buffer, '(');
                            if (paren) {
                                total_ports = atoi(paren + 1);
                                ESP_LOGI(TAG, "Nmap: total ports per host: %d", total_ports);
                            }
                        }
                        // Host count
                        else if (strstr(line_buffer, "host(s),") != NULL) {
                            sscanf(line_buffer, "Scanning %d", &total_hosts_expected);
                            ESP_LOGI(TAG, "Nmap: scanning %d hosts", total_hosts_expected);
                        }
                        // Single-host mode
                        else if (strstr(line_buffer, "Single-host mode") != NULL) {
                            total_hosts_expected = 1;
                        }
                        // New host block
                        else if (strncmp(trimmed, "Host:", 5) == 0) {
                            if (result_count < NMAP_MAX_HOSTS) {
                                char ip[20] = {0};
                                char mac[18] = {0};
                                if (sscanf(trimmed, "Host: %19s (%17[^)])", ip, mac) == 2) {
                                    snprintf(results[result_count].ip, sizeof(results[0].ip), "%s", ip);
                                    snprintf(results[result_count].mac, sizeof(results[0].mac), "%s", mac);
                                } else if (sscanf(trimmed, "Host: %19s (MAC unknown)", ip) == 1) {
                                    snprintf(results[result_count].ip, sizeof(results[0].ip), "%s", ip);
                                    snprintf(results[result_count].mac, sizeof(results[0].mac), "unknown");
                                }
                                results[result_count].port_count = 0;
                                results[result_count].no_open_ports = false;
                                current_host_idx = result_count;
                                result_count++;
                            }
                        }
                        // Progress: "Scanning X.X.X.X ports N-M [current/total]"
                        else if (strstr(trimmed, "Scanning") != NULL && strstr(trimmed, "ports") != NULL && strchr(trimmed, '[')) {
                            int current = 0, total = 0;
                            char *bracket = strchr(trimmed, '[');
                            if (bracket) {
                                sscanf(bracket, "[%d/%d]", &current, &total);
                                if (total > 0) {
                                    int overall_pct = 0;
                                    if (total_hosts_expected > 0 && total_ports > 0) {
                                        int completed_ports = hosts_completed * total_ports + current;
                                        int total_all_ports = total_hosts_expected * total_ports;
                                        overall_pct = (completed_ports * 100) / total_all_ports;
                                    } else if (total > 0) {
                                        overall_pct = (current * 100) / total;
                                    }
                                    if (overall_pct > 100) overall_pct = 100;

                                    if (bsp_display_lock(0)) {
                                        if (ctx && ctx->nmap_progress_bar) {
                                            lv_bar_set_value(ctx->nmap_progress_bar, overall_pct, LV_ANIM_ON);
                                        }
                                        if (ctx && ctx->nmap_progress_label) {
                                            lv_label_set_text_fmt(ctx->nmap_progress_label, "%d%%", overall_pct);
                                        }
                                        bsp_display_unlock();
                                    }
                                }
                            }
                        }
                        // Open port: "  135/tcp  open  MSRPC"
                        else if (strstr(trimmed, "/tcp") != NULL && strstr(trimmed, "open") != NULL) {
                            int port = 0;
                            char service[32] = {0};
                            if (sscanf(trimmed, "%d/tcp %*s %31s", &port, service) >= 1 ||
                                sscanf(trimmed, "%d/tcp  open  %31s", &port, service) >= 1) {
                                if (current_host_idx >= 0 && current_host_idx < result_count) {
                                    nmap_host_t *h = &results[current_host_idx];
                                    if (h->port_count < NMAP_MAX_PORTS_PER_HOST) {
                                        h->ports[h->port_count].port = port;
                                        snprintf(h->ports[h->port_count].service, sizeof(h->ports[0].service), "%s", service);
                                        h->port_count++;

                                        if (bsp_display_lock(0)) {
                                            if (ctx && ctx->nmap_results_container) {
                                                lv_obj_t *port_row = lv_obj_create(ctx->nmap_results_container);
                                                lv_obj_set_size(port_row, lv_pct(100), LV_SIZE_CONTENT);
                                                lv_obj_set_style_bg_color(port_row, lv_color_hex(0x1A2A1A), 0);
                                                lv_obj_set_style_border_width(port_row, 0, 0);
                                                lv_obj_set_style_radius(port_row, 4, 0);
                                                lv_obj_set_style_pad_all(port_row, 6, 0);
                                                lv_obj_set_flex_flow(port_row, LV_FLEX_FLOW_ROW);
                                                lv_obj_set_flex_align(port_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                                                lv_obj_set_style_pad_column(port_row, 10, 0);
                                                lv_obj_clear_flag(port_row, LV_OBJ_FLAG_SCROLLABLE);

                                                lv_obj_t *host_lbl = lv_label_create(port_row);
                                                lv_label_set_text_fmt(host_lbl, "  %s", h->ip);
                                                lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_12, 0);
                                                lv_obj_set_style_text_color(host_lbl, lv_color_hex(0xAAAAAA), 0);
                                                lv_obj_set_width(host_lbl, 140);

                                                lv_obj_t *port_lbl = lv_label_create(port_row);
                                                lv_label_set_text_fmt(port_lbl, "%d/tcp", port);
                                                lv_obj_set_style_text_font(port_lbl, &lv_font_montserrat_14, 0);
                                                lv_obj_set_style_text_color(port_lbl, COLOR_MATERIAL_GREEN, 0);
                                                lv_obj_set_width(port_lbl, 90);

                                                lv_obj_t *svc_lbl = lv_label_create(port_row);
                                                lv_label_set_text(svc_lbl, service);
                                                lv_obj_set_style_text_font(svc_lbl, &lv_font_montserrat_14, 0);
                                                lv_obj_set_style_text_color(svc_lbl, lv_color_hex(0xCCCCCC), 0);
                                            }
                                            bsp_display_unlock();
                                        }
                                    }
                                }
                            }
                        }
                        // No open ports
                        else if (strstr(trimmed, "(no open ports)") != NULL) {
                            if (current_host_idx >= 0 && current_host_idx < result_count) {
                                results[current_host_idx].no_open_ports = true;
                            }
                            hosts_completed++;
                        }
                        // Completion: "Scanned N hosts, found M open ports"
                        else if (strstr(line_buffer, "Scanned") != NULL && strstr(line_buffer, "open ports") != NULL) {
                            int scanned_hosts = 0, found_ports = 0;
                            sscanf(line_buffer, "Scanned %d hosts, found %d open ports", &scanned_hosts, &found_ports);
                            ESP_LOGI(TAG, "Nmap: Complete - %d hosts, %d open ports", scanned_hosts, found_ports);

                            if (bsp_display_lock(0)) {
                                if (ctx && ctx->nmap_progress_bar) {
                                    lv_bar_set_value(ctx->nmap_progress_bar, 100, LV_ANIM_ON);
                                }
                                if (ctx && ctx->nmap_progress_label) {
                                    lv_label_set_text_fmt(ctx->nmap_progress_label,
                                        "Done! %d hosts, %d open ports", scanned_hosts, found_ports);
                                    lv_obj_set_style_text_color(ctx->nmap_progress_label, COLOR_MATERIAL_GREEN, 0);
                                }
                                bsp_display_unlock();
                            }
                            ctx->nmap_scanning = false;
                            break;
                        }
                        // When a new Host: appears after the first, the previous is done
                        if (strncmp(trimmed, "Host:", 5) == 0 && result_count > 1) {
                            hosts_completed = result_count - 1;
                        }

                        line_pos = 0;
                    }
                } else if (line_pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
    // Build final results summary
    if (bsp_display_lock(0)) {
        if (ctx && ctx->nmap_results_container) {
            // Add summary header for hosts with no ports
            for (int h = 0; h < result_count; h++) {
                if (results[h].no_open_ports && results[h].port_count == 0) {
                    lv_obj_t *no_port_row = lv_obj_create(ctx->nmap_results_container);
                    lv_obj_set_size(no_port_row, lv_pct(100), LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(no_port_row, lv_color_hex(0x2A1A1A), 0);
                    lv_obj_set_style_border_width(no_port_row, 0, 0);
                    lv_obj_set_style_radius(no_port_row, 4, 0);
                    lv_obj_set_style_pad_all(no_port_row, 6, 0);
                    lv_obj_clear_flag(no_port_row, LV_OBJ_FLAG_SCROLLABLE);

                    lv_obj_t *lbl = lv_label_create(no_port_row);
                    lv_label_set_text_fmt(lbl, "  %s (%s) - no open ports",
                        results[h].ip, results[h].mac);
                    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
                }
            }
        }
        bsp_display_unlock();
    }


 ctx->nmap_scanning=false; free(results);
}

static void nmap_keyboard_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void nmap_password_input_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    (void)e;
    if (ctx->nmap_keyboard) {
        lv_obj_clear_flag(ctx->nmap_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ctx->nmap_keyboard, ctx->nmap_password_input);
    }
}

static void nmap_back_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    (void)e;
    app_ni_cancel(ctx,0);
    nmap_results_popup_close_cb(NULL);
    nmap_scan_type_popup_close_cb(NULL);
    ESP_LOGI(TAG, "Nmap: back button pressed");

    if (ctx->nmap_scanning) {
        ctx->nmap_scanning = false;
        /* cancellation handled by model */
        if (ctx->nmap_scan_task) {

            ctx->nmap_scan_task = NULL;
        }
    }

    ctx->nmap_wifi_connected = false;
    ctx->nmap_host_count = 0;
    memset(ctx->nmap_target_ssid, 0, sizeof(ctx->nmap_target_ssid));
    memset(ctx->nmap_target_security, 0, sizeof(ctx->nmap_target_security));
    memset(ctx->nmap_target_password, 0, sizeof(ctx->nmap_target_password));
    memset(ctx->nmap_our_ip, 0, sizeof(ctx->nmap_our_ip));
    memset(ctx->nmap_scan_target_ip, 0, sizeof(ctx->nmap_scan_target_ip));

    if (ctx->nmap_page) {
        lv_obj_del(ctx->nmap_page);
        ctx->nmap_page = NULL;
        ctx->nmap_password_input = NULL;
        ctx->nmap_keyboard = NULL;
        ctx->nmap_connect_btn = NULL;
        ctx->nmap_status_label = NULL;
        ctx->nmap_hosts_container = NULL;
        ctx->nmap_list_hosts_btn = NULL;
        ctx->nmap_all_hosts_btn = NULL;
    }


    bool return_to_observer = ctx && ctx->observer_attack_return_to_observer;

    if (ctx) {
        ctx->nmap_page = NULL;
        ctx->observer_attack_return_to_observer = false;
        clear_observer_attack_override(ctx);
    }

    if (return_to_observer) {
        show_observer_page();
        // Restart popup timer so client polling resumes
        if (ctx && ctx->popup_open && ctx->popup_timer != NULL) {
            /* Observer polling is driven by app_observer_tick. */
        }
        return;
    }

    show_scan_page();
}

static void nmap_host_click_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ctx->nmap_host_count) return;
    ESP_LOGI(TAG, "Nmap: Selected host %s for scan", ctx->nmap_hosts[idx].ip);
    nmap_show_scan_type_popup(ctx->nmap_hosts[idx].ip);
}

static void nmap_all_hosts_click_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    (void)e;
    ESP_LOGI(TAG, "Nmap: Scan all hosts selected");
    nmap_show_scan_type_popup(NULL);
}

static void nmap_scan_type_popup_close_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    (void)e;

    if (ctx && ctx->nmap_scan_popup_overlay) {
        lv_obj_del(ctx->nmap_scan_popup_overlay);
        ctx->nmap_scan_popup_overlay = NULL;
        ctx->nmap_scan_popup = NULL;
    }
}

static void nmap_scan_type_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    const char *scan_level = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Nmap: Scan type selected: %s", scan_level);


    if (ctx && ctx->nmap_scan_popup_overlay) {
        lv_obj_del(ctx->nmap_scan_popup_overlay);
        ctx->nmap_scan_popup_overlay = NULL;
        ctx->nmap_scan_popup = NULL;
    }

    nmap_start_scan(ctx->nmap_scan_target_ip, scan_level);
}

static void nmap_show_scan_type_popup(const char *target_ip)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;

    if (!ctx) return;

    if (target_ip && strlen(target_ip) > 0) {
        snprintf(ctx->nmap_scan_target_ip, sizeof(ctx->nmap_scan_target_ip), "%s", target_ip);
    } else {
        memset(ctx->nmap_scan_target_ip, 0, sizeof(ctx->nmap_scan_target_ip));
    }

    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    ctx->nmap_scan_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->nmap_scan_popup_overlay);
    lv_obj_set_size(ctx->nmap_scan_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->nmap_scan_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->nmap_scan_popup_overlay, LV_OPA_50, 0);
    lv_obj_clear_flag(ctx->nmap_scan_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->nmap_scan_popup_overlay, LV_OBJ_FLAG_CLICKABLE);

    ctx->nmap_scan_popup = lv_obj_create(ctx->nmap_scan_popup_overlay);
    lv_obj_set_size(ctx->nmap_scan_popup, 500, 360);
    lv_obj_center(ctx->nmap_scan_popup);
    lv_obj_set_style_bg_color(ctx->nmap_scan_popup, lv_color_hex(0x1A1A2A), 0);
    lv_obj_set_style_border_color(ctx->nmap_scan_popup, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_border_width(ctx->nmap_scan_popup, 2, 0);
    lv_obj_set_style_radius(ctx->nmap_scan_popup, 16, 0);
    lv_obj_set_style_shadow_width(ctx->nmap_scan_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctx->nmap_scan_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->nmap_scan_popup, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(ctx->nmap_scan_popup, 20, 0);
    lv_obj_set_flex_flow(ctx->nmap_scan_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->nmap_scan_popup, 12, 0);
    lv_obj_set_flex_align(ctx->nmap_scan_popup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(ctx->nmap_scan_popup);
    if (ctx->nmap_scan_target_ip[0]) {
        lv_label_set_text_fmt(title, LV_SYMBOL_LIST "  Nmap Scan - %s", ctx->nmap_scan_target_ip);
    } else {
        lv_label_set_text(title, LV_SYMBOL_LIST "  Nmap Scan - All Hosts");
    }
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_GREEN, 0);

    lv_obj_t *subtitle = lv_label_create(ctx->nmap_scan_popup);
    lv_label_set_text(subtitle, "Select scan depth:");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xAAAAAA), 0);

    // Quick button
    lv_obj_t *quick_btn = lv_btn_create(ctx->nmap_scan_popup);
    lv_obj_set_size(quick_btn, lv_pct(90), 60);
    lv_obj_set_style_bg_color(quick_btn, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_radius(quick_btn, 10, 0);
    app_bind(quick_btn, nmap_scan_type_cb, LV_EVENT_CLICKED, (void*)"quick", app_template_line("ui.nmap_show_scan_type_popup.quick_btn.clicked.b2e7a08f8cdd"));
    lv_obj_set_flex_flow(quick_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(quick_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ql1 = lv_label_create(quick_btn);
    lv_label_set_text(ql1, "Quick (20 ports)");
    lv_obj_set_style_text_font(ql1, &lv_font_montserrat_16, 0);
    lv_obj_t *ql2 = lv_label_create(quick_btn);
    lv_label_set_text(ql2, "FTP, SSH, HTTP, SMB, RDP");
    lv_obj_set_style_text_font(ql2, &lv_font_montserrat_12, 0);

    // Medium button
    lv_obj_t *med_btn = lv_btn_create(ctx->nmap_scan_popup);
    lv_obj_set_size(med_btn, lv_pct(90), 60);
    lv_obj_set_style_bg_color(med_btn, COLOR_MATERIAL_AMBER, 0);
    lv_obj_set_style_radius(med_btn, 10, 0);
    app_bind(med_btn, nmap_scan_type_cb, LV_EVENT_CLICKED, (void*)"medium", app_template_line("ui.nmap_show_scan_type_popup.med_btn.clicked.e863ac6a13ce"));
    lv_obj_set_flex_flow(med_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(med_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ml1 = lv_label_create(med_btn);
    lv_label_set_text(ml1, "Medium (50 ports)");
    lv_obj_set_style_text_font(ml1, &lv_font_montserrat_16, 0);
    lv_obj_t *ml2 = lv_label_create(med_btn);
    lv_label_set_text(ml2, "LDAP, MQTT, Docker, Redis");
    lv_obj_set_style_text_font(ml2, &lv_font_montserrat_12, 0);

    // Heavy button
    lv_obj_t *heavy_btn = lv_btn_create(ctx->nmap_scan_popup);
    lv_obj_set_size(heavy_btn, lv_pct(90), 60);
    lv_obj_set_style_bg_color(heavy_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(heavy_btn, 10, 0);
    app_bind(heavy_btn, nmap_scan_type_cb, LV_EVENT_CLICKED, (void*)"heavy", app_template_line("ui.nmap_show_scan_type_popup.heavy_btn.clicked.4f965d7b8814"));
    lv_obj_set_flex_flow(heavy_btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(heavy_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hl1 = lv_label_create(heavy_btn);
    lv_label_set_text(hl1, "Heavy (100 ports)");
    lv_obj_set_style_text_font(hl1, &lv_font_montserrat_16, 0);
    lv_obj_t *hl2 = lv_label_create(heavy_btn);
    lv_label_set_text(hl2, "TFTP, BGP, Modbus, MongoDB, Minecraft");
    lv_obj_set_style_text_font(hl2, &lv_font_montserrat_12, 0);

    // Close button
    lv_obj_t *close_btn = lv_btn_create(ctx->nmap_scan_popup);
    lv_obj_set_size(close_btn, 100, 36);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    app_bind(close_btn, nmap_scan_type_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.nmap_show_scan_type_popup.close_btn.clicked.c7a5f0cb99d9"));
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Cancel");
    lv_obj_center(close_label);
}

static void nmap_results_popup_close_cb(lv_event_t *e)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    (void)e;
    app_ni_cancel(get_current_ctx(),0);
    ESP_LOGI(TAG, "Nmap: results popup closed - sending stop");

    if (ctx->nmap_scanning) {
        ctx->nmap_scanning = false;
        /* cancellation handled by model */
    }


    if (ctx && ctx->nmap_results_popup_overlay) {
        lv_obj_del(ctx->nmap_results_popup_overlay);
        ctx->nmap_results_popup_overlay = NULL;
        ctx->nmap_results_popup = NULL;
        ctx->nmap_progress_bar = NULL;
        ctx->nmap_progress_label = NULL;
        ctx->nmap_results_container = NULL;
    }
}

static void show_nmap_page(void)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;
    snprintf(ctx->nmap_target_ssid,sizeof(ctx->nmap_target_ssid),"%s",nmap_target_ssid);
    snprintf(ctx->nmap_target_security,sizeof(ctx->nmap_target_security),"%s",nmap_target_security);
    if(!ctx->nmap_hosts)ctx->nmap_hosts=calloc(NMAP_MAX_HOSTS,sizeof(nmap_host_t));
    if(!ctx->nmap_hosts)return;
    ESP_LOGI(TAG, "Showing Nmap page for SSID: %s", ctx->nmap_target_ssid);


    if (!ctx) return;



    ctx->nmap_wifi_connected = false;
    ctx->nmap_host_count = 0;
    memset(ctx->nmap_our_ip, 0, sizeof(ctx->nmap_our_ip));
    memset(ctx->nmap_target_password, 0, sizeof(ctx->nmap_target_password));
    memset(ctx->nmap_scan_target_ip, 0, sizeof(ctx->nmap_scan_target_ip));

    if (ctx->scan_page) {
        lv_obj_add_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    ctx->nmap_page = lv_obj_create(container);
    ctx->nmap_page = ctx->nmap_page;
    lv_obj_set_size(ctx->nmap_page, lv_pct(100), lv_pct(100));
    lv_obj_align(ctx->nmap_page, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->nmap_page, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(ctx->nmap_page, 0, 0);
    lv_obj_set_style_pad_all(ctx->nmap_page, 15, 0);
    lv_obj_set_flex_flow(ctx->nmap_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->nmap_page, 10, 0);

    // Header
    lv_obj_t *header = lv_obj_create(ctx->nmap_page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 15, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x444444), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 8, 0);
    app_bind(back_btn, nmap_back_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.back_btn.clicked.15017ea97945"));

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, LV_SYMBOL_LIST "  Nmap Port Scanner");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_GREEN, 0);

    // Target info
    lv_obj_t *target_label = lv_label_create(ctx->nmap_page);
    lv_label_set_text_fmt(target_label, "Target: %s", ctx->nmap_target_ssid);
    lv_obj_set_style_text_font(target_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(target_label, lv_color_hex(0xCCCCCC), 0);

    bool is_open_network = wifi_network_security_is_open(ctx->nmap_target_security);
    bool password_known = false;

    /* Synthetic sessions ask for a password; no saved credential transport. */

    // Connect section
    lv_obj_t *pass_section = lv_obj_create(ctx->nmap_page);
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

        lv_obj_t *pass_left = lv_obj_create(pass_section);
        lv_obj_set_size(pass_left, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(pass_left, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pass_left, 0, 0);
        lv_obj_set_style_pad_all(pass_left, 0, 0);
        lv_obj_set_flex_flow(pass_left, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(pass_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pass_left, 10, 0);
        lv_obj_clear_flag(pass_left, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *pass_label = lv_label_create(pass_left);
        lv_label_set_text(pass_label, "Password:");
        lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);

        ctx->nmap_password_input = lv_textarea_create(pass_left);
        lv_obj_set_size(ctx->nmap_password_input, 300, 40);
        lv_textarea_set_one_line(ctx->nmap_password_input, true);
        lv_textarea_set_placeholder_text(ctx->nmap_password_input, "Optional");
        lv_textarea_set_password_mode(ctx->nmap_password_input, true);
        lv_textarea_set_password_show_time(ctx->nmap_password_input, WPASEC_PASSWORD_SHOW_MS);
        lv_obj_set_style_bg_color(ctx->nmap_password_input, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(ctx->nmap_password_input, COLOR_MATERIAL_GREEN, 0);
        lv_obj_set_style_border_width(ctx->nmap_password_input, 1, 0);
        lv_obj_set_style_text_color(ctx->nmap_password_input, lv_color_hex(0xFFFFFF), 0);
        app_bind(ctx->nmap_password_input, nmap_password_input_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_password_input.clicked.2439d5a7df43"));

        lv_obj_t *btn_row = lv_obj_create(pass_section);
        lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_row, 0, 0);
        lv_obj_set_style_pad_all(btn_row, 0, 0);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn_row, 15, 0);
        lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

        ctx->nmap_connect_btn = lv_btn_create(btn_row);
        lv_obj_set_size(ctx->nmap_connect_btn, 120, 40);
        lv_obj_set_style_bg_color(ctx->nmap_connect_btn, COLOR_MATERIAL_GREEN, 0);
        lv_obj_set_style_radius(ctx->nmap_connect_btn, 8, 0);
        app_bind(ctx->nmap_connect_btn, nmap_connect_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_connect_btn.clicked.885805443e5b"));

        lv_obj_t *connect_label = lv_label_create(ctx->nmap_connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
        lv_obj_center(connect_label);

        ctx->nmap_list_hosts_btn = lv_btn_create(btn_row);
        lv_obj_set_size(ctx->nmap_list_hosts_btn, 120, 40);
        lv_obj_set_style_bg_color(ctx->nmap_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_radius(ctx->nmap_list_hosts_btn, 8, 0);
        app_bind(ctx->nmap_list_hosts_btn, nmap_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_list_hosts_btn.clicked.db6d72c4124a"));
        lv_obj_add_flag(ctx->nmap_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *list_hosts_label = lv_label_create(ctx->nmap_list_hosts_btn);
        lv_label_set_text(list_hosts_label, "List Hosts");
        lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
        lv_obj_center(list_hosts_label);
    } else if (password_known) {
        lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *pass_title = lv_label_create(pass_section);
        lv_label_set_text(pass_title, "Saved password available");
        lv_obj_set_style_text_font(pass_title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pass_title, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t *pass_value = lv_label_create(pass_section);
        lv_label_set_text(pass_value, "Firmware will use --saved");
        lv_obj_set_style_text_font(pass_value, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pass_value, COLOR_MATERIAL_GREEN, 0);

        lv_obj_t *btn_row = lv_obj_create(pass_section);
        lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_row, 0, 0);
        lv_obj_set_style_pad_all(btn_row, 0, 0);
        lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn_row, 15, 0);
        lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

        ctx->nmap_connect_btn = lv_btn_create(btn_row);
        lv_obj_set_size(ctx->nmap_connect_btn, 120, 40);
        lv_obj_set_style_bg_color(ctx->nmap_connect_btn, COLOR_MATERIAL_GREEN, 0);
        lv_obj_set_style_radius(ctx->nmap_connect_btn, 8, 0);
        app_bind(ctx->nmap_connect_btn, nmap_connect_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_connect_btn.clicked.9c120d3d61ab"));

        lv_obj_t *connect_label = lv_label_create(ctx->nmap_connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
        lv_obj_center(connect_label);

        ctx->nmap_list_hosts_btn = lv_btn_create(btn_row);
        lv_obj_set_size(ctx->nmap_list_hosts_btn, 120, 40);
        lv_obj_set_style_bg_color(ctx->nmap_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_radius(ctx->nmap_list_hosts_btn, 8, 0);
        app_bind(ctx->nmap_list_hosts_btn, nmap_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_list_hosts_btn.clicked.dbfd290a2b7e"));
        lv_obj_add_flag(ctx->nmap_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *list_hosts_label = lv_label_create(ctx->nmap_list_hosts_btn);
        lv_label_set_text(list_hosts_label, "List Hosts");
        lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
        lv_obj_center(list_hosts_label);
    } else {
        lv_obj_set_flex_flow(pass_section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(pass_section, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pass_section, 15, 0);

        lv_obj_t *pass_left = lv_obj_create(pass_section);
        lv_obj_set_size(pass_left, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(pass_left, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pass_left, 0, 0);
        lv_obj_set_style_pad_all(pass_left, 0, 0);
        lv_obj_set_flex_flow(pass_left, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(pass_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(pass_left, 10, 0);
        lv_obj_clear_flag(pass_left, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *pass_label = lv_label_create(pass_left);
        lv_label_set_text(pass_label, "Password:");
        lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pass_label, lv_color_hex(0xFFFFFF), 0);

        ctx->nmap_password_input = lv_textarea_create(pass_left);
        lv_obj_set_size(ctx->nmap_password_input, 300, 40);
        lv_textarea_set_one_line(ctx->nmap_password_input, true);
        lv_textarea_set_placeholder_text(ctx->nmap_password_input, "WiFi password");
        lv_obj_set_style_bg_color(ctx->nmap_password_input, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_border_color(ctx->nmap_password_input, COLOR_MATERIAL_GREEN, 0);
        lv_obj_set_style_border_width(ctx->nmap_password_input, 1, 0);
        lv_obj_set_style_text_color(ctx->nmap_password_input, lv_color_hex(0xFFFFFF), 0);
        app_bind(ctx->nmap_password_input, nmap_password_input_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_password_input.clicked.35034580f821"));

        ctx->nmap_connect_btn = lv_btn_create(pass_section);
        lv_obj_set_size(ctx->nmap_connect_btn, 120, 40);
        lv_obj_set_style_bg_color(ctx->nmap_connect_btn, COLOR_MATERIAL_GREEN, 0);
        lv_obj_set_style_radius(ctx->nmap_connect_btn, 8, 0);
        app_bind(ctx->nmap_connect_btn, nmap_connect_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_connect_btn.clicked.b2b5790acc70"));

        lv_obj_t *connect_label = lv_label_create(ctx->nmap_connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_16, 0);
        lv_obj_center(connect_label);

        ctx->nmap_list_hosts_btn = lv_btn_create(pass_section);
        lv_obj_set_size(ctx->nmap_list_hosts_btn, 120, 40);
        lv_obj_set_style_bg_color(ctx->nmap_list_hosts_btn, COLOR_MATERIAL_CYAN, 0);
        lv_obj_set_style_radius(ctx->nmap_list_hosts_btn, 8, 0);
        app_bind(ctx->nmap_list_hosts_btn, nmap_list_hosts_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_nmap_page.nmap_list_hosts_btn.clicked.efee05f0b13f"));
        lv_obj_add_flag(ctx->nmap_list_hosts_btn, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *list_hosts_label = lv_label_create(ctx->nmap_list_hosts_btn);
        lv_label_set_text(list_hosts_label, "List Hosts");
        lv_obj_set_style_text_font(list_hosts_label, &lv_font_montserrat_16, 0);
        lv_obj_center(list_hosts_label);
    }

    // Status label
    ctx->nmap_status_label = lv_label_create(ctx->nmap_page);
    if (is_open_network) {
        lv_label_set_text(ctx->nmap_status_label, "Press Connect to try open, or enter password");
    } else if (password_known) {
        lv_label_set_text(ctx->nmap_status_label, "Press Connect to join network");
    } else {
        lv_label_set_text(ctx->nmap_status_label, "Enter WiFi password to connect");
    }
    lv_obj_set_style_text_color(ctx->nmap_status_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(ctx->nmap_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(ctx->nmap_status_label, lv_pct(100));

    // Hosts container (scrollable)
    ctx->nmap_hosts_container = lv_obj_create(ctx->nmap_page);
    lv_obj_set_size(ctx->nmap_hosts_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ctx->nmap_hosts_container, 1);
    lv_obj_set_style_bg_color(ctx->nmap_hosts_container, lv_color_hex(0x252525), 0);
    lv_obj_set_style_border_width(ctx->nmap_hosts_container, 0, 0);
    lv_obj_set_style_radius(ctx->nmap_hosts_container, 8, 0);
    lv_obj_set_style_pad_all(ctx->nmap_hosts_container, 8, 0);
    lv_obj_set_style_pad_row(ctx->nmap_hosts_container, 5, 0);
    lv_obj_set_flex_flow(ctx->nmap_hosts_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ctx->nmap_hosts_container, LV_DIR_VER);

    lv_obj_t *placeholder = lv_label_create(ctx->nmap_hosts_container);
    lv_label_set_text(placeholder, "Connect to network, then List Hosts to scan for targets");
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x888888), 0);

    // Keyboard (hidden, for password entry)
    if (ctx->nmap_password_input && !password_known) {
        ctx->nmap_keyboard = lv_keyboard_create(ctx->nmap_page);
        lv_obj_set_size(ctx->nmap_keyboard, lv_pct(100), 200);
        lv_obj_add_flag(ctx->nmap_keyboard, LV_OBJ_FLAG_HIDDEN);
        app_bind(ctx->nmap_keyboard, nmap_keyboard_cb, LV_EVENT_ALL, NULL, app_template_line("ui.show_nmap_page.nmap_keyboard.all.6101e816847d"));
    }

    ctx->current_visible_page = ctx->nmap_page;
}

static void app_ni_popup_deleted(lv_event_t *e){
 tab_context_t *ctx=lv_event_get_user_data(e);app_ni_cancel(ctx,0);
 ctx->nmap_results_popup_overlay=NULL;ctx->nmap_results_popup=NULL;
 ctx->nmap_progress_bar=NULL;ctx->nmap_progress_label=NULL;ctx->nmap_results_container=NULL;
}

static void nmap_start_scan(const char *target_ip, const char *scan_level)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;

    if (!ctx) return;

    if(!ctx->nmap_wifi_connected){lv_label_set_text(ctx->nmap_status_label,"Connect first");return;}
    if(!app_ni_begin(ctx,0,3,ctx->nmap_target_ssid,"",target_ip&&target_ip[0]?target_ip:"all",scan_level))return;
    ctx->nmap_scanning = true;
    /* Keep discovery rows stable while a separate scan result is displayed. */

    // Create results popup
    lv_obj_t *container = get_current_tab_container();
    if (!container) return;

    ctx->nmap_results_popup_overlay = lv_obj_create(container);
    lv_obj_remove_style_all(ctx->nmap_results_popup_overlay);
    lv_obj_set_size(ctx->nmap_results_popup_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->nmap_results_popup_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->nmap_results_popup_overlay, LV_OPA_70, 0);
    lv_obj_clear_flag(ctx->nmap_results_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->nmap_results_popup_overlay, LV_OBJ_FLAG_CLICKABLE);

    ctx->nmap_results_popup = lv_obj_create(ctx->nmap_results_popup_overlay);
    lv_obj_set_size(ctx->nmap_results_popup, lv_pct(92), lv_pct(88));
    lv_obj_center(ctx->nmap_results_popup);
    lv_obj_set_style_bg_color(ctx->nmap_results_popup, lv_color_hex(0x111118), 0);
    lv_obj_set_style_border_color(ctx->nmap_results_popup, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_border_width(ctx->nmap_results_popup, 2, 0);
    lv_obj_set_style_radius(ctx->nmap_results_popup, 16, 0);
    lv_obj_set_style_pad_all(ctx->nmap_results_popup, 16, 0);
    lv_obj_set_flex_flow(ctx->nmap_results_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->nmap_results_popup, 8, 0);
    lv_obj_clear_flag(ctx->nmap_results_popup, LV_OBJ_FLAG_SCROLLABLE);

    // Title row
    lv_obj_t *title_row = lv_obj_create(ctx->nmap_results_popup);
    lv_obj_set_size(title_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_row);
    if (target_ip && strlen(target_ip) > 0) {
        lv_label_set_text_fmt(title, LV_SYMBOL_LIST "  Nmap %s (%s)", target_ip, scan_level);
    } else {
        lv_label_set_text_fmt(title, LV_SYMBOL_LIST "  Nmap All Hosts (%s)", scan_level);
    }
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_GREEN, 0);

    lv_obj_t *stop_btn = lv_btn_create(title_row);
    lv_obj_set_size(stop_btn, 90, 36);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_radius(stop_btn, 8, 0);
    app_bind(stop_btn, nmap_results_popup_close_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.nmap_start_scan.stop_btn.clicked.e50bda4e3641"));
    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, "STOP");
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(stop_lbl);

    lv_obj_add_event_cb(ctx->nmap_results_popup_overlay,app_ni_popup_deleted,LV_EVENT_DELETE,ctx);

    // Progress bar row
    lv_obj_t *prog_row = lv_obj_create(ctx->nmap_results_popup);
    lv_obj_set_size(prog_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(prog_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prog_row, 0, 0);
    lv_obj_set_style_pad_all(prog_row, 0, 0);
    lv_obj_set_flex_flow(prog_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prog_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(prog_row, 12, 0);
    lv_obj_clear_flag(prog_row, LV_OBJ_FLAG_SCROLLABLE);

    ctx->nmap_progress_bar = lv_bar_create(prog_row);
    lv_obj_set_size(ctx->nmap_progress_bar, lv_pct(75), 20);
    lv_bar_set_range(ctx->nmap_progress_bar, 0, 100);
    lv_bar_set_value(ctx->nmap_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ctx->nmap_progress_bar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(ctx->nmap_progress_bar, COLOR_MATERIAL_GREEN, LV_PART_INDICATOR);

    ctx->nmap_progress_label = lv_label_create(prog_row);
    lv_label_set_text(ctx->nmap_progress_label, "Scanning...");
    lv_obj_set_style_text_font(ctx->nmap_progress_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->nmap_progress_label, COLOR_MATERIAL_AMBER, 0);

    // Column header
    lv_obj_t *hdr_row = lv_obj_create(ctx->nmap_results_popup);
    lv_obj_set_size(hdr_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(hdr_row, lv_color_hex(0x222233), 0);
    lv_obj_set_style_border_width(hdr_row, 0, 0);
    lv_obj_set_style_radius(hdr_row, 4, 0);
    lv_obj_set_style_pad_all(hdr_row, 6, 0);
    lv_obj_set_flex_flow(hdr_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr_row, 10, 0);
    lv_obj_clear_flag(hdr_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr_host = lv_label_create(hdr_row);
    lv_label_set_text(hdr_host, "HOST");
    lv_obj_set_style_text_font(hdr_host, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_host, lv_color_hex(0x888888), 0);
    lv_obj_set_width(hdr_host, 140);

    lv_obj_t *hdr_port = lv_label_create(hdr_row);
    lv_label_set_text(hdr_port, "PORT");
    lv_obj_set_style_text_font(hdr_port, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_port, lv_color_hex(0x888888), 0);
    lv_obj_set_width(hdr_port, 90);

    lv_obj_t *hdr_svc = lv_label_create(hdr_row);
    lv_label_set_text(hdr_svc, "SERVICE");
    lv_obj_set_style_text_font(hdr_svc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_svc, lv_color_hex(0x888888), 0);

    // Scrollable results container
    ctx->nmap_results_container = lv_obj_create(ctx->nmap_results_popup);
    lv_obj_set_size(ctx->nmap_results_container, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ctx->nmap_results_container, 1);
    lv_obj_set_style_bg_color(ctx->nmap_results_container, lv_color_hex(0x0D0D15), 0);
    lv_obj_set_style_border_width(ctx->nmap_results_container, 0, 0);
    lv_obj_set_style_radius(ctx->nmap_results_container, 6, 0);
    lv_obj_set_style_pad_all(ctx->nmap_results_container, 4, 0);
    lv_obj_set_style_pad_row(ctx->nmap_results_container, 3, 0);
    lv_obj_set_flex_flow(ctx->nmap_results_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ctx->nmap_results_container, LV_DIR_VER);


}


static void nmap_connect_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 if(!ctx->nmap_target_ssid[0]){lv_label_set_text(ctx->nmap_status_label,"Select a network first");return;}
 const char *password=ctx->nmap_password_input?lv_textarea_get_text(ctx->nmap_password_input):"";
 if(!wifi_network_security_is_open(ctx->nmap_target_security)&&!password[0]){lv_label_set_text(ctx->nmap_status_label,"Enter password first");return;}
 app_ni_begin(ctx,0,1,ctx->nmap_target_ssid,password,"","");
}
static void nmap_list_hosts_cb(lv_event_t *e){
 (void)e;tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 if(!ctx->nmap_wifi_connected){lv_label_set_text(ctx->nmap_status_label,"Connect first");return;}
 app_ni_begin(ctx,0,2,ctx->nmap_target_ssid,"","","");
}
static void iot_recon_start_cb(lv_event_t *e){
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)ctx=get_current_ctx();if(!ctx||!iot_recon_ensure_cache(ctx))return;
 if(!app_ni_begin(ctx,1,3,"","","",""))return;
 app_mesh_story_run[tab_id_for_ctx(ctx)]=app_ni_jobs[tab_id_for_ctx(ctx)][1].id;
 ctx->iot_recon_monitoring=true;ctx->iot_pan_count=ctx->iot_node_count=ctx->iot_edge_count=0;
 ctx->iot_expanded_pan[0]=ctx->iot_tracked_pan[0]=ctx->iot_tracked_addr[0]=0;
 lv_label_set_text(ctx->iot_channel_label,"--");lv_label_set_text(ctx->iot_packets_label,"0");
 lv_label_set_text(ctx->iot_networks_label,"0");lv_label_set_text(ctx->iot_dropped_label,"0");
 lv_label_set_text(ctx->iot_status_label,"Monitoring synthetic Mesh - waiting for networks...");
 iot_recon_show_empty_locked(ctx);lv_obj_add_state(ctx->iot_start_btn,LV_STATE_DISABLED);lv_obj_clear_state(ctx->iot_stop_btn,LV_STATE_DISABLED);
}
static void iot_recon_stop_cb(lv_event_t *e){
 tab_context_t *ctx=lv_event_get_user_data(e);if(!ctx)ctx=get_current_ctx();if(!ctx)return;
 if(ctx->iot_recon_monitoring)app_mesh_story_stopped[tab_id_for_ctx(ctx)]=app_mesh_story_run[tab_id_for_ctx(ctx)];
 app_ni_cancel(ctx,1);lv_label_set_text(ctx->iot_status_label,"Mesh recon stopped - results stay visible");
 lv_obj_clear_state(ctx->iot_start_btn,LV_STATE_DISABLED);lv_obj_add_state(ctx->iot_stop_btn,LV_STATE_DISABLED);
}
static bool iot_recon_send_command(tab_context_t *ctx,const char *cmd){
 if(!ctx)return false;if(!strcmp(cmd,"stop")||!strcmp(cmd,"zig_recon_clear")){app_ni_cancel(ctx,1);if(ctx->iot_start_btn)lv_obj_clear_state(ctx->iot_start_btn,LV_STATE_DISABLED);if(ctx->iot_stop_btn)lv_obj_add_state(ctx->iot_stop_btn,LV_STATE_DISABLED);if(ctx->iot_status_label)lv_label_set_text(ctx->iot_status_label,"Mesh recon stopped - results stay visible");return true;}return false;
}


static void iot_recon_clear_cb(lv_event_t *e)
{
    tab_context_t *ctx = (tab_context_t *)lv_event_get_user_data(e);
    if (!ctx) ctx = get_current_ctx();
    if (!ctx) return;

    ESP_LOGI(TAG, "[IoT] Clear clicked on tab %d", tab_id_for_ctx(ctx));
    lv_obj_clear_state(ctx->iot_start_btn,LV_STATE_DISABLED);
    lv_obj_add_state(ctx->iot_stop_btn,LV_STATE_DISABLED);
    bool clear_sent = iot_recon_send_command(ctx, "zig_recon_clear");
    app_mesh_story_cleared[tab_id_for_ctx(ctx)]=app_mesh_story_run[tab_id_for_ctx(ctx)];
    ctx->iot_pan_count = 0;
    ctx->iot_node_count = 0;
    ctx->iot_edge_count = 0;
    ctx->iot_expanded_pan[0] = '\0';
    ctx->iot_tracked_pan[0] = '\0';
    ctx->iot_tracked_addr[0] = '\0';
    ctx->iot_scroll_expanded_once = false;

    if (ctx->iot_channel_label) lv_label_set_text(ctx->iot_channel_label, "--");
    if (ctx->iot_packets_label) lv_label_set_text(ctx->iot_packets_label, "0");
    if (ctx->iot_networks_label) lv_label_set_text(ctx->iot_networks_label, "0");
    if (ctx->iot_dropped_label) lv_label_set_text(ctx->iot_dropped_label, "0");
    if (ctx->iot_status_label) {
        if (clear_sent) {
            lv_label_set_text(ctx->iot_status_label, "Cleared simulated Mesh results");
            lv_obj_set_style_text_color(ctx->iot_status_label, COLOR_MATERIAL_TEAL, 0);
        } else {
            lv_label_set_text(ctx->iot_status_label, "Cleared local state - JanOS clear not sent");
            lv_obj_set_style_text_color(ctx->iot_status_label, COLOR_MATERIAL_AMBER, 0);
        }
    }
    if (ctx->iot_pan_list) {
        iot_recon_show_empty_locked(ctx);
    }
}

static void app_nettools_nmap_iot_tick(void){
 static char reply[16384];
 for(int tab=0;tab<4;tab++)for(int kind=0;kind<2;kind++){
  if(!app_ni_jobs[tab][kind].id)continue;
  tab_context_t *ctx=app_ni_jobs[tab][kind].ctx;
  if(!ctx||!app_ni_jobs[tab][kind].owner||!lv_obj_is_valid(app_ni_jobs[tab][kind].owner)){if(ctx)app_ni_cancel(ctx,kind);continue;}
  int state=app_ni_poll_js(app_ni_jobs[tab][kind].id,app_ni_jobs[tab][kind].sample,reply,sizeof(reply));
  if(kind&&state>=2){
   app_ni_jobs[tab][kind].sample=state-2;
   int saved=current_tab;current_tab=tab;
   iot_recon_process_response(ctx,"zig_recon_list zig_recon_nodes",reply);
   lv_label_set_text(ctx->iot_status_label,"Monitoring synthetic Mesh - STOP keeps results");
   current_tab=saved;continue;
  }
  if(!state){
   if(!kind&&ctx->nmap_scanning&&ctx->nmap_progress_bar){int progress=app_ni_progress_js(app_ni_jobs[tab][kind].id);lv_bar_set_value(ctx->nmap_progress_bar,progress,LV_ANIM_OFF);lv_label_set_text_fmt(ctx->nmap_progress_label,"%d%%",progress);}
   continue;
  }
  int action=app_ni_jobs[tab][kind].action;app_ni_jobs[tab][kind].id=0;
  lv_obj_t *label=kind?ctx->iot_status_label:ctx->nmap_status_label;
  if(state<0){if(label)lv_label_set_text(label,reply);if(!kind){ctx->nmap_wifi_connected=false;
   if(ctx->nmap_connect_btn)lv_obj_clear_state(ctx->nmap_connect_btn,LV_STATE_DISABLED);
   if(ctx->nmap_list_hosts_btn)lv_obj_add_flag(ctx->nmap_list_hosts_btn,LV_OBJ_FLAG_HIDDEN);
   if(ctx->nmap_progress_label)lv_label_set_text(ctx->nmap_progress_label,reply);
  }}
  else if(kind){
   int saved=current_tab;current_tab=tab;
   iot_recon_process_response(ctx,"zig_recon_list zig_recon_nodes",reply);
   lv_label_set_text(ctx->iot_status_label,"Synthetic Mesh scan complete");
   current_tab=saved;
  }else{
   int saved=current_tab;current_tab=tab;
   if(action==1){ctx->nmap_wifi_connected=strstr(reply,"SUCCESS")!=NULL;
    lv_label_set_text(ctx->nmap_status_label,ctx->nmap_wifi_connected?"Connected (synthetic)":"Connection failed");
    if(ctx->nmap_wifi_connected){lv_obj_clear_flag(ctx->nmap_list_hosts_btn,LV_OBJ_FLAG_HIDDEN);lv_obj_add_state(ctx->nmap_connect_btn,LV_STATE_DISABLED);}
   }else if(action==2)app_nmap_hosts_done(reply);
   else if(ctx->nmap_results_container&&lv_obj_is_valid(ctx->nmap_results_container))app_nmap_scan_done(reply);
   current_tab=saved;
  }
  if(kind){ctx->iot_recon_monitoring=false;lv_obj_clear_state(ctx->iot_start_btn,LV_STATE_DISABLED);lv_obj_add_state(ctx->iot_stop_btn,LV_STATE_DISABLED);}
  else ctx->nmap_scanning=false;
 }
}
