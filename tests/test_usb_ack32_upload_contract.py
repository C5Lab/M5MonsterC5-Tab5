"""Execute main.c's real upload loop with deterministic USB, clock and SD I/O.

The transport fake is the hardware boundary: it records bytes emitted by the
production loop and schedules real encoded ACK32/text bytes. No retry or stream
algorithm is reimplemented here. Regressions this catches include discarded
partial ACKs, extended stale deadlines, mutable/unbounded replay, unsafe recovery
injection, late first headers, absent FIN, and early completion on quiet reads.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/main.c").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"static [^\n]+ " + name + r"\([^;]+?\n\{.*?\n\}", source, re.S)
    if not match:
        raise RuntimeError(f"production function missing: {name}")
    return match.group(0)


harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hs_crack_remote_core.h"
#include "hs_crack_cache.h"
typedef int tab_id_t;
typedef int uart_port_t;
typedef uint32_t TickType_t;
#define TAB_USB 2
#define JANOS_UART_FT_HEADER_BYTES 16
#define JANOS_UART_FT_ACK 0x06
#define JANOS_UART_FT_NAK 0x15
#define JANOS_UART_FT_CAN 0x18
#define pdMS_TO_TICKS(ms) (ms)
#define TAG "host"
typedef struct {
    int tab, port;
    bool usable, failed, active;
    unsigned stage_attempt;
    char stage[16];
    char stage_reason[24];
} hs_crack_remote_worker_t;
static struct { bool cancel_requested; char worker_status[4][96]; } hs_crack_ui;
static unsigned ack_wait_timeout_logs;
static void log_ignore(const char *tag, const char *format, ...)
{
    (void)tag;
    if (strstr(format, "USB ACK wait timeout") != NULL) ack_wait_timeout_logs++;
}
#define ESP_LOGW log_ignore
#define ESP_LOGI log_ignore
static void hs_crack_render_workers(void) {}
static void hs_crack_remote_report(hs_crack_remote_worker_t *worker,
                                   const char *stage, unsigned attempt,
                                   const char *reason)
{ (void)worker; (void)stage; (void)attempt; (void)reason; }
static void hs_crack_set_stage(const char *a, const char *b, int c)
{ (void)a; (void)b; (void)c; }
static const char *tab_transport_name(int tab) { (void)tab; return "USB"; }
static unsigned cdc_ack_timeout_snapshots, cdc_ack_nak_snapshots;
static unsigned cdc_ready_rejected_snapshots;
static void usb_log_cdc_state(const char *s)
{
    if (strcmp(s, "ack_timeout") == 0) cdc_ack_timeout_snapshots++;
    if (strcmp(s, "ack_nak") == 0) cdc_ack_nak_snapshots++;
    if (strcmp(s, "ready_rejected") == 0) cdc_ready_rejected_snapshots++;
}
static int64_t now_us, diag_us, can_us, fin_ack_us, first_header_us;
static int64_t recovery_reserve_us;
static unsigned diag_count, can_count, reset_count, file_reads, sends, fins;
static unsigned zero_byte_failures;
static unsigned data_sends[96], finish_sends;
static bool reading_caps;
static const char *caps_line;
static hs_remote_message_t ready_fixture;
static const char *case_name;
static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", case_name, __LINE__, #c); errors++; } } while (0)
enum scenario { SPLIT, EXHAUST, HARD_READ, PARTIAL_WRITE, STALE, STALE_DEADLINE,
    FIN_REPLAY, QUIET_COMPLETION, COMPLETION_TIMEOUT, PREP_SCAN, PREP_FIRST,
    PREFIX_RESET, NO_PROMPT, BAD_ACK, NAK_REPLAY, CANCEL_DATA, CANCEL_FIN,
    PRE_READY_REJECT,
    OVERSHOOT, FIN_EXHAUST, BLOCK_79, PARTIAL_PAYLOAD, LATE_CAN, RESIDUAL_CAN,
    PREP_MARGIN, DATA_MARGIN, FIN_MARGIN, ZERO_FIRST_HEADER, ZERO_NEXT_HEADER };
static enum scenario scenario;
struct event { int64_t at; uint8_t bytes[512]; size_t length, used; };
static struct event events[200];
static unsigned event_count, event_cursor;
struct transmission { uint8_t bytes[1040]; size_t used; int64_t at; };
static struct transmission transmissions[100];
static int64_t esp_timer_get_time(void) { return now_us; }
static void vTaskDelay(int ticks) { now_us += ticks * 1000; }
static uint32_t le32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void enqueue(int64_t at, const void *data, size_t n)
{
    if (event_count >= 200 || n > 512) abort();
    unsigned pos = event_count++;
    while (pos > event_cursor && events[pos-1].at > at) {
        events[pos] = events[pos-1]; pos--;
    }
    events[pos].at=at; events[pos].length=n; events[pos].used=0;
    memcpy(events[pos].bytes,data,n);
}
static void ack_bytes(uint8_t frame[32], uint32_t index, uint64_t offset, uint8_t status)
{
    memset(frame,0,32); memcpy(frame,"FTA\1",4); put32(frame+4,index); frame[8]=status;
    for(unsigned i=0;i<8;i++) frame[12+i]=(uint8_t)(offset>>(8*i));
    put32(frame+20,hs_crack_cache_crc32_update(0,frame,20));
}
static void ack(int64_t at, uint32_t index, uint64_t offset, uint8_t status)
{ uint8_t bytes[32]; ack_bytes(bytes,index,offset,status); enqueue(at,bytes,32); }
static void terminal(int64_t at, bool success, bool prompt)
{
    const char *line=success ? "[CRACK/1] SYNCED kind=wordlist size=1 crc32=00000000\r\n[CRACK/1] END\r\n"
                             : "[CRACK/1] SYNC_ERROR code=cancelled received=0 size=1\r\n[CRACK/1] END\r\n";
    enqueue(at,line,strlen(line));
    if(prompt) enqueue(at+200000,">",1);
}
static void sent_frame(struct transmission *t)
{
    uint32_t index=le32(t->bytes+4), length=le32(t->bytes+8);
    if(length) {
        if(index>=96) abort();
        unsigned attempt=++data_sends[index];
        uint64_t after=ready_fixture.offset+(uint64_t)index*1024+length;
        if(scenario==EXHAUST || scenario==NO_PROMPT || scenario==HARD_READ || scenario==CANCEL_DATA) return;
        if(scenario==BLOCK_79 && index==79 && attempt==1) return;
        if(scenario==LATE_CAN) {ack(now_us+1000,index,after-length,0x18);return;}
        if(scenario==SPLIT && index==0) {
            if(attempt==1) { uint8_t b[32]; ack_bytes(b,0,after,6);
                enqueue(now_us+1000000,b,12); enqueue(now_us+2100000,b+12,20); }
            return;
        }
        if(scenario==NAK_REPLAY && attempt<3) { ack(now_us+1000,index,after-length,0x15); return; }
        if(scenario==BAD_ACK) { ack(now_us+1000,index+1,after,6); return; }
        if((scenario==STALE || scenario==STALE_DEADLINE) && index==1 && attempt==1) {
            ack(now_us+100000,0,1024,6); ack(now_us+900000,0,1024,6);
            ack(now_us+1800000,0,1024,6);
            if(scenario==STALE) ack(now_us+1900000,1,after,6);
            return;
        }
        ack(now_us+1000,index,after,6);
    } else {
        fins++; finish_sends++;
        CHECK(le32(t->bytes+12)==0);
        CHECK(index==(ready_fixture.size-ready_fixture.offset+1023)/1024);
        if(scenario==CANCEL_FIN || scenario==FIN_EXHAUST) return;
        if(scenario==FIN_REPLAY && finish_sends==1) return;
        if(scenario==FIN_REPLAY) ack(now_us+1000,index-1,ready_fixture.size,6);
        fin_ack_us=now_us+2000;
        ack(fin_ack_us,index,ready_fixture.size,6);
        if(scenario==RESIDUAL_CAN) {
            ack(now_us+3000,index,ready_fixture.size,0x18);
            terminal(now_us+500000,false,true);
            return;
        }
        if(scenario==FIN_REPLAY) ack(now_us+3000,index,ready_fixture.size,6);
        if(scenario!=COMPLETION_TIMEOUT && scenario!=FIN_MARGIN) terminal(now_us+7000000,true,true);
    }
}
static int transport_write_bytes_tab(int tab, int port, const char *data, size_t n)
{
    (void)tab; (void)port;
    if(n==1 && (uint8_t)data[0]==0x18) {
        can_count++; can_us=now_us;
        terminal(now_us+500000,false,scenario!=NO_PROMPT);
        return 1;
    }
    if(n==16 && memcmp(data,"FTB\1",4)==0 &&
       ((scenario==ZERO_FIRST_HEADER && sends==0) ||
        (scenario==ZERO_NEXT_HEADER && sends==1))) {
        /* Hardware accepts no bytes. JanOS still waits for the first header
         * byte under the original PREPARING or next-header deadline. */
        if(zero_byte_failures++==0)
            terminal(scenario==ZERO_FIRST_HEADER ? 10000000 : 7000000,false,true);
        return -1;
    }
    if(scenario==PARTIAL_WRITE) {
        if(sends==0) { sends++; memcpy(transmissions[0].bytes,data,7); transmissions[0].used=7; return 7; }
        return -1;
    }
    if(n==16 && memcmp(data,"FTB\1",4)==0) {
        if(sends>=100) abort();
        struct transmission *t=&transmissions[sends++];
        t->used=16; t->at=now_us; memcpy(t->bytes,data,16);
        if(sends==1) first_header_us=now_us;
        if(le32(t->bytes+8)==0) sent_frame(t);
    } else {
        CHECK(sends>0);
        struct transmission *t=&transmissions[sends-1];
        if(scenario==PARTIAL_PAYLOAD) {
            if(t->used==16) {memcpy(t->bytes+16,data,7);t->used+=7;return 7;}
            return -1;
        }
        CHECK(t->used+n<=sizeof(t->bytes));
        memcpy(t->bytes+t->used,data,n); t->used+=n;
        sent_frame(t);
    }
    return (int)n;
}
static int transport_read_bytes_tab(int tab, int port, void *out, size_t n, TickType_t wait)
{
    (void)tab; (void)port;
    CHECK(wait>0);
    if(n==0) { CHECK(n>0); return -1; }
    if(scenario==FIN_MARGIN && fins && now_us==fin_ack_us) {
        now_us=transmissions[1].at+7000000-recovery_reserve_us;
        hs_crack_ui.cancel_requested=true;
        terminal(transmissions[1].at+7100000,false,true);
        return 0;
    }
    if(scenario==HARD_READ && now_us==0) { now_us=1000; return -1; }
    if(scenario==CANCEL_DATA || (scenario==CANCEL_FIN && fins)) hs_crack_ui.cancel_requested=true;
    int64_t until=now_us+(int64_t)wait*1000;
    if(event_cursor==event_count || events[event_cursor].at>until) { now_us=until; return 0; }
    struct event *e=&events[event_cursor];
    if(now_us<e->at) now_us=e->at;
    size_t got=e->length-e->used; if(got>n) got=n;
    memcpy(out,e->bytes+e->used,got); e->used+=got;
    if(e->used==e->length) event_cursor++;
    if((scenario==OVERSHOOT || scenario==LATE_CAN) && data_sends[0]==1 && sends==1) now_us+=2100000;
    return (int)got;
}
static void compromised_transport_flush(int t, int p) { (void)t;(void)p; }
static bool hs_crack_remote_send_line(int t,int p,const char *line)
{ (void)t;(void)p; reading_caps=strstr(line,"capabilities")!=NULL; return true; }
static bool hs_crack_remote_read_message(int t,int p,hs_remote_message_t *message,uint32_t timeout)
{
    (void)t;(void)p;
    if(reading_caps) { reading_caps=false; return hs_remote_parse_line(caps_line,message); }
    if(scenario==PRE_READY_REJECT && file_reads==0 && sends==0) {
        memset(message,0,sizeof(*message));message->type=HS_REMOTE_REJECTED;
        strcpy(message->code,"busy");enqueue(now_us+1000,">",1);return true;
    }
    if(file_reads==0 && sends==0) { *message=ready_fixture; return true; }
    uint8_t b; char line[320]; size_t used=0;
    int64_t deadline=now_us+(int64_t)timeout*1000;
    while(now_us<deadline) {
        if(transport_read_bytes_tab(2,0,&b,1,10)!=1) continue;
        if(b=='\n') { line[used]=0; if(hs_remote_parse_line(line,message)) return true; used=0; }
        else if(b!='\r' && used<sizeof(line)-1) line[used++]=(char)b;
    }
    return false;
}
static bool hs_crack_remote_wait_end(int t,int p,uint32_t timeout)
{ (void)t;(void)p;(void)timeout; return true; }
static bool hs_crack_remote_query_diag(hs_crack_remote_worker_t *w)
{ (void)w;diag_count++;diag_us=now_us;return true; }
static bool hs_crack_remote_reset_partial(hs_crack_remote_worker_t *w,const char *k,uint64_t s,uint32_t c)
{
    (void)w;(void)k;(void)s;(void)c;reset_count++;ready_fixture.offset=0;
    ready_fixture.prefix_crc32=0;ready_fixture.prepare_ms=10000;file_reads=0;return true;
}
static void hs_crack_remote_report_current(hs_crack_remote_worker_t *w,
                                           const char *reason)
{ (void)w; (void)reason; }
static void hs_crack_remote_sync_lost(hs_crack_remote_worker_t *w,
                                      const char *reason)
{ (void)reason; w->failed=true; w->usable=false; }
static bool hs_crack_remote_uart_recover(hs_crack_remote_worker_t *w,
                                         bool consume_ready_end,
                                         bool send_can,
                                         const char *reason)
{
    (void)w; (void)consume_ready_end; (void)send_can; (void)reason;
    return true;
}
static bool hs_crack_remote_reset_safe(hs_crack_remote_worker_t *w,
                                       const char *k,uint64_t s,uint32_t c)
{ return hs_crack_remote_reset_partial(w,k,s,c); }
static size_t delayed_fread(void *p,size_t s,size_t n,FILE *f)
{
    file_reads++;
    if(scenario==PREP_MARGIN || (scenario==DATA_MARGIN && file_reads==2)) {
        int64_t receiver_deadline=scenario==PREP_MARGIN ? 11000000 : 7000000;
        now_us=receiver_deadline-recovery_reserve_us;
        terminal(receiver_deadline+100000,false,true);
        return 0;
    }
    if(scenario==PREP_SCAN) now_us+=5001000;
    if(scenario==PREP_FIRST) now_us+=9001000;
    return fread(p,s,n,f);
}
#define fread delayed_fread
'''

# Extract definitions, never assert against source spelling. Optional USB region
# lets this same executable demonstrate RED against the original upload loop.
for name in ("hs_crack_remote_write_all_force", "hs_crack_remote_read_exact_bytes",
             "hs_crack_remote_capabilities", "hs_crack_remote_file_prefix_crc",
             "hs_crack_remote_put_le32"):
    if name == "hs_crack_remote_read_exact_bytes" and name not in source:
        continue
    harness += function(name) + "\n"
usb_region = re.search(r"/\* USB ACK32 upload helpers begin \*/(.*?)/\* USB ACK32 upload helpers end \*/", source, re.S)
if usb_region:
    harness += usb_region.group(1)
harness += function("hs_crack_remote_upload")
harness += r'''
#undef fread
static void reset(enum scenario selected, uint64_t size)
{
    scenario=selected;now_us=diag_us=can_us=fin_ack_us=first_header_us=0;
    diag_count=can_count=reset_count=file_reads=sends=fins=finish_sends=0;
    zero_byte_failures=0;cdc_ack_timeout_snapshots=cdc_ack_nak_snapshots=0;
    ack_wait_timeout_logs=0;
    cdc_ready_rejected_snapshots=0;
    event_count=event_cursor=0; reading_caps=false;
    memset(data_sends,0,sizeof(data_sends));memset(transmissions,0,sizeof(transmissions));
    memset(&hs_crack_ui,0,sizeof(hs_crack_ui));memset(&ready_fixture,0,sizeof(ready_fixture));
    ready_fixture.type=HS_REMOTE_READY;strcpy(ready_fixture.kind,"wordlist");
    ready_fixture.size=size;ready_fixture.block_size=1024;ready_fixture.ack_size=32;
    ready_fixture.timeout_ms=1273;ready_fixture.ack_wait_ms=2000;
    ready_fixture.next_header_ms=7000;ready_fixture.prepare_ms=10000;
    ready_fixture.finish_linger_ms=7000;
}
static bool upload(const char *path, hs_crack_remote_worker_t *worker)
{ return hs_crack_remote_upload(worker,"wordlist",path,ready_fixture.size,0); }
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    FILE *fixture=fopen(argv[1],"wb"); if(!fixture) return 2;
    for(unsigned i=0;i<90*1024;i++) fputc((int)(i&255),fixture);
    fclose(fixture);
    hs_crack_remote_worker_t worker={.tab=TAB_USB,.port=0,.usable=true};
    case_name="partial ACK survives attempt deadline"; reset(SPLIT,1024);
    CHECK(upload(argv[1],&worker)); CHECK(data_sends[0]==2); CHECK(fins==1);
    CHECK(transmissions[1].at==2000000);
    CHECK(memcmp(transmissions[0].bytes,transmissions[1].bytes,1040)==0);
    case_name="complete ACK read overshooting deadline remains consumable";reset(OVERSHOOT,1024);
    CHECK(upload(argv[1],&worker));CHECK(data_sends[0]==2 && fins==1);
    case_name="late complete CAN never permits another FTB";reset(LATE_CAN,1024);
    CHECK(!upload(argv[1],&worker));CHECK(data_sends[0]==1 && can_count==0);
    case_name="exactly three immutable sends and terminal drain before DIAG";reset(EXHAUST,1024);
    CHECK(!upload(argv[1],&worker)); CHECK(data_sends[0]==3); CHECK(can_count==1);
    CHECK(cdc_ack_timeout_snapshots==3);
    CHECK(ack_wait_timeout_logs==3);
    CHECK(transmissions[1].at==2000000 && transmissions[2].at==4000000);
    CHECK(memcmp(transmissions[0].bytes,transmissions[2].bytes,1040)==0);
    CHECK(diag_count==1 && diag_us>=can_us+700000);
    case_name="hard read error suppresses blind replay and CAN/DIAG";reset(HARD_READ,1024);
    worker.usable=true;worker.failed=false;
    CHECK(!upload(argv[1],&worker));CHECK(sends==1 && can_count==0 && diag_count==0);
    CHECK(worker.failed && !worker.usable); CHECK(now_us<=10001000);
    case_name="partial forward write suppresses injection";reset(PARTIAL_WRITE,1024);
    CHECK(!upload(argv[1],&worker));CHECK(sends==1 && can_count==0 && diag_count==0);
    CHECK(now_us<=13000000);
    case_name="partial payload timeout suppresses injection";reset(PARTIAL_PAYLOAD,1024);
    CHECK(!upload(argv[1],&worker));CHECK(sends==1 && can_count==0 && diag_count==0);
    CHECK(transmissions[0].used==23 && now_us<=13000000);
    case_name="zero-byte first header retains PREPARING deadline";reset(ZERO_FIRST_HEADER,1024);
    worker.usable=true;worker.failed=false;
    CHECK(!upload(argv[1],&worker));
    CHECK(zero_byte_failures==1 && sends==0 && fins==0 && can_count==0);
    CHECK(diag_count==1 && diag_us==10200000 && now_us<=13000000);
    CHECK(event_cursor==event_count && worker.usable && !worker.failed);
    case_name="zero-byte next header retains DATA deadline";reset(ZERO_NEXT_HEADER,2048);
    worker.usable=true;worker.failed=false;
    CHECK(!upload(argv[1],&worker));
    CHECK(zero_byte_failures==1 && sends==1 && data_sends[0]==1 && fins==0 && can_count==0);
    CHECK(diag_count==1 && diag_us==7200000 && now_us<=10000000);
    CHECK(event_cursor==event_count && worker.usable && !worker.failed);
    case_name="recorded block 79 lost ACK recovers";reset(BLOCK_79,85*1024);
    ready_fixture.offset=4096;ready_fixture.prepare_ms=11000;
    ready_fixture.prefix_crc32=0xA2912082U; /* CRC32 of bytes 0..255 repeated 16 times. */
    CHECK(upload(argv[1],&worker));CHECK(data_sends[79]==2 && data_sends[80]==1 && fins==1);
    CHECK(cdc_ack_timeout_snapshots==1);
    CHECK(memcmp(transmissions[79].bytes,transmissions[80].bytes,1040)==0);
    case_name="repeated stale ACKs followed by current ACK";reset(STALE,2048);
    CHECK(upload(argv[1],&worker));CHECK(data_sends[1]==1);
    case_name="stale ACKs do not extend absolute deadline";reset(STALE_DEADLINE,2048);
    CHECK(upload(argv[1],&worker));CHECK(data_sends[1]==2);
    CHECK(transmissions[2].at-transmissions[1].at==2000000);
    case_name="FIN replay accepts stale data and discards duplicate FIN ACK";reset(FIN_REPLAY,1024);
    CHECK(upload(argv[1],&worker)); CHECK(fins==2);
    CHECK(memcmp(transmissions[1].bytes,transmissions[2].bytes,16)==0);
    CHECK(now_us>=9000000);
    case_name="residual CAN after FIN ACK revokes binary output before terminal drain";reset(RESIDUAL_CAN,1024);
    CHECK(!upload(argv[1],&worker));CHECK(can_count==0 && sends==2 && fins==1);
    CHECK(diag_count==1 && diag_us>=transmissions[1].at+700000);
    case_name="lost FIN ACKs stop after three immutable FIN sends";reset(FIN_EXHAUST,1024);
    CHECK(!upload(argv[1],&worker));CHECK(fins==3 && can_count==1 && diag_count==1);
    CHECK(memcmp(transmissions[1].bytes,transmissions[3].bytes,16)==0);
    case_name="completion waits through quiet reads";reset(QUIET_COMPLETION,1024);
    CHECK(upload(argv[1],&worker));CHECK(fins==1 && now_us>=7000000);
    case_name="completion consumes full 12000ms deadline";reset(COMPLETION_TIMEOUT,1024);
    CHECK(!upload(argv[1],&worker));CHECK(fins==1 && now_us>=fin_ack_us+12000000);
    CHECK(can_count==0 && diag_count==0);
    case_name="PREPARING margin checked while scanning";reset(PREP_SCAN,9216);
    ready_fixture.offset=8192;ready_fixture.prepare_ms=11000;
    CHECK(!upload(argv[1],&worker));CHECK(sends==0 && can_count==0 && diag_count==0);
    CHECK(file_reads==2);
    case_name="PREPARING margin checked immediately before first write";reset(PREP_FIRST,1024);
    CHECK(!upload(argv[1],&worker));CHECK(sends==0 && can_count==0 && diag_count==0);
    case_name="prefix mismatch drains and resets before top-level retry";reset(PREFIX_RESET,1024);
    ready_fixture.offset=1;ready_fixture.prepare_ms=11000;ready_fixture.prefix_crc32=123;
    CHECK(!upload(argv[1],&worker));CHECK(can_count==1 && reset_count==1);
    CHECK(first_header_us==0 && now_us>=can_us+700000);
    case_name="END without prompt forbids DIAG";reset(NO_PROMPT,1024);
    CHECK(!upload(argv[1],&worker));CHECK(can_count==1 && diag_count==0);
    case_name="pre-READY rejection drains prompt and queries DIAG";
    reset(PRE_READY_REJECT,1024);worker.usable=true;worker.failed=false;
    CHECK(!upload(argv[1],&worker));CHECK(sends==0 && can_count==0);
    CHECK(cdc_ready_rejected_snapshots==1 && diag_count==1);
    CHECK(worker.usable && !worker.failed);
    case_name="future ACK terminates safely";reset(BAD_ACK,1024);
    CHECK(!upload(argv[1],&worker));CHECK(data_sends[0]==1 && can_count==1 && diag_count==1);
    case_name="NAK replays at most three identical frames";reset(NAK_REPLAY,1024);
    CHECK(upload(argv[1],&worker));CHECK(data_sends[0]==3);
    CHECK(cdc_ack_timeout_snapshots==0 && cdc_ack_nak_snapshots==2);
    CHECK(memcmp(transmissions[0].bytes,transmissions[2].bytes,1040)==0);
    case_name="DATA cancellation recovers at header boundary";reset(CANCEL_DATA,1024);
    CHECK(!upload(argv[1],&worker));CHECK(can_count==1 && diag_count==0);
    case_name="FIN cancellation recovers at header boundary";reset(CANCEL_FIN,1024);
    CHECK(!upload(argv[1],&worker));CHECK(fins==1 && can_count==1 && diag_count==0);
    /* A sub-second reserve in any raw state must suppress CAN; exactly one
     * second is allowed. The real upload reaches recovery after SD failure
     * in PREPARING/DATA, or cancellation after the FIN ACK in FIN_LINGER. */
    const enum scenario margin_modes[]={PREP_MARGIN,DATA_MARGIN,FIN_MARGIN};
    const char *margin_states[]={"PREPARING","DATA","FIN"};
    const int64_t reserves[]={1001000,1000000,999000};
    char margin_case[96];
    for(unsigned state=0;state<3;state++) for(unsigned boundary=0;boundary<3;boundary++) {
        snprintf(margin_case,sizeof(margin_case),"%s CAN with %lld us receiver reserve",
                 margin_states[state],(long long)reserves[boundary]);
        case_name=margin_case;reset(margin_modes[state],state==1 ? 2048 : 1024);
        recovery_reserve_us=reserves[boundary];
        if(state==0) {ready_fixture.offset=1;ready_fixture.prepare_ms=11000;}
        CHECK(!upload(argv[1],&worker));
        CHECK(can_count==(recovery_reserve_us>=1000000 ? 1U : 0U));
        CHECK(sends==state);
        if(state==2) CHECK(diag_count==0);
        else {
            CHECK(diag_count==1);
            if(can_count) CHECK(diag_us>=can_us+700000);
            else CHECK(diag_us>=(state==0 ? 11300000 : 7300000));
        }
    }
    case_name="protocol 3 capability rejected";reset(QUIET_COMPLETION,1024);
    caps_line="[CRACK/1] CAPABILITIES protocol=3 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32";
    CHECK(!hs_crack_remote_capabilities(&worker));
    case_name="protocol 4 mandatory token enforced";
    caps_line="[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32 replay=last_block";
    CHECK(!hs_crack_remote_capabilities(&worker));
    caps_line="[CRACK/1] CAPABILITIES protocol=4 sync=ftb1 ack=byte,frame32 replay=last_block finish=fin32";
    CHECK(hs_crack_remote_capabilities(&worker));
    if(errors) { fprintf(stderr,"%d contract assertions failed\n",errors);return 1; }
    puts("usb_ack32_upload_contract: PASS (37 production-loop scenarios)");
    return 0;
}
'''

transport_harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
typedef int TickType_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_STATE 0x103
#define TAG "host"
static void log_ignore(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
#define ESP_LOGD log_ignore
#define ESP_LOGW log_ignore
static bool usb_transport_ready=true,usb_cdc_connected=true,usb_debug_logs=false;
static void *usb_cdc_handle=(void *)1;
static int read_error;
static size_t available=32;
static bool disconnect_during_wait;
static void usb_transport_init(void) {}
static void usb_log_cdc_state(const char *s) {(void)s;}
static const char *esp_err_to_name(int err) {(void)err;return "error";}
static int usbh_cdc_read_bytes(void *h,uint8_t *out,size_t *n,int ticks)
{
    (void)h;(void)ticks;
    if(disconnect_during_wait){usb_cdc_connected=false;return ESP_ERR_INVALID_STATE;}
    if(read_error!=ESP_OK)return read_error;
    if(available==0){*n=0;return ESP_ERR_TIMEOUT;}
    if(*n>available)*n=available;
    for(size_t i=0;i<*n;i++)out[i]=0;
    return ESP_OK;
}
'''
transport_harness += function("usb_transport_read")
transport_harness += r'''
int main(void) {
    uint8_t out[32];int errors=0;
#define VERIFY(c) do { if(!(c)) {fprintf(stderr,"FAIL USB transport: %s\n",#c);errors++;} } while(0)
    usb_cdc_connected=false;VERIFY(usb_transport_read(out,32,100)<0);
    usb_cdc_connected=true;read_error=ESP_ERR_INVALID_STATE;VERIFY(usb_transport_read(out,32,100)<0);
    read_error=ESP_ERR_TIMEOUT;VERIFY(usb_transport_read(out,32,100)==0);
    read_error=ESP_FAIL;VERIFY(usb_transport_read(out,32,100)==0);
    read_error=ESP_OK;available=0;disconnect_during_wait=true;VERIFY(usb_transport_read(out,32,100)<0);
    disconnect_during_wait=false;usb_cdc_connected=true;VERIFY(usb_transport_read(out,32,100)==0);
    available=32;VERIFY(usb_transport_read(out,32,100)==32);
    if(errors)return 1;puts("usb_transport_error_contract: PASS");return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="usb_ack32_contract_") as temp:
    transport_binary = str(Path(temp) / "transport_test")
    subprocess.run(["gcc", "-x", "c", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-o", transport_binary, "-"], input=transport_harness, text=True, check=True)
    transport_result = subprocess.run([transport_binary])
    binary = str(Path(temp) / "upload_test")
    subprocess.run(["gcc", "-x", "c", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "main"), "-o", binary, "-",
                    str(ROOT / "main/hs_crack_remote_core.c"),
                    str(ROOT / "main/hs_crack_cache.c")],
                   input=harness, text=True, check=True)
    subprocess.run([binary, str(Path(temp) / "wordlist")], check=True)
    transport_result.check_returncode()
