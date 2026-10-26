/* Native Sub-GHz pages use this offline UART and tab host. The model keeps
   command replies and the virtual SD in browser memory for this session. */
#include "subghz_internal.h"
EM_JS(void, app_subghz_send, (int tab, const char *command), {
  try { emulatorDevice.subghz.command(tab, UTF8ToString(command)); }
  catch (error) { dispatchEvent(new CustomEvent('emulator-model-error', {detail: error.message})); }
});
EM_JS(void, app_subghz_flush, (int tab), { emulatorDevice.subghz.flush(tab); });
EM_JS(int, app_subghz_read, (int tab, void *buffer, size_t size), {
  const line = emulatorDevice.subghz.read(tab);
  if (!line) return 0;
  const reply = line + '\n';
  stringToUTF8(reply, buffer, size);
  return Math.min(lengthBytesUTF8(reply), size - 1);
});

subghz_tab_state_t *subghz_host_alloc_state(void) {
  subghz_tab_state_t *state=calloc(1,sizeof(*state));
  if(state){state->freq_mhz=433.92f;state->cc1101_present=1;}
  return state;
}
void subghz_host_free_state(subghz_tab_state_t **state) { if(state && *state){free(*state);*state=NULL;} }
subghz_tab_state_t *subghz_host_state_for_tab(int tab) {
  if(tab<0 || tab>3 || tab==TAB_INTERNAL)return NULL;
  tab_context_t *ctx=get_ctx_for_tab((tab_id_t)tab);
  if(!ctx)return NULL;
  if(!ctx->subghz)ctx->subghz=subghz_host_alloc_state();
  return ctx->subghz;
}
subghz_tab_state_t *subghz_host_state(void) { return subghz_host_state_for_tab((int)current_tab); }
int subghz_host_current_tab(void) { return (int)current_tab; }
bool subghz_host_tab_is_internal(int tab) { return tab==TAB_INTERNAL; }
lv_obj_t *subghz_host_current_container(void) { return get_current_tab_container(); }
lv_obj_t *subghz_host_container_for_tab(int tab) { return get_container_for_tab((tab_id_t)tab); }
void subghz_host_uart_send(const char *command) { subghz_host_uart_send_for_tab((int)current_tab,command); }
void subghz_host_uart_send_for_tab(int tab,const char *command) { if(tab!=TAB_INTERNAL && command)app_subghz_send(tab,command); }
int subghz_host_uart_read_bytes(int tab,void *buffer,size_t size,uint32_t ticks) {
  (void)ticks;return tab==TAB_INTERNAL || !buffer || size<2 ? 0 : app_subghz_read(tab,buffer,size);
}
void subghz_host_uart_flush_input(int tab) { if(tab!=TAB_INTERNAL)app_subghz_flush(tab); }
void subghz_host_hide_all_pages(void) { tab_context_t *ctx=get_current_ctx();if(ctx)hide_all_pages(ctx); }
void subghz_host_show_main_tiles(void) { show_main_tiles(); }

lv_color_t subghz_host_color_red(void) { return COLOR_MATERIAL_RED; }
lv_color_t subghz_host_color_green(void) { return COLOR_MATERIAL_GREEN; }
lv_color_t subghz_host_color_blue(void) { return COLOR_MATERIAL_BLUE; }
lv_color_t subghz_host_color_cyan(void) { return COLOR_MATERIAL_CYAN; }
lv_color_t subghz_host_color_orange(void) { return COLOR_MATERIAL_ORANGE; }
lv_color_t subghz_host_color_purple(void) { return COLOR_MATERIAL_PURPLE; }
lv_color_t subghz_host_color_pink(void) { return COLOR_MATERIAL_PINK; }
lv_color_t subghz_host_color_amber(void) { return COLOR_MATERIAL_AMBER; }
lv_color_t subghz_host_ui_bg(void) { return ui_bg_color(); }
lv_color_t subghz_host_ui_card(void) { return ui_card_color(); }
lv_color_t subghz_host_ui_card_pressed(void) { return ui_card_pressed_color(); }
lv_color_t subghz_host_ui_panel(void) { return ui_panel_color(); }
lv_color_t subghz_host_ui_text(void) { return ui_text_color(); }
lv_color_t subghz_host_ui_muted(void) { return ui_muted_color(); }
lv_color_t subghz_host_ui_border(void) { return ui_border_color(); }

/* Thin entry points in the production screens keep their parsers as the only
   interpretation of JanOS-style replies. They are enabled for this build. */
void subghz_scanner_emulator_feed(subghz_tab_state_t *,const char *);
void subghz_hunter_emulator_feed(subghz_tab_state_t *,const char *);
void subghz_listen_emulator_feed(subghz_tab_state_t *,const char *);
void subghz_manage_emulator_feed(subghz_tab_state_t *,const char *);
void subghz_weather_emulator_feed(subghz_tab_state_t *,const char *);
void subghz_settings_emulator_feed(subghz_tab_state_t *,const char *);

static bool app_subghz_visible(lv_obj_t *page) { return page && !lv_obj_has_flag(page,LV_OBJ_FLAG_HIDDEN); }
static void app_subghz_task_tick(void) {
  for(int tab=0;tab<4;tab++){
    subghz_tab_state_t *st=subghz_host_state_for_tab(tab);
    if(!st)continue;
    for(int i=0;i<16;i++){
      char line[256];int n=app_subghz_read(tab,line,sizeof(line));if(n<=0)break;
      line[n]=0;while(n>0&&(line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;
      if(app_subghz_visible(st->scanner_page))subghz_scanner_emulator_feed(st,line);
      else if(app_subghz_visible(st->hunter_page))subghz_hunter_emulator_feed(st,line);
      else if(app_subghz_visible(st->listen_page))subghz_listen_emulator_feed(st,line);
      else if(app_subghz_visible(st->manage_page))subghz_manage_emulator_feed(st,line);
      else if(app_subghz_visible(st->weather_page))subghz_weather_emulator_feed(st,line);
      else if(app_subghz_visible(st->settings_page))subghz_settings_emulator_feed(st,line);
      else if(app_subghz_visible(st->jammer_page)){
        subghz_note_radio_line(st,line);
        if(strstr(line,"SubGHz jammer ACTIVE"))st->jammer_active_pending=true;
        if(strstr(line,"CC1101 NOT DETECTED"))st->jammer_radio_fail_pending=true;
      }
    }
  }
}
