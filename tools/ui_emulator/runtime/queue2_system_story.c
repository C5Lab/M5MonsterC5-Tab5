EMSCRIPTEN_KEEPALIVE void emu_queue2_system_state(void){
 EM_ASM({globalThis.emulatorQueue2SystemEvidence=({detectorRun:$0,detectorRunning:!!$1,detectorCount:$2,antiRun:$3,antiRunning:!!$4,antiCount:$5,statusPage:!!$6,otaPage:!!$7,otaMonitor:!!$8,updateId:$9,slotId:$10,sdId:$11,sdStopped:$12,sdPage:!!$13,sdConfirm:!!$14});},
  app_deauth[0].run,grove_ctx.deauth_detector_running,deauth_entry_count,
  app_as[0].run,grove_ctx.antisurv_monitoring,grove_ctx.antisurv_follower_count,
  app_story_visible(app_system_page),app_story_visible(g_ota.page),app_story_visible(g_ota.mon_overlay),
  app_queue2_ota_update,app_queue2_ota_slot,app_queue2_sd_job,app_queue2_sd_stopped,
  app_story_visible(internal_ctx.sd_admin_page),app_story_visible(internal_ctx.sd_admin_leave_overlay));
 EM_ASM({emulatorQueue2SystemEvidence.groveReboot=$0;emulatorQueue2SystemEvidence.mbusReboot=$1;},app_system_jobs[0],app_system_jobs[2]);
 EM_ASM({emulatorQueue2SystemEvidence.detectorEvents=$0;emulatorQueue2SystemEvidence.antiEvents=$1;},app_deauth[0].seq,app_as[0].seq);
}
