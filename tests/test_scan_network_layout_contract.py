from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class ScanNetworkLayoutContractTests(unittest.TestCase):
    def test_checkbox_indicator_is_centered_in_a_fixed_touch_slot(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        scan_task = function_body(source, "static void wifi_scan_task")

        self.assertIn("lv_obj_t *selection_slot = lv_obj_create(item)", scan_task)
        self.assertIn("lv_obj_set_size(selection_slot, 50, 50)", scan_task)
        self.assertIn("lv_obj_remove_style_all(selection_slot)", scan_task)
        self.assertIn("lv_obj_t *cb = lv_checkbox_create(selection_slot)", scan_task)
        self.assertIn("lv_obj_set_size(cb, LV_SIZE_CONTENT, LV_SIZE_CONTENT)", scan_task)
        self.assertIn("lv_obj_center(cb)", scan_task)
        self.assertNotIn("lv_obj_set_size(cb, 50, 50)", scan_task)

    def test_scan_rows_use_responsive_two_or_three_line_layout(self):
        source = (ROOT / "main/main.c").read_text(encoding="utf-8")
        formatter = function_body(source, "static void format_network_info")
        scan_task = function_body(source, "static void wifi_scan_task")
        inspect_task = function_body(source, "static void inspect_networks_task")
        creds_fetch = function_body(source, "static void creds_fetch")

        self.assertIn('const char *row_sep = ui_wide_layout() ? "  |  " : "\\n"', formatter)
        self.assertIn("MFP ?", formatter)
        self.assertIn(
            '"  |  %s%sUptime: %s  |  Vendor: %s"',
            formatter,
        )
        self.assertIn("mfp_display, row_sep", formatter)
        self.assertNotIn("row_sep, mfp_display", formatter)
        self.assertNotIn("badge", formatter)
        self.assertIn("lv_obj_t *title_row = lv_obj_create(text_cont)", scan_task)
        self.assertIn("saved_badge", scan_task)
        self.assertIn('lv_label_set_text(saved_badge, "password saved")', scan_task)
        self.assertIn("if (creds_have(ctx, net->ssid))", scan_task)
        self.assertNotIn("lv_obj_add_flag(saved_badge, LV_OBJ_FLAG_HIDDEN)", scan_task)
        self.assertIn("lv_obj_set_style_max_width(ssid_label", scan_task)
        self.assertIn("lv_obj_set_flex_grow(text_cont, 1)", scan_task)
        self.assertNotIn("style_network_row_text", scan_task)
        self.assertNotIn("creds_badge", inspect_task)
        self.assertNotIn("static const char *creds_badge", source)
        self.assertEqual(2, creds_fetch.count("if (line_pos > 0)"))


if __name__ == "__main__":
    unittest.main()
