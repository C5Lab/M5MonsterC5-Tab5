"""Generate source diagrams, NOT screenshots or pixel-accurate UI previews.

The inventory includes show functions and popup constructors. Conditional paths
are combined; labels and callbacks only include literal/direct calls in a body.
Run: python tools/generate_screen_atlas.py
"""
from pathlib import Path
import html
import json
import re
import textwrap

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/screen-atlas'


def mask_c(text):
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: re.sub(r'[^\n]', ' ', m[0]), text)


def functions(path):
    text = path.read_text(encoding='utf-8')
    masked = mask_c(text)
    pattern = r'^\s*(?:(?:static|inline|IRAM_ATTR)\s+)*(?:void|bool|int|lv_obj_t\s*\*)\s*\b(\w+)\s*\([^;{}]*\)\s*\{'
    for match in re.finditer(pattern, masked, re.M):
        begin = match.end()
        depth, end = 1, begin
        while depth and end < len(masked):
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        yield {'name': match[1], 'file': path.relative_to(ROOT).as_posix(),
               'line': text.count('\n', 0, match.start()) + 1,
               'body': text[begin:end-1], 'code': masked[begin:end-1]}


def svg(title, sections):
    rows = []
    for heading, values in sections:
        rows.append((heading, True))
        for value in values or ['—']:
            rows.extend((line, False) for line in textwrap.wrap(value, 103))
        rows.append(('', False))
    height = 125 + len(rows) * 23
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1080" height="{height}" viewBox="0 0 1080 {height}">',
             f'<title>{html.escape(title)}</title>',
             '<rect width="100%" height="100%" fill="#111827"/>',
             f'<text x="28" y="38" fill="#7dd3fc" font-family="monospace" font-size="21">{html.escape(title)}</text>',
             '<text x="28" y="69" fill="#fbbf24" font-family="sans-serif" font-size="17">SOURCE DIAGRAM - not a screenshot or screen render</text>']
    for i, (value, heading) in enumerate(rows):
        color = '#7dd3fc' if heading else '#e5e7eb'
        parts.append(f'<text x="28" y="{110+i*23}" fill="{color}" font-family="monospace" font-size="16">{html.escape(value)}</text>')
    return '\n'.join(parts + ['</svg>'])


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    items = list(functions(ROOT / 'main/main.c'))
    for path in sorted((ROOT / 'main/screens').glob('*.c')):
        items.extend(functions(path))
    screens = [f for f in items if (f['name'].startswith('show_') or
               re.search(r'(?:_show|_show_popup)$', f['name']) or
               ('popup' in f['name'] and 'lv_obj_create(' in f['code']))]
    names = {f['name'] for f in screens}
    index = ['# Screen atlas - source analysis', '',
             'These diagrams document functions that create or show UI. **They are not LVGL renders**.',
             'One function may handle several states; several functions may build one screen.',
             'Conditional paths are combined. Dimensions are source calls, not evaluated geometry.',
             'Missing text means no direct string literal was found, not that the screen is empty.', '',
             'Generator: `python tools/generate_screen_atlas.py`.', '',
             '[Rotation flow and test procedure](../Screen_Rotation_Test_Report.md)', '',
             '| Function / SVG diagram | Source |', '|---|---|']
    manifest = []
    for f in screens:
        body = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*', '', f['body'])
        calls = re.findall(r'lv_(?:label_set_text|dropdown_set_options)\s*\([^;]+;', body)
        literals = [''.join(re.findall(r'"((?:\\.|[^"\\])*)"', call)) for call in calls]
        literals = [value for value in literals if value]
        labels = list(dict.fromkeys(s.replace('\\n', ' / ') for s in literals))
        callbacks = sorted(set(re.findall(r'lv_obj_add_event_cb\s*\([^,]+,\s*(\w+)', f['code'])))
        targets = sorted(set(re.findall(r'\b(\w+)\s*\(', f['code'])) & names)
        dimensions = [re.sub(r'\s+', ' ', s) for s in re.findall(r'lv_obj_set_(?:size|width|height)\s*\([^;]+;', body)]
        filename = Path(f['file']).stem + '--' + f['name'] + '.svg'
        (OUT/filename).write_text(svg(f['name'], [
            ('SOURCE', [f"{f['file']}:{f['line']}"]),
            ('DIRECT TEXT LITERALS IN THIS FUNCTION', labels),
            ('SOURCE DIMENSIONS (UNEVALUATED EXPRESSIONS)', dimensions),
            ('EVENTS -> CALLBACKS', callbacks),
            ('DIRECT UI FUNCTION CALLS', targets),
        ]), encoding='utf-8')
        index.append(f"| [{f['name']}]({filename}) | [{f['file']}:{f['line']}](../../{f['file']}) |")
        manifest.append({k: f[k] for k in ('name', 'file', 'line')} |
                        {'diagram': filename, 'callbacks': callbacks, 'direct_ui_calls': targets,
                         'literal_labels': labels, 'dimension_calls': dimensions})
    photos = sorted(p for p in (ROOT/'docs/tab.stories/src').rglob('*')
                    if p.suffix.lower() in ('.png', '.jpg', '.jpeg') and p.parent.name != 'src')
    index += ['', '## Existing photographs / illustrations', '',
              'Historical material from `tab.stories`; it does not verify the current version or rotation.', '']
    for p in photos:
        link = '../' + p.relative_to(ROOT/'docs').as_posix()
        index.append(f'- [{p.parent.name} / {p.stem}](<{link}>)')
    (OUT/'README.md').write_text('\n'.join(index)+'\n', encoding='utf-8')
    (OUT/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'{len(screens)} source diagrams; {len(photos)} historical image references; {OUT}')


if __name__ == '__main__':
    main()
