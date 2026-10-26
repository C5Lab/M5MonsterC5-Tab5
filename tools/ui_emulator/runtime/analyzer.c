/* Cooperative WFA/1 transport for the production Wi-Fi Analyzer LVGL screen.
 * The browser model owns synthetic radio jobs; no UART or device code runs. */
struct wa_session {
    int tab, job, last_state;
    bool running, stop_pending;
    double repeat_at, committed_at;
    wa_scan_request_t request;
    wa_reader_t reader;
    wa_snapshot_t working, committed;
    wa_status_t status;
};

static wa_session_t *app_analyzer_sessions[3];
EM_JS(int, app_analyzer_start_job,
      (int tab, int band, int profile, int limit, const char *channels), {
  try {
    const request = {band: band === 1 ? '2.4' : band === 2 ? '5' : 'both',
      profile: ['quick', 'detailed', 'passive'][profile], limit, channels: UTF8ToString(channels)};
    return emulatorDevice.device.analyzerStart(emulatorDevice.module(tab), request);
  } catch (error) {
    dispatchEvent(new CustomEvent('emulator-model-error', {detail: error.message}));
    return 0;
  }
});
EM_JS(int, app_analyzer_job_state, (int id), {return emulatorDevice.state(id);});
EM_JS(int, app_analyzer_job_line_count, (int id), {
  const job = emulatorDevice.device.job(id);
  return job && job.result && job.result.lines ? job.result.lines.length : 0;
});
EM_JS(int, app_analyzer_job_line, (int id, int index, char *out, int size), {
  const job = emulatorDevice.device.job(id);
  const record = job && job.result && job.result.lines && job.result.lines[index];
  if (typeof record !== 'string' || lengthBytesUTF8(record) + 1 > size) return 0;
  stringToUTF8(record, out, size);
  return lengthBytesUTF8(record);
});
EM_JS(int, app_analyzer_connected, (int tab), {
  return emulatorDevice.snapshot(tab).connected ? 1 : 0;
});
EM_JS(void, app_analyzer_cancel_job, (int id), {emulatorDevice.cancel(id);});

static void app_analyzer_message(wa_session_t *session, const char *message)
{
    snprintf(session->status.message, sizeof(session->status.message), "%s", message);
    session->status.stale = session->reader.stale;
    session->status.capable = session->reader.caps_ready;
    ++session->status.revision;
}

wa_session_t *wa_session_get(int tab)
{
    if (tab < 0 || tab >= 3) return NULL;
    wa_session_t *session = app_analyzer_sessions[tab];
    if (session) return session;
    session = calloc(1, sizeof(*session));
    if (!session) return NULL;
    session->tab = tab;
    wa_reader_init(&session->reader, &session->working, &session->committed);
    app_analyzer_message(session, "Ready. Scan to explore synthetic access points.");
    app_analyzer_sessions[tab] = session;
    return session;
}

bool wa_busy_tab(int tab)
{
    return tab >= 0 && tab < 3 && app_analyzer_sessions[tab] && app_analyzer_sessions[tab]->running;
}

bool wa_copy_view(wa_session_t *session, wa_snapshot_t *snapshot, wa_status_t *status)
{
    if (!session || !snapshot || !status) return false;
    *snapshot = session->committed;
    *status = session->status;
    status->running = session->running;
    status->capable = session->reader.caps_ready;
    status->stale = session->reader.stale;
    status->age_ms = snapshot->valid ? (uint64_t)(app_device_ms - session->committed_at) : 0;
    return true;
}

bool wa_start(wa_session_t *session, const wa_scan_request_t *request)
{
    if (!session || !request || session->running || request->band > WA_BAND_5 ||
        request->profile > 2 || request->limit < 1 || request->limit > WA_MAX_APS ||
        !memchr(request->channels, 0, sizeof(request->channels))) return false;
    if (!app_analyzer_connected(session->tab)) {
        wa_reader_abort(&session->reader, "Simulated module disconnected");
        wa_reader_reset_connection(&session->reader);
        app_analyzer_message(session, "Simulated module disconnected. Last result is stale; reconnect and Scan.");
        return false;
    }
    int job = app_analyzer_start_job(session->tab, request->band, request->profile,
                                     request->limit, request->channels);
    if (!job) {
        app_analyzer_message(session, "Cannot start acquisition. Check settings or radio ownership.");
        return false;
    }
    session->request = *request;
    session->job = job;
    session->last_state = 1;
    session->running = true;
    session->stop_pending = false;
    session->repeat_at = 0;
    app_analyzer_message(session, "Scanning synthetic WFA/1 networks...");
    return true;
}

void wa_stop(wa_session_t *session)
{
    if (!session || !session->running) return;
    session->stop_pending = true;
    if (session->job) app_analyzer_cancel_job(session->job);
    app_analyzer_message(session, "Stopping synthetic acquisition...");
}

void wa_leave(int tab)
{
    if (tab >= 0 && tab < 3) wa_stop(app_analyzer_sessions[tab]);
}

static void app_analyzer_finish(wa_session_t *session, int state)
{
    session->job = 0;
    session->running = false;
    session->stop_pending = false;
    session->repeat_at = 0;
    session->last_state = state;
    app_analyzer_message(session, state == 3 ? "Stopped. Last complete result retained." :
        "Synthetic acquisition failed. Last complete result retained.");
}

static void app_analyzer_tick(void)
{
    for (int tab = 0; tab < 3; ++tab) {
        wa_session_t *session = app_analyzer_sessions[tab];
        if (!session || !session->running) continue;
        if (!app_analyzer_connected(tab)) {
            if (session->job) app_analyzer_cancel_job(session->job);
            wa_reader_abort(&session->reader, "Simulated module disconnected");
            wa_reader_reset_connection(&session->reader);
            app_analyzer_finish(session, 4);
            app_analyzer_message(session, "Simulated module disconnected. Last result is stale; reconnect and Scan.");
            continue;
        }
        if (session->stop_pending) {
            app_analyzer_finish(session, 3);
            continue;
        }
        if (!session->job) {
            if (app_device_ms < session->repeat_at) continue;
            int job = app_analyzer_start_job(tab, session->request.band, session->request.profile,
                                              session->request.limit, session->request.channels);
            if (!job) { app_analyzer_finish(session, 4); continue; }
            session->job = job;
            session->last_state = 1;
            app_analyzer_message(session, "Scanning synthetic WFA/1 networks...");
            continue;
        }
        int state = app_analyzer_job_state(session->job);
        if (state == 1) continue;
        if (state != 2) {
            app_analyzer_finish(session, state == 3 ? 3 : 4);
            continue;
        }
        uint32_t before = session->reader.commit_count;
        int count = app_analyzer_job_line_count(session->job);
        for (int i = 0; i < count; ++i) {
            char record[WA_MAX_LINE_BYTES + 2];
            int size = app_analyzer_job_line(session->job, i, record, sizeof(record));
            if (size <= 0) break;
            wa_reader_feed(&session->reader, record, (size_t)size);
        }
        if (session->reader.commit_count == before) {
            wa_reader_abort(&session->reader, "Invalid synthetic WFA/1 transaction");
            app_analyzer_finish(session, 4);
            continue;
        }
        session->committed_at = app_device_ms;
        session->last_state = 2;
        char message[160];
        snprintf(message, sizeof(message), "%u APs returned / %u found. Width is SDK-derived (simulated).",
                 session->committed.count, session->committed.found);
        app_analyzer_message(session, message);
        session->job = 0;
        if (session->request.repeat_ms) session->repeat_at = app_device_ms + session->request.repeat_ms;
        else session->running = false;
    }
}

EMSCRIPTEN_KEEPALIVE int emu_analyzer_state(int tab)
{
    if (tab < 0 || tab >= 3 || !app_analyzer_sessions[tab]) return 0;
    return app_analyzer_sessions[tab]->running ? 1 : app_analyzer_sessions[tab]->last_state;
}

EMSCRIPTEN_KEEPALIVE unsigned emu_analyzer_scan(int tab)
{
    if (tab < 0 || tab >= 3 || !app_analyzer_sessions[tab]) return 0;
    return app_analyzer_sessions[tab]->committed.valid ? app_analyzer_sessions[tab]->committed.scan : 0;
}
