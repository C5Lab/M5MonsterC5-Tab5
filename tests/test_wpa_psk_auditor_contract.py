"""Source contract for the first WPA PSK Auditor dashboard slice.

The pure session/history suites cover persistence.  This contract protects the
LVGL integration: the INTERNAL tile must open a dashboard that reads those
same stores and resumes through the existing crack launcher.
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
QUEUE_SOURCE = (ROOT / "main" / "hs_audit_queue.c").read_text(encoding="utf-8")


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
    loader = production_function("wpa_auditor_storage_task")
    assert "hs_session_catalog_list(" in loader
    assert "HS_CRACK_SESSION_A" in loader
    assert "HS_CRACK_SESSION_B" in loader
    assert "hs_audit_history_list(" in loader
    assert "wpa_auditor_storage_request(" in page
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
    loader = production_function("wpa_auditor_storage_task")
    assert "WPA_AUDITOR_RESUME_ROWS" in loader
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


def test_legacy_crack_dialog_can_copy_validate_and_enqueue_a_capture():
    popup = production_function("hs_crack_show_popup")
    assert '"Add to queue"' in popup
    assert "hs_crack_add_to_queue_cb" in popup

    enqueue = production_function("hs_crack_add_to_queue_cb")
    assert "hs_crack_cache_decode_source(" in enqueue
    assert "wpa_auditor_batch_configure_source_selection(" in enqueue
    assert "compromised_start_copy(" in enqueue
    assert "true" in enqueue
    configured = enqueue.index("wpa_auditor_batch_configure_source_selection(")
    saved = enqueue.index("wpa_auditor_batch_save()", configured)
    copied = enqueue.index("compromised_start_copy(", saved)
    assert configured < saved < copied

    transfer = production_function("compromised_transfer_task")
    assert "enqueue_after_copy" in transfer
    valid = transfer.index("validation_result == HS_CAPTURE_VALIDATION_OK")
    append = transfer.index("wpa_auditor_batch_append_local_capture(", valid)
    assert valid < append


def test_legacy_queue_action_uses_close_and_prevents_a_second_start():
    popup = production_function("hs_crack_show_popup")
    assert '"Start now"' in popup
    assert '"Close"' in popup

    enqueue = production_function("hs_crack_add_to_queue_cb")
    assert "hs_crack_mark_queued_ui()" in enqueue

    queued = production_function("hs_crack_mark_queued_ui")
    assert "hs_crack_ui.action_btn" in queued
    assert "LV_OBJ_FLAG_HIDDEN" in queued
    assert "hs_crack_ui.queue_btn" in queued
    assert "LV_STATE_DISABLED" in queued
    assert '"Already queued"' in queued


def test_closing_a_queued_legacy_dialog_refreshes_the_auditor_dashboard():
    queued = production_function("hs_crack_mark_queued_ui")
    assert "hs_crack_ui.queued_to_batch = true" in queued

    dismiss = production_function("hs_crack_dismiss_cb")
    assert "hs_crack_ui.queued_to_batch" in dismiss
    assert "lv_async_call(" in dismiss

    refresh = production_function(
        "hs_crack_refresh_auditor_after_queue_async"
    )
    assert "show_wpa_psk_auditor_page()" in refresh


def test_legacy_enqueue_respects_a_locked_batch_candidate_source():
    configure = production_function(
        "wpa_auditor_batch_configure_source_selection"
    )
    assert "hs_audit_queue_can_change_candidate_source(" in configure
    assert "wpa_auditor_batch_candidate_source_selection()" in configure
    assert "selected ==" in configure


def test_crack_worker_statuses_remain_reachable_in_landscape():
    popup = production_function("hs_crack_show_popup")
    assert "lv_obj_set_scroll_dir(hs_crack_ui.popup, LV_DIR_VER)" in popup
    assert "lv_obj_set_scrollbar_mode(hs_crack_ui.popup, LV_SCROLLBAR_MODE_AUTO)" in popup
    assert "if (!ui_wide_layout())" in popup
    grow = popup.index("lv_obj_set_flex_grow(hs_crack_ui.worker_box, 1)")
    landscape_guard = popup.rfind("if (!ui_wide_layout())", 0, grow)
    assert landscape_guard >= 0
    assert "lv_obj_add_flag(hs_crack_ui.worker_box, LV_OBJ_FLAG_SCROLL_CHAIN_VER)" in popup

    row = production_function("hs_crack_worker_row_create")
    assert "lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER)" in row


def test_start_over_requires_an_explicit_confirmation():
    request = production_function("wpa_auditor_start_over_request_cb")
    assert "Discard saved progress?" in request
    assert "wpa_auditor_start_over_confirm_cb" in request
    confirm = production_function("wpa_auditor_start_over_confirm_cb")
    assert "wpa_auditor_pending_session" in confirm
    assert "wpa_auditor_launch_saved(session_index, true)" in confirm


def test_resumable_audit_can_be_deleted_without_erasing_worker_caches():
    page = production_function("show_wpa_psk_auditor_page")
    assert '"Delete audit"' in page
    assert "wpa_auditor_delete_audit_request_cb" in page

    request = production_function("wpa_auditor_delete_audit_request_cb")
    assert '"Delete saved audit?"' in request
    assert "Capture and wordlist caches stay untouched." in request
    assert "wpa_auditor_delete_audit_confirm_cb" in request

    task = production_function("wpa_auditor_delete_audit_task")
    assert "hs_session_catalog_tombstone(" in task
    assert "hs_crack_remote_cancel_job_id(" in task
    assert "session.workers" in task
    assert '"crack_worker reset' not in task
    assert "unlink(" not in task


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


def test_catalog_renders_each_source_badge_only_once_per_asset():
    render = production_function("wpa_auditor_catalog_render_locked")
    assert "source_badge_shown" in render
    assert "!source_badge_shown[source_index]" in render
    assert "source_badge_shown[source_index] = true" in render


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


def test_sync_all_refreshes_sources_before_rebuilding_its_queue():
    callback = production_function("wpa_auditor_catalog_sync_all_cb")
    assert "wpa_auditor_sync_after_refresh = true" in callback
    assert "wpa_auditor_catalog_request_refresh()" in callback

    scan = production_function("wpa_auditor_catalog_task")
    assert "wpa_auditor_sync_after_refresh" in scan
    assert "lv_async_call(wpa_auditor_catalog_sync_after_refresh_async" in scan

    refreshed = production_function("wpa_auditor_catalog_sync_after_refresh_async")
    assert "wpa_auditor_catalog_start_sync_current()" in refreshed
    start = production_function("wpa_auditor_catalog_start_sync_current")
    assert "wpa_auditor_catalog_build_sync_queue()" in start
    assert "All matching remote captures are already on Tab5." in start

    enabled = production_function("wpa_auditor_catalog_sync_action_available")
    assert "wpa_auditor_catalog_syncable_count" not in enabled
    assert "grove_detected" in enabled


def test_repeat_sync_remains_available_after_every_known_capture_is_local():
    enabled = production_function("wpa_auditor_catalog_sync_action_available")
    assert "wpa_auditor_catalog->asset_count" not in enabled
    assert "wpa_auditor_catalog->assets" not in enabled
    assert "grove_detected || usb_detected || mbus_detected" in enabled

    scope = production_function("wpa_auditor_catalog_source_in_sync_scope")
    assert "location->source == HS_ARTIFACT_SOURCE_LOCAL" in scope
    assert "wpa_auditor_source_filter == HS_ARTIFACT_SOURCE_LOCAL" in scope

    callback = production_function("wpa_auditor_source_filter_cb")
    assert "global_sync" in callback
    assert "HS_ARTIFACT_SOURCE_LOCAL" in callback


def test_sync_completion_releases_busy_state_before_optional_ui_update():
    finish = production_function("compromised_transfer_finish_ui")
    assert "compromised_transfer_ui.active = false" in finish
    release = finish.index("compromised_transfer_ui.active = false")
    lock = finish.index("bsp_display_lock(250)")
    assert release < lock

    task = production_function("wpa_auditor_catalog_sync_all_task")
    assert "bool refresh_catalog = args && wpa_auditor_page;" in task
    assert "bool refresh_started" in task
    assert "if (!refresh_started)" in task
    assert "wpa_auditor_catalog_render_locked()" in task


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


def test_synced_captures_are_validated_and_cached_locally():
    single = production_function("compromised_transfer_task")
    copy = production_function("wpa_auditor_catalog_copy_item")
    local_scan = production_function("wpa_auditor_catalog_scan_local")
    for function in (single, copy, local_scan):
        assert "wpa_auditor_validate_local_capture(" in function
    assert "HS_CAPTURE_VALIDATION_INVALID" in single
    assert "HS_CAPTURE_VALIDATION_INVALID" in copy
    assert "invalid" in production_function("wpa_auditor_catalog_sync_all_task")


def test_sync_index_requires_a_stable_validation_result():
    stable = production_function("wpa_auditor_validation_is_stable")
    assert "HS_CAPTURE_VALIDATION_OK" in stable
    assert "HS_CAPTURE_VALIDATION_INVALID" in stable

    single = production_function("compromised_transfer_task")
    assert "validation_complete = wpa_auditor_validation_is_stable(" in single
    assert "if (validation_complete)" in single
    assert "result_err == ESP_OK && validation_complete" in single

    batch = production_function("wpa_auditor_catalog_sync_all_task")
    assert "wpa_auditor_validation_is_stable(validation_result)" in batch
    assert "failed == 0U && pending == 0U" in batch

    validate = production_function("wpa_auditor_validate_local_capture")
    assert "return saved;" in validate


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
    assert "wpa_auditor_catalog_source_in_sync_scope" in queue
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


def test_collapsed_catalog_keeps_global_sync_all_available():
    scope = production_function("wpa_auditor_catalog_source_in_sync_scope")
    assert "wpa_auditor_source_filter < 0" in scope
    assert "location->source ==" in scope

    count = production_function("wpa_auditor_catalog_syncable_count")
    queue = production_function("wpa_auditor_catalog_build_sync_queue")
    assert "wpa_auditor_catalog_source_in_sync_scope(location)" in count
    assert queue.count("wpa_auditor_catalog_source_in_sync_scope(location)") >= 2

    callback = production_function("wpa_auditor_source_filter_cb")
    assert 'global_sync ? "Sync all to Tab5"' in callback


def test_local_capture_delete_is_confirmed_and_keeps_workers_untouched():
    render = production_function("wpa_auditor_catalog_render_locked")
    assert '"Delete from Tab5"' in render
    assert "wpa_auditor_delete_local_request_cb" in render
    assert "HS_ARTIFACT_SOURCE_LOCAL" in render

    request = production_function("wpa_auditor_delete_local_request_cb")
    assert "wpa_auditor_pending_delete_path" in request
    assert '"Delete local capture?"' in request
    assert "Copies on Grove, USB and M-BUS stay untouched." in request

    confirm = production_function("wpa_auditor_delete_local_confirm_cb")
    assert "unlink(local_path)" in confirm
    assert "hs_capture_validation_sidecar_path" in confirm
    assert "janos_sync_state_remove_local_path" in confirm
    assert "wpa_auditor_catalog_request_refresh" in confirm
    assert '"delete_file' not in confirm

    prune = production_function("janos_sync_state_remove_local_path")
    assert "janos_sync_build_state_path" in prune
    assert "saved_local_path" in prune
    assert "rename(" in prune
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


def test_batch_queue_is_durable_and_reconciles_interrupted_work():
    load = production_function("wpa_auditor_batch_ensure_loaded")
    assert "hs_audit_queue_load_latest_with_scratch(" in load
    assert "WPA_AUDITOR_BATCH_A" in load
    assert "WPA_AUDITOR_BATCH_B" in load
    assert "MALLOC_CAP_SPIRAM" in load
    assert "wpa_auditor_batch_scratch" in load
    assert "hs_audit_queue_reconcile_after_boot(" in load
    save = production_function("wpa_auditor_batch_save")
    assert "hs_audit_queue_save_next_with_scratch(" in save
    assert "result == HS_AUDIT_QUEUE_CORRUPT" in load
    assert "starting empty" in load


def test_batch_queue_does_not_put_full_queue_copies_on_embedded_stack():
    assert "hs_audit_queue_t decoded;" not in QUEUE_SOURCE
    assert "hs_audit_queue_t a, b;" not in QUEUE_SOURCE
    assert "hs_audit_queue_t latest;" not in QUEUE_SOURCE
    assert "hs_audit_queue_load_latest_with_scratch" in QUEUE_SOURCE
    assert "hs_audit_queue_save_next_with_scratch" in QUEUE_SOURCE


def test_dashboard_storage_is_loaded_off_the_lvgl_callback():
    page = production_function("show_wpa_psk_auditor_page")
    loader = production_function("wpa_auditor_storage_task")
    starter = production_function("wpa_auditor_storage_request")
    wordlists = production_function("wpa_auditor_batch_build_wordlist_options")
    assert "hs_session_catalog_list(" not in page
    assert "hs_audit_history_list(" not in page
    assert "hs_session_catalog_list(" in loader
    assert "hs_audit_history_list(" in loader
    assert "hs_crack_scan_wordlists(" in loader
    assert "hs_crack_scan_wordlists(" not in wordlists
    assert "xTaskCreateWithCaps(" in starter
    assert "MALLOC_CAP_SPIRAM" in starter


def test_batch_selection_and_session_matching_use_psram_scratch():
    stash = production_function("wpa_auditor_batch_stash_selection")
    attach = production_function("wpa_auditor_batch_attach_latest_session")
    assert "heap_caps_calloc(" in stash
    assert "MALLOC_CAP_SPIRAM" in stash
    assert "heap_caps_calloc(" in attach
    assert "MALLOC_CAP_SPIRAM" in attach
    assert "entries[WPA_AUDITOR_RESUME_ROWS]" not in attach


def test_batch_reuses_the_cracker_coordinator_without_opening_the_single_audit_popup():
    launcher = production_function("wpa_auditor_batch_launch")
    assert "hs_crack_prepare_local_file(" in launcher
    assert "hs_crack_start_local_file(" not in launcher
    assert "hs_crack_show_popup(" not in launcher
    assert "hs_crack_ui.batch_managed = true" in launcher
    assert "hs_crack_start_configured()" in launcher
    local = production_function("hs_crack_start_local_file")
    assert "hs_crack_show_popup(" in local
    task = production_function("hs_crack_task")
    assert "hs_crack_stage_local_capture(" in task


def test_starting_a_batch_immediately_repaints_background_controls():
    start = production_function("wpa_auditor_batch_start_cb")
    launched = start.index("wpa_auditor_batch_launch_next()")
    repaint = start.index("show_wpa_psk_auditor_page()", launched)
    assert launched < repaint


def test_running_batch_refreshes_live_progress_without_rebuilding_the_page():
    render = production_function("wpa_auditor_batch_render_card")
    assert "wpa_auditor_batch_refresh_start()" in render

    refresh = production_function("wpa_auditor_batch_refresh_cb")
    assert "wpa_auditor_batch_update_status_label()" in refresh
    assert "show_wpa_psk_auditor_page()" not in refresh

    update = production_function("wpa_auditor_batch_update_status_label")
    for token in ("live_progress_percent", "live_tried", "live_elapsed_seconds",
                  "live_eta_seconds", "remote_total"):
        assert token in update

    stop = production_function("wpa_auditor_batch_refresh_stop")
    assert "lv_timer_delete(" in stop
    assert "wpa_auditor_batch_refresh_stop()" in production_function(
        "wpa_auditor_back_cb"
    )
    assert "wpa_auditor_batch_refresh_stop()" in production_function(
        "show_wpa_psk_auditor_page"
    )


def test_expanded_running_queue_row_uses_live_cracker_metrics():
    update = production_function("wpa_auditor_batch_update_status_label")
    assert "wpa_auditor_batch_update_live_item_label()" in update

    live_row = production_function("wpa_auditor_batch_update_live_item_label")
    assert "hs_crack_ui.live_progress_percent" in live_row
    assert "hs_crack_ui.live_tried" in live_row
    assert "wpa_auditor_batch_live_item_label" in live_row

    render = production_function("wpa_auditor_batch_render_card")
    assert "wpa_auditor_batch_live_item_label = label" in render


def test_local_resumable_audits_can_launch_without_a_source_monster():
    page = production_function("show_wpa_psk_auditor_page")
    assert "source != TAB_INTERNAL" not in page
    assert "session->origin.local_path[0]" in page

    launch = production_function("wpa_auditor_launch_saved")
    assert "if (tab == TAB_INTERNAL)" in launch
    assert "session->origin.local_path" in launch
    assert "hs_crack_start_local_file(" in launch


def test_live_batch_progress_matches_percent_u_on_esp_target_toolchains():
    update = production_function("wpa_auditor_batch_update_status_label")
    assert "(unsigned)current_progress" in update


def test_batch_controller_uses_the_shared_human_duration_formatter():
    update = production_function("wpa_auditor_batch_update_status_label")
    assert update.count("compromised_format_duration(") >= 2
    assert '"%llus elapsed' not in update
    assert 'eta_seconds ? "s"' not in update


def test_shared_duration_formatter_includes_days_for_long_audits():
    formatter = production_function("compromised_format_duration")
    harness = f"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
{formatter}

static void expect_duration(uint64_t seconds, const char *expected)
{{
    char actual[32];
    compromised_format_duration(seconds, actual, sizeof(actual));
    if (strcmp(actual, expected) != 0) {{
        fprintf(stderr, "%llu: expected %s, got %s\\n",
                (unsigned long long)seconds, expected, actual);
        exit(1);
    }}
}}

int main(void)
{{
    expect_duration(45U, "45s");
    expect_duration(492U, "8m 12s");
    expect_duration(12240U, "3h 24m");
    expect_duration(187020U, "2d 3h 57m");
    return 0;
}}
"""
    with tempfile.TemporaryDirectory(prefix="wpa_auditor_duration_") as tmp:
        tmp_path = Path(tmp)
        source = tmp_path / "duration_test.c"
        binary = tmp_path / "duration_test"
        source.write_text(harness, encoding="utf-8")
        if os.name == "nt":
            prefix = ["wsl", "--exec"]
            source_host = subprocess.check_output(
                prefix + ["wslpath", "-a", str(source)], text=True
            ).strip()
            binary_host = subprocess.check_output(
                prefix + ["wslpath", "-a", str(binary)], text=True
            ).strip()
        else:
            prefix = []
            source_host = str(source)
            binary_host = str(binary)
        subprocess.run(
            prefix + [
                "cc", "-std=c17", "-Wall", "-Wextra", "-Werror",
                source_host, "-o", binary_host,
            ],
            check=True,
        )
        subprocess.run(prefix + [binary_host], check=True)


def test_found_batch_item_persists_and_displays_the_password_in_green():
    task = production_function("hs_crack_task")
    render = production_function("wpa_auditor_batch_render_card")
    assert "batch_item->reason" in task
    assert "found_pw" in task
    assert "Password: %.63s" in render
    assert "HS_AUDIT_ITEM_FOUND" in render
    assert "COLOR_MATERIAL_GREEN" in render


def test_aggregate_crack_state_uses_the_quoted_host_tested_csv_module():
    source = SOURCE
    wrapper = production_function("hs_crack_state_upsert")
    assert "hs_crack_state_upsert_file(" in wrapper
    assert "hs_crack_csv_sanitize" not in source
    cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
    assert '"hs_crack_state.c"' in cmake


def test_batch_completion_persists_before_advancing_to_the_next_item():
    complete = production_function("wpa_auditor_batch_crack_complete_async")
    for state in ("HS_AUDIT_ITEM_FOUND", "HS_AUDIT_ITEM_NOT_FOUND",
                  "HS_AUDIT_ITEM_PAUSED", "HS_AUDIT_ITEM_CANCELLED",
                  "HS_AUDIT_ITEM_ERROR"):
        assert state in complete
    saved = complete.index("wpa_auditor_batch_save()")
    advanced = complete.index("wpa_auditor_batch_launch_next()")
    assert saved < advanced
    task = production_function("hs_crack_task")
    assert "completion callback unavailable; pausing batch" in task
    assert "wpa_auditor_batch->state = HS_AUDIT_BATCH_PAUSED" in task
    assert "wpa_auditor_batch_save()" in task


def test_batch_captures_metrics_before_terminal_session_tombstone():
    capture = production_function("hs_crack_batch_capture_session_metrics")
    for token in ("session_id", "total_tried", "active_time_ms",
                  "last_eta_seconds", "worker_count",
                  "wpa_auditor_progress_percent"):
        assert token in capture
    task = production_function("hs_crack_task")
    metrics = task.index("hs_crack_batch_capture_session_metrics(")
    tombstone = task.index("hs_session_catalog_tombstone(", metrics)
    assert metrics < tombstone


def test_batch_resume_session_matches_capture_method_and_wordlist_identity():
    matcher = production_function(
        "wpa_auditor_batch_session_matches_item"
    )
    assert "session->capture.size != item->capture_size" in matcher
    assert "session->capture.crc32 != item->capture_crc32" in matcher
    assert "session->config.method != wpa_auditor_batch->method" in matcher
    for token in ("wordlist_size", "wordlist_head_crc32",
                  "wordlist_tail_crc32"):
        assert token in matcher
    attach = production_function(
        "wpa_auditor_batch_attach_session_from_entries"
    )
    assert "wpa_auditor_batch_session_matches_item(" in attach
    latest = production_function("wpa_auditor_batch_attach_latest_session")
    assert "wpa_auditor_batch_attach_session_from_entries(" in latest


def test_batch_controls_distinguish_pause_skip_and_cancel_all():
    pause = production_function("wpa_auditor_batch_pause_cb")
    assert "wpa_auditor_batch_pause_requested = true" in pause
    assert "wpa_auditor_batch_cancel_requested = false" in pause
    assert "hs_crack_ui.cancel_requested = true" in pause

    skip = production_function("wpa_auditor_batch_skip_current_cb")
    assert "wpa_auditor_batch_pause_requested = false" in skip
    assert "wpa_auditor_batch_cancel_requested = false" in skip
    assert "hs_crack_ui.cancel_requested = true" in skip

    cancel = production_function("wpa_auditor_batch_cancel_confirm_cb")
    assert "wpa_auditor_batch_cancel_requested = true" in cancel
    assert "hs_audit_queue_cancel_all(" in cancel
    assert "wpa_auditor_batch_save()" in cancel
    assert "hs_crack_ui.cancel_requested = true" in cancel
    assert "wpa_auditor_batch_launch_next" not in cancel

    request = production_function("wpa_auditor_batch_cancel_request_cb")
    for copy in ("Cancel entire batch?", "Keep files and checkpoints",
                 "Cancel batch"):
        assert copy in request

    remove = production_function("wpa_auditor_batch_remove_cb")
    assert "hs_audit_queue_remove(" in remove
    assert "wpa_auditor_batch_save()" in remove
    assert "unlink(" not in remove
    assert "delete_file" not in remove


def test_cancelled_batch_completion_never_auto_advances():
    complete = production_function("wpa_auditor_batch_crack_complete_async")
    assert "wpa_auditor_batch_cancel_requested" in complete
    assert "hs_audit_queue_cancel_all(wpa_auditor_batch)" in complete
    assert "wpa_auditor_batch_launch_next()" in complete
    guard = complete.index("wpa_auditor_batch->state == HS_AUDIT_BATCH_RUNNING")
    launch = complete.index("wpa_auditor_batch_launch_next()", guard)
    assert guard < launch


def test_batch_dashboard_is_collapsed_by_default_and_shows_aggregate_metrics():
    page = production_function("wpa_auditor_batch_render_card")
    status = production_function("wpa_auditor_batch_update_status_label")
    for copy in ("complete", "aggregate", "elapsed", "ETA", "remote"):
        assert copy in status
    for copy in ("Batch queue", "Start batch", "Resume batch", "Pause",
                  "Skip current", "Cancel batch", "New batch", "Show queue",
                  "Hide queue"):
        assert copy in page
    assert "wpa_auditor_batch_expanded" in page
    assert "wpa_auditor_batch_list = lv_obj_create" in page


def test_finished_batch_has_explicit_reset_instead_of_a_dead_start_action():
    page = production_function("wpa_auditor_batch_render_card")
    assert "hs_audit_queue_next(wpa_auditor_batch)" in page
    assert "wpa_auditor_batch_new_cb" in page
    assert "wpa_auditor_batch_recover_cb" in page
    assert '"Recover batch"' in page
    assert "Batch finished. Start a new batch to change candidate source." in page

    reset = production_function("wpa_auditor_batch_new_cb")
    assert "hs_audit_queue_reset_batch(" in reset
    assert "wpa_auditor_batch_save()" in reset
    assert "unlink(" not in reset
    assert "hs_session_catalog" not in reset
    assert "hs_audit_history" not in reset

    recover = production_function("wpa_auditor_batch_recover_cb")
    assert "wpa_auditor_batch_attach_sessions(" in recover
    assert "hs_audit_queue_recover_unfinished(" in recover
    assert "wpa_auditor_batch_save()" in recover
    assert '"[HS-BATCH] RECOVER' in recover


def test_terminal_recovery_revalidates_saved_session_ids_before_resuming():
    refresh = production_function(
        "wpa_auditor_batch_refresh_recovery_session"
    )
    assert "hs_session_catalog_find_id(" in refresh
    assert "wpa_auditor_batch_session_matches_item(" in refresh
    assert "item->has_session = false" in refresh
    assert "memset(item->session_id" in refresh
    assert "wpa_auditor_batch_attach_session_from_entries(" in refresh

    attach = production_function("wpa_auditor_batch_attach_sessions")
    assert "wpa_auditor_batch_refresh_recovery_session(" in attach


def test_batch_load_logs_slot_state_and_every_item_before_reconciliation():
    load = production_function("wpa_auditor_batch_ensure_loaded")
    assert "hs_audit_queue_slot_t slot" in load
    assert "wpa_auditor_batch_log_snapshot(" in load
    snapshot = production_function("wpa_auditor_batch_log_snapshot")
    assert '"[HS-BATCH] %s slot=%s' in snapshot
    assert '"[HS-BATCH] ITEM' in snapshot
    assert "hs_audit_queue_recoverable_count(" in snapshot


def test_catalog_supports_multiselect_and_all_batch_state_filters():
    page = production_function("show_wpa_psk_auditor_page")
    for copy in ("All states", "New", "Resumable", "Found", "Not found",
                 "Error", "Invalid", "Select visible", "Clear",
                 "Add selected to queue"):
        assert copy in page
    render = production_function("wpa_auditor_catalog_render_locked")
    assert "lv_checkbox_create(" in render
    assert "wpa_auditor_select_asset_cb" in render
    assert "wpa_auditor_asset_matches_state(" in render
    select = production_function("wpa_auditor_select_visible_cb")
    assert "wpa_auditor_asset_invalid" in select


def test_selected_catalog_actions_stay_visible_below_the_scrollable_body():
    page = production_function("show_wpa_psk_auditor_page")
    body = page.index("lv_obj_t *body = lv_obj_create(wpa_auditor_page);")
    dock = page.index(
        "wpa_auditor_selection_dock = lv_obj_create(wpa_auditor_page);"
    )
    assert body < dock
    assert '"Add selected to queue"' in page
    assert "wpa_auditor_batch_add_btn = wpa_auditor_button(" in page
    assert "wpa_auditor_selection_dock" in page

    update = production_function("wpa_auditor_selection_update")
    assert '"Queue selection: %u"' in update
    assert "wpa_auditor_batch_add_btn" in update
    assert "wpa_auditor_batch_clear_btn" in update


def test_catalog_prevents_reselecting_a_capture_already_in_the_queue():
    render = production_function("wpa_auditor_catalog_render_locked")
    assert "wpa_auditor_batch_item_for_asset(asset)" in render
    assert '"IN QUEUE"' in render
    assert '"NOT READY"' in render
    assert '"PAUSE TO SYNC"' in render
    assert "Ready local captures can still be added" in render
    assert "already_queued" in render

    select_one = production_function("wpa_auditor_select_asset_cb")
    assert "wpa_auditor_batch_item_for_asset(" in select_one
    assert "wpa_auditor_batch_selected[index] = false" in select_one

    select_visible = production_function("wpa_auditor_select_visible_cb")
    assert "wpa_auditor_batch_item_for_asset(asset) < 0" in select_visible

    enqueue = production_function("wpa_auditor_batch_enqueue_selected")
    assert "already_queued" in enqueue
    assert '"Already in queue' in enqueue


def test_legacy_crack_dialog_marks_an_existing_local_queue_item_immediately():
    popup = production_function("hs_crack_show_popup")
    assert "wpa_auditor_batch_contains_local_path(" in popup
    assert "hs_crack_mark_queued_ui()" in popup


def test_resume_add_to_queue_reactivates_the_cancelled_item_in_place():
    contains = production_function("wpa_auditor_batch_contains_local_path")
    assert "HS_AUDIT_ITEM_CANCELLED" in contains

    enqueue = production_function("hs_crack_add_to_queue_cb")
    assert "hs_crack_ui.resume_session_valid" in enqueue
    assert "wpa_auditor_batch_restore_cancelled_resume(" in enqueue
    assert "hs_crack_ui.resume_session_id" in enqueue
    restored = enqueue.index("wpa_auditor_batch_restore_cancelled_resume(")
    marked = enqueue.index("hs_crack_mark_queued_ui()", restored)
    assert restored < marked

    restore = production_function(
        "wpa_auditor_batch_restore_cancelled_resume"
    )
    assert "wpa_auditor_batch_local_item_index(" in restore
    assert "hs_audit_queue_resume_cancelled(" in restore
    assert "wpa_auditor_batch_save()" in restore

    append = production_function("wpa_auditor_batch_append_local_capture")
    assert "resume_session_id" in append
    assert "item.has_session = true" in append
    assert "memcpy(item.session_id, resume_session_id" in append

    copy = production_function("compromised_start_copy")
    assert "enqueue_resume_session_valid" in copy
    assert "hs_crack_ui.resume_session_id" in copy
    transfer = production_function("compromised_transfer_task")
    assert "args->enqueue_resume_session_valid" in transfer
    assert "args->enqueue_resume_session_id" in transfer
    assert "wpa_auditor_batch_restore_cancelled_resume(" in transfer


def test_running_batch_accepts_new_local_items_without_unlocking_transports():
    live_append = production_function("wpa_auditor_batch_accepts_live_append")
    assert "hs_crack_ui.batch_managed" in live_append
    assert "HS_AUDIT_BATCH_RUNNING" in live_append
    assert "wpa_auditor_batch_pause_requested" in live_append
    assert "wpa_auditor_batch_cancel_requested" in live_append

    queue_busy = production_function("wpa_auditor_queue_edit_busy")
    assert "wpa_auditor_batch_accepts_live_append()" in queue_busy

    render = production_function("wpa_auditor_catalog_render_locked")
    assert "wpa_auditor_queue_edit_busy()" in render
    assert "wpa_auditor_asset_local_ready(asset)" in render
    assert "queue_edit_busy" in render
    assert "busy || audit_location == SIZE_MAX" in render

    select_visible = production_function("wpa_auditor_select_visible_cb")
    assert "wpa_auditor_batch_accepts_live_append()" in select_visible
    assert "wpa_auditor_asset_local_ready(asset)" in select_visible

    add = production_function("wpa_auditor_add_selected_cb")
    assert "wpa_auditor_queue_edit_busy()" in add
    assert "hs_crack_ui.active" not in add

    enqueue = production_function("wpa_auditor_batch_enqueue_selected")
    assert "wpa_auditor_batch_accepts_live_append()" in enqueue
    assert "wpa_auditor_asset_local_ready(asset)" in enqueue
    assert "queue changed in RAM but its journal could not be saved" in enqueue
    assert '"[HS-BATCH] APPEND_RUNNING' in enqueue


def test_catalog_refresh_resynchronizes_fixed_selection_actions():
    render = production_function("wpa_auditor_catalog_render_locked")
    assert "wpa_auditor_selection_update();" in render
    assert "if (selection_changed) wpa_auditor_selection_update();" not in render

    refresh = production_function("wpa_auditor_open_captures_cb")
    assert "wpa_auditor_selection_update();" in refresh

    click = production_function("wpa_auditor_add_selected_cb")
    assert '"[HS-BATCH] ADD_SELECTED' in click
    assert "Queue editing is temporarily busy" in click


def test_catalog_state_derivation_includes_sessions_queue_and_history():
    derive = production_function("wpa_auditor_asset_filter_state")
    assert "wpa_auditor_asset_invalid" in derive
    assert "wpa_auditor_batch_item_for_asset" in derive
    assert "wpa_auditor_asset_resumable" in derive
    assert "wpa_auditor_asset_history_state" in derive
    history = production_function("wpa_auditor_asset_history_state")
    for outcome in ("HS_SESSION_OUTCOME_FOUND", "HS_SESSION_OUTCOME_NOT_FOUND",
                    "HS_SESSION_OUTCOME_ERROR",
                    "HS_SESSION_OUTCOME_INTERRUPTED"):
        assert outcome in history


def test_remote_only_batch_selection_syncs_before_enqueueing():
    enqueue = production_function("wpa_auditor_batch_enqueue_selected")
    assert "needs_sync |= !invalid" in enqueue
    assert "wpa_auditor_batch_stash_selection()" in enqueue
    assert "wpa_auditor_batch_enqueue_after_sync = true" in enqueue
    assert "wpa_auditor_catalog_start_sync_current()" in enqueue
    assert "!compromised_transfer_ui.active" in enqueue
    assert "Could not start synchronization" in enqueue
    assert "Syncing selected remote captures" in enqueue
    build = production_function("wpa_auditor_catalog_build_sync_queue")
    assert "wpa_auditor_batch_enqueue_after_sync" in build
    assert "wpa_auditor_batch_selected[i]" in build
    scan = production_function("wpa_auditor_catalog_task")
    assert "wpa_auditor_batch_enqueue_after_refresh_async" in scan
    resume = production_function("wpa_auditor_batch_enqueue_after_refresh_async")
    assert "wpa_auditor_batch_restore_selection()" in resume


def test_batch_selection_survives_catalog_reordering_by_content_identity():
    stash = production_function("wpa_auditor_batch_stash_selection")
    restore = production_function("wpa_auditor_batch_restore_selection")
    for token in ("size", "crc32", "crc32_known", "name"):
        assert token in stash
        assert token in restore
    assert "wpa_auditor_batch_selected[i] = true" in restore
    assert "memset(wpa_auditor_batch_selected" in restore


def test_batch_rejects_invalid_or_changed_local_captures_before_launch():
    ready = production_function("wpa_auditor_asset_local_ready")
    assert "wpa_auditor_asset_invalid" in ready
    launch = production_function("wpa_auditor_batch_launch")
    assert "stat(item->local_path" in launch
    assert "S_ISREG" in launch
    assert "hs_crack_cache_file_crc32(" in launch
    assert "current_crc32 != item->capture_crc32" in launch
    assert "local capture changed" in launch
    assert "HS_AUDIT_ITEM_ERROR" in launch


def test_batch_locks_and_revalidates_the_selected_wordlist_fingerprint():
    dropdown = production_function("wpa_auditor_batch_configure_from_dropdown")
    assert "wpa_auditor_batch_configure_source_selection(" in dropdown
    configure = production_function(
        "wpa_auditor_batch_configure_source_selection"
    )
    for token in ("hs_crack_cache_wordlist_id(", "wordlist_size",
                  "wordlist_head_crc32", "wordlist_tail_crc32"):
        assert token in configure
    assert "return false" in configure
    launch = production_function("wpa_auditor_batch_launch")
    assert "identity.size != wpa_auditor_batch->wordlist_size" in launch
    assert "identity.head_crc32 !=" in launch
    assert "identity.tail_crc32 !=" in launch
    assert "wordlist changed" in launch


def test_batch_candidate_source_stays_editable_until_work_starts():
    options = production_function("wpa_auditor_batch_build_wordlist_options")
    assert "hs_audit_queue_can_change_candidate_source" in options
    changed = production_function("wpa_auditor_batch_wordlist_changed_cb")
    assert "wpa_auditor_batch_configure_from_dropdown" in changed
    assert "wpa_auditor_batch_save()" in changed
    render = production_function("wpa_auditor_batch_render_card")
    assert "wpa_auditor_batch_wordlist_changed_cb" in render
    assert "Candidate source applies to every queued capture" in render
    assert "Candidate source is locked while this batch is active" in render
    assert "Candidate source is locked to preserve resumable progress" in render


def test_expanded_batch_delegates_vertical_scroll_to_dashboard_viewport():
    page = production_function("show_wpa_psk_auditor_page")
    assert "lv_obj_set_size(body, lv_pct(100), 0)" in page
    assert "lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE)" in page
    render = production_function("wpa_auditor_batch_render_card")
    assert "lv_obj_clear_flag(wpa_auditor_batch_list, LV_OBJ_FLAG_SCROLLABLE)" in render
    assert "lv_obj_add_flag(wpa_auditor_batch_list, LV_OBJ_FLAG_SCROLL_CHAIN_VER)" in render
    assert "lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE)" in render
    assert "lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER)" in render


def test_queue_slots_are_replaced_on_filesystems_without_rename_overwrite():
    write_slot = re.search(
        r"static\s+hs_audit_queue_result_t\s+write_slot\s*\([^;]+?\n\{(.+?)\n\}",
        QUEUE_SOURCE,
        re.S,
    )
    assert write_slot, "missing write_slot"
    body = write_slot.group(1)
    remove = body.index("unlink(path)")
    replace = body.index("rename(temporary, path)")
    assert remove < replace


if __name__ == "__main__":
    tests = [value for name, value in globals().copy().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"test_wpa_psk_auditor_contract: PASS ({len(tests)} tests)")
