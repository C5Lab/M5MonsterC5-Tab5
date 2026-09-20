"""Production-source contract for bounded worker-stage recovery."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def production_function(name: str) -> str:
    match = re.search(rf"static\s+[^\n]+\s+{name}\s*\([^;]+?\n\{{", SOURCE, re.S)
    assert match, f"missing function {name}"
    start = match.end() - 1
    depth = 0
    for index in range(start, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start : index + 1]
    raise AssertionError(f"unterminated function {name}")


def test_top_level_stages_use_the_shared_three_attempt_budget():
    capabilities = production_function("hs_crack_remote_capabilities")
    sync_file = production_function("hs_crack_remote_sync_file")
    start = production_function("hs_crack_remote_start")
    assert "attempt <= HS_REMOTE_STAGE_ATTEMPTS" in capabilities
    assert "attempt <= HS_REMOTE_STAGE_ATTEMPTS" in sync_file
    assert "const unsigned attempts = HS_REMOTE_STAGE_ATTEMPTS" in start


def test_probe_has_no_nested_retry_multiplier():
    probe = production_function("hs_crack_remote_probe")
    assert "const unsigned attempts = 1U" in probe


def test_sync_retry_requires_a_safe_boundary_and_uses_resume_backoff():
    sync_file = production_function("hs_crack_remote_sync_file")
    assert "bool boundary_safe = !worker->failed" in sync_file
    assert "hs_remote_stage_retry_allowed" in sync_file
    assert "hs_remote_stage_backoff_ms" in sync_file
    assert sync_file.count("janos_uart_set_baud") == 1
    assert sync_file.count("janos_uart_restore_baud") == 1
    failed_before_restore = sync_file.index("attempt == 1U && fast && worker->failed")
    restore = sync_file.index("janos_uart_restore_baud")
    assert failed_before_restore < restore
    assert "hs_crack_remote_abandon_fast_baud(worker)" in sync_file


def test_plain_uart_failure_drains_terminal_text_before_retry():
    upload = production_function("hs_crack_remote_upload")
    recovery = upload.index("block loop ended NOT ok")
    assert upload.index("hs_crack_remote_uart_recover", recovery) > recovery
    helper = production_function("hs_crack_remote_uart_recover")
    assert helper.count("hs_crack_remote_wait_end") == 2
    assert "hs_crack_remote_sync_lost" in helper


def test_uncertain_command_and_ready_boundaries_disable_the_worker():
    upload = production_function("hs_crack_remote_upload")
    assert 'hs_crack_remote_sync_lost(worker, "receive write uncertain")' in upload
    assert 'hs_crack_remote_sync_lost(worker, "READY boundary lost")' in upload
    assert 'hs_crack_remote_sync_lost(worker, "READY without END")' in upload
    assert 'hs_crack_remote_sync_lost(worker, "terminal END missing")' in upload


def test_recoverable_raw_mode_exits_use_can_and_wait_for_cli_boundary():
    upload = production_function("hs_crack_remote_upload")
    assert 'hs_crack_remote_uart_recover(worker, true, true,' in upload
    assert upload.count("hs_crack_remote_uart_recover(worker, false, true,") >= 3
    assert "hs_crack_remote_reset_safe(worker, kind, size, crc32)" in upload


def test_unexpected_uart_reply_never_sends_diag_from_ambiguous_raw_mode():
    upload = production_function("hs_crack_remote_upload")
    branch = upload[upload.index("if (ready.type != HS_REMOTE_READY)"):
                    upload.index("if (strcmp(ready.kind, kind)")]
    assert "ready.type == HS_REMOTE_REJECTED" in branch
    assert "ready.type == HS_REMOTE_SYNC_ERROR" in branch
    assert 'hs_crack_remote_sync_lost(worker, "unexpected reply boundary")' in branch
    assert branch.index("bool safe = ready.type == HS_REMOTE_REJECTED") < branch.index(
        "hs_crack_remote_query_diag(worker)")


def test_usb_recovery_reports_boundary_and_diag_failures_through_worker_stage():
    recovery = production_function("hs_crack_remote_usb_recover")
    diag = production_function("hs_crack_remote_query_diag")
    assert 'hs_crack_remote_sync_lost(worker, "USB terminal boundary lost")' in recovery
    assert 'hs_crack_remote_sync_lost(worker, "USB DIAG boundary lost")' in recovery
    assert "hs_crack_remote_report_current(worker, report)" in diag


def test_start_partial_writes_and_ambiguous_status_disable_worker():
    start = production_function("hs_crack_remote_start")
    assert 'hs_crack_remote_sync_lost(worker, "start write uncertain")' in start
    assert 'hs_crack_remote_sync_lost(worker, "status write uncertain")' in start
    assert 'hs_crack_remote_sync_lost(worker, "status boundary ambiguous")' in start


def test_both_poll_failure_paths_mark_lost_and_show_intermediate_misses():
    poll = production_function("hs_crack_remote_poll")
    assert poll.count("hs_crack_remote_mark_lost") == 2
    assert poll.count("hs_crack_remote_report(worker, \"status\"") == 2
    lost = production_function("hs_crack_remote_mark_lost")
    assert "lost -> local" in lost
    assert "confirmed_safe_offset" in lost


if __name__ == "__main__":
    tests = [value for name, value in globals().copy().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"test_worker_stage_retry_contract: PASS ({len(tests)} tests)")
