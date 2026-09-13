#!/usr/bin/env python3
"""Generate source-backed Mermaid and Graphviz UI navigation maps.

Run from the repository root:
    python tools/ui_flow/generate_ui_flow.py

The generator fails when an evidence anchor can no longer be found inside the
named C function. This keeps the documentation tied to the implementation.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import html
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "ui-render" / "flow"


@dataclass(frozen=True)
class Edge:
    source: str
    target: str
    label: str
    kind: str
    file: str
    function: str
    anchor: str


@dataclass(frozen=True)
class Diagram:
    slug: str
    title: str
    summary: str
    nodes: dict[str, str]
    edges: tuple[Edge, ...]


def e(source: str, target: str, label: str, kind: str, file: str,
      function: str, anchor: str) -> Edge:
    return Edge(source, target, label, kind, file, function, anchor)


MAIN = "main/main.c"

DIAGRAMS = (
    Diagram(
        "01-overview",
        "UI navigation overview",
        "Boot, persistent transport tabs, the shared dashboard, and the two major feature hubs.",
        {
            "boot": "Firmware boot",
            "splash": "Boot intro",
            "no_board": "No-board modal",
            "shell": "Persistent tab shell",
            "uart": "Grove / USB dashboard",
            "mbus": "M-BUS dashboard",
            "internal": "Internal tools",
            "wifi": "Wi-Fi operations",
            "data": "Compromised data / PCAP",
            "bt": "Bluetooth",
            "subghz": "Sub-GHz",
            "settings": "Device settings",
        },
        (
            e("boot", "splash", "automatic: initialize LVGL", "auto", MAIN, "app_main", "show_splash_screen();"),
            e("splash", "no_board", "conditional: no Monster detected", "condition", MAIN, "boot_detection_done_async", "show_no_board_popup();"),
            e("splash", "shell", "automatic: board detected", "auto", MAIN, "boot_detection_done_async", "show_main_tiles();"),
            e("shell", "uart", "tap Grove / USB tab", "click", MAIN, "tab_click_cb", "show_uart1_tiles();"),
            e("shell", "mbus", "tap M-BUS tab", "click", MAIN, "tab_click_cb", "show_mbus_tiles();"),
            e("shell", "internal", "tap Internal tab", "click", MAIN, "tab_click_cb", "show_internal_tiles();"),
            e("uart", "wifi", "tap WiFi / attacks / observer", "click", MAIN, "main_tile_event_cb", "show_scan_page();"),
            e("uart", "data", "tap Compromised Data", "click", MAIN, "main_tile_event_cb", "show_compromised_data_page();"),
            e("uart", "bt", "tap Bluetooth", "click", MAIN, "main_tile_event_cb", "show_bluetooth_menu_page();"),
            e("uart", "subghz", "tap Sub-GHz", "click", MAIN, "main_tile_event_cb", "show_subghz_page();"),
            e("internal", "settings", "tap Settings", "click", MAIN, "internal_tile_event_cb", "show_settings_page();"),
        ),
    ),
    Diagram(
        "02-wifi-operations",
        "Wi-Fi operations and attack routes",
        "Dashboard entry points, selected-network actions, Observer popups, and global attack dialogs.",
        {
            "home": "Transport dashboard",
            "scan": "WiFi Scan & Attack",
            "observer": "Network Observer",
            "net_popup": "Network action popup",
            "client_popup": "Client deauth popup",
            "global": "Global WiFi Attacks",
            "deauth": "Deauth active popup",
            "evil": "Evil Twin setup / active",
            "sae": "SAE Overflow active",
            "handshake": "Handshaker popup",
            "arp": "ARP Poison page",
            "mitm": "MITM setup / active",
            "gitm": "Capture Gateway page",
            "rogue_gitm": "Rogue GITM popup",
            "nmap": "Nmap page / result popups",
            "rogue_ap": "Rogue AP page / popup",
            "radar": "AP Radar",
            "sd": "SD-card warning",
            "blackout": "Blackout confirm / active",
            "sniffer": "SnifferDog confirm / active",
            "global_hs": "Global Handshaker confirm / active",
            "portal": "Phishing Portal setup / active",
            "beacon": "Beacon Spam menu",
            "ssids": "Beacon SSID list / editor",
            "wardrive": "Wardrive page / overlays",
        },
        (
            e("home", "scan", "tap WiFi Scan & Attack", "click", MAIN, "main_tile_event_cb", "show_scan_page();"),
            e("home", "observer", "tap Network Observer", "click", MAIN, "main_tile_event_cb", "show_observer_page();"),
            e("home", "global", "tap Global WiFi Attacks", "click", MAIN, "main_tile_event_cb", "show_global_attacks_page();"),
            e("observer", "net_popup", "tap network row", "click", MAIN, "network_row_click_cb", "show_network_popup(network_idx);"),
            e("observer", "client_popup", "tap client row; Red Team required", "condition", MAIN, "client_row_click_cb", "show_deauth_popup(network_idx, client_idx);"),
            e("scan", "deauth", "tap Deauth; one or more selected", "condition", MAIN, "handle_selected_attack", "show_scan_deauth_popup();"),
            e("scan", "sd", "Evil Twin without SD", "condition", MAIN, "handle_selected_attack", "show_sd_warning_popup(show_evil_twin_popup);"),
            e("scan", "evil", "tap Evil Twin", "click", MAIN, "handle_selected_attack", "show_evil_twin_popup();"),
            e("scan", "sae", "tap SAE Overflow; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_sae_popup(idx);"),
            e("scan", "handshake", "tap Handshaker", "click", MAIN, "handle_selected_attack", "show_handshaker_popup();"),
            e("scan", "arp", "tap ARP Poison; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_arp_poison_page();"),
            e("scan", "mitm", "tap MITM; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_mitm_popup();"),
            e("scan", "gitm", "tap Capture GW", "click", MAIN, "handle_selected_attack", "show_gitm_page_from_scan();"),
            e("scan", "rogue_gitm", "tap Rogue GITM; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_rogue_gitm_popup(ctx, vic);"),
            e("scan", "nmap", "tap Nmap; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_nmap_page();"),
            e("scan", "rogue_ap", "tap Rogue AP; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_rogue_ap_page();"),
            e("scan", "radar", "tap AP Radar; exactly one AP", "condition", MAIN, "handle_selected_attack", "show_ap_radar_page(idx);"),
            e("global", "blackout", "tap Blackout", "click", MAIN, "global_attack_tile_event_cb", "show_blackout_confirm_popup();"),
            e("global", "sniffer", "tap SnifferDog", "click", MAIN, "global_attack_tile_event_cb", "show_snifferdog_confirm_popup();"),
            e("global", "global_hs", "tap Handshaker", "click", MAIN, "global_attack_tile_event_cb", "show_global_handshaker_confirm_popup();"),
            e("global", "sd", "Portal / Wardrive without SD", "condition", MAIN, "global_attack_tile_event_cb", "show_sd_warning_popup(show_phishing_portal_popup);"),
            e("global", "portal", "tap Portal", "click", MAIN, "global_attack_tile_event_cb", "show_phishing_portal_popup();"),
            e("global", "wardrive", "tap Wardrive", "click", MAIN, "global_attack_tile_event_cb", "show_wardrive_page();"),
            e("global", "beacon", "tap Beacon Spam", "click", MAIN, "global_attack_tile_event_cb", "show_beacon_spam_page();"),
            e("global", "gitm", "tap GITM", "click", MAIN, "global_attack_tile_event_cb", "show_gitm_page();"),
            e("beacon", "ssids", "tap List SSIDs", "click", MAIN, "beacon_spam_tile_event_cb", "show_beacon_ssids_page();"),
            e("beacon", "global", "Back", "back", MAIN, "beacon_spam_back_btn_event_cb", "show_global_attacks_page();"),
        ),
    ),
    Diagram(
        "03-data-and-pcap",
        "Compromised data and PCAP analysis",
        "Remote file categories, ESPShark's two storage paths, and the local PCAP analysis drill-downs.",
        {
            "home": "Transport dashboard",
            "hub": "Compromised Data",
            "evil": "Evil Twin Passwords",
            "portal": "Portal Data",
            "hs": "Handshake files",
            "wardrive": "Wardrive files",
            "espshark": "ESPShark hub",
            "remote": "Monster PCAP captures",
            "transfer": "Copy / sync progress popup",
            "local": "Tab5 PCAP browser",
            "analysis": "PCAP packet analysis",
            "packet": "Packet detail",
            "summary": "Capture / DNS summaries",
            "flows": "Connections / protocols",
            "entities": "Devices / remote endpoints",
            "health": "Investigation / capture health",
            "map": "Network map",
            "tools": "Tools popup",
            "objects": "Extracted objects",
            "delete": "Delete / cleanup confirm",
        },
        (
            e("home", "hub", "tap Compromised Data", "click", MAIN, "main_tile_event_cb", "show_compromised_data_page();"),
            e("hub", "evil", "tap Evil Twin Passwords", "click", MAIN, "compromised_data_tile_event_cb", "show_evil_twin_passwords_page();"),
            e("hub", "portal", "tap Portal Data", "click", MAIN, "compromised_data_tile_event_cb", "show_portal_data_page();"),
            e("hub", "hs", "tap Handshakes", "click", MAIN, "compromised_data_tile_event_cb", "show_handshakes_page();"),
            e("hub", "wardrive", "tap Wardrive Files", "click", MAIN, "compromised_data_tile_event_cb", "show_wardrive_files_page();"),
            e("hub", "espshark", "tap ESPShark", "click", MAIN, "compromised_data_tile_event_cb", "show_espshark_page();"),
            e("espshark", "remote", "tap Monster SD", "click", MAIN, "espshark_action_btn_event_cb", "show_pcap_captures_page();"),
            e("espshark", "local", "tap Tab5 SD", "click", MAIN, "espshark_action_btn_event_cb", "show_pcap_viewer_page();"),
            e("remote", "transfer", "tap Copy / Sync", "click", MAIN, "compromised_file_copy_cb", "compromised_transfer_show_popup"),
            e("remote", "delete", "tap Delete / Clean", "click", MAIN, "compromised_file_delete_cb", "show_compromised_delete_confirm"),
            e("local", "analysis", "tap PCAP file", "click", MAIN, "pcap_viewer_file_open_cb", "pcap_viewer_start_file_load"),
            e("analysis", "local", "Back releases capture", "back", MAIN, "pcap_viewer_back_cb", "show_pcap_viewer_page();"),
            e("local", "espshark", "Back with no capture open", "back", MAIN, "pcap_viewer_back_cb", "show_espshark_page();"),
            e("analysis", "packet", "tap packet row", "click", MAIN, "pcap_viewer_packet_detail_cb", "detail_overlay = lv_obj_create"),
            e("analysis", "summary", "tap summary / DNS", "click", MAIN, "pcap_viewer_summary_cb", "pcap_viewer_show_dns_top"),
            e("analysis", "flows", "tap Connections", "click", MAIN, "pcap_viewer_connections_cb", "pcap_viewer_create_analysis_list"),
            e("analysis", "flows", "tap Protocols", "click", MAIN, "pcap_viewer_protocols_cb", "pcap_viewer_create_analysis_list"),
            e("analysis", "entities", "tap Devices", "click", MAIN, "pcap_viewer_devices_cb", "pcap_viewer_render_inventory"),
            e("analysis", "entities", "tap Remote endpoints", "click", MAIN, "pcap_viewer_remote_endpoints_cb", "pcap_viewer_render_inventory"),
            e("analysis", "health", "tap Health / Investigation", "click", MAIN, "pcap_viewer_health_cb", "pcap_viewer_render_investigation"),
            e("analysis", "map", "tap Map", "click", MAIN, "pcap_viewer_map_cb", "detail_overlay = lv_obj_create"),
            e("analysis", "tools", "tap Tools", "click", MAIN, "pcap_viewer_tools_cb", "artifact_overlay = lv_obj_create"),
            e("analysis", "objects", "tap Objects", "click", MAIN, "pcap_viewer_objects_cb", "pcap_viewer_start_extraction"),
        ),
    ),
    Diagram(
        "04-internal-and-bluetooth",
        "Internal tools, settings, and Bluetooth",
        "The Internal tab's two tiles, all Settings destinations, and Bluetooth's scanner drill-down.",
        {
            "shell": "Persistent tab shell",
            "internal": "Internal tools",
            "adhoc": "Ad Hoc Portal & Karma",
            "settings": "Settings",
            "scan_cfg": "Scan Setup popup",
            "red": "Red Team page / disclaimer",
            "timeout": "Screen Timeout popup",
            "brightness": "Screen Brightness popup",
            "rotation": "Screen Rotation popup",
            "theme": "Theme popup",
            "time": "Time popup",
            "lock": "Screen Lock popup",
            "ota": "Monster OTA page / modals",
            "sdadmin": "Monster SD Admin / leave dialog",
            "baud": "Transfer Speed popup",
            "home": "Transport dashboard",
            "bt": "Bluetooth menu",
            "airtag": "AirTag Scan",
            "btscan": "BT Scan & Locate",
            "locator": "BT Locator",
            "jammer": "2.4 GHz Jammer",
        },
        (
            e("shell", "internal", "tap Internal tab", "click", MAIN, "tab_click_cb", "show_internal_tiles();"),
            e("internal", "settings", "tap Settings", "click", MAIN, "internal_tile_event_cb", "show_settings_page();"),
            e("internal", "adhoc", "tap Ad Hoc Portal & Karma", "click", MAIN, "internal_tile_event_cb", "show_adhoc_portal_page();"),
            e("settings", "scan_cfg", "tap Scan Setup", "click", MAIN, "settings_tile_event_cb", "show_scan_time_popup();"),
            e("settings", "red", "tap Red Team", "click", MAIN, "settings_tile_event_cb", "show_red_team_settings_page();"),
            e("settings", "timeout", "tap Screen Timeout", "click", MAIN, "settings_tile_event_cb", "show_screen_timeout_popup();"),
            e("settings", "brightness", "tap Screen Brightness", "click", MAIN, "settings_tile_event_cb", "show_screen_brightness_popup();"),
            e("settings", "rotation", "tap Screen Rotation", "click", MAIN, "settings_tile_event_cb", "show_screen_rotation_popup();"),
            e("settings", "theme", "tap Theme", "click", MAIN, "settings_tile_event_cb", "show_theme_popup();"),
            e("settings", "time", "tap Time", "click", MAIN, "settings_tile_event_cb", "show_time_popup();"),
            e("settings", "lock", "tap Screen Lock", "click", MAIN, "settings_tile_event_cb", "show_screen_lock_popup();"),
            e("settings", "ota", "tap Monster OTA", "click", MAIN, "settings_tile_event_cb", "show_ota_page();"),
            e("settings", "sdadmin", "tap Monster SD Admin", "click", MAIN, "settings_tile_event_cb", "show_sd_admin_page();"),
            e("settings", "baud", "tap Transfer Speed", "click", MAIN, "settings_tile_event_cb", "show_ft_baud_popup();"),
            e("home", "bt", "tap Bluetooth", "click", MAIN, "main_tile_event_cb", "show_bluetooth_menu_page();"),
            e("bt", "airtag", "tap AirTag Scan", "click", MAIN, "bt_menu_tile_event_cb", "show_airtag_scan_page();"),
            e("bt", "btscan", "tap BT Scan & Locate", "click", MAIN, "bt_menu_tile_event_cb", "show_bt_scan_page();"),
            e("bt", "jammer", "tap Jammer; Red Team tile only", "condition", MAIN, "bt_menu_tile_event_cb", "show_jammer_page();"),
            e("btscan", "locator", "tap discovered device", "click", MAIN, "bt_scan_device_click_cb", "show_bt_locator_page(device_idx);"),
            e("btscan", "btscan", "tap Rescan", "click", MAIN, "bt_scan_rescan_cb", "show_bt_scan_page();"),
        ),
    ),
    Diagram(
        "05-subghz",
        "Sub-GHz navigation and modal routes",
        "The Sub-GHz hub, tool settings, scanner-to-listen handoff, and signal action dialogs.",
        {
            "home": "Transport dashboard",
            "hub": "Sub-GHz",
            "scan": "Quick Scan",
            "scan_cfg": "Scanner Settings",
            "hunter": "Hunter",
            "hunter_cfg": "Hunter Settings",
            "listen": "Listen",
            "listen_cfg": "Listen Settings",
            "manage": "SD Signals",
            "weather": "Weather",
            "jammer": "Sub-GHz Jammer",
            "tesla": "Tesla",
            "settings": "Sub-GHz Settings",
            "listen_action": "Captured signal actions",
            "listen_leave": "Leave with captures?",
            "tx_warn": "Transmit warning",
            "hunter_action": "Hunter capture actions",
            "hunter_leave": "Leave with captures?",
            "manage_action": "Saved signal actions",
            "rename": "Rename text input",
            "delete": "Delete confirmation",
            "repeat": "Transmit count",
        },
        (
            e("home", "hub", "tap Sub-GHz", "click", MAIN, "main_tile_event_cb", "show_subghz_page();"),
            e("hub", "scan", "tap Quick Scan", "click", "main/screens/subghz_screen.c", "on_scanner", "show_subghz_scanner_page();"),
            e("hub", "hunter", "tap Hunter", "click", "main/screens/subghz_screen.c", "on_hunter", "show_subghz_hunter_page();"),
            e("hub", "listen", "tap Listen", "click", "main/screens/subghz_screen.c", "on_listen", "show_subghz_listen_page();"),
            e("hub", "manage", "tap SD Signals", "click", "main/screens/subghz_screen.c", "on_manage", "show_subghz_manage_page();"),
            e("hub", "weather", "tap Weather", "click", "main/screens/subghz_screen.c", "on_weather", "show_subghz_weather_page();"),
            e("hub", "jammer", "tap Jammer", "click", "main/screens/subghz_screen.c", "on_jammer", "show_subghz_jammer_page();"),
            e("hub", "tesla", "tap Tesla", "click", "main/screens/subghz_screen.c", "on_tesla", "show_subghz_tesla_page();"),
            e("hub", "settings", "tap Settings", "click", "main/screens/subghz_screen.c", "on_settings", "show_subghz_settings_page();"),
            e("scan", "scan_cfg", "tap Settings", "click", "main/screens/subghz_scanner_screen.c", "on_settings", "show_subghz_scanner_settings_page();"),
            e("scan_cfg", "scan", "Back", "back", "main/screens/subghz_scanner_settings_screen.c", "on_back", "show_subghz_scanner_page();"),
            e("scan", "listen", "tap result; prefill + auto-start", "condition", "main/screens/subghz_scanner_screen.c", "on_tile_clicked", "show_subghz_listen_page_at(freq, true);"),
            e("hunter", "hunter_cfg", "tap Settings", "click", "main/screens/subghz_hunter_screen.c", "on_settings", "show_subghz_hunter_settings_page();"),
            e("hunter_cfg", "hunter", "Back; resume captures", "back", "main/screens/subghz_hunter_settings_screen.c", "on_back", "show_subghz_hunter_page_resume();"),
            e("listen", "listen_cfg", "tap Settings", "click", "main/screens/subghz_listen_screen.c", "on_settings", "show_subghz_listen_settings_page();"),
            e("listen_cfg", "listen", "Back", "back", "main/screens/subghz_listen_settings_screen.c", "on_back", "show_subghz_listen_page();"),
            e("listen", "listen_action", "tap captured signal", "click", "main/screens/subghz_listen_screen.c", "on_signal_row_clicked", "show_action_popup(st, idx);"),
            e("listen", "listen_leave", "Back with unsaved captures", "condition", "main/screens/subghz_listen_screen.c", "on_back", "show_leave_popup(st, total);"),
            e("listen_action", "tx_warn", "tap Transmit", "click", "main/screens/subghz_listen_screen.c", "on_action_transmit", "show_tx_warn_popup(st);"),
            e("hunter", "hunter_action", "tap captured signal", "click", "main/screens/subghz_hunter_screen.c", "on_row_click", "show_action_popup(st, idx);"),
            e("hunter", "hunter_leave", "Back with captures", "condition", "main/screens/subghz_hunter_screen.c", "on_back", "show_leave_popup(st, count);"),
            e("manage", "manage_action", "tap saved signal", "click", "main/screens/subghz_manage_screen.c", "on_row_click", "show_action_popup(st, idx);"),
            e("manage_action", "rename", "tap Rename", "click", "main/screens/subghz_manage_screen.c", "on_action_rename", "subghz_show_text_input_popup"),
            e("manage_action", "delete", "tap Delete", "click", "main/screens/subghz_manage_screen.c", "on_action_delete", "show_delete_confirm"),
            e("manage_action", "repeat", "tap Transmit", "click", "main/screens/subghz_manage_screen.c", "on_action_transmit", "show_tx_count_popup"),
            e("scan", "hub", "Back", "back", "main/screens/subghz_scanner_screen.c", "on_back", "show_subghz_page();"),
            e("hunter", "hub", "Back / confirmed leave", "back", "main/screens/subghz_hunter_screen.c", "perform_back", "show_subghz_page();"),
            e("listen", "hub", "Back / confirmed leave", "back", "main/screens/subghz_listen_screen.c", "perform_back", "show_subghz_page();"),
            e("manage", "hub", "Back", "back", "main/screens/subghz_manage_screen.c", "on_back", "show_subghz_page();"),
        ),
    ),
)


COLORS = {
    "click": ("#e8f3ff", "#1677c8"),
    "auto": ("#eaf8ef", "#238636"),
    "condition": ("#fff4da", "#b26a00"),
    "back": ("#f2edff", "#7655b7"),
}


def function_span(text: str, name: str) -> tuple[int, int]:
    pat = re.compile(
        rf"(?m)^\s*(?:static\s+)?[A-Za-z_][\w\s\*]*?\b{re.escape(name)}\s*"
        rf"\([^;{{}}]*\)\s*\{{"
    )
    match = pat.search(text)
    if not match:
        raise ValueError(f"function definition not found: {name}")
    brace = text.find("{", match.start(), match.end())
    depth = 0
    quote = None
    escaped = False
    line_comment = False
    block_comment = False
    i = brace
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if ch == "\n":
                line_comment = False
        elif block_comment:
            if ch == "*" and nxt == "/":
                block_comment = False
                i += 1
        elif quote:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
        elif ch == "/" and nxt == "/":
            line_comment = True
            i += 1
        elif ch == "/" and nxt == "*":
            block_comment = True
            i += 1
        elif ch in ('"', "'"):
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return match.start(), i + 1
        i += 1
    raise ValueError(f"unterminated function: {name}")


def resolve(edge: Edge) -> int:
    path = ROOT / edge.file
    text = path.read_text(encoding="utf-8", errors="replace")
    start, end = function_span(text, edge.function)
    pos = text.find(edge.anchor, start, end)
    if pos < 0:
        raise ValueError(
            f"evidence anchor not found in {edge.file}:{edge.function}: {edge.anchor!r}"
        )
    return text.count("\n", 0, pos) + 1


def mermaid(diagram: Diagram) -> str:
    lines = ["flowchart LR"]
    for node, label in diagram.nodes.items():
        lines.append(f'    {node}["{label}"]')
    for edge in diagram.edges:
        safe = edge.label.replace('"', "'")
        if edge.kind == "condition":
            lines.append(f'    {edge.source} -. "{safe}" .-> {edge.target}')
        else:
            lines.append(f'    {edge.source} -- "{safe}" --> {edge.target}')
    lines.extend([
        "    classDef screen fill:#111827,color:#f9fafb,stroke:#7dd3fc,stroke-width:1.5px;",
        "    classDef popup fill:#2a2036,color:#f9fafb,stroke:#d8b4fe,stroke-width:1.5px;",
        "    classDef condition fill:#3a2d12,color:#fff7d6,stroke:#f2b84b,stroke-width:1.5px;",
        "    class boot,splash,shell,uart,mbus,internal,home,hub,scan,observer,global,data,bt,subghz,settings,analysis,local,remote screen;",
        "    class no_board,net_popup,client_popup,deauth,evil,sae,handshake,mitm,rogue_gitm,blackout,sniffer,global_hs,portal,transfer,packet,summary,flows,entities,health,map,tools,objects,delete,scan_cfg,red,timeout,brightness,rotation,theme,time,lock,ota,sdadmin,baud,listen_action,listen_leave,tx_warn,hunter_action,hunter_leave,manage_action,rename,repeat popup;",
        "    class sd condition;",
    ])
    return "\n".join(lines) + "\n"


def dot(diagram: Diagram) -> str:
    lines = [
        "digraph ui_flow {",
        '  graph [rankdir=LR, bgcolor="#0b1020", pad="0.25", nodesep="0.35", ranksep="0.65", fontname="Arial"];',
        '  node [shape=box, style="rounded,filled", fillcolor="#182235", color="#7dd3fc", fontcolor="#f8fafc", fontname="Arial", fontsize=11, margin="0.14,0.09"];',
        '  edge [color="#9fb0c8", fontcolor="#dce7f5", fontname="Arial", fontsize=9, arrowsize=0.7];',
        f'  label="{diagram.title}"; labelloc=t; fontsize=19; fontcolor="#ffffff";',
    ]
    for node, label in diagram.nodes.items():
        lines.append(f'  {node} [label="{label}"];')
    for edge in diagram.edges:
        color = COLORS[edge.kind][1]
        style = "dashed" if edge.kind == "condition" else "solid"
        label = edge.label.replace('"', r'\"')
        lines.append(
            f'  {edge.source} -> {edge.target} [label="{label}", color="{color}", style="{style}"];'
        )
    lines.append("}")
    return "\n".join(lines) + "\n"


def write_readme(resolved: dict[Edge, int]) -> None:
    links = "\n".join(
        f"- [{d.title}]({d.slug}.svg) — [Mermaid source]({d.slug}.mmd), [Graphviz source]({d.slug}.dot)"
        for d in DIAGRAMS
    )
    text = f"""# UI navigation flow maps

These maps are derived from the LVGL screen constructors and callbacks in `main/main.c` and `main/screens/*.c`. They document navigation behavior; the screenshots in the sibling UI-render documentation show the actual rendered appearance.

{links}

## Reading the maps

- Blue solid edges are direct user navigation.
- Green solid edges are automatic calls or lifecycle transitions.
- Amber dashed edges require a condition, such as an SD card, Red Team mode, a board detection result, or a selection count.
- Purple solid edges are explicit Back or close routes.
- A node that names multiple states, such as “setup / active,” groups modal states that belong to one feature so the maps remain readable.

Source locations are listed in [source-evidence.md](source-evidence.md). The generator resolves every evidence anchor inside its named function and fails if source drift makes a claim unverifiable.

## Regeneration

```powershell
python tools/ui_flow/generate_ui_flow.py
```

If Graphviz `dot` is on `PATH`, SVG files are rendered automatically. The checked-in SVGs were rendered with Graphviz in a disposable Docker container.
"""
    (OUT / "README.md").write_text(text, encoding="utf-8")


def write_evidence(resolved: dict[Edge, int]) -> None:
    parts = [
        "# UI navigation source evidence\n\n",
        "Each row identifies the callback or lifecycle function that performs the route. Line numbers are regenerated from an anchor found inside that function. `condition` rows are intentionally drawn as dashed routes.\n\n",
    ]
    for diagram in DIAGRAMS:
        parts.append(f"## {diagram.title}\n\n")
        parts.append("| Route | Type | Trigger / condition | Source |\n|---|---|---|---|\n")
        for edge in diagram.edges:
            line = resolved[edge]
            route = f"{diagram.nodes[edge.source]} → {diagram.nodes[edge.target]}"
            source = f"`{edge.file}:{line}` — `{edge.function}()`"
            parts.append(f"| {route} | `{edge.kind}` | {edge.label} | {source} |\n")
        parts.append("\n")
    (OUT / "source-evidence.md").write_text("".join(parts), encoding="utf-8")


def render_with_dot() -> bool:
    dot_bin = shutil.which("dot")
    if not dot_bin:
        return False
    for diagram in DIAGRAMS:
        subprocess.run(
            [dot_bin, "-Tsvg", str(OUT / f"{diagram.slug}.dot"), "-o", str(OUT / f"{diagram.slug}.svg")],
            check=True,
        )
    return True


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    resolved: dict[Edge, int] = {}
    for diagram in DIAGRAMS:
        for edge in diagram.edges:
            resolved[edge] = resolve(edge)
        (OUT / f"{diagram.slug}.mmd").write_text(mermaid(diagram), encoding="utf-8")
        (OUT / f"{diagram.slug}.dot").write_text(dot(diagram), encoding="utf-8")
    write_readme(resolved)
    write_evidence(resolved)
    rendered = render_with_dot()
    print(f"Validated {len(resolved)} source-backed routes across {len(DIAGRAMS)} diagrams.")
    print("Rendered SVG with local Graphviz." if rendered else "Graphviz not found; generated DOT and Mermaid sources.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
