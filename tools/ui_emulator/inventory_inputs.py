"""Production source paths examined by the emulator control inventory."""

from pathlib import Path


def inventory_source_paths(root: Path) -> list[Path]:
    screens = (path for path in sorted((root / 'main/screens').glob('*.c'))
               if not path.name.startswith('lv_font_'))
    return [root / 'main/main.c', root / 'main/observer_view.inc',
            root / 'main/observer_details.inc', *screens]
