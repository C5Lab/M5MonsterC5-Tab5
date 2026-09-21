"""Regression contract for the handshake coordinator task stack budget."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "main.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    match = re.search(rf"static\s+[^\n]+?\b{name}\s*\([^;]+?\n\{{", SOURCE, re.S)
    assert match, f"missing function {name}"
    depth = 0
    opened = False
    for index in range(match.start(), len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
            opened = True
        elif SOURCE[index] == "}":
            depth -= 1
            if opened and depth == 0:
                return SOURCE[match.start() : index + 1]
    raise AssertionError(f"unterminated function {name}")


def test_coordinator_has_headroom_for_nested_transfer_and_logging():
    match = re.search(r"#define\s+HS_CRACK_TASK_STACK_BYTES\s+(\d+)", SOURCE)
    assert match, "missing named hs_crack task stack budget"
    assert int(match.group(1)) >= 16384
    create = re.search(
        r"xTaskCreateWithCaps\(\s*hs_crack_task,\s*\"hs_crack\",\s*"
        r"HS_CRACK_TASK_STACK_BYTES,.*?MALLOC_CAP_SPIRAM\s*\|\s*MALLOC_CAP_8BIT\s*\)",
        SOURCE,
        re.S,
    )
    assert create, "hs_crack coordinator stack must be allocated from byte-addressable PSRAM"

    task = function_body("hs_crack_task")
    assert "vTaskDeleteWithCaps(NULL);" in task


def test_cpu_workers_keep_internal_freertos_stacks():
    start = SOURCE.index("static bool hs_crack_workers_start")
    end = SOURCE.index("static void hs_crack_progress", start)
    workers = SOURCE[start:end]
    assert "xTaskCreatePinnedToCore(hs_crack_worker_task" in workers
    assert "xTaskCreatePinnedToCoreWithCaps(hs_crack_worker_task" not in workers
    assert "heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)" in SOURCE
    assert '"[HS-MEM] local workers stage=%s' in SOURCE
    assert 'hs_crack_log_worker_memory("before")' in workers
    assert 'hs_crack_log_worker_memory("ready")' in workers
    assert 'hs_crack_log_worker_memory("failed")' in workers


def test_coordinator_reports_stack_watermark_around_worker_discovery():
    assert 'hs_crack_log_stack_watermark("start")' in SOURCE
    assert 'hs_crack_log_stack_watermark("before worker discovery")' in SOURCE
    assert 'hs_crack_log_stack_watermark("after worker discovery")' in SOURCE


def test_durable_session_buffers_do_not_live_on_the_coordinator_stack():
    task = function_body("hs_crack_task")
    assert "hs_session_t crack_session =" not in task
    assert "hs_session_t loaded_session =" not in task
    assert re.search(
        r"heap_caps_calloc\(\s*1,\s*sizeof\(\*crack_session\),\s*"
        r"MALLOC_CAP_SPIRAM\s*\)",
        task,
    )

    store = (ROOT / "main" / "hs_crack_session.c").read_text(encoding="utf-8")
    assert "hs_session_t a, b;" not in store
    assert "hs_session_t latest;" not in store
    assert "hs_session_t tombstone =" not in store
