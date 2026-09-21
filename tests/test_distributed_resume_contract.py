"""Source contract for durable distributed crack resume.

The behavioral codec and scheduler tests cover the pure components.  This
contract guards their orchestration in ``main.c`` so Cancel -> Start restores
the exact local and remote suffixes instead of repartitioning the dictionary.
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


def test_runtime_uses_the_ab_session_store():
    assert '#include "hs_crack_session.h"' in SOURCE
    assert '"/sdcard/lab/handshakes/.crack_audit/active.a"' in SOURCE
    assert '"/sdcard/lab/handshakes/.crack_audit/active.b"' in SOURCE
    task = production_function("hs_crack_task")
    assert "hs_session_load_latest(" in task
    assert "hs_session_save_next(" in SOURCE
    assert "hs_session_tombstone(" in task


def test_only_an_exact_active_capture_and_wordlist_can_resume():
    match = production_function("hs_crack_session_matches_active_wordlist")
    for token in (
        "HS_SESSION_ACTIVE",
        "capture->size",
        "capture->crc32",
        "wordlist->path",
        "wordlist->size",
        "wordlist->mtime",
        "wordlist->head_crc32",
        "wordlist->tail_crc32",
    ):
        assert token in match


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
    assert "hs_session_save_next" in checkpoint


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
    load = task.index("hs_session_load_latest(")
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
