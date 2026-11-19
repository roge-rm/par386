#!/usr/bin/env python3
"""Writes courses/sakurabay/: course.txt, hole-NN.txt and hole-NN.png for
Sakura Bay, a Japanese seaside parkland course on the hills above a bay,
with a snow-capped volcano across the water.

Each hole is laid out below from a centre line, the green, bunkers, the
ground's height along the line, the sea and ponds; shapes, objects and the
heightmap come from that with the hole's own random numbers, so running it
again gives the same files. Edit the HOLES table and run it again rather
than editing the output.

The ground follows the heights given along the centre line, with wooded
banks rising either side of the fairway, so the holes run between lines of
cherry, maple and black pine. Where the sea comes in the ground drops to it
over a rocky bank planted with pines; ponds sit in hollows. Bunkers are pits
dug into the heightmap: each one is centred on a heightmap point, which is
lowered, so the sand lies in a bowl with grass faces above it.

The sea and ponds in the table:
  ('coast', pts, corners): the shore walked with the sea on the left, as in
      tools/mkcarrig.py.
  pond (cx, cy, rx, ry, turn): a pond of fresh water 1.5 yards below the
      ground around its middle.

Usage: mksakura.py [COURSEDIR]
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkauldkirk import walk, length, clampxy, fmt, pts_text, ribbon, ellipse_pts, inside, vnoise   # noqa: E402
from mkcarrig import sea_shapes, sea_dist, FACE   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CELL = 8                  # yards between heightmap points
STEP = 1 / 8              # yards per grey step, 31.9 yards from black to white
FINE = 2                  # yards between points of the working height grid
EDGE = 2.0                # height at the top of the bank down to the sea
SHORE = 40                # yards over which the land comes down to EDGE
BANK = 5.0                # how far the wooded banks rise beside the fairways
PIT_FAIRWAY = 1.3         # depth of a fairway bunker's lowest point, yards
PIT_GREEN = 1.7           # and a greenside one
FACE_UP = 1.0             # how far the grass face behind a fairway bunker rises

# Each hole: name, par, width, centre line from the back tee to the middle of
# the green, the ground's height at each point of it (yards above the sea),
# fairway half widths along it, the green (half sizes and turn in degrees),
# bunkers (x, y, r; moved to the nearest heightmap point), the sea, ponds,
# how far the other tees are ahead (front, forward) and extras: the tree mix
# (cherry, maple, pine), a raised tee (yards), extra length past the green,
# tea houses, the pagoda, cranes, clumps of trees, out of bounds on the left
# or right past a bamboo fence.
HOLES = [
    # 1: downhill off the first tee between cherry trees, straight at the volcano
    dict(name="Hanami", par=4, W=220, line=[(110, 14), (106, 250), (116, 400)], rise=[14, 9, 11],
         fw=(36, 30), green=(17, 16, 0), bunkers=[(80, 248, 6), (146, 264, 6), (92, 392, 6), (140, 408, 5)],
         mix=(0.65, 0.1, 0.25), houses=[(30, 30)], raised=1),
    # 2: the sea down the right, black pines along the shore
    dict(name="Black Pine", par=4, W=230, line=[(90, 14), (100, 240), (120, 420)], rise=[9, 6, 7],
         fw=(34, 28), green=(16, 17, 10), bunkers=[(70, 232, 6), (136, 256, 6), (98, 410, 6), (142, 436, 5)],
         sea=[('coast', [(196, 999), (190, 380), (178, 260), (186, 140), (206, 0)], [(230, 0), (230, 999)])],
         mix=(0.4, 0.2, 0.4)),
    # 3: over the crane pond to a green in the trees
    dict(name="Crane Pond", par=3, W=200, line=[(100, 14), (104, 176)], rise=[12, 10],
         fw=(0, 0), green=(16, 15, 0), bunkers=[(76, 170, 5), (128, 186, 5)],
         ponds=[(98, 100, 34, 20, 5)], ahead=(14, 30), cranes=[(140, 92), (60, 108)], raised=1),
    # 4: the long one along the bay, the sea all down the left
    dict(name="Long Shore", par=5, W=240, line=[(150, 14), (140, 270), (122, 430), (112, 550)], rise=[10, 7, 6, 6],
         fw=(36, 32, 28), green=(18, 18, 0), bunkers=[(178, 262, 6), (110, 296, 6), (152, 450, 6), (90, 540, 6),
                                                     (132, 568, 5)],
         sea=[('coast', [(70, 0), (66, 160), (80, 300), (66, 420), (60, 999)], [(0, 999), (0, 0)])],
         mix=(0.35, 0.15, 0.5)),
    # 5: uphill to the temple green, a tea house beside it
    dict(name="Temple Steps", par=4, W=210, line=[(100, 14), (108, 240), (100, 384)], rise=[6, 10, 16],
         fw=(34, 28), green=(16, 15, 0), bunkers=[(74, 248, 6), (134, 232, 6), (80, 376, 6), (122, 366, 5)],
         houses=[(36, 380), (172, 400)], mix=(0.4, 0.4, 0.2)),
    # 6: a par 3 from the hilltop down to a green ringed with sand
    dict(name="Lantern Hill", par=3, W=200, line=[(100, 14), (96, 188)], rise=[21, 9],
         fw=(0, 0), green=(17, 16, 0), bunkers=[(72, 184, 6), (122, 178, 5), (98, 212, 5)],
         ahead=(16, 34), raised=2, mix=(0.5, 0.3, 0.2)),
    # 7: bending left round a stand of maples
    dict(name="Maple Bend", par=4, W=240, line=[(170, 14), (160, 240), (90, 384)], rise=[12, 9, 10],
         fw=(34, 28), green=(16, 17, -15), bunkers=[(130, 248, 6), (196, 262, 6), (72, 376, 6), (108, 398, 5)],
         clumps=[(112, 196, 1), (98, 228, 1), (124, 168, 1), (86, 200, 1)], mix=(0.2, 0.6, 0.2)),
    # 8: out along the clifftop, the sea right and the volcano ahead
    dict(name="Sea Wind", par=4, W=230, line=[(80, 14), (92, 260), (112, 424)], rise=[9, 6, 5],
         fw=(36, 30), green=(16, 16, 0), bunkers=[(60, 252, 6), (124, 274, 6), (90, 416, 6), (132, 442, 5)],
         sea=[('coast', [(200, 999), (178, 420), (172, 300), (186, 160), (206, 0)], [(230, 0), (230, 999)])],
         mix=(0.3, 0.2, 0.5)),
    # 9: uphill home to the pagoda behind the green
    dict(name="Pagoda", par=5, W=230, line=[(110, 14), (120, 270), (104, 420), (112, 526)], rise=[8, 10, 12, 15],
         fw=(38, 34, 30), green=(18, 17, 0), bunkers=[(84, 262, 6), (150, 284, 6), (130, 440, 6), (88, 518, 6),
                                                     (136, 540, 5)],
         pagoda=(150, 584), extra=40, houses=[(60, 576)], mix=(0.6, 0.2, 0.2)),
    # 10: through the plum gate, a bamboo fence and out of bounds down the right
    dict(name="Plum Gate", par=4, W=220, line=[(100, 14), (110, 260), (114, 414)], rise=[16, 11, 12],
         fw=(36, 30), green=(17, 16, 0), bunkers=[(80, 250, 6), (140, 270, 6), (96, 406, 6), (134, 398, 5)],
         oob_right=196, mix=(0.6, 0.2, 0.2)),
    # 11: off the ridge, a long drop to the fairway
    dict(name="Ridge Path", par=4, W=230, line=[(120, 14), (116, 250), (96, 428)], rise=[23, 10, 8],
         fw=(36, 30), green=(17, 16, 0), bunkers=[(90, 264, 6), (152, 240, 6), (70, 424, 6), (116, 448, 5)],
         raised=2, mix=(0.3, 0.3, 0.4)),
    # 12: a short par 3 over the koi pond
    dict(name="Koi Pond", par=3, W=200, line=[(96, 14), (100, 158)], rise=[10, 9],
         fw=(0, 0), green=(15, 15, 0), bunkers=[(74, 152, 5), (104, 182, 5)],
         ponds=[(120, 120, 32, 16, -10)], ahead=(12, 26), cranes=[(158, 118)], houses=[(170, 170)]),
    # 13: the sea down the right all the way, the green out by the shore
    dict(name="Tide Line", par=5, W=240, line=[(80, 14), (96, 270), (120, 430), (128, 534)], rise=[8, 6, 5, 6],
         fw=(36, 32, 28), green=(18, 17, 10), bunkers=[(70, 262, 6), (130, 286, 6), (152, 450, 6), (106, 526, 6),
                                                     (150, 546, 5)],
         sea=[('coast', [(220, 999), (198, 480), (186, 360), (196, 220), (214, 0)], [(240, 0), (240, 999)])],
         mix=(0.3, 0.2, 0.5)),
    # 14: a long par 4 climbing straight at the volcano
    dict(name="Fire Mountain", par=4, W=220, line=[(110, 14), (112, 260), (110, 440)], rise=[7, 10, 13],
         fw=(36, 30), green=(17, 17, 0), bunkers=[(84, 264, 6), (146, 252, 6), (86, 432, 6), (134, 428, 5),
                                                  (110, 464, 5)],
         mix=(0.5, 0.2, 0.3)),
    # 15: bending right through the bamboo, out of bounds down the left
    dict(name="Bamboo Grove", par=4, W=220, line=[(76, 14), (88, 230), (150, 372)], rise=[14, 12, 11],
         fw=(34, 28), green=(16, 17, 15), bunkers=[(112, 240, 6), (54, 254, 6), (130, 368, 6), (166, 384, 5)],
         oob_left=18, clumps=[(134, 196, 2), (124, 172, 2), (148, 214, 2)], mix=(0.3, 0.3, 0.4)),
    # 16: a par 3 along the bay, the sea close left of the green
    dict(name="Moon Bay", par=3, W=210, line=[(140, 14), (110, 180)], rise=[9, 5],
         fw=(0, 0), green=(16, 16, -10), bunkers=[(140, 174, 5), (110, 202, 5)],
         sea=[('coast', [(40, 0), (56, 100), (66, 170), (56, 999)], [(0, 999), (0, 0)])],
         ahead=(14, 30), raised=2, extra=20, mix=(0.3, 0.1, 0.6)),
    # 17: a short par 4 through a field of deep bunkers
    dict(name="Deep Sand", par=4, W=230, line=[(110, 14), (116, 200), (106, 330)], rise=[10, 12, 11],
         fw=(32, 26), green=(16, 15, 0), bunkers=[(80, 200, 6), (146, 184, 6), (112, 240, 6), (78, 236, 6),
                                                  (86, 320, 6), (130, 322, 5), (108, 352, 5)],
         mix=(0.4, 0.4, 0.2)),
    # 18: home past a pond to the green under the clubhouse
    dict(name="Last Blossom", par=5, W=230, line=[(110, 14), (120, 270), (100, 420), (108, 516)], rise=[12, 9, 8, 10],
         fw=(38, 34, 30), green=(18, 17, 0), bunkers=[(86, 262, 6), (150, 276, 6), (80, 504, 6), (116, 540, 5)],
         ponds=[(156, 466, 26, 20, 10)], houses=[(70, 582), (150, 586)], extra=40, cranes=[(186, 470)],
         mix=(0.75, 0.1, 0.15)),
]

TEES = ["Blue", "White", "Red"]
# Each hole's map turned against the course in degrees, which moves the
# backdrop (tools/mkobj.py sakura_backdrop). The volcano is at 60 across the
# strip and a hole's view centres on 120 minus its turn, so turns near 60
# face it: 1, 2, 4, 8, 13, 14 and 18.
TURNS = [60, 70, 150, 40, 170, 100, 120, 55, 160, 100, 30, 140, 75, 60, 175, 90, 130, 65]
PINS_ALONG = [(-0.45, -0.3), (0.4, -0.35), (0.35, 0.45), (-0.4, 0.4)]


def line_param(line, xs, ys):
    """Yards along the line to the nearest point on it, and the distance off it."""
    best = np.full(xs.shape, 1e9)
    s = np.zeros(xs.shape)
    acc = 0.0
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        dx, dy = bx - ax, by - ay
        m = math.hypot(dx, dy)
        t = np.clip(((xs - ax) * dx + (ys - ay) * dy) / (m * m), 0, 1)
        d = np.hypot(xs - (ax + t * dx), ys - (ay + t * dy))
        take = d < best
        best = np.where(take, d, best)
        s = np.where(take, acc + t * m, s)
        acc += m
    return s, best


def smoothstep(a):
    a = np.clip(a, 0, 1)
    return a * a * (3 - 2 * a)


def make_hole(n, d, out):
    rng = np.random.default_rng(5000 + n)
    W, par, line = d["W"], d["par"], d["line"]
    gx, gy = line[-1]
    grx, gry, gturn = d["green"]
    L = int(gy + gry + 40 + d.get("extra", 0))
    for hx, hy in d.get("houses", []):
        L = max(L, int(hy + 14))
    if d.get("pagoda"):
        L = max(L, int(d["pagoda"][1] + 16))
    L = min(L, 640)
    seas = sea_shapes(d.get("sea", []), W, L, rng)
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
    ahead = d.get("ahead", (22, 55) if par > 3 else (14, 30))
    tee_pts = []
    for k, a in enumerate((0,) + tuple(ahead)):
        (x, y), (dx, dy) = walk(line, a)
        side = (0, 9, -10)[k] * (1 if n % 2 else -1)
        x, y = x + dy * side, y - dx * side
        tee_pts.append((x, y))
        lines.append("tee %s %s %s %d" % (TEES[k], fmt(x), fmt(y), yards if k == 0 else round(yards - a)))
    lines.append("")

    t = math.radians(gturn)
    pin_pts = []
    for px, py in PINS_ALONG:
        ox, oy = px * grx * 1.2, py * gry * 1.2
        pin_pts.append((gx + ox * math.cos(t) - oy * math.sin(t), gy + ox * math.sin(t) + oy * math.cos(t)))
        lines.append("pin %s %s" % (fmt(pin_pts[-1][0]), fmt(pin_pts[-1][1])))
    lines.append("")

    # Bunkers on the nearest heightmap point, so each sits in its own pit.
    bunkers = []
    for bx, by, r in d["bunkers"]:
        near = math.hypot(bx - gx, by - gy) < grx + gry + 12
        if near:
            # greenside: on the same side of the green, the heightmap point
            # nearest to just outside the fringe so the green stays whole
            a = math.atan2(by - gy, bx - gx) - math.radians(gturn)
            edge = 1 / math.hypot(math.cos(a) / grx, math.sin(a) / gry)
            r *= 1.25
            want = edge + 3.5 + r * 0.6
            a += math.radians(gturn)
            tx, ty = gx + math.cos(a) * want, gy + math.sin(a) * want
            best = None
            for cx in (math.floor(tx / CELL) * CELL, math.ceil(tx / CELL) * CELL):
                for cy in (math.floor(ty / CELL) * CELL, math.ceil(ty / CELL) * CELL):
                    if math.hypot(cx - gx, cy - gy) < edge + 3.5 + r * 0.3:
                        continue
                    k_ = math.hypot(cx - tx, cy - ty)
                    if best is None or k_ < best[0]:
                        best = (k_, cx, cy)
            bx, by = (best[1], best[2]) if best else (round(tx / CELL) * CELL, round(ty / CELL) * CELL)
        else:
            bx, by = round(bx / CELL) * CELL, round(by / CELL) * CELL
            r *= 1.3
        # the face: toward the green's middle for a greenside bunker, else on along the hole
        fx_, fy_ = (gx - bx, gy - by) if near else (0, 1)
        m = math.hypot(fx_, fy_) or 1
        bunkers.append((bx, by, r, near, fx_ / m, fy_ / m))

    # First cut and fairway, ponds, the bank down to the sea and the sea.
    tee_gap = 45 if par > 3 else 0
    green_front = length(line) - gry - 4
    if par > 3:
        hw = list(d["fw"])
        shapes.append(("first-cut", ribbon(line, tee_gap - 6, green_front + 6, hw, 5, rng, W, L)))
        shapes.append(("fairway", ribbon(line, tee_gap, green_front + 2, hw, 0, rng, W, L)))
    else:
        shapes.append(("first-cut", ellipse_pts(gx, gy - gry - 6, grx + 4, 12, gturn, 12, W, L)))
    ponds = []
    for cx, cy, rx, ry, turn in d.get("ponds", []):
        ponds.append((cx, cy, ellipse_pts(cx, cy, rx, ry, turn, 22, W, L, rng, 0.08)))
    for _, _, pts in ponds:
        shapes.append(("water", pts))
    for water, face, _ in seas:
        shapes.append(("deep-rough", face))
    for water, face, _ in seas:
        shapes.append(("water", water))

    shapes.append(("fringe", ellipse_pts(gx, gy, grx + 3.5, gry + 3.5, gturn, 20, W, L)))
    shapes.append(("green", ellipse_pts(gx, gy, grx, gry, gturn, 20, W, L)))
    for bx, by, r, near, ux, uy in bunkers:
        # the sand sits up the far face so it shows from the front; wider across
        cx, cy = bx + ux * r * 0.35, by + uy * r * 0.35
        turn = math.degrees(math.atan2(uy, ux)) + 90
        pts = ellipse_pts(cx, cy, r * rng.uniform(1.1, 1.35), r * 0.85, turn, 12, W, L, rng, 0.1)
        if any(inside(pts, np.array(px), np.array(py)) for px, py in pin_pts):
            raise SystemExit("hole %d: the bunker at %d, %d covers a pin" % (n, bx, by))
        shapes.append(("bunker", pts))
    for k, (x, y) in enumerate(tee_pts):
        hw_ = 7 if k == 0 else 5
        shapes.append(("tee", [clampxy((x - hw_, y - 6), W, L), clampxy((x + hw_, y - 6), W, L),
                               clampxy((x + hw_, y + 6), W, L), clampxy((x - hw_, y + 6), W, L)]))
    if d.get("oob_right"):
        x0 = d["oob_right"]
        shapes.append(("oob", [(x0, 0), (W, 0), (W, L), (x0, L)]))
    if d.get("oob_left"):
        x0 = d["oob_left"]
        shapes.append(("oob", [(0, 0), (x0, 0), (x0, L), (0, L)]))

    # Heights on a fine grid: the heights along the line, wooded banks
    # rising away from the fairway, humps, calmer on fairways; down to the
    # sea over a bank, ponds in hollows, the green a tilted plane, tees level.
    fw_, fh_ = W // FINE + 1, L // FINE + 1
    ys, xs = np.mgrid[0:fh_, 0:fw_] * float(FINE)
    cum = np.concatenate([[0], np.cumsum([math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(line, line[1:])])])
    s, lat = line_param(line, xs, ys)
    base = np.interp(s, cum, d["rise"])
    band = (max(d["fw"]) + 16) if par > 3 else 32
    dist, wet = sea_dist(seas, xs, ys)
    inland = smoothstep((dist - 30) / 50)
    bank = smoothstep((lat - band) / 60) * BANK * inland
    hum = vnoise((fh_, fw_), 10.0, rng) * 1.2 + vnoise((fh_, fw_), 22.0, rng) * 1.8
    fw_mask = np.zeros(xs.shape, dtype=bool)
    for name, pts in shapes:
        if name in ("fairway", "first-cut", "tee"):
            fw_mask |= inside(pts, xs, ys)
    smooth = base + bank
    z = smooth + hum * np.where(fw_mask, 0.4, 1.0)

    def shore(a):
        s_ = smoothstep((dist - FACE) / SHORE)
        a = EDGE + (a - EDGE) * s_
        return a * smoothstep((dist - 2) / (FACE - 4))
    if seas:
        z = shore(z)
        smooth = shore(smooth)
    pond_wet = np.zeros(xs.shape, dtype=bool)
    pdist = np.full(xs.shape, 999.0)
    for cx, cy, pts in ponds:
        lvl = float(np.interp(line_param(line, np.array([cx]), np.array([cy]))[0], cum, d["rise"])[0]) - 1.5
        w_ = inside(pts, xs, ys)
        pond_wet |= w_
        dd = np.min([np.hypot(xs - px, ys - py) for px, py in pts], axis=0)
        dd = np.where(w_, 0, dd)
        pdist = np.minimum(pdist, dd)
        f = smoothstep(dd / 16)
        z = np.where(w_, lvl - 0.2, (lvl + 0.3) * (1 - f) + z * f)
        smooth = np.where(w_, lvl - 0.2, (lvl + 0.3) * (1 - f) + smooth * f)
    # the green: a plane through its middle's height, tilting up to the back
    gd = np.hypot((xs - gx) / (grx + 14), (ys - gy) / (gry + 14))
    gw = np.clip((1.3 - gd) / 0.5, 0, 1)
    g0 = smooth[int(gy / FINE), int(gx / FINE)]
    gz = g0 + 0.3 + (ys - gy) * 0.022 + (xs - gx) * 0.01 * rng.choice([-1, 1])
    z = z * (1 - gw) + gz * gw
    for k, (x, y) in enumerate(tee_pts):
        tw = np.clip(1.6 - np.hypot(xs - x, ys - y) / 14, 0, 1)
        lift = d.get("raised", 0) if k == 0 else d.get("raised", 0) * 0.5
        z = z * (1 - tw) + (smooth + 0.4 + lift) * tw
    z = np.where(wet, -0.2, z)
    # sample every CELL yards; next to water the lowest point within 4
    # yards, so the water stays flat
    hw_, hh_ = W // CELL + 2, L // CELL + 2
    zp = np.pad(z, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    dp = np.pad(np.minimum(dist - FACE, pdist), ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    hm = np.zeros((hh_, hw_))
    k = CELL // FINE
    for j in range(hh_):
        for i in range(hw_):
            jj, ii = j * k + 2, i * k + 2
            near = dp[jj - 2:jj + 3, ii - 2:ii + 3].min() < 0.5
            hm[j, i] = zp[jj - 2:jj + 3, ii - 2:ii + 3].min() if near else zp[jj, ii]
    # the bunker pits: the point under each bunker goes down below the
    # lowest of its neighbours, and the one on its far side from the tee or
    # the green's middle comes up a little for a grass face
    for bx, by, r, near, ux, uy in bunkers:
        i, j = bx // CELL, by // CELL
        nb = [hm[j + dj, i + di] for dj in (-1, 0, 1) for di in (-1, 0, 1)
              if (di or dj) and 0 <= j + dj < hh_ and 0 <= i + di < hw_]
        hm[j, i] = max(0.0, min(nb) - (PIT_GREEN if near else PIT_FAIRWAY))
        fi, fj = i + round(ux), j + round(uy)
        px_, py_ = fi * CELL, fj * CELL
        on_green = math.hypot((px_ - gx) / (grx + 4), (py_ - gy) / (gry + 4)) < 1
        if 0 <= fj < hh_ and 0 <= fi < hw_ and not on_green:
            hm[fj, fi] += FACE_UP if not near else 0.6
    grey = np.clip(np.round(hm / STEP), 0, 255).astype(np.uint8)
    Image.fromarray(grey[::-1]).save(out / ("hole-%02d.png" % n))

    def solid(x, y):
        for s_, p in shapes:
            if s_ in ("first-cut", "fairway", "green", "fringe", "tee", "bunker", "water", "oob"):
                if inside(p, np.array(x), np.array(y)):
                    return True
        return False

    def sea_at(x, y):
        dd, _ = sea_dist(seas, np.array([x]), np.array([y]))
        return dd[0]

    def off_line(x, y):
        return float(line_param(line, np.array([x]), np.array([y]))[1][0])

    def pond_at(x, y):
        return min([999.0] + [math.hypot(x - px, y - py) for _, _, pts in ponds for px, py in pts])

    mix = np.array(d.get("mix", (0.45, 0.2, 0.35)))
    mix = mix / mix.sum()

    def tree(x, y, shore_side=False, big=1.0):
        kind = 2 if shore_side else rng.choice(3, p=mix)
        sc = int(rng.integers(68, 96) * big)
        if kind == 2:
            objects.append(("pine", x, y, 0, min(sc + 8, 127)))
        else:
            objects.append(("tree", x, y, int(kind), min(sc, 127)))

    def free(x, y, clear):
        return (4 < x < W - 4 and 4 < y < L - 4 and not solid(x, y) and off_line(x, y) > clear
                and sea_at(x, y) > FACE + 3 and pond_at(x, y) > 6)

    # Rocks along the shore and pines on the bank above it.
    for water, face, edges in seas:
        for p, q in edges:
            seg = math.hypot(q[0] - p[0], q[1] - p[1])
            nx, ny = (q[1] - p[1]) / seg, -(q[0] - p[0]) / seg   # to the land
            at = rng.uniform(0, 10)
            while at < seg:
                tt = at / seg
                at += rng.uniform(14, 28)
                x = p[0] + (q[0] - p[0]) * tt + nx * rng.uniform(0.5, 4)
                y = p[1] + (q[1] - p[1]) * tt + ny * rng.uniform(0.5, 4)
                if 0 < x < W and 0 < y < L and sea_at(x, y) > 0.3 and rng.random() < 0.5:
                    objects.append(("rock", x, y, 0, int(rng.integers(80, 150))))
                x2 = p[0] + (q[0] - p[0]) * tt + nx * rng.uniform(14, 22)
                y2 = p[1] + (q[1] - p[1]) * tt + ny * rng.uniform(14, 22)
                if 4 < x2 < W - 4 and 4 < y2 < L - 4 and not solid(x2, y2) and off_line(x2, y2) > band - 6 \
                        and rng.random() < 0.6:
                    tree(x2, y2, shore_side=True, big=1.1)

    # Lines of trees down both sides of the hole, a few further back, and
    # more behind the green.
    clear = (max(d["fw"]) + 8) if par > 3 else 28
    total = length(line)
    for side in (-1, 1):
        at = rng.uniform(4, 16)
        while at < total + 10:
            (lx, ly), (dx, dy) = walk(line, min(at, total))
            nx, ny = dy * side, -dx * side
            off = clear + rng.uniform(2, 14)
            x, y = lx + nx * off, ly + ny * off
            if free(x, y, clear):
                tree(x, y, shore_side=sea_at(x, y) < 50)
            if rng.random() < 0.3:
                off2 = off + rng.uniform(18, 34)
                x, y = lx + nx * off2, ly + ny * off2
                if free(x, y, clear):
                    tree(x, y, shore_side=sea_at(x, y) < 50, big=1.15)
            at += rng.uniform(20, 32)
    for kk in range(7):
        x = gx + rng.uniform(-1, 1) * (grx + 40)
        y = gy + gry + rng.uniform(16, 40)
        if free(x, y, 12):
            tree(x, y, shore_side=sea_at(x, y) < 50, big=1.1)
    for x, y, v in d.get("clumps", []):
        for kk in range(4):
            xx, yy = x + rng.normal(0, 6), y + rng.normal(0, 6)
            if free(xx, yy, 18):
                objects.append(("tree" if v < 2 else "pine", xx, yy, v if v < 2 else 0, int(rng.integers(60, 80))))
    # Azaleas by the tees and the green, rocks and cranes by the ponds.
    for x, y in tee_pts[:1]:
        for kk in range(3):
            xx, yy = x + rng.choice([-1, 1]) * rng.uniform(14, 24), y + rng.uniform(-6, 10)
            if free(xx, yy, 10):
                objects.append(("bush", xx, yy, 0, int(rng.integers(56, 80))))
    for kk in range(4):
        a = rng.uniform(0, 2 * math.pi)
        xx, yy = gx + math.cos(a) * (grx + 12), gy + math.sin(a) * (gry + 12)
        if a > 0.3 and a < math.pi - 0.3 and free(xx, yy, 10):
            objects.append(("bush", xx, yy, 0, int(rng.integers(56, 80))))
    for cx, cy, pts in ponds:
        for kk, (px, py) in enumerate(pts[::4]):
            ox, oy = px - cx, py - cy
            m = math.hypot(ox, oy) or 1
            xx, yy = px + ox / m * 2, py + oy / m * 2
            if 0 < xx < W and 0 < yy < L:
                objects.append(("rock", xx, yy, 0, int(rng.integers(60, 100))))
    for x, y in d.get("cranes", []):
        objects.append(("animal", x, y, 0, 64))
    for x, y in d.get("houses", []):
        objects.append(("house", x, y, 0, 64))
    if d.get("pagoda"):
        x, y = d["pagoda"]
        objects.append(("landmark", x, y, 0, 64))
    for key, x0 in (("oob_right", d.get("oob_right")), ("oob_left", d.get("oob_left"))):
        if x0:
            xf = x0 - 2 if key == "oob_right" else x0 + 2
            for k2 in range(int((L - 20) / 6)):
                objects.append(("wall", xf, 10 + k2 * 6 + 3, 0, 64))
    objects.sort(key=lambda o: (o[2], o[1]))

    for s_, pts in shapes:
        lines.append("poly %s %s" % (s_, pts_text(pts)))
    lines.append("")
    for typ, x, y, v, sc in objects:
        x, y = clampxy((x, y), W, L)
        lines.append("object %s %s %s %d %d" % (typ, fmt(x), fmt(y), v, sc))
    head = ["# Sakura Bay, hole %d. Made by tools/mksakura.py; edit that and run it again." % n]
    (out / ("hole-%02d.txt" % n)).write_text("\n".join(head + lines) + "\n")
    return par, yards, [round(yards - a) for a in ahead], len(objects)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "courses/sakurabay"
    out.mkdir(parents=True, exist_ok=True)
    (out / "course.txt").write_text(
        "# Sakura Bay, a Japanese seaside parkland course on the hills above a bay.\n"
        "# Made by tools/mksakura.py.\n"
        "name Sakura Bay\n" + "".join("tee %s\n" % t for t in TEES) +
        "holes %d\n" % len(HOLES) +
        "scenery 3         # the bay backdrop: the sea, the volcano and wooded hills\n"
        "wind 8            # sheltered by the trees, lighter than the usual 10\n")
    total, pars, tot_t = 0, 0, [0, 0]
    for n, d in enumerate(HOLES, 1):
        par, yards, others, nobj = make_hole(n, d, out)
        total += yards
        pars += par
        tot_t = [a + b for a, b in zip(tot_t, others)]
        print("%2d %-14s par %d %3d yd (%s) %d objects" % (n, d["name"], par, yards,
                                                           ", ".join(str(o) for o in others), nobj))
    print("par %d, %d yd (%s)" % (pars, total, ", ".join(str(o) for o in tot_t)))


if __name__ == "__main__":
    main()
