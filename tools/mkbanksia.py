#!/usr/bin/env python3
"""Writes courses/banksiaheath/: course.txt, hole-NN.txt and hole-NN.png for
Banksia Heath, an Australian sandbelt course on low sandy ground, with heath
and gum trees and big ragged bunkers cut right into the greens.

Each hole is laid out below from a centre line, the green, its bunkers and
the ground's height along the line; shapes, objects and the heightmap come
from that with the hole's own random numbers, so running it again gives the
same files. Edit the HOLES table and run it again rather than editing the
output.

Outside a wide band of rough the ground is heath (deep rough) with low
shrubs, tussocks, clumps of gum trees and the odd patch of bare sand. A
bunker is a few ragged lobes of sand with a dark heath lip round its outer
side; greenside ones are drawn after the green, so the sand bites into it
with no fringe between. Heightmap points under the sand are lowered, so the
sand falls away from the green's edge.

Greenside bunkers in the table are (angle, radius, cut): the angle round the
green in degrees (0 right, 90 behind, -90 in front, turned with the green),
the radius in yards and how many yards the sand cuts into the green.
Fairway bunkers are (x, y, radius).

Usage: mkbanksia.py [COURSEDIR]
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
BANK = 2.5                # how far the heath rises beside the fairways
PIT_GREEN = 1.4           # how far heightmap points under greenside sand go down, yards
PIT_FAIRWAY = 0.9         # and under fairway sand
GREEN_KEEP = 7            # heightmap points this far out from a green stay up, so it doesn't run off into the sand
LIP = 3.0                 # width of the heath lip round a bunker, yards
PIN_CLEAR = 2.5           # yards a pin must stay from the sand

# Each hole: name, par, width, centre line from the back tee to the middle of
# the green, the ground's height at each point of it, fairway half widths
# along it, the green (half sizes and turn in degrees), greenside bunkers
# (gb), fairway bunkers (fb), ponds (cx, cy, rx, ry, turn), how far the
# other tees are ahead (front, forward) and extras: a raised tee (yards),
# extra length past the green, sheds, the windmill, kangaroos.
HOLES = [
    # 1: a gentle start, gums on both sides, bunkers by the green's front corners
    dict(name="Wattle Gate", par=4, W=220, line=[(110, 14), (104, 250), (116, 396)], rise=[8, 7, 9],
         fw=(38, 32), green=(17, 15, 0), gb=[(-130, 9, 3), (-45, 8, 3), (110, 6, 2)],
         fb=[(80, 236, 9), (146, 262, 8)], houses=[(40, 30)], roos=[(190, 210)]),
    # 2: the long par 5 doglegs right then left round heath, sand at each turn
    dict(name="Kookaburra", par=5, W=240, line=[(120, 14), (110, 270), (140, 440), (130, 540)], rise=[6, 8, 7, 9],
         fw=(38, 34, 30), green=(18, 16, 5), gb=[(-150, 10, 3), (-20, 8, 3), (120, 7, 2)],
         fb=[(152, 268, 10), (104, 434, 9), (166, 470, 7)]),
    # 3: a par 3 to a green with sand close on the left and right
    dict(name="Red Gum", par=3, W=200, line=[(100, 14), (104, 184)], rise=[9, 10],
         fw=(0, 0), green=(16, 14, 0), gb=[(-160, 10, 4), (-20, 9, 4), (100, 6, 2)], raised=1),
    # 4: a short par 4, a big bunker right across the front of the green
    dict(name="Short Cut", par=4, W=220, line=[(110, 14), (118, 230), (108, 316)], rise=[8, 9, 10],
         fw=(34, 26), green=(15, 13, 0), gb=[(-95, 12, 4), (20, 7, 3), (160, 7, 3)],
         fb=[(84, 214, 8)]),
    # 5: dogleg right past a long bunker on the corner
    dict(name="Tea Tree", par=4, W=230, line=[(90, 14), (100, 256), (140, 420)], rise=[7, 8, 8],
         fw=(38, 30), green=(16, 16, 15), gb=[(-170, 10, 3), (-60, 8, 3), (60, 7, 2)],
         fb=[(128, 248, 11), (70, 278, 7)]),
    # 6: the only water, a small farm dam short and left of the green
    dict(name="The Dam", par=3, W=200, line=[(110, 14), (100, 166)], rise=[8, 7],
         fw=(0, 0), green=(16, 15, -10), gb=[(-30, 9, 3), (150, 7, 3), (80, 6, 2)],
         ponds=[(62, 126, 20, 12, 10)], roos=[(150, 90), (162, 104)]),
    # 7: the longest hole, three shots between the gums
    dict(name="Long Paddock", par=5, W=240, line=[(130, 14), (120, 280), (96, 450), (104, 570)], rise=[9, 8, 7, 9],
         fw=(38, 34, 30), green=(18, 17, 0), gb=[(-120, 10, 3), (-40, 9, 3), (170, 7, 2)],
         fb=[(150, 270, 10), (70, 460, 9), (128, 440, 7)], houses=[(200, 300)]),
    # 8: bending gently left, the green set on a rise
    dict(name="Ironbark", par=4, W=220, line=[(100, 14), (96, 260), (80, 420)], rise=[7, 8, 11],
         fw=(38, 30), green=(16, 15, -10), gb=[(-150, 9, 3), (-30, 9, 3), (90, 6, 2)],
         fb=[(70, 252, 9), (130, 274, 8)]),
    # 9: home to the windmill behind the green
    dict(name="Windmill", par=4, W=220, line=[(110, 14), (116, 250), (110, 404)], rise=[9, 8, 10],
         fw=(38, 32), green=(17, 15, 0), gb=[(-100, 10, 3), (0, 8, 3), (170, 8, 3)],
         fb=[(86, 246, 9), (150, 236, 8)], mill=(162, 446), extra=30, houses=[(60, 436)]),
    # 10: dogleg left across the corner
    dict(name="Magpie", par=4, W=230, line=[(130, 14), (120, 240), (90, 370)], rise=[10, 8, 8],
         fw=(36, 30), green=(16, 15, -15), gb=[(-120, 9, 3), (-10, 9, 3), (130, 6, 2)],
         fb=[(96, 230, 10), (156, 246, 7)], roos=[(30, 160)]),
    # 11: a par 5 out across the heath, sand in the second shot's way
    dict(name="Heath Run", par=5, W=240, line=[(100, 14), (110, 270), (130, 440), (124, 540)], rise=[8, 7, 8, 7],
         fw=(38, 34, 30), green=(18, 16, 0), gb=[(-140, 10, 3), (-40, 8, 3), (100, 7, 2)],
         fb=[(80, 266, 9), (140, 282, 8), (112, 420, 11)]),
    # 12: a long par 3 to a big green with sand beside both front corners
    dict(name="Stringybark", par=3, W=210, line=[(110, 14), (114, 244)], rise=[9, 9],
         fw=(0, 0), green=(19, 17, 0), gb=[(-135, 11, 4), (-45, 11, 4), (160, 7, 2)],
         ahead=(25, 55), raised=1),
    # 13: a short par 4, a diagonal green with a long bunker across its front
    dict(name="Sand Hollow", par=4, W=220, line=[(100, 14), (96, 220), (116, 320)], rise=[9, 7, 8],
         fw=(34, 26), green=(17, 12, 25), gb=[(-110, 12, 4), (-30, 7, 3), (70, 7, 2)],
         fb=[(70, 206, 8)]),
    # 14: a long straight par 4 named after the shrub that lines it
    dict(name="Banksia", par=4, W=220, line=[(100, 14), (110, 262), (104, 440)], rise=[8, 9, 8],
         fw=(38, 30), green=(17, 16, 0), gb=[(-150, 10, 3), (-30, 10, 3), (110, 6, 2)],
         fb=[(80, 260, 9), (142, 280, 9)]),
    # 15: a par 5 bending left, a field of sand at the dogleg
    dict(name="Saltbush", par=5, W=240, line=[(140, 14), (126, 270), (100, 430), (112, 546)], rise=[8, 8, 9, 8],
         fw=(38, 34, 30), green=(18, 16, 10), gb=[(-120, 10, 3), (-20, 9, 3), (150, 6, 2)],
         fb=[(150, 266, 9), (78, 410, 9), (96, 448, 7), (130, 420, 7)], roos=[(210, 380), (24, 300)]),
    # 16: a short par 3 to a small green ringed with sand
    dict(name="Grass Tree", par=3, W=200, line=[(100, 14), (96, 164)], rise=[10, 8],
         fw=(0, 0), green=(14, 13, 0), gb=[(-120, 8, 3), (-50, 8, 3), (20, 6, 3), (150, 6, 3), (90, 6, 2)],
         raised=1),
    # 17: the hardest hole, long and up a gentle rise
    dict(name="Mallee", par=4, W=230, line=[(90, 14), (104, 270), (126, 446)], rise=[7, 8, 11],
         fw=(38, 30), green=(17, 16, 5), gb=[(-150, 10, 3), (-40, 9, 3), (90, 7, 2)],
         fb=[(76, 262, 10), (136, 286, 8)]),
    # 18: home to the clubhouse behind the green
    dict(name="Home Paddock", par=4, W=220, line=[(110, 14), (118, 260), (104, 418)], rise=[8, 8, 10],
         fw=(38, 32), green=(18, 16, 0), gb=[(-130, 10, 3), (-50, 9, 3), (160, 6, 2)],
         fb=[(86, 250, 9), (150, 270, 8)], houses=[(70, 466), (150, 470)], extra=30),
]

TEES = ["Gold", "Blue", "Red"]
# Each hole's map turned against the course in degrees, which moves the
# backdrop (tools/mkobj.py banksia_backdrop).
TURNS = [20, 110, 200, 70, 150, 300, 40, 250, 160, 90, 10, 230, 130, 280, 60, 190, 320, 140]
PINS_ALONG = [(-0.45, -0.3), (0.4, -0.35), (0.35, 0.45), (-0.4, 0.4)]


def lobes(cx, cy, r, ux, uy, rng, grow=0.0):
    """A ragged bunker as a few lobes round (cx, cy), long across (ux, uy):
    (x, y, rx, ry, turn) each, grow yards bigger all round."""
    tx, ty = -uy, ux                     # along the bunker
    turn = math.degrees(math.atan2(ty, tx))
    out = [(cx, cy, r * 1.25 + grow, r * 0.75 + grow, turn)]
    for side in (-1, 1):
        k = rng.uniform(0.7, 1.0)
        out.append((cx + tx * side * r * k + ux * r * 0.25, cy + ty * side * r * k + uy * r * 0.25,
                    r * 0.6 + grow, r * 0.5 + grow, turn + rng.uniform(-30, 30)))
    return out


def make_hole(n, d, out):
    rng = np.random.default_rng(7000 + n)
    W, par, line = d["W"], d["par"], d["line"]
    gx, gy = line[-1]
    grx, gry, gturn = d["green"]
    L = int(gy + gry + 40 + d.get("extra", 0))
    for hx, hy in d.get("houses", []):
        L = max(L, int(hy + 14))
    if d.get("mill"):
        L = max(L, int(d["mill"][1] + 14))
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
    pin_at = len(lines)      # the pins go here once the sand is known
    lines.append("")

    def on_green(x, y, m):
        """Inside the green grown by m yards."""
        ca, sa = math.cos(-t), math.sin(-t)
        rx, ry = (x - gx) * ca - (y - gy) * sa, (x - gx) * sa + (y - gy) * ca
        return math.hypot(rx / (grx + m), ry / (gry + m)) < 1

    # Bunkers as lobes: greenside ones on the green's edge cutting in, the
    # fairway ones where the table puts them.
    bunkers = []          # (lobes, lip lobes, greenside)
    for ang, r, cut in d.get("gb", []):
        a = math.radians(ang)
        edge = 1 / math.hypot(math.cos(a) / grx, math.sin(a) / gry)
        a += t
        ux, uy = math.cos(a), math.sin(a)
        want = edge + r * 0.75 - cut
        cx, cy = gx + ux * want, gy + uy * want
        sub = np.random.default_rng(rng.integers(1 << 30))
        bunkers.append((lobes(cx, cy, r, ux, uy, sub), None, True, ux, uy))
    for bx, by, r in d.get("fb", []):
        ang = rng.uniform(-0.3, 0.3)
        ux, uy = math.sin(ang), -math.cos(ang)       # the face toward the tee
        bunkers.append((lobes(bx, by, r, ux, uy, np.random.default_rng(rng.integers(1 << 30))), None, False, ux, uy))

    def lobe_pts(lb, grow, seed, wob, counts=(10, 7, 7)):
        r_ = np.random.default_rng(seed)
        return [ellipse_pts(x, y, rx + grow, ry + grow, tr, c, W, L, r_, wob)
                for (x, y, rx, ry, tr), c in zip(lb, counts)]

    # Rough round the fairway, the green and the tees; first cut, fairway.
    tee_gap = 45 if par > 3 else 0
    green_front = length(line) - gry - 4
    total = length(line)
    if par > 3:
        hw = list(d["fw"])
        shapes.append(("rough", ribbon(line, 0, total + gry + 12, hw, 16, rng, W, L)))
        shapes.append(("first-cut", ribbon(line, tee_gap - 6, green_front + 6, hw, 4, rng, W, L)))
        shapes.append(("fairway", ribbon(line, tee_gap, green_front + 2, hw, 0, rng, W, L)))
    else:
        shapes.append(("rough", ribbon(line, 0, total - gry - 10, [22], 0, rng, W, L, step=30)))
        shapes.append(("rough", ellipse_pts(gx, gy, grx + 16, gry + 16, gturn, 16, W, L, rng, 0.08)))
        shapes.append(("first-cut", ellipse_pts(gx, gy - gry - 6, grx + 4, 12, gturn, 12, W, L)))
    for k, (x, y) in enumerate(tee_pts[:1]):
        shapes.append(("rough", ellipse_pts(x, y + 4, 22, 18, 0, 12, W, L, rng, 0.1)))

    # Bare sand out in the heath, a patch or two a hole.
    for kk in range(int(rng.integers(1, 3))):
        for _try in range(20):
            at = rng.uniform(60, total - 40)
            (lx, ly), (dx, dy) = walk(line, at)
            side = rng.choice([-1, 1])
            off = (max(d["fw"]) if par > 3 else 22) + rng.uniform(26, 44)
            x, y = lx + dy * side * off, ly - dx * side * off
            if 20 < x < W - 20:
                shapes.append(("bunker", ellipse_pts(x, y, rng.uniform(8, 13), rng.uniform(5, 8),
                                                     rng.uniform(0, 180), 12, W, L, rng, 0.25)))
                break

    ponds = []
    for cx, cy, rx, ry, turn in d.get("ponds", []):
        ponds.append((cx, cy, ellipse_pts(cx, cy, rx, ry, turn, 20, W, L, rng, 0.035)))
    for _, _, pts in ponds:
        shapes.append(("water", pts))

    # The heath lips and sand sit below the fringe and the putting surface.
    for k, (lb, _, near, ux, uy) in enumerate(bunkers):
        for pts in lobe_pts(lb, LIP, 100 * n + k, 0.12, (8, 5, 5)):
            shapes.append(("deep-rough", pts))
    sand = []
    for k, (lb, _, near, ux, uy) in enumerate(bunkers):
        for pts in lobe_pts(lb, 0, 100 * n + k, 0.2):
            shapes.append(("bunker", pts))
            sand.append((pts, near))
    shapes.append(("fringe", ellipse_pts(gx, gy, grx + 3, gry + 3, gturn, 16, W, L)))
    shapes.append(("green", ellipse_pts(gx, gy, grx, gry, gturn, 18, W, L)))
    # Pins toward the green's corners, moved in toward the middle where the
    # sand comes too close.
    def clear_of_sand(px, py):
        for a_ in range(8):
            ox, oy = PIN_CLEAR * math.cos(a_ * math.pi / 4), PIN_CLEAR * math.sin(a_ * math.pi / 4)
            if not on_green(px + ox, py + oy, 0.5) and not (ox == 0 and oy == 0):
                return False
            if any(inside(pts, np.array(px + ox), np.array(py + oy)) for pts, _ in sand):
                return False
        return True
    pin_pts = []
    for px, py in PINS_ALONG:
        for f in (1.2, 1.1, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5):
            ox, oy = px * grx * f, py * gry * f
            pp = (gx + ox * math.cos(t) - oy * math.sin(t), gy + ox * math.sin(t) + oy * math.cos(t))
            if clear_of_sand(*pp):
                break
        else:
            raise SystemExit("hole %d: no pin clear of the sand toward %s" % (n, (px, py)))
        pin_pts.append(pp)
    lines[pin_at:pin_at] = ["pin %s %s" % (fmt(x), fmt(y)) for x, y in pin_pts]
    for k, (x, y) in enumerate(tee_pts):
        hw_ = 7 if k == 0 else 5
        shapes.append(("tee", [clampxy((x - hw_, y - 6), W, L), clampxy((x + hw_, y - 6), W, L),
                               clampxy((x + hw_, y + 6), W, L), clampxy((x - hw_, y + 6), W, L)]))

    # Heights on a fine grid: the heights along the line, the heath rising a
    # little away from the fairway, low humps, calmer on fairways, ponds in
    # hollows, the green a tilted plane, tees level.
    fw_, fh_ = W // FINE + 1, L // FINE + 1
    ys, xs = np.mgrid[0:fh_, 0:fw_] * float(FINE)
    cum = np.concatenate([[0], np.cumsum([math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(line, line[1:])])])
    s, lat = line_param(line, xs, ys)
    base = np.interp(s, cum, d["rise"])
    band = (max(d["fw"]) + 18) if par > 3 else 30
    bank = smoothstep((lat - band) / 50) * BANK
    hum = vnoise((fh_, fw_), 9.0, rng) * 0.9 + vnoise((fh_, fw_), 24.0, rng) * 1.6
    fw_mask = np.zeros(xs.shape, dtype=bool)
    for name, pts in shapes:
        if name in ("fairway", "first-cut", "tee"):
            fw_mask |= inside(pts, xs, ys)
    smooth = base + bank
    z = smooth + hum * np.where(fw_mask, 0.45, 1.0)
    pdist = np.full(xs.shape, 999.0)
    for cx, cy, pts in ponds:
        lvl = float(np.interp(line_param(line, np.array([cx]), np.array([cy]))[0], cum, d["rise"])[0]) - 1.2
        w_ = inside(pts, xs, ys)
        dd = np.min([np.hypot(xs - px, ys - py) for px, py in pts], axis=0)
        dd = np.where(w_, 0, dd)
        pdist = np.minimum(pdist, dd)
        f = smoothstep(dd / 14)
        z = np.where(w_, lvl - 0.2, (lvl + 0.3) * (1 - f) + z * f)
        smooth = np.where(w_, lvl - 0.2, (lvl + 0.3) * (1 - f) + smooth * f)
    gd = np.hypot((xs - gx) / (grx + 14), (ys - gy) / (gry + 14))
    gw = np.clip((1.3 - gd) / 0.5, 0, 1)
    g0 = smooth[int(gy / FINE), int(gx / FINE)]
    gz = g0 + 0.4 + (ys - gy) * 0.02 + (xs - gx) * 0.01 * rng.choice([-1, 1])
    z = z * (1 - gw) + gz * gw
    for k, (x, y) in enumerate(tee_pts):
        tw = np.clip(1.6 - np.hypot(xs - x, ys - y) / 14, 0, 1)
        lift = d.get("raised", 0) if k == 0 else d.get("raised", 0) * 0.5
        z = z * (1 - tw) + (smooth + 0.4 + lift) * tw
    # sample every CELL yards; by a pond the lowest point within 4 yards,
    # so the water stays flat
    hw_, hh_ = W // CELL + 2, L // CELL + 2
    zp = np.pad(z, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    dp = np.pad(pdist, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    hm = np.zeros((hh_, hw_))
    k = CELL // FINE
    for j in range(hh_):
        for i in range(hw_):
            jj, ii = j * k + 2, i * k + 2
            near = dp[jj - 2:jj + 3, ii - 2:ii + 3].min() < 0.5
            hm[j, i] = zp[jj - 2:jj + 3, ii - 2:ii + 3].min() if near else zp[jj, ii]
    # the sand goes down: heightmap points under a bunker (or the nearest
    # one to its middle when none is) drop, but not those on or near the green
    gy_, gx_ = np.mgrid[0:hh_, 0:hw_] * float(CELL)
    for lb, _, near, ux, uy in bunkers:
        pts_all = lobe_pts(lb, 1.5, 0, 0)
        under = np.zeros(gy_.shape, dtype=bool)
        for p in pts_all:
            under |= inside(p, gx_, gy_)
        hit = [(j, i) for j, i in zip(*np.nonzero(under)) if not on_green(i * CELL, j * CELL, GREEN_KEEP)]
        if not hit:
            cx, cy = lb[0][0], lb[0][1]
            i, j = round(cx / CELL), round(cy / CELL)
            if not on_green(i * CELL, j * CELL, GREEN_KEEP):
                hit.append((j, i))
        for j, i in hit:
            if 0 <= j < hh_ and 0 <= i < hw_:
                hm[j, i] = max(0.0, hm[j, i] - (PIT_GREEN if near else PIT_FAIRWAY))
    grey = np.clip(np.round(hm / STEP), 0, 255).astype(np.uint8)
    Image.fromarray(grey[::-1]).save(out / ("hole-%02d.png" % n))

    # where objects can't go, on the fine grid
    solid_m = np.zeros(xs.shape, dtype=bool)
    for s_, p in shapes:
        if s_ in ("rough", "first-cut", "fairway", "green", "fringe", "tee", "bunker", "water"):
            solid_m |= inside(p, xs, ys)

    # where the tee gums can't go: the rough is allowed
    hard_m = np.zeros(xs.shape, dtype=bool)
    for s_, p in shapes:
        if s_ in ("first-cut", "fairway", "green", "fringe", "tee", "bunker", "water"):
            hard_m |= inside(p, xs, ys)

    def solid(x, y):
        return bool(solid_m[min(int(round(y / FINE)), fh_ - 1), min(int(round(x / FINE)), fw_ - 1)])

    def off_line(x, y):
        return float(line_param(line, np.array([x]), np.array([y]))[1][0])

    def pond_at(x, y):
        return min([999.0] + [math.hypot(x - px, y - py) for _, _, pts in ponds for px, py in pts])

    def free(x, y, clear):
        return (4 < x < W - 4 and 4 < y < L - 4 and not solid(x, y) and off_line(x, y) > clear
                and pond_at(x, y) > 6)

    # Clumps of gums down both sides, heath shrubs and tussocks between.
    clear = (max(d["fw"]) + 16) if par > 3 else 24
    for side in (-1, 1):
        at = rng.uniform(0, 20)
        while at < total + 20:
            (lx, ly), (dx, dy) = walk(line, min(at, total))
            nx, ny = dy * side, -dx * side
            off = clear + rng.uniform(6, 40)
            for kk in range(int(rng.integers(1, 4))):
                x, y = lx + nx * (off + rng.normal(0, 5)), ly + ny * (off + rng.normal(0, 5)) + rng.normal(0, 6)
                if free(x, y, clear):
                    v = int(rng.random() < 0.4)
                    objects.append(("tree", x, y, v, int(rng.integers(70, 104))))
            at += rng.uniform(34, 56)
        at = rng.uniform(0, 10)
        while at < total + 10:
            (lx, ly), (dx, dy) = walk(line, min(at, total))
            nx, ny = dy * side, -dx * side
            off = clear - 6 + rng.uniform(0, 26)
            x, y = lx + nx * off, ly + ny * off
            if free(x, y, clear - 8):
                if rng.random() < 0.55:
                    objects.append(("bush", x, y, 0, int(rng.integers(56, 90))))
                else:
                    objects.append(("grass", x, y, 0, int(rng.integers(64, 100))))
            at += rng.uniform(22, 36)
    # a few gums out from the tee, in the rough, to frame the drive
    for side in (-1, 1):
        for kk in range(int(rng.integers(1, 3))):
            (lx, ly), (dx, dy) = walk(line, rng.uniform(50, 110))
            off = rng.uniform(32, 48)
            x, y = lx + dy * side * off, ly - dx * side * off
            if 4 < x < W - 4 and not hard_m[int(round(y / FINE)), int(round(x / FINE))] and off_line(x, y) > 28:
                objects.append(("tree", x, y, int(rng.random() < 0.4), int(rng.integers(84, 112))))
    for kk in range(6):
        x = gx + rng.uniform(-1, 1) * (grx + 36)
        y = gy + gry + rng.uniform(18, 40)
        if free(x, y, 14):
            objects.append(("tree", x, y, int(rng.random() < 0.4), int(rng.integers(80, 110))))
    if rng.random() < 0.5:
        (lx, ly), (dx, dy) = walk(line, rng.uniform(80, total - 40))
        side = rng.choice([-1, 1])
        x, y = lx + dy * side * (clear + 20), ly - dx * side * (clear + 20)
        if free(x, y, clear):
            objects.append(("pine", x, y, 0, int(rng.integers(70, 90))))
    for cx, cy, pts in ponds:
        for kk, (px, py) in enumerate(pts[::5]):
            ox, oy = px - cx, py - cy
            m = math.hypot(ox, oy) or 1
            xx, yy = px + ox / m * 2, py + oy / m * 2
            if 0 < xx < W and 0 < yy < L:
                objects.append(("grass", xx, yy, 0, int(rng.integers(60, 90))))
    for x, y in d.get("roos", []):
        objects.append(("animal", x, y, 0, 64))
    for x, y in d.get("houses", []):
        objects.append(("house", x, y, 0, 64))
    if d.get("mill"):
        x, y = d["mill"]
        objects.append(("landmark", x, y, 0, 64))
    objects.sort(key=lambda o: (o[2], o[1]))

    for s_, pts in shapes:
        lines.append("poly %s %s" % (s_, pts_text(pts)))
    lines.append("")
    for typ, x, y, v, sc in objects:
        x, y = clampxy((x, y), W, L)
        lines.append("object %s %s %s %d %d" % (typ, fmt(x), fmt(y), v, sc))
    head = ["# Banksia Heath, hole %d. Made by tools/mkbanksia.py; edit that and run it again." % n]
    (out / ("hole-%02d.txt" % n)).write_text("\n".join(head + lines) + "\n")
    return par, yards, [round(yards - a) for a in ahead], len(objects), len(shapes)


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "courses/banksiaheath"
    out.mkdir(parents=True, exist_ok=True)
    mesh_options = {1: " putting bunkers", 6: " putting", 7: " putting", 13: " hole-13-visual.txt hazards"}
    (out / "course.txt").write_text(
        "# Banksia Heath, an Australian sandbelt course among heath and gum trees.\n"
        "# Made by tools/mkbanksia.py.\n"
        "name Banksia Heath\n" + "".join("tee %s\n" % t for t in TEES) +
        "holes %d\n" % len(HOLES) +
        "scenery 4         # the sandbelt backdrop: low sandy hills in the heat haze\n"
        "wind 12           # a moderate breeze\n"
        "green 13          # firm, fast sandbelt greens\n" +
        "".join("terrain-mesh %d%s\n" % (n, mesh_options.get(n, ""))
                for n in range(1, len(HOLES) + 1)))
    source = ROOT / "courses/banksiaheath/hole-13-visual.txt"
    override = out / source.name
    if override.resolve() != source:
        override.write_bytes(source.read_bytes())
    total, pars, tot_t = 0, 0, [0, 0]
    for n, d in enumerate(HOLES, 1):
        par, yards, others, nobj, nsh = make_hole(n, d, out)
        total += yards
        pars += par
        tot_t = [a + b for a, b in zip(tot_t, others)]
        print("%2d %-14s par %d %3d yd (%s) %d objects, %d shapes" % (
            n, d["name"], par, yards, ", ".join(str(o) for o in others), nobj, nsh))
    print("par %d, %d yd (%s)" % (pars, total, ", ".join(str(o) for o in tot_t)))


if __name__ == "__main__":
    main()
