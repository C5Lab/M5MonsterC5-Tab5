#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* MODEL */
typedef struct { int unused; } lv_obj_t;
typedef int lv_coord_t;
typedef int lv_event_t;
typedef int tab_id_t;
typedef int uart_port_t;
typedef void *TaskHandle_t;
typedef struct {
    lv_obj_t *deauth_detector_table, *deauth_detector_count_label;
    lv_obj_t *deauth_detector_start_btn, *deauth_detector_stop_btn;
    deauth_entry_t *deauth_entries;
    int deauth_entry_count;
    bool deauth_detector_running;
    TaskHandle_t deauth_detector_task;
    int tab;
} tab_context_t;
static tab_context_t *selected_ctx, *read_ctx;
static int current_tab, reads, alarms, last_read_tab;
static int task_creation_result = 1;
static int stop_after_delay, delay_calls;
static size_t wire_offset, read_chunk = 511;
static const char *wire;
static char command[64], texts[64][256];
static int text_count;
static int renders;
static int64_t test_clock_us;
int64_t esp_timer_get_time(void) { return test_clock_us; }
static lv_obj_t object;
static int lv_font_montserrat_12, lv_font_montserrat_14;
#define TAG "test"
static void test_log(const char *format, ...) { (void)format; }
#define ESP_LOGI(tag,...) ((void)(tag), test_log(__VA_ARGS__))
#define ESP_LOGE(tag,...) ((void)(tag), test_log(__VA_ARGS__))
#define pdPASS 1
#define TAB_MBUS 2
#define UART2_NUM 2
#define UART_NUM 1
#define uart2_initialized true
#define pdMS_TO_TICKS(x) (x)
#define LV_SIZE_CONTENT 0
#define LV_FLEX_FLOW_ROW 1
#define LV_FLEX_FLOW_COLUMN 2
#define LV_FLEX_ALIGN_SPACE_BETWEEN 1
#define LV_FLEX_ALIGN_CENTER 2
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_LABEL_LONG_DOT 1
#define LV_LABEL_LONG_WRAP 2
#define LV_STATE_DISABLED 1
#define LV_ANIM_OFF 0
#define COLOR_MATERIAL_AMBER 1
#define COLOR_MATERIAL_GREEN 2
#define COLOR_MATERIAL_RED 3
#define ALERT_TONE_ALARM 1
#define lv_pct(x) (x)
#define lv_color_hex(x) (x)
#define lv_obj_get_scroll_y(x) (0)
#define lv_obj_clean(x) ((void)(x), text_count = 0, ++renders)
#define lv_obj_create(x) ((void)(x), &object)
#define lv_label_create(x) ((void)(x), &object)
#define lv_obj_set_size(...) ((void)0)
#define lv_obj_set_style_bg_color(...) ((void)0)
#define lv_obj_set_style_border_width(...) ((void)0)
#define lv_obj_set_style_radius(...) ((void)0)
#define lv_obj_set_style_pad_all(...) ((void)0)
#define lv_obj_set_style_pad_row(...) ((void)0)
#define lv_obj_set_flex_flow(...) ((void)0)
#define lv_obj_set_flex_align(...) ((void)0)
#define lv_obj_clear_flag(...) ((void)0)
#define lv_obj_set_style_text_font(o,f,p) ((void)(f))
#define lv_obj_set_style_text_color(...) ((void)0)
#define lv_obj_set_width(...) ((void)0)
#define lv_obj_set_flex_grow(...) ((void)0)
#define lv_label_set_long_mode(...) ((void)0)
#define lv_obj_scroll_to_y(o,y,a) ((void)(y))
#define lv_obj_add_state(...) ((void)0)
#define lv_obj_clear_state(...) ((void)0)
#define lv_obj_is_valid(o) ((o) != NULL)
#define bsp_display_lock(...) ((void)0)
#define bsp_display_unlock(...) ((void)0)
static void fake_delay(int ticks) {
    (void)ticks;
    if(stop_after_delay && ++delay_calls==stop_after_delay)
        read_ctx->deauth_detector_running=false;
}
#define vTaskDelay(ticks) fake_delay(ticks)
#define vTaskDelete(...) ((void)0)
#define alert_chime_play(...) (++alarms)
static void lv_label_set_text(lv_obj_t *o, const char *text) {
    (void)o; assert(text_count < 64);
    snprintf(texts[text_count++], 256, "%s", text);
}
static void lv_label_set_text_fmt(lv_obj_t *o, const char *format, ...) {
    char buffer[256]; va_list args; va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    lv_label_set_text(o,buffer);
}
static bool has_text(const char *text) {
    for(int i=0;i<text_count;i++) if(strstr(texts[i],text)) return true;
    return false;
}
static tab_context_t *get_current_ctx(void) { return selected_ctx; }
static tab_id_t tab_id_for_ctx(tab_context_t *ctx) { return ctx->tab; }
static const char *tab_transport_name(tab_id_t tab) { (void)tab; return "test"; }
static void uart_send_command_for_tab(const char *cmd) { snprintf(command,64,"%s",cmd); }
static int xTaskCreate(void (*task)(void *), const char *name, int stack,
                        void *ctx, int priority, TaskHandle_t *handle) {
    (void)task;(void)name;(void)stack;(void)ctx;(void)priority;
    if(task_creation_result==pdPASS) *handle=&object;
    return task_creation_result;
}
static int transport_read_bytes_tab(tab_id_t tab, uart_port_t port, void *out,
                                    size_t capacity, int ticks) {
    (void)port;(void)ticks;last_read_tab=tab;
    test_clock_us += 100000;
    if(!reads++) wire_offset=0;
    size_t length=strlen(wire+wire_offset);
    if(!length) { read_ctx->deauth_detector_running=false;return 0; }
    if(length>capacity) length=capacity;
    if(length>read_chunk) length=read_chunk;
    memcpy(out,wire+wire_offset,length);wire_offset+=length;return (int)length;
}
/* PARSER */
/* TABLE */
/* TASK */
/* CALLBACKS */
static double app_device_ms;
static int app_deauth_line_js(int tab, int seq, char *out, int size) {
    (void)tab;(void)seq;return snprintf(out,(size_t)size,"%s",wire);
}
/* ADAPTER */

static const char *beacon="I (9) JanOS: [CSA] RX: 6 | TO: 11 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -61 | VIA: beacon | COUNT: 3 | MODE: 1 | CLASS: - | SSID: Lab (east) | AP";
int main(void) {
    deauth_entry_t entry={0};
    assert(parse_csa_line(beacon,&entry));
    assert(entry.is_csa && entry.channel==6 && entry.target_channel==11);
    assert(entry.rssi==-61 && entry.csa_count==3 && entry.csa_mode==1 && entry.csa_class==-1);
    assert(!strcmp(entry.bssid,"00:11:22:33:44:55"));
    assert(!strcmp(entry.claimed_tx,"AA:BB:CC:DD:EE:FF"));
    assert(!strcmp(entry.ap_name,"Lab (east) | AP") && !strcmp(entry.csa_via,"beacon"));
    deauth_entry_t entries[DEAUTH_DETECTOR_MAX_ENTRIES]={0};entries[0]=entry;
    tab_context_t first={.tab=0,.deauth_entries=entries,.deauth_entry_count=1,
        .deauth_detector_table=&object,.deauth_detector_count_label=&object};
    update_deauth_table(&first);
    assert(has_text("CSA") && has_text("Lab (east) | AP"));
    assert(has_text("RX CH6") && has_text("TO CH11") && has_text("-61 dBm"));
    assert(has_text("BSSID: 00:11:22:33:44:55") && has_text("Claimed TX: AA:BB:CC:DD:EE:FF"));
    assert(has_text("beacon") && has_text("Count: 3"));
    assert(!has_text("attack") && !has_text("Attacker"));
    const char *action="[CSA] RX: 0 | TO: 255 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -128 | VIA: action | COUNT: 0 | MODE: 255 | CLASS: 131 | SSID: <Unknown>";
    assert(parse_csa_line(action,&entry) && entry.channel==0 && entry.target_channel==255);
    assert(entry.csa_class==131 && !strcmp(entry.ap_name,"<Unknown>"));
    const char *probe="[CSA] RX: 36 | TO: 40 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -45 | VIA: probe | COUNT: 255 | MODE: 0 | CLASS: 0 | SSID: Guest";
    assert(parse_csa_line(probe,&entry) && entry.csa_count==255 && !strcmp(entry.csa_via,"probe"));
    const char *bad[]={"RX: 6 | TO: 11", "[CSA] RX: x | TO: 11", "[CSA] RX: 9999999999999999999999",NULL};
    for(int i=0;bad[i];i++) assert(!parse_csa_line(bad[i],&entry));
    const char *keys[]={"RX: 36","TO: 40","RSSI: -45","COUNT: 255","MODE: 0","CLASS: 0","VIA: probe","BSSID: 00","TX: AA","SSID: Guest"};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
        char invalid[256];snprintf(invalid,sizeof(invalid),"%s",probe);
        char *field=strstr(invalid,keys[i]);assert(field);
        char *value=strchr(field,':')+2;*value=i==9?'\0':'!';
        assert(!parse_csa_line(invalid,&entry));
    }
    for(size_t i=0;i<strlen(probe)-strlen("Guest");i++) {
        char truncated[256];memcpy(truncated,probe,i);truncated[i]='\0';
        assert(!parse_csa_line(truncated,&entry));
    }
    char out_of_range[256];snprintf(out_of_range,sizeof(out_of_range),"%s",probe);
    memcpy(strstr(out_of_range,"255"),"256",3);assert(!parse_csa_line(out_of_range,&entry));
    char long_line[300];memset(long_line,'P',sizeof(long_line));
    size_t prefix=255-strlen(probe);memcpy(long_line+prefix,probe,strlen(probe)+1);
    assert(strlen(long_line)==255 && parse_csa_line(long_line,&entry));
    memmove(long_line+1,long_line,256);long_line[0]='P';
    assert(!parse_csa_line(long_line,&entry));
    assert(!parse_csa_line(NULL,&entry) && !parse_csa_line(probe,NULL));
    assert(parse_deauth_line("[DEAUTH] CH: 6 | AP: Office (00:11:22:33:44:55) | RSSI: -52",&entry));
    assert(!entry.is_csa && entry.channel==6 && entry.rssi==-52 && !strcmp(entry.ap_name,"Office"));
    assert(parse_deauth_line("I (1234) JanOS: [DEAUTH] CH: 6 | AP: Office (00:11:22:33:44:55) | RSSI: -52",&entry));
    assert(entry.channel==6 && entry.rssi==-52 && !strcmp(entry.bssid,"00:11:22:33:44:55"));
    assert(!parse_deauth_line("I (1234) JanOS: Output: [DEAUTH] CH: <ch> | AP: <name> (<bssid>) | RSSI: <rssi>",&entry));
    assert(!parse_deauth_line("[DEAUTH] CH: 6 | AP: Office (<bssid>) | RSSI: -52",&entry));
    assert(!parse_deauth_line("[DEAUTH] CH: 6 | AP: Office (00:11:22:33:44:55) | RSSI: <rssi>",&entry));
    entries[0]=entry;update_deauth_table(&first);
    assert(has_text("CH6") && has_text("Office") && has_text("00:11:22:33:44:55"));
    assert(!has_text("Claimed TX"));
    assert(parse_deauth_line("I (1234) JanOS: [DEAUTH] CH: 6 | AP: Cafe (Guest) (00:11:22:33:44:55) | RSSI: -52",&entry));
    assert(!strcmp(entry.ap_name,"Cafe (Guest)") && !strcmp(entry.bssid,"00:11:22:33:44:55"));
    deauth_entry_t other_entries[DEAUTH_DETECTOR_MAX_ENTRIES]={0};
    tab_context_t second={.tab=2,.deauth_entries=other_entries,.deauth_detector_table=&object};
    selected_ctx=&second;current_tab=2;read_ctx=&first;first.deauth_detector_running=true;
    wire="[CSA] RX: 36 | TO: 40 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -45 | VIA: probe | COUNT: 255 | MODE: 0 | CLASS: 0 | SSID: Guest\n";
    deauth_detector_task(&first);
    assert(last_read_tab==0 && current_tab==2 && first.deauth_entry_count==2 && second.deauth_entry_count==0);
    assert(first.deauth_entries[0].is_csa && alarms==0);
    char overflow[450];memset(overflow,'X',260);snprintf(overflow+260,sizeof(overflow)-260,"%s",wire);
    reads=0;first.deauth_detector_running=true;wire=overflow;deauth_detector_task(&first);
    assert(first.deauth_entry_count==2); /* discard whole overlong line, never its suffix */
    wire="[CSA] RX: 0 | TO: 0 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -45 | VIA: probe | COUNT: 255 | MODE: 0 | CLASS: 0 | SSID: Guest\r\n";
    for(int tab=0;tab<3;tab++) {
        first.tab=tab;reads=0;read_chunk=7;first.deauth_detector_running=true;
        deauth_detector_task(&first);
        assert(last_read_tab==tab && first.deauth_entry_count==3+tab);
        assert(first.deauth_entries[0].target_channel==0 && second.deauth_entry_count==0);
    }
    wire="[DEAUTH] CH: 6 | AP: Office (00:11:22:33:44:55) | RSSI: -52\n";
    reads=0;first.deauth_detector_running=true;deauth_detector_task(&first);
    assert(!first.deauth_entries[0].is_csa && alarms==1);
    int entries_before_instruction=first.deauth_entry_count;
    wire="I (1234) JanOS: Output: [DEAUTH] CH: <ch> | AP: <name> (<bssid>) | RSSI: <rssi>\n";
    reads=0;first.deauth_detector_running=true;deauth_detector_task(&first);
    assert(first.deauth_entry_count==entries_before_instruction && alarms==1);
    char flood[2000]={0};
    for(int i=0;i<8;i++) strcat(flood,"[CSA] RX: 6 | TO: 11 | BSSID: 00:11:22:33:44:55 | TX: AA:BB:CC:DD:EE:FF | RSSI: -61 | VIA: beacon | COUNT: 3 | MODE: 1 | CLASS: - | SSID: Lab\n");
    wire=flood;read_chunk=511;reads=0;renders=0;first.deauth_detector_running=true;
    deauth_detector_task(&first);
    assert(first.deauth_entry_count==14 && renders<=2);
    reads=0;read_chunk=200;renders=0;stop_after_delay=2;delay_calls=0;
    first.deauth_detector_running=true;deauth_detector_task(&first);stop_after_delay=0;
    assert(first.deauth_entry_count==16 && renders==2); /* flush pending rows when stopped during delay */
    selected_ctx=&first;current_tab=first.tab;deauth_detector_start_cb(NULL);assert(!strcmp(command,"deauth_detector"));
    deauth_detector_stop_cb(NULL);assert(!strcmp(command,"stop"));
    first.deauth_detector_task=NULL;task_creation_result=0;command[0]='\0';
    deauth_detector_start_cb(NULL);
    assert(!first.deauth_detector_running && first.deauth_detector_task==NULL);
    assert(!strcmp(command,"stop")); /* cancel radio start if monitor cannot be created */
    wire=probe;first.deauth_detector_running=true;second.deauth_detector_running=true;
    first.tab=0;current_tab=2;app_device_ms=2000;
    app_deauth_start(&first);app_deauth_start(&second);app_deauth_tick();
    assert(first.deauth_entry_count==17 && second.deauth_entry_count==1);
    assert(first.deauth_entries[0].is_csa && second.deauth_entries[0].is_csa);
    first.deauth_detector_running=false;second.deauth_detector_running=false;
    app_device_ms+=2000;app_deauth_tick();
    assert(!app_deauth[0].active && !app_deauth[2].active);
    puts("CSA parser/UI/transport/legacy assertions passed");return 0;
}
