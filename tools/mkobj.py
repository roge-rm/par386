#!/usr/bin/env python3
"""Makes the renderer's art for the test hole, cut to the master palette
(tools/mkpal.py), and packs it for the core:
  OUT/Spr8-ID.bin  object sprites, ID 1000 + type * 10 + size (0 small, 1 big),
                   the golfer's poses, ID 2000 + 10 * pose + 0 or 1 for 20 or
                   40 px, and the course sets from tools/mkstyle.py
  OUT/Bkd8-ID.bin  the backdrop strip, 1000 for 160 wide screens, 1001 for 320
  OUT/obj.rcp      PilRC DATA lines for all of them
and previews to PREVIEW if given.

Spr8: big-endian u16 width, height, anchor x, anchor y (the base, on the
ground), point x, point y (the golfer's ball, else the anchor), then width *
height palette indices row by row, 0 clear.
Bkd8: u16 width, height, then each row packed with PackBits on its own. 0 is
clear. The strip repeats twice in a full turn.

Trees and the rest are drawn as lit shapes with grain and no outline, lit
from the left and behind the camera like the golfer and the terrain. The
art per course style is in tools/mkstyle.py.

Backdrops come in sets by the course's scenery number: Bkd8 1000 + 10 *
scenery + 0 or 1. Scenery 0 is the tree line, 1 the links (sea, hills and a
town with a kirk), 2 the Irish clifftop (the Atlantic, a far headland with
its lighthouse, the mainland and clouds).

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


def finish(H, layers, anchor, grng=None):
    """Shrinks the supersampled layers, cuts them to their ramps with grain and
    returns (indices, anchor x, anchor y)."""
    grng = grng or rng
    h, w = layers[0][0].shape
    idx = np.zeros((h // SS, w // SS), dtype=np.uint8)
    for mask, name, shade in layers:
        cov = shrink(mask.astype(float), SS)
        s = shrink(np.where(mask, shade, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        grain = grng.random(idx.shape) - 0.5
        k = ramp_index(name, np.clip(s, 0, 1), grain * 0.9)
        idx[on] = k[on]
    return idx, round(anchor[0]), round(anchor[1])


def spr8(idx, ax, ay, px=None, py=None):
    h, w = idx.shape
    if px is None:
        px, py = ax, ay
    return struct.pack('>HHHHhh', w, h, ax, ay, px, py) + idx.astype(np.uint8).tobytes()


# Golfer poses the game shows, as frames of tools/golfer: address, half way
# back, the top of the swing and the finish. Spr8 2000 + 10 * pose + size.
POSES = (1, 2, 4, 13)
GOLFER_SIZES = (20, 40)


def golfer(frame, n):
    """A golfer frame from art/golfer (already on the master palette, 0
    clear), trimmed to its pixels. The point is the ball, at the club head
    in the address frame; all frames share one canvas, so it holds for each."""
    def load(f):
        return np.array(Image.open(ROOT / f'art/golfer/f{f:02d}-{n}.png'))
    a0 = load(1)
    h, w = a0.shape
    ys, xs = np.nonzero(a0[:, w * 2 // 3:])
    k = np.argmax(ys * 100 + xs)
    bx, by = int(xs[k] + w * 2 // 3), int(ys[k])
    ys, xs = np.nonzero(a0)
    feet = int(ys.max()) + 1
    fx = int(round(xs[ys == ys.max()].mean()))
    a = load(frame)
    ys, xs = np.nonzero(a)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return a[y0:y1, x0:x1].astype(np.uint8), fx - x0, feet - y0, bx - x0, by - y0


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


# Links art: the stone wall, a stone house, gorse and dune grass, and the
# links backdrop. They draw from their own random numbers, so the art above
# comes out the same as before.
OBJ_HOUSE, OBJ_WALL, OBJ_GORSE, OBJ_GRASS = 6, 8, 9, 10
LINKS_SIZES = {OBJ_HOUSE: (28, 84), OBJ_WALL: (8, 24), OBJ_GORSE: (12, 36), OBJ_GRASS: (8, 20)}
lrng = np.random.default_rng(1386)


def wall(H, seed):
    """A dry-stone wall section, 4 times as long as it's tall, with a row of
    upright cope stones along the top."""
    r = np.random.default_rng(seed)
    h, w = H * SS, H * 4 * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    cope = h * 0.22
    # coursed stones: rows of uneven height, joints dark
    rows = np.cumsum(r.uniform(0.6, 1.0, 8))
    rows = cope + rows / rows[-1] * (h - cope)
    shade = np.full((h, w), 0.55)
    joint = np.zeros((h, w), dtype=bool)
    top = cope
    for k, bot in enumerate(rows):
        band = (y >= top) & (y < bot)
        edges = np.cumsum(r.uniform(0.8, 1.6, 64)) * (bot - top) * 1.4 - r.random() * h
        cell = np.searchsorted(edges, x[0])
        bump = r.uniform(-0.18, 0.18, len(edges) + 1)
        shade[band] = (0.55 + bump[cell])[None, :].repeat(h, 0)[band]
        # each stone lit from the top left
        fy = np.clip((y - top) / max(bot - top, 1), 0, 1)
        shade[band] += (0.15 - fy[band] * 0.3)
        joint |= band & (np.abs(y - top) < SS * 0.6)
        for e in edges:
            joint |= band & (np.abs(x - e) < SS * 0.5)
        top = bot
    # cope stones on edge
    cw = h * 0.16
    cx = (x // cw)
    lean = (r.random(int(w // cw) + 2) - 0.5) * 0.3
    cop = (y < cope) & (y > cope * (0.15 + 0.3 * np.abs(np.sin(cx * 1.7))))
    cop &= (x % cw) > SS * 0.6
    shade[y < cope] = 0.6 + lean[cx.astype(int)][y < cope] - ((x % cw) / cw * 0.25)[y < cope]
    fine = r.random((h, w)) - 0.5
    shade = np.clip(shade + fine * 0.25 - joint * 0.45, 0, 1)
    body = (y >= cope) | cop
    # a ragged top line
    body &= ~((y < cope) & ~cop)
    return finish(H, [(body, 'wall', shade)], (w / 2 / SS, h / SS), lrng)


def house(H, seed):
    """An old stone house with a pitched slate roof and chimneys at the gables."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 1.6) * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    eave, ridge = h * 0.48, h * 0.12
    x0, x1 = w * 0.06, w * 0.94
    walls = (y >= eave) & (x >= x0) & (x < x1)
    # roof: two slopes seen from the long side, the near one lit
    roof = (y >= ridge) & (y < eave) & (x >= x0 - SS) & (x < x1 + SS)
    fine = r.random((h, w)) - 0.5
    stone = vnoise((h, w), SS * 2, seed) - 0.5
    ws = 0.62 + stone * 0.35 + fine * 0.2 - (y - eave) / h * 0.3
    # windows and a door, dark
    win = np.zeros((h, w), dtype=bool)
    for k in range(5):
        wx = x0 + (x1 - x0) * (k + 0.5) / 5
        if k == 2:
            win |= (np.abs(x - wx) < w * 0.035) & (y > h * 0.72)
        else:
            win |= (np.abs(x - wx) < w * 0.03) & (np.abs(y - h * 0.68) < h * 0.07)
    ws = np.where(win, 0.05, ws)
    slate = vnoise((h, w), SS, seed + 1) - 0.5
    rs = 0.32 + (y - ridge) / (eave - ridge) * 0.15 + slate * 0.2 + fine * 0.15
    rs = np.where(((y - ridge) % (SS * 1.5)) < SS * 0.4, rs - 0.15, rs)
    chim = np.zeros((h, w), dtype=bool)
    for cx in (x0 + w * 0.05, x1 - w * 0.05):
        chim |= (np.abs(x - cx) < w * 0.035) & (y > ridge - h * 0.1) & (y < ridge + h * 0.1)
    cs = 0.55 + fine * 0.2
    return finish(H, [(walls, 'wall', np.clip(ws, 0, 1)), (roof, 'grey', np.clip(rs, 0, 1)),
                      (chim, 'wall', np.clip(cs, 0, 1))], (w / 2 / SS, h / SS), lrng)


def gorse(H, seed):
    """A gorse clump: a dark spiny mound with yellow flowers on its lit side."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 1.6) * SS
    balls = []
    for _ in range(22):
        bx = w * (0.15 + r.random() * 0.7)
        rad = h * (0.2 + r.random() * 0.2)
        by = h - rad * (0.6 + r.random() * 0.9)
        balls.append((bx, max(rad * 0.9, by), r.random() * rad * 0.5, rad))
    zb, n, hit = spheres(h, w, balls)
    fine = r.random((h, w)) - 0.5
    spike = vnoise((h, w), max(2, SS * H // 18), seed + 1) - 0.5
    # spiny edge: thin out the outline with fine noise
    edge = shrink(hit.astype(float), 1)
    shade = lambert(n) * 0.55 + 0.02 + spike * 0.35 + fine * 0.25
    lit = lambert(n)
    flower = hit & (r.random((h, w)) < 0.01 + 0.3 * np.clip(lit - 0.5, 0, 1) ** 1.2) & (spike > 0)
    # flowers in small clumps a few supersamples across
    fl = shrink(flower.astype(float), SS).repeat(SS, 0).repeat(SS, 1) > 0.1
    fl &= hit
    del edge
    return finish(H, [(hit, 'pine', np.clip(shade, 0, 1)),
                      (fl, 'gold', np.clip(0.55 + lit * 0.4 + fine * 0.2, 0, 1))],
                  (w / 2 / SS, h / SS), lrng)


def grass(H, seed):
    """A tuft of marram grass: thin pale blades fanning out from the sand."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * 1.4) * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    hit = np.zeros((h, w), dtype=bool)
    shade = np.zeros((h, w))
    for k in range(26):
        lean = r.uniform(-0.9, 0.9)
        tall = h * r.uniform(0.45, 1.0)
        bx = w / 2 + r.uniform(-0.18, 0.18) * w
        t = np.clip((h - y) / tall, 0, 1)
        cxl = bx + lean * (h - y) * 0.6 + lean * t * t * tall * 0.4
        thick = SS * (1.1 - t * 0.7)
        blade = (np.abs(x - cxl) < thick) & (y > h - tall)
        hit |= blade
        shade = np.where(blade, 0.45 + 0.45 * t + (0.15 if lean < 0 else -0.1) + r.uniform(-0.1, 0.1), shade)
    return finish(H, [(hit, 'oob', np.clip(shade, 0, 1))], (w / 2 / SS, h / SS), lrng)


def links_backdrop(width, height, seed):
    """The links backdrop: the sea along the horizon, distant hills across
    part of the turn, and a town with a kirk spire on the far shore. Repeats
    twice in a full turn. The bottom rows are sea, so the terrain's far edge
    meets it."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 2 * math.pi
    r = np.random.default_rng(seed)

    def wave(freqs):
        out = np.zeros_like(u)
        for f, amp in freqs:
            out += amp * np.sin(u * f + r.random() * 2 * math.pi)
        return out
    sea_top = h * 0.80
    # hills over about half the strip, fading to nothing at their ends
    env = np.clip(np.sin(u - 0.6) * 1.6, 0, 1) ** 0.8
    hills = sea_top - env * h * (0.40 + wave([(3, 0.08), (7, 0.05), (19, 0.025)]))
    near = sea_top - np.clip(np.sin(u * 1.0 + 2.2) * 1.3, 0, 1) * h * (0.18 + wave([(5, 0.04), (23, 0.02)]))
    fine = lrng.random((h, w)) - 0.5
    hill = (y >= hills[None, :]) & (y < sea_top)
    hs = np.clip(0.85 - (y - hills[None, :]) / h * 0.5 + (vnoise((h, w), SS * 3, seed) - 0.5) * 0.25 + fine * 0.1, 0, 1)
    nh = (y >= near[None, :]) & (y < sea_top)
    ns = np.clip(0.55 - (y - near[None, :]) / h * 0.6 + (vnoise((h, w), SS * 2, seed + 1) - 0.5) * 0.3 + fine * 0.15, 0, 1)
    # the town: a row of houses with a kirk tower and spire, on the near shore
    town = np.zeros((h, w), dtype=bool)
    ts = np.zeros((h, w))
    tx0 = w * 0.30
    k = 0
    xx = tx0
    while xx < w * 0.36:
        bw = SS * r.uniform(2.5, 5)
        bh = h * r.uniform(0.06, 0.13)
        m = (x >= xx) & (x < xx + bw) & (y >= sea_top - bh) & (y < sea_top)
        town |= m
        ts = np.where(m, 0.35 + 0.4 * (k % 2) + fine * 0.1, ts)
        xx += bw + SS * r.uniform(0, 1.5)
        k += 1
    kx = w * 0.335
    tower = (np.abs(x - kx) < SS * 1.6) & (y >= sea_top - h * 0.30) & (y < sea_top)
    spire = (np.abs(x - kx) < SS * 1.6 * np.clip((y - (sea_top - h * 0.48)) / (h * 0.18), 0, 1)) & \
            (y >= sea_top - h * 0.48) & (y < sea_top - h * 0.30)
    kirk = tower | spire
    ks = np.where(x < kx, 0.7, 0.35) + fine * 0.1
    sea = y >= sea_top
    ss = np.clip(0.45 + (y - sea_top) / (h - sea_top) * 0.3 +
                 np.where(((y // SS) % 2 == 0) & (fine > 0.3), 0.25, 0) + fine * 0.1, 0, 1)
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, s in ((hill, 'far', hs * 0.45 + 0.55), (nh, 'far', ns * 0.7),
                          (town, 'wall', ts), (kirk, 'wall', ks), (sea, 'water', ss)):
        cov = shrink(mask.astype(float), SS)
        sh = shrink(np.where(mask, s, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(sh, 0, 1), (lrng.random(idx.shape) - 0.5) * 0.9)
        idx[on] = kk[on]
    return idx


def links_art(res, prev):
    makers = {OBJ_HOUSE: house, OBJ_WALL: wall, OBJ_GORSE: gorse, OBJ_GRASS: grass}
    for t, sizes in LINKS_SIZES.items():
        for k, H in enumerate(sizes):
            idx, ax, ay = makers[t](H, 100 * t + k)
            rid = 1000 + t * 10 + k
            res.append(('Spr8', rid, spr8(idx, ax, ay)))
            if prev:
                preview(idx, prev / f'spr-{rid}.png', 4)
    # Scenery 1, the links: Bkd8 1010 and 1011.
    big = links_backdrop(968, 40, 11)
    for k, idx in enumerate((big.reshape(20, 2, 484, 2)[:, 0, :, 0], big)):
        res.append(('Bkd8', 1010 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1010 + k}.png', 2)


# Carrig Head (scenery 2): the Atlantic, a far headland with its lighthouse,
# the mainland hills and the clouds above, from their own random numbers.
crng = np.random.default_rng(2386 + 2)


def pnoise(h, w, cell, seed, cellx=None):
    """Value noise that wraps round left to right, about cell pixels a step
    down and cellx across."""
    r = np.random.default_rng(seed)
    cellx = cellx or cell
    gh, gw = h // cell + 2, max(1, round(w / cellx))
    g = r.random((gh, gw + 1))
    g[:, gw] = g[:, 0]
    y = np.arange(h)[:, None] / cell
    x = np.arange(w)[None, :] * (gw / w)
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int)
    fy, fx = y - y0, x - x0
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx
    b = g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def carrig_backdrop(width, height, seed):
    """The clifftop backdrop, half a turn wide (it repeats twice a turn).
    Across it in degrees: open Atlantic with a far sea stack (0 to 55), a
    far headland with sheer cliffs and the lighthouse on its tip (55 to
    100), the mainland's green hills with white cottages (105 to 175), then
    the sea again. Sea along the bottom rows so the terrain's far edge
    meets it, and a band of Atlantic cumulus in the rows above."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 180.0
    ur = np.radians(u) * 2          # a whole turn across the strip, for waves that wrap
    fine = crng.random((h, w)) - 0.5
    land_h = h * 40 / 72              # the bottom 40 rows hold the land and sea
    sea_top = h - land_h * 0.2

    def bump(c, half, p=1.0):
        return np.clip(1 - np.abs(u - c) / half, 0, 1) ** p

    # the far headland: rises from the sea, a long rolling top, sheer at the tip
    head = np.clip((u - 55) / 12, 0, 1) ** 0.7 * np.where(u < 100.5, 1.0, 0.0)
    head *= 0.34 + 0.05 * np.sin(ur * 7 + 1) + 0.025 * np.sin(ur * 23)
    head *= np.clip((100.5 - u) / 4, 0.6, 1)       # the point dips before the sheer end
    # the mainland: rolling hills, highest in the middle
    hills = (bump(140, 36, 0.8) * (0.36 + 0.06 * np.sin(ur * 5 + 2) + 0.03 * np.sin(ur * 19))
             + bump(112, 12, 1.5) * 0.12)
    stack = bump(30, 1.6, 0.4) * 0.16
    prof = np.maximum(np.maximum(head, hills), stack)
    top = sea_top - prof * land_h
    land = (y >= top[None, :]) & (y < sea_top)
    # cliffs: the lower part of the headland and the stack are bare rock faces
    cliff_line = sea_top - np.where(u < 101, head, stack) * land_h * 0.6
    rocky = land & (((u > 62) & (u < 100.6)) | (np.abs(u - 30) < 2))[None, :] & (y >= cliff_line[None, :])
    tip = (u > 98.6)[None, :] & land
    rocky |= tip & (y >= (top + (sea_top - top) * 0.12)[None, :])
    # grass on top, lit from the left, darker further down
    gs = np.clip(0.8 - (y - top[None, :]) / land_h * 1.4 + (pnoise(h, w, SS * 4, seed) - 0.5) * 0.25, 0, 1)
    rs = np.clip(0.6 + (pnoise(h, w, SS * 2, seed + 1, SS * 6) - 0.5) * 0.4 - np.where(tip, 0.12, 0), 0, 1)
    rs -= ((y // (SS * 2)) % 3 == 0) * 0.1
    # white cottages on the mainland
    cot = np.zeros((h, w), dtype=bool)
    for c in (118, 127, 131, 146, 158, 163):
        cx = c / 180 * w
        yy = sea_top - np.interp(cx, x[0], prof) * land_h + SS * 3
        cot |= (np.abs(x - cx) < SS * 1.2) & (y >= yy) & (y < yy + SS * 1.6)
    # the lighthouse on the tip of the headland
    lx = 97.6 / 180 * w
    lbase = sea_top - np.interp(lx, x[0], prof) * land_h + SS
    lt = lbase - SS * 11
    tower = (np.abs(x - lx) < SS * (1.1 + 0.5 * (y - lt) / (lbase - lt))) & (y >= lt) & (y < lbase)
    lband = tower & ((np.abs(y - (lt + SS * 4.5)) < SS * 1.0) | (np.abs(y - (lt + SS * 8)) < SS * 1.0))
    lamp = (np.abs(x - lx) < SS * 0.8) & (y >= lt - SS * 2) & (y < lt)
    # the sea, a little lighter to the horizon with streaks of swell
    sea = y >= sea_top
    ss = np.clip(0.42 + (y - sea_top) / (h - sea_top) * 0.25 +
                 np.where(((y // SS) % 2 == 0) & (fine > 0.32), 0.25, 0) + fine * 0.1, 0, 1)
    foot = sea & (np.abs(y - sea_top) < SS * 1.2) & ((rocky.any(axis=0))[None, :])
    # cumulus: heaps with flat grey bases in a band above the horizon,
    # more over the open sea, the sky kept clear over the lighthouse
    base = h * (0.50 + 0.06 * np.sin(ur * 3 + 0.5))[None, :]
    heap = pnoise(h, w, SS * 9, seed + 5, SS * 30) * 0.7 + pnoise(h, w, SS * 4, seed + 6, SS * 9) * 0.3
    over_sea = np.clip(np.cos(ur - np.radians(70)) * 0.5 + 0.5, 0, 1)[None, :]
    rise = (base - y) / h                          # how far above the base
    want = 0.50 - over_sea * 0.12 + np.clip(rise - 0.12, 0, 1) * 1.2 + np.clip(-rise, 0, 1) * 9
    want += bump(97.6, 9)[None, :] * 0.6
    cloud = (heap > want) & ~land & (y < sea_top - SS * 2)
    cs = 0.62 + np.clip(rise, 0, 0.4) * 0.9 - np.clip(heap - want, 0, 1) * 0.5
    cs = np.round(np.clip(cs, 0, 1) * 5) / 5
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, sh, dith in ((cloud, 'grey', 0.74 + cs * 0.26, 0.2),
                                 (land, 'far', gs * 0.55 + 0.25, 0.9),
                                 (rocky, 'rock', rs * 0.8 + 0.1, 0.9),
                                 (cot, 'wall', np.full((h, w), 0.95), 0.3),
                                 (sea, 'water', ss, 0.9), (foot, 'grey', np.full((h, w), 0.9), 0.5),
                                 (tower, 'grey', np.full((h, w), 0.97), 0.1), (lband, 'red', np.full((h, w), 0.6), 0.1),
                                 (lamp, 'gold', np.full((h, w), 0.9), 0.1)):
        cov = shrink(mask.astype(float), SS)
        shd = shrink(np.where(mask, sh, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(shd, 0, 1), (crng.random(idx.shape) - 0.5) * dith)
        idx[on] = kk[on]
    return idx


def carrig_art(res, prev):
    """Scenery 2, Carrig Head: Bkd8 1020 and 1021."""
    big = carrig_backdrop(968, 72, 21)
    for k, idx in enumerate((big.reshape(36, 2, 484, 2)[:, 0, :, 0], big)):
        res.append(('Bkd8', 1020 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1020 + k}.png', 2)


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
    for p, frame in enumerate(POSES):
        for k, n in enumerate(GOLFER_SIZES):
            idx, fx, fy, bx, by = golfer(frame, n)
            res.append(('Spr8', 2000 + 10 * p + k, spr8(idx, fx, fy, bx, by)))
            if prev:
                preview(idx, prev / f'golfer-{p}-{n}.png', 4)
    # Wide enough for half a turn: 160 px is the view's 59.5 degrees (render.c).
    for k, (w, h) in enumerate(((484, 20), (968, 40))):
        idx = backdrop(w, h, 7) if k == 1 else None
        if k == 0:
            big = backdrop(968, 40, 7)
            idx = big.reshape(20, 2, 484, 2)[:, 0, :, 0]
        res.append(('Bkd8', 1000 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1000 + k}.png', 2)
    links_art(res, prev)
    carrig_art(res, prev)
    import mkstyle
    mkstyle.make(res, prev)
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
