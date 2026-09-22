"""Source contract for the first WPA PSK Auditor dashboard slice.

The pure session/history suites cover persistence.  This contract protects the
LVGL integration: the INTERNAL tile must open a dashboard that reads those
same stores and resumes through the existing crack launcher.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def production_function(name: str) -> str:
    match = re.search(rf"static\s+[^\n]+?\b{name}\s*\([^;]+?\n\{{", SOURCE, re.S)
    assert match, f"missing function {name}"
    start = match.start()
    depth = 0
    opened = False
    for index in range(start, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
            opened = True
        elif SOURCE[index] == "}":
            depth -= 1
            if opened and depth == 0:
                return SOURCE[start : index + 1]
    raise AssertionError(f"unterminated function {name}")


def test_internal_menu_exposes_the_auditor_tile():
    tiles = production_function("show_internal_tiles")
    assert '"WPA PSK\\nAuditor"' in tiles
    assert '"WPA PSK Auditor"' in tiles
    handler = production_function("internal_tile_event_cb")
    assert 'strcmp(tile_name, "WPA PSK Auditor")' in handler
    assert "show_wpa_psk_auditor_page();" in handler


def test_dashboard_uses_the_durable_session_and_history_stores():
    page = production_function("show_wpa_psk_auditor_page")
    assert "hs_session_catalog_list(" in page
    assert "HS_CRACK_SESSION_A" in page
    assert "HS_CRACK_SESSION_B" in page
    assert "hs_audit_history_list(" in page
    for copy in ("Active audit", "Resumable audit", "Recent history", "Capture catalog"):
        assert copy in page


def test_runtime_persists_dashboard_metrics_and_history():
    metrics = production_function("hs_crack_session_update_metrics")
    for token in ("active_time_ms", "last_rate_milli_per_second", "last_eta_seconds"):
        assert token in metrics
    history = production_function("hs_crack_history_append_session")
    assert "hs_audit_history_from_session(" in history
    assert "hs_audit_history_append(" in history
    task = production_function("hs_crack_task")
    cancel = task.index("if (hs_crack_ui.cancel_requested && crack_session_active)")
    checkpoint = task.index("hs_crack_session_checkpoint(", cancel)
    note = task.rfind("hs_crack_session_update_metrics(", cancel, checkpoint)
    assert note >= cancel
    append = task.index("hs_crack_history_append_session(", checkpoint)
    tombstone = task.index("hs_session_catalog_tombstone(", checkpoint)
    assert checkpoint < append < tombstone


def test_resume_reuses_the_existing_crack_launcher():
    launch = production_function("wpa_auditor_launch_saved")
    assert "session_index" in launch
    assert "wpa_auditor_sessions[session_index]" in launch
    assert "HS_SESSION_ACTIVE" in launch
    assert "hs_crack_ui.active" in launch
    assert "hs_crack_ui.task" in launch
    assert "origin.remote_path" in launch
    assert "hs_crack_start_file(" in launch
    assert "wpa_auditor_apply_saved_config(" in launch
    assert "hs_crack_ui.force_rerun = start_over" in launch
    assert "hs_crack_ui.resume_session_valid = true" in launch


def test_dashboard_lists_multiple_resumable_sessions_with_per_row_actions():
    page = production_function("show_wpa_psk_auditor_page")
    assert "WPA_AUDITOR_RESUME_ROWS" in page
    assert "wpa_auditor_sessions" in page
    assert "wpa_auditor_session_count" in page
    assert "for (size_t i = 0; i < wpa_auditor_session_count; ++i)" in page
    assert "wpa_auditor_resume_cb" in page
    assert "wpa_auditor_start_over_request_cb" in page


def test_dashboard_bounds_capture_and_wordlist_identity_formatting():
    page = production_function("show_wpa_psk_auditor_page")
    assert '"%.150s\\nWordlist  %.150s"' in page


def test_resume_restores_the_saved_wordlist_and_sync_controls():
    restore = production_function("wpa_auditor_apply_saved_config")
    assert "session->wordlists[0].path" in restore
    assert "hs_crack_cache_wordlist_id(" in restore
    assert "head_crc32" in restore
    assert "tail_crc32" in restore
    assert "lv_dropdown_set_selected(" in restore
    assert "wpa_auditor_resume_source_changed_cb" in restore
    assert "session->config.sync_capture" in restore
    assert "session->config.sync_wordlists" in restore
    assert "Resume settings restored" in restore


def test_resume_does_not_silently_replace_a_missing_or_changed_wordlist():
    restore = production_function("wpa_auditor_apply_saved_config")
    assert "Saved wordlist is missing or changed" in restore
    assert "LV_STATE_DISABLED" in restore
    assert "lv_dropdown_set_selected(hs_crack_ui.wordlist_dropdown, 0)" in restore
    assert restore.index("session->config.sync_capture") < restore.index("if (!valid)")
    assert restore.index("session->config.sync_wordlists") < restore.index("if (!valid)")
    launch = production_function("wpa_auditor_launch_saved")
    assert "!wpa_auditor_apply_saved_config" in launch
    assert "return false" in launch
    replacement = production_function("wpa_auditor_resume_source_changed_cb")
    assert "hs_crack_ui.force_rerun = true" in replacement
    assert "this starts a new audit" in replacement


def test_legacy_flow_and_auditor_share_one_launch_and_preflight_path():
    legacy = production_function("hs_crack_file_cb")
    assert "hs_crack_start_file(" in legacy
    assert "wpa_auditor_apply_saved_config" not in legacy
    launcher = production_function("hs_crack_start_file")
    assert "hs_crack_ui.method = HS_CRACK_METHOD_GENERIC" in launcher
    popup = production_function("hs_crack_show_popup")
    assert "lv_dropdown_set_selected(hs_crack_ui.wordlist_dropdown, 1)" in popup
    task = production_function("hs_crack_task")
    analyze = task.index("hs_capture_analyze_pcap(")
    session = task.index("hs_crack_session_begin(")
    assert analyze < session


def test_start_over_requires_an_explicit_confirmation():
    request = production_function("wpa_auditor_start_over_request_cb")
    assert "Discard saved progress?" in request
    assert "wpa_auditor_start_over_confirm_cb" in request
    confirm = production_function("wpa_auditor_start_over_confirm_cb")
    assert "wpa_auditor_pending_session" in confirm
    assert "wpa_auditor_launch_saved(session_index, true)" in confirm


def test_catalog_action_starts_one_merged_inventory_scan():
    browse = production_function("wpa_auditor_open_captures_cb")
    assert "wpa_auditor_catalog_request_refresh" in browse
    assert "show_handshakes_page();" not in browse
    request = production_function("wpa_auditor_catalog_request_refresh")
    assert "wpa_auditor_catalog_task" in request
    assert "xTaskCreate" in request


def test_catalog_discovers_local_and_remote_sources_serially():
    task = production_function("wpa_auditor_catalog_task")
    local = task.index("wpa_auditor_catalog_scan_local")
    remote = task.index("wpa_auditor_catalog_scan_remote", local)
    assert local < remote
    assert "TAB_GROVE" in task
    assert "TAB_USB" in task
    assert "TAB_MBUS" in task
    remote_scan = production_function("wpa_auditor_catalog_scan_remote")
    assert "compromised_transport_lock_begin(" in remote_scan
    assert "compromised_transport_lock_end(" in remote_scan
    assert '"artifact_inventory capabilities"' in remote_scan
    assert "wpa_auditor_catalog_scan_artifact" in remote_scan
    assert "wpa_auditor_catalog_scan_legacy" in remote_scan


def test_catalog_uses_correlated_artifact_pages_and_bounded_merge():
    page = production_function("wpa_auditor_catalog_scan_artifact")
    assert '"artifact_inventory list %s handshakes %lu %u"' in page
    assert "hs_artifact_request_accept(" in page
    assert "HS_ARTIFACT_CATALOG_MAX_ASSETS" in page
    assert "wpa_auditor_catalog_add_location(" in page
    merge = production_function("wpa_auditor_catalog_add_location")
    assert "hs_artifact_catalog_add(" in merge
    fallback = production_function("wpa_auditor_catalog_scan_legacy")
    assert '"list_dir /sdcard/lab/handshakes -s"' in fallback
    assert "hs_artifact_parse_legacy_sized_row(" in fallback


def test_catalog_renders_source_badges_and_actions():
    render = production_function("wpa_auditor_catalog_render_locked")
    assert "wpa_auditor_catalog_source_name(" in render
    assert '"Audit"' in render
    assert '"Sync to Tab5"' in render
    assert "wpa_auditor_catalog_audit_cb" in render
    assert "wpa_auditor_catalog_sync_cb" in render
    assert "!wpa_auditor_catalog_local_equivalent(location)" in render


def test_catalog_sync_all_queues_only_missing_remote_assets():
    page = production_function("show_wpa_psk_auditor_page")
    assert '"Sync all to Tab5"' in page
    assert "wpa_auditor_catalog_sync_all_cb" in page
    build = production_function("wpa_auditor_catalog_build_sync_queue")
    assert "wpa_auditor_catalog_local_equivalent" in build
    assert "HS_ARTIFACT_VALIDATION_INVALID" in build
    assert "wpa_auditor_catalog_remote_path" in build
    task = production_function("wpa_auditor_catalog_sync_all_task")
    assert "for (size_t i = 0; i < args->item_count; ++i)" in task
    assert "wpa_auditor_sync_item_find_indexed(" in task
    assert "wpa_auditor_catalog_copy_item(" in task
    assert "Copied: %u | Already local: %u | Duplicates: %u | Failed: %u" in task
    assert "wpa_auditor_catalog_request_refresh" in task


def test_sync_all_coalesces_duplicate_workers_and_indexes_every_source():
    local = production_function("wpa_auditor_catalog_local_equivalent")
    assert "hs_artifact_locations_same_content(" in local
    build = production_function("wpa_auditor_catalog_build_sync_queue")
    assert "hs_artifact_locations_same_content(" in build
    assert "wpa_auditor_sync_item_add_source(" in build
    task = production_function("wpa_auditor_catalog_sync_all_task")
    assert "wpa_auditor_sync_item_index_sources(" in task
    assert "item->source_count" in task
    assert "Duplicates: %u" in task


def test_batch_transfer_detail_is_bounded_for_strict_format_builds():
    progress = production_function("compromised_transfer_progress_cb")
    assert 'snprintf(detail, sizeof(detail), "%.255s", batch_detail)' in progress


def test_sources_are_a_compact_dropdown_and_filter_catalog_actions():
    page = production_function("show_wpa_psk_auditor_page")
    assert "wpa_auditor_source_dropdown" in page
    assert "wpa_auditor_source_dropdown_options" in page
    assert "wpa_auditor_source_filter_cb" in page
    assert '"Sources"' not in page
    render = production_function("wpa_auditor_catalog_render_locked")
    assert "wpa_auditor_catalog_source_selected" in render
    queue = production_function("wpa_auditor_catalog_build_sync_queue")
    assert "wpa_auditor_catalog_source_selected" in queue
    callback = production_function("wpa_auditor_source_filter_cb")
    assert "lv_dropdown_get_selected" in callback
    assert "wpa_auditor_catalog_render_locked" in callback
    assert '"Sync source to Tab5"' in callback


def test_catalog_is_collapsed_until_a_worker_is_selected():
    options = production_function("wpa_auditor_source_dropdown_options")
    assert "Workers (%u) - tap to browse" in options
    assert "\\nAll sources" in options
    assert "wpa_auditor_source_filter = -2" in options
    selected = production_function("wpa_auditor_catalog_source_selected")
    assert "wpa_auditor_source_filter != -2" in selected
    render = production_function("wpa_auditor_catalog_render_locked")
    assert '"Select a worker above to show its captures."' in render
    callback = production_function("wpa_auditor_source_filter_cb")
    assert "selected == 0U" in callback
    assert "selected == 1U" in callback


def test_inventory_retains_remote_crc_for_exact_duplicate_merging():
    scan = production_function("wpa_auditor_catalog_scan_artifact")
    assert "location->crc32_known = message.crc32_known" in scan
    assert "location->crc32 = message.crc32" in scan


def test_dashboard_has_explicit_empty_and_unavailable_states():
    page = production_function("show_wpa_psk_auditor_page")
    assert "No resumable audit" in page
    assert "No audit history yet" in page
    assert "No storage source is available" in SOURCE


def test_home_metadata_and_file_listing_share_transport_ownership():
    owner = production_function("compromised_transport_lock_begin")
    assert "xSemaphoreTake" in owner
    assert "transport_console_mutex" in owner
    home = production_function("home_read_remote_meta")
    listing = production_function("compromised_load_handshake_files")
    for function in (home, listing):
        assert "compromised_transport_lock_begin(" in function
        assert "compromised_transport_lock_end(" in function


if __name__ == "__main__":
    tests = [value for name, value in globals().copy().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"test_wpa_psk_auditor_contract: PASS ({len(tests)} tests)")
