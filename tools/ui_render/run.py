"""Rebuild all host renders with Docker Desktop. Run from any directory."""
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
work=root/'tools/ui_render'
out=root/'docs/ui-render'
out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(root/'tools/ui_flow/generate_ui_flow.py')],check=True)
subprocess.run(['docker','build','-t','tab5-lvgl-render:local',str(work)],check=True)
subprocess.run(['docker','run','--rm',
    '-v',f'{root.as_posix()}:/repo:ro',
    '-v',f'{work.as_posix()}:/work',
    '-v',f'{out.as_posix()}:/out',
    'tab5-lvgl-render:local','/work/run.sh'],check=True)
