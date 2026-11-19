#!/usr/bin/env python3
"""The master palette: 256 colours close to VGA, laid out in ramps.

Run as a script from the repo root (`make tables`) it writes
  src/core/pal.h     where each ramp starts, for any core file
  src/core/paltab.h  grey levels, 2-bit patterns and grass noise, only render.c includes it
Both are committed. tools/mkart.py and tools/mkobj.py import it for the RGB
values (the Pal8 resource) and to cut art to the ramps.

Layout: 0 white, 1 to UIC_COUNT the UI colours (UI colour n is index n + 1,
src/core/uicol.h), then the ramps below, dark to light, and 255 black. The
Palm wants white at 0 and black at 255.

2-bit patterns: on the IIIx the two middle greys look close to white (about
94% and 80%), so mid tones are made by mixing in black. Each index gets a
4x4 pattern, one byte (4 pixels, Palm order, 0 white) per row. Terrain ramps
get their own textures so the surfaces stay apart: fairway is dark grey with
a few black dots (more in the dark mowing bands), first cut more, rough and
deep rough over half black, fringe fewer,
green is grey with one black dot in 16, sand is near white and water is in
lines. The golfer is a black silhouette with a light cap and shoes, and
the flag is white so it shows against the trees. The club is black.
"""
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
B, D, L, W = 3, 2, 1, 0                  # Palm 2-bit values
SEEN = {B: 0.0, D: 0.80, L: 0.94, W: 1.0}

RAMP_N = 8    # levels in each terrain ramp

# Terrain ramps in surface order (src/core/hole.h): name, dark, middle, light, 2-bit style.
TERRAIN = [
    ('rough',      (26, 52, 18),   (62, 98, 32),   (114, 146, 58),  ('dots', D, 10, 6)),
    ('first_cut',  (32, 70, 24),   (72, 126, 42),  (128, 178, 72),  ('dots', D, 6, 8)),
    ('fairway',    (44, 96, 30),   (100, 164, 58), (166, 216, 100), ('dots', D, 3, 8)),
    ('deep_rough', (22, 38, 14),   (50, 70, 26),   (96, 112, 48),   ('dots', D, 13, 6)),
    ('green',      (48, 112, 52),  (110, 184, 92), (180, 232, 140), ('smooth', D, 0, 0)),
    ('fringe',     (44, 104, 40),  (98, 170, 70),  (164, 220, 112), ('dots', D, 2, 4)),
    ('sand',       (150, 124, 80), (214, 192, 142), (250, 238, 204), ('sand', L, 0, 0)),
    ('water',      (18, 38, 76),   (52, 88, 140),  (130, 166, 210), ('lines', D, 0, 0)),
    ('path',       (100, 94, 86),  (162, 154, 140), (214, 208, 194), ('dots', L, 4, 8)),
    ('oob',        (56, 60, 32),   (104, 100, 58), (160, 150, 104), ('dots', D, 10, 8)),
    ('tee',        (40, 90, 28),   (92, 156, 54),  (156, 208, 94),  ('dots', D, 3, 8)),
]

HAZE_RGB = (178, 190, 212)

# Other ramps: name, entries, colour stops dark to light, 2-bit style.
OTHER = [
    ('haze',     8, None, ('dots', L, 5, 6)),
    ('sky',     16, [(64, 104, 186), (120, 154, 214), (196, 212, 232)], ('sky', 0, 0, 0)),
    ('far',      8, [(34, 58, 54), (90, 112, 112), (156, 170, 182)], ('dots', D, 8, 16)),
    ('tree',    12, [(12, 30, 10), (46, 88, 28), (132, 168, 62)], ('luma', 0, 0, 0)),
    ('pine',    10, [(8, 26, 18), (34, 66, 40), (96, 132, 74)], ('luma', 0, 0, 0)),
    ('bark',     6, [(36, 26, 18), (82, 62, 44), (146, 116, 84)], ('luma', 0, 0, 0)),
    ('rock',     8, [(46, 46, 44), (110, 108, 100), (206, 202, 190)], ('luma', 0, 0, 0)),
    ('wall',     8, [(58, 52, 44), (122, 112, 96), (200, 188, 164)], ('luma', 0, 0, 0)),
    ('skin',     6, [(60, 42, 34), (160, 100, 80), (244, 186, 142)], ('sil', 0, 0, 0)),
    ('shirt',    6, [(96, 48, 42), (170, 88, 80), (230, 124, 112)], ('sil', 0, 0, 0)),
    ('trousers', 5, [(20, 24, 24), (46, 56, 55), (104, 112, 110)], ('sil', 0, 0, 0)),
    ('cap',      4, [(150, 156, 150), (210, 214, 206), (255, 255, 252)], ('flat', 0, 0, 0)),
    ('shoes',    3, [(92, 92, 90), (170, 170, 166), (232, 232, 228)], ('flat', 0, 0, 0)),
    ('club',     4, [(40, 40, 42), (110, 112, 112), (196, 198, 196)], ('ink', 0, 0, 0)),
    ('flag',     4, [(110, 10, 10), (190, 30, 24), (244, 70, 54)], ('flag', 0, 0, 0)),
    ('grey',     8, [(28, 28, 28), (128, 128, 128), (236, 236, 236)], ('luma', 0, 0, 0)),
    # for the title and menu art
    ('wood',     6, [(56, 26, 8), (130, 74, 32), (206, 148, 88)], ('luma', 0, 0, 0)),
    ('red',      6, [(80, 8, 8), (160, 24, 20), (236, 70, 48)], ('luma', 0, 0, 0)),
    ('gold',     6, [(110, 70, 10), (200, 150, 40), (255, 228, 120)], ('luma', 0, 0, 0)),
    ('navy',     6, [(8, 10, 36), (34, 44, 100), (96, 110, 180)], ('luma', 0, 0, 0)),
    # cherry blossom for the Sakura Bay trees
    ('blossom',  4, [(122, 52, 74), (214, 128, 156), (252, 218, 228)], ('luma', 0, 0, 0)),
]


def ui_colours():
    text = (ROOT / 'src/core/uicol.h').read_text()
    return [tuple(int(v) for v in m) for m in re.findall(r'X\(UIC_\w+,\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+\)', text)]


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def stops(cols, n):
    """n colours spread over the stops, in gamma space."""
    out = []
    for k in range(n):
        t = k / (n - 1) * (len(cols) - 1)
        i = min(int(t), len(cols) - 2)
        out.append(tuple(round(v) for v in lerp(cols[i], cols[i + 1], t - i)))
    return out


def luma(c):
    return (c[0] * 0.30 + c[1] * 0.59 + c[2] * 0.11) / 255


def build():
    """Returns (rgb list of 256, ramps {name: (start, count)}, styles per index)."""
    rgb = [(255, 255, 255)]
    style = [('flat', 0, 0, 0)]
    level = [1.0]
    ramps = {}
    for c in ui_colours():
        rgb.append(c)
        style.append(('luma', 0, 0, 0))
        level.append(0)
    ramps['ui'] = (1, len(rgb) - 1)
    ramps['terrain'] = (len(rgb), len(TERRAIN) * RAMP_N)
    for name, a, m, b, st in TERRAIN:
        ramps[name] = (len(rgb), RAMP_N)
        for k, c in enumerate(stops([a, m, b], RAMP_N)):
            rgb.append(c)
            style.append(st)
            level.append(k / (RAMP_N - 1))
    grass = [tuple(sum(t[j][i] for t in TERRAIN[:3]) / 3 for i in range(3)) for j in (1, 2, 3)]
    for name, n, cols, st in OTHER:
        if cols is None:     # haze: grass mixed most of the way to the haze colour
            cols = [lerp(g, HAZE_RGB, 0.6) for g in grass]
        ramps[name] = (len(rgb), n)
        for k, c in enumerate(stops(cols, n)):
            rgb.append(c)
            style.append(st)
            level.append(k / (n - 1))
    assert len(rgb) <= 255, len(rgb)
    ramps['spare'] = (len(rgb), 255 - len(rgb))
    while len(rgb) < 255:
        rgb.append((0, 0, 0))
        style.append(('luma', 0, 0, 0))
        level.append(0)
    rgb.append((0, 0, 0))
    style.append(('flat', 0, 0, 0))
    level.append(0)
    return rgb, ramps, style, level


def pattern(c, st, lvl):
    """4 rows of 4 Palm 2-bit values for colour c."""
    kind, base, black, spread = st
    rows = []
    for y in range(4):
        row = []
        for x in range(4):
            t = BAYER[y][x]
            if kind == 'dots':
                # black dots on a grey, fewer as the level rises: spread of 16 across the ramp
                n = max(0, black + round((0.5 - lvl) * spread))
                v = B if t < n else base
                if base == D and n == 0 and t >= 14 and lvl > 0.8:
                    v = L
            elif kind == 'smooth':
                # greens: dark and light grey mixed, lighter as the level rises,
                # with one black dot in 16 so they read apart from sand
                n = 3 + round(lvl * 8)
                v = L if t < n else D
                if t == 15 and lvl < 0.9:
                    v = B
                if lvl < 0.1 and t < 4:
                    v = B
            elif kind == 'sand':
                # near white; the darkest levels (the near lip's shadow) get black
                n = round((1 - lvl) * 6)
                v = D if t < n else (W if t >= 13 and lvl > 0.5 else L)
                if lvl < 0.2 and t < 8:
                    v = B
            elif kind == 'lines':
                # water: black lines, the rows between dark or light grey;
                # the lightest level, far off, a pale sheen between dark banks
                if lvl > 0.9:
                    v = (D, L, W, L)[y]
                elif y in (0, 2):
                    v = B
                elif y == 1:
                    v = D if lvl < 0.7 else L
                else:
                    v = B if lvl < 0.3 else D
            elif kind == 'sky':
                n = round(lvl * 18)
                v = W if t < n else L
                if lvl < 0.25 and t >= 12:
                    v = D
            elif kind == 'flag':
                # white with a black lower edge, to show against the trees behind
                v = B if lvl < 0.4 else W
            elif kind == 'ink':
                # the club's shaft: black, so the thin line shows on light grass
                v = B
            elif kind == 'sil':
                # the golfer's body: a black silhouette, only the lightest parts light
                v = B if luma(c) < 0.75 else L
            elif kind == 'flat':
                y_ = luma(c)
                v = B if y_ < 0.42 else D if y_ < 0.7 else L if y_ < 0.9 else W
            else:    # luma: ordered dither between the seen levels
                y_ = luma(c)
                lv = sorted(SEEN.items(), key=lambda kv: kv[1])
                k = 0
                while k < len(lv) - 2 and y_ > lv[k + 1][1]:
                    k += 1
                (va, a), (vb, b) = lv[k], lv[k + 1]
                f = max(0.0, min(1.0, (y_ - a) / (b - a)))
                v = vb if f * 16 > t + 0.5 else va
            row.append(v)
        rows.append(row)
    return rows


def pattern_bytes(rows):
    return [(r[0] << 6) | (r[1] << 4) | (r[2] << 2) | r[3] for r in rows]


def seen_rgb(rows):
    """How a pattern looks on the IIIx, as a grey 0..255."""
    return round(sum(SEEN[v] for r in rows for v in r) / 16 * 255)


def noise_table():
    """16x16 grass noise, -8..7, with blades: values run on down the columns."""
    import random
    rnd = random.Random(386)
    col = [[rnd.randint(-8, 7) for _ in range(16)] for _ in range(16)]
    out = []
    for y in range(16):
        for x in range(16):
            v = (col[x][y] * 2 + col[x][(y + 1) % 16] + col[x][(y + 15) % 16]) // 4
            out.append(max(-8, min(7, v * 3 // 2)))
    return out


def rows_c(vals, per=16):
    return '\n'.join('\t' + ', '.join(str(v) for v in vals[i:i + per]) + ',' for i in range(0, len(vals), per))


def nearest(c, idxs, rgb):
    best, bd = idxs[0], 1e9
    for i in idxs:
        p = rgb[i]
        d = (p[0] - c[0]) ** 2 * 3 + (p[1] - c[1]) ** 2 * 4 + (p[2] - c[2]) ** 2 * 2
        if d < bd:
            best, bd = i, d
    return best


def main():
    rgb, ramps, style, level = build()
    up = {k: k.upper() for k in ramps}
    h = ['/* Made by tools/mkpal.py, don\'t edit. Where each ramp of the master',
         ' * palette starts and how many entries it has, dark to light. 0 is white,',
         ' * 255 black and UI colour n is index n + 1. */',
         '#ifndef PAL_H', '#define PAL_H', '',
         '#define PAL_RAMP_N %d    /* levels in each terrain ramp, in surface order from PAL_TERRAIN */' % RAMP_N,
         '#define PAL_WHITE 0', '#define PAL_BLACK 255']
    for k, (s, n) in ramps.items():
        h.append('#define PAL_%s %d' % (up[k], s))
        h.append('#define PAL_%s_N %d' % (up[k], n))
    h += ['', '#endif', '']
    (ROOT / 'src/core/pal.h').write_text('\n'.join(h))

    grey = [round(luma(c) * 255) for c in rgb]
    pats = []
    for i in range(256):
        pats += pattern_bytes(pattern(rgb[i], style[i], level[i]))
    t = ['/* Made by tools/mkpal.py, don\'t edit. Only render.c includes it. */', '',
         '/* Grey of each palette index, 0 black to 255 white, for 1 and 4 bit screens. */',
         'static const uint8_t pal_grey[256] = {', rows_c(grey), '};', '',
         '/* 2-bit pattern of each index for the IIIx greys: 4 bytes, one per row',
         ' * (y & 3), each 4 pixels in Palm order (x & 3 from the top bits, 0 white). */',
         'static const uint8_t pal_pat2[1024] = {', rows_c(pats), '};', '',
         '/* Grass noise, 16 x 16, row major, -8 to 7. */',
         'static const int8_t pal_noise[256] = {', rows_c(noise_table()), '};', '']
    (ROOT / 'src/core/paltab.h').write_text('\n'.join(t))
    c565 = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b in rgb]
    t = ['/* Made by tools/mkpal.py, don\'t edit. Only gfx.c includes it. */', '',
         '/* Each palette index as 16-bit colour, 5 bits red, 6 green, 5 blue. */',
         'static const uint16_t pal_565[256] = {', rows_c(c565, 12), '};', '']
    (ROOT / 'src/core/pal565.h').write_text('\n'.join(t))
    print('palette: %d used, %d spare' % (ramps['spare'][0], ramps['spare'][1]))


if __name__ == '__main__':
    main()
