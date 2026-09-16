"""Create an explicit dependency review queue; never generate no-op stubs."""
import json
import re
import argparse
import hashlib
from source_index import ROOT
x=json.loads((ROOT/'tools/ui_emulator/coverage.json').read_text(encoding='utf-8'))
local={f['name'] for f in x['functions'].values()}
symbols={}
for fid,f in x['functions'].items():
    for c in f['calls']:
        if c['name'] not in local:
            symbols.setdefault(c['name'],[]).append({'function':fid,'line':c['line']})
def policy(n):
    exact={
      'esp_err_to_name':('adapt_error_names','Map ESP error values used by retained code to their SDK names; preserve Unknown for unrecognized values.'),
      'esp_ip4addr_aton':('retain_ipv4_parser','Retain IPv4 parsing semantics and return success/failure; this helper does not access a network.'),
      'esp_log_level_set':('adapt_logging','Set the virtual logger filter without changing scenario state.'),
      'esp_rom_crc32_le':('retain_crc32','Use the SDK-compatible seeded little-endian CRC32 algorithm; never substitute a constant checksum.'),
      'janos_file_transfer_state_name':('retain_transfer_labels','Compile the existing pure enum-to-label function from janos_file_transfer.c.'),
      'janos_file_transfer_query_size':('adapt_http_file_query','Query virtual JanOS file metadata; preserve not-found, invalid path, timeout and HTTP error outcomes; write size_out only on success.'),
      'janos_file_transfer_download':('adapt_http_file_download','A resumable job implements the existing config/result/progress contract: QUERYING, DOWNLOADING, VERIFYING, COMMITTING then DONE/CANCELLED/ERROR; validate bytes and CRC; atomic .part rename only on success; respect cancel_requested and callback context.'),
      'action':('retain_bound_callback','sd_warning_continue_cb captures sd_warning_pending_action before popup cleanup, invokes it only if non-null, then resets the acknowledgement flag. Preserve this ordering.'),
      'cb':('retain_bound_callback','Keep the callback selected by original popup construction; hidden SSID copies the text and callback before clearing/closing the popup. SubGHz popup callers are deferred.'),
      'on_line':('retain_bound_callback','gitm_read_lines dispatches complete lines to its supplied status/event/start/connect/stop parser and stops when that parser returns true; preserve user context and partial-line buffering.'),
      'on_cancel':('deferred_subghz_callback','Only recorded SubGHz popup callers use this symbol; do not include these constructors in the current profile.'),
      'pthread_internal_local_storage_destructor_callback':('exclude_esp_tls_branch','Disable the guarded ESP PSRAM/TLS deletion branch in the browser target; scheduler job cleanup must still run exactly once before clearing the task handle.')
    }
    if n in exact:return exact[n]
    if n.startswith('lv_'):return 'retain_lvgl','Compile repository LVGL; keep event/timer behavior.'
    if n.startswith('pcap_'):return 'retain_component','Compile production PCAP component over virtual files; inspect platform dependencies.'
    if n.startswith('janos_file_transfer_'):return 'review_transfer_boundary','Retain parsing/state helpers; adapt actual transport and virtual file IO.'
    if re.match(r'^(xTask|vTask|xTimer|vTimer|pvTimer|xSemaphore|vSemaphore|xEventGroup|vEventGroup|portENTER|portEXIT|pdMS|esp_timer)',n):return 'adapt_scheduler','Explicit bounded jobs, clock and synchronization semantics; no dummy successful task creation.'
    if n.startswith('nvs_'):return 'adapt_settings','Versioned virtual settings; return real simulated errors.'
    if re.match(r'^(esp_wifi|esp_netif|esp_hosted|esp_event|httpd_|socket$|bind$|recvfrom$|sendto$|setsockopt$)',n):return 'adapt_network','Local scenario events and simulated server outcomes; no real radio/backend request.'
    if re.match(r'^(bsp_|uart_|usb_|usbh_|i2c_|ledc_|codec->)',n):return 'adapt_hardware','Per-device adapter; preserve expected read/write/availability behavior.'
    if n.startswith('heap_caps_'):return 'adapt_allocator','Map allocations to host memory; retain allocation-failure semantics.'
    if n in {'fopen','fclose','fread','fwrite','fseek','ftell','ferror','fflush','fgets','fileno','fsync','setvbuf','opendir','readdir','closedir','stat','mkdir','rename','unlink','access','close'} or n.startswith('esp_vfs_'):return 'adapt_filesystem','Virtual filesystem; preserve partial IO, size, seek and missing-file errors.'
    if n in {'time','localtime_r','mktime','settimeofday','esp_random','esp_reset_reason','esp_restart'}:return 'adapt_lifecycle','Deterministic scenario time/randomness or simulated reset; never host reboot.'
    if n.startswith(('ESP_LOG','ESP_ERROR','ESP_NETIF_','WIFI_INIT_','HTTPD_DEFAULT','USB_EP_')) or n in {'GITM_LOG_RX','IP2STR','LV_PCT','S_ISDIR','S_ISREG','va_start','va_end'} or n.startswith('('):return 'retain_macro_or_expression','Preprocessor/cast expression; validate against actual compiler, not a callable adapter.'
    if n in {'cb','action','on_cancel','on_line','pthread_internal_local_storage_destructor_callback'}:return 'resolve_indirect_call','Resolve actual callback target and lifetime before browser integration.'
    if n.startswith('esp_'):return 'review_platform_helper','Inspect portability; CRC must retain exact algorithm, logging/error helpers may be lightweight adapters.'
    if re.match(r'^(str|mem|is|to|at|snprintf$|sscanf$|vsnprintf$|fprintf$|malloc$|calloc$|free$|qsort$|abs$|fabs|cos$|sin|sqrt$|powf$|roundf$|lroundf$|htons$)',n):return 'retain_libc','Use equivalent standard-library behavior in Emscripten.'
    return 'unclassified_review_required','No default stub is permitted.'
registry=[{'symbol':n,'policy':policy(n)[0],'contract':policy(n)[1],'call_sites':sites,'status':'specified-for-implementation','allow_default_noop':False} for n,sites in sorted(symbols.items())]
assert not any(r['policy'].startswith(('review_','resolve_','unclassified')) for r in registry), 'Unresolved dependency policy'
ap=argparse.ArgumentParser();ap.add_argument('--enroll',action='store_true');args=ap.parse_args()
decision_path=ROOT/'tools/ui_emulator/adapter-decisions.json'
support_paths=['components/janos_file_transfer/janos_file_transfer.c','components/janos_file_transfer/include/janos_file_transfer.h']
decisions={'source_hashes':{**x['sources'],**{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in support_paths}},'symbols':{r['symbol']:{'policy':r['policy'],'contract':r['contract']} for r in registry}}
if args.enroll:
    decision_path.write_text(json.dumps(decisions,indent=2)+'\n',encoding='utf-8')
else:
    assert decision_path.exists(), 'Explicit initial policy enrollment required'
    assert json.loads(decision_path.read_text(encoding='utf-8'))==decisions, 'Policy/source drift: explicit adapter review required'
(ROOT/'tools/ui_emulator/dependency-registry.json').write_text(json.dumps({'status':'phase-1-explicit-adapter-contracts-not-implemented','sources':x['sources'],'dependencies':registry},indent=2)+'\n',encoding='utf-8')
print(f'{len(registry)} external symbols recorded; unclassified: '+', '.join(r['symbol'] for r in registry if r['policy']=='unclassified_review_required'))
