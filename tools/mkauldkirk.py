#!/usr/bin/env python3
"""Writes courses/auldkirk/: course.txt, hole-NN.txt and hole-NN.png for
Auld Kirk Links, an old links running out along the shore and back.

Each hole is laid out below from a centre line, the green and a few
features; the shapes, objects and heightmap come from that with the hole's
own random numbers, so running it again gives the same files. Edit the
HOLES table and run it again rather than editing the output.

The outward nine (1 to 9) have the dunes and the beach on the left, the
inward nine (10 to 18) the inland boundary on the left. Out of bounds is the
beach and the boundary. Double greens: 4 and 14, 6 and 12, 7 and 11 share a
green, each with its own pins on its own half.

Usage: mkauldkirk.py [COURSEDIR]
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CELL = 8                  # yards between heightmap points
STEP = 3 / 64             # yards per grey step, 12 yards from black to white
BASE = 64                 # grey of the level ground

# Each hole: name, par, width, centre line from the back tee to the middle of
# the green (yards; x across, y along), fairway half widths along it, the
# green (half sizes and turn in degrees), which side a double green goes
# (+1 right, -1 left), pot bunkers as (x, y, r), and extras.
HOLES = [
    dict(name="Kirk Gate", par=4, W=220, line=[(120, 14), (118, 260), (110, 408)],
         fw=(40, 34), green=(18, 16, 0), bunkers=[(92, 248, 4), (140, 300, 4), (96, 392, 4), (128, 418, 3.5)],
         town=(1, 0, 240)),
    dict(name="Saut Pans", par=4, W=220, line=[(110, 14), (126, 255), (100, 432)],
         fw=(36, 30), green=(16, 19, 20), bunkers=[(150, 235, 4), (154, 262, 3.5), (104, 296, 4), (84, 420, 4),
                                                   (118, 452, 3.5)]),
    dict(name="Whaup's Nest", par=3, W=200, line=[(110, 14), (96, 190)],
         fw=(0, 0), green=(17, 15, -15), bunkers=[(78, 178, 4), (84, 202, 3.5), (116, 176, 4)],
         hollow=(100, 150, 18)),
    dict(name="Lang Dunes", par=5, W=230, line=[(100, 14), (110, 265), (138, 430), (126, 560)],
         fw=(38, 34, 30), green=(17, 18, 0), double=1,
         bunkers=[(76, 252, 4), (138, 270, 4), (112, 340, 3.5), (120, 352, 3.5), (160, 440, 4),
                  (106, 514, 4), (110, 546, 3.5)]),
    dict(name="Hidie Hole", par=4, W=210, line=[(104, 14), (96, 250), (120, 392)],
         fw=(36, 26), green=(15, 16, 25), bunkers=[(64, 236, 4), (124, 262, 4), (102, 374, 4)],
         ridge=(330, 2.2)),
    dict(name="Gorse Brae", par=4, W=220, line=[(96, 14), (112, 262), (140, 420)],
         fw=(32, 28), green=(16, 18, 10), double=1, bunkers=[(86, 270, 4), (138, 248, 4), (122, 402, 4)],
         gorse_heavy=True),
    dict(name="The Neuk", par=4, W=210, line=[(100, 14), (104, 235), (78, 362)],
         fw=(40, 30), green=(16, 15, -20), double=1, bunkers=[(130, 226, 4), (96, 304, 4), (100, 350, 4),
                                                              (64, 336, 3.5)]),
    dict(name="Selkie", par=3, W=200, line=[(120, 14), (88, 172)],
         fw=(0, 0), green=(16, 17, 0), bunkers=[(68, 162, 4), (106, 160, 4), (96, 194, 3.5)],
         shore=True),
    dict(name="The Ootby", par=4, W=220, line=[(110, 14), (112, 275), (100, 440)],
         fw=(38, 32), green=(17, 17, 0), bunkers=[(84, 266, 4), (90, 280, 3.5), (140, 300, 4), (120, 424, 4)]),
    dict(name="The Haar", par=4, W=220, line=[(110, 14), (102, 260), (116, 410)],
         fw=(40, 32), green=(17, 17, 0), bunkers=[(130, 246, 4), (76, 290, 4), (100, 390, 4), (138, 404, 3.5)]),
    dict(name="Bents", par=3, W=200, line=[(96, 14), (110, 205)],
         fw=(0, 0), green=(16, 16, 10), double=1, bunkers=[(92, 196, 4), (128, 186, 4), (104, 172, 4)]),
    dict(name="Burnside", par=4, W=220, line=[(116, 14), (110, 245), (124, 378)],
         fw=(40, 32), green=(16, 17, 0), double=1, bunkers=[(86, 232, 4), (100, 360, 3.5)],
         burn=275),
    dict(name="Deil's Kailyard", par=5, W=240, line=[(112, 14), (104, 262), (128, 450), (120, 585)],
         fw=(40, 38, 30), green=(18, 19, 0), bunkers=[(76, 250, 4), (138, 266, 4), (106, 548, 4), (138, 574, 3.5)],
         field=(300, 440, 22)),
    dict(name="Corbie", par=5, W=230, line=[(118, 14), (110, 258), (90, 420), (110, 552)],
         fw=(38, 32, 28), green=(17, 18, 0), double=1,
         bunkers=[(146, 250, 4), (78, 238, 4), (118, 400, 4), (64, 430, 3.5), (88, 536, 4)]),
    dict(name="The Smiddy", par=4, W=220, line=[(104, 14), (116, 250), (100, 418)],
         fw=(36, 30), green=(16, 18, -10), bunkers=[(142, 236, 4), (88, 260, 4), (124, 404, 4), (84, 410, 3.5)],
         gorse_heavy=True),
    dict(name="Doocot", par=3, W=200, line=[(108, 14), (98, 218)],
         fw=(0, 0), green=(17, 16, 0), bunkers=[(78, 210, 4), (118, 206, 4), (100, 238, 3.5)],
         houses=[(170, 262, 1)]),
    dict(name="The Causey", par=4, W=220, line=[(124, 14), (120, 270), (96, 450)],
         fw=(36, 28), green=(13, 19, 10), bunkers=[(150, 262, 4), (92, 428, 4), (108, 296, 3.5)],
         road=True),
    dict(name="Hame", par=5, W=230, line=[(110, 14), (118, 262), (104, 420), (116, 520)],
         fw=(40, 36, 32), green=(19, 18, 0), bunkers=[(84, 248, 4), (146, 274, 4), (130, 450, 3.5), (96, 500, 4)],
         houses=[(80, 580, 0), (116, 588, 2), (156, 584, 1)], town=(-1, 380, 600)),
]

TEES = ["Medal", "Club", "Forward"]
# Each hole's map turned against the course in degrees, which moves the
# backdrop: going out the hills are ahead, coming back the town and the kirk.
# The backdrop repeats twice a turn, so 60 here is the far side of the bay.
TURNS = [0, 8, -10, 5, 0, 12, 20, 35, 25, 50, 70, 60, 55, 62, 58, 66, 60, 60]
PINS_ALONG = [(-0.45, -0.3), (0.4, -0.35), (0.35, 0.45), (-0.4, 0.4)]


def walk(line, d):
    """The point and direction d yards along a polyline."""
    for (x0, y0), (x1, y1) in zip(line, line[1:]):
        seg = math.hypot(x1 - x0, y1 - y0)
        if d <= seg:
            t = d / seg
            return (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t), ((x1 - x0) / seg, (y1 - y0) / seg)
        d -= seg
    (x0, y0), (x1, y1) = line[-2], line[-1]
    seg = math.hypot(x1 - x0, y1 - y0)
    return line[-1], ((x1 - x0) / seg, (y1 - y0) / seg)


def length(line):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(line, line[1:]))


def clampxy(p, W, L):
    return (min(max(p[0], 0), W), min(max(p[1], 0), L))


def fmt(v):
    s = "%.1f" % v
    return s[:-2] if s.endswith(".0") else s


def pts_text(pts):
    return " ".join("%s,%s" % (fmt(x), fmt(y)) for x, y in pts)


def ribbon(line, d0, d1, halfw, extra, rng, W, L, step=20):
    """A fairway-like polygon along the line from d0 to d1, half width from
    the list halfw (one per segment) plus extra, with wavy edges."""
    left, right = [], []
    segs = []
    acc = 0
    for a, b in zip(line, line[1:]):
        acc += math.hypot(b[0] - a[0], b[1] - a[1])
        segs.append(acc)
    n = max(2, int((d1 - d0) / step) + 1)
    n = min(n, 30)
    wob = rng.uniform(-1, 1, n)
    for i in range(n):
        d = d0 + (d1 - d0) * i / (n - 1)
        k = next((j for j, s in enumerate(segs) if d <= s), len(segs) - 1)
        hw = halfw[min(k, len(halfw) - 1)] + extra
        # narrow towards the ends
        t = i / (n - 1)
        hw *= 0.7 + 0.3 * math.sin(math.pi * min(1, t * 1.6 + 0.15)) if t < 0.5 else 0.8 + 0.2 * math.sin(math.pi * t)
        (x, y), (dx, dy) = walk(line, d)
        nx, ny = dy, -dx     # to the right
        lw = hw * (1 + 0.12 * wob[i])
        rw = hw * (1 - 0.12 * wob[(i + 3) % n])
        left.append(clampxy((x - nx * lw, y - ny * lw), W, L))
        right.append(clampxy((x + nx * rw, y + ny * rw), W, L))
    return left + right[::-1]


def ellipse_pts(cx, cy, rx, ry, turn, count, W, L, rng=None, wob=0.0):
    out = []
    t = math.radians(turn)
    for i in range(count):
        a = 2 * math.pi * i / count
        k = 1 + (rng.uniform(-wob, wob) if rng is not None else 0)
        x, y = rx * k * math.cos(a), ry * k * math.sin(a)
        x, y = x * math.cos(t) - y * math.sin(t), x * math.sin(t) + y * math.cos(t)
        out.append(clampxy((cx + x, cy + y), W, L))
    return out


def inside(poly, x, y):
    """Even-odd test, numpy arrays x and y."""
    res = np.zeros(np.broadcast(x, y).shape, dtype=bool)
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        cond = (y0 > y) != (y1 > y)
        with np.errstate(divide='ignore', invalid='ignore'):
            xi = x0 + (y - y0) * (x1 - x0) / np.where(y1 - y0 == 0, 1e-9, y1 - y0)
        res ^= cond & (x < xi)
    return res


def vnoise(shape, cell, rng):
    h, w = shape
    gh, gw = int(h / cell) + 3, int(w / cell) + 3
    g = rng.random((gh, gw))
    y = np.arange(h)[:, None] / cell
    x = np.arange(w)[None, :] / cell
    y0, x0 = np.floor(y).astype(int), np.floor(x).astype(int)
    fy, fx = y - y0, x - x0
    fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
    a = g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx
    b = g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy - 0.5


def make_hole(n, d, out):
    rng = np.random.default_rng(1000 + n)
    W, par, line = d["W"], d["par"], d["line"]
    gx, gy = line[-1]
    grx, gry, gturn = d["green"]
    L = int(gy + gry + 40)
    if d.get("houses"):
        L = max(L, int(max(h[1] for h in d["houses"]) + 12))
    L = min(L, 640)
    out_nine = n <= 9
    sea = -1 if out_nine else 0      # the beach is on the left going out
    lines = []
    shapes = []
    objects = []

    yards = round(length(line))
    lines.append("name %s" % d["name"])
    lines.append("par %d" % par)
    lines.append("size %d %d" % (W, L))
    lines.append("heightmap hole-%02d.png %d" % (n, CELL))
    lines.append("height 0 %s" % STEP)
    lines.append("base rough")
    aim = line[1] if par > 3 else line[-1]
    lines.append("aim %s %s" % (fmt(aim[0]), fmt(aim[1])))
    lines.append("turn %d" % TURNS[n - 1])
    lines.append("")

    # Tees: the back one at the line's start, the others on along it.
    tee_pts = []
    for k, ahead in enumerate((0, 22 if par > 3 else 14, 55 if par > 3 else 30)):
        (x, y), (dx, dy) = walk(line, ahead)
        side = (0, 9, -12)[k] * (1 if n % 2 else -1)
        x, y = x + dy * side, y - dx * side
        tee_pts.append((x, y))
        if k == 0:
            lines.append("tee %s %s %s %d" % (TEES[k], fmt(x), fmt(y), yards))
        else:
            lines.append("tee %s %s %s %d" % (TEES[k], fmt(x), fmt(y), round(yards - ahead)))
    lines.append("")

    # Green, double greens reaching to one side.
    double = d.get("double", 0)
    if double:
        gcx = gx + double * grx * 0.9
        g_rx = grx * 1.95
    else:
        gcx, g_rx = gx, grx
    t = math.radians(gturn)
    for px, py in PINS_ALONG:
        ox, oy = px * grx * 1.2, py * gry * 1.2
        lines.append("pin %s %s" % (fmt(gx + ox * math.cos(t) - oy * math.sin(t)),
                                    fmt(gy + ox * math.sin(t) + oy * math.cos(t))))
    lines.append("")

    # Out of bounds and the ground beyond the edges.
    beach_w = 10
    # fescue and dunes along the beach or the boundary side
    dune_side = 0  # x from 0
    shapes.append(("deep-rough", [(0, 0), (34, 0), (30 + 8 * math.sin(n), L * 0.5), (36, L), (0, L)]))
    if not out_nine:
        shapes.append(("deep-rough", [(W, 0), (W - 20, 0), (W - 24, L * 0.5), (W - 18, L), (W, L)]))

    # First cut and fairway.
    tee_gap = 45 if par > 3 else 0
    green_front = length(line) - gry - 4
    if par > 3:
        hw = list(d["fw"])
        shapes.append(("first-cut", ribbon(line, tee_gap - 6, green_front + 6, hw, 5, rng, W, L)))
        shapes.append(("fairway", ribbon(line, tee_gap, green_front + 2, hw, 0, rng, W, L)))
    else:
        # an apron in front of the green
        shapes.append(("first-cut", ellipse_pts(gx, gy - gry - 6, grx + 4, 14, gturn, 12, W, L)))

    # The road and the wall on the left of the road hole, round behind the green.
    road = None
    if d.get("road"):
        road_l = [(44, 160), (50, 260), (62, 360), (gx - grx - 7, gy - 10), (gx - grx - 2, gy + gry + 6),
                  (gx + 30, gy + gry + 16), (W, gy + gry + 18)]
        road = road_l
        # a road 7 yards wide: the line offset each way
        lpts, rpts = [], []
        for i, (x, y) in enumerate(road_l):
            a = road_l[max(i - 1, 0)]
            b = road_l[min(i + 1, len(road_l) - 1)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            m = math.hypot(dx, dy)
            nx, ny = dy / m, -dx / m
            lpts.append(clampxy((x - nx * 3.5, y - ny * 3.5), W, L))
            rpts.append(clampxy((x + nx * 3.5, y + ny * 3.5), W, L))
        shapes.append(("path", lpts + rpts[::-1]))

    # Green and fringe.
    shapes.append(("fringe", ellipse_pts(gcx, gy, g_rx + 3.5, gry + 3.5, gturn, 20, W, L)))
    shapes.append(("green", ellipse_pts(gcx, gy, g_rx, gry, gturn, 20, W, L)))

    # Pot bunkers, the bunker field and the burn.
    pots = list(d["bunkers"])
    if d.get("field"):
        y0, y1, count = d["field"]
        placed = 0
        while placed < count:
            y = rng.uniform(y0, y1)
            (cx, _), _ = walk(line, y - 14)
            x = cx + rng.uniform(-40, 40)
            if all(math.hypot(x - bx, y - by) > 14 for bx, by, _ in pots):
                pots.append((x, y, rng.uniform(3, 4.2)))
                placed += 1
    for bx, by, r in pots:
        shapes.append(("bunker", ellipse_pts(bx, by, r * rng.uniform(0.9, 1.15), r, rng.uniform(0, 180), 10, W, L,
                                             rng, 0.08)))
    burn = d.get("burn")
    if burn:
        top, bot = [], []
        for i in range(12):
            x = W * i / 11
            y = burn + 6 * math.sin(i * 0.9 + 1) + 3 * math.sin(i * 2.3)
            top.append(clampxy((x, y + 2.5), W, L))
            bot.append(clampxy((x, y - 2.5), W, L))
        shapes.append(("water", bot + top[::-1]))

    # Tee boxes.
    for k, (x, y) in enumerate(tee_pts):
        hw = 7 if k == 0 else 5
        shapes.append(("tee", [clampxy((x - hw, y - 6), W, L), clampxy((x + hw, y - 6), W, L),
                               clampxy((x + hw, y + 6), W, L), clampxy((x - hw, y + 6), W, L)]))

    # Out of bounds: the beach going out, the boundary coming back, the town.
    shapes.append(("oob", [(0, 0), (beach_w, 0), (beach_w + 2, L * 0.5), (beach_w, L), (0, L)]))
    if road:
        # beyond the wall
        wall_x = [(x - 6.5, y) for x, y in road[:4]]
        shapes.append(("oob", [(0, 0), (40, 0)] + [clampxy(p, W, L) for p in wall_x] + [(0, gy)]))
    town = d.get("town")
    if town:
        side, y0, y1 = town
        if side > 0:
            shapes.append(("oob", [(W - 12, y0), (W, y0), (W, y1), (W - 12, y1)]))
        else:
            shapes.append(("oob", [(0, y0), (16, y0), (16, y1), (0, y1)]))

    # Heightmap.
    hw_, hh_ = W // CELL + 2, L // CELL + 2
    ys, xs = np.mgrid[0:hh_, 0:hw_] * CELL
    xs, ys = xs.astype(float), ys.astype(float)
    # humps and hollows everywhere, bigger in the rough
    hum = vnoise((hh_, hw_), 2.0, rng) * 4.5 + vnoise((hh_, hw_), 4.0, rng) * 4.0 + vnoise((hh_, hw_), 1.3, rng) * 1.6
    z = 3.0 + hum
    # the dunes along the left edge, highest just inside the beach, then a
    # broken second line, dropping to the beach
    ridge = 6.0 + vnoise((hh_, 1), 2.5, rng)[:, 0] * 6 + (1.0 if out_nine else -2.5)
    dune = np.clip(1 - np.abs(xs - (beach_w + 14)) / 18, 0, 1) ** 1.2 * ridge[:, None]
    second = np.clip(vnoise((hh_, hw_), 2.5, rng) * 3 + 0.4, 0, 1)
    dune += np.clip(1 - np.abs(xs - 48) / 16, 0, 1) * second * (4.0 if out_nine else 2.0)
    z += dune
    z = np.where(xs < beach_w, 0.8 + xs / beach_w * 0.6, z)
    if not out_nine:
        # banks along the inland boundary
        z += np.clip(1 - (W - xs) / 30, 0, 1) * (2.5 + vnoise((hh_, hw_), 3.0, rng) * 3)
    if d.get("ridge"):
        ry, rh = d["ridge"]
        z += np.clip(1 - np.abs(ys - ry) / 20, 0, 1) * rh * (0.6 + 0.4 * np.cos((xs - gx) / 60))
    if d.get("hollow"):
        hx, hy, hr = d["hollow"]
        z -= np.clip(1 - np.hypot(xs - hx, ys - hy) / hr, 0, 1) * 1.6
    if d.get("shore"):
        # the green sits on a shelf above the beach
        z += np.clip(1 - np.hypot(xs - gx, ys - gy) / 50, 0, 1) * 0.8
    # fairways and greens calmer: pull towards a smooth version
    smooth = 2.5 + vnoise((hh_, hw_), 5.0, rng) * 4.0
    fw_mask = np.zeros_like(z, dtype=bool)
    for name, pts in shapes:
        if name in ("fairway", "first-cut", "tee"):
            fw_mask |= inside(pts, xs, ys)
    soft = np.where(fw_mask, 0.5, 1.0)
    z = smooth + (z - smooth) * soft
    # the green: a plane tilting up to the back, level over the whole green
    # and fringe and blending into the ground 10 yd past them
    gd = np.hypot((xs - gcx) / (g_rx + 4), (ys - gy) / (gry + 4))
    gw = np.clip((1.8 - gd) / 0.8, 0, 1)
    gz0 = smooth[int(round(gy / CELL)), int(round(gcx / CELL))]
    gz = gz0 + (ys - gy) * 0.018 + (xs - gcx) * 0.006 * rng.choice([-1, 1])
    if d.get("shore"):
        gz += 0.6
    z = z * (1 - gw) + gz * gw
    # tees: level and a little up
    for x, y in tee_pts:
        tw = np.clip(1.6 - np.hypot(xs - x, ys - y) / 14, 0, 1)
        z = z * (1 - tw) + (smooth + 0.4) * tw
    # the burn's banks
    if burn:
        z -= np.clip(1 - np.abs(ys - burn) / 10, 0, 1) * 0.9
    # the road a little below the green
    grey = np.clip(np.round(z / STEP), 0, 255).astype(np.uint8)
    Image.fromarray(grey[::-1]).save(out / ("hole-%02d.png" % n))

    # Objects: gorse in clumps off the line, marram on the dunes, the wall, houses.
    fairway = next((p for s, p in shapes if s == "first-cut"), None)

    def clear(x, y, gap):
        if x < beach_w + 2 or x > W - 2 or y < 30 or y > L - 2:
            return False
        for s, p in shapes:
            if s in ("first-cut", "fairway", "green", "fringe", "tee", "path", "bunker", "water", "oob"):
                if inside(p, np.array(x), np.array(y)):
                    return False
        if fairway is not None:
            pass
        # off the line of play by gap yards
        best = 1e9
        for k in range(0, int(length(line)), 10):
            (lx, ly), _ = walk(line, k)
            best = min(best, math.hypot(x - lx, y - ly))
        return best > gap

    clumps = 7 if d.get("gorse_heavy") else 4
    if par == 3:
        clumps = 3
    for c in range(clumps):
        for tries in range(40):
            cy = rng.uniform(70, L - 30)
            (lx, _), _ = walk(line, cy - 14)
            side = rng.choice([-1, 1])
            cx = lx + side * rng.uniform(30, 58)
            if clear(cx, cy, 26):
                break
        else:
            continue
        for k in range(int(rng.integers(6, 12))):
            x = cx + rng.normal(0, 6)
            y = cy + rng.normal(0, 9)
            if clear(x, y, 22):
                objects.append(("gorse", x, y, int(rng.integers(0, 3)), int(rng.integers(60, 96))))
    for k in range(24):
        x = rng.uniform(beach_w + 3, 60)
        y = rng.uniform(40, L - 10)
        if clear(x, y, 20):
            objects.append(("grass", x, y, int(rng.integers(0, 3)), int(rng.integers(70, 110))))
    if not out_nine:
        for k in range(8):
            x = rng.uniform(W - 24, W - 4)
            y = rng.uniform(40, L - 10)
            objects.append(("gorse", x, y, 0, int(rng.integers(56, 80))))
    if road:
        # wall sections every 4 yards on the outer side of the road
        pts = [(x - 6.5, y) for x, y in road[:4]]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            seg = math.hypot(x1 - x0, y1 - y0)
            for k in range(int(seg / 4)):
                t = k * 4 / seg
                objects.append(("wall", x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 0, 64))
        for x, y in ((26, 300), (28, 380), (gx - grx - 22, gy + 4)):
            objects.append(("house", x, y, 0, 64))
    for x, y, v in d.get("houses", []):
        objects.append(("house", x, y, v, 64))
    if town:
        side, y0, y1 = town
        for k in range(int((y1 - y0) / 45)):
            x = W - 5 if side > 0 else 7
            objects.append(("house", x, y0 + 20 + k * 45, k % 3, 64))
    # far from the camera first doesn't matter; sort by y to keep the files readable
    objects.sort(key=lambda o: (o[2], o[1]))

    for s, pts in shapes:
        lines.append("poly %s %s" % (s, pts_text(pts)))
    lines.append("")
    for t, x, y, v, sc in objects:
        x, y = clampxy((x, y), W, L)
        lines.append("object %s %s %s %d %d" % (t, fmt(x), fmt(y), v, sc))
    head = ["# Auld Kirk Links, hole %d. Made by tools/mkauldkirk.py; edit that and run it again." % n]
    (out / ("hole-%02d.txt" % n)).write_text("\n".join(head + lines) + "\n")
    return par, yards


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "courses/auldkirk"
    out.mkdir(parents=True, exist_ok=True)
    (out / "course.txt").write_text(
        "# Auld Kirk Links, an old links out along the shore and back.\n"
        "# Made by tools/mkauldkirk.py.\n"
        "name Auld Kirk\n" + "".join("tee %s\n" % t for t in TEES) +
        "holes %d\n" % len(HOLES) +
        "scenery 1         # the links backdrop: sea, hills and the kirk\n")
    total, pars = 0, 0
    for n, d in enumerate(HOLES, 1):
        par, yards = make_hole(n, d, out)
        total += yards
        pars += par
        print("%2d %-16s par %d %3d yd" % (n, d["name"], par, yards))
    print("par %d, %d yd" % (pars, total))


if __name__ == "__main__":
    main()
