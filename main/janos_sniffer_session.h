#ifndef JANOS_SNIFFER_SESSION_H
#define JANOS_SNIFFER_SESSION_H
#include "sniffer_clients_model.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    JS_EMPTY, JS_LOADING, JS_READY, JS_STARTING, JS_ACTIVE, JS_ERROR, JS_RELOAD_REQUIRED
} JsPhase;
typedef bool (*JsSendCommand)(void* context, const char* command);
typedef struct {
    sc_model_t model;
    JsPhase phase;
    uint8_t pending_ap[6], pending_sta[6];
    uint32_t list_started_ms, last_record_ms, start_sent_ms;
    char error[48];
    JsSendCommand send;
    void* send_context;
} JsSession;

/* Zero-initialize once. Call all functions from the same worker/task. */
static inline void js_init(JsSession* s, JsSendCommand send, void* context) {
    memset(s, 0, sizeof(*s));
    s->send = send;
    s->send_context = context;
}
static inline void js_set_error(JsSession* s, JsPhase phase, const char* message) {
    s->phase = phase;
    snprintf(s->error, sizeof(s->error), "%s", message);
}
static inline bool js_send(JsSession* s, const char* command) {
    return s->send && s->send(s->send_context, command);
}
static inline bool js_load(JsSession* s, uint32_t now_ms) {
    if(s->phase == JS_LOADING || s->phase == JS_STARTING || s->phase == JS_ACTIVE) return false;
    sc_model_free(&s->model);
    s->error[0] = 0;
    s->phase = JS_LOADING;
    s->list_started_ms = s->last_record_ms = now_ms;
    if(js_send(s, "show_sniffer_clients")) return true;
    js_set_error(s, JS_ERROR, "UART TX failed");
    return false;
}
static inline bool js_start(JsSession* s, unsigned selected_client, uint32_t now_ms) {
    if(s->phase != JS_READY || !s->model.complete || s->model.failed ||
       selected_client >= s->model.client_count) return false;
    const sc_client_t* client = &s->model.clients[selected_client];
    const sc_ap_t* ap = &s->model.aps[client->ap];
    char bssid[18], station[18], command[80];
    memcpy(s->pending_ap, ap->bssid, 6);
    memcpy(s->pending_sta, client->mac, 6);
    sc_mac_text(bssid, s->pending_ap);
    sc_mac_text(station, s->pending_sta);
    snprintf(command, sizeof(command), "start_deauth_station %s %s", bssid, station);
    s->phase = JS_STARTING;
    s->start_sent_ms = now_ms;
    s->error[0] = 0;
    if(js_send(s, command)) return true;
    js_set_error(s, JS_RELOAD_REQUIRED, "UART TX failed; check link");
    return false;
}
static inline sc_event_t js_line(JsSession* s, const char* line, uint32_t now_ms) {
    bool starting = s->phase == JS_STARTING;
    if(!starting && s->phase != JS_LOADING) return SC_IGNORED;
    sc_event_t event = sc_model_line(
        &s->model, line, starting ? s->pending_ap : NULL, starting ? s->pending_sta : NULL);
    if(!starting && event == SC_UPDATED) {
        s->last_record_ms = now_ms;
        if(s->model.failed) js_set_error(s, JS_ERROR, s->model.error);
        else if(s->model.complete) s->phase = JS_READY;
    } else if(starting && event == SC_STARTED) {
        s->phase = JS_ACTIVE;
    } else if(starting && event == SC_REFUSED) {
        js_set_error(s, JS_READY, s->model.error);
    }
    return event;
}
/* Poll even if no bytes arrive. Unsigned subtraction handles millisecond wrap. */
static inline void js_tick(JsSession* s, uint32_t now_ms) {
    if(s->phase == JS_STARTING && (uint32_t)(now_ms - s->start_sent_ms) >= 15000U) {
        bool sent = js_send(s, "stop");
        js_set_error(s, JS_RELOAD_REQUIRED, sent ? "Timeout; stop requested; reload" : "Timeout; stop TX failed");
    } else if(s->phase == JS_LOADING &&
              ((uint32_t)(now_ms - s->last_record_ms) >= 5000U ||
               (uint32_t)(now_ms - s->list_started_ms) >= 300000U)) {
        js_set_error(s, JS_ERROR, "Incomplete list; update JanOS / reload");
    }
}
static inline bool js_cancel(JsSession* s) {
    bool starting = s->phase == JS_STARTING;
    bool active = s->phase == JS_ACTIVE;
    bool sent = (!starting && !active) || js_send(s, "stop");
    if(starting || !sent) s->phase = JS_RELOAD_REQUIRED;
    else if(s->model.complete && !s->model.failed) s->phase = JS_READY;
    else s->phase = JS_EMPTY;
    if(!sent) snprintf(s->error, sizeof(s->error), "Stop TX failed; check link");
    return sent; /* This is TX status, not proof of radio shutdown. */
}
static inline void js_rx_loss(JsSession* s) {
    bool need_stop = s->phase == JS_STARTING || s->phase == JS_ACTIVE;
    bool sent = !need_stop || js_send(s, "stop");
    js_set_error(s, JS_RELOAD_REQUIRED, sent ? "RX lost; reload required" : "RX lost; stop TX failed");
}
static inline void js_free(JsSession* s) {
    /* Stop/cancel and join UART callbacks before freeing the session. */
    sc_model_free(&s->model);
}

/* Optional byte framer, useful when the app has no bounded line reader.
 * Use existing complete-line callbacks OR this framer, not both in parallel. */
typedef struct { char text[1024]; size_t used; bool discard; } JsLine;
static inline void js_feed(JsSession* s, JsLine* line,
                           const uint8_t* bytes, size_t count, uint32_t now_ms) {
    for(size_t i = 0; i < count; ++i) {
        uint8_t byte = bytes[i];
        if(byte == '\r' || byte == '\n') {
            if(!line->discard && line->used) {
                line->text[line->used] = 0;
                js_line(s, line->text, now_ms);
            }
            line->used = 0;
            line->discard = false;
        } else if(!line->discard) {
            if(byte == 0 || line->used + 1 >= sizeof(line->text)) {
                line->used = 0;
                line->discard = true;
                js_rx_loss(s);
            } else line->text[line->used++] = (char)byte;
        }
    }
}
#endif
