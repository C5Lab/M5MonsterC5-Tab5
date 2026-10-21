/* Portable behavioral harness. Intentionally not compiled/run during source-only work.
 * Example later: cc -std=c11 -Wall -Wextra -I main tests/wifi_analyzer_model_test.c
 * main/wifi_analyzer_model.c -o wifi_analyzer_model_test
 */
#include "wifi_analyzer_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static wa_reader_t reader;
static wa_snapshot_t working, committed;
#define ID "\"v\":1,\"boot\":\"0123456789abcdef\",\"scan\":1,"
static const char *begin = "[WFA1] {" ID "\"type\":\"begin\",\"limit\":2,\"band\":\"both\",\"channels\":[6,36],\"profile\":\"quick\",\"started_ms\":9223372036854775807}\n";
static const char *ap = "[WFA1] {" ID "\"type\":\"ap\",\"seq\":0,\"bssid\":\"02:00:00:00:00:01\",\"ssid_hex\":\"486f6d65ff00\",\"band\":\"2.4\",\"primary\":6,\"rssi\":-55,\"auth\":\"WPA2_PSK\",\"phy\":[\"11n\"],\"bandwidth\":null,\"secondary\":null,\"center1_mhz\":null,\"center2_mhz\":null}\n";
static const char *end = "[WFA1] {" ID "\"type\":\"end\",\"status\":\"ok\",\"found\":1,\"returned\":1,\"truncated\":false,\"duration_ms\":1500}\n";
static void feed(const char *s) { wa_reader_feed(&reader, s, strlen(s)); }
static void init(void) { wa_reader_init(&reader, &working, &committed); }
static void replace_feed(const char *s, const char *from, const char *to)
{
    char out[1200];
    const char *p = strstr(s, from);
    assert(p);
    size_t n = (size_t)(p - s);
    memcpy(out, s, n);
    snprintf(out + n, sizeof(out) - n, "%s%s", to, p + strlen(from));
    feed(out);
}
static void success(void) { feed(begin); feed(ap); feed(end); }

static void test_commit_and_fragmentation(void)
{
    init();
    for (size_t i = 0; i < strlen(begin); ++i) wa_reader_feed(&reader, begin+i, 1);
    feed("ordinary log\r\n"); feed(ap);
    assert(reader.pending && !committed.valid);
    wa_reader_feed(&reader, end, strlen(end)-1);
    assert(!committed.valid);
    feed("\r\n");
    assert(reader.commit_count == 1 && committed.count == 1);
    assert(committed.started_ms == UINT64_C(9223372036854775807));
    assert(committed.aps[0].ssid_len == 6 && committed.aps[0].width == 0);
    assert(strcmp(committed.aps[0].ssid_display, "Home\xef\xbf\xbd\xef\xbf\xbd") == 0);
}
static void test_stale_failure_empty_and_reset(void)
{
    init(); success();
    replace_feed(begin, "\"scan\":1", "\"scan\":2");
    feed("[WFA1] {\"v\":1,\"boot\":\"0123456789abcdef\",\"scan\":2,\"type\":\"end\",\"status\":\"timeout\",\"found\":null,\"returned\":0,\"truncated\":false,\"duration_ms\":20,\"code\":\"scan_timeout\"}\n");
    assert(reader.terminal_count == 2 && reader.error_count == 0 && reader.stale);
    assert(committed.scan == 1);
    replace_feed(begin, "\"scan\":1", "\"scan\":3");
    feed("[WFA1] {\"v\":1,\"boot\":\"0123456789abcdef\",\"scan\":3,\"type\":\"end\",\"status\":\"ok\",\"found\":0,\"returned\":0,\"truncated\":false,\"duration_ms\":0}\n");
    assert(committed.valid && committed.count == 0 && !reader.stale);
    wa_reader_reset_connection(&reader);
    assert(committed.valid && reader.stale && !reader.caps_ready);
    success(); assert(committed.scan == 1);
    wa_reader_abort(&reader, "disconnected");
    assert(committed.valid && reader.stale);
}
static void invalid_ap(const char *from, const char *to)
{
    init(); feed(begin); replace_feed(ap, from, to); feed(end);
    assert(!committed.valid && reader.error_count > 0);
}
static void test_rejections(void)
{
    invalid_ap("\"seq\":0", "\"seq\":1");
    invalid_ap("\"rssi\":-55", "\"rssi\":true");
    invalid_ap("\"rssi\":-55", "\"rssi\":-55.0");
    invalid_ap("\"rssi\":-55", "\"rssi\":NaN");
    invalid_ap("\"rssi\":-55", "\"rssi\":-55,\"extra\":1e999");
    invalid_ap("\"rssi\":-55", "\"rssi\":-55,\"rssi\":-60");
    invalid_ap("\"rssi\":-55", "\"rssi\":-55,\"r\\u0073si\":-60");
    invalid_ap("\"primary\":6", "\"primary\":11");
    invalid_ap("\"phy\":[\"11n\"]", "\"phy\":[\"11n\",\"11n\"]");
    invalid_ap("\"bandwidth\":null", "\"bandwidth\":\"20\"");
    invalid_ap("WPA2_PSK", "BAD\xc0\xaf");
    invalid_ap("WPA2_PSK", "WPA2\\u0000_PSK");
    invalid_ap("\"phy\":[\"11n\"]", "\"phy\":[\"11n\"],\"extra\":{\"a\":[],\"a\":0}");
    init(); replace_feed(begin, "9223372036854775807", "9223372036854775808");
    assert(!reader.pending && reader.error_count);
    init(); success(); feed(begin); assert(reader.error_count && committed.valid);
    init(); feed(begin); replace_feed(ap, "\"v\":1", "\"v\":2");
    assert(!reader.pending && reader.error_count);
}
static void test_overflow_and_duplicate_bssid(void)
{
    char overflow[1100]; memset(overflow, 'x', sizeof(overflow));
    init(); feed(begin); wa_reader_feed(&reader, overflow, sizeof(overflow));
    feed(begin); assert(!reader.pending && reader.error_count);
    replace_feed(begin, "\"scan\":1", "\"scan\":2"); assert(reader.pending);
    init(); feed(begin); feed(ap); replace_feed(ap, "\"seq\":0", "\"seq\":1");
    assert(!reader.pending && !committed.valid);
}
static void test_controls_and_filters(void)
{
    init(); feed(begin);
    feed("[WFACTL1] {\"v\":1,\"type\":\"error\",\"command\":\"scan\",\"code\":\"busy\",\"reason\":\"wifi_analyzer\"}\n");
    assert(reader.pending && reader.control_count == 1 && reader.error_count == 0);
    assert(!strcmp(reader.control_type,"error") && !strcmp(reader.control_code,"busy"));
    feed(ap); feed(end); assert(committed.valid);
    wa_filter_t f = {.band=WA_BAND_ANY, .min_rssi=-127, .width=-1};
    assert(wa_ap_matches(&committed.aps[0], &f));
    strcpy(f.search, "hOmE"); assert(wa_ap_matches(&committed.aps[0], &f));
    f.band = WA_BAND_5; assert(!wa_ap_matches(&committed.aps[0], &f));
    assert(wa_frequency(14)==2484 && wa_frequency(36)==5180 && wa_frequency(15)==0);
    wa_segment_t segments[2];
    assert(wa_ap_segments(&committed.aps[0],segments)==0);
    wa_ap_t wide = {.width=8080,.center1_mhz=5210,.center2_mhz=5530};
    assert(wa_ap_segments(&wide,segments)==2);
    assert(segments[0].low_mhz==5170 && segments[0].high_mhz==5250);
    assert(segments[1].low_mhz==5490 && segments[1].high_mhz==5570);
    wa_ap_t same=committed.aps[0]; strcpy(same.ssid_display,"Different SSID");
    assert(wa_ap_color_rgb(&committed.aps[0])==wa_ap_color_rgb(&same));
}
static void test_widths_and_scope(void)
{
    const struct { const char *width,*secondary,*center1,*center2; int primary; bool valid; } cases[]={
        {"\"20\"","\"none\"","2437","null",6,true},
        {"\"40\"","\"above\"","2447","null",6,true},
        {"\"80\"","\"above\"","5210","null",36,true},
        {"\"160\"","\"above\"","5250","null",36,true},
        {"\"160\"","\"below\"","5250","null",64,true},
        {"\"160\"","\"below\"","5290","null",64,false},
        {"\"160\"","\"above\"","5570","null",100,true},
        {"\"160\"","\"above\"","5815","null",149,true},
        {"\"80+80\"","\"above\"","5210","5530",36,true},
        {"\"80+80\"","\"above\"","5210","5290",36,false},
        {"\"80\"","\"below\"","5210","null",36,false},
        {"\"160\"","\"above\"","5250","5530",36,false},
        {"\"80\"","\"above\"","2447","null",6,false},
        {"null","\"none\"","null","null",6,false},
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        char line[768], plan[40]; init();
        snprintf(plan,sizeof(plan),"\"channels\":[%d]",cases[i].primary);
        replace_feed(begin,"\"channels\":[6,36]",plan);
        snprintf(line,sizeof(line),"[WFA1] {" ID "\"type\":\"ap\",\"seq\":0,\"bssid\":\"02:00:00:00:00:01\",\"ssid_hex\":\"\",\"band\":\"%s\",\"primary\":%d,\"rssi\":20,\"auth\":\"UNKNOWN\",\"phy\":[],\"bandwidth\":%s,\"secondary\":%s,\"center1_mhz\":%s,\"center2_mhz\":%s}\n",
                 cases[i].primary==6 ? "2.4" : "5",cases[i].primary,cases[i].width,cases[i].secondary,cases[i].center1,cases[i].center2);
        feed(line); feed(end);
        assert(committed.valid==cases[i].valid);
    }
}
static void test_control_caps_and_reboot(void)
{
    init(); success();
    feed("[WFACTL1] {\"v\":1,\"type\":\"caps\",\"protocol\":\"WFA/1\",\"default_records\":64,\"max_records\":128,\"max_line_bytes\":1024,\"bands\":[\"2.4\",\"5\"],\"profiles\":[\"quick\",\"detailed\",\"passive\"],\"width_metadata\":\"sdk_unverified\",\"channel_scope\":\"requested_driver_filtered\"}\n");
    assert(reader.caps_ready && reader.max_records==128 && reader.default_records==64);
    replace_feed(begin,"\"scan\":1","\"scan\":2");
    feed("[WFACTL1] {\"v\":1,\"type\":\"status\",\"state\":\"idle\",\"boot\":\"fedcba9876543210\",\"active_scan\":null,\"last_success_scan\":null,\"last_error\":null,\"psram_bytes\":0,\"capacity\":0,\"driver_scan_id\":0}\n");
    assert(!reader.pending && !reader.caps_ready && reader.stale && committed.scan==1);
    assert(!strcmp(reader.control_type,"status") && !strcmp(reader.control_state,"idle"));
    uint32_t controls=reader.control_count;
    feed("[WFACTL1] {\"v\":1,\"type\":\"status\",\"state\":\"idle\",\"boot\":\"fedcba9876543210\",\"active_scan\":1,\"last_success_scan\":null,\"last_error\":null,\"psram_bytes\":0,\"capacity\":0,\"driver_scan_id\":0}\n");
    assert(reader.control_count==controls);
    feed("[WFACTL2] {\"v\":2,\"type\":\"status\"}\n");
    assert(reader.error_count>0);
}
static void test_capacity_and_terminal_counts(void)
{
    char line[768]; init();
    replace_feed(begin,"\"limit\":2","\"limit\":128");
    for (unsigned i=0;i<128;i++) {
        snprintf(line,sizeof(line),"[WFA1] {" ID "\"type\":\"ap\",\"seq\":%u,\"bssid\":\"02:00:00:00:00:%02x\",\"ssid_hex\":\"41\",\"band\":\"2.4\",\"primary\":6,\"rssi\":-55,\"auth\":\"OPEN\",\"phy\":[],\"bandwidth\":null,\"secondary\":null,\"center1_mhz\":null,\"center2_mhz\":null}\n",i,i);
        feed(line);
    }
    assert(reader.pending && working.count==128 && !committed.valid);
    feed("[WFA1] {" ID "\"type\":\"end\",\"status\":\"ok\",\"found\":300,\"returned\":128,\"truncated\":true,\"duration_ms\":2}\n");
    assert(committed.valid && committed.count==128 && committed.found==300 && committed.truncated);
    init(); feed(begin); feed(ap);
    replace_feed(end,"\"found\":1","\"found\":2");
    assert(!committed.valid && reader.error_count);
    init(); feed(begin); feed(ap);
    replace_feed(end,"\"returned\":1","\"returned\":0");
    assert(!committed.valid && reader.error_count);
}
static void test_line_boundary_and_additive_fields(void)
{
    char line[WA_MAX_LINE_BYTES+2];
    size_t n=strlen(begin)-1;
    init(); memcpy(line,begin,n);
    memset(line+n,' ',WA_MAX_LINE_BYTES-n);
    line[WA_MAX_LINE_BYTES]='\n'; line[WA_MAX_LINE_BYTES+1]=0;
    feed(line); feed(ap); feed(end);
    assert(committed.valid && !reader.error_count);
    init();
    replace_feed(begin,"\"limit\":2","\"extra\":{\"array\":[true,null,1.5,\"\\uD83D\\uDE00\"],\"other\":2},\"limit\":2");
    feed(ap); feed(end); assert(committed.valid && !reader.error_count);
    init(); feed(begin);
    feed("[WFACTL1] {\"v\":1,\"v\":1,\"type\":\"stopped\",\"state\":\"idle\"}\n");
    assert(reader.pending && reader.error_count==1 && reader.control_count==0);
    feed(ap); feed(end); assert(committed.valid);
}
static void test_plot_range(void)
{
    init();
    committed.valid = true;
    committed.count = 2;
    committed.aps[0] = (wa_ap_t){.band=WA_BAND_5,.primary=36,.rssi=-60,.width=80,.center1_mhz=5210};
    committed.aps[1] = (wa_ap_t){.band=WA_BAND_5,.primary=64,.rssi=-90};
    wa_filter_t f = {.min_rssi=-100,.width=-1};
    wa_segment_t r = wa_plot_range(&committed,&f,WA_BAND_5,false);
    assert(r.low_mhz==5150 && r.high_mhz==5350);
    r = wa_plot_range(&committed,&f,WA_BAND_5,true);
    assert(r.low_mhz==5150 && r.high_mhz==5900);
    r = wa_plot_range(&committed,&f,WA_BAND_24,false);
    assert(r.low_mhz==2400 && r.high_mhz==2500);
    committed.aps[1].width=8080;
    committed.aps[1].center1_mhz=5290;
    committed.aps[1].center2_mhz=5775;
    r = wa_plot_range(&committed,&f,WA_BAND_5,false);
    assert(r.low_mhz<=5170 && r.high_mhz>=5815); /* Both disjoint segments fit. */
    f.min_rssi=-70;
    r = wa_plot_range(&committed,&f,WA_BAND_5,false);
    assert(r.low_mhz==5150 && r.high_mhz==5350); /* Hidden weak AP cannot widen it. */
    f.band=WA_BAND_24;
    r = wa_plot_range(&committed,&f,WA_BAND_5,false);
    assert(r.low_mhz==5150 && r.high_mhz==5900); /* No matching APs: full band. */
    f.band=WA_BAND_ANY; f.min_rssi=-100;
    committed.count=1;
    committed.aps[0]=(wa_ap_t){.band=WA_BAND_5,.primary=177,.rssi=-60,.width=20,.center1_mhz=5885};
    r = wa_plot_range(&committed,&f,WA_BAND_5,false);
    assert(r.low_mhz==5700 && r.high_mhz==5900);
    r = wa_plot_range(NULL,&f,WA_BAND_5,false);
    assert(r.low_mhz==5150 && r.high_mhz==5900);
}

static void advice_full_plan(void)
{
    static const uint8_t plan[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,
        132,136,140,144,149,153,157,161,165,169,173,177};
    init();
    committed.valid = true;
    committed.band = WA_BAND_ANY;
    memcpy(committed.channels, plan, sizeof(plan));
    committed.channel_count = sizeof(plan);
}

static void test_channel_advice(void)
{
    wa_channel_advice_t advice;
    wa_propose_channels(NULL, WA_BAND_24, false, NULL, &advice);
    assert(advice.state == WA_ADVICE_NO_SNAPSHOT && !advice.count);
    advice_full_plan();
    wa_propose_channels(&committed, WA_BAND_24, false, NULL, &advice);
    assert(advice.state == WA_ADVICE_OK && advice.count == 3 && !advice.observed);
    assert(advice.choices[0].channel == 1 && advice.choices[1].channel == 6 && advice.choices[2].channel == 11);
    committed.count = 2;
    committed.aps[0] = (wa_ap_t){.band=WA_BAND_24,.primary=1,.width=20,.center1_mhz=2412,.rssi=-40,.bssid="own"};
    committed.aps[1] = (wa_ap_t){.band=WA_BAND_24,.primary=6,.width=20,.center1_mhz=2437,.rssi=-85,.bssid="neighbor"};
    wa_propose_channels(&committed, WA_BAND_24, false, NULL, &advice);
    assert(advice.choices[0].channel == 11 && advice.choices[1].channel == 6 && advice.choices[2].channel == 1);
    assert(advice.choices[2].strongest_rssi == -40 && advice.choices[2].overlapping == 1);
    wa_propose_channels(&committed, WA_BAND_24, false, "own", &advice);
    assert(advice.observed == 1 && advice.choices[0].channel == 1);
    committed.truncated = true;
    wa_propose_channels(&committed, WA_BAND_24, false, NULL, &advice);
    assert(advice.state == WA_ADVICE_TRUNCATED && !advice.count);
    committed.truncated = false; committed.channel_count = 1;
    wa_propose_channels(&committed, WA_BAND_24, false, NULL, &advice);
    assert(advice.state == WA_ADVICE_INCOMPLETE_SCOPE && !advice.count);
    advice_full_plan();
    committed.count = 1;
    committed.aps[0] = (wa_ap_t){.band=WA_BAND_5,.primary=36,.width=80,.center1_mhz=5210,.rssi=-60};
    wa_propose_channels(&committed, WA_BAND_5, false, NULL, &advice);
    assert(advice.count == 4 && advice.observed == 1 && !advice.unknown);
    for (unsigned i = 0; i < 4; ++i) {
        assert(advice.choices[i].score == 33620 && advice.choices[i].overlapping == 1);
        assert(advice.choices[i].channel == 36 + (int)i * 4 && !advice.choices[i].dfs);
    }
    committed.aps[0].width = 0;
    wa_propose_channels(&committed, WA_BAND_5, true, NULL, &advice);
    assert(advice.count == 25 && advice.unknown == 1);
    for (unsigned i = 0; i < advice.count; ++i) {
        int c = advice.choices[i].channel;
        assert(advice.choices[i].score == 33620 && !advice.choices[i].overlapping);
        assert(advice.choices[i].dfs == (c >= 52 && c <= 144));
    }
    committed.aps[0].width = 8080; committed.aps[0].center2_mhz = 5530;
    wa_propose_channels(&committed, WA_BAND_5, true, NULL, &advice);
    for (unsigned i = 0; i < advice.count; ++i) {
        int c = advice.choices[i].channel;
        if (c == 64) assert(advice.choices[i].score == 0);
        if (c == 36 || c == 100) assert(advice.choices[i].score > 0);
    }
    committed.aps[0].width = 160; committed.aps[0].center1_mhz = 5250;
    committed.aps[0].center2_mhz = 0;
    wa_propose_channels(&committed, WA_BAND_5, true, NULL, &advice);
    for (unsigned i = 0; i < advice.count; ++i) {
        int c = advice.choices[i].channel;
        if (c >= 36 && c <= 64) assert(advice.choices[i].score == 33620);
        if (c == 100) assert(advice.choices[i].score == 0);
    }
    committed.count = WA_MAX_APS;
    for (unsigned i = 0; i < WA_MAX_APS; ++i)
        committed.aps[i] = (wa_ap_t){.band=WA_BAND_24,.primary=1,.width=20,.center1_mhz=2412,.rssi=20};
    wa_propose_channels(&committed, WA_BAND_24, false, NULL, &advice);
    assert(advice.choices[2].score == 16796160 && advice.choices[2].overlapping == WA_MAX_APS);
}

int main(void)
{
    wa_segment_t base={5150,5350};
    wa_segment_t zoom=wa_plot_zoom(base,2,0);
    assert(zoom.low_mhz==5200 && zoom.high_mhz==5300);
    zoom=wa_plot_zoom(base,4,5180);
    assert(zoom.low_mhz==5155 && zoom.high_mhz==5205);
    for (int center=5000;center<6000;center+=5) {
        zoom=wa_plot_zoom(base,4,center);
        assert(zoom.low_mhz>=base.low_mhz && zoom.high_mhz<=base.high_mhz);
        assert(zoom.high_mhz-zoom.low_mhz==50);
    }
    zoom=wa_plot_zoom((wa_segment_t){2400,2500},4,2484);
    assert(zoom.low_mhz==2472 && zoom.high_mhz==2497);
    zoom=wa_plot_zoom(base,1,5180);
    assert(zoom.low_mhz==5150 && zoom.high_mhz==5350);
    test_plot_range();
    test_channel_advice();
    test_commit_and_fragmentation(); test_stale_failure_empty_and_reset();
    test_rejections(); test_overflow_and_duplicate_bssid(); test_controls_and_filters();
    test_widths_and_scope(); test_control_caps_and_reboot();
    test_capacity_and_terminal_counts(); test_line_boundary_and_additive_fields();
    puts("wifi_analyzer_model: all checks passed");
    return 0;
}
