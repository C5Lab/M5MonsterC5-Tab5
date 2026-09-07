"""Run actual UART receiver functions from main.c with a host UART simulator.

Usage: python3 tests/run_uart_transfer_tests.py (requires a POSIX C compiler).
The filesystem is real; only hardware, progress reporting and allocation faults
are substituted. Production functions are extracted to avoid linking the UI.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/main.c').read_text(encoding='utf-8')

def function(name):
    import re
    match = re.search(r'^static [^\n]+\b' + name + r'\(', source, re.M)
    start = match.start()
    brace = source.index('{', match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

names = ['janos_transfer_source_folder', 'janos_transfer_sanitize_filename',
         'janos_transfer_build_unique_local_path', 'janos_uart_crc32',
         'janos_uart_field_u64', 'janos_uart_header_timeout_ms']
for optional in ['janos_uart_prepare_directories', 'janos_uart_abort']:
    if 'static ' in source and optional + '(' in source:
        names.append(optional)
names.append('janos_uart_download')
fixture = (ROOT / 'tests/uart_transfer_test.c').read_text()
with tempfile.TemporaryDirectory(prefix='uart-transfer-') as tmp:
    tmp = Path(tmp)
    (tmp / 'esp_err.h').write_text('typedef int esp_err_t;\n')
    (tmp / 'test.c').write_text(fixture.replace('/* PRODUCTION */', '\n'.join(map(function, names))))
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-I' + str(tmp), '-I' + str(ROOT / 'components/janos_file_transfer/include'),
                    str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], cwd=tmp, check=True)
