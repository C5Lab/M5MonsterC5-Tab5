"""Source contract for durable distributed crack resume.

The behavioral codec and scheduler tests cover the pure components.  This
contract guards their orchestration in ``main.c`` so Cancel -> Start restores
the exact local and remote suffixes instead of repartitioning the dictionary.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
SESSION_SOURCE = (ROOT / "main" / "hs_crack_session.c").read_text(encoding="utf-8")
CATALOG_SOURCE = (ROOT / "main" / "hs_session_catalog.c").read_text(encoding="utf-8")


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


def test_runtime_uses_the_multi_session_ab_catalog():
    assert '#include "hs_crack_session.h"' in SOURCE
    assert '#include "hs_session_catalog.h"' in SOURCE
    assert '"/sdcard/lab/handshakes/.crack_audit/active.a"' in SOURCE
    assert '"/sdcard/lab/handshakes/.crack_audit/active.b"' in SOURCE
    assert '"/sdcard/lab/handshakes/.crack_audit/active"' in SOURCE
    task = production_function("hs_crack_task")
    assert "hs_crack_load_matching_session(" in task
    checkpoint = production_function("hs_crack_session_checkpoint")
    assert "hs_session_catalog_save(" in checkpoint
    assert "hs_session_catalog_tombstone(" in task


def test_starting_a_different_audit_does_not_retire_other_sessions():
    task = production_function("hs_crack_task")
    fresh = task.index("if (!restored_session)")
    begin = task.index("hs_crack_session_begin(", fresh)
    window = task[fresh:begin]
    assert "hs_session_tombstone(" not in window


def test_only_an_exact_active_capture_and_wordlist_can_resume():
    match_start = SESSION_SOURCE.index("bool hs_session_matches_active_wordlist(")
    match_end = SESSION_SOURCE.index("\n}\n", match_start) + 2
    match = SESSION_SOURCE[match_start:match_end]
    for token in (
        "HS_SESSION_ACTIVE",
        "capture_size",
        "capture_crc32",
        "wordlist_path",
        "wordlist_size",
        "wordlist_mtime",
        "wordlist_head_crc32",
        "wordlist_tail_crc32",
    ):
        assert token in match
    assert "wordlist_index" not in match
    task = production_function("hs_crack_task")
    assert "crack_session->phase.wordlist_index = (uint16_t)wi" in task


def test_all_mode_remaps_the_saved_identity_before_iterating_the_catalog():
    resolver = production_function("hs_crack_resume_wordlist_index")
    assert "hs_session_catalog_find_newest_wordlist(" in resolver
    assert "hs_crack_cache_wordlist_id(" in resolver
    assert "hs_session_find_active_wordlist(" in resolver
    task = production_function("hs_crack_task")
    remap = task.index("hs_crack_resume_wordlist_index(&capture_id)")
    loop = task.index("for (int wi = wl_from; wi < wl_to; wi++)")
    assert remap < loop
    assert "wl_from = resume_index" in task[remap:loop]


def test_all_mode_fails_closed_when_active_wordlist_becomes_unreadable():
    resolver = production_function("hs_crack_resume_wordlist_index")
    assert "HS_CRACK_RESUME_STALE" in resolver
    assert "matching_capture" in resolver
    task = production_function("hs_crack_task")
    stale = task.index("resume_index == HS_CRACK_RESUME_STALE")
    loop = task.index("for (int wi = wl_from; wi < wl_to; wi++)")
    assert stale < loop
    assert "goto finish" in task[stale:loop]


def test_single_wordlist_also_runs_fail_closed_preflight_before_iteration():
    task = production_function("hs_crack_task")
    wordlist_phase = task.index("bool all = (hs_crack_ui.wl_choice")
    preflight = task.index(
        "if (capture_cache_ready && !hs_crack_ui.force_rerun)", wordlist_phase)
    loop = task.index("for (int wi = wl_from; wi < wl_to; wi++)")
    assert preflight < loop
    window = task[preflight:loop]
    assert "hs_crack_resume_wordlist_index(&capture_id)" in window
    assert "resume_index == HS_CRACK_RESUME_STALE" in window
    assert "if (all && resume_index >= 0)" in window


def test_cancel_terminal_status_advances_remote_safe_offsets():
    cancel = production_function("hs_crack_remote_cancel_one")
    terminal = cancel[cancel.index("HS_REMOTE_DONE") :]
    assert "hs_remote_lease_note_response" in terminal
    assert "hs_sched_note_progress" in terminal


def test_checkpoint_snapshots_local_and_remote_shards():
    checkpoint = production_function("hs_crack_session_checkpoint")
    assert "local_safe_offset" in checkpoint
    assert "remote_shards" in checkpoint
    assert "confirmed_safe_offset" in checkpoint
    assert "hs_session_catalog_save" in checkpoint


def test_distributed_runs_checkpoint_after_a_drained_local_boundary():
    task = production_function("hs_crack_task")
    assert "if (!distributed &&" not in task
    periodic = task.index("hs_crack_checkpoint_due(")
    checkpoint = task.index("hs_crack_session_checkpoint(", periodic)
    drained = task.rfind("hs_crack_collect", 0, checkpoint)
    assert drained >= 0
    assert "local_safe_offset = hs_crack_ui.wordlist_offset" in task[drained:checkpoint]


def test_resume_restores_ranges_before_starting_workers():
    task = production_function("hs_crack_task")
    load = task.index("hs_crack_load_matching_session(")
    restore = task.index("hs_crack_restore_session_shards(")
    first_remote_start = task.index("hs_crack_remote_start(", restore)
    assert load < restore < first_remote_start
    assert "[HS-CRACK] RESUME session=" in task


def test_cancel_checkpoint_is_written_after_remote_cancel_and_before_close():
    task = production_function("hs_crack_task")
    cancel_branch = task.index("if (stop_source || hs_crack_ui.cancel_requested")
    cancel = task.index("hs_crack_remote_cancel(", cancel_branch)
    checkpoint = task.index("hs_crack_session_checkpoint(", cancel)
    close = task.index("fclose(wl)", cancel)
    assert cancel < checkpoint < close


if __name__ == "__main__":
    tests = [value for name, value in globals().copy().items()
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"test_distributed_resume_contract: PASS ({len(tests)} tests)")
