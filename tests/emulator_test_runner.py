"""Stream nested gate output while retaining it for verification reports."""
import os
import atexit
import json
from pathlib import Path
import re
import sys
import uuid
import subprocess
import threading
import time


ANSI = re.compile(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b\[[0-?]*[ -/]*[@-~]|\x1b[@-_]")
_session = None
_started = time.monotonic()
_failed = False


def plain(text):
    return ANSI.sub('', text)


def summary():
    rows = sorted((json.loads(p.read_text(encoding='utf-8'))
                   for p in _session.glob('*.result.json')), key=lambda r: r['started'])
    groups = {}
    for row in rows:
        groups.setdefault(row['section'], []).append(row)
    lines = ['','TEST SUMMARY', '=' * 78]
    for section, checks in groups.items():
        lines.append(section)
        for row in checks:
            lines.append(f"  {row['status']:11} {row['seconds']:8.2f}s  {row['name']}")
    leaves = [r for r in rows if not r['aggregate']]
    failures = [r for r in rows if r['status'] != 'PASS']
    status = 'FAIL' if failures or _failed else 'PASS'
    elapsed = time.monotonic() - _started
    lines += ['-' * 78, f"Result: {status} | scripts PASS: {sum(r['status']=='PASS' for r in leaves)} | FAIL/ERROR: {sum(r['status']!='PASS' for r in leaves)}",
              f'Total elapsed: {elapsed:.2f}s',
              'Section wrapper times include their children; do not add them together.']
    if status != 'PASS':
        lines.append('Run incomplete: checks after the failure may NOT HAVE RUN. Missing entries are not passes.')
    for row in failures:
        lines.append('Failure log: ' + row['log'])
    lines.append('Report: ' + str(_session / 'summary.json'))
    output = '\n'.join(lines) + '\n'
    (_session / 'summary.txt').write_text(output, encoding='utf-8')
    (_session / 'summary.json').write_text(json.dumps({'status':status, 'seconds':elapsed, 'checks':rows}, indent=2), encoding='utf-8')
    print(output, flush=True)


def session():
    global _session
    if _session is None:
        inherited = os.environ.get('EMULATOR_TEST_SESSION')
        if inherited:
            _session = Path(inherited)
        else:
            base = Path(os.environ.get('EMULATOR_TEST_REPORT_DIR',
                        str(Path(__file__).resolve().parents[1] / 'docs/ui-emulator/test-runs')))
            _session = (base / (time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])).resolve()
            _session.mkdir(parents=True)
            os.environ['EMULATOR_TEST_SESSION'] = str(_session)
            atexit.register(summary)
            prior = sys.excepthook
            def exception(kind, value, tb):
                global _failed
                _failed = True
                prior(kind, value, tb)
            sys.excepthook = exception
    return _session


def run_logged(command, *, cwd, **_capture_options):
    folder = session()
    label = plain(' '.join(map(str, command)))
    token = uuid.uuid4().hex
    began = time.time()
    section = Path(sys.argv[0]).stem.replace('verify_emulator_', '')
    name = plain(' '.join(Path(str(c)).name for c in command[1:] if not str(c).startswith('-')))
    logfile = folder / (token + '.log')
    print('EXEC ' + label, flush=True)
    env = dict(os.environ, PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8', NO_COLOR='1', FORCE_COLOR='0', CLICOLOR='0', TERM='dumb')
    started = time.monotonic()
    chunks = []
    code = None
    status = 'ERROR'
    try:
        with subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                              errors='replace') as process:
            def relay():
                for line in process.stdout:
                    line = plain(line)
                    chunks.append(line)
                    print(line, end='', flush=True)
    
            reader = threading.Thread(target=relay, daemon=True)
            reader.start()
            try:
                while True:
                    try:
                        code = process.wait(timeout=30)
                        break
                    except subprocess.TimeoutExpired:
                        print(f'WAIT {int(time.monotonic()-started)}s: {label}', flush=True)
                reader.join()
            except BaseException:
                process.terminate()
                process.wait()
                raise
        status = 'PASS' if code == 0 else 'FAIL'
    except KeyboardInterrupt:
        status = 'INTERRUPTED'
        raise
    except BaseException as error:
        chunks.append(plain(str(error)) + '\n')
        raise
    finally:
        output = ''.join(chunks)
        logfile.write_text(output, encoding='utf-8')
        row = dict(section=section, name=name, started=began, seconds=time.monotonic()-started,
                   status=status, exit_code=code, log=str(logfile),
                   aggregate=any(Path(str(c)).name.startswith('verify_emulator_') for c in command))
        (folder / (token + '.result.json')).write_text(json.dumps(row), encoding='utf-8')
    return subprocess.CompletedProcess(command, code, output, '')


# Register before fingerprint/preflight checks so their failures also get a summary.
if Path(sys.argv[0]).name.startswith('verify_emulator_'):
    session()
