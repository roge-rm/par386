#!/usr/bin/env python3
"""Makes the renderer's art for the test hole, cut to the master palette
(tools/mkpal.py), and packs it for the core:
  OUT/Spr8-ID.bin  object sprites, ID 1000 + type * 10 + size (0 small, 1 big),
                   and the golfer, ID 2000 + 0, 1, 2 for 20, 40 and 60 px
  OUT/Bkd8-ID.bin  the backdrop strip, 1000 for 160 wide screens, 1001 for 320
  OUT/obj.rcp      PilRC DATA lines for all of them
and previews to PREVIEW if given.

Spr8: big-endian u16 width, height, anchor x, anchor y (the base, on the
ground), point x, point y (the golfer's ball, else the anchor), then width *
height palette indices row by row, 0 clear.
Bkd8: u16 width, height, then each row packed with PackBits on its own. 0 is
clear. The strip repeats twice in a full turn.

Trees and the rest are drawn as lit shapes with grain and no outline, lit
from the left and behind the camera like the golfer and the terrain. They'll
be drawn again per course in milestone 11.

Usage: mkobj.py OUT [PREVIEW]
"""
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mkpal          # noqa: E402
from mkart import packbits, unpackbits   # noqa: E402

ROOT = HERE.parent
RGB, RAMPS, _, _ = mkpal.build()
PAL = np.array(RGB, dtype=np.float64)
rng = np.random.default_rng(386)

# To the sun in (right, down, toward the viewer) image space: left, up and from behind the camera.
SUN = np.array([-0.55, -0.62, 0.56])
SUN /= np.linalg.norm(SUN)

OBJ_TREE, OBJ_PINE, OBJ_BUSH, OBJ_ROCK = 1, 2, 4, 5
# Sprite heights in pixels for the small and big sizes. World heights are in render.c.
SIZES = {OBJ_TREE: (28, 84), OBJ_PINE: (32, 96), OBJ_BUSH: (12, 36), OBJ_ROCK: (8, 24)}
SS = 4    # supersampling


def ramp_index(name, shade, dither):
    """Shade 0..1 to an index of a ramp, with dither -0.5..0.5 of a step."""
    s, n = RAMPS[name]
    k = np.clip(np.floor(shade * (n - 1) + 0.5 + dither), 0, n - 1).astype(int)
    return s + k


def shrink(a, f):
    h, w = a.shape[:2]
    return a.reshape(h // f, f, w // f, f, *a.shape[2:]).mean(axis=(1, 3))


def vnoise(shape, cell, seed):
    """Smooth value noise, cell pixels per step."""
    r = np.random.default_rng(seed)
    h, w = shape
    gh, gw = h // cell + 2, w // cell + 2
    g = r.random((gh, gw))
    y = np.arange(h)[:, None] / cell
    x = np.arange(w)[None, :] / cell
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int)
    fy, fx = y - y0, x - x0
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx
    b = g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def spheres(h, w, balls):
    """Front-most hit of a set of spheres (cx, cy, cz, r): depth, normal, which, hit mask."""
    zb = np.full((h, w), -1e9)
    n = np.zeros((h, w, 3))
    y, x = np.mgrid[0:h, 0:w] + 0.5
    for cx, cy, cz, r in balls:
        d2 = (x - cx) ** 2 + (y - cy) ** 2
        m = d2 < r * r
        z = cz + np.sqrt(np.maximum(r * r - d2, 0))
        m &= z > zb
        zb[m] = z[m]
        n[m] = np.stack([(x - cx)[m], (y - cy)[m], (z - cz)[m]], -1) / r
    return zb, n, zb > -1e8


def lambert(n):
    return np.clip(n @ SUN, 0, 1)


def tree(H, seed):
    """A broadleaf tree H pixels tall: a crown of leaf clumps on a short trunk."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 0.85) * SS
    cx, ctop, cbot = w / 2, h * 0.03, h * 0.80
    cy, ry, rx = (ctop + cbot) / 2, (cbot - ctop) / 2, w * 0.44
    balls = []
    for _ in range(70):
        a, rr = r.random() * 2 * math.pi, math.sqrt(r.random()) * 0.82
        bx, by = cx + math.cos(a) * rr * rx * 0.85, cy + math.sin(a) * rr * ry * 0.85
        rad = (0.16 + r.random() * 0.12) * rx
        bz = math.sqrt(max(0, 1 - rr * rr)) * rx * 0.5
        balls.append((bx, by, bz, rad))
    zb, n, hit = spheres(h, w, balls)
    # leaf grain: bumps the normal and darkens gaps between clumps
    leaf = vnoise((h, w), max(2, SS * H // 40), seed + 1) - 0.5
    fine = r.random((h, w)) - 0.5
    lit = lambert(n) * 0.85 + 0.15
    depth = np.clip((zb + rx * 0.2) / (rx * 0.9), 0, 1)
    shade = lit * (0.55 + 0.45 * depth) + leaf * 0.35 + fine * 0.15
    # the crown is darker underneath
    y = np.mgrid[0:h, 0:w][0]
    shade -= np.clip((y - cy) / ry, 0, 1) * 0.25
    # trunk: lit from the left, behind the crown
    ty = np.mgrid[0:h, 0:w][0]
    tx = np.mgrid[0:h, 0:w][1]
    tw = w * 0.035 * (1.0 + (ty / h) * 0.5)
    trunk = (np.abs(tx - cx) < tw) & (ty > cy) & ~hit
    bark = np.clip(0.75 - (tx - cx) / tw * 0.45 + fine * 0.3, 0, 1)
    return finish(H, [(hit, 'tree', shade), (trunk, 'bark', bark)], (cx / SS, h / SS))


def pine(H, seed):
    """A pine H pixels tall: drooping tiers of needles getting wider down the tree."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 0.52) * SS
    cx = w / 2
    y, x = np.mgrid[0:h, 0:w] + 0.5
    top, bot = h * 0.02, h * 0.9
    t = np.clip((y - top) / (bot - top), 0, 1)
    tiers = 7
    ph = (t * tiers) % 1.0
    half = w * 0.48 * (0.15 + 0.85 * t) * (0.55 + 0.45 * ph)
    rag = (vnoise((h, w), SS * 2, seed) - 0.5) * w * 0.12
    dx = (x - cx) / np.maximum(half, 1)
    hit = (np.abs(x - cx) < half + rag) & (y > top) & (y < bot)
    nz = np.sqrt(np.clip(1 - dx * dx, 0, 1))
    n = np.stack([dx * 0.9, -0.4 + ph * 0.8, nz], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    fine = r.random((h, w)) - 0.5
    needle = vnoise((h, w), max(2, SS * H // 48), seed + 2) - 0.5
    shade = lambert(n) * 0.8 + 0.12 + needle * 0.35 + fine * 0.18 - (1 - ph) * 0.18
    tw = w * 0.04
    trunk = (np.abs(x - cx) < tw) & (y >= bot - h * 0.04) & ~hit
    bark = np.clip(0.6 - (x - cx) / tw * 0.4 + fine * 0.3, 0, 1)
    return finish(H, [(hit, 'pine', shade), (trunk, 'bark', bark)], (cx / SS, h / SS))


def bush(H, seed):
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 1.4) * SS
    balls = []
    for _ in range(18):
        bx = w * (0.2 + r.random() * 0.6)
        rad = h * (0.22 + r.random() * 0.18)
        by = h - rad * (0.7 + r.random() * 0.6)
        balls.append((bx, max(rad, by), r.random() * rad * 0.5, rad))
    zb, n, hit = spheres(h, w, balls)
    fine = r.random((h, w)) - 0.5
    leaf = vnoise((h, w), max(2, SS * H // 14), seed + 1) - 0.5
    shade = lambert(n) * 0.7 + 0.08 + leaf * 0.3 + fine * 0.15
    return finish(H, [(hit, 'tree', shade)], (w / 2 / SS, h / SS))


def rock(H, seed):
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 1.5) * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    cx, cy = w / 2, h * 1.0
    rx, ry = w * 0.46, h * 0.95
    lump = 1 + (vnoise((h, w), SS * 3, seed) - 0.5) * 0.25
    dx, dy = (x - cx) / rx, (y - cy) / ry
    d2 = (dx * dx + dy * dy) / lump
    hit = d2 < 1
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    n = np.stack([dx, dy, nz], -1)
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
    fine = r.random((h, w)) - 0.5
    shade = lambert(n) * 0.85 + 0.12 + fine * 0.2 + (vnoise((h, w), SS, seed + 3) - 0.5) * 0.25
    return finish(H, [(hit, 'rock', shade)], (w / 2 / SS, h / SS))


def finish(H, layers, anchor):
    """Shrinks the supersampled layers, cuts them to their ramps with grain and
    returns (indices, anchor x, anchor y)."""
    h, w = layers[0][0].shape
    idx = np.zeros((h // SS, w // SS), dtype=np.uint8)
    for mask, name, shade in layers:
        cov = shrink(mask.astype(float), SS)
        s = shrink(np.where(mask, shade, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        grain = rng.random(idx.shape) - 0.5
        k = ramp_index(name, np.clip(s, 0, 1), grain * 0.9)
        idx[on] = k[on]
    return idx, round(anchor[0]), round(anchor[1])


def spr8(idx, ax, ay, px=None, py=None):
    h, w = idx.shape
    if px is None:
        px, py = ax, ay
    return struct.pack('>HHHHhh', w, h, ax, ay, px, py) + idx.astype(np.uint8).tobytes()


def golfer(n):
    """The golfer's address frame remapped to the golfer ramps. The ball is at the club head."""
    im = Image.open(ROOT / f'art/golfer/f01-{n}.png')
    a = np.array(im)
    p = np.array(im.getpalette()[:768]).reshape(-1, 3)
    allowed = []
    for name in ('skin', 'shirt', 'trousers', 'cap', 'shoes', 'club', 'grey'):
        s, k = RAMPS[name]
        allowed += range(s, s + k)
    allowed.append(255)
    out = np.zeros(a.shape, dtype=np.uint8)
    cache = {}
    for v in np.unique(a[a > 0]):
        cache[v] = mkpal.nearest(tuple(int(c) for c in p[v]), allowed, RGB)
    for v, i in cache.items():
        out[a == v] = i
    h, w = a.shape
    ys, xs = np.nonzero(a[:, w * 2 // 3:])
    k = np.argmax(ys * 100 + xs)
    bx, by = int(xs[k] + w * 2 // 3), int(ys[k])
    ys, xs = np.nonzero(a)
    feet = int(ys.max()) + 1
    fx = int(round(xs[ys == ys.max()].mean()))
    return out, fx, feet, bx, by


def backdrop(width, height, seed):
    """A distant tree line in front of low hills, repeating across the width.
    Bottom rows are solid so the terrain's far edge meets it."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x / w * 2 * math.pi

    def wave(freqs, seed):
        r = np.random.default_rng(seed)
        out = np.zeros_like(u[0])
        for f, amp in freqs:
            out += amp * np.sin(u[0] * f + r.random() * 2 * math.pi)
        return out
    hills = h * (0.30 - wave([(2, 0.10), (5, 0.06), (11, 0.03)], seed))
    trees = h * (0.62 - wave([(3, 0.06), (17, 0.05), (41, 0.04), (97, 0.04)], seed + 1))
    # crowns: little bumps along the tree line
    bump = np.abs(np.sin(u[0] * 157 + wave([(7, 1.0)], seed + 2)))
    trees -= bump * h * 0.08
    hill = y >= hills[None, :]
    tl = y >= trees[None, :]
    fine = rng.random((h, w)) - 0.5
    hs = np.clip(0.8 - (y - hills[None, :]) / h * 0.6 + fine * 0.1, 0, 1)
    tn = vnoise((h, w), SS * 2, seed + 3) - 0.5
    ts = np.clip(0.55 - (y - trees[None, :]) / h * 0.9 + tn * 0.5 + fine * 0.2, 0, 1)
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, s in ((hill, 'far', hs * 0.5 + 0.5), (tl, 'far', ts * 0.6)):
        cov = shrink(mask.astype(float), SS)
        sh = shrink(np.where(mask, s, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        k = ramp_index(name, np.clip(sh, 0, 1), (rng.random(idx.shape) - 0.5) * 0.9)
        idx[on] = k[on]
    idx[-2:][idx[-2:] == 0] = RAMPS['far'][0] + 2
    return idx


def bkd8(idx):
    h, w = idx.shape
    out = bytearray(struct.pack('>HH', w, h))
    for row in idx:
        raw = row.astype(np.uint8).tobytes()
        p = packbits(raw)
        assert unpackbits(p, len(raw)) == raw
        out += p
    return bytes(out)


def preview(idx, path, scale):
    rgb = PAL[idx].astype(np.uint8)
    rgb[idx == 0] = (255, 0, 255)
    Image.fromarray(rgb).resize((idx.shape[1] * scale, idx.shape[0] * scale), Image.NEAREST).save(path)


def main():
    out = Path(sys.argv[1])
    prev = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    out.mkdir(parents=True, exist_ok=True)
    if prev:
        prev.mkdir(parents=True, exist_ok=True)
    res = []
    makers = {OBJ_TREE: tree, OBJ_PINE: pine, OBJ_BUSH: bush, OBJ_ROCK: rock}
    for t, sizes in SIZES.items():
        for k, H in enumerate(sizes):
            idx, ax, ay = makers[t](H, 100 * t + k)
            rid = 1000 + t * 10 + k
            res.append(('Spr8', rid, spr8(idx, ax, ay)))
            if prev:
                preview(idx, prev / f'spr-{rid}.png', 4)
    for k, n in enumerate((20, 40, 60)):
        idx, fx, fy, bx, by = golfer(n)
        res.append(('Spr8', 2000 + k, spr8(idx, fx, fy, bx, by)))
        if prev:
            preview(idx, prev / f'golfer-{n}.png', 4)
    # Wide enough for half a turn: 160 px is the view's 59.5 degrees (render.c).
    for k, (w, h) in enumerate(((484, 20), (968, 40))):
        idx = backdrop(w, h, 7) if k == 1 else None
        if k == 0:
            big = backdrop(968, 40, 7)
            idx = big.reshape(20, 2, 484, 2)[:, 0, :, 0]
        res.append(('Bkd8', 1000 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1000 + k}.png', 2)
    rcp = ['/* Made by tools/mkobj.py. */']
    total = 0
    for typ, rid, data in res:
        name = f'{typ}-{rid}.bin'
        (out / name).write_bytes(data)
        rcp.append(f'DATA "{typ}" ID {rid} "{out / name}"')
        total += len(data)
    (out / 'obj.rcp').write_text('\n'.join(rcp) + '\n')
    print(f'obj: {total} bytes in {len(res)} resources')


if __name__ == '__main__':
    main()
