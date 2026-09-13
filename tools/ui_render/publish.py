"""Verify image artifacts and publish the English render gallery and provenance."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import shutil
from PIL import Image, ImageOps, ImageDraw

OUT=Path('/out'); WORK=Path('/work'); ROOT=Path('/repo')
results=json.loads((OUT/'coverage.json').read_text())
screens=sorted({r['screen'] for r in results})
counts=Counter(r['status'] for r in results)
extraction=json.loads((WORK/'extraction.json').read_text())
extraction['source_sha256']={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
    for p in [ROOT/'main/main.c',ROOT/'lv_conf.h',*sorted((ROOT/'main/screens').glob('subghz*.c'))]}
(OUT/'provenance.json').write_text(json.dumps(extraction,indent=2)+'\n')
readme=['# Tab5 LVGL render gallery','',
    'Actual software rendering of production LVGL widgets, using the repository fonts and styles.',
    'The host adapter supplies synthetic test data and does not run radio, network, audio, or FreeRTOS tasks.', '',
    f"**{counts['rendered']} rendered frames across {len(screens)} named cases**, with 0, 90, 180, and 270 degree passes.", '',
    '[Navigation flow maps](flow/README.md) | [Portrait contact sheet](contact-sheet-0.png) | [Landscape contact sheet](contact-sheet-90.png)', '',
    '## Reading the images','',
    '- `0.png`, `90.png`, `180.png`, `270.png`: logical LVGL screenshots, upright relative to the user.',
    '- `*-native-panel.png`: expected placement on the fixed 720 x 1280 panel. These visibly distinguish all four rotations.',
    '- `rotations.png`: a comparison of all four native-panel orientations.',
    '- Native-panel placement is a host transform, not a test of ESP32 PPA output or physical touch.',
    '- Captures include the active screen and top/system layers through the LVGL flush callback.', '',
    '## Coverage and fixture limits','',
    'Cases include primary pages, settings, selected popups, and Sub-GHz pages. Some names are wrappers for the same view.',
    'This is not exhaustive coverage of every asynchronous state, callback-only dialog, or loaded PCAP analysis.',
    'The synthetic fixture has three LAB networks, an observer client, a Bluetooth device, two HTML filenames,',
    'Grove/MBus detection, SD present flags, and Red Team controls enabled. No action is executed.',
    'NVS reads use defaults. Task creation is accepted without execution; loading states can therefore remain visible.',
    'File libraries and RF results can be empty. Battery and clock placeholders reflect unavailable host hardware.',
    'The scan row builder is extracted verbatim from the production receive function and run on fixture data.', '',
    '[Machine-readable results](coverage.json) | [Source hashes and substituted functions](provenance.json)', '',
    '## Screens','', '| Case | Four rotations | Upright portrait | Upright landscape | Status |','|---|---|---|---|---|']
for screen in screens:
    rows=[r for r in results if r['screen']==screen]
    good=all(r['status']=='rendered' for r in rows)
    folder=OUT/'screens'/screen
    if good:
        sheet=Image.new('RGB',(4*192,365),'#101923'); draw=ImageDraw.Draw(sheet)
        for i,angle in enumerate((0,90,180,270)):
            p=folder/f'{angle}.png'
            with Image.open(p) as img:
                expected=(1280,720) if angle%180 else (720,1280)
                assert img.size==expected
                with Image.open(folder/f'{angle}-native-panel.png') as native:
                    assert native.size==(720,1280)
                    assert native.tobytes()==img.rotate(angle,expand=True).tobytes()
                    sheet.paste(ImageOps.contain(native,(180,320)),(i*192+6,30))
            draw.text((i*192+8,10),f'{angle} degrees',fill='white')
        sheet.save(folder/'rotations.png')
        readme.append(f'| `{screen}` | [Compare](screens/{screen}/rotations.png) | [0](screens/{screen}/0.png) / [180](screens/{screen}/180.png) | [90](screens/{screen}/90.png) / [270](screens/{screen}/270.png) | rendered |')
    else:
        status=', '.join(sorted({r['status'] for r in rows}))
        readme.append(f'| `{screen}` | - | - | - | {status} |')
readme += ['', '## Reproduction','',
    'With Docker Desktop running, execute from the repository root:', '',
    '```powershell','python tools/ui_render/run.py','```','',
    'The command builds a host image and runs the renderer with the repository mounted read-only.',
    'Generated code stays under `tools/ui_render`; outputs stay under `docs/ui-render`.',
    'The firmware source and the attached Tab5 are not modified. The runtime container is removed after the command.', '',
    '## Verification','',
    'Each render runs in a fresh process with AddressSanitizer enabled for the host adapter and Sub-GHz sources.',
    'Leak checking is disabled because each process exits after one screenshot. LVGL itself is not ASan-instrumented.',
    'The publisher checks PNG dimensions and the exact relationship between logical and native-panel images.',
    'A nonuniform content region is required for a frame to count as rendered; this is not a complete visual layout audit.', '']
(OUT/'README.md').write_text('\n'.join(readme),encoding='utf-8')
print('Published and verified:',dict(counts))
