#!/usr/bin/env python3
"""Object sprites per course style, cut to the master palette. Imported by
tools/mkobj.py, which packs them with the rest.

Each course's scenery number (course header) picks a set. A set fills some
object type slots (src/core/hole.h) with its own art, in up to two variants
(the object's variant byte, 0 or 1); slots it leaves out use the shared
art (Spr8 1000 + type * 10 + size). Set sprites are
  Spr8 3000 + 200 * scenery + 10 * type + 2 * variant + size
size 0 small (160 wide screens and far off), 1 big (twice the small).

  1 links (Auld Kirk Links): the kirk, sheep, a sea-worn rock, broom, a whitewashed cottage
  2 Irish clifftop (Carrig Head): wind-bent hawthorn, gorse, cliff rock
    and a layered rock face for the foot of the cliffs, thatched cottage, pale stone wall, marram, the lighthouse, sheep
  3 Japanese seaside (Sakura Bay): cherry and maple, black pine, clipped
    azalea, garden rock, tea house, bamboo fence, pagoda, crane
  4 Australian sandbelt (Banksia Heath): gum trees, she-oak, banksia shrub,
    sandstone, tin-roofed shed, tussock, windmill, kangaroo
  5 South African bush (Crocodile Bend): acacia and marula, ilala palm,
    thorn bush, granite, rondavel, dry grass, baobab, crocodile and hippo
  6 Canadian Rockies (Cascade Springs): aspen, spruce and fir, juniper,
    granite, log cabin, the hotel, elk

Everything is drawn supersampled as lit shapes: masks with a height field
for the light (sun from the left, up and behind the camera, as the terrain),
a little texture and grain, no outline.
"""
import math

import numpy as np

import mkobj
from mkobj import SS, vnoise, lambert, finish, OBJ_TREE, OBJ_PINE, OBJ_BUSH, OBJ_ROCK

OBJ_PALM, OBJ_HOUSE, OBJ_STAND, OBJ_WALL, OBJ_GORSE, OBJ_GRASS = 3, 6, 7, 8, 9, 10
OBJ_LANDMARK, OBJ_ANIMAL = 11, 12

srng = np.random.default_rng(2386)


def grid(h, w):
    y, x = np.mgrid[0:h, 0:w] + 0.5
    return y, x


def blur(a, r):
    """Three box blurs of radius r: close to a Gaussian."""
    r = max(1, int(r))
    out = a.astype(float)
    for _ in range(3):
        for ax in (0, 1):
            p = np.pad(out, [(r + 1, r) if k == ax else (0, 0) for k in (0, 1)], mode='edge')
            c = np.cumsum(p, axis=ax)
            if ax == 0:
                out = (c[2 * r + 1:] - c[:-2 * r - 1]) / (2 * r + 1)
            else:
                out = (c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)
    return out


def dome(mask, r, k=1.0):
    """Light on a shape as if it bulged out: a height field from the blurred
    mask, its normals, lit by the sun. 0 to 1."""
    hgt = blur(mask, r) * r * 2 * k
    gy, gx = np.gradient(hgt)
    n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return lambert(n)


def blobs(h, w, items):
    """Front-most hit of a set of ellipsoids (cx, cy, cz, rx, ry): normal, hit mask."""
    y, x = grid(h, w)
    zb = np.full((h, w), -1e9)
    n = np.zeros((h, w, 3))
    for cx, cy, cz, rx, ry in items:
        dx, dy = (x - cx) / rx, (y - cy) / ry
        d2 = dx * dx + dy * dy
        m = d2 < 1
        nz = np.sqrt(np.maximum(1 - d2, 0))
        z = cz + nz * min(rx, ry)
        m &= z > zb
        zb[m] = z[m]
        v = np.stack([dx[m], dy[m], nz[m]], -1)
        n[m] = v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-6)
    return n, zb > -1e8


def seg(x, y, x0, y0, x1, y1, r0, r1=None):
    """Mask of a thick line from (x0, y0) to (x1, y1), r0 to r1 wide, and
    where across it each pixel is (-1 to 1)."""
    r1 = r0 if r1 is None else r1
    vx, vy = x1 - x0, y1 - y0
    L2 = vx * vx + vy * vy + 1e-9
    t = np.clip(((x - x0) * vx + (y - y0) * vy) / L2, 0, 1)
    px, py = x0 + t * vx, y0 + t * vy
    d = np.hypot(x - px, y - py)
    r = r0 + (r1 - r0) * t
    side = ((x - px) * vy - (y - py) * vx) / math.sqrt(L2)
    return d < r, np.clip(side / np.maximum(r, 1e-6), -1, 1)


def limb(x, y, pts, r0, r1):
    """A bent limb through pts, tapering from r0 to r1: mask and across."""
    m = np.zeros(x.shape, dtype=bool)
    a = np.zeros(x.shape)
    n = len(pts) - 1
    for i in range(n):
        ra = r0 + (r1 - r0) * i / n
        rb = r0 + (r1 - r0) * (i + 1) / n
        mi, ai = seg(x, y, *pts[i], *pts[i + 1], ra, rb)
        a = np.where(mi & ~m, ai, a)
        m |= mi
    return m, a


def cyl(across, base=0.62, k=0.42):
    """Light across a round trunk or post: lit on the left."""
    return base - across * k


def tex(shape, cell, seed, amt):
    return (vnoise(shape, max(2, int(cell)), seed) - 0.5) * amt


# ------------------------------------------------------------------ trees
def crown_tree(H, seed, ratio=0.85, ctop=0.03, cbot=0.78, ramp='tree', nb=60, flat=1.0,
               trunk='bark', tw=0.035, sparse=0.0, bloom=0.0, lean=0.0, branches=0,
               bright=0.0, pale_trunk=False):
    """A broadleaf tree: a crown of leaf clumps on a trunk. flat < 1 squashes
    the clumps (acacia); sparse lets the sky through; bloom covers the lit
    clumps in blossom; branches draws that many limbs up into the crown."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    cx = w / 2 + lean * w * 0.25
    ct, cb = h * ctop, h * cbot
    cy, ry, rx = (ct + cb) / 2, (cb - ct) / 2, w * 0.45
    items = []
    for _ in range(nb):
        a, rr = r.random() * 2 * math.pi, math.sqrt(r.random()) * 0.85
        bx, by = cx + math.cos(a) * rr * (rx - w * 0.08), cy + math.sin(a) * rr * (ry * 0.85)
        rad = (0.15 + r.random() * 0.12) * rx
        bz = math.sqrt(max(0, 1 - rr * rr)) * rx * 0.5
        items.append((bx, by, bz, rad, rad * flat))
    n, hit = blobs(h, w, items)
    leaf = tex((h, w), SS * H // 40, seed + 1, 1.0)
    fine = r.random((h, w)) - 0.5
    shade = lambert(n) * 0.85 + 0.12 + leaf * 0.35 + fine * 0.15 + bright
    shade -= np.clip((y - cy) / ry, 0, 1) * 0.25
    if sparse:
        holes = vnoise((h, w), max(2, SS * H // 22), seed + 5)
        hit &= (holes > sparse) | (n[..., 2] > 0.85)
    layers = [(hit, ramp, np.clip(shade, 0, 1))]
    if bloom:
        bl = hit & ((leaf + lambert(n) * 0.6 + fine * 0.3) > 0.55 - bloom)
        layers.append((bl, 'blossom', np.clip(lambert(n) * 0.75 + 0.25 + fine * 0.25 + leaf * 0.3, 0, 1)))
    # trunk from the ground to the crown, bent with the lean
    base_x = w / 2
    pts = [(base_x, h), (base_x + (cx - base_x) * 0.4, h - (h - cb) * 0.5), (cx, cy)]
    tm, ta = limb(x, y, pts, w * tw * 1.4, w * tw * 0.8)
    for k in range(branches):
        ang = -math.pi / 2 + (k - (branches - 1) / 2) * 0.55 + r.uniform(-0.15, 0.15)
        ex, ey = cx + math.cos(ang) * rx * 0.75, cb - ry * 0.25 + math.sin(ang) * ry * 0.2
        bm, ba = limb(x, y, [(cx, cb + (h - cb) * 0.25), (ex, ey)], w * tw * 0.8, w * tw * 0.4)
        ta = np.where(bm & ~tm, ba, ta)
        tm |= bm
    tm &= ~(hit & (y < cb - ry * 0.15))
    bark = np.clip(cyl(ta, 0.75 if pale_trunk else 0.6) + fine * 0.3, 0, 1)
    layers.insert(0, (tm, trunk, bark))
    return finish(H, layers, (w / 2 / SS, h / SS), srng)


def conifer(H, seed, ratio=0.5, tiers=7, ramp='pine', droop=0.45, rag=0.12, top=0.02, bot=0.9, bright=0.0):
    """A spruce or fir: drooping tiers of needles getting wider down the tree."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    cx = w / 2
    t0, b0 = h * top, h * bot
    t = np.clip((y - t0) / (b0 - t0), 0, 1)
    ph = (t * tiers) % 1.0
    half = w * 0.48 * (0.1 + 0.9 * t) * (1 - droop + droop * ph)
    ragn = (vnoise((h, w), SS * 2, seed) - 0.5) * w * rag
    dx = (x - cx) / np.maximum(half, 1)
    hit = (np.abs(x - cx) < half + ragn) & (y > t0) & (y < b0)
    nz = np.sqrt(np.clip(1 - dx * dx, 0, 1))
    n = np.stack([dx * 0.9, -0.4 + ph * 0.8, nz], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    fine = r.random((h, w)) - 0.5
    needle = tex((h, w), SS * H // 48, seed + 2, 1.0)
    shade = lambert(n) * 0.8 + 0.12 + needle * 0.35 + fine * 0.18 - (1 - ph) * 0.18 + bright
    trunk = (np.abs(x - cx) < w * 0.04) & (y >= b0 - h * 0.04) & ~hit
    bark = np.clip(0.6 - (x - cx) / (w * 0.04) * 0.4 + fine * 0.3, 0, 1)
    return finish(H, [(hit, ramp, np.clip(shade, 0, 1)), (trunk, 'bark', bark)], (cx / SS, h / SS), srng)


def pad_pine(H, seed, ratio=0.95):
    """A Japanese black pine: a crooked trunk with flat clouds of needles."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    pts = [(w * 0.45, h), (w * 0.38, h * 0.7), (w * 0.55, h * 0.45), (w * 0.48, h * 0.18)]
    tm, ta = limb(x, y, pts, w * 0.045, w * 0.02)
    pads = [(0.48, 0.14, 0.26), (0.30, 0.34, 0.24), (0.70, 0.40, 0.25), (0.25, 0.58, 0.2), (0.66, 0.66, 0.16)]
    items = []
    for k, (px, py, pw) in enumerate(pads):
        bm, ba = limb(x, y, [(pts[min(k, 3)][0], h * (py + 0.08)), (w * px, h * py)], w * 0.022, w * 0.012)
        ta = np.where(bm & ~tm, ba, ta)
        tm |= bm
        for _ in range(9):
            items.append((w * px + r.uniform(-1, 1) * w * pw * 0.7, h * py + r.uniform(-0.4, 0.3) * h * 0.05,
                          r.random() * w * 0.05, w * pw * r.uniform(0.3, 0.5), h * r.uniform(0.035, 0.06)))
    n, hit = blobs(h, w, items)
    fine = r.random((h, w)) - 0.5
    needle = tex((h, w), SS * H // 40, seed + 3, 1.0)
    shade = lambert(n) * 0.8 + 0.12 + needle * 0.4 + fine * 0.2
    tm &= ~hit
    bark = np.clip(cyl(ta, 0.5) + fine * 0.3, 0, 1)
    return finish(H, [(tm, 'bark', bark), (hit, 'pine', np.clip(shade, 0, 1))], (w * 0.45 / SS, h / SS), srng)


def palm(H, seed, ratio=0.9, fronds=11):
    """A palm: a bending trunk and a head of drooping fronds."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    top = (w * 0.56, h * 0.2)
    tm, ta = limb(x, y, [(w * 0.46, h), (w * 0.5, h * 0.6), top], w * 0.035, w * 0.025)
    rings = ((y // (SS * 1.2)) % 2) * 0.12
    fm = np.zeros((h, w), dtype=bool)
    fs = np.zeros((h, w))
    for k in range(fronds):
        a = -math.pi + (k + 0.5) / fronds * math.pi * 2 + r.uniform(-0.2, 0.2)
        L = w * r.uniform(0.38, 0.5)
        ex = top[0] + math.cos(a) * L
        rise = math.sin(a) * L * 0.45
        pts = [top, (top[0] + math.cos(a) * L * 0.5, top[1] + rise * 0.5 - h * 0.05),
               (ex, top[1] + rise + h * 0.12 + abs(math.cos(a)) * h * 0.08)]
        m, ac = limb(x, y, pts, h * 0.035, h * 0.006)
        leaflets = (np.sin((x + y) * 1.3 / SS) > -0.2)
        m &= leaflets | (np.abs(ac) < 0.3)
        fs = np.where(m & ~fm, 0.55 - ac * 0.3 + (0.15 if math.sin(a) < 0 else -0.1), fs)
        fm |= m
    fine = r.random((h, w)) - 0.5
    tm &= ~fm
    return finish(H, [(tm, 'bark', np.clip(cyl(ta, 0.62) - rings + fine * 0.3, 0, 1)),
                      (fm, 'tree', np.clip(fs + fine * 0.25, 0, 1))], (w * 0.46 / SS, h / SS), srng)


def baobab(H, seed, ratio=1.0):
    """A baobab: a fat bottle trunk with short bare limbs and a thin crown."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    cx = w / 2
    t = np.clip((h - y) / (h * 0.62), 0, 1)
    half = w * (0.2 - 0.07 * t * t)
    trunk = (np.abs(x - cx) < half) & (t < 1)
    across = np.clip((x - cx) / np.maximum(half, 1), -1, 1)
    tm, ta = trunk, across
    for k in range(7):
        a = -math.pi / 2 + (k - 3) * 0.32 + r.uniform(-0.1, 0.1)
        sx, sy = cx + (k - 3) * w * 0.03, h * 0.4
        L = h * r.uniform(0.18, 0.26)
        ex, ey = sx + math.cos(a) * L, sy + math.sin(a) * L
        bm, ba = limb(x, y, [(sx, sy), ((sx + ex) / 2 + r.uniform(-1, 1) * w * 0.03, (sy + ey) / 2), (ex, ey)],
                      w * 0.035, w * 0.012)
        ta = np.where(bm & ~tm, ba, ta)
        tm |= bm
    items = [(cx + r.uniform(-0.38, 0.38) * w, h * r.uniform(0.04, 0.2), r.random() * w * 0.04,
              w * r.uniform(0.05, 0.09), h * r.uniform(0.03, 0.05)) for _ in range(24)]
    n, hit = blobs(h, w, items)
    fine = r.random((h, w)) - 0.5
    bark = cyl(ta, 0.62, 0.45) + tex((h, w), SS * 2, seed, 0.3) + fine * 0.2 - ((x // SS) % 3 == 0) * 0.06
    shade = lambert(n) * 0.8 + 0.15 + tex((h, w), SS * 2, seed + 1, 0.4) + fine * 0.2
    return finish(H, [(tm, 'wall', np.clip(bark, 0, 1)), (hit & ~(trunk), 'tree', np.clip(shade, 0, 1))],
                  (cx / SS, h / SS), srng)


# ------------------------------------------------------------------ bushes, rocks, grass
def shrub(H, seed, ratio=1.4, ramp='tree', n=18, flowers=None, fl_amt=0.25, flat=1.0, bright=0.0, spiky=False):
    """A shrub of round clumps; flowers (a ramp) dot its lit side."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    items = []
    for _ in range(n):
        rad = h * (0.22 + r.random() * 0.18)
        bx = w * 0.5 + r.uniform(-1, 1) * (w * 0.5 - rad)
        by = h - rad * flat * (0.75 + r.random() * 0.6)
        items.append((bx, max(rad * flat, by), r.random() * rad * 0.5, rad, rad * flat))
    n_, hit = blobs(h, w, items)
    fine = r.random((h, w)) - 0.5
    leaf = tex((h, w), SS * H // 14, seed + 1, 1.0)
    lit = lambert(n_)
    shade = lit * 0.7 + 0.1 + leaf * 0.3 + fine * (0.3 if spiky else 0.15) + bright
    layers = [(hit, ramp, np.clip(shade, 0, 1))]
    if flowers:
        fl = hit & (r.random((h, w)) < 0.02 + fl_amt * np.clip(lit - 0.4, 0, 1)) & (leaf > -0.1)
        fl = mkobj.shrink(fl.astype(float), SS).repeat(SS, 0).repeat(SS, 1) > 0.1
        fl &= hit
        layers.append((fl, flowers, np.clip(0.5 + lit * 0.45 + fine * 0.2, 0, 1)))
    return finish(H, layers, (w / 2 / SS, h / SS), srng)


def boulder(H, seed, ratio=1.5, ramp='rock', facets=3.0, bright=0.0, layered=False):
    """A rock: a lumpy dome with flat faces and cracks."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    cx, rx, ry = w / 2, w * 0.47, h * 0.97
    lump = 1 + (vnoise((h, w), SS * 3, seed) - 0.5) * 0.35
    dx, dy = (x - cx) / rx, (y - h) / ry
    d2 = (dx * dx + dy * dy) / lump
    hit = d2 < 1
    # faceted: quantise the normal's direction a little
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    ang = np.round(np.arctan2(dy, dx) * facets / math.pi) * math.pi / facets
    rr = np.sqrt(np.clip(d2, 0, 1))
    n = np.stack([np.cos(ang) * rr, np.sin(ang) * rr, nz], -1)
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
    fine = r.random((h, w)) - 0.5
    shade = lambert(n) * 0.8 + 0.15 + fine * 0.2 + tex((h, w), SS, seed + 3, 0.25) + bright
    if layered:
        shade -= ((y // (SS * 2.5)) % 2) * 0.12
    crack = np.abs(vnoise((h, w), SS * 4, seed + 7) - 0.5) < 0.03
    shade = np.where(crack, shade - 0.35, shade)
    return finish(H, [(hit, ramp, np.clip(shade, 0, 1))], (cx / SS, h / SS), srng)


def cliff_rock(H, seed, ratio=1.7):
    """A slab of layered dark rock for the foot of a sea cliff: a broad
    block with a ragged top, steep sides and ledges, wet and dark at the
    bottom."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    u = x / w
    top = h * (0.12 + 0.18 * (vnoise((1, w), SS * 3, seed)[0] - 0.5) + 0.25 * np.abs(u[0] - 0.45) ** 1.5)
    side = np.clip(np.minimum(u, 1 - u) / 0.12, 0, 1)
    hit = (y >= top[None, :] + (1 - side) * h * 0.5) & (y < h)
    fine = r.random((h, w)) - 0.5
    ledge = ((y + vnoise((h, w), SS * 4, seed + 2) * SS * 3) // (SS * 2.2)) % 3 == 0
    lit = 0.62 - (u - 0.3) * 0.35
    shade = lit + fine * 0.18 + tex((h, w), SS, seed + 3, 0.3) - ledge * 0.22 - np.clip((y / h - 0.75) * 1.6, 0, 1) * 0.35
    crack = np.abs(vnoise((h, w), SS * 3, seed + 7) - 0.5) < 0.025
    shade = np.where(crack, shade - 0.3, shade)
    # its own grain numbers, so the sets after it come out as before
    return finish(H, [(hit, 'rock', np.clip(shade, 0, 1))], (w / 2 / SS, h / SS), np.random.default_rng(seed + 11))


def tuft(H, seed, ratio=1.4, ramp='oob', blades=26, spread=0.9, bright=0.0, heads=False):
    """A tuft of grass: thin blades fanning out from the ground."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    hit = np.zeros((h, w), dtype=bool)
    shade = np.zeros((h, w))
    for k in range(blades):
        lean = r.uniform(-spread, spread)
        tall = h * r.uniform(0.45, 1.0)
        bx = w / 2 + r.uniform(-0.2, 0.2) * w
        t = np.clip((h - y) / tall, 0, 1)
        cxl = bx + lean * (h - y) * 0.6 + lean * t * t * tall * 0.4
        thick = SS * (1.1 - t * 0.7)
        blade = (np.abs(x - cxl) < thick) & (y > h - tall)
        if heads:
            blade |= (np.abs(x - cxl) < SS * 1.3) & (y > h - tall) & (y < h - tall * 0.85)
        hit |= blade
        shade = np.where(blade, 0.45 + 0.45 * t + (0.15 if lean < 0 else -0.1) + r.uniform(-0.1, 0.1) + bright, shade)
    return finish(H, [(hit, ramp, np.clip(shade, 0, 1))], (w / 2 / SS, h / SS), srng)


# ------------------------------------------------------------------ buildings
def front(h, w, x0, x1, y0, y1):
    y, x = grid(h, w)
    return (x >= x0) & (x < x1) & (y >= y0) & (y < y1)


def windows(x, y, x0, x1, y0, y1, cols, rows, ww=0.35, wh=0.45, door=None):
    """Dark window panes in a grid over a wall, and a door in column door."""
    m = np.zeros(x.shape, dtype=bool)
    cw, rh = (x1 - x0) / cols, (y1 - y0) / rows
    for i in range(cols):
        cx = x0 + cw * (i + 0.5)
        for j in range(rows):
            cy = y0 + rh * (j + 0.5)
            if door is not None and i == door and j == rows - 1:
                m |= (np.abs(x - cx) < cw * ww * 0.6) & (y > cy - rh * 0.35) & (y < y1)
            else:
                m |= (np.abs(x - cx) < cw * ww / 2) & (np.abs(y - cy) < rh * wh / 2)
    return m


def cottage(H, seed, ratio=1.6, wall='grey', wall_lt=0.88, roof='thatch', roof_ramp='wood'):
    """A low cottage seen from its long side: whitewashed or stone walls,
    a thatched or slate roof, small windows and a door."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    eave = h * 0.5
    x0, x1 = w * 0.07, w * 0.93
    walls = front(h, w, x0, x1, eave, h)
    ws = wall_lt + tex((h, w), SS * 2, seed, 0.15) + fine * 0.1 - (y - eave) / h * 0.25
    ws -= np.clip(1 - (y - eave) / (SS * 2), 0, 1) * 0.35        # the eaves' shadow
    win = windows(x, y, x0, x1, eave + h * 0.06, h, 5, 1, 0.28, 0.5, door=2)
    ws = np.where(win, 0.05, ws)
    if roof == 'thatch':
        # rounded thatch hanging over the eaves
        rise = np.clip(np.minimum(x - (x0 - SS * 2), (x1 + SS * 2) - x) / (w * 0.12), 0, 1)
        top = eave - (eave - h * 0.1) * np.sqrt(rise)
        rf = (y >= top) & (y < eave + SS * 1.5) & (x > x0 - SS * 2) & (x < x1 + SS * 2)
        straw = np.sin(x / SS * 2.1 + vnoise((h, w), SS * 2, seed + 4) * 4) * 0.12
        rs = 0.5 + straw + fine * 0.2 - (y - top) / h * 0.4 + tex((h, w), SS * 3, seed + 5, 0.3)
    else:
        rf = (y >= h * 0.14) & (y < eave) & (x >= x0 - SS) & (x < x1 + SS)
        rs = 0.35 + (y - h * 0.14) / eave * 0.15 + tex((h, w), SS, seed + 1, 0.2) + fine * 0.15
        rs = np.where(((y - h * 0.14) % (SS * 1.5)) < SS * 0.4, rs - 0.15, rs)
    chim = (np.abs(x - x1 + w * 0.08) < w * 0.035) & (y > h * 0.05) & (y < h * 0.3)
    return finish(H, [(walls, wall, np.clip(ws, 0, 1)), (rf, roof_ramp, np.clip(rs, 0, 1)),
                      (chim & ~rf | chim & (y < h * 0.2), wall, np.clip(0.6 + fine * 0.2, 0, 1))],
                  (w / 2 / SS, h / SS), srng)


def shed(H, seed, ratio=1.8):
    """A weatherboard shed with a corrugated iron roof and a veranda."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    eave = h * 0.42
    x0, x1 = w * 0.05, w * 0.95
    walls = front(h, w, x0, x1, eave, h)
    boards = ((y // (SS * 1.3)) % 2) * 0.08
    ws = 0.72 - boards + fine * 0.1 - (y - eave) / h * 0.2
    ws -= np.clip(1 - (y - eave) / (SS * 6), 0, 1) * 0.45          # under the veranda
    win = windows(x, y, x0, x1, eave + h * 0.1, h, 6, 1, 0.3, 0.45, door=3)
    ws = np.where(win, 0.06, ws)
    rf = (y >= h * 0.12) & (y < eave) & (x > x0 - SS * 2) & (x < x1 + SS * 2)
    rs = 0.6 + np.sin(x / SS * 2.4) * 0.12 + (y - h * 0.12) / h * 0.2 + fine * 0.15
    posts = np.zeros((h, w), dtype=bool)
    for k in range(7):
        posts |= (np.abs(x - (x0 + (x1 - x0) * k / 6)) < SS * 0.7) & (y >= eave)
    return finish(H, [(walls, 'sand', np.clip(ws, 0, 1)), (rf, 'grey', np.clip(rs, 0, 1)),
                      (posts, 'wood', np.clip(0.6 + fine * 0.2, 0, 1))], (w / 2 / SS, h / SS), srng)


def rondavel(H, seed, ratio=1.0):
    """A round hut: an ochre wall under a conical thatch."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    cx, eave = w / 2, h * 0.55
    rx = w * 0.4
    across = np.clip((x - cx) / rx, -1, 1)
    walls = (np.abs(x - cx) < rx) & (y >= eave)
    ws = 0.6 - across * 0.35 + tex((h, w), SS * 2, seed, 0.15) + fine * 0.1
    ws -= np.clip(1 - (y - eave) / (SS * 3), 0, 1) * 0.4
    door = (np.abs(x - cx - rx * 0.15) < w * 0.07) & (y > h * 0.7)
    ws = np.where(door, 0.05, ws)
    t = np.clip((y - h * 0.04) / (eave + SS * 2 - h * 0.04), 0, 1)
    cone = (np.abs(x - cx) < (rx + SS * 3) * t) & (y < eave + SS * 2) & (t > 0)
    ca = np.clip((x - cx) / np.maximum((rx + SS * 3) * t, 1), -1, 1)
    straw = np.sin(np.arctan2(x - cx, y - h * 0.04) * 60) * 0.1
    cs = 0.6 - ca * 0.35 + straw + fine * 0.2
    return finish(H, [(walls, 'wood', np.clip(ws, 0, 1)), (cone, 'sand', np.clip(cs - 0.15, 0, 1))],
                  (cx / SS, h / SS), srng)


def tea_house(H, seed, ratio=1.7):
    """A tea house: dark timber and paper walls under a deep tiled roof with
    eaves turned up at the ends."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    eave = h * 0.52
    x0, x1 = w * 0.12, w * 0.88
    walls = front(h, w, x0, x1, eave, h * 0.94)
    panels = ((((x - x0) // (w * 0.0952)) % 2) == 0)
    ws = np.where(panels, 0.85, 0.75) + fine * 0.08
    frame = (((x - x0) % (w * 0.0952)) < SS * 0.8) | (np.abs(y - h * 0.72) < SS * 0.5)
    ws -= np.clip(1 - (y - eave) / (SS * 4), 0, 1) * 0.5
    base = front(h, w, x0 - SS * 2, x1 + SS * 2, h * 0.94, h)
    u = (x - w / 2) / (w * 0.5)
    lip = eave - (u ** 4) * h * 0.12
    top = h * 0.12 + np.abs(u) * h * 0.06
    rf = (y >= top) & (y < lip + SS * 1.2) & (np.abs(u) < 1)
    rs = 0.3 + (y - top) / h * 0.4 + np.where((x // (SS * 1.4)) % 2 == 0, 0.08, -0.04) + fine * 0.12
    return finish(H, [(walls, 'cap', np.clip(ws, 0, 1)), (walls & frame, 'bark', np.clip(0.3 + fine * 0.2, 0, 1)),
                      (base, 'rock', np.clip(0.5 + fine * 0.2, 0, 1)), (rf, 'navy', np.clip(rs, 0, 1))],
                  (w / 2 / SS, h / SS), srng)


def log_cabin(H, seed, ratio=1.6):
    """A log cabin: round logs, a stone chimney and a steep shingle roof."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    eave = h * 0.5
    x0, x1 = w * 0.08, w * 0.92
    walls = front(h, w, x0, x1, eave, h)
    lg = SS * 1.6
    ly = ((y - eave) % lg) / lg * 2 - 1
    ws = 0.55 - ly * 0.3 + fine * 0.15 + tex((h, w), SS * 3, seed, 0.15)
    ws -= np.clip(1 - (y - eave) / (SS * 3), 0, 1) * 0.35
    win = windows(x, y, x0, x1, eave + h * 0.06, h, 4, 1, 0.3, 0.5, door=1)
    ws = np.where(win, 0.05, ws)
    rf = (y >= h * 0.06) & (y < eave) & (x > x0 - SS * 2) & (x < x1 + SS * 2)
    rs = 0.3 + (y - h * 0.06) / h * 0.3 + np.where(((y // SS) % 2 == 0) & (fine > 0.2), 0.1, 0) + fine * 0.12
    chim = (np.abs(x - x0 - w * 0.06) < w * 0.05) & (y > h * 0.0) & (y < h)
    cs = 0.55 - (x - x0 - w * 0.06) / (w * 0.05) * 0.2 + tex((h, w), SS, seed + 2, 0.4) + fine * 0.2
    return finish(H, [(walls, 'wood', np.clip(ws, 0, 1)), (rf, 'pine', np.clip(rs, 0, 1)),
                      (chim, 'rock', np.clip(cs, 0, 1))], (w / 2 / SS, h / SS), srng)


def kirk(H, seed, ratio=1.5):
    """An old stone kirk: a nave with a slate roof and a square tower with a short spire."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    stone = tex((h, w), SS * 2, seed, 0.3) + fine * 0.2
    tx0, tx1 = w * 0.06, w * 0.34
    tower = front(h, w, tx0, tx1, h * 0.28, h)
    ts = 0.62 - (x - tx0) / (tx1 - tx0) * 0.15 + stone
    ts = np.where(np.abs(y - h * 0.38) < h * 0.04, ts - 0.2, ts)       # string course
    ts = np.where((np.abs(x - (tx0 + tx1) / 2) < w * 0.03) & (np.abs(y - h * 0.5) < h * 0.06), 0.05, ts)
    batt = (y >= h * 0.25) & (y < h * 0.28) & (x >= tx0) & (x < tx1) & (((x - tx0) // (SS * 1.5)) % 2 == 0)
    st = np.clip((y - h * 0.02) / (h * 0.26), 0, 1)
    spire = (np.abs(x - (tx0 + tx1) / 2) < (tx1 - tx0) * 0.38 * st) & (y < h * 0.28) & (st > 0)
    ss = np.where(x < (tx0 + tx1) / 2, 0.45, 0.25) + fine * 0.15
    eave = h * 0.62
    nave = front(h, w, tx1, w * 0.96, eave, h)
    ns = 0.6 + stone - (y - eave) / h * 0.2
    ns -= np.clip(1 - (y - eave) / (SS * 2), 0, 1) * 0.3
    win = windows(x, y, tx1, w * 0.96, eave, h, 4, 1, 0.18, 0.6)
    ns = np.where(win, 0.05, ns)
    rf = (y >= h * 0.44) & (y < eave) & (x >= tx1) & (x < w * 0.97)
    rs = 0.3 + (y - h * 0.44) / h * 0.4 + fine * 0.15
    rs = np.where(((y - h * 0.44) % (SS * 1.5)) < SS * 0.4, rs - 0.12, rs)
    return finish(H, [(nave, 'wall', np.clip(ns, 0, 1)), (rf, 'grey', np.clip(rs, 0, 1)),
                      (tower | batt, 'wall', np.clip(ts, 0, 1)), (spire, 'grey', np.clip(ss, 0, 1))],
                  (w / 2 / SS, h / SS), srng)


def lighthouse(H, seed, ratio=0.42):
    """A lighthouse: a white tapering tower with a red band, a gallery and the lantern."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    cx = w / 2
    t = np.clip((y - h * 0.22) / (h * 0.74), 0, 1)
    half = w * (0.2 + 0.12 * t)
    body = (np.abs(x - cx) < half) & (y >= h * 0.22) & (y < h * 0.96)
    across = np.clip((x - cx) / np.maximum(half, 1), -1, 1)
    sh = cyl(across, 0.75, 0.35) + fine * 0.08
    band = body & ((np.abs(y - h * 0.5) < h * 0.06) | (np.abs(y - h * 0.76) < h * 0.06))
    win = body & (np.abs(x - cx + w * 0.03) < w * 0.03) & ((np.abs(y - h * 0.38) < h * 0.025) | (np.abs(y - h * 0.63) < h * 0.025))
    gal = (np.abs(x - cx) < w * 0.3) & (y >= h * 0.2) & (y < h * 0.23)
    lantern = (np.abs(x - cx) < w * 0.14) & (y >= h * 0.09) & (y < h * 0.2)
    glass = lantern & (((x - cx) // (SS * 1.2)) % 2 == 0) & (y > h * 0.11)
    dome_m = ((x - cx) / (w * 0.15)) ** 2 + ((y - h * 0.09) / (h * 0.06)) ** 2 < 1
    dome_m &= y < h * 0.09
    base = (np.abs(x - cx) < w * 0.42) & (y >= h * 0.93)
    return finish(H, [(body, 'grey', np.clip(sh + 0.1, 0, 1)), (band, 'red', np.clip(sh, 0, 1)),
                      (win, 'grey', np.full((h, w), 0.05)), (gal | base, 'grey', np.clip(0.35 + fine * 0.1, 0, 1)),
                      (lantern, 'grey', np.clip(0.3 + fine * 0.1, 0, 1)), (glass, 'gold', np.clip(0.8 + fine * 0.2, 0, 1)),
                      (dome_m, 'red', np.clip(cyl(np.clip((x - cx) / (w * 0.15), -1, 1), 0.5) + fine * 0.1, 0, 1))],
                  (cx / SS, h / SS), srng)


def pagoda(H, seed, ratio=0.55, tiers=5):
    """A pagoda: storeys getting smaller up the tower, each under a deep roof
    with turned-up corners, and a spire on top."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    cx = w / 2
    layers = []
    walls = np.zeros((h, w), dtype=bool)
    roofs = np.zeros((h, w), dtype=bool)
    rs = np.zeros((h, w))
    top0, bot = h * 0.18, h
    th = (bot - top0) / tiers
    for k in range(tiers):
        yb = bot - th * k
        yt = yb - th
        sc = 1 - k * 0.12
        walls |= (np.abs(x - cx) < w * 0.24 * sc) & (y >= yt + th * 0.35) & (y < yb)
        u = (x - cx) / (w * 0.5 * sc)
        lip = yt + th * 0.42 - (u ** 4) * th * 0.25
        rf = (np.abs(u) < 1) & (y >= yt + th * 0.12 + np.abs(u) * th * 0.12) & (y < lip)
        rs = np.where(rf & ~roofs, 0.3 + (y - yt) / th * 0.5 + fine * 0.12, rs)
        roofs |= rf
    ws = cyl(np.clip((x - cx) / (w * 0.24), -1, 1), 0.6, 0.3) + fine * 0.1
    ws -= ((y % th) < th * 0.5) * 0.25
    spire = (np.abs(x - cx) < SS * 0.9) & (y >= h * 0.0) & (y < top0 + th * 0.2)
    rings = spire & ((y // SS) % 3 == 0)
    return finish(H, [(walls & ~roofs, 'red', np.clip(ws, 0, 1)), (roofs, 'navy', np.clip(rs, 0, 1)),
                      (spire, 'gold', np.clip(0.5 + fine * 0.2, 0, 1)), (rings, 'gold', np.full((h, w), 0.9))],
                  (cx / SS, h / SS), srng)


def windmill(H, seed, ratio=0.6):
    """A farm windmill: a steel lattice tower with a bladed wheel and a tail vane."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    cx, hub = w * 0.5, (w * 0.5, h * 0.2)
    tower = np.zeros((h, w), dtype=bool)
    for sx in (-1, 1):
        m, _ = seg(x, y, cx + sx * w * 0.3, h, cx + sx * w * 0.04, h * 0.22, SS * 0.7)
        tower |= m
    for k in range(6):
        y0, y1 = h - k * h * 0.13, h - (k + 1) * h * 0.13
        if y1 < h * 0.25:
            break
        hw0, hw1 = w * (0.3 - 0.26 * k * 0.13 / 0.78), w * (0.3 - 0.26 * (k + 1) * 0.13 / 0.78)
        for sx in (-1, 1):
            m, _ = seg(x, y, cx - sx * hw0, y0, cx + sx * hw1, y1, SS * 0.4)
            tower |= m
    wheel = np.zeros((h, w), dtype=bool)
    rr = w * 0.36
    for k in range(18):
        a = k / 18 * 2 * math.pi
        m, _ = seg(x, y, hub[0] + math.cos(a) * rr * 0.3, hub[1] + math.sin(a) * rr * 0.3,
                   hub[0] + math.cos(a) * rr, hub[1] + math.sin(a) * rr, SS * 1.3, SS * 1.8)
        wheel |= m
    rim = np.abs(np.hypot(x - hub[0], y - hub[1]) - rr) < SS * 0.6
    tail, _ = seg(x, y, hub[0], hub[1], w * 0.98, h * 0.16, SS * 0.6)
    vane = (x > w * 0.82) & (x < w) & (np.abs(y - h * 0.16) < h * 0.05)
    ws = 0.65 - (x - hub[0]) / w * 0.6 + fine * 0.2
    return finish(H, [(tower | tail, 'grey', np.clip(0.4 + fine * 0.15, 0, 1)),
                      (wheel | rim | vane, 'grey', np.clip(ws, 0, 1))], (cx / SS, h / SS), srng)


def hotel(H, seed, ratio=1.25):
    """A grand mountain hotel: stone storeys with rows of windows, steep
    green copper roofs with dormers and a corner tower."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    stone = tex((h, w), SS * 2, seed, 0.25) + fine * 0.15
    layers = []
    # three blocks: left wing, the main block, the tower
    blocks = [(0.02, 0.36, 0.5), (0.3, 0.8, 0.32), (0.74, 0.98, 0.18)]
    for x0f, x1f, topf in blocks:
        x0, x1, eave = w * x0f, w * x1f, h * (topf + 0.14)
        wall = front(h, w, x0, x1, eave, h)
        ws = 0.62 + stone - (x - x0) / (x1 - x0) * 0.1
        rows = max(2, int((h - eave) / (h * 0.09)))
        win = windows(x, y, x0, x1, eave + h * 0.02, h, max(2, int((x1 - x0) / (w * 0.06))), rows, 0.4, 0.45)
        ws = np.where(win, 0.08, ws)
        u = np.abs((x - (x0 + x1) / 2) / ((x1 - x0) / 2))
        roof = (y >= h * topf + u * h * 0.04) & (y < eave) & (x >= x0 - SS) & (x < x1 + SS)
        rs = 0.45 + (y - h * topf) / h * 0.6 + np.where((x // (SS * 1.2)) % 2 == 0, 0.06, -0.04) + fine * 0.12
        dorm = roof & windows(x, y, x0, x1, h * topf + h * 0.04, eave, max(2, int((x1 - x0) / (w * 0.12))), 1, 0.3, 0.4)
        layers += [(wall, 'wall', np.clip(ws, 0, 1)), (roof, 'far', np.clip(rs, 0, 1)),
                   (dorm, 'wall', np.clip(0.4 + fine * 0.1, 0, 1))]
    # the tower's pointed cap
    tx = w * 0.86
    t = np.clip((y - h * 0.0) / (h * 0.18), 0, 1)
    cap = (np.abs(x - tx) < w * 0.12 * t) & (y < h * 0.18) & (t > 0)
    layers.append((cap, 'far', np.clip(np.where(x < tx, 0.7, 0.45) + fine * 0.1, 0, 1)))
    return finish(H, layers, (w / 2 / SS, h / SS), srng)


def fence(H, seed, ratio=4.0):
    """A low bamboo fence: canes on two rails, tied in."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    cw = SS * 2.2
    k = x // cw
    tops = h * (0.05 + r.random(int(w // cw) + 2) * 0.06)
    cane = ((x % cw) > SS * 0.35) & (y >= tops[k.astype(int)])
    ac = ((x % cw) - cw / 2) / (cw / 2)
    cs = 0.65 - ac * 0.3 + fine * 0.15 - ((y // (SS * 6)) % 2) * 0.04
    rails = (np.abs(y - h * 0.3) < SS * 0.8) | (np.abs(y - h * 0.7) < SS * 0.8)
    return finish(H, [(cane, 'sand', np.clip(cs, 0, 1)), (rails, 'bark', np.clip(0.45 + fine * 0.2, 0, 1))],
                  (w / 2 / SS, h / SS), srng)


def grandstand(H, seed, ratio=2.4):
    """A small grandstand: tiers of seats full of people under a roof."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    frame = front(h, w, w * 0.02, w * 0.98, h * 0.12, h)
    fs = 0.35 + fine * 0.1 + ((y - h * 0.12) // (h * 0.11) % 2) * 0.06
    crowd = frame & (y < h * 0.82) & (y > h * 0.2) & ((y - h * 0.2) % (h * 0.11) < h * 0.07) & (r.random((h, w)) < 0.6)
    crowd = mkobj.shrink(crowd.astype(float), SS).repeat(SS, 0).repeat(SS, 1) > 0.4
    who = (vnoise((h, w), SS, seed + 3) * 6).astype(int)
    roof = front(h, w, 0, w, h * 0.05, h * 0.12)
    layers = [(frame, 'grey', np.clip(fs, 0, 1)), (roof, 'grey', np.clip(0.75 + fine * 0.1, 0, 1))]
    for k, ramp in enumerate(('shirt', 'navy', 'cap', 'gold', 'red', 'skin')):
        layers.append((crowd & frame & (who == k), ramp, np.clip(0.6 + fine * 0.3, 0, 1)))
    return finish(H, layers, (w / 2 / SS, h / SS), srng)


# ------------------------------------------------------------------ animals, side on, facing left
def animal(H, ratio, seed, parts, legs, ramp, bright=0.0, extra=()):
    """Ellipsoid parts (fractions of the box) lit as bodies, thin legs, and
    extra (mask maker, ramp, shade) layers on top."""
    r = np.random.default_rng(seed)
    h, w = H * SS, int(H * ratio) * SS
    y, x = grid(h, w)
    fine = r.random((h, w)) - 0.5
    items = [(cx * w, cy * h, cz * w, rx * w, ry * h) for cx, cy, cz, rx, ry in parts]
    n, hit = blobs(h, w, items)
    shade = lambert(n) * 0.85 + 0.12 + fine * 0.2 + tex((h, w), SS * 2, seed + 1, 0.2) + bright
    lm = np.zeros((h, w), dtype=bool)
    la = np.zeros((h, w))
    for x0, y0, x1, y1, th in legs:
        m, a = seg(x, y, x0 * w, y0 * h, x1 * w, y1 * h, th * w)
        la = np.where(m & ~lm, a, la)
        lm |= m
    layers = [(lm & ~hit, ramp, np.clip(cyl(la, 0.4) + fine * 0.2 + bright, 0, 1)), (hit, ramp, np.clip(shade, 0, 1))]
    for make, rp, s in extra:
        m = make(x, y, w, h)
        layers.append((m, rp, np.clip(s + fine * 0.2, 0, 1) * np.ones((h, w))))
    return finish(H, layers, (w / 2 / SS, h / SS), srng)


def sheep(H, seed):
    head = lambda x, y, w, h: ((x - w * 0.14) / (w * 0.1)) ** 2 + ((y - h * 0.38) / (h * 0.16)) ** 2 < 1
    legs = [(0.32, 0.6, 0.32, 1.0, 0.03), (0.42, 0.6, 0.42, 0.98, 0.03),
            (0.68, 0.6, 0.68, 1.0, 0.03), (0.78, 0.6, 0.78, 0.98, 0.03)]
    def wool(x, y, w, h):
        return np.zeros(x.shape, dtype=bool)
    return animal(H, 1.4, seed, [(0.55, 0.45, 0, 0.36, 0.3), (0.38, 0.4, 0.05, 0.2, 0.24), (0.72, 0.42, 0.05, 0.2, 0.25)],
                  legs, 'cap', 0.05, extra=[(head, 'trousers', 0.4), (wool, 'cap', 0.8)])


def kangaroo(H, seed):
    legs = [(0.55, 0.75, 0.42, 0.98, 0.05), (0.42, 0.98, 0.25, 1.0, 0.03), (0.38, 0.45, 0.3, 0.6, 0.025)]
    tail = lambda x, y, w, h: seg(x, y, w * 0.6, h * 0.75, w * 0.98, h * 0.98, w * 0.07, w * 0.025)[0]
    ears = lambda x, y, w, h: (seg(x, y, w * 0.33, h * 0.12, w * 0.36, h * 0.0, w * 0.035)[0] |
                               seg(x, y, w * 0.38, h * 0.12, w * 0.43, h * 0.01, w * 0.035)[0])
    return animal(H, 0.8, seed, [(0.52, 0.6, 0, 0.18, 0.26), (0.45, 0.35, 0.02, 0.1, 0.14),
                                 (0.33, 0.17, 0.05, 0.1, 0.08), (0.58, 0.78, 0, 0.17, 0.14)],
                  legs, 'wood', 0.12, extra=[(tail, 'wood', 0.55), (ears, 'wood', 0.5)])


def crocodile(H, seed):
    def scutes(x, y, w, h):
        return (y < h * 0.45) & (np.abs(y - h * 0.38) < h * 0.08) & ((x // (SS * 1.5)) % 2 == 0) & (x > w * 0.15) & (x < w * 0.8)
    tail = lambda x, y, w, h: seg(x, y, w * 0.6, h * 0.62, w * 0.99, h * 0.85, h * 0.25, h * 0.04)[0]
    legs = [(0.3, 0.7, 0.26, 1.0, 0.025), (0.5, 0.7, 0.54, 1.0, 0.025)]
    return animal(H, 4.0, seed, [(0.42, 0.62, 0, 0.24, 0.32), (0.12, 0.72, 0.02, 0.12, 0.17), (0.2, 0.65, 0.02, 0.08, 0.25)],
                  legs, 'oob', -0.05, extra=[(tail, 'oob', 0.35), (scutes, 'oob', 0.15)])


def hippo(H, seed):
    legs = [(0.3, 0.7, 0.3, 1.0, 0.06), (0.42, 0.7, 0.42, 0.98, 0.06), (0.66, 0.7, 0.66, 1.0, 0.06), (0.78, 0.7, 0.78, 0.98, 0.06)]
    ear = lambda x, y, w, h: ((x - w * 0.24) / (w * 0.025)) ** 2 + ((y - h * 0.15) / (h * 0.06)) ** 2 < 1
    return animal(H, 1.7, seed, [(0.56, 0.48, 0, 0.36, 0.33), (0.22, 0.42, 0.05, 0.13, 0.2), (0.12, 0.52, 0.06, 0.1, 0.17)],
                  legs, 'rock', -0.1, extra=[(ear, 'rock', 0.3)])


def elk(H, seed):
    legs = [(0.3, 0.55, 0.28, 1.0, 0.025), (0.38, 0.55, 0.38, 0.98, 0.025),
            (0.66, 0.55, 0.68, 1.0, 0.025), (0.74, 0.55, 0.74, 0.98, 0.025)]
    def antlers(x, y, w, h):
        m = seg(x, y, w * 0.2, h * 0.2, w * 0.36, h * 0.0, w * 0.012)[0]
        for t in (0.25, 0.5, 0.75):
            bx, by = w * (0.2 + 0.16 * t), h * (0.2 - 0.2 * t)
            m |= seg(x, y, bx, by, bx - w * 0.06, by - h * 0.07, w * 0.009)[0]
        return m
    neck = lambda x, y, w, h: seg(x, y, w * 0.35, h * 0.42, w * 0.2, h * 0.24, w * 0.06, w * 0.045)[0]
    return animal(H, 1.25, seed, [(0.53, 0.42, 0, 0.26, 0.17), (0.17, 0.27, 0.05, 0.08, 0.07)],
                  legs, 'wood', -0.05, extra=[(neck, 'wood', 0.25), (antlers, 'sand', 0.65)])


def crane(H, seed):
    legs = [(0.48, 0.62, 0.46, 1.0, 0.018), (0.55, 0.62, 0.58, 1.0, 0.018)]
    neck = lambda x, y, w, h: seg(x, y, w * 0.4, h * 0.42, w * 0.3, h * 0.08, w * 0.04, w * 0.03)[0]
    black = lambda x, y, w, h: seg(x, y, w * 0.38, h * 0.38, w * 0.31, h * 0.12, w * 0.045)[0] | \
        (((x - w * 0.72) / (w * 0.16)) ** 2 + ((y - h * 0.52) / (h * 0.08)) ** 2 < 1)
    crown = lambda x, y, w, h: ((x - w * 0.3) / (w * 0.04)) ** 2 + ((y - h * 0.06) / (h * 0.025)) ** 2 < 1
    beak = lambda x, y, w, h: seg(x, y, w * 0.28, h * 0.08, w * 0.12, h * 0.12, w * 0.014)[0]
    return animal(H, 0.8, seed, [(0.55, 0.48, 0, 0.22, 0.13)], legs, 'cap', 0.0,
                  extra=[(neck, 'cap', 0.85), (black, 'trousers', 0.2), (crown, 'flag', 0.7), (beak, 'grey', 0.5)])


# ------------------------------------------------------------------ the sets
def stone_wall(H, seed):
    return mkobj.wall(H, seed)


# scenery: [(type, variant, maker, small height in px)]
SETS = {
    1: [(OBJ_LANDMARK, 0, kirk, 36),
        (OBJ_ANIMAL, 0, sheep, 8),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.7, 'rock', 4, 0.05, layered=True), 8),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.5, 'pine', 20, 'gold', 0.5, 0.8, spiky=True), 12),
        (OBJ_HOUSE, 1, lambda H, s: cottage(H, s, 1.7, 'grey', 0.9, 'slate', 'grey'), 24)],
    2: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 1.1, 0.15, 0.62, 'tree', 50, 0.7, lean=-0.9, sparse=0.3), 20),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.6, 'pine', 22, 'gold', 0.6, 0.85, spiky=True), 12),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.3, 'rock', 2.5, -0.05), 10),
        (OBJ_ROCK, 1, cliff_rock, 12),
        (OBJ_HOUSE, 0, lambda H, s: cottage(H, s, 1.6, 'grey', 0.95, 'thatch', 'wood'), 22),
        (OBJ_WALL, 0, lambda H, s: mkobj.wall(H, s), 8),
        (OBJ_GRASS, 0, lambda H, s: tuft(H, s, 1.4, 'oob', 28, 1.0, 0.1), 8),
        (OBJ_LANDMARK, 0, lighthouse, 40),
        (OBJ_ANIMAL, 0, sheep, 8)],
    3: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 1.15, 0.05, 0.62, 'tree', 70, 0.75, bloom=0.5, branches=4, sparse=0.15), 24),
        (OBJ_TREE, 1, lambda H, s: crown_tree(H, s, 0.9, 0.04, 0.72, 'red', 60, 0.8, bright=0.05), 24),
        (OBJ_PINE, 0, pad_pine, 30),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.6, 'tree', 14, 'blossom', 0.9, 0.7, bright=0.05), 10),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.1, 'rock', 2.0, -0.1), 10),
        (OBJ_HOUSE, 0, tea_house, 22),
        (OBJ_WALL, 0, fence, 6),
        (OBJ_LANDMARK, 0, pagoda, 44),
        (OBJ_ANIMAL, 0, crane, 10)],
    4: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 0.85, 0.02, 0.55, 'far', 45, 0.8, trunk='wall', tw=0.03, sparse=0.38,
                                              branches=3, lean=0.3, pale_trunk=True), 30),
        (OBJ_TREE, 1, lambda H, s: crown_tree(H, s, 0.95, 0.05, 0.6, 'far', 55, 0.9, trunk='wall', tw=0.04, sparse=0.3,
                                              branches=4, lean=-0.4, pale_trunk=True), 26),
        (OBJ_PINE, 0, lambda H, s: conifer(H, s, 0.55, 5, 'far', 0.7, 0.2, 0.04, 0.85, 0.05), 24),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.3, 'far', 18, 'gold', 0.35, 1.0), 12),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.6, 'sand', 3, -0.1, layered=True), 8),
        (OBJ_HOUSE, 0, shed, 20),
        (OBJ_GRASS, 0, lambda H, s: tuft(H, s, 1.2, 'sand', 34, 0.7, -0.15), 8),
        (OBJ_LANDMARK, 0, windmill, 40),
        (OBJ_ANIMAL, 0, kangaroo, 12)],
    5: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 1.25, 0.05, 0.3, 'tree', 45, 0.45, tw=0.03, branches=5, sparse=0.2), 24),
        (OBJ_TREE, 1, lambda H, s: crown_tree(H, s, 1.0, 0.05, 0.6, 'tree', 60, 0.8, branches=3), 26),
        (OBJ_PALM, 0, palm, 28),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.5, 'oob', 18, None, 0, 0.8, -0.05, spiky=True), 10),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.4, 'rock', 2.2, 0.05), 12),
        (OBJ_HOUSE, 0, rondavel, 20),
        (OBJ_GRASS, 0, lambda H, s: tuft(H, s, 1.4, 'sand', 30, 0.8, -0.2, heads=True), 10),
        (OBJ_LANDMARK, 0, baobab, 40),
        (OBJ_ANIMAL, 0, crocodile, 5),
        (OBJ_ANIMAL, 1, hippo, 10)],
    6: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 0.55, 0.03, 0.7, 'tree', 45, 1.0, trunk='grey', tw=0.05,
                                              bright=0.18, sparse=0.25, pale_trunk=True), 30),
        (OBJ_PINE, 0, lambda H, s: conifer(H, s, 0.36, 11, 'pine', 0.35, 0.1, 0.01, 0.93), 36),
        (OBJ_PINE, 1, lambda H, s: conifer(H, s, 0.3, 9, 'pine', 0.2, 0.08, 0.01, 0.9, 0.08), 40),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.7, 'pine', 16, None, 0, 0.6, 0.05), 8),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.4, 'rock', 3.5, 0.1), 12),
        (OBJ_HOUSE, 0, log_cabin, 22),
        (OBJ_LANDMARK, 0, hotel, 48),
        (OBJ_ANIMAL, 0, elk, 14)],
    7: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 0.57, 0.03, 0.65, 'tree', 57, 0.75, trunk='grey',
                                             tw=0.04, branches=4, sparse=0.25, pale_trunk=True), 28),
        (OBJ_BUSH, 0, lambda H, s: shrub(H, s, 1.7, 'far', 20, 'blossom', 0.9, 0.70), 9),
        (OBJ_HOUSE, 0, lambda H, s: cottage(H, s, 1.8, 'wall', 0.7, 'slate', 'grey'), 23)],
    8: [(OBJ_PALM, 0, palm, 30),
        (OBJ_ROCK, 0, lambda H, s: boulder(H, s, 1.6, 'sand', 2.7, 0.07, layered=True), 9),
        (OBJ_HOUSE, 0, lambda H, s: cottage(H, s, 1.9, 'sand', 0.72, 'flat', 'wall'), 22)],
    9: [(OBJ_TREE, 0, lambda H, s: crown_tree(H, s, 1.23, 0.05, 0.6, 'tree', 57, 0.65, trunk='bark',
                                             tw=0.06, branches=5, sparse=0.22, lean=0.25), 27),
        (OBJ_PINE, 0, lambda H, s: crown_tree(H, s, 1.2, 0.03, 0.4, 'pine', 53, 0.62, trunk='bark',
                                             tw=0.045, branches=3, sparse=0.14), 30),
        (OBJ_WALL, 0, stone_wall, 8),
        (OBJ_HOUSE, 0, lambda H, s: cottage(H, s, 1.9, 'grey', 0.97, 'slate', 'sand'), 24)],
}

# shared art for slots no set fills yet
SHARED = [(OBJ_STAND, 0, grandstand, (16, 48))]

NAMES = {1: 'links', 2: 'Irish clifftop', 3: 'Japanese seaside', 4: 'Australian sandbelt',
         5: 'South African bush', 6: 'Canadian Rockies', 7: 'English heathland',
         8: 'Gulf desert', 9: 'Algarve hillside'}


def set_id(scenery, typ, variant, size):
    return 3000 + 200 * scenery + 10 * typ + 2 * variant + size


def make(res, prev):
    """Adds every set's sprites to res as ('Spr8', id, data); previews and a
    sheet per set to prev if given. Returns the bytes added."""
    total = 0
    sheets = {}
    for typ, v, maker, sizes in SHARED:
        for k, H in enumerate(sizes):
            idx, ax, ay = maker(H, 7000 + 100 * typ + 10 * v + k)
            data = mkobj.spr8(idx, ax, ay)
            res.append(('Spr8', 1000 + typ * 10 + k, data))
            total += len(data)
            if prev:
                mkobj.preview(idx, prev / f'spr-{1000 + typ * 10 + k}.png', 4)
    for s, items in SETS.items():
        sheets[s] = []
        for typ, v, maker, sizes in items:
            # the big size is twice the small one, not three times as for
            # the shared art, to keep the PRC down; near objects grow past it
            for k, H in enumerate((sizes, sizes * 2)):
                idx, ax, ay = maker(H, 1000 * s + 100 * typ + 10 * v + k)
                rid = set_id(s, typ, v, k)
                data = mkobj.spr8(idx, ax, ay)
                res.append(('Spr8', rid, data))
                total += len(data)
                if prev:
                    mkobj.preview(idx, prev / f'spr-{rid}.png', 4 if k == 0 else 2)
                if k == 1:
                    sheets[s].append(idx)
    if prev:
        from PIL import Image
        for s, ims in sheets.items():
            pad = 6
            W = sum(i.shape[1] for i in ims) + pad * (len(ims) + 1)
            Hh = max(i.shape[0] for i in ims) + 2 * pad
            sheet = np.zeros((Hh, W, 3), dtype=np.uint8)
            sheet[:] = (92, 150, 70)
            xx = pad
            for idx in ims:
                rgb = mkobj.PAL[idx].astype(np.uint8)
                hh, ww = idx.shape
                y0 = Hh - pad - hh
                sub = sheet[y0:y0 + hh, xx:xx + ww]
                sub[idx > 0] = rgb[idx > 0]
                xx += ww + pad
            Image.fromarray(sheet).resize((W * 2, Hh * 2), Image.NEAREST).save(prev / f'set-{s}.png')
    return total
