/* Cooperative Pixie UART worker. The production page and pure parser run in
 * Wasm; the command stream comes only from the deterministic browser model. */
static void pixie_uart_task(void *arg) { (void)arg; /* Intercepted in xTaskCreate. */ }

EM_JS(int, app_pixie_command_js, (int tab, const char *command), {
  try {
    emulatorDevice.device.wpsCommand(emulatorDevice.module(tab), UTF8ToString(command));
    return 1;
  } catch (error) {
    dispatchEvent(new CustomEvent('emulator-model-error', {detail: error.message}));
    return 0;
  }
});

EM_JS(int, app_pixie_read_js, (int tab, char *out, int capacity), {
  const line=emulatorDevice.device.wpsRead(emulatorDevice.module(tab));
  if (line===null) return 0;
  if (lengthBytesUTF8(line)+1>capacity) return -1;
  stringToUTF8(line,out,capacity);
  return lengthBytesUTF8(line);
});

static void app_pixie_tick(void)
{
    if (!g_pixie.active || g_pixie.worker_done) return;
    pixie_session_t *session = &g_pixie.model;
    uint64_t now = (uint64_t)app_device_ms;
    pixie_outgoing_t outgoing = {0};
    pixie_command_t kind = pixie_session_take_command(session, &outgoing, now);
    if (kind != PIXIE_CMD_NONE) {
        bool sent = app_pixie_command_js(outgoing.tab, outgoing.text) != 0;
        /* A rejected start has a definite refusal, not an ambiguous transport
         * write. The production parser recognizes the same JanOS refusal line. */
        pixie_session_write_result(session, kind, sent || kind == PIXIE_CMD_START, now);
        if (!sent && kind == PIXIE_CMD_START) {
            const char rejected[] = "Command returned non-zero\n";
            pixie_session_feed(session, rejected, sizeof(rejected) - 1, now);
        }
    }
    for (int i = 0; i < 32; i++) {
        char line[512];
        int length = app_pixie_read_js(session->tab, line, sizeof(line));
        if (length <= 0) break;
        pixie_session_feed(session, line, (size_t)length, now);
        pixie_session_feed(session, "\n", 1, now);
    }
    if (session->close_ready) {
        g_pixie.worker_done = true;
        g_pixie.task = NULL;
    }
}
