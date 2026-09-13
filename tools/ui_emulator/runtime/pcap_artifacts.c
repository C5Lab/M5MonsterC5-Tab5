/* Production artifact workers run between LVGL events. Commit their MEMFS
 * changes atomically to the virtual SD model, including its capacity guard. */
EM_JS(void,app_pcap_fs_begin,(void),{
 const snapshot=()=>{const files=new Map();const walk=dir=>{
  if(!FS.analyzePath(dir).exists)return;
  for(const name of FS.readdir(dir)){if(name==='.'||name==='..')continue;
   const path=dir+'/'+name,mode=FS.stat(path).mode;
   if(FS.isDir(mode))walk(path);else if(FS.isFile(mode))files.set(path,FS.readFile(path));
  }};walk('/sdcard');return files;};
 Module.pcapFsSnapshot=snapshot;Module.pcapFsBefore=snapshot();
});
EM_JS(int,app_pcap_fs_commit,(int tab,char *error,int size),{
 const before=Module.pcapFsBefore,after=Module.pcapFsSnapshot();
 try {
  const writes=[],deletes=[];
  for(const [path,bytes] of after){const old=before.get(path);if(!old||old.length!==bytes.length||bytes.some((v,i)=>v!==old[i]))writes.push({path,bytes});}
  for(const path of before.keys())if(!after.has(path))deletes.push(path);
  emulatorDevice.device.commitFileChanges(emulatorDevice.module(tab),writes,deletes);
  delete Module.pcapFsBefore;return 1;
 }catch(e){
  for(const path of after.keys())if(!before.has(path))FS.unlink(path);
  for(const [path,bytes] of before){FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));FS.writeFile(path,bytes);}
  delete Module.pcapFsBefore;stringToUTF8(e.message,error,size);return 0;
 }
});
static void app_pcap_artifact_run(pcap_viewer_state_t *state) {
 if(!state)return;
 app_pcap_fs_begin();pcap_viewer_artifact_task(state);
 char error[160];
 if(!app_pcap_fs_commit(tab_id_for_ctx(state->ctx),error,sizeof(error))){
  state->artifact_success=false;state->extract_status=PCAP_EXTRACT_IO_ERROR;
  snprintf(state->artifact_message,sizeof(state->artifact_message),"Could not save to SD: %s",error);
 }
}
EMSCRIPTEN_KEEPALIVE int emu_load_pcap_examples(void) {
 if(current_tab!=TAB_GROVE&&current_tab!=TAB_MBUS)return 0;
 if(app_model_sync_files(current_tab)<0)return 0;
 show_pcap_viewer_page();return 1;
}

/* Native delete control, with virtual-SD accounting around the real deletion. */
static void pcap_viewer_object_delete_cb(lv_event_t *e) {
 uintptr_t encoded=(uintptr_t)lv_event_get_user_data(e);
 pcap_viewer_state_t *state=pcap_viewer_get_state(get_current_ctx(),false);
 if(!state||!state->extract_result||!encoded||encoded>state->extract_result->object_count)return;
 app_pcap_fs_begin();pcap_extract_result_t prior=*state->extract_result;
 pcap_extract_delete_object(state->extract_result,(uint32_t)(encoded-1));char error[160];
 if(!app_pcap_fs_commit(tab_id_for_ctx(state->ctx),error,sizeof(error))){
  *state->extract_result=prior;emu_unavailable(error);
 }
 lv_async_call(pcap_viewer_show_objects_async,state);
}
