from pathlib import Path
import unittest
from test_observer_incremental_ui_contract import function_body
from test_observer_extended_receive import definition, execute_c
ROOT=Path(__file__).resolve().parents[1]
class PopupTransitions(unittest.TestCase):
 def test_close_schedules_one_worker_and_handles_failure(self):
  code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {bool popup_open,observer_running; void *popup_close_task;} tab_context_t;
static tab_context_t ctx;
static int scheduled, destroyed, creation_result;
static tab_context_t *get_current_ctx(void) {return &ctx;}
static void destroy_network_popup_ui(tab_context_t *c) {++destroyed;c->popup_open=false;}
static void popup_close_task(void *arg) {(void)arg;}
static int xTaskCreate(void (*task)(void *),const char *name,int stack,void *arg,int priority,void **handle) {
 assert(task==popup_close_task && arg==&ctx);(void)name;(void)stack;(void)priority;
 ++scheduled;if(creation_result) *handle=&ctx;return creation_result;
}
#define pdPASS 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
"""+definition('close_network_popup','void')+r"""
int main(void) {
 ctx.popup_open=ctx.observer_running=true; creation_result=1;
 close_network_popup();assert(!ctx.popup_open && ctx.popup_close_task && scheduled==1 && destroyed==1);
 close_network_popup();assert(scheduled==1 && destroyed==1);
 ctx.popup_open=true;close_network_popup();assert(scheduled==1);
 ctx.popup_close_task=NULL;creation_result=0;close_network_popup();
 assert(scheduled==2 && !ctx.observer_running && !ctx.popup_close_task);
 return 0;
}
"""
  execute_c(code)
 def test_close_callback_does_not_wait_for_uart(self):
  s=(ROOT/'main/main.c').read_text(encoding='utf-8')
  body=function_body(s,'static void close_network_popup(void)')
  self.assertNotIn('vTaskDelay',body)
  self.assertNotIn('observer_wait_for_command_prompt',body)
  self.assertIn('xTaskCreate',body)
 def test_station_return_selects_clients(self):
  s=(ROOT/'main/main.c').read_text(encoding='utf-8')
  body=function_body(s,'static void stop_and_close_deauth_popup')
  self.assertIn('lv_tabview_set_active(ctx->observer_details_tabs, 3, LV_ANIM_OFF)',body)
 def test_station_handoff_does_not_restart_broad_sniffer(self):
  s=(ROOT/'main/main.c').read_text(encoding='utf-8')
  body=function_body(s,'static void popup_client_row_click_cb')
  self.assertNotIn('close_network_popup()',body)
  self.assertIn('destroy_network_popup_ui(ctx)',body)
if __name__=='__main__': unittest.main()
