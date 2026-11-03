#!/usr/bin/env python3
# Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
"""Renders the demo video from tools/demo/demo.c's output: each frame's recorded page (over SISTER's still art, grey
when the page drew no teal: the effect is off), a caption band, the title and end cards, and the plugin's audio.

    python3 tools/demo/render.py WORK_DIR OUT.mp4      (needs Playwright and ffmpeg)
"""
import os
import subprocess
import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

work, out = Path(sys.argv[1]), Path(sys.argv[2])
art = (Path(__file__).resolve().parents[2] / 'plugin' / 'sammi.sister' / 'sister.svg').read_text()
art = art.split('>', 1)[1].rsplit('</svg>', 1)[0]
grey = art.replace('#0B9A9E', '#585858').replace('#FFFFFF', '#CCCCCC')      # sister_page.c grey_of
caps = (work / 'caption.txt').read_text().split('\n')
frames = sorted((work / 'frames').glob('*.svg'))
W, H, FPS = 1280, 720, 30
N = len(frames)

HTML = f'''<html><head><style>
body{{margin:0;background:#000;width:{W}px;height:{H}px;overflow:hidden;font-family:Inter,sans-serif;color:#fff}}
#scr{{position:absolute;left:80px;top:24px;width:1120px;height:560px;border-radius:18px;background:#000;
     box-shadow:0 0 0 2px #1d1d22}}
#cap{{position:absolute;left:0;right:0;top:612px;text-align:center;font-size:27px;font-weight:300;color:#d8d8e0;letter-spacing:.2px}}
#card{{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;
      background:rgba(0,0,0,.82);text-align:center}}
.big{{font-size:76px;font-weight:200;letter-spacing:1px}} .mid{{font-size:34px;font-weight:300;color:#00ED95;margin-top:10px}}
.sm{{font-size:24px;font-weight:300;color:#a8a8b8;margin-top:22px;line-height:1.5}}
.t{{font-family:Inter,sans-serif;font-weight:300}}
</style></head><body>
<svg id="scr" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 320 160"><g id="art"></g><g id="live"></g></svg>
<div id="cap"></div><div id="card" style="opacity:0"></div></body></html>'''

TITLE = ('<div class="big">welcome to opus</div><div class="mid">sister &middot; a cathedral reverb</div>'
         '<div class="sm">hundekraft&rsquo;s sister, now a native opus 2 plugin<br>for the original op-1</div>')
END = ('<div class="big">sammi.sister.opmod</div><div class="mid">opus 2 &middot; beta 3.2 &middot; op-1 firmware 246</div>'
       '<div class="sm">copy it into the mods folder and restart<br>'
       'sister by hundekraft &middot; opus port by sammibabe666 &middot; gpl-3.0</div>')

jpg = work / 'jpg'
jpg.mkdir(exist_ok=True)
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page(viewport={'width': W, 'height': H})
    pg.set_content(HTML)
    last_art = None
    only = {int(x) for x in os.environ.get('ONLY', '').split(',') if x}
    for i, f in enumerate(frames):
        if only and i not in only:
            continue
        live = f.read_text()
        a = 'grey' if '#0B9A9E' not in live else 'norm'
        t = i / FPS
        cap = caps[i] if i < len(caps) else ''
        card, op = '', 0.0
        if cap == 'TITLE' or t < 4.6:
            card, op = TITLE, 1.0 if t < 3.4 else max(0.0, 1 - (t - 3.4) / 1.2)
            op = min(op, t / 0.6) if t < 0.6 else op
        elif cap == 'END':
            card, op = END, min(1.0, (t - 46.0) / 1.0)
        args = {'live': live, 'art': (grey if a == 'grey' else art) if a != last_art else None,
                'cap': '' if cap in ('TITLE', 'END') else cap, 'card': card, 'op': op}
        pg.evaluate('''a => { if (a.art !== null) document.getElementById('art').innerHTML = a.art;
            document.getElementById('live').innerHTML = a.live; document.getElementById('cap').textContent = a.cap;
            const c = document.getElementById('card'); if (c.dataset.k !== a.card) { c.innerHTML = a.card; c.dataset.k = a.card; }
            c.style.opacity = a.op; }''', args)
        last_art = a
        pg.screenshot(path=str(jpg / f'{i:04d}.jpg'), type='jpeg', quality=92)
    b.close()

if only:
    sys.exit(0)
tmp = out.with_name(out.stem + '.part.mp4')
dur = N / FPS
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(FPS), '-i', str(jpg / '%04d.jpg'),
                '-f', 's16le', '-ar', '44160', '-ac', '2', '-i', str(work / 'audio.raw'),
                '-filter_complex', f'[1:a]aresample=48000,volume=3.0,afade=t=in:d=0.3,afade=t=out:st={dur - 1.5}:d=1.5[a]',
                '-map', '0:v', '-map', '[a]', '-c:v', 'libx264', '-crf', '18', '-preset', 'veryfast', '-pix_fmt', 'yuv420p',
                '-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart', '-shortest', str(tmp)], check=True)
tmp.rename(out)
print(out)
