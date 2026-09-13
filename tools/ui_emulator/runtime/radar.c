/* Native radar geometry, per-tab state and offline signal samples. */
typedef struct {tab_context_t *ctx;lv_obj_t *page,*panel,*label;int job,story_job,rssi,angle,channel;double sweep_ms;float distance;char ssid[33],bssid[18];} app_radar_t;
static app_radar_t app_radars[4];
static unsigned app_radar_selection_errors[4];
static int app_radar_rejected_count[4];
static char app_radar_rejected_bssids[4][2][18];
EM_JS(int,app_radar_start_js,(int tab,const char *bssid,char *error),{
 try{return emulatorDevice.device.attackStart(emulatorDevice.module(tab),'ap_radar',{bssids:[UTF8ToString(bssid)]});}
 catch(e){stringToUTF8(e.message,error,192);return 0;}
});
EM_JS(int,app_radar_poll_js,(int id,int *rssi,char *error),{
 const j=emulatorDevice.device.job(id);
 if(j?.state!=='running'){stringToUTF8(j?.error||'Radar stopped',error,192);return 0;}
 HEAP32[rssi>>2]=j.result.signal.rssi;return 1;
});
static void app_radar_deleted(lv_event_t *e) {
 app_radar_t *r=lv_event_get_user_data(e);
 if(r->job)app_model_cancel(r->job);
 r->job=0;r->page=r->panel=r->label=NULL;
 if(r->ctx){r->ctx->ap_radar_page=NULL;r->ctx->ap_radar_running=false;}
}
static void app_radar_close(void) {
 tab_context_t *ctx=get_current_ctx();if(!ctx)return;
 app_radar_t *r=&app_radars[tab_id_for_ctx(ctx)];
 if(r->job)app_rem_stop(r->job);r->job=0;ctx->ap_radar_running=false;
 if(r->page)lv_obj_del(r->page);
 if(ctx->scan_page){lv_obj_clear_flag(ctx->scan_page,LV_OBJ_FLAG_HIDDEN);ctx->current_visible_page=ctx->scan_page;}
}
static void ap_radar_back_btn_event_cb(lv_event_t *e){(void)e;app_radar_close();}
static void ap_radar_stop_btn_event_cb(lv_event_t *e){(void)e;app_radar_close();}
static void app_radar_tick(double ms) {
 for(int tab=0;tab<4;tab++){
  app_radar_t *r=&app_radars[tab];if(!r->job||!r->page)continue;
  char error[192];
  if(!app_radar_poll_js(r->job,&r->rssi,error)){
   r->job=0;r->ctx->ap_radar_running=false;
   lv_label_set_text_fmt(r->label,"No signal - %s",error);lv_obj_invalidate(r->panel);continue;
  }
  r->distance=ap_radar_rssi_to_dist_frac(r->rssi);
  r->sweep_ms+=ms;if(r->sweep_ms>=50){r->angle=(r->angle+12*(int)(r->sweep_ms/50))%360;r->sweep_ms=fmod(r->sweep_ms,50);}
  lv_label_set_text_fmt(r->label,"%d dBm (SIM)",r->rssi);
  lv_obj_set_style_text_color(r->label,ap_radar_rssi_color(r->rssi),0);lv_obj_invalidate(r->panel);
 }
}

static void ap_radar_panel_draw_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DRAW_MAIN) return;

    lv_obj_t *obj = lv_event_get_target_obj(e);
    app_radar_t *r=NULL;
    for(int i=0;i<4;i++)if(app_radars[i].panel==obj){r=&app_radars[i];break;}
    if(!r)return;
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t w = lv_area_get_width(&coords);
    int32_t h = lv_area_get_height(&coords);
    int32_t cx = coords.x1 + w / 2;
    int32_t cy = coords.y1 + h / 2;
    int32_t max_r = (w < h ? w : h) / 2 - 12;
    if (max_r < 20) max_r = 20;

    lv_color_t grid_col = lv_color_hex(0x1E4D3A);
    lv_color_t axis_col = lv_color_hex(0x2E6B52);
    lv_color_t sweep_col = lv_color_hex(0x33FF99);

    for (int ring = 1; ring <= 4; ring++) {
        int32_t r = max_r * ring / 4;
        lv_draw_arc_dsc_t arc_dsc;
        lv_draw_arc_dsc_init(&arc_dsc);
        arc_dsc.base.layer = layer;
        arc_dsc.color = grid_col;
        arc_dsc.width = 1;
        arc_dsc.center.x = cx;
        arc_dsc.center.y = cy;
        arc_dsc.radius = r;
        arc_dsc.start_angle = 0;
        arc_dsc.end_angle = 360;
        arc_dsc.opa = LV_OPA_60;
        lv_draw_arc(layer, &arc_dsc);
    }

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.base.layer = layer;
    line_dsc.color = axis_col;
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_50;

    line_dsc.p1.x = cx - max_r;
    line_dsc.p1.y = cy;
    line_dsc.p2.x = cx + max_r;
    line_dsc.p2.y = cy;
    lv_draw_line(layer, &line_dsc);

    line_dsc.p1.x = cx;
    line_dsc.p1.y = cy - max_r;
    line_dsc.p2.x = cx;
    line_dsc.p2.y = cy + max_r;
    lv_draw_line(layer, &line_dsc);

    int16_t sweep = (int16_t)r->angle;
    int32_t sx = cx + (max_r * lv_trigo_cos(sweep)) / 32767;
    int32_t sy = cy + (max_r * lv_trigo_sin(sweep)) / 32767;

    line_dsc.color = sweep_col;
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_70;
    line_dsc.p1.x = cx;
    line_dsc.p1.y = cy;
    line_dsc.p2.x = sx;
    line_dsc.p2.y = sy;
    lv_draw_line(layer, &line_dsc);

    if(!r->job)return;
    int32_t blip_r_px = (int32_t)(r->distance * max_r);
    int32_t bx = cx;
    int32_t by = cy - blip_r_px;

    lv_area_t blip_area;
    blip_area.x1 = bx - 8;
    blip_area.y1 = by - 8;
    blip_area.x2 = bx + 7;
    blip_area.y2 = by + 7;

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.base.layer = layer;
    rect_dsc.bg_color = ap_radar_rssi_color(r->rssi);
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.radius = LV_RADIUS_CIRCLE;
    rect_dsc.border_width = 0;
    lv_draw_rect(layer, &rect_dsc, &blip_area);
}

static void app_radar_render(int network_idx)
{
    tab_context_t *ctx = get_current_ctx();
    if (!ctx) return;

    if (network_idx < 0 || network_idx >= ctx->network_count) return;

    app_radar_t *r=&app_radars[tab_id_for_ctx(ctx)];r->ctx=ctx;
    wifi_network_t *net = &ctx->networks[network_idx];

    strncpy(r->ssid, net->ssid, sizeof(r->ssid) - 1);
    r->ssid[sizeof(r->ssid) - 1] = '\0';
    strncpy(r->bssid, net->bssid, sizeof(r->bssid) - 1);
    r->bssid[sizeof(r->bssid) - 1] = '\0';
    r->channel = net->channel;

    r->distance = ap_radar_rssi_to_dist_frac(net->rssi);
    r->rssi = net->rssi;
    r->angle = 0;

    lv_obj_t *container = get_current_tab_container();
    if (!container) {
        ESP_LOGE(TAG, "Container not initialized for AP Radar");
        return;
    }


    hide_all_pages(ctx);

    if (ctx->ap_radar_page) {
        lv_obj_del(ctx->ap_radar_page);
        ctx->ap_radar_page = NULL;
        r->page = NULL;
        r->panel = NULL;
        r->label = NULL;
    }

    if (ctx->scan_page) {
        lv_obj_add_flag(ctx->scan_page, LV_OBJ_FLAG_HIDDEN);
    }

    ctx->ap_radar_page = lv_obj_create(container);
    r->page = ctx->ap_radar_page;
    lv_obj_set_size(r->page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(r->page, ui_bg_color(), 0);
    lv_obj_set_style_border_width(r->page, 0, 0);
    lv_obj_set_style_pad_all(r->page, 10, 0);
    lv_obj_set_flex_flow(r->page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(r->page, 8, 0);

    lv_obj_t *header = lv_obj_create(r->page);
    lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 72, 60);
    style_back_nav_button(back_btn);
    app_bind(back_btn, ap_radar_back_btn_event_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_ap_radar_page.back_btn.clicked.00fa97fe265a"));

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_icon);

    lv_obj_t *title_col = lv_obj_create(header);
    lv_obj_set_size(title_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_col, 0, 0);
    lv_obj_set_style_pad_all(title_col, 0, 0);
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(title_col, 2, 0);
    lv_obj_clear_flag(title_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(title_col);
    lv_label_set_text(title, LV_SYMBOL_GPS "  AP Radar");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, COLOR_MATERIAL_BLUE, 0);

    lv_obj_t *subtitle = lv_label_create(title_col);
    const char *ssid_display = strlen(r->ssid) > 0 ? r->ssid : "(Hidden)";
    lv_label_set_text_fmt(subtitle, "%s  |  %s  |  Ch %d",
                          ssid_display, r->bssid, r->channel);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, ui_muted_color(), 0);

    r->panel = lv_obj_create(r->page);
    lv_obj_set_width(r->panel, lv_pct(100));
    lv_obj_set_flex_grow(r->panel, 1);
    lv_obj_set_style_min_height(r->panel, 160, 0);
    lv_obj_set_style_bg_color(r->panel, lv_color_hex(0x0A1A12), 0);
    lv_obj_set_style_border_color(r->panel, COLOR_MATERIAL_GREEN, 0);
    lv_obj_set_style_border_width(r->panel, 1, 0);
    lv_obj_set_style_border_opa(r->panel, LV_OPA_40, 0);
    lv_obj_set_style_radius(r->panel, 12, 0);
    lv_obj_clear_flag(r->panel, LV_OBJ_FLAG_SCROLLABLE);
    app_bind(r->panel, ap_radar_panel_draw_cb, LV_EVENT_DRAW_MAIN, NULL, app_template_line("ui.show_ap_radar_page.ap_radar_panel.draw_main.0e445efe83b8"));

    r->label = lv_label_create(r->page);
    lv_label_set_text_fmt(r->label, "%d dBm", r->rssi);
    lv_obj_set_style_text_font(r->label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(r->label, ap_radar_rssi_color(r->rssi), 0);
    lv_obj_set_style_text_align(r->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(r->label, lv_pct(100));

    lv_obj_t *stop_btn = lv_btn_create(r->page);
    lv_obj_set_size(stop_btn, lv_pct(100), 56);
    lv_obj_set_style_bg_color(stop_btn, COLOR_MATERIAL_RED, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_darken(COLOR_MATERIAL_RED, LV_OPA_20), LV_STATE_PRESSED);
    lv_obj_set_style_radius(stop_btn, 10, 0);
    app_bind(stop_btn, ap_radar_stop_btn_event_cb, LV_EVENT_CLICKED, NULL, app_template_line("ui.show_ap_radar_page.stop_btn.clicked.df98eff08806"));

    lv_obj_t *stop_lbl = lv_label_create(stop_btn);
    lv_label_set_text(stop_lbl, LV_SYMBOL_STOP "  STOP");
    lv_obj_set_style_text_font(stop_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(stop_lbl, lv_color_white(), 0);
    lv_obj_center(stop_lbl);




    ctx->ap_radar_running = true;



    (lv_obj_add_event_cb)(r->page,app_radar_deleted,LV_EVENT_DELETE,r);
    ctx->current_visible_page = ctx->ap_radar_page;
    ESP_LOGI(TAG, "AP Radar page shown for %s", r->ssid);
}

static void show_ap_radar_page(int index) {
 tab_context_t *ctx=get_current_ctx();if(!ctx||index<0||index>=ctx->network_count)return;
 char error[192];int id=app_radar_start_js(tab_id_for_ctx(ctx),ctx->networks[index].bssid,error);
 if(!id){if(ctx->scan_status_label)lv_label_set_text(ctx->scan_status_label,error);return;}
 app_radar_render(index);
 app_radar_t *r=&app_radars[tab_id_for_ctx(ctx)];
 if(!r->page){app_model_cancel(id);return;}
 r->job=r->story_job=id;r->sweep_ms=0;app_radar_tick(0);
}
