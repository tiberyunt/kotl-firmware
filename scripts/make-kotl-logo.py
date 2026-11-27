#!/usr/bin/env python3
"""Encode the supplied dark Kotl logo for the Bochs framebuffer."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent.parent
with Image.open(root / 'kotl-logo/kotl-logo-dark.png') as source:
    grayscale = source.convert('L')
    logo = grayscale.crop(grayscale.getbbox()).resize((288, 106), Image.Resampling.LANCZOS)
    (root / 'firmware/kotl/font/logo-gray.bin').write_bytes(logo.tobytes())
