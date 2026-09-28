"""Check C grammar only; never invoke a compiler or claim SDK validation.

Optional dependencies: tree-sitter==0.26.0, tree-sitter-c==0.24.2.
The large main.c is checked as selected function bodies, avoiding unrelated
compiler-specific constructs elsewhere in the application.
"""
from pathlib import Path
import sys
from tree_sitter import Language, Parser
import tree_sitter_c

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_wifi_analyzer_integration import function_body


def main():
    parser = Parser(Language(tree_sitter_c.language()))
    sources = {name: (ROOT / name).read_bytes() for name in (
        "main/wifi_analyzer.c", "main/wifi_analyzer.h",
        "main/wifi_analyzer_model.c", "main/wifi_analyzer_model.h",
        "main/screens/wifi_analyzer_screen.c", "main/screens/wifi_analyzer_screen.h",
        "tests/wifi_analyzer_model_test.c",
    )}
    host = (ROOT / "main/main.c").read_text(encoding="utf-8")
    for name in (
        "wa_transport_blocked", "wa_any_busy", "wa_guarded_uart_read",
        "wa_guarded_uart_write", "wa_guarded_uart_flush", "wa_guarded_usb_flush",
        "wa_page_deleted", "wa_back_to_tiles", "main_tile_event_cb",
        "wa_host_connected", "wa_legacy_busy", "wa_host_available", "wa_host_claim",
        "wa_host_release", "wa_host_read", "wa_host_write", "wa_host_flush",
        "wa_host_baud", "transport_read_bytes_tab", "transport_write_bytes_tab",
        "usb_transport_read", "usb_transport_write", "tab_click_cb",
        "hide_all_pages", "home_meta_refresh_allowed", "app_main",
        "create_uart_tiles_in_container",
    ):
        sources["main.c::" + name] = ("void check(void) {" + function_body(host, name) + "}").encode()
    errors = 0
    for name, source in sources.items():
        nodes = [parser.parse(source).root_node]
        problems = []
        while nodes:
            node = nodes.pop()
            if node.type == "ERROR" or node.is_missing:
                problems.append(f"{node.type} at {node.start_point}")
            nodes.extend(node.children)
        print(f"{name}: {len(problems)} grammar diagnostics")
        for problem in problems:
            print("  " + problem)
        errors += len(problems)
    print(f"Parsed {len(sources)} sources/fragments; {errors} diagnostics. No compilation.")
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
