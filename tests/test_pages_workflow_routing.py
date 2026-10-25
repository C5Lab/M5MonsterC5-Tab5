"""Exercise the shell decisions that choose the Pages publisher."""

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PAGES = ROOT / ".github/workflows/pages-only.yml"
FIRMWARE = ROOT / ".github/workflows/esp32p4-build-master.yml"


def run_step(path: Path, name: str) -> str:
    lines = path.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        if line.strip() != f"- name: {name}":
            continue
        step_indent = len(line) - len(line.lstrip())
        for run_index in range(index + 1, len(lines)):
            item = lines[run_index]
            indent = len(item) - len(item.lstrip())
            if item.strip() and indent <= step_indent:
                break
            if item.strip() == "run: |":
                block = []
                for script_line in lines[run_index + 1 :]:
                    script_indent = len(script_line) - len(script_line.lstrip())
                    if script_line.strip() and script_indent <= indent:
                        break
                    block.append(script_line)
                return textwrap.dedent("\n".join(block)) + "\n"
    raise AssertionError(f"Cannot find run block for {name} in {path}")


def push_paths(path: Path) -> set[str]:
    paths = set()
    in_push = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line == "  push:":
            in_push = True
            continue
        if in_push and line.strip() and not line.startswith("    "):
            break
        if in_push and line.strip().startswith('- "'):
            paths.add(line.strip()[3:-1])
    return paths


class PagesWorkflowRoutingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        (self.work / "main").mkdir()
        (self.work / "main/main.c").write_text(
            '#define JANOS_TAB_VERSION "1.5.6"\n', encoding="utf-8"
        )
        bin_dir = self.work / "bin"
        bin_dir.mkdir()
        (bin_dir / "git").write_text(
            '#!/bin/sh\n'
            'if [ "$1" = diff ]; then printf "%s\\n" "$FAKE_CHANGED"; '
            'else exit 91; fi\n',
            encoding="utf-8",
        )
        (bin_dir / "gh").write_text(
            '#!/bin/sh\n'
            'if [ "${FAKE_GH_FAILURE:-0}" = 1 ]; then exit 42; fi\n'
            'if [ "$1" = api ]; then printf "%s\\n" "$FAKE_TAG"; exit 0; fi\n'
            'if [ "$1" = release ] && [ "$2" = download ]; then\n'
            '  while [ "$#" -gt 0 ]; do\n'
            '    if [ "$1" = --dir ]; then dest="$2"; break; fi\n'
            '    shift\n'
            '  done\n'
            '  mkdir -p "$dest"; cp "$FAKE_ASSET_DIR"/* "$dest"/; exit 0\n'
            'fi\n'
            'exit 92\n',
            encoding="utf-8",
        )
        for tool in (bin_dir / "git", bin_dir / "gh"):
            tool.chmod(0o755)
        self.env = os.environ.copy()
        self.assets = self.work / "release-assets"
        self.assets.mkdir()
        for name in (
            "bootloader.bin",
            "partition-table.bin",
            "M5MonsterC5-Tab5.bin",
            "M5MonsterC5-Tab5-1.5.6.bin",
        ):
            (self.assets / name).write_bytes(b"fixture")
        (self.assets / "M5MonsterC5-Tab5-1.5.6.bin").write_bytes(b"current-app")
        (self.assets / "M5MonsterC5-Tab5-full-1.5.6.bin").write_bytes(b"full-image")
        self.env.update(
            {
                "PATH": str(bin_dir) + os.pathsep + self.env["PATH"],
                "GITHUB_REPOSITORY": "C5Lab/M5MonsterC5-Tab5",
                "BASE_SHA": "base",
                "HEAD_SHA": "head",
                "FAKE_TAG": "v1.5.6",
                "FAKE_ASSET_DIR": str(self.assets),
                "FW_VERSION": "1.5.6",
            }
        )

    def execute(self, path: Path, step: str, changed: str, event="push", tag="v1.5.6", api_fails=False, route_tag="", input_tag=""):
        script = run_step(path, step)
        if path == FIRMWARE:
            script = script.replace("${{ github.event.before }}", "$BASE_SHA")
            script = script.replace("${{ github.sha }}", "$HEAD_SHA")
        script = script.replace("${{ steps.meta.outputs.tag }}", "$FAKE_TAG")
        output_file = self.work / "github-output"
        output_file.write_text("", encoding="utf-8")
        env = self.env | {
            "GITHUB_EVENT_NAME": event,
            "GITHUB_OUTPUT": str(output_file),
            "FAKE_CHANGED": changed,
            "FAKE_TAG": tag,
            "FAKE_GH_FAILURE": "1" if api_fails else "0",
            "ROUTE_TAG": route_tag,
            "INPUT_TAG": input_tag,
        }
        result = subprocess.run(
            ["bash", "-c", script], cwd=self.work, env=env, text=True, capture_output=True
        )
        return result, output_file.read_text(encoding="utf-8")

    def test_both_push_triggers_cover_firmware_inputs(self):
        required = {
            "main/**",
            "components/**",
            "managed_components/**",
            "dependencies/**",
            "CMakeLists.txt",
            "post_build.cmake",
            "dependencies.lock",
            "lv_conf.h",
            "partitions.csv",
            "sdkconfig*",
        }
        for workflow in (FIRMWARE, PAGES):
            with self.subTest(workflow=workflow.name):
                self.assertTrue(required <= push_paths(workflow), required - push_paths(workflow))

    def test_source_push_uses_release_publisher_even_with_workflow_change(self):
        for changed in (
            "main/main.c",
            "main/main.c\n.github/workflows/pages-only.yml",
            ".github/scripts/container_build.sh",
            ".github/scripts/container_build.sh\n.github/workflows/pages-only.yml",
            "main/janos_sniffer_session.h",
            "main/janos_sniffer_session.h\n.github/workflows/pages-only.yml",
            "components/pcap_reader/reader.c",
            "components/pcap_reader/reader.c\n.github/workflows/pages-only.yml",
            "managed_components/lvgl__lvgl/src/lv_obj.c",
            "dependencies/custom_component/CMakeLists.txt",
            "CMakeLists.txt",
            "post_build.cmake",
            "dependencies.lock",
            "lv_conf.h",
            "partitions.csv",
            "sdkconfig.defaults",
        ):
            with self.subTest(changed=changed):
                release, release_output = self.execute(
                    FIRMWARE, "Check workflow diff", changed
                )
                pages, pages_output = self.execute(
                    PAGES, "Skip when firmware workflow publishes this push", changed,
                    tag="v1.5.5", api_fails=True,
                )
                self.assertEqual(release.returncode, 0, release.stderr)
                self.assertIn("workflow_changed=false", release_output)
                self.assertEqual(pages.returncode, 0, pages.stderr)
                self.assertIn("deploy=false", pages_output)

    def test_workflow_only_push_uses_pages_only_when_release_matches(self):
        changed = ".github/workflows/pages-only.yml"
        release, release_output = self.execute(FIRMWARE, "Check workflow diff", changed)
        pages, pages_output = self.execute(
            PAGES, "Skip when firmware workflow publishes this push", changed
        )
        self.assertEqual(release.returncode, 0, release.stderr)
        self.assertIn("workflow_changed=true", release_output)
        self.assertEqual(pages.returncode, 0, pages.stderr)
        self.assertIn("deploy=true", pages_output)

    def test_docs_and_emulator_changes_remain_pages_only(self):
        self.assertTrue({"docs/**", "tools/ui_emulator/**"} <= push_paths(PAGES))
        self.assertFalse({"docs/**", "tools/ui_emulator/**"} & push_paths(FIRMWARE))
        for changed in ("docs/index.html", "tools/ui_emulator/web/app.mjs"):
            with self.subTest(changed=changed):
                pages, output = self.execute(
                    PAGES, "Skip when firmware workflow publishes this push", changed
                )
                self.assertEqual(pages.returncode, 0, pages.stderr)
                self.assertIn("deploy=true", output)

    def test_emulator_only_push_skips_stale_release(self):
        pages, output = self.execute(
            PAGES, "Skip when firmware workflow publishes this push",
            "tools/ui_emulator/runtime/application.c", tag="v1.5.5",
        )
        self.assertEqual(pages.returncode, 0, pages.stderr)
        self.assertIn("deploy=false", output)

    def test_push_uses_the_release_tag_validated_by_route(self):
        route, route_output = self.execute(
            PAGES, "Skip when firmware workflow publishes this push",
            "tools/ui_emulator/runtime/application.c", tag="v1.5.6",
        )
        self.assertEqual(route.returncode, 0, route.stderr)
        self.assertIn("tag=v1.5.6", route_output)
        resolve, resolve_output = self.execute(
            PAGES, "Resolve release tag", "", tag="v1.5.7", api_fails=True,
            route_tag="v1.5.6",
        )
        self.assertEqual(resolve.returncode, 0, resolve.stderr)
        self.assertIn("tag=v1.5.6", resolve_output)

    def test_manual_dispatch_may_use_explicit_older_release(self):
        release, release_output = self.execute(
            FIRMWARE, "Check workflow diff", "", event="workflow_dispatch"
        )
        pages, pages_output = self.execute(
            PAGES, "Skip when firmware workflow publishes this push", "",
            event="workflow_dispatch", tag="v1.5.5", api_fails=True,
        )
        self.assertEqual(release.returncode, 0, release.stderr)
        self.assertIn("workflow_changed=false", release_output)
        self.assertEqual(pages.returncode, 0, pages.stderr)
        self.assertIn("deploy=true", pages_output)
        resolve, resolve_output = self.execute(
            PAGES, "Resolve release tag", "", event="workflow_dispatch",
            tag="v1.5.7", api_fails=True, input_tag="v1.5.5",
        )
        self.assertEqual(resolve.returncode, 0, resolve.stderr)
        self.assertIn("tag=v1.5.5", resolve_output)

    def test_release_lookup_error_cannot_publish_pages(self):
        pages, output = self.execute(
            PAGES, "Skip when firmware workflow publishes this push",
            "tools/ui_emulator/runtime/application.c", api_fails=True,
        )
        self.assertNotEqual(pages.returncode, 0)
        self.assertNotIn("deploy=true", output)

    def test_pages_asset_download_reports_dotted_firmware_version(self):
        result, output = self.execute(
            PAGES, "Download release assets", "", tag="v1.5.6"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("fw_version=1.5.6", output)

    def test_pages_asset_download_rejects_missing_versioned_app(self):
        (self.assets / "M5MonsterC5-Tab5-1.5.6.bin").unlink()
        result, output = self.execute(
            PAGES, "Download release assets", "", tag="v1.5.6"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("fw_version=", output)

    def test_multiple_versioned_assets_use_current_release_app(self):
        (self.assets / "M5MonsterC5-Tab5-1.5.5.bin").write_bytes(b"old-app")
        for workflow in (PAGES, FIRMWARE):
            with self.subTest(workflow=workflow.name):
                result, output = self.execute(
                    workflow, "Download release assets", "", tag="v1.5.6"
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    (self.work / "docs/firmware/M5MonsterC5-Tab5.bin").read_bytes(),
                    b"current-app",
                )
                if workflow == PAGES:
                    self.assertIn("fw_version=1.5.6", output)

    def test_full_image_cannot_replace_missing_app_alias(self):
        (self.assets / "M5MonsterC5-Tab5.bin").unlink()
        (self.assets / "M5MonsterC5-Tab5-1.5.6.bin").unlink()
        for workflow in (PAGES, FIRMWARE):
            with self.subTest(workflow=workflow.name):
                result, _ = self.execute(
                    workflow, "Download release assets", "", tag="v1.5.6"
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.work / "docs/firmware/M5MonsterC5-Tab5.bin").exists())


if __name__ == "__main__":
    unittest.main()
