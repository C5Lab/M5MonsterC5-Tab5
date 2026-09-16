/* Read-only native evidence for S16/S17. No actions are invoked by these exports. */
static int app_q2_wd_visible(lv_obj_t *o){return o&&lv_obj_is_valid(o)&&lv_obj_is_visible(o);}
EMSCRIPTEN_KEEPALIVE int emu_queue2_wardrive_state(int tab,int field){
 if(tab!=TAB_GROVE&&tab!=TAB_MBUS)return 0;
 tab_context_t *ctx=get_ctx_for_tab(tab);
 switch(field){
 case 0:return app_q2_wd_visible(ctx->wardrive_page);
 case 1:return app_wd_story_run[tab];
 case 2:return app_q2_wd_visible(ctx->wardrive_setup_overlay);
 case 3:return app_wd_story_applied[tab];
 case 4:{unsigned h=2166136261u;const unsigned char *p=(const unsigned char*)&ctx->wardrive_config;
  for(unsigned i=0;i<sizeof(ctx->wardrive_config);i++)h=(h^p[i])*16777619u;return (h^ctx->wardrive_trace_enabled)&0x7fffffff;}
 case 5:return app_q2_wd_visible(ctx->wardrive_gps_debug_overlay);
 case 6:return ctx->wardrive_gps_debug_running;
 case 7:return app_wd_story_gps_samples[tab];
 case 8:return app_q2_wd_visible(ctx->home_mgmt_overlay);
 case 9:return app_wd_lists[tab].home_count;
 case 10:return app_q2_wd_visible(ctx->wardrive_blacklist_overlay);
 case 11:return app_wd_lists[tab].black_count;
 case 12:return app_q2_wd_visible(ctx->wardrive_home_confirm_overlay);
 case 13:return g_wd_autoupload_enabled;
 case 14:return g_wd_autoupload_wigle;
 case 15:return g_wd_autoupload_wdgwars;
 case 16:return app_q2_wd_visible(ctx->handshakes_page);
 case 17:return app_q2_wd_visible(ctx->wardrive_files_page);
 case 18:return app_q2_wd_visible(ctx->compromised_confirm_overlay);
 case 19:return ctx->wardrive_wigle_selected_count;
 case 20:return app_q2_wd_visible(ctx->wardrive_wigle_popup);
 case 21:return ctx->wardrive_upload_provider;
 case 22:return app_q2_wd_visible(ctx->tiles)&&ctx->current_visible_page==ctx->tiles;
 case 23:return ctx->compromised_files_loaded;
 case 24:return app_q2_wd_visible(ctx->compromised_cleanup_overlay);
 case 25:return app_wpasec_jobs[tab];
 case 26:return app_pf_story_copied[tab];
 case 27:return app_pf_story_deleted[tab];
 default:return 0;
 }
}
EMSCRIPTEN_KEEPALIVE const char *emu_queue2_wardrive_text(int tab,int field){
 if(tab!=TAB_GROVE&&tab!=TAB_MBUS)return "";
 tab_context_t *ctx=get_ctx_for_tab(tab);
 switch(field){
 case 0:return ctx->compromised_cleanup_pending&&ctx->compromised_cleanup_pending->path_count?ctx->compromised_cleanup_pending->paths[0]:"";
 case 1:return app_wd_lists[tab].home_count?app_wd_lists[tab].home[0].bssid:"";
 case 2:return app_wd_lists[tab].black_count?app_wd_lists[tab].black[0]:"";
 case 3:return app_wd_error[tab];
 case 4:return app_q2_wd_visible(ctx->wardrive_gps_debug_log_label)?lv_label_get_text(ctx->wardrive_gps_debug_log_label):"";
 case 5:return app_pf_story_copy_path[tab];
 case 6:return app_pf_story_delete_path[tab];
 default:return "";
 }
}
