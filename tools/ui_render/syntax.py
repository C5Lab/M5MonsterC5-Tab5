from pathlib import Path
import subprocess
args=['gcc','-w','-fsyntax-only','-DLV_CONF_INCLUDE_SIMPLE','-I/work','-I/work/shims','-I/repo/managed_components/lvgl__lvgl','-I/repo/main/screens']
args += ['-I'+str(p) for p in Path('/repo/components').glob('*/include')]
r=subprocess.run(args+['/work/generated.c'],capture_output=True,text=True)
Path('/work/syntax.log').write_text(r.stderr)
print('\n'.join(r.stderr.splitlines()[:100]))
print('EXIT',r.returncode)
