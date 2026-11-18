#!/usr/bin/env python3
"""Turns render-test's PGMs into PNGs and compares them with stored ones.

  refcmp.py PGMDIR OUTDIR REFDIR [--update] [NAME ...]
Every PGM in PGMDIR becomes OUTDIR/NAME.png: 8-bit ones with the master
palette, 2-bit ones in the greys the IIIx shows, plus a 3x copy of each
160 one (NAME-x3.png) for looking at. NAMEs listed (default all) are compared
pixel for pixel with REFDIR/NAME.png; --update copies them there instead.
Exits 1 on any difference or missing reference.
"""
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mkpal   # noqa: E402

GREYS = [255, 240, 204, 0]    # Palm 2-bit values as the IIIx shows them


def read_pgm(path):
    data = path.read_bytes()
    parts, i = [], 0
    while len(parts) < 4:
        while data[i:i + 1].isspace():
            i += 1
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1
    w, h, mx = int(parts[1]), int(parts[2]), int(parts[3])
    return w, h, mx, data[i:i + w * h]


def to_png(path):
    w, h, mx, px = read_pgm(path)
    im = Image.frombytes('P', (w, h), px)
    if mx == 255:
        rgb, _, _, _ = mkpal.build()
        im.putpalette([v for c in rgb for v in c])
    else:
        im.putpalette([v for g in GREYS for v in (g, g, g)])
    return im


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    update = '--update' in sys.argv
    src, out, ref = Path(args[0]), Path(args[1]), Path(args[2])
    names = args[3:]
    out.mkdir(parents=True, exist_ok=True)
    bad = 0
    seen = set()
    for p in sorted(src.glob('*.pgm')):
        im = to_png(p)
        im.save(out / f'{p.stem}.png', optimize=True)
        if im.width == 160:
            im.convert('RGB').resize((480, 480), Image.NEAREST).save(out / f'{p.stem}-x3.png')
        if names and p.stem not in names:
            continue
        seen.add(p.stem)
        r = ref / f'{p.stem}.png'
        if update:
            ref.mkdir(parents=True, exist_ok=True)
            im.save(r, optimize=True)
            print('refcmp: updated', r)
            continue
        if not r.exists():
            print('refcmp: no reference', r)
            bad += 1
            continue
        a = Image.open(r)
        if a.size != im.size or a.mode != 'P' or a.tobytes() != im.tobytes():
            diff = sum(x != y for x, y in zip(a.tobytes(), im.tobytes())) if a.size == im.size else -1
            print(f'refcmp: {p.stem} differs from {r} ({diff} pixels)')
            bad += 1
    for n in names:
        if n not in seen:
            print(f'refcmp: no render for {n} in {src}')
            bad += 1
    if not update:
        print(f'refcmp: {"all match" if not bad else f"{bad} differ"}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
