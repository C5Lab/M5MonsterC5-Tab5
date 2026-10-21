"""Execute the production station-popup close handler against navigation spies."""
from pathlib import Path
import unittest

from test_observer_incremental_ui_contract import function_body
from test_observer_extended_receive import execute_c


class StationNavigation(unittest.TestCase):
    def test_stop_and_close_return_to_the_originating_view(self):
        source = (Path(__file__).resolve().parents[1] / "main/main.c").read_text(encoding="utf-8")
        handler = "static void stop_and_close_deauth_popup(bool resume_observer)" + function_body(
            source, "static void stop_and_close_deauth_popup")
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef struct {
 bool observer_station_return_to_network, observer_running;
 int observer_station_return_network_idx, observer_network_count;
 void *observer_timer, *observer_details_tabs;
} tab_context_t;
static tab_context_t context;
static int stopped, broad_started, timer_started, destroyed, reopened, selected_tab;
#define LV_ANIM_OFF 0
static void lv_tabview_set_active(void *tabs,int index,int anim) {assert(tabs); (void)anim; selected_tab=index;}
static tab_context_t *get_current_ctx(void) { return &context; }
static void uart_send_command_for_tab(const char *s) {
 if (!strcmp(s,"stop")) ++stopped;
 else { assert(!strcmp(s,"start_sniffer_noscan")); ++broad_started; }
}
static void destroy_deauth_popup_ui(void) {
 ++destroyed; context.observer_station_return_to_network=false;
}
static void show_network_popup(int index) { reopened=index; context.observer_details_tabs=&context; }
static void vTaskDelay(int ticks) { (void)ticks; }
static void xTimerStart(void *timer,int ticks) { (void)timer; (void)ticks; ++timer_started; }
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
''' + handler + r'''
static void reset(void) {
 memset(&context,0,sizeof(context)); context.observer_running=true;
 context.observer_network_count=3; context.observer_timer=&context;
 stopped=broad_started=timer_started=destroyed=0; reopened=selected_tab=-1;
}
int main(void) {
 reset(); context.observer_station_return_to_network=true;
 context.observer_station_return_network_idx=1;
 stop_and_close_deauth_popup(true);
 assert(stopped==1 && destroyed==1 && reopened==1 && selected_tab==3);
 assert(broad_started==0 && timer_started==0);
 assert(!context.observer_station_return_to_network);
 reset(); stop_and_close_deauth_popup(true);
 assert(stopped==1 && destroyed==1 && reopened==-1);
 assert(broad_started==1 && timer_started==1);
 reset(); context.observer_station_return_to_network=true;
 context.observer_station_return_network_idx=1;
 stop_and_close_deauth_popup(false);
 assert(stopped==1 && destroyed==1 && reopened==-1);
 assert(broad_started==0 && timer_started==0);
 reset(); context.observer_station_return_to_network=true;
 context.observer_station_return_network_idx=99;
 stop_and_close_deauth_popup(true);
 assert(reopened==-1 && broad_started==1 && timer_started==1);
 return 0;
}
'''
        execute_c(code)


if __name__ == "__main__":
    unittest.main()
