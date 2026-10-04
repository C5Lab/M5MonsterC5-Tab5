from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    search_from = 0
    while True:
        start = source.index(signature, search_from)
        opening = source.index("{", start)
        semicolon = source.find(";", start, opening)
        if semicolon == -1:
            break
        search_from = start + len(signature)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class ObserverIncrementalUiContractTests(unittest.TestCase):
    def test_station_stop_restores_the_focused_network_when_opened_from_popup(self):
        source = read("main/main.c")
        popup = function_body(source, "static void popup_client_row_click_cb")
        stop = function_body(source, "static void stop_and_close_deauth_popup")
        inline = function_body(source, "static void client_row_click_cb")
        self.assertIn("ctx->observer_station_return_to_network = true", popup)
        self.assertIn("ctx->observer_station_return_network_idx = network_idx", popup)
        self.assertIn("ctx->observer_station_return_to_network = false", inline)
        self.assertIn("show_network_popup(return_network)", stop)
        self.assertLess(stop.index("show_network_popup(return_network)"),
                        stop.index('"start_sniffer_noscan"'))

    def test_saved_badge_and_long_vendor_do_not_expand_the_title_row(self):
        table = function_body(read("main/main.c"), "static void update_observer_table")
        self.assertIn("lv_obj_set_height(ssid_label, lv_font_montserrat_18.line_height)", table)
        self.assertIn("lv_obj_set_width(ui->summary_label, wide_layout ? 0 : lv_pct(100))", table)
        self.assertIn("lv_obj_set_flex_grow(ui->summary_label, wide_layout ? 1 : 0)", table)
        self.assertIn("lv_obj_set_height(ui->summary_label, lv_font_montserrat_12.line_height)", table)

    def test_observer_metadata_uses_responsive_two_or_three_row_layout(self):
        source = read("main/main.c")
        formatter_signature = "static void format_observer_network_info"
        summary_signature = "static void format_observer_summary_info"

        self.assertIn(formatter_signature, source)
        self.assertIn(summary_signature, source)

        formatter = function_body(source, formatter_signature)
        summary = function_body(source, summary_signature)
        table = function_body(source, "static void update_observer_table")
        inspect = function_body(source, "static void inspect_observer_task")
        ssid_update = function_body(source, "static void observer_update_ssid_label")

        self.assertIn("net->security", formatter)
        self.assertIn("sec_col", formatter)
        self.assertIn('dBm#  |  %s"', formatter)
        self.assertNotIn("\\n", formatter)
        self.assertNotIn("Uptime:", formatter)
        self.assertNotIn("Vendor:", formatter)
        self.assertIn('"Uptime: %s  |  Vendor: %s"', summary)
        self.assertNotIn("\\n", summary)
        self.assertNotIn("badge", formatter)
        self.assertIn("format_observer_network_info", table)
        self.assertIn("summary_label", table)
        self.assertIn("LV_LABEL_LONG_DOT", table)
        self.assertIn("bool wide_layout = ui_wide_layout()", table)
        self.assertIn("lv_obj_t *title_content = lv_obj_create(title_row)", table)
        self.assertIn("wide_layout ? title_content : header", table)
        self.assertIn("lv_obj_set_style_max_width(ssid_label", table)
        self.assertIn("ui->saved_badge", table)
        self.assertIn('LV_SYMBOL_OK " saved"', table)
        self.assertIn("creds_have(ctx, net->ssid)", table)
        self.assertNotIn("style_network_row_text(header", table)
        self.assertEqual(2, inspect.count("format_observer_network_info"))
        self.assertEqual(2, inspect.count("observer_update_summary_label(ctx, i)"))
        self.assertNotIn("format_network_info", inspect)
        self.assertNotIn("creds_have(ctx, net->ssid)", ssid_update)
        self.assertNotIn("password saved", ssid_update)
        self.assertIn("lv_label_set_recolor(ssid_label, true)", table)
        self.assertIn(
            "lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_18, 0)",
            table,
        )
        self.assertIn(
            "lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0)",
            table,
        )

    def test_observer_clients_are_collapsed_and_expand_as_an_accordion(self):
        source = read("main/main.c")
        toggle_signature = "static void observer_client_toggle_cb"

        self.assertIn(toggle_signature, source)

        toggle = function_body(source, toggle_signature)
        add_client = function_body(source, "static void observer_add_client_row")
        sync = function_body(source, "static void observer_sync_changed_tiles")
        table = function_body(source, "static void update_observer_table")

        self.assertIn("clients_expanded", source)
        self.assertIn("client_toggle_label", source)
        self.assertIn("lv_event_stop_bubbling(e)", toggle)
        self.assertIn("other_ui->clients_expanded = false", toggle)
        self.assertIn(
            "lv_obj_add_flag(other_ui->client_container, LV_OBJ_FLAG_HIDDEN)",
            toggle,
        )
        self.assertNotIn(
            "lv_obj_clear_flag(ui->client_container, LV_OBJ_FLAG_HIDDEN)",
            add_client,
        )
        self.assertIn("observer_update_client_toggle(ctx, i)", sync)
        self.assertIn(
            "lv_obj_add_flag(ui->client_container, LV_OBJ_FLAG_HIDDEN)",
            table,
        )
        self.assertIn("LV_SYMBOL_DOWN", source)
        self.assertIn("LV_SYMBOL_UP", source)

    def test_incremental_flush_is_declared_before_popup_close_uses_it(self):
        source = read("main/main.c")
        declaration = "static void observer_flush_incremental_ui(tab_context_t *ctx);"
        popup_close = "static void close_network_popup(void)\n{"

        declaration_index = source.find(declaration)
        popup_close_index = source.index(popup_close)

        self.assertNotEqual(-1, declaration_index)
        self.assertLess(declaration_index, popup_close_index)

    def test_periodic_poll_updates_changed_tiles_without_rebuilding_the_table(self):
        source = read("main/main.c")
        poll = function_body(source, "static void observer_poll_task")
        sync = function_body(source, "static void observer_sync_changed_tiles")
        add_client = function_body(source, "static void observer_add_client_row")

        self.assertNotIn("update_observer_table(ctx)", poll)
        self.assertIn("observer_flush_incremental_ui(ctx)", poll)
        self.assertIn("rendered_client_count", sync)
        self.assertIn("observer_add_client_row", sync)
        self.assertNotIn("lv_obj_clean", sync)
        self.assertIn("lv_obj_create(ui->client_container)", add_client)

    def test_incremental_update_is_deferred_until_scrolling_finishes(self):
        source = read("main/main.c")
        flush = function_body(source, "static void observer_flush_incremental_ui")
        scroll_end = function_body(source, "static void observer_table_scroll_end_cb")
        page = function_body(source, "static void show_observer_page")

        self.assertIn("lv_obj_is_scrolling(ctx->observer_table)", flush)
        self.assertIn("LV_INDEV_STATE_PRESSED", flush)
        self.assertIn("observer_ui_refresh_pending = true", flush)
        self.assertIn("observer_sync_changed_tiles(ctx)", scroll_end)
        self.assertIn("observer_table_scroll_end_cb", page)
        self.assertIn("LV_EVENT_SCROLL_END", page)
        self.assertIn("observer_touch_release_cb", source)
        self.assertIn("observer_touch_release_cb,", source)
        self.assertIn("LV_EVENT_RELEASED", source)


class ObserverVendorResolutionContractTests(unittest.TestCase):
    def test_poll_tasks_pick_the_vendor_query_when_enabled(self):
        source = read("main/main.c")
        observer_poll = function_body(source, "static void observer_poll_task")
        popup_poll = function_body(source, "static void popup_poll_task")

        for poll in (observer_poll, popup_poll):
            # Both the plain and the vendor query must remain reachable so the
            # non-vendor path still works when the toggle is off.
            self.assertIn("show_sniffer_results_vendor\\r\\n", poll)
            self.assertIn("show_sniffer_results\\r\\n", poll)
            self.assertIn("observer_resolve_vendors", poll)

    def test_client_parse_carries_vendor_into_the_data_model(self):
        source = read("main/main.c")
        client_parser = function_body(
            source, "static bool parse_sniffer_client_line"
        )
        add_client = function_body(
            source, "static bool add_client_mac(observer_network_t *net"
        )

        # Client line parser exposes a vendor out-param (checked against the
        # full source since function_body returns only the body) and only reads
        # the bracket when resolution is on.
        self.assertIn("char *vendor_out, size_t vendor_size", source)
        self.assertIn("observer_resolve_vendors", client_parser)
        self.assertIn("observer_extract_vendor_bracket", client_parser)

        # The per-client vendor is stored alongside the MAC.
        self.assertIn("char client_vendors[MAX_CLIENTS_PER_NETWORK][48]", source)
        self.assertIn("const char *vendor)", source)
        self.assertIn("net->client_vendors", add_client)

    def test_vendor_bracket_is_normalised_and_unknown_becomes_empty(self):
        source = read("main/main.c")
        extractor = function_body(
            source, "static bool observer_extract_vendor_bracket"
        )
        self.assertIn('strcmp(vendor_out, "Unknown")', extractor)
        self.assertIn("strrchr", extractor)

    def test_plain_network_parse_does_not_touch_vendor(self):
        source = read("main/main.c")
        net_parser = function_body(
            source, "static bool parse_sniffer_network_line"
        )
        # Vendor extraction on AP lines is gated behind the toggle so an SSID
        # containing brackets is never mistaken for a vendor tag.
        self.assertIn("if (observer_resolve_vendors)", net_parser)

    def test_setup_toggle_is_persisted_and_shared_with_observer(self):
        source = read("main/main.c")
        cb = function_body(source, "static void scan_setup_vendor_switch_cb")
        page = function_body(source, "static void show_scan_time_popup")
        controls = function_body(source, "static void scan_setup_update_vendor_controls")
        self.assertIn("save_observer_vendor_to_nvs", cb)
        self.assertIn("observer_apply_vendor_settings(ctx)", cb)
        apply = function_body(source, "static void observer_apply_vendor_settings")
        self.assertIn('"vendor set on"', apply)
        self.assertIn('"vendor set off"', apply)
        self.assertIn('"Show Vendors"', page)
        self.assertIn("ctx->home_vendors_present", controls)
        self.assertIn("LV_STATE_DISABLED", controls)
        self.assertNotIn("vendor_read_from_target", cb)

    def test_vendor_setting_has_its_own_nvs_key(self):
        source = read("main/main.c")
        self.assertIn('#define NVS_KEY_OBS_VENDOR', source)
        self.assertIn("load_observer_vendor_from_nvs();", source)


if __name__ == "__main__":
    unittest.main()
