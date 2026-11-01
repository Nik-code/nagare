#!/usr/bin/env python3
"""Builds resources/nagare.ico from docs/nagare/icon.png. Run with: uv run --with pillow make_icon.py"""

from pathlib import Path

from PIL import Image

SIZES = [16, 20, 24, 32, 48, 64, 256]
# The Mac icon sits inside Apple's margin, which on Windows would only make it look small.
TILE = (46, 46, 466, 466)

here = Path(__file__).resolve().parent
source = Image.open(here.parents[1] / "docs" / "nagare" / "icon.png").convert("RGBA").crop(TILE)
images = {size: source.resize((size, size), Image.LANCZOS) for size in SIZES}
images[256].save(
    here / "nagare.ico",
    sizes=[(size, size) for size in SIZES],
    append_images=[images[size] for size in SIZES if size != 256],
)
