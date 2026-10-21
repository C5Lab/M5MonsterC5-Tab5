#include "wifi_analyzer.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define WA_PORTS 3
#define WA_PROBE_MS 3500U
#define WA_WORKER_STACK 8192U

struct wa_session {
    int tab;
    SemaphoreHandle_t mutex;
    atomic_bool running, cancel;
    wa_scan_request_t request;
    wa_reader_t reader;
    wa_snapshot_t working, committed;
    wa_status_t status;
    uint64_t committed_at;
};

static wa_host_hooks_t s_host;
static wa_session_t *s_sessions[WA_PORTS];
static portMUX_TYPE s_sessions_lock = portMUX_INITIALIZER_UNLOCKED;

static uint64_t milliseconds(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static wa_session_t *session_for(int tab)
{
    if (tab < 0 || tab >= WA_PORTS) return NULL;
    portENTER_CRITICAL(&s_sessions_lock);
    wa_session_t *session = s_sessions[tab];
    portEXIT_CRITICAL(&s_sessions_lock);
    return session;
}

void wa_init(const wa_host_hooks_t *hooks)
{
    if (hooks) s_host = *hooks;
}

wa_session_t *wa_session_get(int tab)
{
    if (tab < 0 || tab >= WA_PORTS) return NULL;
    wa_session_t *session = session_for(tab);
    if (session) return session;
    /* Called on the LVGL thread. Large buffers never fall back to internal RAM. */
    session = heap_caps_calloc(1, sizeof(*session), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!session) return NULL;
    session->mutex = xSemaphoreCreateMutex();
    if (!session->mutex) { heap_caps_free(session); return NULL; }
    session->tab = tab;
    atomic_init(&session->running, false);
    atomic_init(&session->cancel, false);
    wa_reader_init(&session->reader, &session->working, &session->committed);
    snprintf(session->status.message, sizeof(session->status.message),
             "Ready. Scan to check JanOS analyzer support.");
    portENTER_CRITICAL(&s_sessions_lock);
    s_sessions[tab] = session;
    portEXIT_CRITICAL(&s_sessions_lock);
    return session;
}

bool wa_busy_tab(int tab)
{
    wa_session_t *session = session_for(tab);
    return session && atomic_load(&session->running);
}

static bool cancelled(wa_session_t *session)
{
    return atomic_load(&session->cancel);
}

static void message(wa_session_t *session, const char *text, bool stale, bool recovery)
{
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    snprintf(session->status.message, sizeof(session->status.message), "%s", text);
    session->status.stale = stale;
    session->status.needs_recovery = recovery;
    ++session->status.revision;
    xSemaphoreGive(session->mutex);
}

bool wa_copy_view(wa_session_t *session, wa_snapshot_t *snapshot, wa_status_t *status)
{
    if (!session || !snapshot || !status || xSemaphoreTake(session->mutex, 0) != pdTRUE)
        return false;
    memcpy(snapshot, &session->committed, sizeof(*snapshot));
    *status = session->status;
    status->running = atomic_load(&session->running);
    status->capable = session->reader.caps_ready;
    status->stale = status->stale || session->reader.stale;
    status->age_ms = session->committed.valid ? milliseconds() - session->committed_at : 0;
    xSemaphoreGive(session->mutex);
    return true;
}

void wa_stop(wa_session_t *session)
{
    if (session) atomic_store(&session->cancel, true);
}

void wa_leave(int tab)
{
    wa_stop(session_for(tab));
}

static bool validate_request(const wa_scan_request_t *request)
{
    if (!request || request->band < WA_BAND_ANY || request->band > WA_BAND_5 ||
        request->profile > 2 || request->limit < 1 || request->limit > WA_MAX_APS ||
        (request->repeat_ms && (request->repeat_ms < 1000 || request->repeat_ms > 3600000)) ||
        !memchr(request->channels, '\0', sizeof(request->channels))) return false;
    const char *p = request->channels;
    uint8_t channels[WA_MAX_CHANNELS];
    unsigned count = 0;
    while (*p) {
        unsigned channel = 0;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            channel = channel * 10U + (unsigned)(*p++ - '0');
            if (channel > 177) return false;
        }
        if (!wa_frequency((int)channel) || count >= WA_MAX_CHANNELS) return false;
        if ((request->band == WA_BAND_24 && channel > 14) ||
            (request->band == WA_BAND_5 && channel <= 14)) return false;
        for (unsigned i = 0; i < count; ++i) if (channels[i] == channel) return false;
        channels[count++] = (uint8_t)channel;
        if (!*p) break;
        if (*p++ != ',' || !*p) return false;
    }
    return true;
}

static bool write_line(wa_session_t *session, const char *line)
{
    size_t length = strlen(line), sent = 0;
    uint64_t deadline = milliseconds() + 2000;
    while (sent < length && milliseconds() < deadline && s_host.connected(session->tab)) {
        int n = s_host.write(session->tab, line + sent, length - sent);
        if (n <= 0 || (size_t)n > length - sent) return false;
        sent += (size_t)n;
    }
    return sent == length;
}

/* Only the worker mutates the reader. The UI copies under this same mutex. */
static bool pump(wa_session_t *session)
{
    uint8_t bytes[512];
    if (!s_host.connected(session->tab)) return false;
    int n = s_host.read(session->tab, bytes, sizeof(bytes), 80);
    if (n < 0 || (size_t)n > sizeof(bytes)) return false;
    if (n) {
        xSemaphoreTake(session->mutex, portMAX_DELAY);
        uint32_t before = session->reader.commit_count;
        wa_reader_feed(&session->reader, bytes, (size_t)n);
        if (session->reader.commit_count != before) {
            session->committed_at = milliseconds();
            session->status.stale = false;
            ++session->status.revision;
        }
        xSemaphoreGive(session->mutex);
    } else vTaskDelay(1);
    return true;
}

static void abort_transaction(wa_session_t *session, const char *reason)
{
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    wa_reader_abort(&session->reader, reason);
    xSemaphoreGive(session->mutex);
    message(session, reason, true, false);
}

static bool query_idle(wa_session_t *session, uint32_t timeout_ms)
{
    uint32_t controls = session->reader.control_count;
    if (!write_line(session, "wifi_analyzer status\r\n")) return false;
    uint64_t deadline = milliseconds() + timeout_ms;
    while (milliseconds() < deadline) {
        if (!pump(session)) return false;
        if (session->reader.control_count != controls &&
            (!strcmp(session->reader.control_type, "status") ||
             !strcmp(session->reader.control_type, "stopped")) &&
            !strcmp(session->reader.control_state, "idle")) return true;
    }
    return false;
}

/* Once a scan command might have reached JanOS, releasing RX on a timeout is
 * unsafe. Keep this worker/owner alive until idle is acknowledged or the link
 * disconnects. The UI stays responsive and other physical ports remain usable. */
static void recover_idle(wa_session_t *session)
{
    char prior_message[sizeof(session->status.message)];
    bool was_stale = session->status.stale;
    snprintf(prior_message, sizeof(prior_message), "%s", session->status.message);
    bool stopping = cancelled(session);
    message(session, stopping ? "Stopping scan; waiting for JanOS idle..." :
            "Synchronizing with JanOS...", session->status.stale, false);
    /* Leading newline terminates a possibly partial previous command. */
    (void)write_line(session, "\r\nwifi_analyzer stop\r\n");
    uint64_t start = milliseconds();
    while (s_host.connected(session->tab)) {
        if (query_idle(session, 1800)) {
            message(session, prior_message, was_stale, false);
            return;
        }
        if (milliseconds() - start > 6500) {
            message(session, "JanOS did not confirm idle. Restart/reconnect JanOS to recover.", true, true);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static uint32_t scan_budget(const wa_scan_request_t *request, uint32_t baud)
{
    if (!baud) baud = 115200;
    unsigned channels = request->band == WA_BAND_24 ? 14 : request->band == WA_BAND_5 ? 28 : 42;
    if (request->channels[0]) {
        channels = 1;
        for (const char *p = request->channels; *p; ++p) if (*p == ',') ++channels;
    }
    unsigned dwell = request->profile == 0 ? 300 : 600;
    uint64_t transmission = ((uint64_t)(request->limit + 2) * 1027 * 10000 + baud - 1) / baud;
    return (uint32_t)(10000 + channels * (dwell + 150) + transmission);
}

static void analyzer_task(void *argument)
{
    wa_session_t *session = argument;
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool owned = false, remote_may_scan = false;
    uint64_t claim_deadline = milliseconds() + 3000;
    while (!cancelled(session) && s_host.connected(session->tab) && milliseconds() < claim_deadline) {
        if (s_host.claim(session->tab)) { owned = true; break; }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    if (!owned) {
        message(session, cancelled(session) ? "Stopped." : "Transport busy or disconnected. Stop the current operation and retry.", true, false);
        goto done;
    }
    s_host.flush(session->tab);
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    wa_reader_reset_connection(&session->reader);
    xSemaphoreGive(session->mutex);
    message(session, "Checking JanOS analyzer support...", session->committed.valid, false);
    if (!write_line(session, "wifi_analyzer caps\r\n")) {
        message(session, "Cannot send capabilities request. Check the connection.", true, false);
        goto done;
    }
    uint64_t deadline = milliseconds() + WA_PROBE_MS;
    while (!session->reader.caps_ready && !cancelled(session) && milliseconds() < deadline) {
        if (!pump(session)) goto disconnected;
    }
    if (cancelled(session)) { message(session, "Stopped.", true, false); goto done; }
    if (!session->reader.caps_ready) {
        message(session, "WFA/1 unavailable. Update JanOS; the legacy scanner remains available.", true, false);
        goto done;
    }
    if (session->request.limit > session->reader.max_records) {
        message(session, "Requested AP limit exceeds this JanOS capability.", true, false);
        goto done;
    }

    do {
        char command[256];
        static const char *const profiles[] = {"quick", "detailed", "passive"};
        const char *band = session->request.band == WA_BAND_24 ? "2.4" :
                           session->request.band == WA_BAND_5 ? "5" : "both";
        int n = snprintf(command, sizeof(command),
            "wifi_analyzer scan --band %s --profile %s --limit %u%s%s\r\n", band,
            profiles[session->request.profile], (unsigned)session->request.limit,
            session->request.channels[0] ? " --channels " : "", session->request.channels);
        if (n <= 0 || (size_t)n >= sizeof(command)) {
            message(session, "Scan command is too long.", true, false); break;
        }
        uint32_t begins = session->reader.begin_count;
        uint32_t terminals = session->reader.terminal_count;
        uint32_t errors = session->reader.error_count;
        uint32_t controls = session->reader.control_count;
        message(session, "Scanning requested channels...", session->committed.valid, false);
        if (cancelled(session)) break;
        remote_may_scan = true; /* Even a partial write needs cooperative cleanup. */
        if (!write_line(session, command)) {
            abort_transaction(session, "Scan command write failed."); break;
        }
        deadline = milliseconds() + scan_budget(&session->request,
                                     s_host.baud ? s_host.baud(session->tab) : 115200);
        bool rejected = false;
        while (!cancelled(session) && milliseconds() < deadline &&
               session->reader.terminal_count == terminals && session->reader.error_count == errors) {
            if (!pump(session)) goto disconnected;
            if (session->reader.control_count != controls &&
                !strcmp(session->reader.control_type, "error") && session->reader.begin_count == begins) {
                char detail[160];
                snprintf(detail, sizeof(detail), "JanOS: %.40s (%.90s)",
                         session->reader.control_code, session->reader.control_reason);
                message(session, detail, true, false);
                rejected = true;
                /* A rejected request can mean a previous analyzer scan still
                 * owns JanOS after reconnect. Its output must not leak into
                 * legacy readers when this worker releases the port. Status
                 * is read-only; analyzer stop never cancels legacy operations. */
                if (!query_idle(session, 3000)) recover_idle(session);
                if (!s_host.connected(session->tab)) goto disconnected;
                remote_may_scan = false;
                break;
            }
        }
        if (rejected) break;
        if (cancelled(session)) { abort_transaction(session, "Scan cancelled; keeping the last complete result."); break; }
        if (session->reader.error_count != errors) {
            message(session, "Invalid/incomplete WFA/1 data; keeping the last complete result.", true, false); break;
        }
        if (session->reader.terminal_count == terminals) {
            abort_transaction(session, "Scan timed out; keeping the last complete result."); break;
        }
        bool success = !strcmp(session->reader.last_terminal_status, "ok");
        /* end is written before JanOS releases ownership. Confirm idle before
         * scheduling a repeat or handing the port back to legacy consumers. */
        vTaskDelay(pdMS_TO_TICKS(120));
        if (!query_idle(session, 3000)) recover_idle(session);
        if (!s_host.connected(session->tab)) goto disconnected;
        remote_may_scan = false;
        if (!session->reader.caps_ready) {
            message(session, "JanOS restarted. Previous result is stale; Scan to reconnect.", true, false);
            break;
        }
        if (!success) {
            char detail[160];
            snprintf(detail, sizeof(detail), "Scan %.12s: %.95s", session->reader.last_terminal_status, session->reader.error);
            message(session, detail, true, false); break;
        }
        char detail[160];
        snprintf(detail, sizeof(detail), "%u APs returned / %u found%s. Width is SDK-derived.",
                 (unsigned)session->committed.count, (unsigned)session->committed.found,
                 session->committed.truncated ? " (truncated)" : "");
        message(session, detail, false, false);
        if (!session->request.repeat_ms || cancelled(session)) break;
        deadline = milliseconds() + session->request.repeat_ms;
        while (!cancelled(session) && milliseconds() < deadline) {
            if (!s_host.connected(session->tab)) goto disconnected;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    } while (!cancelled(session));
    if (remote_may_scan) {
        recover_idle(session);
        remote_may_scan = false;
    }
    if (!s_host.connected(session->tab)) goto disconnected;
    if (cancelled(session)) message(session, "Stopped. Last complete result retained.", session->reader.stale, false);
    goto done;

disconnected:
    abort_transaction(session, "Disconnected. Last complete result is stale; reconnect and Scan.");
    xSemaphoreTake(session->mutex, portMAX_DELAY);
    wa_reader_reset_connection(&session->reader);
    xSemaphoreGive(session->mutex);
done:
    if (owned) s_host.release(session->tab);
    /* No session/reader access after publication: a new worker may start now. */
    atomic_store(&session->running, false);
    vTaskDelete(NULL);
}

bool wa_start(wa_session_t *session, const wa_scan_request_t *request)
{
    if (!session || atomic_load(&session->running)) return false;
    if (!validate_request(request)) {
        message(session, "Invalid scan settings. Check band, channels and AP limit.", true, false);
        return false;
    }
    if (!s_host.claim || !s_host.release || !s_host.connected || !s_host.read || !s_host.write || !s_host.flush ||
        (s_host.available && !s_host.available(session->tab))) {
        message(session, "Transport busy or disconnected. Stop the current operation first.", true, false);
        return false;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&session->running, &expected, true)) return false;
    session->request = *request;
    atomic_store(&session->cancel, false);
    message(session, "Acquiring transport...", session->committed.valid, false);
    TaskHandle_t worker = NULL;
    if (xTaskCreate(analyzer_task, "wifi_analyzer", WA_WORKER_STACK, session, 4, &worker) != pdPASS) {
        message(session, "Not enough internal memory to start the analyzer task.", true, false);
        atomic_store(&session->running, false);
        return false;
    }
    xTaskNotifyGive(worker);
    return true;
}
