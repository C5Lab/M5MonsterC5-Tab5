"""Local-only browser harness; serves only the compiled emulator directory."""
import hashlib
from contextlib import contextmanager
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'tools/ui_emulator/dist'

class QuietHandler(SimpleHTTPRequestHandler):
    # Keep asset connections alive. Closing HTTP/1.0 connections after the
    # Wasm response intermittently leaves Chromium's local transfer pending.
    protocol_version = 'HTTP/1.1'

    def log_message(self, *_args):
        pass

@contextmanager
def browser_session():
    server = ThreadingHTTPServer(('127.0.0.1', 0), partial(QuietHandler, directory=str(DIST)))
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            try:
                yield browser, f'http://127.0.0.1:{server.server_port}'
            finally:
                browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join()

def artifact_fingerprint():
    """Identify the exact build a report was produced from."""
    files = sorted(path for path in DIST.rglob('*') if path.is_file())
    names = {path: path.relative_to(DIST).as_posix() for path in files}
    return {'download_bytes': {names[path]: path.stat().st_size for path in files},
            'artifact_sha256': {names[path]: hashlib.sha256(path.read_bytes()).hexdigest() for path in files}}

def objects(page):
    return page.evaluate('''() => {
      emulator.module._emu_inspect();
      return emulatorObjects.map(obj => ({...obj,
        binding: emulator.module.UTF8ToString(emulator.module._emu_binding_id(obj.id)),
        generation: emulator.module._emu_binding_generation(obj.id)}));
    }''')

def click_text(page, text, contains=False):
    matches = [obj for obj in objects(page) if (text in obj['text'] if contains else text == obj['text'])]
    assert len(matches) == 1, (text, [(m['text'], m['x'], m['y']) for m in matches])
    click_object(page, matches[0])

def click_object(page, obj):
    canvas = page.locator('#display')
    canvas.scroll_into_view_if_needed()
    box = canvas.bounding_box()
    size = canvas.evaluate('(c) => ({width:c.width,height:c.height})')
    x, y = obj['x'] + obj['width']/2, obj['y'] + obj['height']/2
    assert 0 <= x < size['width'] and 0 <= y < size['height'], obj
    page.mouse.click(box['x'] + x*box['width']/size['width'], box['y'] + y*box['height']/size['height'])

if __name__ == '__main__':
    import json
    with browser_session() as (browser, url):
        page = browser.new_page(viewport={'width':1440,'height':1000})
        page.on('pageerror', lambda e: print('BROWSER ERROR:', e))
        page.on('console', lambda m: print('CONSOLE:', m.text) if m.type == 'error' else None)
        page.goto(url)
        try:
            page.wait_for_function('globalThis.emulator?.measurements.frames > 2', timeout=20000)
        finally:
            print(page.locator('#status').inner_text())
        print(json.dumps([o for o in objects(page) if o['text'] or o['binding']], indent=2))
        page.screenshot(path=str(ROOT/'docs/ui-emulator/prototype-home.png'), full_page=True)
