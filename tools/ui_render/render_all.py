"""Render each case in an isolated process, save logical and native-panel PNGs."""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import subprocess
import os
from PIL import Image, ImageOps, ImageDraw

OUT=Path('/out')
OUT.mkdir(exist_ok=True)
cases=json.loads(Path('/work/cases.json').read_text())

def render(case, angle):
    folder=OUT/'screens'/case
    folder.mkdir(parents=True,exist_ok=True)
    ppm=folder/f'{angle}.ppm'
    result={'screen':case,'rotation':angle,'fixture':'host defaults; hardware I/O inert'}
    try:
        p=subprocess.run(['/tmp/tab5-build/render',case,str(angle),str(ppm)],capture_output=True,text=True,timeout=6,
                         env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        result['exit_code']=p.returncode
        (folder/f'{angle}.log').write_text(p.stdout+p.stderr)
        if p.returncode: result['status']='failed'; return result
        with Image.open(ppm) as original: img=original.convert('RGB')
        expected=(1280,720) if angle%180 else (720,1280)
        assert img.size==expected, (img.size,expected)
        result['size']=img.size
        result['content_colors']=len(img.crop((0,130,*img.size)).getcolors(img.width*img.height) or [])
        result['status']='rendered' if result['content_colors']>1 else 'empty-content'
        result['sha256']=hashlib.sha256(img.tobytes()).hexdigest()
        img.save(folder/f'{angle}.png')
        native=img.rotate(angle,expand=True)
        native.save(folder/f'{angle}-native-panel.png')
        ppm.unlink()
    except subprocess.TimeoutExpired:
        result['status']='timeout'
    except Exception as e:
        result['status']='failed'; result['error']=str(e)
    return result

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    futures=[pool.submit(render,case,angle) for case in cases for angle in (0,90,180,270)]
    results=[f.result() for f in futures]
(OUT/'coverage.json').write_text(json.dumps(results,indent=2))
for angle in (0,90):
    available=[r for r in results if r['rotation']==angle and r['status']=='rendered']
    tw,th=(180,320) if angle==0 else (320,180)
    cols=6 if angle==0 else 4
    sheet=Image.new('RGB',(cols*(tw+12),((len(available)+cols-1)//cols)*(th+42)), '#20252f')
    draw=ImageDraw.Draw(sheet)
    for i,r in enumerate(available):
        x=(i%cols)*(tw+12); y=(i//cols)*(th+42)
        with Image.open(OUT/'screens'/r['screen']/f'{angle}.png') as im:
            sheet.paste(ImageOps.contain(im,(tw,th)),(x,y))
        name=r['screen'].removeprefix('show_')
        draw.text((x,y+th+3),name[:28],fill='white')
        draw.text((x,y+th+16),name[28:],fill='white')
    sheet.save(OUT/f'contact-sheet-{angle}.png')
from collections import Counter
print(dict(Counter(r['status'] for r in results)))
for r in results:
    if r['rotation']==0 and r['status']!='rendered': print(r['screen'],r['status'],r.get('exit_code'))
