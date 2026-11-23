#!/usr/bin/env python3
"""Makes the title and menu pictures and packs them for the Palm.

Draws the logo and clubhouse board over game renders from Carrig Head's
15th fairway, then writes:
  OUT/Pic2-ID.bin   4 greys: flat greys by region, ordered dither between
  OUT/Pic8-ID.bin   8 bits on the master palette
  OUT/Pal8-1000.bin the master palette (tools/mkpal.py), RGB
  OUT/art.rcp       PilRC DATA lines for all of them
and pictures of each to PREVIEW if given. Each picture is split into bands of
rows (resource ID, ID + 1, ...) packed with PackBits, so no resource gets near
64 KB. Band layout is in src/core/gfx.h.

Text the game draws itself (PRESS ANY BUTTON, the plate's name, the menu
lines) isn't in the pictures.

Usage: mkart.py OUT [PREVIEW]
"""
import os
import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mkpal   # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
MOCK = HERE / 'mockup'
TITLE_SCENES = ROOT / 'art/title'
PIC_TITLE, PIC_MENU, PIC_PREVIEW = 1000, 1100, 1200
DANA_TITLE, DANA_MENU = 4100, 4110
FULL_SCREENS = ((320, 480, 4000, 4010), (480, 320, 4020, 4030))
BAND_ROWS = {2: 160, 8: 64}


def load_mockup():
    """The drawing functions of mock1b2.py, without its main part."""
    src = (MOCK / 'mock1b2.py').read_text()
    src = src.replace("exec(_src[:_src.index('# 16-colour')])", "exec(_src[:_src.index('# 16-colour')], globals())")
    ns = {'__file__': str(MOCK / 'mock1b2.py'), '__name__': 'mock1b2'}
    exec(src[:src.index('out = sys.argv[1]')], ns)
    return ns


def ui_colours():
    """(r, g, b) of each UI colour in src/core/uicol.h, in order."""
    text = (ROOT / 'src/core/uicol.h').read_text()
    return [tuple(int(v) for v in m) for m in re.findall(r'X\(UIC_\w+,\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+\)', text)]


def title(m, scene):
    c = m['C'](2)
    c.img.paste(scene)
    yb = m['logo'](c, y=4, size=28)
    m['ribbon'](c, yb + 1, 'CHAMPIONSHIP GOLF')
    return c


def menu_board(m, c):
    """The clubhouse board: wood frame, felt and a blank brass plate."""
    x, y, w, h = 6, 12, 148, 140
    c.rect(x + 3, y + 3, w, h, (0, 0, 0))
    m['wood'](c, x, y, w, 7, False)
    m['wood'](c, x, y + h - 7, w, 7, False)
    m['wood'](c, x, y, 7, h, True)
    m['wood'](c, x + w - 7, y, 7, h, True)
    m['bevel'](c, x, y, w, h, (190, 130, 70), (40, 18, 6))
    m['bevel'](c, x + 6, y + 6, w - 12, h - 12, (40, 18, 6), (180, 120, 64))
    m['felt'](c, x + 7, y + 7, w - 14, h - 14)
    m['plaque'](c, 38, 5, 84, 13, '')


def full_screen(m, scene):
    """Keep the clubhouse board in the centre square."""
    width, height = scene.size
    x, y = (width - 320) // 2, (height - 320) // 2
    centre = (x, y, x + 320, y + 320)

    splash = scene.copy()
    logo_y = y // 2
    logo_area = (x, logo_y, x + 320, logo_y + 320)
    splash.paste(title(m, scene.crop(logo_area)).img, (x, logo_y))

    board = m['C'](2)
    backdrop = ImageEnhance.Brightness(scene).enhance(0.38)
    board.img.paste(backdrop.crop(centre))
    menu_board(m, board)
    backdrop.paste(board.img, (x, y))
    return splash, backdrop


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
MAGENTA = (255, 0, 255)
BLACK, DARK, LIGHT, WHITE = 3, 2, 1, 0   # Palm 2-bit values


# How light each 2-bit value looks on the IIIx (Frond shows the same): the
# middle greys sit close to white, so anything darker has to dither with black.
SEEN = [(0.0, BLACK), (0.80, DARK), (0.94, LIGHT), (1.0, WHITE)]


def dither(v, x, y):
    """A level 0 (black) to 1 (white), as it should look, to a 2-bit value.
    Each step between greys is steepened so most of an area is one flat
    grey and the dither only shows where it changes."""
    v = max(0.0, min(1.0, v))
    k = 0
    while k < len(SEEN) - 2 and v > SEEN[k + 1][0]:
        k += 1
    (a, va), (b, vb) = SEEN[k], SEEN[k + 1]
    f = max(0.0, min(1.0, ((v - a) / (b - a) - 0.5) * 3.0 + 0.5))
    return vb if f > (BAYER[y % 4][x % 4] + 0.5) / 16 else va


def layer(m, draw):
    """A 160 canvas filled with magenta, after draw(c)."""
    c = m['C'](1)
    c.img.paste(MAGENTA, (0, 0, c.N, c.N))
    c.px = c.img.load()
    c.d = ImageDraw.Draw(c.img)
    draw(c)
    return c.img


def lum(c):
    return (c[0] * 30 + c[1] * 59 + c[2] * 11) / 25500


def grey_scene(name):
    image = Image.open(TITLE_SCENES / name).convert('L')
    w, h = image.size
    p = image.load()
    return [[3 - p[x, y] // 85 for x in range(w)] for y in range(h)]


def title_overlay(m, rows, ox):
    """Draw the logo and ribbon in flat greys."""
    yb = [0]
    def logo(c):
        yb[0] = m['logo'](c, y=4, size=28)
    lg = layer(m, logo).load()
    rb = layer(m, lambda c: m['ribbon'](c, yb[0] + 1, 'CHAMPIONSHIP GOLF')).load()
    for y in range(160):
        for x in range(160):
            c = rb[x, y]
            if c != MAGENTA:
                rows[y][ox + x] = WHITE if lum(c) > 0.6 else BLACK
            c = lg[x, y]
            if c != MAGENTA:
                rows[y][ox + x] = BLACK if c == (70, 30, 6) else DARK if c == (10, 10, 30) else WHITE


def title_grey(m):
    rows = grey_scene('square-grey.png')
    title_overlay(m, rows, 0)
    # A black strip for PRESS ANY BUTTON.
    for y in range(144, 157):
        rows[y] = [BLACK] * 160
    return rows


def dana_grey(m):
    rows = grey_scene('dana-grey.png')
    backdrop = [[min(BLACK, v + ((x + y) & 1)) for x, v in enumerate(row)] for y, row in enumerate(rows)]
    title_overlay(m, rows, 200)
    return rows, backdrop


def menu_grey(title):
    """The menu board for 4 greys: the title one grey darker round it, a
    black frame with a two-tone bevel, flat dark grey felt and a light grey
    plate. Text goes on flat greys only."""
    rows = [[min(BLACK, v + 1) for v in row] for row in title]
    def rect(x, y, w, h, v):
        for j in range(max(0, y), min(160, y + h)):
            for i in range(max(0, x), min(160, x + w)):
                rows[j][i] = v
    def bevel(x, y, w, h, lt, dk):
        rect(x, y, w, 1, lt); rect(x, y, 1, h, lt)
        rect(x, y + h - 1, w, 1, dk); rect(x + w - 1, y, 1, h, dk)
    x, y, w, h = 6, 12, 148, 140
    rect(x + 3, y + 3, w, h, BLACK)
    rect(x, y, w, h, BLACK)
    bevel(x, y, w, h, LIGHT, DARK)
    bevel(x + 6, y + 6, w - 12, h - 12, DARK, LIGHT)
    rect(x + 7, y + 7, w - 14, h - 14, DARK)
    rect(39, 6, 84, 13, BLACK)
    rect(38, 5, 84, 13, LIGHT)
    bevel(38, 5, 84, 13, WHITE, BLACK)
    return rows


def pack_rows(rows, depth):
    out = []
    per = 8 // depth
    for row in rows:
        b = bytearray()
        for i in range(0, len(row), per):
            v = 0
            for k in range(per):
                v = (v << depth) | (row[i + k] if i + k < len(row) else 0)
            b.append(v)
        out.append(bytes(b))
    return out


def packbits(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i
        while j < n and j - i < 128 and data[j] == data[i]:
            j += 1
        if j - i >= 3:
            out += bytes([(257 - (j - i)) & 0xFF, data[i]])
            i = j
            continue
        j = i
        while j < n and j - i < 128:
            if j + 2 < n and data[j] == data[j + 1] == data[j + 2]:
                break
            j += 1
        out.append(j - i - 1)
        out += data[i:j]
        i = j
    return bytes(out)


def unpackbits(data, size):
    out = bytearray()
    i = 0
    while len(out) < size:
        c = data[i]
        i += 1
        if c < 128:
            out += data[i:i + c + 1]
            i += c + 1
        elif c > 128:
            out += bytes([data[i]]) * (257 - c)
            i += 1
    return bytes(out)


def bands(rows, depth):
    """Band resources for packed rows."""
    res = []
    step = BAND_ROWS[depth]
    for y0 in range(0, len(rows), step):
        part = rows[y0:y0 + step]
        raw = b''.join(part)
        body = packbits(raw)
        assert unpackbits(body, len(raw)) == raw
        res.append(struct.pack('>HHHBB', y0, len(part), len(part[0]), depth, 0) + body)
    return res


def write_bands(out, rcp, pid, depth, rows):
    total = 0
    for k, data in enumerate(bands(rows, depth)):
        name = f'Pic{depth}-{pid + k}.bin'
        (out / name).write_bytes(data)
        rcp.append(f'DATA "Pic{depth}" ID {pid + k} "{out / name}"')
        total += len(data)
    return total


def rows8(img):
    raw = img.tobytes()
    return [raw[i:i + img.width] for i in range(0, len(raw), img.width)]


def palette():
    """The master palette from tools/mkpal.py, 768 RGB bytes."""
    rgb, _, _, _ = mkpal.build()
    return [v for c in rgb for v in c]


def to8(img, pal, dither=Image.Dither.FLOYDSTEINBERG):
    p = Image.new('P', (1, 1))
    p.putpalette(pal)
    q = img.convert('RGB').quantize(palette=p, dither=dither)
    return q


def preview_grey(img, lab):
    """A course preview at 160 size as rows of 2-bit values, cut by region
    as the title: sky light to white, the distant band and rough dark grey
    going to black, the fairway light grey, objects black with their light
    parts light."""
    import mkpreview as mp
    p = img.load()
    rows = []
    for y in range(img.height):
        row = []
        for x in range(img.width):
            r, g, b = p[x, y]
            L = (r * 30 + g * 59 + b * 11) / 25500
            k = lab[y][x]
            if k == mp.SKY:
                v = dither(0.93 + 0.1 * (L - 0.65) / 0.25, x, y)
            elif k == mp.FAR:
                v = dither(0.6 + 0.3 * (L - 0.3) / 0.4, x, y)
            elif k == mp.ROUGH:
                v = dither(0.7 + 0.15 * (L - 0.3) / 0.3, x, y)
            elif k == mp.FAIRWAY:
                v = dither(0.9 + 0.06 * (L - 0.5) / 0.15, x, y)
            else:
                v = LIGHT if L > 0.7 else BLACK
            row.append(v)
        rows.append(row)
    return rows


def main():
    out = Path(sys.argv[1])
    prev = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    out.mkdir(parents=True, exist_ok=True)
    m = load_mockup()
    gt = title_grey(m)
    grey = {PIC_TITLE: gt, PIC_MENU: menu_grey(gt)}
    wide = Image.open(TITLE_SCENES / 'wide.png').convert('RGB')
    tall = Image.open(TITLE_SCENES / 'tall.png').convert('RGB')
    square = wide.crop((80, 0, 400, 320))
    hi = dict(zip((PIC_TITLE, PIC_MENU), full_screen(m, square)))
    pal = palette()
    (out / 'Pal8-1000.bin').write_bytes(bytes(pal))
    rcp = ['/* Made by tools/mkart.py. */', f'DATA "Pal8" ID 1000 "{out / "Pal8-1000.bin"}"']
    total = 0
    for pid in (PIC_TITLE, PIC_MENU):
        g = grey[pid]
        q = to8(hi[pid], pal)
        total += write_bands(out, rcp, pid, 2, pack_rows(g, 2))
        total += write_bands(out, rcp, pid, 8, rows8(q))
        if prev:
            prev.mkdir(parents=True, exist_ok=True)
            gi = Image.new('L', (160, 160))
            gi.putdata([(3 - v) * 85 for row in g for v in row])
            gi.resize((480, 480), Image.NEAREST).save(prev / f'art-{pid}-160-grey.png')
            q.convert('RGB').save(prev / f'art-{pid}-320.png')
    for (w, h, title_id, menu_id), scene in zip(FULL_SCREENS, (tall, wide)):
        for pid, img in zip((title_id, menu_id), full_screen(m, scene)):
            q = to8(img, pal, Image.Dither.NONE)
            total += write_bands(out, rcp, pid, 8, rows8(q))
            if prev:
                q.convert('RGB').save(prev / f'art-{pid}-{w}x{h}.png')
    for pid, g in zip((DANA_TITLE, DANA_MENU), dana_grey(m)):
        total += write_bands(out, rcp, pid, 2, pack_rows(g, 2))
        if prev:
            gi = Image.new('L', (560, 160))
            gi.putdata([(3 - v) * 85 for row in g for v in row])
            gi.save(prev / f'art-{pid}-560x160-grey.png')
    # course select previews, by scenery number (tools/mkpreview.py)
    import mkpreview
    for sc, (img, lab) in mkpreview.make().items():
        pid = PIC_PREVIEW + 10 * sc
        small = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)
        g = preview_grey(small, lab[1::2, ::2])
        # no dither: the painted grain is enough, and runs pack smaller
        q = to8(img, pal, Image.Dither.NONE)
        total += write_bands(out, rcp, pid, 2, pack_rows(g, 2))
        total += write_bands(out, rcp, pid, 8, rows8(q))
        if prev:
            gi = Image.new('L', (small.width, small.height))
            gi.putdata([(3 - v) * 85 for row in g for v in row])
            gi.resize((small.width * 4, small.height * 4), Image.NEAREST).save(prev / f'preview-{sc}-160-grey.png')
            q.convert('RGB').resize((img.width * 2, img.height * 2), Image.NEAREST).save(prev / f'preview-{sc}-320.png')
    (out / 'art.rcp').write_text('\n'.join(rcp) + '\n')
    print(f'art: {total} bytes in {len(rcp) - 2} bands, palette 768')


if __name__ == '__main__':
    main()
