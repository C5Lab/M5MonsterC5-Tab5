/* Browser-only show_pass reply. The production pages and parsers stay native.
 * Responses come from each module's volatile SD files, never from a radio. */
static char app_compromised_buf[4][4096];
static int app_compromised_len[4],app_compromised_pos[4];
static bool app_compromised_active[4];

EM_JS(int,app_compromised_text_js,(int tab,const char *kind,char *out,int size),{
 return stringToUTF8(emulatorDevice.compromisedText(tab,UTF8ToString(kind)),out,size);
});

static void app_compromised_clear(int tab){
 if(tab<0||tab>=4)return;
 app_compromised_active[tab]=false;app_compromised_len[tab]=app_compromised_pos[tab]=0;
}
static void app_compromised_stage(int tab,const char *kind){
 if(tab<0||tab>=4)return;
 app_compromised_active[tab]=true;
 app_compromised_pos[tab]=0;
 app_compromised_len[tab]=app_compromised_text_js(tab,kind,app_compromised_buf[tab],sizeof(app_compromised_buf[tab]));
 if(app_compromised_len[tab]<0)app_compromised_len[tab]=0;
}
static int app_compromised_read(int tab,void *data,size_t len){
 if(tab<0||tab>=4||!app_compromised_active[tab])return -1;
 int available=app_compromised_len[tab]-app_compromised_pos[tab];
 if(available<=0)return 0;
 int n=(int)len<available?(int)len:available;
 memcpy(data,app_compromised_buf[tab]+app_compromised_pos[tab],n);
 app_compromised_pos[tab]+=n;
 return n;
}

/* The native confirmation stores globals. The emulator's existing offline ARP
 * page uses per-tab targets; copy the selected credentials across that boundary. */
static void evil_twin_connect_popup_yes_cb(lv_event_t *event){
 (void)event;
 tab_context_t *ctx=get_current_ctx();
 if(!ctx)return;
 if(evil_twin_connect_popup_overlay){
  lv_obj_del(evil_twin_connect_popup_overlay);
  evil_twin_connect_popup_overlay=NULL;evil_twin_connect_popup_obj=NULL;
 }
 snprintf(ctx->arp_target_ssid,sizeof(ctx->arp_target_ssid),"%s",arp_target_ssid);
 snprintf(ctx->arp_target_password,sizeof(ctx->arp_target_password),"%s",arp_target_password);
 ctx->arp_return_to_evil_twin_passwords=true;
 ctx->arp_auto_mode=true;
 show_arp_poison_page();
}
