/* Read-only S01 evidence. Native visibility, including clipping, is authoritative. */
static bool app_story_visible(lv_obj_t *obj) {
 return obj && lv_obj_is_valid(obj) && lv_obj_is_visible(obj);
}
static bool app_story_full_label(lv_obj_t *obj) {
 if(!app_story_visible(obj))return false;
 lv_area_t area;lv_obj_get_coords(obj,&area);
 for(lv_obj_t *parent=lv_obj_get_parent(obj);parent;parent=lv_obj_get_parent(parent)){
  lv_area_t bounds;lv_obj_get_coords(parent,&bounds);
  if(area.x1<bounds.x1||area.y1<bounds.y1||area.x2>bounds.x2||area.y2>bounds.y2)return false;
 }
 return true;
}
EMSCRIPTEN_KEEPALIVE int emu_startup_state(void) {
 int state=0;
 if(current_tab==0&&grove_ctx.current_visible_page==grove_ctx.tiles&&app_story_visible(grove_ctx.tiles))state|=1;
 if(current_tab==2&&mbus_ctx.current_visible_page==mbus_ctx.tiles&&app_story_visible(mbus_ctx.tiles))state|=2;
 if(current_tab==TAB_INTERNAL&&app_story_visible(internal_tiles))state|=4;
 if(current_tab==TAB_INTERNAL&&app_story_visible(app_system_page)){
  state|=8;
  if(app_story_full_label(app_system_labels[0]))state|=128;
  if(app_story_full_label(app_system_labels[2]))state|=256;
 }
 if(board_detection_popup_open&&app_story_visible(board_detect_popup))state|=16;
 for(int i=0;i<1024;i++)if(app_bindings[i].object&&strstr(app_bindings[i].id,"show_version_mismatch_popup")&&app_story_visible(app_bindings[i].object)){state|=32;break;}
 if(app_story_visible(sd_warning_popup_obj)){
  state|=64;
  if(current_tab==TAB_GROVE&&sd_warning_pending_action==show_wardrive_page)state|=512;
 }
 return state;
}
