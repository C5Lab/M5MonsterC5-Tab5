"""Host tests for sniffer_clients_model. No ESP-IDF required."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ModelTests(unittest.TestCase):
 def test_real_model(self):
  with tempfile.TemporaryDirectory() as tmp:
    executable=str(Path(tmp)/'clients')
    includes = []
    sources = []
    for candidate in [
        ROOT/'main'/'screens',
        ROOT/'main',
        ROOT/'components'/'janos_client'/'include',
        ROOT/'components'/'janos_client',
        ROOT/'src',
    ]:
        if (candidate/'sniffer_clients_model.h').exists() or (candidate/'sniffer_clients_model.c').exists():
            includes.append(str(candidate))
        c = candidate/'sniffer_clients_model.c'
        if c.exists():
            sources.append(str(c))
    cmd=['cc','-std=c11','-Wall','-Wextra','-Werror']
    for inc in includes:
        cmd.extend(['-I',inc])
    cmd.extend(sources+[str(ROOT/'tests'/'sniffer_clients_model_test.c'),'-o',executable])
    subprocess.run(cmd,check=True)
    self.assertIn('ADV identity model PASS',subprocess.check_output([executable],text=True))
