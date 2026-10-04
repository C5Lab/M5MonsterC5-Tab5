"""Execute the production station-popup close handler against navigation spies."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_observer_incremental_ui_contract import function_body


@unittest.skipUnless(shutil.which("gcc"), "host gcc required (also runs in WSL)")
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
 void *observer_timer;
} tab_context_t;
static tab_context_t context;
static int stopped, broad_started, timer_started, destroyed, reopened;
static tab_context_t *get_current_ctx(void) { return &context; }
static void uart_send_command_for_tab(const char *s) {
 if (!strcmp(s,"stop")) ++stopped;
 else { assert(!strcmp(s,"start_sniffer_noscan")); ++broad_started; }
}
static void destroy_deauth_popup_ui(void) {
 ++destroyed; context.observer_station_return_to_network=false;
}
static void show_network_popup(int index) { reopened=index; }
static void vTaskDelay(int ticks) { (void)ticks; }
static void xTimerStart(void *timer,int ticks) { (void)timer; (void)ticks; ++timer_started; }
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
''' + handler + r'''
static void reset(void) {
 memset(&context,0,sizeof(context)); context.observer_running=true;
 context.observer_network_count=3; context.observer_timer=&context;
 stopped=broad_started=timer_started=destroyed=0; reopened=-1;
}
int main(void) {
 reset(); context.observer_station_return_to_network=true;
 context.observer_station_return_network_idx=1;
 stop_and_close_deauth_popup(true);
 assert(stopped==1 && destroyed==1 && reopened==1);
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
        with tempfile.TemporaryDirectory() as directory:
            cfile, binary = Path(directory) / "navigation.c", Path(directory) / "navigation"
            cfile.write_text(code)
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(cfile), "-o", str(binary)], check=True, capture_output=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
