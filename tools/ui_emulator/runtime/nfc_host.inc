/* Native NFC pages consume JanOS-style replies from a session-only model. */
#include "nfc_host.h"

EM_JS(void, nfc_emulator_send, (int tab, const char *command), {
  try { emulatorDevice.nfc.command(tab, UTF8ToString(command)); }
  catch (error) { dispatchEvent(new CustomEvent('emulator-model-error', {detail: error.message})); }
});
EM_JS(void, nfc_emulator_flush, (int tab), { emulatorDevice.nfc.flush(tab); });
EM_JS(int, nfc_emulator_read, (int tab, void *buffer, size_t size), {
  const line = emulatorDevice.nfc.read(tab);
  if (!line) return 0;
  const reply = line + '\n';
  stringToUTF8(reply, buffer, size);
  return Math.min(lengthBytesUTF8(reply), size - 1);
});

nfc_tab_state_t *nfc_host_state_for_tab(int tab)
{
    if (tab < 0 || tab > 3 || tab == TAB_INTERNAL) return NULL;
    tab_context_t *ctx = get_ctx_for_tab((tab_id_t)tab);
    if (!ctx) return NULL;
    if (!ctx->nfc) {
        ctx->nfc = nfc_host_alloc_state();
        if (ctx->nfc) ctx->nfc->tab_id = tab;
    }
    return ctx->nfc;
}

nfc_tab_state_t *nfc_host_state(void)
{
    return nfc_host_state_for_tab((int)current_tab);
}
