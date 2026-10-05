#ifndef OBSERVER_EXTENDED_H
#define OBSERVER_EXTENDED_H
/* JanOS ext_ver=1: raw preserves the complete bounded suffix for export.
 * Display strings escape control bytes, invalid UTF-8 and LVGL recolor '#'. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

typedef struct {
 bool present;
 /* Matches JanOS sniffer_extended.h SX_AP_SUFFIX_BYTES and SX_TEXT_BYTES. */
 char raw[2304], resolved_ssid[129];
 int hidden, pmf_capable, pmf_required, wps_present, rssi;
 bool rssi_known;
 uint32_t age_ms;
 bool age_known;
 char ssid_source[24], profile_source[24];
 char security[64], pairwise[128], group[64], akm[128], pmf[24], wps[32];
} observer_extended_t;

static inline void observer_ext_copy(char *out, size_t cap, const char *s, size_t n) {
 if (!out || !cap) return;
 if (n >= cap) n=cap-1;
 memcpy(out,s,n); out[n]=0;
}
static inline void observer_ext_append(char *out, size_t cap, const char *fmt, ...) {
 if (!out || !cap) return;
 size_t n=strlen(out); if(n>=cap-1) return;
 va_list ap; va_start(ap,fmt); vsnprintf(out+n,cap-n,fmt,ap); va_end(ap);
}
static inline bool observer_ext_value(const observer_extended_t *e, const char *key, char *out, size_t cap) {
 if(out && cap) out[0]=0;
 if(!e || !key || !out || !cap) return false;
 size_t k=strlen(key); const char *p=e->raw;
 while(*p) {
  while(*p==' ' || *p=='|') p++;
  const char *end=strchr(p,'|'); if(!end) end=p+strlen(p);
  const char *eq=memchr(p,'=',(size_t)(end-p));
  if(eq && (size_t)(eq-p)==k && !memcmp(p,key,k)) {
   const char *v=eq+1; while(v<end && *v==' ') v++;
   while(end>v && (end[-1]==' ' || end[-1]=='\r' || end[-1]=='\n')) end--;
   observer_ext_copy(out,cap,v,(size_t)(end-v)); return true;
  }
  p=*end ? end+1:end;
 }
 return false;
}
static inline int observer_ext_nibble(char c) {
 if(c>='0' && c<='9') return c-'0';
 if(c>='a' && c<='f') return c-'a'+10;
 if(c>='A' && c<='F') return c-'A'+10;
 return -1;
}
static inline bool observer_ext_hex_valid(const char *s) {
 if(!strcmp(s,"unknown") || !strcmp(s,"absent")) return true;
 size_t n=strlen(s); if(n%2) return false;
 for(size_t i=0;i<n;i++) if(observer_ext_nibble(s[i])<0) return false;
 return true;
}
/* Valid UTF-8 codepoints remain intact; unsafe bytes become ASCII \\xNN. */
static inline bool observer_ext_selector_valid(const char *s) {
 if(!strcmp(s,"unknown") || !strcmp(s,"absent")) return true;
 if(!s[0]) return true;
 const char *p=s;
 while(*p) {
  for(int i=0;i<8;i++) { if(!p[i] || observer_ext_nibble(p[i])<0) return false; }
  p+=8; if(!*p) return true;
  if(*p!=',' || !p[1]) return false;
  p++;
 }
 return false;
}
static inline void observer_ext_hex_display(const char *hex, char *out, size_t cap) {
 if(!out || !cap) return;
 out[0]=0;
 if(!strcmp(hex,"unknown") || !strcmp(hex,"absent")) { observer_ext_copy(out,cap,hex,strlen(hex)); return; }
 size_t len=strlen(hex)/2;
 for(size_t i=0;i<len;) {
  unsigned char bytes[4]; bytes[0]=(unsigned char)((observer_ext_nibble(hex[i*2])<<4)|observer_ext_nibble(hex[i*2+1]));
  size_t count=1; bool safe=bytes[0]>=32 && bytes[0]<127 && bytes[0]!='#';
  if(bytes[0]>=0xC2 && bytes[0]<=0xF4) {
   count=bytes[0]<0xE0?2:bytes[0]<0xF0?3:4; safe=i+count<=len;
   for(size_t j=1;j<count && safe;j++) {
    bytes[j]=(unsigned char)((observer_ext_nibble(hex[(i+j)*2])<<4)|observer_ext_nibble(hex[(i+j)*2+1]));
    if(bytes[j]<0x80 || bytes[j]>0xBF) safe=false;
   }
   if(safe && ((bytes[0]==0xE0 && bytes[1]<0xA0) || (bytes[0]==0xED && bytes[1]>=0xA0) || (bytes[0]==0xF0 && bytes[1]<0x90) || (bytes[0]==0xF4 && bytes[1]>=0x90))) safe=false;
   /* C1 controls and Unicode line separators are escaped too. */
   if(safe && ((bytes[0]==0xC2 && bytes[1]<0xA0) || (count==3 && bytes[0]==0xE2 && bytes[1]==0x80 && (bytes[2]==0xA8 || bytes[2]==0xA9)))) safe=false;
  }
  if(safe) { size_t n=strlen(out); if(n+count>=cap) break; memcpy(out+n,bytes,count); out[n+count]=0; i+=count; }
  else { observer_ext_append(out,cap,"\\x%02X",bytes[0]); i++; }
 }
}
static inline int observer_ext_tri(const observer_extended_t *e,const char *key) {
 char v[32]; if(!observer_ext_value(e,key,v,sizeof v)) return -1;
 return !strcmp(v,"0")?0:!strcmp(v,"1")?1:-1;
}
static inline const char *observer_ext_tri_name(int v) { return v<0?"unknown":v?"yes":"no"; }
static inline void observer_ext_get(const observer_extended_t *e,const char *key,char *v,size_t cap) {
 if(!observer_ext_value(e,key,v,cap)) observer_ext_copy(v,cap,"unknown",7);
}
static inline const char *observer_ext_selector_name(const char *s,bool akm) {
 if(strlen(s)!=8) return NULL;
 uint32_t selector=0;
 for(int i=0;i<8;i++) { int nibble=observer_ext_nibble(s[i]); if(nibble<0) return NULL; selector=(selector<<4)|(unsigned)nibble; }
 bool rsn=(selector>>8)==0x000FAC, wpa=(selector>>8)==0x0050F2;
 if(!rsn && !wpa) return NULL;
 int type=(observer_ext_nibble(s[6])<<4)|observer_ext_nibble(s[7]);
 if(akm) {
  switch(type) {
   case 1:return "802.1X"; case 2:return "PSK";
   case 3:return rsn?"FT-802.1X":NULL; case 4:return rsn?"FT-PSK":NULL;
   case 5:return rsn?"802.1X-SHA256":NULL; case 6:return rsn?"PSK-SHA256":NULL;
   case 8:return rsn?"SAE":NULL; case 9:return rsn?"FT-SAE":NULL;
   case 11:return rsn?"Suite-B":NULL; case 12:return rsn?"Suite-B-192":NULL;
   case 13:return rsn?"FT-802.1X-SHA384":NULL; case 18:return rsn?"OWE":NULL;
   default:return NULL;
  }
 }
 switch(type) {
  case 0:return "Use group"; case 1:return "WEP-40"; case 2:return "TKIP";
  case 4:return "CCMP-128"; case 5:return "WEP-104";
  case 6:return rsn?"BIP-CMAC-128":NULL; case 8:return rsn?"GCMP-128":NULL;
  case 9:return rsn?"GCMP-256":NULL; case 10:return rsn?"CCMP-256":NULL;
  case 11:return rsn?"BIP-GMAC-128":NULL; case 12:return rsn?"BIP-GMAC-256":NULL;
  case 13:return rsn?"BIP-CMAC-256":NULL; default:return NULL;
 }
}
static inline void observer_ext_selectors(const char *raw,bool akm,char *out,size_t cap) {
 if(!out || !cap) return;
 out[0]=0; const char *p=raw;
 while(*p) {
  const char *end=strchr(p,','); if(!end) end=p+strlen(p);
  char s[64]; observer_ext_copy(s,sizeof s,p,(size_t)(end-p));
  const char *name=observer_ext_selector_name(s,akm);
  observer_ext_append(out,cap,"%s%s",out[0]?", ":"",name?name:s);
  p=*end?end+1:end;
 }
 if(!out[0]) observer_ext_copy(out,cap,"unknown",7);
}
static inline void observer_ext_profile_field(const observer_extended_t *e,const char *field,bool akm,char *out,size_t cap) {
 char key[40],raw[256],display[256],status[32]; out[0]=0;
 const char *profiles[]={"rsn","wpa"};
 for(int i=0;i<2;i++) {
  snprintf(key,sizeof key,"%s_status",profiles[i]); observer_ext_get(e,key,status,sizeof status);
  if(strcmp(status,"valid") && strcmp(status,"truncated")) continue;
  snprintf(key,sizeof key,"%s_%s",profiles[i],field); observer_ext_get(e,key,raw,sizeof raw);
  observer_ext_selectors(raw,akm,display,sizeof display);
  observer_ext_append(out,cap,"%s%s%s",out[0]?"; ":"",i?"WPA: ":"",display);
 }
 if(!out[0]) observer_ext_copy(out,cap,"unknown",7);
}
/* Locate the suffix after the complete legacy AP/client record, not inside
 * an SSID/vendor and not at ext_ver (which may occur anywhere in the suffix).
 * Generic parser-only records use the first delimiter after their prefix. */
static inline const char *observer_ext_after_legacy(const char *p) {
 while(*p==' ' || *p=='\t') p++;
 if(*p=='[') { const char *end=strchr(p,']'); if(!end) return NULL; p=end+1; }
 while(*p==' ' || *p=='\t') p++;
 return *p=='|'?p:NULL;
}
static inline const char *observer_ext_suffix_start(const char *line) {
 if(!line) return NULL;
 const char *scan=line, *ap_suffix=NULL; bool ap_header=false;
 while((scan=strstr(scan,", CH"))!=NULL) {
  const char *p=scan+4; scan+=4;
  if(*p<'0' || *p>'9') continue;
  while(*p>='0' && *p<='9') p++;
  if(*p++!=':') continue;
  while(*p==' ' || *p=='\t') p++;
  if(*p<'0' || *p>'9') continue;
  while(*p>='0' && *p<='9') p++;
  ap_header=true;
  const char *suffix=observer_ext_after_legacy(p);
  if(suffix) ap_suffix=suffix;
 }
 if(ap_header) return ap_suffix;
 const char *p=line; while(*p==' ' || *p=='\t') p++;
 bool mac=true;
 for(int octet=0;octet<6 && mac;octet++) {
  if(!p[0] || observer_ext_nibble(p[0])<0) { mac=false; break; }
  p++; if(!p[0] || observer_ext_nibble(p[0])<0) { mac=false; break; }
  p++; if(octet<5) { if(*p!=':') { mac=false; break; } p++; }
 }
 if(mac) return observer_ext_after_legacy(p);
 /* Fixture/API callers may supply a simple textual legacy prefix. */
 const char *suffix=strstr(line," | ");
 return suffix?suffix+1:NULL;
}
static inline bool observer_ext_parse(const char *line, observer_extended_t *out) {
 if(!out) return false;
 memset(out,0,sizeof *out); out->hidden=out->pmf_capable=out->pmf_required=out->wps_present=-1;
 if(!line) return false;
 const char *start=observer_ext_suffix_start(line);
 if(!start) return false;
 start++; while(*start==' ') start++;
 size_t n=strlen(start); while(n && (start[n-1]=='\r' || start[n-1]=='\n' || start[n-1]==' ')) n--;
 if(n>=sizeof out->raw) return false;
 for(size_t i=0;i<n;i++) if((unsigned char)start[i]<32 || (unsigned char)start[i]==127) return false;
 observer_ext_copy(out->raw,sizeof out->raw,start,n);
 /* Validate token boundaries, duplicated keys, hex, and numeric fields before publishing. */
 const char *p=out->raw; int version_count=0;
 while(*p) {
  while(*p==' ') p++;
  const char *end=strchr(p,'|'); if(!end) end=p+strlen(p);
  const char *trim=end; while(trim>p && trim[-1]==' ') trim--;
  const char *eq=memchr(p,'=',(size_t)(trim-p));
  if(!eq || eq==p || (size_t)(eq-p)>=64) goto invalid;
  char key[64],val[129]; observer_ext_copy(key,sizeof key,p,(size_t)(eq-p));
  for(size_t i=0;key[i];i++) if(!((key[i]>='a' && key[i]<='z') || (key[i]>='0' && key[i]<='9') || key[i]=='_')) goto invalid;
  const char *v=eq+1; while(v<trim && *v==' ') v++;
  size_t vl=(size_t)(trim-v);
  observer_ext_copy(val,sizeof val,v,vl);
  /* No duplicate key: conflicting values must never silently overwrite. */
  const char *prior=out->raw;
  while(prior<p) { while(*prior==' ') prior++; const char *pe=strchr(prior,'|'); if(!pe || pe>=p) break;
   const char *pq=memchr(prior,'=',(size_t)(pe-prior));
   if(pq && (size_t)(pq-prior)==strlen(key) && !memcmp(prior,key,strlen(key))) goto invalid;
   prior=pe+1;
  }
  if(!strcmp(key,"ext_ver")) { version_count++; if(strcmp(val,"1")) goto invalid; }
  size_t kl=strlen(key);
  if(kl>=4 && !strcmp(key+kl-4,"_hex")) {
   bool sentinel=(vl==7 && !memcmp(v,"unknown",7)) || (vl==6 && !memcmp(v,"absent",6));
   if(!sentinel) {
    if(vl%2) goto invalid;
    for(size_t i=0;i<vl;i++) if(observer_ext_nibble(v[i])<0) goto invalid;
    if((!strncmp(key,"wps_",4) && vl>128) || (!strcmp(key,"resolved_ssid_hex") && vl>64)) goto invalid;
   }
  }
  if((!strncmp(key,"rsn_",4) || !strncmp(key,"wpa_",4)) &&
     (!strcmp(key+4,"group") || !strcmp(key+4,"pairwise") || !strcmp(key+4,"akm") || !strcmp(key+4,"group_mgmt")) && !observer_ext_selector_valid(val)) goto invalid;
  if(!strcmp(key,"resolved_ssid_hex") && strcmp(val,"unknown") && strcmp(val,"absent") && strlen(val)>64) goto invalid;
  if(!strcmp(key,"rssi") && strcmp(val,"unknown")) {
   if(vl>=32) goto invalid;
   char *tail; errno=0; long r=strtol(val,&tail,10); if(!val[0] || *tail || errno || r< -127 || r>0) goto invalid;
   out->rssi=(int)r; out->rssi_known=true;
  }
  if(!strcmp(key,"age_ms") && strcmp(val,"unknown")) {
   if(!val[0] || vl>=32) goto invalid;
   uint32_t a=0; for(size_t i=0;val[i];i++) { if(val[i]<'0' || val[i]>'9') goto invalid; unsigned d=(unsigned)(val[i]-'0'); if(a>(UINT32_MAX-d)/10) goto invalid; a=a*10+d; }
   out->age_ms=a; out->age_known=true;
  }
  if(!*end) break;
  p=end+1; if(!*p) goto invalid;
 }
 if(version_count!=1) goto invalid;
 out->hidden=observer_ext_tri(out,"hidden"); out->pmf_capable=observer_ext_tri(out,"pmf_capable"); out->pmf_required=observer_ext_tri(out,"pmf_required"); out->wps_present=observer_ext_tri(out,"wps_present");
 char val[129],rsn[32],wpa[32],akm[80];
 observer_ext_get(out,"resolved_ssid_hex",val,sizeof val); observer_ext_hex_display(val,out->resolved_ssid,sizeof out->resolved_ssid);
 if(!strcmp(out->resolved_ssid,"unknown") || !strcmp(out->resolved_ssid,"absent")) out->resolved_ssid[0]=0;
 observer_ext_get(out,"ssid_source",out->ssid_source,sizeof out->ssid_source); observer_ext_get(out,"profile_source",out->profile_source,sizeof out->profile_source);
 observer_ext_get(out,"rsn_status",rsn,sizeof rsn); observer_ext_get(out,"wpa_status",wpa,sizeof wpa); observer_ext_get(out,"rsn_akm",akm,sizeof akm);
 /* Normalize only temporary AKM display classification; raw stays byte exact. */
 for(size_t i=0;akm[i];i++) if(akm[i]>='a' && akm[i]<='f') akm[i]=(char)(akm[i]-'a'+'A');
 const char *security="Unknown";
 if(!strcmp(rsn,"valid")) {
  bool sae=strstr(akm,"000FAC08") || strstr(akm,"000FAC09"), psk=strstr(akm,"000FAC02") || strstr(akm,"000FAC04") || strstr(akm,"000FAC06");
  security=sae?(psk?"WPA2/WPA3 Personal":"WPA3 Personal"):strstr(akm,"000FAC12")?"OWE":strstr(akm,"000FAC0C")?"WPA3 Enterprise 192-bit":psk?"WPA2 Personal":"RSN";
 } else if(!strcmp(wpa,"valid")) security="WPA";
 else if(!strcmp(rsn,"absent") && !strcmp(wpa,"absent")) { int privacy=observer_ext_tri(out,"privacy"); security=privacy==0?"Open":privacy==1?"Privacy (no WPA/RSN)":"Unknown"; }
 observer_ext_copy(out->security,sizeof out->security,security,strlen(security));
 if(!strcmp(rsn,"valid") && !strcmp(wpa,"valid")) observer_ext_append(out->security,sizeof out->security," + WPA");
 observer_ext_profile_field(out,"pairwise",false,out->pairwise,sizeof out->pairwise); observer_ext_profile_field(out,"group",false,out->group,sizeof out->group); observer_ext_profile_field(out,"akm",true,out->akm,sizeof out->akm);
 const char *pmf=out->pmf_required==1?"Required":out->pmf_capable==1 && out->pmf_required==0?"Capable":out->pmf_capable==0 && out->pmf_required==0?"Not advertised":"Unknown";
 observer_ext_copy(out->pmf,sizeof out->pmf,pmf,strlen(pmf));
 const char *wps=out->wps_present==1?"WPS":out->wps_present==0?"WPS not advertised":"WPS unknown"; observer_ext_copy(out->wps,sizeof out->wps,wps,strlen(wps));
 out->present=true; return true;
invalid:
 memset(out,0,sizeof *out); out->hidden=out->pmf_capable=out->pmf_required=out->wps_present=-1; return false;
}
static inline void observer_ext_detail_field(const observer_extended_t *e,const char *key,const char *label,bool hex,char *out,size_t cap) {
 char value[129],display[257]; observer_ext_get(e,key,value,sizeof value);
 if(hex) observer_ext_hex_display(value,display,sizeof display); else observer_ext_copy(display,sizeof display,value,strlen(value));
 observer_ext_append(out,cap,"%s: %s\n",label,display);
}
static inline void observer_ext_details(const observer_extended_t *e,int tab,char *out,size_t cap) {
 if(!out || !cap) return;
 out[0]=0;
 if(!e || !e->present) { observer_ext_append(out,cap,"Extended unavailable"); return; }
 if(tab==4) { observer_ext_append(out,cap,"%s",e->raw); return; }
 if(tab==0) {
  observer_ext_append(out,cap,"Resolved SSID: %s\nHidden: %s\n",e->resolved_ssid[0]?e->resolved_ssid:"unknown",observer_ext_tri_name(e->hidden));
  observer_ext_detail_field(e,"bssid","BSSID",false,out,cap); observer_ext_detail_field(e,"rssi","RSSI (dBm)",false,out,cap); observer_ext_detail_field(e,"age_ms","Last seen age (ms)",false,out,cap);
  observer_ext_detail_field(e,"ssid_source","SSID source",false,out,cap); observer_ext_detail_field(e,"profile_source","Profile source",false,out,cap); observer_ext_detail_field(e,"frame_status","Frame status",false,out,cap);
 } else if(tab==1) {
  observer_ext_append(out,cap,"Advertised profile: %s\nPMF: %s\nPMF capable: %s\nPMF required: %s\n",e->security,e->pmf,observer_ext_tri_name(e->pmf_capable),observer_ext_tri_name(e->pmf_required));
  const char *profiles[]={"rsn","wpa"};
  const char *fields[]={"status","group","pairwise","akm","group_mgmt","truncated"};
  for(int i=0;i<2;i++) {
   observer_ext_append(out,cap,"\n%s\n",i?"Legacy WPA":"RSN");
   for(int j=0;j<6;j++) {
    if(i && j==4) continue;
    char key[40],value[256],display[256]; snprintf(key,sizeof key,"%s_%s",profiles[i],fields[j]); observer_ext_get(e,key,value,sizeof value);
    if(j>=1 && j<=4) observer_ext_selectors(value,j==3,display,sizeof display); else observer_ext_copy(display,sizeof display,value,strlen(value));
    observer_ext_append(out,cap,"%s: %s\n",fields[j],display);
   }
  }
  observer_ext_detail_field(e,"privacy","Privacy",false,out,cap);
 } else if(tab==2) {
  if(e->wps_present==0) { observer_ext_append(out,cap,"WPS not advertised"); return; }
  observer_ext_append(out,cap,"%s\n",e->wps);
  const char *keys[]={"wps_status","wps_state","wps_config_methods","wps_setup_locked","wps_selected_registrar","wps_manufacturer_hex","wps_model_name_hex","wps_model_number_hex","wps_device_name_hex","wps_manufacturer_truncated","wps_model_name_truncated","wps_model_number_truncated","wps_device_name_truncated","wps_truncated"};
  const char *labels[]={"IE status","State","Configuration methods","Setup locked","Selected registrar","Manufacturer","Model name","Model number","Device name","Manufacturer truncated","Model name truncated","Model number truncated","Device name truncated","IE truncated"};
  for(size_t i=0;i<sizeof keys/sizeof keys[0];i++) {
   if(i==1) {
    char value[32]; observer_ext_get(e,keys[i],value,sizeof value);
    observer_ext_append(out,cap,"State: %s (%s)\n",!strcmp(value,"1")?"Not configured":!strcmp(value,"2")?"Configured":"unknown",value);
   } else if(i==2) {
    char value[32]; observer_ext_get(e,keys[i],value,sizeof value);
    observer_ext_append(out,cap,"Configuration methods: %s",value);
    char *tail; unsigned long methods=strtoul(value,&tail,10);
    if(value[0] && !*tail && methods<=65535) {
     const unsigned bits[]={1,2,4,8,16,32,64,128,256,512,1024,8192,16384};
     const char *names[]={"USB","Ethernet","Label","Display","External NFC","Internal NFC","NFC interface","Push button","Keypad","Virtual push button","Physical push button","Virtual display","Physical display"};
     for(size_t j=0;j<sizeof bits/sizeof bits[0];j++) if(methods & bits[j]) observer_ext_append(out,cap,"; %s",names[j]);
    }
    observer_ext_append(out,cap,"\n");
   } else if(i==3 || i==4) observer_ext_append(out,cap,"%s: %s\n",labels[i],observer_ext_tri_name(observer_ext_tri(e,keys[i])));
   else observer_ext_detail_field(e,keys[i],labels[i],i>=5 && i<=8,out,cap);
  }
 }
}
#endif
