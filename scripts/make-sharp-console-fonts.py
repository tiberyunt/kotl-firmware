#!/usr/bin/env python3
"""Rasterize native-size glyphs for high resolution firmware consoles."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root = Path(__file__).resolve().parent.parent
folder = root/'firmware/kotl/font'
output = bytearray()
for width, height, size, baseline in ((24, 43, 36, 35), (32, 57, 48, 47), (48, 86, 72, 70)):
    font = ImageFont.truetype(str(folder/'NotoSansMono-Regular.ttf'), size*4)
    atlas = bytearray()
    for code in range(32,127):
        image = Image.new('L', (width*4,height*4))
        ImageDraw.Draw(image).text(((width*4-font.getlength(chr(code)))//2,baseline*4),chr(code),font=font,fill=255,anchor='ls')
        atlas.extend(image.resize((width,height),Image.Resampling.LANCZOS).tobytes())
    encoded = bytearray()
    index = 0
    while index < len(atlas):
        count = 1
        while index+count < len(atlas) and atlas[index+count] == atlas[index] and count < 255:
            count += 1
        encoded.extend((count,atlas[index]));index+=count
    output.extend(len(encoded).to_bytes(4,'little'));output.extend(encoded)
(folder/'sharp-fonts.rle').write_bytes(output)
print(len(output))
