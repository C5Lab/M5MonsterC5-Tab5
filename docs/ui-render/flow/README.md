# UI navigation flow maps

These maps are derived from the LVGL screen constructors and callbacks in `main/main.c` and `main/screens/*.c`. They document navigation behavior; the screenshots in the sibling UI-render documentation show the actual rendered appearance.

- [UI navigation overview](01-overview.svg) — [Mermaid source](01-overview.mmd), [Graphviz source](01-overview.dot)
- [Wi-Fi operations and attack routes](02-wifi-operations.svg) — [Mermaid source](02-wifi-operations.mmd), [Graphviz source](02-wifi-operations.dot)
- [Compromised data and PCAP analysis](03-data-and-pcap.svg) — [Mermaid source](03-data-and-pcap.mmd), [Graphviz source](03-data-and-pcap.dot)
- [Internal tools, settings, and Bluetooth](04-internal-and-bluetooth.svg) — [Mermaid source](04-internal-and-bluetooth.mmd), [Graphviz source](04-internal-and-bluetooth.dot)
- [Sub-GHz navigation and modal routes](05-subghz.svg) — [Mermaid source](05-subghz.mmd), [Graphviz source](05-subghz.dot)

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
