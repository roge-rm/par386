#!/usr/bin/env python3
"""Writes courses/cascadesprings/: course.txt, hole-NN.txt and hole-NN.png
for Cascade Springs, a Canadian Rockies resort course in a mountain valley
with a creek, spruce and fir woods and a grand old hotel.

Each hole is laid out below from a centre line, the green, its bunkers, the
creek where it comes into play and the ground's height along the line;
shapes, objects and the heightmap come from that with the hole's own random
numbers, so running it again gives the same files. Edit the HOLES table and
run it again rather than editing the output.

Outside a band of rough the ground is forest floor (deep rough) with spruce
and fir lining the holes, aspen in clumps, juniper and granite boulders.
The valley sides rise away from the fairways. Some tees sit on a bench high
above the hole and the ground falls away steeply past the forward tees. The
creek is a band of water along a centre line given per hole (it can run off
the edges, where the water carries on), its level following the valley
floor, with rocks and juniper on its banks. Elk graze in the meadows and the
hotel stands over a few holes as the course's landmark.

Greenside bunkers in the table are (angle, radius): the angle round the
green in degrees (0 right, 90 behind, -90 in front, turned with the green),
set just off the fringe. Fairway bunkers are (x, y, radius).

Usage: mkcascade.py [COURSEDIR]
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
STEPS = (1 / 8, 5 / 32, 3 / 16, 1 / 4)   # yards per grey step, the finest that holds the hole's heights
FINE = 2                  # yards between points of the working height grid
BANK = 5.0                # how far the valley sides rise beside the fairways, yards
RIM = 0.7                 # on a high tee hole the sides rise this much of the tee's height
PIT = 0.9                 # how far heightmap points under sand go down, yards
GREEN_KEEP = 7            # heightmap points this far out from a green stay up
RIVER_DROP = 1.2          # the water's level below the valley floor, yards
RIVER_SLOPE = 20          # yards over which the ground falls to the water
ANIMAL_CLEAR = 6          # yards an animal keeps from fairway, first cut, green, fringe, tee and sand
HOTEL_SCALE = 128         # the hotel at twice the usual landmark size, 32 yards high

# Each hole: name, par, width, centre line from the back tee through the
# landing area to the middle of the green, the valley floor's height at each
# point of it, fairway half widths along it, the green (half sizes and turn
# in degrees), greenside bunkers (gb), fairway bunkers (fb), creeks as
# (centre line, half width), where the fairway starts (fw_from), how far the
# other tees are ahead (ahead) and extras: a high tee (drop: the ground
# falls h yards from d0 to d1 yards along the line, the forward tees on
# terraces down the slope), how much the valley side rises on the left and
# right (bank), extra length past the green, log cabins, granite boulders (rocks), elk grazing, aspens round
# a par 3 green and the hotel (x, y).
HOLES = [
    # 1: down off the first tee, the hotel over the green on the right
    dict(name="Trailhead", par=4, W=220, line=[(110, 14), (104, 262), (112, 404)], rise=[10, 9, 10],
         drop=(14, 80, 6), fw=(36, 30), green=(17, 15, 0), gb=[(-140, 8), (-40, 7), (120, 6)], fb=[(140, 250, 8)],
         hotel=(196, 488)),
    # 2: dogleg right past an elk meadow on the left
    dict(name="Elk Meadow", par=4, W=230, line=[(90, 14), (98, 250), (150, 376)], rise=[9, 8, 10],
         fw=(36, 30), green=(16, 15, 10), gb=[(-160, 8), (-60, 7), (60, 6)], fb=[(132, 236, 9)],
         graze=[(22, 296), (30, 322), (18, 344)]),
    # 3: a par 3 from a high tee over the creek
    dict(name="Glacier View", par=3, W=200, line=[(100, 14), (106, 190)], rise=[5, 6],
         drop=(14, 84, 18), green=(16, 14, 0), gb=[(-150, 7), (-30, 7), (100, 6)],
         rivers=[([(-30, 128), (60, 120), (140, 132), (230, 124)], 8)]),
    # 4: the long par 5 climbing up the valley, the creek all down the left
    dict(name="Switchback", par=5, W=240, line=[(130, 14), (124, 270), (150, 440), (132, 540)], rise=[7, 9, 12, 14],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-150, 8), (-30, 8), (120, 6)], fb=[(168, 262, 9), (118, 450, 8)],
         rivers=[([(30, -30), (40, 200), (58, 380), (48, 640)], 10)]),
    # 5: a big drop off the tee into a hollow of firs
    dict(name="Fir Hollow", par=4, W=220, line=[(110, 14), (112, 262), (106, 410)], rise=[6, 5, 7],
         drop=(14, 110, 22), fw=(36, 30), fw_from=145, green=(17, 15, -5), gb=[(-120, 8), (-20, 7), (150, 6)],
         fb=[(78, 268, 9)]),
    # 6: the creek across in front of the green
    dict(name="Snowmelt", par=4, W=220, line=[(100, 14), (112, 250), (104, 360)], rise=[12, 11, 10],
         fw=(36, 30), green=(16, 15, 0), gb=[(-30, 7), (100, 6), (160, 6)],
         rivers=[([(-30, 302), (80, 310), (160, 298), (260, 306)], 8)]),
    # 7: a short par 3 up to a green ringed with aspens
    dict(name="Ptarmigan", par=3, W=200, line=[(100, 14), (98, 164)], rise=[6, 9],
         green=(15, 13, 0), gb=[(-120, 6), (-50, 6), (40, 5), (150, 5)], aspens=10),
    # 8: boulders down the hillside on the left
    dict(name="Rockslide", par=4, W=230, line=[(120, 14), (112, 262), (128, 432)], rise=[8, 10, 12],
         fw=(36, 30), green=(17, 15, 5), gb=[(-140, 8), (-40, 7), (90, 6)], fb=[(150, 250, 9)], bank=(8, 4),
         rocks=[(48, 280), (40, 296), (58, 306), (34, 318), (52, 328), (28, 300), (62, 342), (44, 352)]),
    # 9: uphill over the creek to the hotel behind the green
    dict(name="Grand Terrace", par=5, W=230, line=[(110, 14), (118, 270), (104, 440), (112, 526)], rise=[6, 8, 11, 14],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-130, 8), (-40, 8), (160, 6)], fb=[(78, 270, 9), (150, 290, 8)],
         rivers=[([(-30, 346), (90, 336), (170, 352), (270, 342)], 9)], hotel=(112, 604)),
    # 10: the biggest drop, a tee high over the valley and whitewater down the right
    dict(name="Whitewater", par=4, W=230, line=[(90, 14), (98, 262), (122, 420)], rise=[5, 4, 6],
         drop=(14, 130, 28), fw=(36, 32), fw_from=165, green=(17, 15, 0), gb=[(-150, 8), (-40, 7), (190, 6)],
         fb=[(64, 280, 8)], rivers=[([(260, 120), (196, 190), (186, 330), (198, 470), (186, 640)], 12)]),
    # 11: dogleg left, the hotel high on the right
    dict(name="Larch Ridge", par=4, W=230, line=[(140, 14), (132, 250), (96, 386)], rise=[9, 10, 9],
         fw=(36, 30), green=(16, 15, -15), gb=[(-120, 8), (-10, 8), (130, 6)], fb=[(96, 240, 10)],
         hotel=(200, 476)),
    # 12: the long par 3, down from a bench
    dict(name="Bighorn", par=3, W=210, line=[(110, 14), (112, 214)], rise=[6, 6],
         drop=(14, 90, 12), green=(18, 16, 0), gb=[(-140, 9), (-40, 9), (150, 6)], ahead=(25, 55)),
    # 13: a par 5 down off the tee, the creek across before the green
    dict(name="Beaver Dam", par=5, W=240, line=[(110, 14), (104, 262), (124, 430), (116, 522)], rise=[6, 6, 8, 9],
         drop=(14, 110, 16), fw=(38, 34, 30), fw_from=145, green=(18, 16, 0), gb=[(-140, 8), (-40, 8), (170, 6)],
         fb=[(150, 452, 8)], rivers=[([(-30, 338), (70, 330), (150, 344), (270, 334)], 10)]),
    # 14: past the rangers' log cabins on the right
    dict(name="Ranger's Cabin", par=4, W=230, line=[(100, 14), (110, 250), (100, 382)], rise=[10, 9, 10],
         fw=(36, 30), green=(16, 15, 0), gb=[(-100, 9), (20, 7), (160, 6)], fb=[(72, 236, 9)],
         cabins=[(198, 286), (206, 318), (192, 350)]),
    # 15: the long par 4, granite boulders on the right
    dict(name="Hoodoo", par=4, W=230, line=[(120, 14), (126, 280), (112, 446)], rise=[9, 8, 9],
         drop=(14, 80, 8), fw=(38, 32), green=(17, 16, 0), gb=[(-130, 8), (-40, 7), (90, 6)], fb=[(160, 264, 9)],
         rocks=[(186, 330), (198, 346), (178, 356), (206, 364), (190, 374)]),
    # 16: the par 3 off the cliff, over the creek
    dict(name="Eagle's Drop", par=3, W=200, line=[(100, 14), (98, 180)], rise=[5, 6],
         drop=(14, 80, 24), green=(15, 13, 0), gb=[(-125, 6), (30, 5), (150, 6)],
         rivers=[([(-30, 112), (60, 104), (140, 118), (230, 110)], 8)]),
    # 17: tight between spruces
    dict(name="Spruce Gate", par=4, W=220, line=[(110, 14), (104, 262), (110, 412)], rise=[8, 9, 8],
         fw=(34, 28), green=(17, 15, 0), gb=[(-140, 8), (-40, 7), (110, 6)], fb=[(130, 268, 9)], dense=True),
    # 18: home along the creek to the hotel
    dict(name="Homeward", par=5, W=240, line=[(120, 14), (126, 280), (110, 450), (118, 545)], rise=[7, 7, 9, 11],
         drop=(14, 90, 10), fw=(38, 34, 30), green=(18, 16, 0), gb=[(-130, 8), (-40, 8), (160, 6)],
         fb=[(160, 286, 9), (150, 444, 8)], rivers=[([(-30, 170), (36, 240), (46, 420), (36, 640)], 11)],
         hotel=(126, 620)),
]

TEES = ["Summit", "Lodge", "Meadow"]
# Each hole's map turned against the course in degrees, which moves the
# backdrop (tools/mkobj.py cascade_backdrop).
TURNS = [20, 110, 200, 290, 60, 150, 240, 330, 80, 170, 260, 350, 40, 130, 220, 310, 100, 190]
PINS_ALONG = [(-0.45, -0.3), (0.4, -0.35), (0.35, 0.45), (-0.4, 0.4)]
SURF = {"rough": 0, "first-cut": 1, "fairway": 2, "deep-rough": 3, "green": 4, "fringe": 5,
        "bunker": 6, "water": 7, "path": 8, "oob": 9, "tee": 10}
PLAYED = (1, 2, 4, 5, 6, 10)     # first cut, fairway, green, fringe, bunker, tee: no animals near


def side_of(line, xs, ys):
    """-1 left of the line, 1 right of it (looking along it), by the nearest segment."""
    best = np.full(xs.shape, 1e9)
    sd = np.ones(xs.shape)
    for (ax, ay), (bx, by) in zip(line, line[1:]):
        dx, dy = bx - ax, by - ay
        m = math.hypot(dx, dy)
        t = np.clip(((xs - ax) * dx + (ys - ay) * dy) / (m * m), 0, 1)
        d = np.hypot(xs - (ax + t * dx), ys - (ay + t * dy))
        take = d < best
        best = np.where(take, d, best)
        # right is (dy, -dx), as in ribbon()
        sd = np.where(take, np.where((xs - ax) * dy - (ys - ay) * dx >= 0, 1.0, -1.0), sd)
    return sd


def river_poly(line, hw, W, L, rng):
    """The water along a centre line, hw yards each side with a little wobble,
    cut to the hole (points past the edge are pulled onto it)."""
    total = length(line)
    n = min(30, max(3, int(total / 14) + 1))
    left, right = [], []
    for i in range(n):
        d = total * i / (n - 1)
        (x, y), (dx, dy) = walk(line, d)
        if 0 < i < n - 1:
            (xa, ya), _ = walk(line, total * (i - 1) / (n - 1))
            (xb, yb), _ = walk(line, total * (i + 1) / (n - 1))
            m = math.hypot(xb - xa, yb - ya) or 1
            dx, dy = (xb - xa) / m, (yb - ya) / m
        nx, ny = dy, -dx
        lw = hw * (1 + rng.uniform(-0.08, 0.08))
        rw = hw * (1 + rng.uniform(-0.08, 0.08))
        left.append(clampxy((x - nx * lw, y - ny * lw), W, L))
        right.append(clampxy((x + nx * rw, y + ny * rw), W, L))
    return left + right[::-1]


def make_hole(n, d, out):
    rng = np.random.default_rng(9000 + n)
    W, par, line = d["W"], d["par"], d["line"]
    gx, gy = line[-1]
    grx, gry, gturn = d["green"]
    L = int(gy + gry + 40 + d.get("extra", 40 if d.get("drop") else 0))
    for hx, hy in d.get("cabins", []):
        L = max(L, int(hy + 14))
    if d.get("hotel"):
        L = max(L, int(d["hotel"][1] + 16))
    L = min(L, 640)
    lines = []
    shapes = []
    objects = []

    yards = round(length(line))
    lines.append("name %s" % d["name"])
    lines.append("par %d" % par)
    lines.append("size %d %d" % (W, L))
    lines.append("heightmap hole-%02d.png %d" % (n, CELL))
    hline = len(lines)
    lines.append("height")      # filled in once the heights are known
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
        shapes.append(("rough", ribbon(line, 0, total + gry + 12, hw, 14, rng, W, L)))
        shapes.append(("first-cut", ribbon(line, tee_gap - 6, green_front + 6, hw, 4, rng, W, L)))
        shapes.append(("fairway", ribbon(line, tee_gap, green_front + 2, hw, 0, rng, W, L)))
    else:
        shapes.append(("rough", ribbon(line, 0, total - gry - 10, [22], 0, rng, W, L, step=30)))
        shapes.append(("rough", ellipse_pts(gx, gy, grx + 14, gry + 14, gturn, 16, W, L, rng, 0.08)))
        shapes.append(("first-cut", ellipse_pts(gx, gy - gry - 6, grx + 4, 10, gturn, 12, W, L)))
    shapes.append(("rough", ellipse_pts(tee_pts[0][0], tee_pts[0][1] + 4, 22, 18, 0, 12, W, L, rng, 0.1)))

    # the creek over the grass, then the sand, the fringe, the green and the tees
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

    # Heights on a fine grid: the valley floor along the line, the tee bench
    # and its drop, the valley sides rising away from the fairway, low humps,
    # calmer on fairways, the ground falling to the creek, the green a
    # tilted plane, tees level.
    cum = np.concatenate([[0], np.cumsum([math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(line, line[1:])])])
    s, lat = line_param(line, xs, ys)
    floor = np.interp(s, cum, d["rise"])
    base = floor.copy()
    if d.get("drop"):
        d0, d1, dh = d["drop"]
        base = base + dh * (1 - smoothstep((s - d0) / (d1 - d0)))
    band = (max(d["fw"]) + 16) if par > 3 else 30
    # on a high tee hole the valley sides and the ground past the green
    # rise most of the way back to the tee, so from the tee the land off
    # the map (held level past the edge) stays a thin strip at the horizon
    rim = max(BANK, d["drop"][2] * RIM) if d.get("drop") else BANK
    bl, br = d.get("bank", (rim, rim))
    sd = side_of(line, xs, ys)
    bank = smoothstep((lat - band) / 60) * np.where(sd < 0, bl, br)
    ex, ey = line[-1][0] - line[-2][0], line[-1][1] - line[-2][1]
    em = math.hypot(ex, ey)
    past = ((xs - gx) * ex + (ys - gy) * ey) / em - gry
    bank = np.maximum(bank, smoothstep((past - 24) / 50) * rim)
    hum = vnoise((fh_, fw_), 9.0, rng) * 0.9 + vnoise((fh_, fw_), 24.0, rng) * 1.6
    fw_mask = np.isin(surf, (SURF["fairway"], SURF["first-cut"], SURF["tee"]))
    smooth = np.maximum(base, floor + bank)
    z = smooth + hum * np.where(fw_mask, 0.45, 1.0)
    # the creek's level follows the valley floor where it runs, with no bench
    lvl = np.full(xs.shape, 1e9)
    rdist = np.full(xs.shape, 999.0)
    for rl, rhw in rivers:
        rs, rlat = line_param(rl, xs, ys)
        # the valley floor under each point of the creek's own line
        pts = np.array([walk(rl, v)[0] for v in np.linspace(0, length(rl), 60)])
        ps, _ = line_param(line, pts[:, 0], pts[:, 1])
        pl = np.interp(ps, cum, d["rise"]) - RIVER_DROP
        lv = np.interp(rs, np.linspace(0, length(rl), 60), pl)
        dd = np.maximum(0, rlat - rhw)
        take = dd < rdist
        rdist = np.where(take, dd, rdist)
        lvl = np.where(take, lv, lvl)
    rdist = np.where(wet, 0, rdist)
    f = smoothstep(rdist / RIVER_SLOPE)
    z = np.where(rdist < 900, (lvl + 0.4) * (1 - f) + z * f, z)
    smooth = np.where(rdist < 900, (lvl + 0.4) * (1 - f) + smooth * f, smooth)
    gd = np.hypot((xs - gx) / (grx + 14), (ys - gy) / (gry + 14))
    gw = np.clip((1.3 - gd) / 0.5, 0, 1)
    g0 = smooth[int(gy / FINE), int(gx / FINE)]
    if rivers:
        g0 = max(g0, float(lvl[int(gy / FINE), int(gx / FINE)]) + 1.0)
    gz = g0 + 0.4 + (ys - gy) * 0.02 + (xs - gx) * 0.01 * rng.choice([-1, 1])
    z = z * (1 - gw) + gz * gw
    for k, (x, y) in enumerate(tee_pts):
        tw = np.clip(1.6 - np.hypot(xs - x, ys - y) / 14, 0, 1)
        top = float(smooth[int(round(y / FINE)), int(round(x / FINE))]) + 0.4
        z = z * (1 - tw) + top * tw
    z = np.where(wet, lvl, z)
    # sample every CELL yards; by the water the lowest point within 4 yards,
    # so the water stays level across
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
    hm = np.maximum(hm, 0)
    step = next(st for st in STEPS if hm.max() / st <= 255)
    lines[hline] = "height 0 %s" % fmt_step(step)
    grey = np.clip(np.round(hm / step), 0, 255).astype(np.uint8)
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

    def free(x, y, clear):
        return (4 < x < W - 4 and 4 < y < L - 4 and at(x, y) == SURF["deep-rough"] and off_line(x, y) > clear
                and river_at(x, y) > 5 and not near_play(x, y, 4))

    def conifer():
        return ("pine", int(rng.random() < 0.4))

    # Spruce and fir lining both sides close in, aspens in clumps among
    # them, juniper at the edge of the rough.
    clear = (max(d["fw"]) + 10) if par > 3 else 22
    gap = (16, 26) if d.get("dense") else (30, 46) if d.get("hotel") else (24, 38)
    for side in (-1, 1):
        at_ = rng.uniform(0, 12)
        while at_ < total + 20:
            (lx, ly), (dx, dy) = walk(line, min(at_, total))
            nx, ny = dy * side, -dx * side
            off = clear + rng.uniform(0, 20)
            aspen = rng.random() < 0.15
            for kk in range(int(rng.integers(1, 4))):
                x, y = lx + nx * (off + rng.normal(0, 4)), ly + ny * (off + rng.normal(0, 4)) + rng.normal(0, 5)
                if free(x, y, clear):
                    if aspen:
                        objects.append(("tree", x, y, 0, int(rng.integers(60, 84))))
                    else:
                        typ, v = conifer()
                        objects.append((typ, x, y, v, int(rng.integers(72, 112))))
            at_ += rng.uniform(*gap)
        at_ = rng.uniform(0, 30)
        while at_ < total:
            (lx, ly), (dx, dy) = walk(line, min(at_, total))
            nx, ny = dy * side, -dx * side
            off = clear - 4 + rng.uniform(0, 8)
            x, y = lx + nx * off, ly + ny * off
            if free(x, y, clear - 6):
                objects.append(("bush", x, y, 0, int(rng.integers(64, 100))))
            at_ += rng.uniform(50, 90)
    # a conifer either side out from the tee to frame the drive
    for side in (-1, 1):
        (lx, ly), (dx, dy) = walk(line, rng.uniform(50, 90))
        off = rng.uniform(32, 44)
        x, y = lx + dy * side * off, ly - dx * side * off
        if 4 < x < W - 4 and at(x, y) in (SURF["rough"], SURF["deep-rough"]) and not near_play(x, y, 6) \
                and river_at(x, y) > 5:
            typ, v = conifer()
            objects.append((typ, x, y, v, int(rng.integers(90, 116))))
    # a wall of conifers behind the green
    for kk in range(7):
        x = gx + rng.uniform(-1, 1) * (grx + 34)
        y = gy + gry + rng.uniform(18, 36)
        if free(x, y, 14):
            typ, v = conifer()
            objects.append((typ, x, y, v, int(rng.integers(84, 116))))
    # on a high tee hole, conifers up the slope that rises past the green,
    # but not where the hotel stands (the scene keeps only the nearest 48
    # objects, and the hotel is the farthest)
    if d.get("drop") and not d.get("hotel"):
        for kk in range(10):
            x = gx + rng.uniform(-1, 1) * (grx + 60)
            y = gy + gry + rng.uniform(36, 80)
            if free(x, y, 14):
                typ, v = conifer()
                objects.append((typ, x, y, v, int(rng.integers(84, 116))))
    for kk in range(d.get("aspens", 0)):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(grx + 14, grx + 34)
        x, y = gx + math.cos(a) * r, gy + math.sin(a) * r * 0.8
        if free(x, y, 12):
            objects.append(("tree", x, y, 0, int(rng.integers(64, 90))))

    # The creek banks: granite boulders and juniper, clear of the play.
    for rl, rhw in rivers:
        rt = length(rl)
        at_ = rng.uniform(0, 20)
        while at_ < rt:
            (x, y), (dx, dy) = walk(rl, at_)
            side = rng.choice([-1, 1])
            off = rhw + rng.uniform(1.5, 4)
            px, py = x + dy * side * off, y - dx * side * off
            if 2 < px < W - 2 and 2 < py < L - 2 and at(px, py) != SURF["water"] and not near_play(px, py, 4):
                if rng.random() < 0.5:
                    objects.append(("rock", px, py, 0, int(rng.integers(60, 110))))
                else:
                    objects.append(("bush", px, py, 0, int(rng.integers(60, 90))))
            at_ += rng.uniform(30, 48)

    elk = list(d.get("graze", []))
    for x, y in elk:
        assert not near_play(x, y, ANIMAL_CLEAR), (n, x, y)
        objects.append(("animal", x, y, 0, 96))
    for x, y in d.get("rocks", []):
        assert not near_play(x, y, 4), (n, x, y)
        objects.append(("rock", x, y, 0, int(rng.integers(90, 160))))
    for x, y in d.get("cabins", []):
        assert not near_play(x, y, 8), (n, x, y)
        objects.append(("house", x, y, 0, 64))
    if d.get("hotel"):
        x, y = d["hotel"]
        assert not near_play(x, y, 20), (n, x, y)
        objects.append(("landmark", x, y, 0, HOTEL_SCALE))
    objects.sort(key=lambda o: (o[2], o[1]))

    for s_, pts in shapes:
        lines.append("poly %s %s" % (s_, pts_text(pts)))
    lines.append("")
    for typ, x, y, v, sc in objects:
        x, y = clampxy((x, y), W, L)
        lines.append("object %s %s %s %d %d" % (typ, fmt(x), fmt(y), v, sc))
    head = ["# Cascade Springs, hole %d. Made by tools/mkcascade.py; edit that and run it again." % n]
    (out / ("hole-%02d.txt" % n)).write_text("\n".join(head + lines) + "\n")
    drop = round(float(z[int(round(tee_pts[0][1] / FINE)), int(round(tee_pts[0][0] / FINE))] - gz[int(gy / FINE), int(gx / FINE)]), 1)
    return par, yards, [round(yards - a) for a in ahead], len(objects), len(shapes), step, drop


def fmt_step(v):
    return ("%.6f" % v).rstrip("0")


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "courses/cascadesprings"
    out.mkdir(parents=True, exist_ok=True)
    (out / "course.txt").write_text(
        "# Cascade Springs, a Canadian Rockies resort course in a mountain valley.\n"
        "# Made by tools/mkcascade.py.\n"
        "name Cascade Springs\n" + "".join("tee %s\n" % t for t in TEES) +
        "holes %d\n" % len(HOLES) +
        "scenery 6         # the Rockies backdrop: snow-capped peaks over wooded slopes\n"
        "wind 8            # light to moderate\n"
        "terrain-mesh 13 putting bunkers\n")
    total, pars, tot_t = 0, 0, [0, 0]
    for n, d in enumerate(HOLES, 1):
        par, yards, others, nobj, nsh, step, drop = make_hole(n, d, out)
        total += yards
        pars += par
        tot_t = [a + b for a, b in zip(tot_t, others)]
        print("%2d %-14s par %d %3d yd (%s) %d objects, %d shapes, tee %+.1f yd over the green, step %s" % (
            n, d["name"], par, yards, ", ".join(str(o) for o in others), nobj, nsh, drop, fmt_step(step)))
    print("par %d, %d yd (%s)" % (pars, total, ", ".join(str(o) for o in tot_t)))


if __name__ == "__main__":
    main()
