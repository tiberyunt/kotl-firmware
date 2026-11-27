#!/usr/bin/env python3
"""Rasterize the bundled OFL Noto Sans Mono into an antialiased firmware atlas.

Requires Pillow only when regenerating; ordinary firmware builds use ascii20.bin.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / 'firmware/kotl/font'
SCALE = 4
font = ImageFont.truetype(str(FONT / 'NotoSansMono-Regular.ttf'), 20 * SCALE)
atlas = bytearray()
for code in range(32, 127):
    cell = Image.new('L', (12 * SCALE, 28 * SCALE))
    ImageDraw.Draw(cell).text((0, 22 * SCALE), chr(code), font=font, fill=255, anchor='ls')
    cell = cell.resize((12, 28), Image.Resampling.LANCZOS)
    atlas.extend(cell.tobytes())
(FONT / 'ascii20.bin').write_bytes(atlas)
print(f'{len(atlas)} bytes: ASCII 32..126, 12x28 cells, 8-bit grayscale coverage')
