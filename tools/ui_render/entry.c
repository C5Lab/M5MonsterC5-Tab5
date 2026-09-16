static uint16_t *host_frame;
static int host_visible_spinner_count(lv_obj_t *parent) {
    int count = 0;
    uint32_t child_count = lv_obj_get_child_count(parent);
    for(uint32_t i=0;i<child_count;i++) {
        lv_obj_t *child=lv_obj_get_child(parent,(int32_t)i);
        if(lv_obj_check_type(child,&lv_spinner_class) &&
           !lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN)) count++;
        count+=host_visible_spinner_count(child);
    }
    return count;
}
static void host_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    int w=lv_display_get_horizontal_resolution(display);
    unsigned stride=lv_draw_buf_width_to_stride(area->x2-area->x1+1,LV_COLOR_FORMAT_RGB565);
    for(int y=area->y1;y<=area->y2;y++)
        memcpy(host_frame+y*w+area->x1,pixels+(y-area->y1)*stride,(area->x2-area->x1+1)*2);
    lv_display_flush_ready(display);
}
static bool host_show(const char *screen) {
    if(!strcmp(screen,"show_scan_scanning")) { show_scan_page(); return true; }
    if(!strcmp(screen,"show_scan_filter_summary")) { show_scan_page(); return true; }
    #include "dispatch.h"
}
int main(int argc, char **argv) {
    if(argc < 4) return 2;
    const char *screen=argv[1]; int rotation=atoi(argv[2]);
    lv_init();
    lv_display_t *display=lv_display_create(720,1280);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_rotation(display,rotation/90);
    void *buffer=malloc(720*1280*2);
    host_frame=calloc(720*1280,2);
    lv_display_set_buffers(display,buffer,NULL,720*1280*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,host_flush);
    grove_detected=true; uart1_detected=true; internal_sd_present=true;
    mbus_detected=true; uart2_initialized=true; mbus_ctx.sd_card_present=true;
    grove_ctx.sd_card_present=true;
    grove_ctx.has_subghz=true;
    enable_red_team=true;
    screen_rotation_setting=rotation/90;
    grove_ctx.networks=calloc(MAX_NETWORKS,sizeof(wifi_network_t));
    grove_ctx.network_count=3; grove_ctx.selected_count=1; grove_ctx.selected_indices[0]=0;
    for(int i=0;i<3;i++) {
        wifi_network_t *n=&grove_ctx.networks[i]; n->index=i+1; n->rssi=-42-i*12; n->channel=i<2?1:11;
        snprintf(n->ssid,sizeof(n->ssid),"LAB-NETWORK-%d",i+1);
        snprintf(n->bssid,sizeof(n->bssid),"02:00:00:00:00:%02d",i+1);
        strcpy(n->security,"WPA2"); strcpy(n->band,"2.4GHz"); strcpy(n->vendor,"Test fixture");
    }
    evil_twin_html_count=2;
    strcpy(evil_twin_html_files[0],"lab-portal.html"); strcpy(evil_twin_html_files[1],"demo.html");
    grove_ctx.observer_networks=calloc(MAX_OBSERVER_NETWORKS,sizeof(observer_network_t));
    grove_ctx.observer_network_count=1;
    observer_network_t *on=&grove_ctx.observer_networks[0];
    strcpy(on->ssid,"LAB-NETWORK-1"); strcpy(on->bssid,"02:00:00:00:00:01");
    strcpy(on->band,"2.4GHz"); strcpy(on->security,"WPA2"); strcpy(on->vendor,"Test fixture");
    strcpy(on->uptime,"2h 14m"); on->scan_index=1; on->channel=1; on->rssi=-42;
    on->client_count=1; strcpy(on->clients[0],"02:00:00:00:01:01");
    bt_device_count=1;
    strcpy(bt_devices[0].name,"LAB Bluetooth device"); strcpy(bt_devices[0].mac,"02:00:00:00:02:01");
    bt_devices[0].rssi=-56;
    if(strstr(screen,"version_mismatch")) {
        grove_ctx.janos_version_mismatch=true; strcpy(grove_ctx.janos_version,"1.0.0");
    }
    grove_ctx.container=NULL;
    lv_obj_set_style_bg_color(lv_screen_active(),ui_bg_color(),0);
    create_tab_containers();
    grove_ctx.container=grove_container; internal_ctx.container=internal_container; mbus_ctx.container=mbus_container;
    if(strstr(screen,"mbus_tiles")) {
        current_tab=TAB_MBUS; lv_obj_add_flag(grove_container,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mbus_container,LV_OBJ_FLAG_HIDDEN);
    }
    bool settings=strstr(screen,"settings") || strstr(screen,"screen_") || strstr(screen,"theme") ||
      strstr(screen,"time_popup") || strstr(screen,"scan_time") || strstr(screen,"ft_baud") ||
      strstr(screen,"ota_page") || strstr(screen,"sd_admin") || strstr(screen,"internal_tiles") ||
      strstr(screen,"adhoc_portal");
    if(settings) {
        current_tab=TAB_INTERNAL;
        lv_obj_add_flag(grove_container,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(internal_container,LV_OBJ_FLAG_HIDDEN);
    }
    create_status_bar(); create_tab_bar(); update_tab_styles();
    if(strstr(screen,"popup") && settings) show_settings_page();
    if(strstr(screen,"wardrive_") && strcmp(screen,"show_wardrive_page") && strcmp(screen,"show_wardrive_files_page")) show_wardrive_page();
    if(strstr(screen,"home_mgmt")) show_wardrive_page();
    fprintf(stderr,"RENDER %s %d\n",screen,rotation);
    if(!host_show(screen)) { fprintf(stderr,"Unknown case\n"); return 3; }
    if(!strcmp(screen,"show_scan_page")) {
        host_scan_rows(&grove_ctx);
        lv_label_set_text_fmt(grove_ctx.scan_status_label,"Found %d networks",grove_ctx.network_count);
    }
    if(!strcmp(screen,"show_scan_scanning")) {
        show_scan_overlay();
        if(grove_ctx.spinner) lv_obj_clear_flag(grove_ctx.spinner,LV_OBJ_FLAG_HIDDEN);
    }
    if(!strcmp(screen,"show_scan_filter_summary")) {
        grove_ctx.scan_filter.sort_key=SCAN_SORT_SIGNAL;
        grove_ctx.scan_filter.reverse=true;
        grove_ctx.scan_filter.security_mask=SCAN_SECURITY_OPEN|SCAN_SECURITY_WPA2|
                                              SCAN_SECURITY_WPA3|SCAN_SECURITY_UNKNOWN;
        grove_ctx.scan_filter.visibility=SCAN_VISIBILITY_HIDDEN;
        strcpy(grove_ctx.scan_filter.ssid_query,"lab");
        scan_filter_update_button(&grove_ctx);
    }
    if(!strcmp(screen,"show_observer_page")) update_observer_table(&grove_ctx);
    lv_obj_update_layout(lv_screen_active());
    if(!strcmp(screen,"show_scan_scanning") && host_visible_spinner_count(lv_screen_active()) != 1) {
        fprintf(stderr,"Expected exactly one visible scan spinner, got %d\n",
                host_visible_spinner_count(lv_screen_active()));
        return 6;
    }
    lv_refr_now(display);
    FILE *out=fopen(argv[3],"wb"); if(!out) return 5;
    int w=lv_display_get_horizontal_resolution(display), h=lv_display_get_vertical_resolution(display);
    fprintf(out,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;i++) {
        unsigned p=host_frame[i],r=(p>>11)&31,g=(p>>5)&63,b=p&31;
        fputc((r<<3)|(r>>2),out); fputc((g<<2)|(g>>4),out); fputc((b<<3)|(b>>2),out);
    }
    fclose(out);
    return 0;
}
