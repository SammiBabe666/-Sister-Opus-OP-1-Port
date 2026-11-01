#!/usr/bin/env python3
# Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
"""Renders the page previews: each tests/page_preview.c fragment (the strokes and texts sister_page.c drew) over the
still art (sister.svg, unchanged), 320 x 160 on black, at 2.5x (the size of the Workshop manual's native capture).
A browser's renderer and a stand-in font, not the OP-1's: for checking geometry, colour and motion, not pixels.

    python3 tests/page_preview.py OUT_DIR      (after page_preview has written OUT_DIR/*.frag.svg; needs Playwright)
"""
import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

out = Path(sys.argv[1])
art = (Path(__file__).resolve().parents[1] / 'plugin' / 'sammi.sister' / 'sister.svg').read_text()
inner = art.split('>', 1)[1].rsplit('</svg>', 1)[0]
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page(viewport={'width': 800, 'height': 400})
    for frag in sorted(out.glob('*.frag.svg')):
        live = frag.read_text()
        html = ('<html><body style="margin:0;background:#000">'
                '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="400" viewBox="0 0 320 160">'
                '<style>.t{font-family:"DejaVu Sans",sans-serif;font-weight:300}</style>'
                f'<rect width="320" height="160" fill="#000"/>{inner}{live}</svg></body></html>')
        pg.set_content(html)
        png = out / (frag.name.replace('.frag.svg', '.png'))
        pg.screenshot(path=str(png))
        print(png)
    b.close()
