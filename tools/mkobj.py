#!/usr/bin/env python3
"""Makes the renderer's art for the test hole, cut to the master palette
(tools/mkpal.py), and packs it for the core:
  OUT/Spr8-ID.bin  object sprites, ID 1000 + type * 10 + size (0 small, 1 big),
                   the golfer's poses, base 2000 (male) or 2100 (female) +
                   10 * pose + 0 or 1 for 40 or 80 px, and the course sets from tools/mkstyle.py
  OUT/Bkd8-ID.bin  the backdrop strip, 1000 for 160 wide screens, 1001 for 320
  OUT/obj.rcp      PilRC DATA lines for all of them
and previews to PREVIEW if given.

Spr8: big-endian u16 width, height, anchor x, anchor y (the base, on the
ground), point x, point y (the golfer's ball, else the anchor), then width *
height palette indices row by row, 0 clear.
Bkd8: u16 width, height, then each row packed with PackBits on its own. 0 is
clear. A whole turn is 968 px on 160 wide screens and 1936 on 320; a
narrower strip repeats (the plain tree line is half a turn). Across a
whole turn strip, degree d shows straight ahead when the view faces
29.75 - d degrees on the course (angles as in doc/course-format.md, with
each hole's turn added), so a landmark at d is seen looking that way.

Trees and the rest are drawn as lit shapes with grain and no outline, lit
from the left and behind the camera like the golfer and the terrain. The
art per course style is in tools/mkstyle.py.

Backdrops come in sets by the course's scenery number: Bkd8 1000 + 10 *
scenery + 0 or 1. Scenery 0 is the tree line, 1 the links (sea, hills and a
town with a kirk), 2 the Irish clifftop (the Atlantic, a far headland with
its lighthouse, the mainland and clouds), 3 the Japanese bay (the sea, a
snow-capped volcano, wooded ridges with a pagoda and thin cloud), 4 the
sandbelt (low sandy hills, scrub and gums in the heat haze), 5 the bush
(a far escarpment, a granite koppie and flat-topped acacias under a warm
sky), 6 the Rockies (snow-capped peaks over wooded foothills and spruce).

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
GOLFER_SIZES = (40, 80)


def golfer(frame, n, female=False):
    """A golfer frame from art/golfer (already on the master palette, 0
    clear), trimmed to its pixels. The point is the ball, at the club head
    in the address frame; all frames share one canvas, so it holds for each."""
    folder = ROOT / 'art/golfer'
    if female:
        folder /= 'female'

    def load(f):
        return np.array(Image.open(folder / f'f{f:02d}-{n}.png'))
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


def half(big):
    """The 160 wide screens' strip from the 320 one: every other pixel and row."""
    return big[::2, ::2]


def dbump(d, c, half_w, p=1.0):
    """1 at degree c falling to 0 half_w degrees either side, wrapping round the turn."""
    a = np.abs((d - c + 180.0) % 360.0 - 180.0)
    return np.clip(1 - a / half_w, 0, 1) ** p


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
    """The links backdrop, a whole turn: the sea along the horizon all
    round, distant hills behind a town with a kirk spire on the far shore
    (the kirk at 240 degrees), lower hills and a far headland the other way
    and dunes here and there. The bottom rows are sea, so the terrain's far
    edge meets it."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    d = x[0] / w * 360.0
    u = np.radians(d)
    r = np.random.default_rng(seed)

    def wave(freqs):
        out = np.zeros_like(u)
        for f, amp in freqs:
            out += amp * np.sin(u * f + r.random() * 2 * math.pi)
        return out
    sea_top = h * 0.80
    # far hills: a long range behind the town, a lower one and a headland across the water
    env = np.maximum(np.maximum(dbump(d, 262, 82, 0.8), dbump(d, 105, 45, 0.9) * 0.7), dbump(d, 40, 14, 0.7) * 0.55)
    hills = sea_top - env * h * (0.40 + wave([(5, 0.08), (11, 0.05), (37, 0.025)]))
    near_env = np.maximum(np.maximum(dbump(d, 205, 26), dbump(d, 340, 30)), dbump(d, 140, 18) * 0.8)
    near = sea_top - np.clip(near_env * 1.3, 0, 1) * h * (0.18 + wave([(9, 0.04), (45, 0.02)]))
    fine = lrng.random((h, w)) - 0.5
    hill = (y >= hills[None, :]) & (y < sea_top)
    hs = np.clip(0.85 - (y - hills[None, :]) / h * 0.5 + (vnoise((h, w), SS * 3, seed) - 0.5) * 0.25 + fine * 0.1, 0, 1)
    nh = (y >= near[None, :]) & (y < sea_top)
    ns = np.clip(0.55 - (y - near[None, :]) / h * 0.6 + (vnoise((h, w), SS * 2, seed + 1) - 0.5) * 0.3 + fine * 0.15, 0, 1)
    # the town: a row of houses with a kirk tower and spire, on the near shore
    town = np.zeros((h, w), dtype=bool)
    ts = np.zeros((h, w))
    k = 0
    xx = w * 234 / 360
    while xx < w * 245 / 360:
        bw = SS * r.uniform(2.5, 5)
        bh = h * r.uniform(0.06, 0.13)
        m = (x >= xx) & (x < xx + bw) & (y >= sea_top - bh) & (y < sea_top)
        town |= m
        ts = np.where(m, 0.35 + 0.4 * (k % 2) + fine * 0.1, ts)
        xx += bw + SS * r.uniform(0, 1.5)
        k += 1
    kx = w * 240.3 / 360
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
    big = links_backdrop(1936, 40, 11)
    for k, idx in enumerate((half(big), big)):
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
    """The clifftop backdrop, a whole turn. Across it in degrees: the
    mainland's green hills with white cottages and blue mountains behind
    (0 to 155), the open Atlantic with a far sea stack (155 to 235), a far
    headland with sheer cliffs and the lighthouse on its tip (235 to 280,
    the lighthouse at 277.6, seen looking 112 degrees, out along the course),
    then more hills and cottages (285 to 360). Sea along the bottom rows so
    the terrain's far edge meets it, and a band of Atlantic cumulus in the
    rows above, thickest over the open sea."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 360.0
    ur = np.radians(u) * 2          # waves that wrap round the turn
    fine = crng.random((h, w)) - 0.5
    land_h = h * 40 / 72              # the bottom 40 rows hold the land and sea
    sea_top = h - land_h * 0.2

    def bump(c, half_w, p=1.0):
        return dbump(u, c, half_w, p)

    # the far headland: rises from the sea, a long rolling top, sheer at the tip
    head = np.clip((u - 235) / 12, 0, 1) ** 0.7 * np.where(u < 280.5, 1.0, 0.0)
    head *= 0.34 + 0.05 * np.sin(ur * 7 + 1) + 0.025 * np.sin(ur * 23)
    head *= np.clip((280.5 - u) / 4, 0.6, 1)       # the point dips before the sheer end
    # the mainland: rolling hills either side of the open sea
    hills = (bump(320, 36, 0.8) * (0.36 + 0.06 * np.sin(ur * 5 + 2) + 0.03 * np.sin(ur * 19))
             + bump(292, 12, 1.5) * 0.12)
    hills = np.maximum(hills, bump(68, 88, 0.6) * (0.30 + 0.05 * np.sin(ur * 4 + 0.7) + 0.04 * np.sin(np.radians(u) * 9 + 2)
                                                  + 0.02 * np.sin(ur * 21)))
    stack = bump(210, 1.6, 0.4) * 0.16
    prof = np.maximum(np.maximum(head, hills), stack)
    top = sea_top - prof * land_h
    land = (y >= top[None, :]) & (y < sea_top)
    # blue mountains inland, behind the hills
    mp = bump(78, 36, 1.1) * (0.66 + 0.04 * np.sin(ur * 9)) + bump(122, 15, 1.0) * 0.46 + bump(40, 12, 1.2) * 0.42
    mtop = sea_top - mp * land_h
    mtn = (y >= mtop[None, :]) & (y < sea_top) & (mp > prof)[None, :] & ~land
    ms = np.clip(0.80 - (y - mtop[None, :]) / land_h * 0.3 + np.where(u < 78, 0.06, -0.06)[None, :]
                 + (pnoise(h, w, SS * 3, seed + 7, SS * 8) - 0.5) * 0.15, 0, 1)
    # cliffs: the lower part of the headland and the stack are bare rock faces
    cliff_line = sea_top - np.where(u > 200, np.where(u < 281, head, 0), 0) * land_h * 0.6
    cliff_line = np.where(np.abs(u - 210) < 2, sea_top - stack * land_h * 0.6, cliff_line)
    rocky = land & (((u > 242) & (u < 280.6)) | (np.abs(u - 210) < 2))[None, :] & (y >= cliff_line[None, :])
    tip = ((u > 278.6) & (u < 281))[None, :] & land
    rocky |= tip & (y >= (top + (sea_top - top) * 0.12)[None, :])
    # grass on top, lit from the left, darker further down
    gs = np.clip(0.8 - (y - top[None, :]) / land_h * 1.4 + (pnoise(h, w, SS * 4, seed) - 0.5) * 0.25, 0, 1)
    rs = np.clip(0.6 + (pnoise(h, w, SS * 2, seed + 1, SS * 6) - 0.5) * 0.4 - np.where(tip, 0.12, 0), 0, 1)
    rs -= ((y // (SS * 2)) % 3 == 0) * 0.1
    # white cottages on the mainland
    cot = np.zeros((h, w), dtype=bool)
    for c in (15, 28, 44, 58, 96, 112, 137, 298, 307, 311, 326, 338, 343):
        cx = c / 360 * w
        yy = sea_top - np.interp(cx, x[0], prof) * land_h + SS * 3
        cot |= (np.abs(x - cx) < SS * 1.2) & (y >= yy) & (y < yy + SS * 1.6)
    # the lighthouse on the tip of the headland
    lx = 277.6 / 360 * w
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
    over_sea = np.clip(np.cos(np.radians(u - 215)) * 0.5 + 0.5, 0, 1)[None, :]
    rise = (base - y) / h                          # how far above the base
    want = 0.50 - over_sea * 0.12 + np.clip(rise - 0.12, 0, 1) * 1.2 + np.clip(-rise, 0, 1) * 9
    want += bump(277.6, 9)[None, :] * 0.6
    cloud = (heap > want) & ~land & ~mtn & (y < sea_top - SS * 2)
    cs = 0.62 + np.clip(rise, 0, 0.4) * 0.9 - np.clip(heap - want, 0, 1) * 0.5
    cs = np.round(np.clip(cs, 0, 1) * 5) / 5
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, sh, dith in ((cloud, 'grey', 0.74 + cs * 0.26, 0.2),
                                 (mtn, 'far', ms, 0.6),
                                 (land, 'far', gs * 0.55 + 0.25, 0.9),
                                 (rocky, 'rock', rs * 0.8 + 0.1, 0.9),
                                 (cot, 'wall', np.full((h, w), 0.95), 0.3),
                                 (sea, 'water', ss, 0.9), (foot, 'grey', np.full((h, w), 0.9), 0.5),
                                 # the tower's right side in shade, so it shows at 2 bits too
                                 (tower, 'grey', np.where(x < lx - SS * 0.5, 0.97, 0.3), 0.1), (lband, 'red', np.full((h, w), 0.4), 0.1),
                                 (lamp, 'gold', np.full((h, w), 0.9), 0.1)):
        cov = shrink(mask.astype(float), SS)
        shd = shrink(np.where(mask, sh, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(shd, 0, 1), (crng.random(idx.shape) - 0.5) * dith)
        idx[on] = kk[on]
    return idx


def carrig_art(res, prev):
    """Scenery 2, Carrig Head: Bkd8 1020 and 1021."""
    big = carrig_backdrop(1936, 72, 21)
    for k, idx in enumerate((half(big), big)):
        res.append(('Bkd8', 1020 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1020 + k}.png', 2)


# Sakura Bay (scenery 3): the bay, a snow-capped volcano across it, wooded
# hills and thin cloud, from their own random numbers.
srng = np.random.default_rng(2386 + 3)


def sakura_backdrop(width, height, seed):
    """The bay backdrop, a whole turn. Across it in degrees: wooded ridges
    (0 to 85), open sea with far islands and a low far coast (85 to 205),
    the volcano rising over low wooded foothills, its peak at 240 with snow
    down its gullies (205 to 280, seen looking 150 degrees, the way most
    tees face), then wooded ridges round the bay with a pagoda on one (280
    to 360). Sea along the bottom rows so the terrain's far edge meets it,
    a band of thin cloud across the volcano's flank. Below, u is the degree
    less 180, so the volcano is at u = 60."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 360.0 - 180.0
    ur = np.radians(u) * 2          # waves that wrap round the turn
    fine = srng.random((h, w)) - 0.5
    sea_top = h - h * 0.11

    def bump(c, half, p=1.0):
        return dbump(u, c, half, p)

    # the volcano: concave sides, a flat top with a small notch for the crater
    du = np.abs(u - 60)
    cone = np.clip(1 - du / 30, 0, 1) ** 1.7
    cone = np.minimum(cone, 0.80) + np.where(du < 2.2, -0.012, 0)
    vtop = sea_top - cone * h * 0.72
    peak_y = sea_top - 0.80 * h * 0.72
    vol = (y >= vtop[None, :]) & (y < sea_top) & (cone > 0.002)[None, :]
    # snow: down to about a third of the height, in tongues down the gullies
    gul = pnoise(h, w, SS * 30, seed + 1, SS * 12)
    tongue = np.clip(1 - np.abs(np.sin(np.radians(u - 61) * 70)), 0, 1)[None, :] ** 3
    snow_line = peak_y + (sea_top - peak_y) * (0.30 + tongue * (0.12 + 0.30 * gul) + (gul - 0.5) * 0.08)
    snow = vol & (y < snow_line)
    # lit from the left: the left flank light, the right in shade
    side = np.clip((60 - u) / 12, -1, 1)[None, :]
    vs = np.clip(0.55 + side * 0.18 + (gul - 0.5) * 0.25 + fine * 0.08 - (y - peak_y) / h * 0.15, 0, 1)
    ss_ = np.clip(0.80 + side * 0.18 + (gul - 0.5) * 0.15, 0, 1)
    # foothills under it and wooded ridges round the bay
    foot = (bump(52, 34, 0.7) * (0.14 + 0.03 * np.sin(ur * 9 + 1) + 0.015 * np.sin(ur * 31))
            + bump(18, 5, 0.8) * 0.07)
    ridge = bump(140, 42, 0.6) * (0.30 + 0.06 * np.sin(ur * 6 + 2) + 0.03 * np.sin(ur * 17 + 1))
    ridge = np.maximum(ridge, bump(108, 10, 1.2) * 0.16)
    # the other way round the bay: more wooded ridges, then far islands and a low coast over the sea
    ridge = np.maximum(ridge, bump(-142, 48, 0.6) * (0.26 + 0.05 * np.sin(ur * 5 + 0.3) + 0.03 * np.sin(np.radians(u) * 13 + 1)))
    foot = np.maximum(foot, bump(-70, 26, 0.5) * (0.045 + 0.01 * np.sin(ur * 11)))
    foot = np.maximum(foot, np.maximum(bump(-58, 5, 0.8) * 0.075, bump(-36, 3.5, 0.8) * 0.05))
    tops = np.abs(np.sin(u * 3.1)) * 0.015 + np.abs(np.sin(u * 7.3 + 1)) * 0.01   # crowns of trees
    hill_p = np.maximum(foot, ridge) + np.where(np.maximum(foot, ridge) > 0.01, tops, 0)
    htop = sea_top - hill_p * h
    hill = (y >= htop[None, :]) & (y < sea_top) & (hill_p > 0.005)[None, :]
    near_ridge = (ridge >= foot)[None, :] & hill
    hs = np.clip(0.55 - (y - htop[None, :]) / h * 1.0 + (pnoise(h, w, SS * 3, seed + 2, SS * 3) - 0.5) * 0.35, 0, 1)
    hs = np.where(near_ridge, hs * 0.75, hs)
    # a pagoda on the ridge: stacked roofs, wider at the bottom
    px = 330 / 360 * w
    pbase = sea_top - np.interp(px, x[0], hill_p * h) + SS
    pag = np.zeros((h, w), dtype=bool)
    for k in range(4):
        ty = pbase - SS * (1.0 + 1.6 * k)
        half = SS * (2.0 - 0.3 * k)
        pag |= (np.abs(x - px) < half) & (y >= ty - SS * 0.6) & (y < ty)
        pag |= (np.abs(x - px) < SS * 0.8) & (y >= ty) & (y < ty + SS * 1.0)
    pag |= (np.abs(x - px) < SS * 0.3) & (y >= pbase - SS * 8.5) & (y < pbase - SS * 6)
    # the sea, a little lighter to the horizon with streaks of swell
    sea = y >= sea_top
    ss = np.clip(0.45 + (y - sea_top) / (h - sea_top) * 0.25 +
                 np.where(((y // SS) % 2 == 0) & (fine > 0.32), 0.25, 0) + fine * 0.1, 0, 1)
    # thin cloud: long streaks, one band across the volcano's flank
    band_y = peak_y + (sea_top - peak_y) * 0.42 + h * 0.03 * np.sin(ur * 2 + 0.4)[None, :]
    streak = pnoise(h, w, SS * 3, seed + 5, SS * 26) * 0.75 + pnoise(h, w, SS * 2, seed + 6, SS * 8) * 0.25
    want = 0.48 + np.clip(np.abs(y - band_y) / (h * 0.035), 0, 3) * 0.3 - bump(60, 26)[None, :] * 0.06
    hi_y = h * (0.22 + 0.04 * np.sin(ur * 3 + 2))[None, :]
    want = np.minimum(want, 0.70 + np.clip(np.abs(y - hi_y) / (h * 0.035), 0, 3) * 0.25 + bump(60, 20)[None, :] * 0.4)
    cloud = (streak > want) & (y < sea_top - SS * 2)
    cs = np.clip(0.80 + (streak - want) * 1.5, 0, 1)
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, sh, dith in ((vol, 'far', vs * 0.5 + 0.12, 0.9),
                                 (snow, 'grey', 0.72 + ss_ * 0.28, 0.4),
                                 (hill, 'far', hs * 0.5 + 0.08, 0.9),
                                 (pag, 'grey', np.full((h, w), 0.15), 0.1),
                                 (cloud, 'grey', cs, 0.2),
                                 (sea, 'water', ss, 0.9)):
        cov = shrink(mask.astype(float), SS)
        shd = shrink(np.where(mask, sh, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(shd, 0, 1), (srng.random(idx.shape) - 0.5) * dith)
        idx[on] = kk[on]
    return idx


def sakura_art(res, prev):
    """Scenery 3, Sakura Bay: Bkd8 1030 and 1031."""
    big = sakura_backdrop(1936, 72, 31)
    for k, idx in enumerate((half(big), big)):
        res.append(('Bkd8', 1030 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1030 + k}.png', 2)


# Banksia Heath (scenery 4): low sandy hills, scrub and gums in the heat
# haze, from their own random numbers.
brng = np.random.default_rng(2386 + 4)


def banksia_backdrop(width, height, seed):
    """The sandbelt backdrop, a whole turn:
    far low ranges, faint in the haze, with low sandy hills in front
    patched with scrub, then a line of distant gums and heath along the
    bottom rows so the terrain's far edge meets it. Heat haze over it all:
    a pale band at the hills' feet broken into shimmering streaks, which
    lift the bottoms of the gums off the ground like a mirage."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 360.0
    ur = np.radians(u) * 2          # waves that wrap round the turn
    u1 = np.radians(u)              # and ones that only repeat once a turn
    fine = brng.random((h, w)) - 0.5
    ground = h - SS * 2.5             # the bottom rows: the heath meets the terrain here

    # the far ranges: long and low, highest in two places
    far_p = (0.40 + 0.05 * np.sin(ur * 2 + 0.6) + 0.06 * np.sin(u1 * 3 + 1.4) + 0.04 * np.sin(ur * 5 + 2.1)
             + 0.03 * np.sin(u1 * 7 + 0.2) + 0.02 * np.sin(ur * 13 + 0.3))
    far_p *= 0.70 + 0.30 * np.cos(u1 - 1.2)
    far_top = ground - far_p * h * 0.95
    far = y >= far_top[None, :]
    # sandy hills: rounded dunes, bare sand on the crests, scrub lower down
    sh_p = (0.22 + 0.05 * np.sin(ur * 3 + 1.0) + 0.04 * np.sin(u1 * 5 + 2.6) + 0.035 * np.sin(ur * 7 + 0.4)
            + 0.02 * np.sin(u1 * 17 + 1.1) + 0.025 * np.abs(np.sin(ur * 11 + 2)))
    sand_top = ground - sh_p * h * 0.85
    hill = y >= sand_top[None, :]
    depth = (y - sand_top[None, :]) / h
    scrub_n = pnoise(h, w, SS * 2, seed + 1, SS * 3)
    scrub = hill & (scrub_n + depth * 2.2 > 0.95)
    hs = np.clip(0.80 - depth * 1.2 + (pnoise(h, w, SS * 3, seed + 2, SS * 10) - 0.5) * 0.3 + fine * 0.1, 0, 1)
    cs = np.clip(0.45 + (scrub_n - 0.5) * 0.6 + fine * 0.2, 0, 1)
    # distant gums: clumps of ragged round crowns on thin pale trunks
    crowns = np.zeros((h, w), dtype=bool)
    trunk = np.zeros((h, w), dtype=bool)
    crown_y = np.full(w, float(ground))
    for c in np.arange(0, 360, 1)[brng.random(360) < 0.10]:
        for k in range(int(brng.integers(1, 5))):
            cx = ((c + brng.normal(0, 1.2)) % 360) / 360 * w
            r = SS * brng.uniform(2.4, 3.8)
            cy = ground - SS * brng.uniform(2.5, 5.0) - r
            for b in range(3):
                bx, by = cx + brng.normal(0, r * 0.5), cy + brng.normal(0, r * 0.35)
                crowns |= ((x - bx) / (r * 0.8)) ** 2 + ((y - by) / (r * 0.6)) ** 2 < 1
            trunk |= (np.abs(x - cx) < SS * 0.5) & (y > cy) & (y < ground)
            crown_y = np.minimum(crown_y, np.where(np.abs(x[0] - cx) < r * 1.5, cy, ground))
    gum_n = pnoise(h, w, SS * 2, seed + 3, SS * 2)
    heath = y >= ground - SS * (1.0 + 1.2 * pnoise(h, w, SS * 2, seed + 5, SS * 2))
    gs = np.clip(0.42 + (gum_n - 0.5) * 0.5 - (y - crown_y[None, :]) / h * 0.8 + fine * 0.2, 0, 1)
    # heat haze: a pale band rising off the ground, streaked across
    band = (ground - y) / (h * 0.30)
    streak = pnoise(h, w, SS * 1, seed + 6, SS * 30)
    rows = ((y // SS) % 3 != 1)
    haze = (band > 0.05) & (band < 0.7) & rows & (streak > 0.58 + band * 0.4)
    mirage = (band > 0.08) & (band < 0.25) & (streak > 0.62)
    zs = np.clip(0.80 + (1 - band) * 0.15 + fine * 0.05, 0, 1)
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, sh_, dith in ((far, 'far', 0.82 + fine * 0.1, 0.9),
                                  (hill, 'sand', hs * 0.55 + 0.25, 0.9),
                                  (scrub, 'oob', cs * 0.6 + 0.25, 0.9),
                                  (crowns, 'far', gs * 0.6 + 0.12, 0.9),
                                  (trunk, 'wall', np.full((h, w), 0.8), 0.3),
                                  (heath, 'oob', cs * 0.5 + 0.2, 0.9),
                                  (haze & ~crowns, 'grey', zs, 0.3),
                                  (mirage, 'grey', zs, 0.3)):
        cov = shrink(mask.astype(float), SS)
        shd = shrink(np.where(mask, sh_, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(shd, 0, 1), (brng.random(idx.shape) - 0.5) * dith)
        idx[on] = kk[on]
    # the heat haze over the far ground: long pale streaks of shimmer,
    # closer together low down, over the far ranges and the sky above them
    yy, xx = np.mgrid[0:height, 0:width]
    top_px = (sand_top[::SS] / SS)[None, :]
    lift = (top_px - yy) / (height * 0.3)           # 0 at the sandy hills' tops, 1 well above
    gap = np.where(lift < 0.2, 2, 3)
    run = pnoise(height, width, 1, seed + 7, 40)
    glow = (lift > -0.2) & (lift < 0.45) & (yy % gap == 0) & (run > 0.3 + 0.8 * np.clip(lift, 0, 1))
    glow &= (idx == 0) | np.isin(idx, np.arange(RAMPS['far'][0], RAMPS['far'][0] + RAMPS['far'][1]))
    glow &= ~shrink(crowns.astype(float), SS).astype(bool)
    idx[glow] = RAMPS['sky'][0] + RAMPS['sky'][1] - 1
    return idx


def banksia_art(res, prev):
    """Scenery 4, Banksia Heath: Bkd8 1040 and 1041."""
    big = banksia_backdrop(1936, 48, 41)
    for k, idx in enumerate((half(big), big)):
        res.append(('Bkd8', 1040 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1040 + k}.png', 2)


# Crocodile Bend (scenery 5): the bush under a warm sky, flat-topped
# acacias, a far escarpment and a granite koppie, from their own random
# numbers.
krng = np.random.default_rng(2386 + 5)


def croc_backdrop(width, height, seed):
    """The bush backdrop, a whole turn. Across it in degrees: a long
    flat-topped escarpment far off with a cliff band under its rim and a
    gorge cut in it (12 to 100), low hills (100 to 125), a granite koppie of
    piled boulders (128 to 152), low hills, a lone flat-topped butte (223
    to 241), and a lower stretch of escarpment (285 to 340).
    Bush along the bottom rows so the terrain's far edge meets it, with
    flat-topped acacias standing out of it, and a warm glow low in the sky."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 360.0
    ur = np.radians(u) * 2          # waves that wrap round the turn
    u1 = np.radians(u)              # and ones that only repeat once a turn
    fine = krng.random((h, w)) - 0.5
    ground = h - SS * 3.0             # the bush meets the terrain here
    land_h = h * 0.62

    def bump(c, half, p=1.0):
        return dbump(u, c, half, p)

    # the escarpment: steep ends, a flat rim with a gorge, a talus slope below
    esc = np.clip((u - 12) / 7, 0, 1) ** 0.8 * np.clip((100 - u) / 6, 0, 1) ** 0.8
    esc *= 0.62 + 0.015 * np.sin(ur * 9 + 1) + 0.01 * np.sin(ur * 27)
    esc -= bump(58, 2.4, 0.6) * 0.16
    # the butte and the lower stretch of escarpment the other way
    butte = np.clip((u - 223) / 3, 0, 1) * np.clip((241 - u) / 3, 0, 1) * (0.30 + 0.01 * np.sin(ur * 31))
    esc2 = (np.clip((u - 285) / 8, 0, 1) ** 0.8 * np.clip((340 - u) / 9, 0, 1) ** 0.8
            * (0.40 + 0.012 * np.sin(ur * 11 + 2) + 0.008 * np.sin(ur * 29)))
    esc = np.maximum(esc, np.maximum(butte, esc2))
    # low hills and the koppie
    hills = ((0.13 + 0.025 * np.sin(ur * 5 + 0.7) + 0.025 * np.sin(u1 * 3 + 1.9) + 0.015 * np.sin(ur * 13 + 2))
             * (1 - bump(56, 60, 0.5) * 0.6))
    kop = bump(140, 13, 0.5) * 0.44
    prof = np.maximum(np.maximum(esc, hills), kop)
    top = ground - prof * land_h
    land = y >= top[None, :]
    esc_m = land & (esc >= np.maximum(hills, kop))[None, :] & (esc > 0.02)[None, :]
    rim = top + (ground - top) * 0.34
    cliff = esc_m & (y < rim[None, :])
    strata = ((y / SS) % 2.6 < 0.7) & cliff
    es = np.clip(0.80 - (y - top[None, :]) / h * 0.5 + (pnoise(h, w, SS * 3, seed + 1, SS * 8) - 0.5) * 0.2, 0, 1)
    cs = np.clip(0.62 + np.clip((u - 58) / 30, -0.3, 0.3)[None, :] * 0.4 + fine * 0.1, 0, 1)
    # the koppie: boulders piled in a dome, lit from the left, dark cracks
    kop_m = land & (kop >= np.maximum(esc, hills))[None, :] & (kop > 0.02)[None, :]
    ks = np.zeros((h, w))
    cracks = np.zeros((h, w), dtype=bool)
    kx = 140 / 360 * w
    for b in range(34):
        bx = kx + krng.normal(0, w * 8 / 360)
        r = SS * krng.uniform(2.0, 4.2)
        by = ground - np.interp(bx, x[0], kop * land_h) + r * krng.uniform(0.3, 2.5)
        dd = np.hypot((x - bx) / r, (y - by) / (r * 0.8))
        shade = np.clip(0.75 - ((x - bx) - (y - by)) / r * 0.25, 0, 1)
        ks = np.where(dd < 1, shade, ks)
        cracks |= (dd > 0.86) & (dd < 1.0)
    cracks &= kop_m
    hill_m = land & ~esc_m & ~kop_m
    hs = np.clip(0.55 - (y - top[None, :]) / h * 0.8 + (pnoise(h, w, SS * 3, seed + 2, SS * 5) - 0.5) * 0.3, 0, 1)
    # the bush along the bottom: a bumpy line of low scrub
    bush_top = ground - SS * (1.2 + 1.6 * pnoise(1, w, 1, seed + 3, SS * 2)[0])
    bush = y >= bush_top[None, :]
    bs = np.clip(0.40 + (pnoise(h, w, SS * 1, seed + 4, SS * 2) - 0.5) * 0.6 + fine * 0.2, 0, 1)
    # flat-topped acacias: a thin trunk forking under a wide flat crown
    crowns = np.zeros((h, w), dtype=bool)
    trunk = np.zeros((h, w), dtype=bool)
    for c in np.arange(0, 360, 1.0)[krng.random(360) < 0.16]:
        for k in range(int(krng.integers(1, 3))):
            cx = ((c + krng.normal(0, 1.0)) % 360) / 360 * w
            big = krng.uniform(0.7, 1.4) * (1.5 if krng.random() < 0.2 else 1.0)
            cw, ch = SS * 3.2 * big, SS * 0.9 * big
            cy = ground - SS * krng.uniform(3.5, 6.0) * big
            crowns |= (((x - cx) / cw) ** 2 + ((y - cy) / ch) ** 4 < 1) & (y < cy + ch * 0.5)
            fork = cy + ch * 1.6
            trunk |= (np.abs(x - cx) < SS * 0.45) & (y > fork) & (y < ground)
            for sd in (-1, 1):
                t_ = np.clip((fork - y) / (fork - cy), 0, 1)
                trunk |= (np.abs(x - (cx + sd * t_ * cw * 0.5)) < SS * 0.35) & (y <= fork) & (y > cy)
    gs = np.clip(0.30 + (pnoise(h, w, SS * 1, seed + 5, SS * 1) - 0.5) * 0.4 + fine * 0.2, 0, 1)
    # the warm glow low in the sky: solid near the ground, then long
    # streaks thinning upward, whole rows so the strip packs small
    glow_t = np.clip((y - h * 0.12) / (ground - h * 0.12), 0, 1)
    streak = pnoise(h, w, SS, seed + 6, SS * 40)
    row = (y // SS) % 2 == 0
    warm = ((glow_t > 0.72) | ((streak < glow_t * 1.1 - 0.15) & (row | (glow_t > 0.5)))) & ~land
    ws = np.clip(0.55 + glow_t * 0.45, 0, 1)
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, sh_, dith in ((warm, 'sand', ws, 0.0),
                                  (hill_m, 'far', hs * 0.45 + 0.30, 0.9),
                                  (esc_m, 'far', es * 0.35 + 0.55, 0.9),
                                  (cliff, 'wood', cs * 0.35 + 0.55, 0.9),
                                  (strata, 'wood', cs * 0.3 + 0.35, 0.6),
                                  (kop_m, 'rock', ks * 0.55 + 0.35, 0.9),
                                  (cracks, 'rock', np.full((h, w), 0.1), 0.3),
                                  (bush, 'oob', bs * 0.6 + 0.1, 0.9),
                                  (trunk, 'bark', np.full((h, w), 0.35), 0.3),
                                  (crowns, 'tree', gs, 0.9)):
        cov = shrink(mask.astype(float), SS)
        shd = shrink(np.where(mask, sh_, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(shd, 0, 1), (krng.random(idx.shape) - 0.5) * dith)
        idx[on] = kk[on]
    return idx


def croc_art(res, prev):
    """Scenery 5, Crocodile Bend: Bkd8 1050 and 1051."""
    big = croc_backdrop(1936, 64, 51)
    for k, idx in enumerate((half(big), big)):
        res.append(('Bkd8', 1050 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1050 + k}.png', 2)



# Cascade Springs (scenery 6): snow-capped peaks of the Rockies over wooded
# slopes and a spruce forest, from their own random numbers.
mrng = np.random.default_rng(2386 + 6)


def cascade_backdrop(width, height, seed):
    """The Rockies backdrop, a whole turn. Across it in degrees: a long slab
    mountain rising to a steep face on the right (5 to 70), a gap with a far
    glacier (70 to 88), a tall horn peak (88 to 122, the summit at 104, seen
    looking 286 degrees, as from the tee on 3, Glacier View), then a jagged
    range (122 to 182) and lower peaks with a twin summit the rest of the
    way round (182 to 360). Lit from the left:
    the faces left of each ridge line light, right of it in shade. Snow down
    to about half way, in tongues down the gullies, thin on the steep faces.
    In front, wooded foothills and a spruce forest along the bottom rows so
    the terrain's far edge meets it."""
    h, w = height * SS, width * SS
    y, x = np.mgrid[0:h, 0:w] + 0.5
    u = x[0] / w * 360.0
    ur = np.radians(u) * 2          # waves that wrap round the turn
    u1 = np.radians(u)              # and ones that only repeat once a turn
    ground = h - SS * 2.0             # the forest meets the terrain here
    land_h = h * 0.86

    # peaks: (centre, half width left, half width right, height, sharpness)
    peaks = [(16, 14, 26, 0.70, 0.9), (46, 22, 22, 0.80, 1.0), (79, 9, 9, 0.42, 1.1),
             (104, 16, 18, 1.00, 1.15), (134, 12, 12, 0.66, 1.0), (152, 10, 12, 0.74, 1.05),
             (170, 10, 14, 0.58, 1.0), (196, 12, 16, 0.62, 1.0), (221, 14, 13, 0.72, 1.05),
             (244, 9, 11, 0.50, 1.1), (262, 13, 9, 0.80, 1.0), (276, 8, 16, 0.76, 1.0),
             (306, 14, 12, 0.62, 1.05), (334, 12, 14, 0.68, 1.0), (376, 14, 26, 0.70, 0.9)]
    prof = np.zeros_like(u)
    owner = np.zeros(u.shape, dtype=int)
    for i, (c, hl, hr, ph, sh) in enumerate(peaks):
        d = np.where(u < c, (c - u) / hl, (u - c) / hr)
        v = ph * np.clip(1 - d, 0, 1) ** sh
        owner = np.where(v > prof, i, owner)
        prof = np.maximum(prof, v)
    # the wrapped copy of the first peak at 376 shares its owner
    owner = np.where(owner == len(peaks) - 1, 0, owner)
    # the slab mountain: a long gentle back rising to the left, a steep
    # broken face on the right
    slab = np.clip((u - 6) / 54, 0, 1) * np.clip((66 - u) / 6, 0, 1) ** 0.7
    slab = (0.48 + 0.30 * np.clip((u - 6) / 54, 0, 1)) * (slab > 0) * np.clip((u - 6) / 4, 0, 1)
    slab = np.where(u < 66, slab * np.clip((66 - u) / 6, 0, 1) ** 0.6, 0)
    slab_on = slab > prof
    prof = np.maximum(prof, slab)
    # serrate the ridges: fine jagged noise, more on the high parts
    jag = (pnoise(1, w, 1, seed + 1, SS * 3)[0] - 0.5) * 0.05 + (pnoise(1, w, 1, seed + 2, SS * 10)[0] - 0.5) * 0.08
    prof = np.where(prof > 0.05, prof + jag * np.clip(prof * 2, 0, 1), prof)
    top = ground - prof * land_h
    mtn = (y >= top[None, :])
    alt = (ground - y) / land_h                     # 0 at the bottom, 1 at the tallest summit
    # ridge lines down from each summit: left of them lit, right in shade
    wig = (pnoise(h, 1, SS * 4, seed + 3, 1)[:, 0] - 0.5)[:, None]
    lit = np.zeros((h, w), dtype=bool)
    for i, (c, hl, hr, ph, sh) in enumerate(peaks):
        cx = c / 360 * w
        py = ground - ph * land_h
        bx = cx + (y - py) * (0.30 + 0.20 * np.sin(i * 1.7)) + wig * SS * 5
        lit |= (owner[None, :] == (0 if i == len(peaks) - 1 else i)) & (x < bx)
    # the slab's long back faces the sun, its broken face doesn't
    lit = np.where(slab_on[None, :], (u < 60)[None, :], lit)
    # snow: down to about half way, lower down the gullies, with rock ribs
    # showing through and thin on the steep faces
    slope = np.abs(np.gradient(prof)) * w / 360 * 40
    gul = pnoise(h, w, SS * 12, seed + 4, SS * 3.5)
    patch = pnoise(h, w, SS * 3, seed + 8, SS * 4)
    snow_alt = (0.42 + (patch - 0.5) * 0.14 - np.clip(gul - 0.6, 0, 1) * 0.6
                + np.clip(slope - 0.5, 0, 1)[None, :] * 0.08)
    snow = mtn & (alt > snow_alt) & (alt > 0.28) & ~((gul < 0.2) & (alt < 0.85))
    # a crest of bare rock along the snowy ridges, so the peaks stand off a
    # pale sky on 4 greys
    crest = snow & (y < top[None, :] + SS * 2)
    # a far range behind, pale in the haze, its tops white
    bp = (0.42 + 0.07 * np.sin(ur * 3 + 1.1) + 0.06 * np.sin(u1 * 5 + 2.2) + 0.05 * np.sin(ur * 8 + 0.3)
          + (pnoise(1, w, 1, seed + 9, SS * 6)[0] - 0.5) * 0.12)
    bp = bp - np.abs(np.sin(ur * 11 + 0.7)) * 0.07
    btop = ground - bp * land_h
    back = (y >= btop[None, :]) & ~mtn
    bsnow = back & (y < (btop + SS * (2 + 3 * pnoise(1, w, 1, seed + 10, SS * 3)[0]))[None, :])
    # wooded foothills in front, a darker ridge of conifers
    fh = (0.17 + 0.04 * np.sin(ur * 7 + 0.5) + 0.03 * np.sin(u1 * 9 + 2.4) + 0.03 * np.sin(ur * 19 + 1.3)
          + (pnoise(1, w, 1, seed + 5, SS * 2)[0] - 0.5) * 0.03)
    ftop = ground - fh * land_h
    foot = y >= ftop[None, :]
    # the spruce forest along the bottom: a row of spiky tops
    sp = np.zeros(w)
    sun = np.zeros(w, dtype=bool)
    for c in np.arange(0, w, SS * 3.2):
        cx = c + mrng.uniform(-SS * 0.6, SS * 0.6)
        th = SS * mrng.uniform(3.0, 6.5)
        v = th * np.clip(1 - np.abs(x[0] - cx) / (th * 0.28), 0, 1)
        sun = np.where(v > sp, x[0] < cx, sun)
        sp = np.maximum(sp, v)
    forest = y >= (ground - SS * 2.2 - sp)[None, :]
    # shades
    ms = np.clip(0.55 + (pnoise(h, w, SS * 3, seed + 6, SS * 5) - 0.5) * 0.3 + alt * 0.1, 0, 1)
    rock_s = np.where(lit, ms * 0.40 + 0.42, ms * 0.30 + 0.02)
    snow_s = np.where(lit, 0.96, 0.66 + (patch - 0.5) * 0.1)
    fs = np.clip(0.40 + (pnoise(h, w, SS * 4, seed + 7, SS * 6) - 0.5) * 0.4 - (y - ftop[None, :]) / h * 1.5, 0, 1)
    tr = np.where(sun[None, :] & (y < ground - SS * 2.0), 0.30, 0.08)
    idx = np.zeros((height, width), dtype=np.uint8)
    for mask, name, sh_, dith in ((back, 'haze', np.full((h, w), 0.35), 0.0),
                                  (bsnow, 'grey', np.full((h, w), 0.86), 0.0),
                                  (mtn, 'far', rock_s, 0.0),
                                  (snow, 'grey', snow_s, 0.0),
                                  (crest, 'far', np.where(lit, 0.45, 0.2), 0.0),
                                  (foot, 'pine', fs * 0.7 + 0.15, 0.0),
                                  (forest, 'pine', tr, 0.0)):
        cov = shrink(mask.astype(float), SS)
        shd = shrink(np.where(mask, sh_, 0), SS) / np.maximum(cov, 1e-6)
        on = cov >= 0.5
        kk = ramp_index(name, np.clip(shd, 0, 1), (mrng.random(idx.shape) - 0.5) * dith)
        idx[on] = kk[on]
    return idx


def cascade_art(res, prev):
    """Scenery 6, Cascade Springs: Bkd8 1060 and 1061."""
    big = cascade_backdrop(1936, 80, 61)
    for k, idx in enumerate((half(big), big)):
        res.append(('Bkd8', 1060 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1060 + k}.png', 2)


def expansion_backdrop(style, width=1936):
    """Full turn through the heath, dunes or coast."""
    height = 48
    y, x = np.ogrid[:height, :width]
    t = x * 2 * math.pi / width
    idx = np.zeros((height, width), dtype=np.uint8)
    if style == 7:
        hills = 24 + 5 * np.sin(3 * t) + 3 * np.sin(7 * t + 1)
        trees = 34 + 4 * np.sin(8 * t) - np.abs(np.sin(173 * t)) * 2.5
        idx[:] = np.where(y >= hills, ramp_index('far', 0.70 - (y - hills) / 150, 0), idx)
        idx[:] = np.where(y >= trees, ramp_index('far', 0.35 + 0.10 * np.sin(93 * t), 0), idx)
    elif style == 8:
        far = 26 + 3 * np.sin(6 * t + 1) + 3 * np.sin(11 * t)
        dune = 34 + 5 * np.sin(9 * t + 3 * np.sin(2 * t)) + 2 * np.sin(17 * t)
        sun = 0.12 * np.sin(9 * t + 3 * np.sin(2 * t) + 1)
        idx[:] = np.where(y >= far, ramp_index('wall', 0.64 - (y - far) / 90, 0), idx)
        idx[:] = np.where(y >= dune, ramp_index('sand', 0.78 + sun - (y - dune) / 75, 0), idx)
    else:
        coast = 35 - 9 * np.maximum(0, np.sin(2 * t + 1)) + 2 * np.sin(13 * t)
        cliffs = y >= coast + 3
        idx[:] = np.where(y >= 35, ramp_index('water', 0.7 - (y - 35) / 96, 0), idx)
        land = (np.sin(2 * t + 1) + 0.35 * np.sin(5 * t)) > -0.3
        shade = 0.68 - 0.10 * np.sin((y - coast) * 1.2) - (y - coast) / 100
        idx[:] = np.where(land & (y >= coast), np.where(cliffs, ramp_index('wall', shade, 0),
                                                        ramp_index('far', shade - 0.2, 0)), idx)
    return idx


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
    for model, base in (('male', 2000), ('female', 2100)):
        for p, frame in enumerate(POSES):
            for k, n in enumerate(GOLFER_SIZES):
                idx, fx, fy, bx, by = golfer(frame, n, model == 'female')
                res.append(('Spr8', base + 10 * p + k, spr8(idx, fx, fy, bx, by)))
                if prev:
                    prefix = 'golfer' if model == 'male' else 'golfer-female'
                    preview(idx, prev / f'{prefix}-{p}-{n}.png', 4)
    # The plain tree line is half a turn, repeated: it has no landmark.
    # 160 px is the view's 59.5 degrees (render.c), a little under 968 /
    # 360 a degree, so the strip moves a touch faster than the ground.
    big = backdrop(968, 40, 7)
    for k, idx in enumerate((half(big), big)):
        res.append(('Bkd8', 1000 + k, bkd8(idx)))
        if prev:
            preview(idx, prev / f'backdrop-{1000 + k}.png', 2)
    links_art(res, prev)
    carrig_art(res, prev)
    sakura_art(res, prev)
    banksia_art(res, prev)
    croc_art(res, prev)
    cascade_art(res, prev)
    for style in (7, 8, 9):
        big = expansion_backdrop(style)
        for k, idx in enumerate((half(big), big)):
            rid = 1000 + 10 * style + k
            res.append(('Bkd8', rid, bkd8(idx)))
            if prev:
                preview(idx, prev / f'backdrop-{rid}.png', 2)
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
