#!/usr/bin/env python3
"""Writes the app icons (a flag on a green with a ball) to res/ as BMP files.

Usage: mkicon.py RESDIR [PREVIEW.png]

The 1x icons are character maps drawn by hand. The double-density ones are
drawn from shapes at their own size. Each depth has its own colour table:
8-bit uses colours from the Palm system palette (the 216 web-safe ones) so
they map exactly, 16-bit uses the game's greens and reds, 4 and 2 bit are
hand-picked greys, 2-bit leaning on black because the IIIx shows the two
middle greys close to white, and 1-bit uses dither patterns.

'.' is the background: magenta (pilrc TRANSPARENT 255 0 255) at 8 and 16
bit, white at 1, 2 and 4 bit.

With a second argument it also writes an enlarged preview sheet.
"""
import struct
import sys
from pathlib import Path

# Large icon, 32x22, art kept within about x 5..27. Rows are padded with
# '.' on the right. '#' is the green, filled with mowing stripes.
LARGE = [
    '',
    '................pR',
    '................pRRR',
    '................pRRRRR',
    '................pRRRRRRR',
    '................pRRRRr',
    '................pRrr',
    '................pr',
    '................p',
    '................p',
    '................p',
    '..........C#####p#####C',
    '.......CC#######p########C',
    '......D#########p#########D',
    '.....D####WW####p##########D',
    '.....D###WWWw##HHH#########D',
    '.....D###WWww##############D',
    '.....D####wwss#############D',
    '......D###################D',
    '.......ddDDDDDDDDDDDDDDDdd',
    '..........ddddddddddddd',
    '',
]

# Small icon, 15x9.
SMALL = [
    '........pRR',
    '........pRRRr',
    '........pRr',
    '........p',
    '....C###p###C',
    '..D#WW#HHH####D',
    '..D#Wws#######D',
    '...dDDDDDDDDDd',
    '.....ddddddd',
]

# Mowing stripes, diagonal bands three pixels wide.
def stripe(x, y, width=3):
    return 'A' if ((x - y * 2) // width) % 2 == 0 else 'B'


def checker(x, y):
    return (x + y) % 2


def dots4(x, y):
    """One pixel in four, staggered."""
    return 1 if (x % 2 == 0 and (y % 2 == 0) == (x % 4 == 0)) else 0


MAGENTA = (255, 0, 255)

# Per depth: char -> value. Values are RGB tuples (8, 16 bit), grey levels
# 0 white .. max black (4, 2, 1 bit), or functions of (x, y).
COLOURS = {
    8: {  # web-safe, so they land on the Palm system palette as is
        '.': MAGENTA,
        'A': (0x66, 0xCC, 0x33), 'B': (0x33, 0x99, 0x33), 'C': (0x99, 0xCC, 0x66),
        'D': (0x00, 0x66, 0x00), 'd': (0x00, 0x33, 0x00),
        'R': (0xFF, 0x33, 0x00), 'r': (0x99, 0x00, 0x00),
        'P': (0xFF, 0xFF, 0xFF), 'p': (0x66, 0x66, 0x66),
        'W': (0xFF, 0xFF, 0xFF), 'w': (0xCC, 0xCC, 0xCC), 's': (0x00, 0x66, 0x00),
        'H': (0x00, 0x00, 0x00),
    },
    16: {  # the game's green and flag ramps
        '.': MAGENTA,
        'A': (118, 192, 96), 'B': (82, 150, 70), 'C': (180, 232, 140),
        'D': (48, 112, 52), 'd': (24, 62, 28),
        'R': (236, 52, 36), 'r': (130, 14, 12),
        'P': (240, 240, 232), 'p': (104, 104, 100),
        'W': (255, 255, 255), 'w': (192, 192, 184), 's': (34, 84, 38),
        'H': (0, 0, 0),
    },
    4: {  # 0 white .. 15 black
        '.': 0,
        'A': 6, 'B': 8, 'C': 5, 'D': 11, 'd': 13,
        'R': 13, 'r': 15, 'P': 2, 'p': 11,
        'W': 0, 'w': 3, 's': 12, 'H': 15, 'K': 15,
    },
    2: {  # 0 white .. 3 black
        '.': 0,
        'A': 2, 'B': lambda x, y: 3 if dots4(x, y) else 2, 'C': 2,
        'D': 3, 'd': 3,
        'R': 3, 'r': 3, 'P': 0, 'p': 3,
        'W': 0, 'w': 0, 's': 3, 'H': 3, 'K': 3,
    },
    1: {
        '.': 0,
        'A': 0, 'B': dots4, 'C': 1,
        'D': 1, 'd': 1,
        'R': 1, 'r': 1, 'P': 0, 'p': 1,
        'W': 0, 'w': 0, 's': 1, 'H': 1, 'K': 1,
    },
}


def charmap(rows, w, h):
    g = []
    for y in range(h):
        row = rows[y] if y < len(rows) else ''
        assert len(row) <= w, (y, row)
        g.append(list(row.ljust(w, '.')))
    for y in range(h):
        for x in range(w):
            if g[y][x] == '#':
                g[y][x] = stripe(x, y)
    return g


def hires(w, h, cx, cy, rx, ry, pole, top, flag_len, flag_h, ball, br, stripe_w):
    """Double-density icon drawn from shapes. pole is the left pole column
    (two wide), top the first pole row, ball the ball centre (bx, by)."""
    g = [['.'] * w for _ in range(h)]
    # green
    for y in range(h):
        for x in range(w):
            fx, fy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            e = fx * fx + fy * fy
            if e > 1:
                continue
            if fy > 0.55 and e > 0.55:
                c = 'd' if fy > 0.8 or e > 0.85 else 'D'
            elif e > 0.82 and fy > -0.2:
                c = 'D'
            elif e > 0.8 and fy <= -0.2:
                c = 'C'
            else:
                c = stripe(x, y, stripe_w)
            g[y][x] = c
    # hole, a dark ellipse where the pole goes in
    hy = int(cy - ry * 0.35)
    for x in range(pole - 2, pole + 4):
        g[hy][x] = 'H'
    for x in range(pole - 1, pole + 3):
        g[hy + 1][x] = 'H'
    # pole, light on the left and dark on the right
    for y in range(top, hy):
        g[y][pole] = 'P'
        g[y][pole + 1] = 'p'
    # pennant, widest in the middle, with a dark lower edge and a fold
    mid = (flag_h - 1) / 2
    for i in range(flag_h):
        t = 1 - abs(i - mid) / (mid + 1)
        n = max(1, round(flag_len * t))
        y = top + i
        for j in range(n):
            x = pole + 2 + j
            c = 'R'
            if i > mid and j >= n - 2:
                c = 'r'
            if i >= flag_h - 2:
                c = 'r'
            if flag_len > 8 and j in (flag_len // 2, flag_len // 2 + 1) and i > mid - 1:
                c = 'r'
            g[y][x] = c
    # ball shadow, then the ball, lit from the upper left
    bx, by = ball
    for y in range(h):
        for x in range(w):
            fx, fy = (x + 0.5 - bx - br * 0.7) / (br * 1.1), (y + 0.5 - by - br * 0.6) / (br * 0.6)
            if fx * fx + fy * fy <= 1 and g[y][x] in 'ABCD':
                g[y][x] = 's'
    for y in range(h):
        for x in range(w):
            dx, dy = x + 0.5 - bx, y + 0.5 - by
            if dx * dx + dy * dy <= br * br:
                lit = (dx + dy) / br
                g[y][x] = 'w' if lit > 0.55 else 'W'
    return g


def large2():
    return hires(64, 44, cx=32, cy=33, rx=22.5, ry=9.5, pole=32, top=2,
                 flag_len=16, flag_h=15, ball=(21.5, 32.5), br=3.6, stripe_w=5)


def small2():
    return hires(30, 18, cx=16, cy=13.6, rx=13, ry=4.4, pole=16, top=0,
                 flag_len=6, flag_h=7, ball=(9, 12), br=2.3, stripe_w=3)


def outlined(g):
    """Copy of g with the green next to the ball turned black ('K'), so the
    ball shows on the light greens at 1 and 2 bit."""
    h, w = len(g), len(g[0])
    o = [row[:] for row in g]
    for y in range(h):
        for x in range(w):
            if g[y][x] in 'ABCDs' and any(
                    0 <= x + dx < w and 0 <= y + dy < h and g[y + dy][x + dx] in 'Ww'
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                o[y][x] = 'K'
    return o


_low = {}


def value(g, x, y, bits):
    if bits <= 2:
        if id(g) not in _low:
            _low[id(g)] = (g, outlined(g))
        g = _low[id(g)][1]
    v = COLOURS[bits][g[y][x]]
    return v(x, y) if callable(v) else v


def rgb(g, x, y, bits):
    """The colour a pixel is written as (and shown in the preview)."""
    v = value(g, x, y, bits)
    if bits >= 8:
        return v
    maxv = (1 << bits) - 1
    k = 255 - 255 * v // maxv
    return (k, k, k)


def bmp24(g, bits, path):
    """pilrc reads 24-bit BMPs and cuts them to each depth."""
    h, w = len(g), len(g[0])
    rowbytes = (w * 3 + 3) // 4 * 4
    data = bytearray()
    for y in reversed(range(h)):
        out = bytearray(rowbytes)
        for x in range(w):
            r, gg, b = rgb(g, x, y, bits)
            out[x * 3:x * 3 + 3] = bytes([b, gg, r])
        data += out
    hdr = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    Path(path).write_bytes(hdr + info + bytes(data))


def bmp1(g, path):
    """1-bit BMP, black is 1."""
    h, w = len(g), len(g[0])
    rowbytes = ((w + 31) // 32) * 4
    pal = [bytes([255, 255, 255, 0]), bytes([0, 0, 0, 0])]
    data = bytearray()
    for y in reversed(range(h)):
        out = bytearray(rowbytes)
        for x in range(w):
            if value(g, x, y, 1):
                out[x // 8] |= 0x80 >> (x % 8)
        data += out
    off = 14 + 40 + 8
    hdr = b'BM' + struct.pack('<IHHI', off + len(data), 0, 0, off)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 1, 0, len(data), 2835, 2835, 2, 0)
    Path(path).write_bytes(hdr + info + b''.join(pal) + bytes(data))


def icons():
    """(name, grid, depths) for every icon file."""
    lg, sm = charmap(LARGE, 32, 22), charmap(SMALL, 15, 9)
    return [('icon', lg, (1, 2, 4, 8, 16)), ('small', sm, (1, 2, 4, 8, 16)),
            ('icon-hd', large2(), (8, 16)), ('small-hd', small2(), (8, 16))]


def write(res):
    res.mkdir(parents=True, exist_ok=True)
    for name, g, depths in icons():
        for bits in depths:
            p = res / f'{name}-{bits}.bmp'
            if bits == 1:
                bmp1(g, p)
            else:
                bmp24(g, bits, p)


# 2-bit greys as the IIIx shows them (tools/mkpal.py SEEN).
SEEN2 = {0: 255, 1: 240, 2: 204, 3: 0}


def shown(g, x, y, bits, bg):
    if g[y][x] == '.' and bits >= 8:
        return bg
    if bits == 2:
        k = SEEN2[value(g, x, y, 2)]
        return (k, k, k)
    return rgb(g, x, y, bits)


def preview(path):
    from PIL import Image, ImageDraw
    sets = icons()
    light, dark = (255, 255, 255), (40, 44, 92)
    sheet = Image.new('RGB', (1500, 1700), (160, 160, 160))
    d = ImageDraw.Draw(sheet)
    yy = 8
    for name, g, depths in sets:
        h, w = len(g), len(g[0])
        z = 8 if 'hd' not in name else 4
        xx = 8
        d.text((8, yy), f'{name} {w}x{h} at {z}x', fill=(0, 0, 0))
        yy += 14
        for bits in depths:
            for bg in (light, dark):
                im = Image.new('RGB', (w, h))
                for y in range(h):
                    for x in range(w):
                        im.putpixel((x, y), shown(g, x, y, bits, bg))
                big = im.resize((w * z, h * z), Image.NEAREST)
                frame = Image.new('RGB', (w * z + 8, h * z + 8), bg)
                frame.paste(big, (4, 4))
                if xx + frame.width > sheet.width:
                    xx = 8
                    yy += h * z + 24
                sheet.paste(frame, (xx, yy))
                d.text((xx, yy + h * z + 9), f'{bits} bit', fill=(0, 0, 0))
                xx += frame.width + 6
        yy += h * z + 28
    # Launcher mocks at 1x and 3x, one per depth.
    lg, sm = sets[0][1], sets[1][1]
    xx = 8
    for bits in (1, 2, 4, 8, 16):
        bg = light if bits < 8 else (light if bits == 8 else dark)
        m = Image.new('RGB', (160, 160), bg)
        md = ImageDraw.Draw(m)
        fg = (0, 0, 0) if bg == light else (255, 255, 255)
        md.rectangle((0, 0, 159, 14), fill=(0, 0, 0) if bits < 8 else (0, 51, 153))
        md.text((4, 2), 'Games', fill=(255, 255, 255))
        for i in range(12):
            cx, cy = (i % 4) * 40, 20 + (i // 4) * 34
            if i == 5:
                for y in range(22):
                    for x in range(32):
                        m.putpixel((cx + 4 + x, cy + y), shown(lg, x, y, bits, bg))
                md.text((cx + 2, cy + 23), 'Par 386', fill=fg)
            else:
                md.rectangle((cx + 12, cy + 3, cx + 27, cy + 18), outline=fg)
                md.text((cx + 6, cy + 23), 'App', fill=fg)
        # list view row with the small icon
        for y in range(9):
            for x in range(15):
                m.putpixel((4 + x, 146 + y), shown(sm, x, y, bits, bg))
        md.text((24, 145), 'Par 386', fill=fg)
        sheet.paste(m, (xx, yy))
        d.text((xx, yy + 162), f'{bits} bit launcher 1x', fill=(0, 0, 0))
        xx += 170
    yy += 180
    # The same mocks at 3x so the 1x pixels can be seen.
    xx = 8
    for bits in (1, 2, 4, 8, 16):
        bg = dark if bits == 16 else light
        im = Image.new('RGB', (48, 40), bg)
        for y in range(22):
            for x in range(32):
                im.putpixel((8 + x, 2 + y), shown(lg, x, y, bits, bg))
        for y in range(9):
            for x in range(15):
                im.putpixel((16 + x, 28 + y), shown(sm, x, y, bits, bg))
        sheet.paste(im.resize((48 * 5, 40 * 5), Image.NEAREST), (xx, yy))
        xx += 48 * 5 + 6
    yy += 210
    sheet = sheet.crop((0, 0, sheet.width, yy))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


if __name__ == '__main__':
    write(Path(sys.argv[1]))
    if len(sys.argv) > 2:
        preview(sys.argv[2])
