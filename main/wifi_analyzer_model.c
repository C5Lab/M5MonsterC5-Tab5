#include "wifi_analyzer_model.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JT_OBJECT=1, JT_ARRAY, JT_STRING, JT_NUMBER, JT_TRUE, JT_FALSE, JT_NULL };

static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}

/* Returns the length of one valid UTF-8 scalar, excluding surrogates/overlongs. */
static size_t utf8(const unsigned char *s, size_t n)
{
    if (!n) return 0;
    if (s[0] < 0x80) return 1;
    size_t k = s[0]>=0xc2 && s[0]<=0xdf ? 2 :
               s[0]>=0xe0 && s[0]<=0xef ? 3 : s[0]>=0xf0 && s[0]<=0xf4 ? 4 : 0;
    if (!k || k>n) return 0;
    for (size_t i=1; i<k; i++) if ((s[i]&0xc0)!=0x80) return 0;
    if ((s[0]==0xe0 && s[1]<0xa0) || (s[0]==0xed && s[1]>=0xa0) ||
        (s[0]==0xf0 && s[1]<0x90) || (s[0]==0xf4 && s[1]>=0x90)) return 0;
    return k;
}

static void ws(wa_reader_t *r)
{
    while (r->parse_pos<r->line_len &&
           (r->line[r->parse_pos]==' ' || r->line[r->parse_pos]=='\t' ||
            r->line[r->parse_pos]=='\r' || r->line[r->parse_pos]=='\n')) r->parse_pos++;
}

static bool digit(char c) { return c>='0' && c<='9'; }
static bool parse_value(wa_reader_t *r, unsigned depth)
{
    ws(r);
    if (depth>32 || r->parse_pos>=r->line_len || r->token_count==WA_JSON_TOKENS) return false;
    uint16_t ti=r->token_count++;
    wa_json_token_t *t=&r->tokens[ti];
    t->start=r->parse_pos;
    char c=r->line[r->parse_pos++];
    if (c=='{' || c=='[') {
        t->type=c=='{' ? JT_OBJECT : JT_ARRAY;
        char end=c=='{' ? '}' : ']';
        ws(r);
        if (r->line[r->parse_pos]!=end) {
            for (;;) {
                if (c=='{') {
                    ws(r);
                    if (r->line[r->parse_pos]!='"' || !parse_value(r,depth+1)) return false;
                    ws(r);
                    if (r->line[r->parse_pos++]!=':') return false;
                }
                if (!parse_value(r,depth+1)) return false;
                ws(r);
                if (r->line[r->parse_pos]!=',') break;
                r->parse_pos++;
            }
        }
        if (r->parse_pos>=r->line_len || r->line[r->parse_pos++]!=end) return false;
    } else if (c=='"') {
        t->type=JT_STRING;
        t->start=r->parse_pos;
        bool closed=false;
        while (r->parse_pos<r->line_len) {
            unsigned char ch=(unsigned char)r->line[r->parse_pos++];
            if (ch=='"') { closed=true; break; }
            if (ch<32) return false;
            if (ch=='\\') {
                if (r->parse_pos>=r->line_len) return false;
                ch=(unsigned char)r->line[r->parse_pos++];
                if (ch=='u') {
                    for (int j=0;j<4;j++) {
                        if (r->parse_pos>=r->line_len || hex((unsigned char)r->line[r->parse_pos++])<0) return false;
                    }
                } else if (!strchr("\"\\/bfnrt",ch)) return false;
            }
        }
        if (!closed) return false;
    } else if (c=='-' || digit(c)) {
        t->type=JT_NUMBER;
        size_t p=t->start;
        if (r->line[p]=='-') p++;
        if (!digit(r->line[p])) return false;
        if (r->line[p]=='0') p++;
        else while (digit(r->line[p])) p++;
        if (r->line[p]=='.') {
            p++; if (!digit(r->line[p])) return false;
            while (digit(r->line[p])) p++;
        }
        if (r->line[p]=='e' || r->line[p]=='E') {
            p++; if (r->line[p]=='+' || r->line[p]=='-') p++;
            if (!digit(r->line[p])) return false;
            while (digit(r->line[p])) p++;
        }
        r->parse_pos=(uint16_t)p;
    } else {
        const char *word=c=='t' ? "true" : c=='f' ? "false" : c=='n' ? "null" : NULL;
        if (!word || strlen(word)>r->line_len-t->start ||
            memcmp(r->line+t->start,word,strlen(word))) return false;
        t->type=c=='t' ? JT_TRUE : c=='f' ? JT_FALSE : JT_NULL;
        r->parse_pos=(uint16_t)(t->start+strlen(word));
    }
    t->end=r->parse_pos-(t->type==JT_STRING ? 1 : 0);
    t->next=r->token_count;
    return true;
}

/* Decode strings to UTF-8 with explicit lengths, so escaped NUL cannot alias a
 * model field or a duplicate key. Unknown additive string values may contain it. */
static bool decode(const wa_reader_t *r, int i, char *out, size_t cap, size_t *length)
{
    if (i<0 || r->tokens[i].type!=JT_STRING) return false;
    size_t n=0, p=r->tokens[i].start, end=r->tokens[i].end;
    while (p<end) {
        unsigned char c=(unsigned char)r->line[p++];
        unsigned char bytes[4]; size_t k=1; bytes[0]=c;
        if (c=='\\') {
            c=(unsigned char)r->line[p++];
            if (c=='u') {
                uint32_t u=0;
                for (int j=0;j<4;j++) u=(u<<4)|(unsigned)hex((unsigned char)r->line[p++]);
                if (u>=0xd800 && u<=0xdbff) {
                    if (p+6>end || r->line[p]!='\\' || r->line[p+1]!='u') return false;
                    p+=2; uint32_t low=0;
                    for (int j=0;j<4;j++) low=(low<<4)|(unsigned)hex((unsigned char)r->line[p++]);
                    if (low<0xdc00 || low>0xdfff) return false;
                    u=0x10000+((u-0xd800)<<10)+(low-0xdc00);
                } else if (u>=0xdc00 && u<=0xdfff) return false;
                if (u<0x80) bytes[0]=(unsigned char)u;
                else if (u<0x800) { k=2; bytes[0]=0xc0|(u>>6); bytes[1]=0x80|(u&63); }
                else if (u<0x10000) { k=3; bytes[0]=0xe0|(u>>12); bytes[1]=0x80|((u>>6)&63); bytes[2]=0x80|(u&63); }
                else { k=4; bytes[0]=0xf0|(u>>18); bytes[1]=0x80|((u>>12)&63); bytes[2]=0x80|((u>>6)&63); bytes[3]=0x80|(u&63); }
            } else {
                const char *a="\"\\/bfnrt", *b="\"\\/\b\f\n\r\t";
                const char *m=strchr(a,c); if (!m) return false;
                bytes[0]=(unsigned char)b[m-a];
            }
        }
        if (n+k>=cap) return false;
        memcpy(out+n,bytes,k); n+=k;
    }
    out[n]=0; *length=n; return true;
}

static bool string(const wa_reader_t *r, int i, char *out, size_t cap)
{
    size_t n;
    return decode(r,i,out,cap,&n) && !memchr(out,0,n);
}
static bool eq(const wa_reader_t *r, int i, const char *s)
{
    char value[96];
    return string(r,i,value,sizeof(value)) && !strcmp(value,s);
}
static int field(const wa_reader_t *r, int object, const char *key)
{
    if (object<0 || r->tokens[object].type!=JT_OBJECT) return -1;
    for (int i=object+1; i<r->tokens[object].next; i=r->tokens[i+1].next)
        if (eq(r,i,key)) return i+1;
    return -1;
}
static bool null(const wa_reader_t *r, int i) { return i>=0 && r->tokens[i].type==JT_NULL; }
static bool integer(const wa_reader_t *r, int i, int64_t low, int64_t high, int64_t *out)
{
    if (i<0 || r->tokens[i].type!=JT_NUMBER) return false;
    size_t p=r->tokens[i].start, end=r->tokens[i].end;
    bool neg=r->line[p]=='-'; if (neg) p++;
    uint64_t n=0;
    for (;p<end;p++) {
        if (!digit(r->line[p])) return false;
        unsigned d=(unsigned)(r->line[p]-'0');
        if (n>((uint64_t)INT64_MAX-d)/10) return false;
        n=n*10+d;
    }
    int64_t v=neg ? -(int64_t)n : (int64_t)n;
    if (v<low || v>high) return false;
    *out=v; return true;
}
static bool num(const wa_reader_t *r, const char *key, int64_t low, int64_t high, int64_t *out)
{ return integer(r,field(r,0,key),low,high,out); }

static bool strict_json(wa_reader_t *r, size_t offset)
{
    for (size_t p=offset; p<r->line_len;) {
        size_t k=utf8((const unsigned char *)r->line+p,r->line_len-p);
        if (!k || !r->line[p]) return false;
        p+=k;
    }
    r->token_count=0; r->parse_pos=(uint16_t)offset;
    if (!parse_value(r,0) || r->tokens[0].type!=JT_OBJECT) return false;
    ws(r); if (r->parse_pos!=r->line_len) return false;
    char a[WA_MAX_LINE_BYTES+1], b[WA_MAX_LINE_BYTES+1];
    for (int ti=0;ti<r->token_count;ti++) {
        size_t al,bl;
        if (r->tokens[ti].type==JT_NUMBER) {
            size_t length=r->tokens[ti].end-r->tokens[ti].start;
            memcpy(a,r->line+r->tokens[ti].start,length); a[length]=0;
            if (!isfinite(strtod(a,NULL))) return false;
        }
        if (r->tokens[ti].type==JT_STRING && !decode(r,ti,a,sizeof(a),&al)) return false;
        if (r->tokens[ti].type!=JT_OBJECT) continue;
        for (int i=ti+1;i<r->tokens[ti].next;i=r->tokens[i+1].next) {
            if (!decode(r,i,a,sizeof(a),&al)) return false;
            for (int j=r->tokens[i+1].next;j<r->tokens[ti].next;j=r->tokens[j+1].next) {
                if (!decode(r,j,b,sizeof(b),&bl)) return false;
                if (al==bl && !memcmp(a,b,al)) return false;
            }
        }
    }
    return true;
}

static void fault(wa_reader_t *r, const char *reason, bool invalidate)
{
    r->error_count++; r->stale=true;
    snprintf(r->error,sizeof(r->error),"%s",reason);
    if (invalidate) r->pending=false;
}
static wa_band_t channel_band(int channel)
{
    if (channel>=1 && channel<=14) return WA_BAND_24;
    if ((channel>=36 && channel<=64 && channel%4==0) ||
        (channel>=100 && channel<=144 && channel%4==0) ||
        (channel>=149 && channel<=177 && (channel-149)%4==0)) return WA_BAND_5;
    return WA_BAND_ANY;
}
int wa_frequency(int channel)
{
    wa_band_t band=channel_band(channel);
    return band==WA_BAND_24 ? (channel==14 ? 2484 : 2407+5*channel) :
           band==WA_BAND_5 ? 5000+5*channel : 0;
}
static int band(const wa_reader_t *r,int i)
{ return eq(r,i,"2.4") ? WA_BAND_24 : eq(r,i,"5") ? WA_BAND_5 : eq(r,i,"both") ? WA_BAND_ANY : -1; }
static bool boot_value(const wa_reader_t *r, int i, char out[17])
{
    if (!string(r,i,out,17) || strlen(out)!=16) return false;
    for (int j=0;j<16;j++) if (!digit(out[j]) && !(out[j]>='a' && out[j]<='f')) return false;
    return true;
}

static bool begin_record(wa_reader_t *r, const char *boot, uint32_t scan)
{
    int64_t limit,started,ch;
    int b=band(r,field(r,0,"band"));
    char profile[10];
    int channels=field(r,0,"channels");
    uint8_t plan[WA_MAX_CHANNELS], count=0;
    if ((r->boot[0] && strcmp(r->boot,boot)) || scan<=r->last_scan ||
        !num(r,"limit",1,WA_MAX_APS,&limit) || !num(r,"started_ms",0,INT64_MAX,&started) || b<0 ||
        !string(r,field(r,0,"profile"),profile,sizeof(profile)) ||
        (strcmp(profile,"quick") && strcmp(profile,"detailed") && strcmp(profile,"passive")) ||
        channels<0 || r->tokens[channels].type!=JT_ARRAY) return false;
    for (int i=channels+1;i<r->tokens[channels].next;i=r->tokens[i].next) {
        if (count==WA_MAX_CHANNELS || !integer(r,i,1,177,&ch) || !channel_band((int)ch) ||
            (b!=WA_BAND_ANY && b!=(int)channel_band((int)ch))) return false;
        for (int j=0;j<count;j++) if (plan[j]==ch) return false;
        plan[count++]=(uint8_t)ch;
    }
    if (!count) return false;
    if (r->pending) fault(r,"New begin interrupted incomplete snapshot",true);
    memset(r->working,0,sizeof(*r->working));
    wa_snapshot_t *w=r->working;
    memcpy(w->boot,boot,17); memcpy(r->boot,boot,17);
    w->scan=scan; r->last_scan=scan; w->limit=(uint16_t)limit;
    w->band=(wa_band_t)b; w->started_ms=(uint64_t)started;
    memcpy(w->channels,plan,count); w->channel_count=count; strcpy(w->profile,profile);
    r->pending=true; r->stale=true; r->begin_count++;
    return true;
}

static bool geometry(wa_reader_t *r, wa_ap_t *a)
{
    int width=field(r,0,"bandwidth"),secondary=field(r,0,"secondary");
    int c1=field(r,0,"center1_mhz"),c2=field(r,0,"center2_mhz");
    a->secondary=2;
    if (null(r,width)) return null(r,secondary) && null(r,c1) && null(r,c2);
    a->width=eq(r,width,"20") ? 20 : eq(r,width,"40") ? 40 : eq(r,width,"80") ? 80 :
             eq(r,width,"160") ? 160 : eq(r,width,"80+80") ? 8080 : 0;
    int64_t center;
    if (!a->width || !integer(r,c1,a->band==WA_BAND_24 ? 2400 : 5000,
                              a->band==WA_BAND_24 ? 2500 : 5900,&center)) return false;
    a->center1_mhz=(int)center;
    int frequency=wa_frequency(a->primary);
    a->secondary=eq(r,secondary,"none") ? 0 : eq(r,secondary,"above") ? 1 : eq(r,secondary,"below") ? -1 : 2;
    if (a->width==20) return a->center1_mhz==frequency && a->secondary==0 && null(r,c2);
    if (a->secondary!=1 && a->secondary!=-1) return false;
    int other=a->primary+a->secondary*4;
    if (channel_band(other)!=a->band || other==14 || a->primary==14) return false;
    if (a->width==40) return a->center1_mhz==frequency+a->secondary*10 && null(r,c2);
    if (a->band!=WA_BAND_5) return false;
    /* A primary-80 center is not the center of the complete 160 MHz block. */
    if (a->width==160 && a->center1_mhz!=5250 &&
        a->center1_mhz!=5570 && a->center1_mhz!=5815) return false;
    int delta=frequency-a->center1_mhz;
    int absolute=delta<0 ? -delta : delta;
    if (absolute!=10 && absolute!=30 && !(a->width==160 && (absolute==50 || absolute==70))) return false;
    int slot=(delta+(a->width==160 ? 80 : 40)-10)/20;
    if (a->secondary!=(slot%2==0 ? 1 : -1)) return false;
    if (a->width!=8080) return null(r,c2);
    if (!integer(r,c2,5000,5900,&center)) return false;
    a->center2_mhz=(int)center; delta=a->center2_mhz-a->center1_mhz;
    return delta>80 || delta<-80;
}

static void display_ssid(wa_ap_t *a)
{
    size_t p=0,n=0;
    while (p<a->ssid_len) {
        size_t k=utf8(a->ssid+p,a->ssid_len-p);
        /* Replace invalid bytes and control characters; preserve valid UTF-8. */
        bool control=a->ssid[p]<32 || a->ssid[p]==127 ||
                     (k==2 && a->ssid[p]==0xc2 && a->ssid[p+1]>=0x80 && a->ssid[p+1]<=0x9f);
        if (!k || control) {
            memcpy(a->ssid_display+n,"\xef\xbf\xbd",3); n+=3; p+=k ? k : 1;
        } else { memcpy(a->ssid_display+n,a->ssid+p,k); n+=k; p+=k; }
    }
    a->ssid_display[n]=0;
}
static bool ap_record(wa_reader_t *r)
{
    wa_snapshot_t *w=r->working;
    int64_t seq,primary,rssi;
    wa_ap_t a={0}; char ssid_hex[65];
    int b=band(r,field(r,0,"band"));
    if (!num(r,"seq",0,127,&seq) || seq!=w->count || seq>=w->limit ||
        !num(r,"primary",1,177,&primary) || b<=0 || (int)channel_band((int)primary)!=b ||
        !num(r,"rssi",-127,20,&rssi) || !string(r,field(r,0,"bssid"),a.bssid,sizeof(a.bssid)) || strlen(a.bssid)!=17 ||
        !string(r,field(r,0,"ssid_hex"),ssid_hex,sizeof(ssid_hex)) || strlen(ssid_hex)%2 ||
        !string(r,field(r,0,"auth"),a.auth,sizeof(a.auth)) || !a.auth[0]) return false;
    bool in_scope=false;
    for (int i=0;i<w->channel_count;i++) if (w->channels[i]==primary) in_scope=true;
    if (!in_scope) return false;
    for (int i=0;i<17;i++) {
        if (i%3==2) { if (a.bssid[i]!=':') return false; }
        else { int h=hex((unsigned char)a.bssid[i]); if (h<0) return false; a.bssid[i]="0123456789abcdef"[h]; }
    }
    for (int i=0;i<w->count;i++) if (!strcmp(a.bssid,w->aps[i].bssid)) return false;
    a.ssid_len=(uint8_t)(strlen(ssid_hex)/2);
    for (int i=0;i<a.ssid_len;i++) {
        int hi=hex((unsigned char)ssid_hex[i*2]),lo=hex((unsigned char)ssid_hex[i*2+1]);
        if (hi<0 || lo<0) return false;
        a.ssid[i]=(uint8_t)(hi*16+lo);
    }
    a.primary=(int)primary; a.rssi=(int)rssi; a.band=(wa_band_t)b;
    int phy=field(r,0,"phy");
    if (phy<0 || r->tokens[phy].type!=JT_ARRAY) return false;
    static const char *names[]={"11a","11b","11g","11n","11ac","11ax","lr"};
    for (int i=phy+1;i<r->tokens[phy].next;i=r->tokens[i].next) {
        int flag=0;
        for (int j=0;j<7;j++) if (eq(r,i,names[j])) flag=1<<j;
        if (!flag || (a.phy_bits&flag)) return false;
        a.phy_bits|=(uint8_t)flag;
    }
    if (!geometry(r,&a)) return false;
    display_ssid(&a); w->aps[w->count++]=a;
    return true;
}

static bool end_record(wa_reader_t *r)
{
    wa_snapshot_t *w=r->working;
    int64_t duration,returned,found;
    char status[12],code[65];
    int trunc=field(r,0,"truncated");
    if (!num(r,"duration_ms",0,INT64_MAX,&duration) || !num(r,"returned",0,w->limit,&returned) || returned!=w->count ||
        trunc<0 || (r->tokens[trunc].type!=JT_TRUE && r->tokens[trunc].type!=JT_FALSE) ||
        !string(r,field(r,0,"status"),status,sizeof(status))) return false;
    bool truncated=r->tokens[trunc].type==JT_TRUE;
    bool ok=!strcmp(status,"ok");
    if (ok) {
        if (!num(r,"found",0,65535,&found) || found<returned || truncated!=(found>returned) ||
            returned!=(found<w->limit ? found : w->limit)) return false;
    } else {
        if ((strcmp(status,"error") && strcmp(status,"cancelled") && strcmp(status,"timeout")) ||
            !null(r,field(r,0,"found")) || truncated ||
            !string(r,field(r,0,"code"),code,sizeof(code)) || !code[0]) return false;
        snprintf(r->error,sizeof(r->error),"%s: %s",status,code);
    }
    r->pending=false; r->terminal_count++; strcpy(r->last_terminal_status,status);
    if (ok) {
        w->found=(uint16_t)found; w->duration_ms=(uint64_t)duration; w->truncated=truncated; w->valid=true;
        memcpy(r->committed,w,sizeof(*w)); r->commit_count++; r->stale=false; r->error[0]=0;
    } else r->stale=true;
    return true;
}

static bool one_of(const wa_reader_t *r, int i, const char *const *values, size_t count)
{
    for (size_t j=0;j<count;j++) if (eq(r,i,values[j])) return true;
    return false;
}
static bool required_string(wa_reader_t *r,const char *key,char *out,size_t cap)
{ return string(r,field(r,0,key),out,cap) && out[0]; }
static bool enum_array(wa_reader_t *r,const char *key,const char *const *values,size_t count)
{
    int t=field(r,0,key); unsigned mask=0;
    if (t<0 || r->tokens[t].type!=JT_ARRAY) return false;
    for (int i=t+1;i<r->tokens[t].next;i=r->tokens[i].next) {
        unsigned bit=0;
        for (size_t j=0;j<count;j++) if (eq(r,i,values[j])) bit=1u<<j;
        if (!bit || (mask&bit)) return false;
        mask|=bit;
    }
    return mask==((1u<<count)-1);
}
static bool control_record(wa_reader_t *r)
{
    int64_t version,n,maximum,defaults;
    if (!num(r,"v",1,1,&version)) return false;
    int type=field(r,0,"type");
    char control_type[16],control_code[40]={0},control_reason[96]={0};
    if (!string(r,type,control_type,sizeof(control_type))) return false;
    if (eq(r,type,"caps")) {
        static const char *const bands[]={"2.4","5"}, *const profiles[]={"quick","detailed","passive"};
        if (!eq(r,field(r,0,"protocol"),"WFA/1") || !num(r,"max_records",1,128,&maximum) ||
            !num(r,"default_records",1,maximum,&defaults) || !num(r,"max_line_bytes",1024,1024,&n) ||
            !enum_array(r,"bands",bands,2) || !enum_array(r,"profiles",profiles,3) ||
            !eq(r,field(r,0,"width_metadata"),"sdk_unverified") ||
            !eq(r,field(r,0,"channel_scope"),"requested_driver_filtered")) return false;
        r->max_records=(uint16_t)maximum; r->default_records=(uint16_t)defaults; r->caps_ready=true;
    } else if (eq(r,type,"status")) {
        static const char *const states[]={"idle","preparing","scanning","collecting","publishing","cancelling","quarantined"};
        char boot[17],state[24],last_error[96];
        if (!boot_value(r,field(r,0,"boot"),boot) ||
            !one_of(r,field(r,0,"state"),states,7) || !string(r,field(r,0,"state"),state,sizeof(state)) ||
            !num(r,"psram_bytes",0,INT64_MAX,&n) || !num(r,"capacity",0,128,&n) ||
            !num(r,"driver_scan_id",0,255,&n)) return false;
        int active=field(r,0,"active_scan"),last=field(r,0,"last_success_scan"),err=field(r,0,"last_error");
        if ((!null(r,active) && !integer(r,active,1,UINT32_MAX,&n)) ||
            (!null(r,last) && !integer(r,last,1,UINT32_MAX,&n)) ||
            (!null(r,err) && (!string(r,err,last_error,sizeof(last_error)) || !last_error[0]))) return false;
        if (!strcmp(state,"idle") && !null(r,active)) return false;
        if (r->boot[0] && strcmp(r->boot,boot)) {
            /* A validated status explicitly identifies a reboot. Reset the
             * connection context before accepting any new snapshot history. */
            if (r->pending) fault(r,"Device reboot interrupted snapshot",true);
            wa_reader_reset_connection(r);
        }
        memcpy(r->boot,boot,sizeof(r->boot));
        strcpy(r->control_state,state);
    } else if (eq(r,type,"error")) {
        char command[33],code[65],reason[65];
        if (!required_string(r,"command",command,sizeof(command)) || !required_string(r,"code",code,sizeof(code))) return false;
        int reason_token=field(r,0,"reason"); reason[0]=0;
        if (reason_token>=0 && !string(r,reason_token,reason,sizeof(reason))) return false;
        if (strlen(code)>=sizeof(control_code)) return false;
        strcpy(control_code,code); strcpy(control_reason,reason);
        snprintf(r->error,sizeof(r->error),"%.32s: %.60s",command,reason[0] ? reason : code);
        r->stale=true;
    } else if (eq(r,type,"stopped") || eq(r,type,"cleared")) {
        if (!eq(r,field(r,0,"state"),"idle")) return false;
        strcpy(r->control_state,"idle");
    } else return false;
    strcpy(r->control_type,control_type); strcpy(r->control_code,control_code);
    strcpy(r->control_reason,control_reason);
    r->control_count++;
    return true;
}

static void process_line(wa_reader_t *r)
{
    bool snapshot=r->line_len>=7 && !memcmp(r->line,"[WFA1] ",7);
    bool control=r->line_len>=10 && !memcmp(r->line,"[WFACTL1] ",10);
    if (!snapshot && !control) {
        if (r->line_len>=5 && !memcmp(r->line,"[WFA",4) && digit(r->line[4]))
            fault(r,"Unsupported WFA frame version",true);
        else if (r->line_len>=8 && !memcmp(r->line,"[WFACTL",7) && digit(r->line[7]))
            fault(r,"Unsupported WFA control version",false);
        return;
    }
    if (!strict_json(r,snapshot ? 7 : 10)) { fault(r,"Invalid strict JSON",snapshot); return; }
    if (control) { if (!control_record(r)) fault(r,"Invalid WFA control record",false); return; }
    char boot[17]; int64_t scan,version;
    if (!num(r,"v",1,1,&version) || !num(r,"scan",1,UINT32_MAX,&scan) || !boot_value(r,field(r,0,"boot"),boot)) {
        fault(r,"Invalid snapshot identity/version",true); return;
    }
    int type=field(r,0,"type"); bool valid=false;
    if (eq(r,type,"begin")) valid=begin_record(r,boot,(uint32_t)scan);
    else if (r->pending && !strcmp(boot,r->working->boot) && scan==r->working->scan) {
        if (eq(r,type,"ap")) valid=ap_record(r);
        else if (eq(r,type,"end")) valid=end_record(r);
    }
    if (!valid) fault(r,"Invalid snapshot record/transaction",true);
}

void wa_reader_init(wa_reader_t *r, wa_snapshot_t *working, wa_snapshot_t *committed)
{
    if (!r || !working || !committed || working==committed) return;
    memset(r,0,sizeof(*r)); memset(working,0,sizeof(*working)); memset(committed,0,sizeof(*committed));
    r->working=working; r->committed=committed; r->stale=true;
}
void wa_reader_feed(wa_reader_t *r, const void *bytes, size_t len)
{
    if (!r || !r->working || !r->committed || (!bytes && len)) return;
    const unsigned char *p=bytes;
    for (size_t i=0;i<len;i++) {
        if (p[i]=='\r' || p[i]=='\n') {
            if (!r->dropping && r->line_len) { r->line[r->line_len]=0; process_line(r); }
            r->line_len=0; r->dropping=false;
        } else if (!r->dropping) {
            if (r->line_len==WA_MAX_LINE_BYTES) {
                if (r->pending || !memcmp(r->line,"[WFA",4)) fault(r,"Line exceeds 1024 bytes",true);
                r->line_len=0; r->dropping=true;
            } else r->line[r->line_len++]=(char)p[i];
        }
    }
}
void wa_reader_abort(wa_reader_t *r, const char *reason)
{
    if (!r) return;
    fault(r,reason ? reason : "Transport interrupted",true);
    r->line_len=0; r->dropping=false;
}
void wa_reader_reset_connection(wa_reader_t *r)
{
    if (!r) return;
    r->pending=false; r->caps_ready=false; r->stale=true;
    r->boot[0]=0; r->last_scan=0; r->line_len=0; r->dropping=false;
    r->max_records=0; r->default_records=0; r->control_state[0]=0;
    r->error[0]=0; r->last_terminal_status[0]=0;
    r->control_type[0]=0; r->control_code[0]=0; r->control_reason[0]=0;
}

static unsigned char lower(unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; }
static bool contains(const char *haystack, const char *needle)
{
    if (!needle[0]) return true;
    for (;*haystack;haystack++) {
        size_t i=0;
        while (needle[i] && haystack[i] && lower((unsigned char)needle[i])==lower((unsigned char)haystack[i])) i++;
        if (!needle[i]) return true;
    }
    return false;
}
bool wa_ap_matches(const wa_ap_t *a, const wa_filter_t *f)
{
    if (!a || !f) return false;
    return (!f->band || a->band==f->band) && a->rssi>=f->min_rssi &&
           (!f->primary || a->primary==f->primary) && (f->width<0 || a->width==f->width) &&
           (!f->auth[0] || !strcmp(a->auth,f->auth)) &&
           (contains(a->ssid_display,f->search) || contains(a->bssid,f->search));
}
uint32_t wa_ap_color_rgb(const wa_ap_t *a)
{
    static const uint32_t palette[]={0x51c5e5,0xffb454,0xbc8cff,0x64d98b,0xff7792,0x64a8ff,0xe5ce66,0xde8fe6};
    uint32_t hash=2166136261u;
    if (!a) return palette[0];
    for (size_t i=0;a->bssid[i];i++) hash=(hash^lower((unsigned char)a->bssid[i]))*16777619u;
    return palette[hash%(sizeof(palette)/sizeof(palette[0]))];
}

size_t wa_ap_segments(const wa_ap_t *a, wa_segment_t segments[2])
{
    if (!a || !segments || (a->width!=20 && a->width!=40 && a->width!=80 &&
        a->width!=160 && a->width!=8080) || !a->center1_mhz) return 0;
    int half=a->width==8080 ? 40 : a->width/2;
    segments[0]=(wa_segment_t){a->center1_mhz-half,a->center1_mhz+half};
    if (a->width!=8080) return 1;
    if (!a->center2_mhz) return 0;
    segments[1]=(wa_segment_t){a->center2_mhz-40,a->center2_mhz+40};
    return 2;
}

wa_segment_t wa_plot_range(const wa_snapshot_t *snapshot, const wa_filter_t *filter,
                           wa_band_t band, bool full_band)
{
    if (band==WA_BAND_24) return (wa_segment_t){2400,2500};
    wa_segment_t range={5150,5900};
    if (full_band || !snapshot || !snapshot->valid) return range;
    int low=5900, high=5150;
    bool found=false;
    for (unsigned i=0;i<snapshot->count && i<WA_MAX_APS;++i) {
        const wa_ap_t *ap=&snapshot->aps[i];
        if (ap->band!=WA_BAND_5 || (filter && !wa_ap_matches(ap,filter))) continue;
        int primary=wa_frequency(ap->primary);
        if (primary<5150 || primary>5900) continue;
        found=true;
        if (primary<low) low=primary;
        if (primary>high) high=primary;
        wa_segment_t segments[2];
        size_t count=wa_ap_segments(ap,segments);
        for (size_t s=0;s<count;++s) {
            if (segments[s].low_mhz<low) low=segments[s].low_mhz;
            if (segments[s].high_mhz>high) high=segments[s].high_mhz;
        }
    }
    if (!found) return range;
    low-=20; high+=20;
    /* Keep useful channel context even for a single AP or primary marker. */
    if (high-low<200) { int center=(low+high)/2; low=center-100; high=low+200; }
    if (low<5150) { high+=5150-low; low=5150; }
    if (high>5900) { low-=high-5900; high=5900; }
    low=(low/10)*10; high=((high+9)/10)*10;
    if (low<5150) low=5150;
    if (high>5900) high=5900;
    return (wa_segment_t){low,high};
}

wa_segment_t wa_plot_zoom(wa_segment_t base, unsigned factor, int center_mhz)
{
    int span=base.high_mhz-base.low_mhz;
    if ((factor!=2 && factor!=4) || span<=20) return base;
    int width=span/(int)factor;
    if (width<20) width=20;
    if (!center_mhz) center_mhz=(base.low_mhz+base.high_mhz)/2;
    if (center_mhz<base.low_mhz) center_mhz=base.low_mhz;
    if (center_mhz>base.high_mhz) center_mhz=base.high_mhz;
    int low=center_mhz-width/2;
    if (low<base.low_mhz) low=base.low_mhz;
    if (low+width>base.high_mhz) low=base.high_mhz-width;
    return (wa_segment_t){low,low+width};
}

static const uint8_t advice_channels_5[] = {
    36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,
    132,136,140,144,149,153,157,161,165,169,173,177
};

static bool advice_scope_complete(const wa_snapshot_t *snapshot, wa_band_t band)
{
    if (snapshot->band != WA_BAND_ANY && snapshot->band != band) return false;
    unsigned required = band == WA_BAND_24 ? 13 : sizeof(advice_channels_5);
    for (unsigned i = 0; i < required; ++i) {
        int channel = band == WA_BAND_24 ? (int)i + 1 : advice_channels_5[i];
        bool found = false;
        for (unsigned j = 0; j < snapshot->channel_count && j < WA_MAX_CHANNELS; ++j)
            if (snapshot->channels[j] == channel) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

void wa_propose_channels(const wa_snapshot_t *snapshot, wa_band_t band,
                         bool extended_5ghz, const char *exclude_bssid,
                         wa_channel_advice_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->state = WA_ADVICE_NO_SNAPSHOT;
    if (!snapshot || !snapshot->valid) return;
    out->state = WA_ADVICE_INVALID_BAND;
    if (band != WA_BAND_24 && band != WA_BAND_5) return;
    out->state = WA_ADVICE_TRUNCATED;
    if (snapshot->truncated) return;
    out->state = WA_ADVICE_INCOMPLETE_SCOPE;
    if (!advice_scope_complete(snapshot, band)) return;
    out->state = WA_ADVICE_OK;
    out->count = band == WA_BAND_24 ? 3 : extended_5ghz ? WA_ADVICE_MAX_CHOICES : 4;
    for (unsigned c = 0; c < out->count; ++c) {
        wa_channel_choice_t *choice = &out->choices[c];
        choice->channel = band == WA_BAND_24 ? 1 + (int)c * 5 : advice_channels_5[c];
        choice->dfs = band == WA_BAND_5 && choice->channel >= 52 && choice->channel <= 144;
        choice->strongest_rssi = -128;
        int center = wa_frequency(choice->channel);
        for (unsigned i = 0; i < snapshot->count && i < WA_MAX_APS; ++i) {
            const wa_ap_t *ap = &snapshot->aps[i];
            if (ap->band != band || (exclude_bssid && exclude_bssid[0] &&
                                    !strcmp(ap->bssid, exclude_bssid))) continue;
            if (!c) ++out->observed;
            int rssi = ap->rssi < -100 ? -100 : ap->rssi > -20 ? -20 : ap->rssi;
            uint32_t weight = (uint32_t)((rssi + 101) * (rssi + 101));
            wa_segment_t segments[2];
            size_t count = wa_ap_segments(ap, segments);
            if (!count) {
                if (!c) ++out->unknown;
                choice->score += weight * 20;
                continue;
            }
            int overlap = 0;
            for (size_t s = 0; s < count; ++s) {
                int low = segments[s].low_mhz > center - 10 ? segments[s].low_mhz : center - 10;
                int high = segments[s].high_mhz < center + 10 ? segments[s].high_mhz : center + 10;
                if (high > low) overlap += high - low;
            }
            if (overlap > 20) overlap = 20;
            choice->score += weight * (uint32_t)overlap;
            if (overlap) {
                ++choice->overlapping;
                if (ap->rssi > choice->strongest_rssi) choice->strongest_rssi = ap->rssi;
            }
        }
    }
    /* Ascending score, then channel. Equal scores are ties, never a claim that
     * the first channel is better. The UI explicitly reports best-score ties. */
    for (unsigned i = 1; i < out->count; ++i) {
        wa_channel_choice_t value = out->choices[i];
        unsigned j = i;
        while (j && (out->choices[j-1].score > value.score ||
               (out->choices[j-1].score == value.score && out->choices[j-1].channel > value.channel))) {
            out->choices[j] = out->choices[j-1]; --j;
        }
        out->choices[j] = value;
    }
}
