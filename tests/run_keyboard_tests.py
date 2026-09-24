#!/usr/bin/env python3
"""Run real protocol and LVGL tests with GCC (Linux/WSL), no hardware needed."""
from pathlib import Path
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
with tempfile.TemporaryDirectory(prefix="tab5-keyboard-tests-") as tmp:
    build = Path(tmp)
    flags = ["gcc", "-std=c11", "-g", "-O0", "-fsanitize=address,undefined",
             "-fno-omit-frame-pointer", "-I", "main"]
    protocol = build / "protocol"
    subprocess.run(flags + ["-Wall", "-Wextra", "-Werror", "tests/tab5_keyboard_test.c",
                           "main/tab5_keyboard_protocol.c", "-o", str(protocol)], check=True)
    subprocess.run([str(protocol)], check=True)
    lvgl = ROOT / "managed_components/lvgl__lvgl"
    ui_flags = flags + ["-I", str(lvgl), "-DLV_CONF_SKIP", "-DLV_USE_OS=LV_OS_NONE",
                       "-DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB"]
    # Compile against the actual bundled LVGL, not a mock of its lifecycle.
    # Disabled optional integrations preprocess to empty translation units.
    sources = ["main/app_keyboard.c", "main/app_keyboard_navigation.c",
               "main/tab5_keyboard_protocol.c"]
    sources += [str(p) for p in sorted((lvgl / "src").rglob("*.c"))
                if not {"drivers", "gltf"}.intersection(p.relative_to(lvgl / "src").parts)]
    def compile_source(item):
        i, source = item
        obj = build / f"{i}.o"
        subprocess.run(ui_flags + ["-c", source, "-o", str(obj)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return str(obj)
    try:
        with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 1)) as pool:
            objects = list(pool.map(compile_source, enumerate(sources)))
    except subprocess.CalledProcessError as error:
        print(error.stderr.decode(), flush=True)
        raise
    for index, test in enumerate(("app_keyboard_test", "app_keyboard_navigation_test")):
        test_obj = compile_source((len(sources) + index, f"tests/{test}.c"))
        binary = build / test
        subprocess.run(flags + objects + [test_obj, "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
