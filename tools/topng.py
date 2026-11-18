#!/usr/bin/env python3
"""Saves every .ppm in DIR as a PNG in OUT, named PREFIX + its name.
Usage: topng.py DIR OUT [PREFIX]"""
import sys
from pathlib import Path

from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
prefix = sys.argv[3] if len(sys.argv) > 3 else ''
out.mkdir(parents=True, exist_ok=True)
for f in sorted(src.glob('*.ppm')):
    Image.open(f).save(out / f'{prefix}{f.stem}.png')
