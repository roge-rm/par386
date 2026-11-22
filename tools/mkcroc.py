#!/usr/bin/env python3
"""Writes courses/crocodilebend/: course.txt, hole-NN.txt and hole-NN.png for
Crocodile Bend, a South African bush course beside a river with crocodiles
and hippos, thorn trees and bush.

Each hole is laid out below from a centre line, the green, its bunkers, the
river where it comes into play and the ground's height along the line;
shapes, objects and the heightmap come from that with the hole's own random
numbers, so running it again gives the same files. Edit the HOLES table and
run it again rather than editing the output.

Outside a band of rough the ground is bush (deep rough) with acacias,
marulas, thorn bushes and dry grass. The river is a band of water along a
centre line given per hole (it can run off the hole's edges, where the water
carries on), with reeds and the odd ilala palm on its banks, crocodiles
basking on the banks and hippos in the water. The ground falls to the
river, and the water is level.

Greenside bunkers in the table are (angle, radius): the angle round the
green in degrees (0 right, 90 behind, -90 in front, turned with the green),
set just off the fringe. Fairway bunkers are (x, y, radius).

Usage: mkcroc.py [COURSEDIR]
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkauldkirk import walk, length, clampxy, fmt, pts_text, ribbon, ellipse_pts, inside, vnoise   # noqa: E402
from mksakura import line_param, smoothstep   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CELL = 8                  # yards between heightmap points
STEP = 1 / 8              # yards per grey step
FINE = 2                  # yards between points of the working height grid
BANK = 2.0                # how far the bush rises beside the fairways
PIT = 0.9                 # how far heightmap points under sand go down, yards
GREEN_KEEP = 7            # heightmap points this far out from a green stay up
RIVER_DROP = 1.2          # the water's level below the lowest ground along the line, yards
RIVER_SLOPE = 24          # yards over which the ground falls to the water
ANIMAL_CLEAR = 6          # yards an animal keeps from fairway, first cut, green, fringe, tee and sand

# Each hole: name, par, width, centre line from the back tee to the middle of
# the green, the ground's height at each point of it, fairway half widths
# along it, the green (half sizes and turn in degrees), greenside bunkers
# (gb), fairway bunkers (fb), rivers as (centre line, half width), where
# the fairway starts if a river crosses before it (fw_from), how far the
# other tees are ahead (front, forward) and extras: a raised tee (yards),
# extra length past the green, rondavels (huts), the baobab, granite rocks
# (koppie), crocodiles and hippos per river, hippos grazing in the bush.
HOLES = [
    # 1: a gentle start between acacias
    dict(name="Mopane Gate", par=4, W=220, line=[(110, 14), (106, 256), (114, 414)], rise=[8, 7, 9],
         fw=(38, 32), green=(17, 15, 0), gb=[(-135, 8), (-45, 7), (110, 6)], fb=[(76, 240, 8)]),
    # 2: the long par 5 along the river bank, the water all down the left
    dict(name="Long Bank", par=5, W=240, line=[(150, 14), (146, 270), (134, 440), (140, 530)], rise=[7, 7, 6, 7],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-150, 8), (-30, 8), (120, 6)], fb=[(176, 262, 9), (164, 452, 8)],
         rivers=[([(64, -30), (60, 150), (70, 320), (60, 470), (72, 640)], 14)], crocs=3, hippos=1),
    # 3: a par 3 over the river
    dict(name="Weaver's Nest", par=3, W=200, line=[(100, 14), (104, 172)], rise=[8, 9],
         fw=(0, 0), green=(16, 14, 0), gb=[(-150, 7), (-30, 7), (90, 6)],
         rivers=[([(-30, 98), (40, 90), (110, 100), (160, 92), (230, 98)], 14)], crocs=2, hippos=1, raised=3),
    # 4: a straight par 4, hippos grazing in the bush on the right
    dict(name="Impala Flats", par=4, W=220, line=[(110, 14), (118, 262), (108, 432)], rise=[8, 9, 8],
         fw=(38, 30), green=(17, 16, 5), gb=[(-150, 8), (-40, 8), (80, 6)], fb=[(146, 258, 9), (82, 272, 8)],
         graze=[(196, 300), (204, 318)]),
    # 5: the drive carries the river
    dict(name="Sand River", par=4, W=220, line=[(100, 14), (108, 150), (118, 260), (112, 368)], rise=[9, 7, 9, 10],
         fw=(36, 34, 30), green=(16, 15, -10), gb=[(-120, 8), (-20, 7), (150, 6)], fb=[(150, 270, 8)],
         rivers=[([(-30, 90), (60, 106), (140, 124), (250, 116)], 13)], fw_from=148, crocs=2, hippos=1, raised=2.5),
    # 6: dogleg right round the bush
    dict(name="Termite Hill", par=4, W=230, line=[(90, 14), (98, 262), (146, 394)], rise=[8, 9, 10],
         fw=(36, 30), green=(16, 15, 10), gb=[(-160, 8), (-60, 7), (60, 6)], fb=[(132, 240, 10), (64, 262, 7)],
         rocks=[(176, 200), (182, 210)]),
    # 7: a par 5 with the river across in front of the green
    dict(name="Kingfisher", par=5, W=240, line=[(120, 14), (126, 280), (110, 450), (118, 548)], rise=[8, 8, 7, 8],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-30, 7), (100, 6), (-160, 7)], fb=[(160, 286, 9), (80, 420, 8)],
         rivers=[([(-30, 470), (60, 478), (140, 490), (270, 480)], 12)], crocs=2, hippos=1),
    # 8: the signature par 3, the green out on a bend of the river with
    # water left, behind and right
    dict(name="Hippo Pool", par=3, W=200, line=[(100, 14), (100, 168)], rise=[9, 8],
         fw=(0, 0), green=(15, 13, 0), gb=[(-125, 6)], extra=34,
         rivers=[([(-30, 114), (36, 124), (62, 144), (66, 174), (78, 198), (100, 206),
                   (122, 198), (134, 174), (138, 144), (164, 124), (230, 114)], 13)],
         crocs=3, hippos=3, raised=4),
    # 9: home to the rondavels behind the green
    dict(name="Rondavel", par=4, W=220, line=[(110, 14), (104, 262), (112, 438)], rise=[9, 8, 9],
         fw=(38, 32), green=(17, 15, 0), gb=[(-120, 8), (-20, 7), (160, 7)], fb=[(84, 248, 9), (148, 236, 8)],
         huts=[(78, 484), (142, 488)], extra=30),
    # 10: dogleg left to the baobab behind the green
    dict(name="Baobab", par=4, W=230, line=[(130, 14), (120, 240), (92, 390)], rise=[10, 9, 9],
         fw=(36, 30), green=(16, 15, -15), gb=[(-120, 8), (-10, 8), (130, 6)], fb=[(94, 232, 10), (158, 248, 7)],
         baobab=(112, 446), extra=30),
    # 11: a par 5 with the river along the right
    dict(name="Fever Tree", par=5, W=240, line=[(92, 14), (96, 270), (112, 440), (104, 524)], rise=[8, 7, 7, 8],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-140, 8), (-40, 8), (170, 6)], fb=[(60, 272, 9), (82, 430, 8)],
         rivers=[([(178, -30), (172, 160), (180, 330), (170, 480), (184, 640)], 14)], crocs=3, hippos=2),
    # 12: a short par 4 through thorn trees, sand at the corner
    dict(name="Thorn Veld", par=4, W=220, line=[(100, 14), (110, 252), (100, 378)], rise=[9, 8, 9],
         fw=(34, 28), green=(16, 14, 0), gb=[(-100, 9), (20, 7), (160, 6)], fb=[(74, 232, 9), (140, 254, 8)],
         graze=[(28, 320)]),
    # 13: the long par 3
    dict(name="Lowveld", par=3, W=210, line=[(110, 14), (112, 212)], rise=[10, 9],
         fw=(0, 0), green=(18, 16, 0), gb=[(-140, 9), (-40, 9), (150, 6)], ahead=(25, 55), raised=1),
    # 14: the river cuts across the drive on a slant, shorter on the left
    dict(name="The Drift", par=4, W=230, line=[(130, 14), (125, 140), (118, 290), (104, 420)], rise=[10, 7.5, 8.5, 9],
         fw=(36, 36, 30), green=(17, 15, 0), gb=[(-150, 8), (-40, 7), (100, 6)], fb=[(154, 300, 8)],
         rivers=[([(-30, 110), (80, 144), (160, 174), (260, 210)], 14)], fw_from=196, crocs=2, hippos=1, raised=2),
    # 15: uphill toward a koppie of granite on the left
    dict(name="Koppie", par=4, W=220, line=[(110, 14), (114, 260), (122, 414)], rise=[7, 10, 13],
         fw=(36, 30), green=(17, 15, 0), gb=[(-130, 8), (-40, 7), (90, 6)], fb=[(150, 244, 9)],
         rocks=[(36, 300), (26, 318), (46, 330), (32, 346), (54, 312), (22, 336), (44, 356)]),
    # 16: a short par 3 among ilala palms
    dict(name="Ilala", par=3, W=200, line=[(100, 14), (98, 158)], rise=[9, 8],
         fw=(0, 0), green=(14, 13, 0), gb=[(-120, 6), (-50, 6), (30, 5), (150, 5)], palms=8),
    # 17: along the bank to a green on the river's bend, water left and behind
    dict(name="Crocodile Bend", par=4, W=230, line=[(150, 14), (146, 260), (132, 400)], rise=[8, 7, 7],
         fw=(36, 30), green=(16, 15, 0), gb=[(-60, 7), (10, 6)], fb=[(184, 262, 9)], extra=28,
         rivers=[([(58, -30), (62, 150), (72, 300), (90, 390), (126, 438), (180, 444), (260, 436)], 14)],
         crocs=3, hippos=2),
    # 18: home, the clubhouse rondavels behind the green
    dict(name="Sundowner", par=5, W=240, line=[(120, 14), (110, 270), (132, 440), (124, 530)], rise=[9, 8, 9, 10],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-130, 8), (-40, 8), (160, 6)], fb=[(80, 262, 9), (150, 286, 8), (100, 430, 8)],
         huts=[(90, 578), (152, 582), (122, 594)], extra=50),
]

TEES = ["Black", "Green", "Gold"]
# Each hole's map turned against the course in degrees, which moves the
# backdrop (tools/mkobj.py croc_backdrop).
TURNS = [30, 120, 210, 80, 160, 310, 50, 260, 170, 100, 20, 240, 140, 290, 70, 200, 330, 150]
PINS_ALONG = [(-0.45, -0.3), (0.4, -0.35), (0.35, 0.45), (-0.4, 0.4)]
SURF = {"rough": 0, "first-cut": 1, "fairway": 2, "deep-rough": 3, "green": 4, "fringe": 5,
        "bunker": 6, "water": 7, "path": 8, "oob": 9, "tee": 10}
PLAYED = (1, 2, 4, 5, 6, 10)     # first cut, fairway, green, fringe, bunker, tee: no animals near


def river_poly(line, hw, W, L, rng):
    """The water along a centre line, hw yards each side with a little wobble,
    cut to the hole (points past the edge are pulled onto it)."""
    total = length(line)
    n = min(30, max(3, int(total / 14) + 1))
    left, right = [], []
    for i in range(n):
        d = total * i / (n - 1)
        (x, y), (dx, dy) = walk(line, d)
        if i > 0 and i < n - 1:
            # the normal from the neighbours, so the bends stay smooth
            (xa, ya), _ = walk(line, total * (i - 1) / (n - 1))
            (xb, yb), _ = walk(line, total * (i + 1) / (n - 1))
            m = math.hypot(xb - xa, yb - ya) or 1
            dx, dy = (xb - xa) / m, (yb - ya) / m
        nx, ny = dy, -dx
        lw = hw * (1 + rng.uniform(-0.06, 0.06))
        rw = hw * (1 + rng.uniform(-0.06, 0.06))
        left.append(clampxy((x - nx * lw, y - ny * lw), W, L))
        right.append(clampxy((x + nx * rw, y + ny * rw), W, L))
    return left + right[::-1]


def make_hole(n, d, out):
    rng = np.random.default_rng(8000 + n)
    W, par, line = d["W"], d["par"], d["line"]
    gx, gy = line[-1]
    grx, gry, gturn = d["green"]
    L = int(gy + gry + 40 + d.get("extra", 0))
    for hx, hy in d.get("huts", []):
        L = max(L, int(hy + 14))
    if d.get("baobab"):
        L = max(L, int(d["baobab"][1] + 14))
    L = min(L, 640)
    lines = []
    shapes = []
    objects = []

    yards = round(length(line))
    lines.append("name %s" % d["name"])
    lines.append("par %d" % par)
    lines.append("size %d %d" % (W, L))
    lines.append("heightmap hole-%02d.png %d" % (n, CELL))
    lines.append("height 0 %s" % STEP)
    lines.append("base deep-rough")
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

    def on_green(x, y, m):
        """Inside the green grown by m yards."""
        ca, sa = math.cos(-t), math.sin(-t)
        rx, ry = (x - gx) * ca - (y - gy) * sa, (x - gx) * sa + (y - gy) * ca
        return math.hypot(rx / (grx + m), ry / (gry + m)) < 1

    pin_pts = []
    for px, py in PINS_ALONG:
        ox, oy = px * grx, py * gry
        pp = (gx + ox * math.cos(t) - oy * math.sin(t), gy + ox * math.sin(t) + oy * math.cos(t))
        assert on_green(pp[0], pp[1], -2.5), (n, pp)
        pin_pts.append(pp)
    lines += ["pin %s %s" % (fmt(x), fmt(y)) for x, y in pin_pts]
    lines.append("")

    # Rough round the fairway, the green and the tees; first cut, fairway.
    total = length(line)
    tee_gap = d.get("fw_from", 45) if par > 3 else 0
    green_front = total - gry - 4
    if par > 3:
        hw = list(d["fw"])
        shapes.append(("rough", ribbon(line, 0, total + gry + 12, hw, 16, rng, W, L)))
        shapes.append(("first-cut", ribbon(line, tee_gap - 6, green_front + 6, hw, 4, rng, W, L)))
        shapes.append(("fairway", ribbon(line, tee_gap, green_front + 2, hw, 0, rng, W, L)))
    else:
        shapes.append(("rough", ribbon(line, 0, total - gry - 10, [22], 0, rng, W, L, step=30)))
        shapes.append(("rough", ellipse_pts(gx, gy, grx + 14, gry + 14, gturn, 16, W, L, rng, 0.08)))
        shapes.append(("first-cut", ellipse_pts(gx, gy - gry - 6, grx + 4, 10, gturn, 12, W, L)))
    shapes.append(("rough", ellipse_pts(tee_pts[0][0], tee_pts[0][1] + 4, 22, 18, 0, 12, W, L, rng, 0.1)))

    # the river over the grass, then the sand, the fringe, the green and the tees
    rivers = d.get("rivers", [])
    for rl, rhw in rivers:
        shapes.append(("water", river_poly(rl, rhw, W, L, rng)))
    bunkers = []
    for ang, r in d.get("gb", []):
        a = math.radians(ang)
        edge = 1 / math.hypot(math.cos(a) / grx, math.sin(a) / gry)
        a += t
        ux, uy = math.cos(a), math.sin(a)
        want = edge + 3 + r * 0.7
        cx, cy = gx + ux * want, gy + uy * want
        turn = math.degrees(a) + 90
        bunkers.append((ellipse_pts(cx, cy, r * 1.3, r * 0.75, turn, 12, W, L, rng, 0.12), True))
    for bx, by, r in d.get("fb", []):
        bunkers.append((ellipse_pts(bx, by, r * 1.2, r * 0.7, rng.uniform(-20, 20), 12, W, L, rng, 0.15), False))
    shapes.append(("fringe", ellipse_pts(gx, gy, grx + 3, gry + 3, gturn, 16, W, L)))
    for pts, _ in bunkers:
        shapes.append(("bunker", pts))
    shapes.append(("green", ellipse_pts(gx, gy, grx, gry, gturn, 18, W, L)))
    for k, (x, y) in enumerate(tee_pts):
        hw_ = 7 if k == 0 else 5
        shapes.append(("tee", [clampxy((x - hw_, y - 6), W, L), clampxy((x + hw_, y - 6), W, L),
                               clampxy((x + hw_, y + 6), W, L), clampxy((x - hw_, y + 6), W, L)]))

    # the surface on the fine grid, later shapes winning as on the Palm
    fw_, fh_ = W // FINE + 1, L // FINE + 1
    ys, xs = np.mgrid[0:fh_, 0:fw_] * float(FINE)
    surf = np.full(xs.shape, SURF["deep-rough"], dtype=np.uint8)
    for s_, p in shapes:
        surf[inside(p, xs + 0.01, ys + 0.01)] = SURF[s_]
    wet = surf == SURF["water"]
    for k, (x, y) in enumerate(tee_pts):
        assert surf[int(round(y / FINE)), int(round(x / FINE))] == SURF["tee"], (n, k)
    for x, y in pin_pts:
        assert surf[int(round(y / FINE)), int(round(x / FINE))] == SURF["green"], (n, x, y)

    # Heights on a fine grid: the heights along the line, the bush rising a
    # little away from the fairway, low humps, calmer on fairways, the
    # ground falling to the river and the water level, the green a tilted
    # plane, tees level.
    cum = np.concatenate([[0], np.cumsum([math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(line, line[1:])])])
    s, lat = line_param(line, xs, ys)
    base = np.interp(s, cum, d["rise"])
    band = (max(d["fw"]) + 18) if par > 3 else 30
    bank = smoothstep((lat - band) / 50) * BANK
    hum = vnoise((fh_, fw_), 9.0, rng) * 0.9 + vnoise((fh_, fw_), 24.0, rng) * 1.4
    fw_mask = np.isin(surf, (SURF["fairway"], SURF["first-cut"], SURF["tee"]))
    smooth = base + bank
    z = smooth + hum * np.where(fw_mask, 0.45, 1.0)
    lvl = min(d["rise"]) - RIVER_DROP
    rdist = np.full(xs.shape, 999.0)
    for rl, rhw in rivers:
        rdist = np.minimum(rdist, np.maximum(0, line_param(rl, xs, ys)[1] - rhw))
    rdist = np.where(wet, 0, rdist)
    f = smoothstep(rdist / RIVER_SLOPE)
    z = (lvl + 0.4) * (1 - f) + z * f
    smooth = (lvl + 0.4) * (1 - f) + smooth * f
    gd = np.hypot((xs - gx) / (grx + 14), (ys - gy) / (gry + 14))
    gw = np.clip((1.3 - gd) / 0.5, 0, 1)
    g0 = max(smooth[int(gy / FINE), int(gx / FINE)], lvl + 1.0)
    gz = g0 + 0.4 + (ys - gy) * 0.02 + (xs - gx) * 0.01 * rng.choice([-1, 1])
    z = z * (1 - gw) + gz * gw
    for k, (x, y) in enumerate(tee_pts):
        tw = np.clip(1.6 - np.hypot(xs - x, ys - y) / 14, 0, 1)
        lift = d.get("raised", 0) if k == 0 else min(0.5, d.get("raised", 0) * 0.5)
        z = z * (1 - tw) + (smooth + 0.4 + lift) * tw
    z = np.where(wet, lvl, z)
    # sample every CELL yards; by the water the lowest point within 4 yards,
    # so the water stays level
    hw_, hh_ = W // CELL + 2, L // CELL + 2
    zp = np.pad(z, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    wp = np.pad(rdist, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    hm = np.zeros((hh_, hw_))
    k = CELL // FINE
    for j in range(hh_):
        for i in range(hw_):
            jj, ii = j * k + 2, i * k + 2
            near = wp[jj - 2:jj + 3, ii - 2:ii + 3].min() < 0.5
            hm[j, i] = zp[jj - 2:jj + 3, ii - 2:ii + 3].min() if near else zp[jj, ii]
    # the sand goes down a little, but not on or near the green
    gy_, gx_ = np.mgrid[0:hh_, 0:hw_] * float(CELL)
    for pts, near in bunkers:
        under = inside(pts, gx_, gy_)
        for j, i in zip(*np.nonzero(under)):
            if not on_green(i * CELL, j * CELL, GREEN_KEEP):
                hm[j, i] = max(0.0, hm[j, i] - PIT)
    grey = np.clip(np.round(hm / STEP), 0, 255).astype(np.uint8)
    Image.fromarray(grey[::-1]).save(out / ("hole-%02d.png" % n))

    def at(x, y):
        return surf[min(max(int(round(y / FINE)), 0), fh_ - 1), min(max(int(round(x / FINE)), 0), fw_ - 1)]

    def near_play(x, y, r):
        """Any fairway, first cut, green, fringe, sand or tee within r yards."""
        j0, j1 = max(0, int((y - r) / FINE)), min(fh_, int((y + r) / FINE) + 2)
        i0, i1 = max(0, int((x - r) / FINE)), min(fw_, int((x + r) / FINE) + 2)
        return bool(np.isin(surf[j0:j1, i0:i1], PLAYED).any())

    def off_line(x, y):
        return float(line_param(line, np.array([x]), np.array([y]))[1][0])

    def river_at(x, y):
        return min([999.0] + [max(0.0, float(line_param(rl, np.array([x]), np.array([y]))[1][0]) - rhw)
                              for rl, rhw in rivers])

    def free(x, y, clear, wet_ok=False):
        return (4 < x < W - 4 and 4 < y < L - 4 and at(x, y) in (SURF["deep-rough"],) and off_line(x, y) > clear
                and (wet_ok or river_at(x, y) > 5) and not near_play(x, y, 4))

    # Acacias and marulas in clumps down both sides, thorn bushes and dry
    # grass between.
    clear = (max(d["fw"]) + 16) if par > 3 else 24
    for side in (-1, 1):
        at_ = rng.uniform(0, 20)
        while at_ < total + 20:
            (lx, ly), (dx, dy) = walk(line, min(at_, total))
            nx, ny = dy * side, -dx * side
            off = clear + rng.uniform(6, 40)
            for kk in range(int(rng.integers(1, 4))):
                x, y = lx + nx * (off + rng.normal(0, 5)), ly + ny * (off + rng.normal(0, 5)) + rng.normal(0, 6)
                if free(x, y, clear):
                    v = int(rng.random() < 0.3)
                    objects.append(("tree", x, y, v, int(rng.integers(70, 104))))
            at_ += rng.uniform(34, 56)
        at_ = rng.uniform(0, 10)
        while at_ < total + 10:
            (lx, ly), (dx, dy) = walk(line, min(at_, total))
            nx, ny = dy * side, -dx * side
            off = clear - 6 + rng.uniform(0, 26)
            x, y = lx + nx * off, ly + ny * off
            if free(x, y, clear - 8):
                if rng.random() < 0.55:
                    objects.append(("bush", x, y, 0, int(rng.integers(60, 96))))
                else:
                    objects.append(("grass", x, y, 0, int(rng.integers(64, 100))))
            at_ += rng.uniform(24, 38)
    # a thorn tree or two out from the tee to frame the drive
    for side in (-1, 1):
        (lx, ly), (dx, dy) = walk(line, rng.uniform(50, 100))
        off = rng.uniform(34, 50)
        x, y = lx + dy * side * off, ly - dx * side * off
        if 4 < x < W - 4 and at(x, y) in (SURF["rough"], SURF["deep-rough"]) and not near_play(x, y, 6) \
                and river_at(x, y) > 5:
            objects.append(("tree", x, y, 0, int(rng.integers(86, 112))))
    for kk in range(6):
        x = gx + rng.uniform(-1, 1) * (grx + 36)
        y = gy + gry + rng.uniform(18, 40)
        if free(x, y, 14):
            objects.append(("tree", x, y, int(rng.random() < 0.3), int(rng.integers(80, 110))))
    for kk in range(d.get("palms", 0)):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(grx + 14, grx + 40)
        x, y = gx + math.cos(a) * r, gy + math.sin(a) * r * 0.8
        if free(x, y, 12):
            objects.append(("palm", x, y, 0, int(rng.integers(70, 100))))

    # The river banks: reeds (dry grass) and the odd ilala palm, crocodiles
    # basking on the banks and hippos in the water, all clear of the play.
    animals = []
    for rl, rhw in rivers:
        rt = length(rl)
        at_ = rng.uniform(0, 16)
        while at_ < rt:
            (x, y), (dx, dy) = walk(rl, at_)
            for side in (-1, 1):
                off = rhw + rng.uniform(1.5, 4)
                px, py = x + dy * side * off, y - dx * side * off
                if 2 < px < W - 2 and 2 < py < L - 2 and at(px, py) != SURF["water"] and not near_play(px, py, 3):
                    if rng.random() < 0.12 and not near_play(px, py, 10):
                        objects.append(("palm", px, py, 0, int(rng.integers(60, 90))))
                    else:
                        objects.append(("grass", px, py, 0, int(rng.integers(56, 84))))
            at_ += rng.uniform(22, 34)
        for kind, count, offs in (("croc", d.get("crocs", 0), (2.5, 5)), ("hippo", d.get("hippos", 0), (-0.6, -0.3))):
            placed = 0
            for _try in range(400):
                if placed >= count:
                    break
                (x, y), (dx, dy) = walk(rl, rng.uniform(0, rt))
                side = rng.choice([-1, 1])
                off = rhw + rng.uniform(*offs) if kind == "croc" else rhw * (1 + rng.uniform(*offs))
                px, py = x + dy * side * off, y - dx * side * off
                if not (6 < px < W - 6 and 6 < py < L - 6):
                    continue
                if (at(px, py) == SURF["water"]) != (kind == "hippo"):
                    continue
                if near_play(px, py, ANIMAL_CLEAR + 2):
                    continue
                if any(math.hypot(px - ax, py - ay) < 14 for _, ax, ay in animals):
                    continue
                animals.append((kind, px, py))
                placed += 1
    for x, y in d.get("graze", []):
        animals.append(("hippo", x, y))
    for kind, x, y in animals:
        assert not near_play(x, y, ANIMAL_CLEAR), (n, kind, x, y)
        objects.append(("animal", x, y, 1 if kind == "hippo" else 0, 64))

    for x, y in d.get("rocks", []):
        assert not near_play(x, y, 4), (n, x, y)
        objects.append(("rock", x, y, 0, int(rng.integers(90, 150))))
    for x, y in d.get("huts", []):
        objects.append(("house", x, y, 0, 64))
    if d.get("baobab"):
        x, y = d["baobab"]
        objects.append(("landmark", x, y, 0, 64))
    objects.sort(key=lambda o: (o[2], o[1]))

    for s_, pts in shapes:
        lines.append("poly %s %s" % (s_, pts_text(pts)))
    lines.append("")
    for typ, x, y, v, sc in objects:
        x, y = clampxy((x, y), W, L)
        lines.append("object %s %s %s %d %d" % (typ, fmt(x), fmt(y), v, sc))
    head = ["# Crocodile Bend, hole %d. Made by tools/mkcroc.py; edit that and run it again." % n]
    (out / ("hole-%02d.txt" % n)).write_text("\n".join(head + lines) + "\n")
    crocs = sum(1 for k_, _, _ in animals if k_ == "croc")
    return par, yards, [round(yards - a) for a in ahead], len(objects), len(shapes), crocs, len(animals) - crocs


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "courses/crocodilebend"
    out.mkdir(parents=True, exist_ok=True)
    (out / "course.txt").write_text(
        "# Crocodile Bend, a South African bush course beside a river.\n"
        "# Made by tools/mkcroc.py.\n"
        "name Crocodile Bend\n" + "".join("tee %s\n" % t for t in TEES) +
        "holes %d\n" % len(HOLES) +
        "scenery 5         # the bush backdrop: acacias, a far escarpment and a koppie under a warm sky\n"
        "wind 9            # light to moderate\n"
        "terrain-mesh all putting bunkers water\n"
        "terrain-mesh 8 putting bunkers\n")
    total, pars, tot_t = 0, 0, [0, 0]
    for n, d in enumerate(HOLES, 1):
        par, yards, others, nobj, nsh, nc, nh = make_hole(n, d, out)
        total += yards
        pars += par
        tot_t = [a + b for a, b in zip(tot_t, others)]
        print("%2d %-14s par %d %3d yd (%s) %d objects, %d shapes, %d crocodiles, %d hippos" % (
            n, d["name"], par, yards, ", ".join(str(o) for o in others), nobj, nsh, nc, nh))
    print("par %d, %d yd (%s)" % (pars, total, ", ".join(str(o) for o in tot_t)))


if __name__ == "__main__":
    main()
