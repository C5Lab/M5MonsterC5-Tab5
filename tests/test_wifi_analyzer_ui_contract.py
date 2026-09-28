"""Compiler-free guards for the LVGL/controller lifetime boundary.

These source contracts intentionally do not claim rendering or target execution
coverage. Run hardware acceptance after the user's later authorized build.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/screens/wifi_analyzer_screen.c").read_text(encoding="utf-8")
HEADER = (ROOT / "main/screens/wifi_analyzer_screen.h").read_text(encoding="utf-8")


def body(name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", SOURCE)
    if not match:
        raise AssertionError(f"Missing function {name}")
    start = match.end()
    depth = 1
    # Ignore braces in comments and literals while preserving offsets.
    tail = SOURCE[start:]
    clean = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                   lambda m: " " * len(m.group()), tail, flags=re.S)
    for i, char in enumerate(clean):
        depth += (char == "{") - (char == "}")
        if not depth:
            return tail[:i]
    raise AssertionError(f"Unbalanced function {name}")


class AnalyzerUIContract(unittest.TestCase):
    def test_details_follow_fixed_list_and_have_bounded_scroll_content(self):
        page = body("wa_screen_show")
        self.assertLess(page.index("u->rows = container"), page.index("u->details = container"))
        self.assertIn("lv_obj_set_height(u->rows, 320)", page)
        self.assertIn("u->detail_scroll", page)
        self.assertIn("lv_obj_set_height(card, 248)", page)
        self.assertIn("LV_OBJ_FLAG_OVERFLOW_VISIBLE", page)
        self.assertIn("LV_DIR_VER", page.split("u->detail_scroll =", 1)[1])
        self.assertNotIn("lv_obj_scroll_to_view", body("select_ap"))
        self.assertNotIn("u->dirty = true", body("select_ap"))
        self.assertNotIn("lv_obj_clean", body("highlight_selection"))
        self.assertIn("lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS)", body("refresh"))

    def test_chart_and_rows_share_bssid_selection_without_rebuilding(self):
        self.assertIn("select_ap(u, index", body("row_event"))
        self.assertIn("select_ap(u,", body("chart_tap"))
        self.assertIn("highlight_selection(u)", body("select_ap"))
        self.assertIn("ap->bssid", body("select_ap"))
        self.assertIn("LV_EVENT_SHORT_CLICKED", body("wa_screen_show"))
        self.assertIn("LV_INDEV_TYPE_POINTER", body("chart_tap"))
        self.assertIn("wa_plot_zoom", body("chart_tap"))
        self.assertIn("u->visible_count", body("chart_tap"))
        self.assertIn("ap->band != u->chart_band", body("chart_tap"))
        self.assertIn("chart_hit_distance", body("chart_tap"))
        self.assertIn("wa_ap_segments", body("chart_hit_distance"))
        self.assertIn("if (right < left) continue", body("chart_hit_distance"))
        self.assertIn("if (best == INT_MAX) return", body("chart_tap"))
        self.assertIn("!u->snapshot->valid", body("chart_tap"))
        for function in ("chart_draw", "chart_tap"):
            self.assertIn("chart_area", body(function))
        self.assertIn("best + 4", body("chart_tap"))

    def test_detail_animation_is_bounded_cancelled_and_respects_motion_off(self):
        transition = body("details_transition")
        self.assertIn("u->motion", transition)
        self.assertIn("lv_anim_set_duration(&animation, 220)", transition)
        self.assertIn("details_anim_exec", transition)
        for function in ("wa_screen_hide", "delete_event"):
            self.assertIn("lv_anim_delete(u, details_anim_exec)", body(function))
        self.assertIn("details_transition", body("motion_event"))
        self.assertIn("details_transition(u, false)", body("close_details"))
        self.assertNotIn("u->selected[0] = 0", body("close_details"))

    def test_motion_is_bounded_display_only_and_deleted_with_page(self):
        motion = body("chart_reveal")
        self.assertIn("lv_anim_set_duration(&animation, 480)", motion)
        self.assertIn("lv_anim_path_ease_out", motion)
        self.assertNotIn("repeat", motion)
        for name in ("delete_event", "wa_screen_hide"):
            self.assertIn("lv_anim_delete(u, chart_anim_exec)", body(name))
        self.assertIn("lv_obj_is_visible", body("chart_anim_exec"))
        self.assertIn("old_scan", body("tick"))
        self.assertIn("old_boot", body("tick"))
        self.assertIn("u->reveal", body("chart_draw"))
        self.assertNotRegex(body("chart_anim_exec"), r"(?:rssi\s*=|wa_start|wa_stop)")
        self.assertIn('"Grow\\nOff"', SOURCE)

    def test_zoom_is_local_bounded_and_clips_outside_primary_markers(self):
        draw = body("chart_draw")
        self.assertIn("wa_plot_zoom", draw)
        self.assertIn("primary_visible", draw)
        self.assertIn("if (!primary_visible) continue", draw)
        for name in ("zoom_event", "pan_event", "fit_event"):
            self.assertNotRegex(body(name), r"wa_(?:start|stop|leave)\s*\(")
        self.assertIn('"1x\\n2x\\n4x"', SOURCE)

    def test_5ghz_range_uses_filtered_footprints_and_has_full_band_override(self):
        draw = body("chart_draw")
        self.assertIn("wa_plot_range(u->snapshot, &u->filter", draw)
        self.assertIn("u->full_band", draw)
        self.assertIn("range.low_mhz", draw)
        self.assertIn("range.high_mhz", draw)
        self.assertIn('"Auto\\nFull band"', SOURCE)
        self.assertIn("chart_range_event", SOURCE)
        self.assertNotRegex(body("chart_range_event"), r"wa_(?:start|stop|leave)\s*\(")
        self.assertIn("x - last_tick", draw)
        self.assertNotIn("int band", SOURCE.split("static int plot_x", 1)[1].split("{", 1)[0])

    def test_external_ports_only_and_per_port_state(self):
        self.assertIn("views[3]", SOURCE)
        for function in ("wa_screen_show", "wa_screen_hide"):
            self.assertRegex(body(function), r"tab\s*<\s*0\s*\|\|\s*tab\s*>=\s*3")

    def test_back_is_tab_scoped_and_waits_for_worker(self):
        self.assertIn("void (*on_back)(int tab)", HEADER)
        self.assertIn("wa_busy_tab(u->tab)", body("back_event"))
        self.assertIn("u->pending_back = true", body("back_event"))
        self.assertIn("u->pending_back && !wa_busy_tab(u->tab)", body("tick"))
        self.assertIn("u->on_back(u->tab)", body("tick"))
        self.assertNotIn("lv_event_t *", body("tick"))
        self.assertIn("u->pending_back = false", body("wa_screen_hide"))

    def test_delete_releases_ui_but_never_session(self):
        cleanup = body("delete_event")
        self.assertIn("wa_leave(u->tab)", cleanup)
        self.assertIn("lv_timer_delete(u->timer)", cleanup)
        self.assertIn("heap_caps_free(u->snapshot)", cleanup)
        self.assertNotRegex(SOURCE, r"(?:free|delete)\s*\(\s*u->session")
        self.assertNotIn("vTaskDelete", SOURCE)

    def test_psram_only_copy_and_ui_timer(self):
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", body("wa_screen_show"))
        self.assertNotRegex(SOURCE, r"(?<!_)\b(?:malloc|calloc|realloc)\s*\(")
        self.assertIn("wa_copy_view(u->session, u->snapshot, &u->status)", body("tick"))
        self.assertIn("lv_timer_create(tick, 250, u)", SOURCE)

    def test_filter_callbacks_cannot_start_acquisition(self):
        for function in ("filter_event", "reset_event", "chart_band_event", "sort_event"):
            self.assertNotRegex(body(function), r"wa_(?:start|stop|leave)\s*\(")
        self.assertNotRegex(SOURCE, r"(?:select_networks|uart_write|uart_read|legacy_scan)")

    def test_plot_uses_real_geometry_and_frequency(self):
        draw = body("chart_draw")
        self.assertIn("wa_ap_segments(ap, segments)", draw)
        self.assertIn("wa_frequency(ap->primary)", draw)
        self.assertIn("if (!segment_count)", draw)
        self.assertIn("d.text_local = 1", body("draw_text"))
        self.assertIn("TRUNCATED", body("refresh"))
        self.assertIn("s->found", body("refresh"))

    def test_lvgl_function_names_exist_in_installed_headers(self):
        headers = ROOT / "managed_components/lvgl__lvgl"
        if not headers.exists():
            self.skipTest("Managed LVGL dependency not present")
        declarations = "\n".join(p.read_text(encoding="utf-8", errors="replace")
                                 for p in headers.rglob("*.h"))
        names = set(re.findall(r"\b(lv_[a-zA-Z0-9_]+)\s*\(", SOURCE))
        missing = sorted(n for n in names if not re.search(r"\b" + n + r"\s*\(", declarations))
        self.assertEqual([], missing, "UI used an API absent from installed LVGL")


if __name__ == "__main__":
    unittest.main()
