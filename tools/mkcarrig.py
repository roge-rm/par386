#!/usr/bin/env python3
"""Writes courses/carrighead/: course.txt, hole-NN.txt and hole-NN.png for
Carrig Head, an Irish clifftop links out along a headland to the lighthouse
at its tip and back.

Each hole is laid out below from a centre line, the green, bunkers, the sea
and a few features; shapes, objects and the heightmap come from that with
the hole's own random numbers, so running it again gives the same files.
Edit the HOLES table and run it again rather than editing the output.

The land is a clifftop about 6 yards above the sea, lower near the shore so
the sea shows from the fairways. Where the sea
comes in, the ground drops to it over a steep grassy face (deep rough) with
rocks at the bottom; the sea is water, so a ball in it takes a penalty drop
where it crossed the shore. Water on the edge of a hole's map carries on
past it, so the sea runs out to the horizon.

The sea in the table:
  ('coast', pts, corners): the shore line walked with the sea on the left,
      from the map's edge to its edge, then the map corners that close the sea.
  ('inlet', cx, cy, rx, ry, turn): a wobbly ellipse of sea, clipped to the map.

Usage: mkcarrig.py [COURSEDIR]
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkauldkirk import walk, length, clampxy, fmt, pts_text, ribbon, ellipse_pts, inside, vnoise   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CELL = 8                  # yards between heightmap points
STEP = 3 / 32             # yards per grey step, 23.9 yards from black to white
FINE = 2                  # yards between points of the working height grid
FACE = 10                 # yards from the water's edge to the top of the face
TOP = 6.0                 # height of the clifftop inland
EDGE = 3.0                # height at the top of the face
SHORE = 36                # yards over which the land comes down from TOP to EDGE

# Each hole: name, par, width, centre line from the back tee to the middle of
# the green, fairway half widths along it, the green (half sizes and turn in
# degrees), bunkers (x, y, r), the sea, how far the other tees are ahead
# (club, forward) and extras: dunes (x, y, rx, ry, height), sheep, houses,
# walls, the lighthouse, a raised tee (yards), extra length past the green.
HOLES = [
    # 1: a gentle start between dunes, cottages and a wall out of bounds on the right
    dict(name="The Well", par=4, W=220, line=[(110, 14), (114, 250), (126, 412)],
         fw=(38, 30), green=(17, 16, 0), bunkers=[(84, 236, 5), (150, 262, 5), (106, 394, 4.5), (146, 420, 4)],
         dunes=[(36, 200, 26, 60, 6), (40, 360, 22, 40, 5)], oob_right=(196, 0, 460),
         houses=[(206, 120, 0), (208, 300, 0)], wall_x=193, sheep=[(30, 120)], trees=[(186, 80), (184, 240)]),
    # 2: along the strand, bending left with the sea all down the left
    dict(name="Strand Head", par=5, W=240, line=[(160, 14), (150, 270), (120, 420), (104, 540)],
         fw=(36, 32, 28), green=(17, 18, -10), bunkers=[(186, 262, 5), (118, 300, 5), (152, 452, 4.5), (124, 528, 4)],
         sea=[('coast', [(80, 0), (78, 150), (96, 270), (80, 380), (78, 470), (58, 560), (56, 999)], [(0, 999), (0, 0)])],
         dunes=[(210, 180, 24, 60, 5), (200, 420, 24, 50, 6)], sheep=[(220, 340)]),
    # 3: over Gull Rock's inlet to a green on the far cliff
    dict(name="Gull Rock", par=3, W=200, line=[(104, 14), (102, 172)],
         fw=(0, 0), green=(16, 15, 0), bunkers=[(78, 168, 4.5), (128, 182, 4)],
         sea=[('inlet', 30, 100, 112, 38, 6)], ahead=(14, 32), raised=5, rocks_extra=[(70, 100, 255)]),
    # 4: bending right round the cliff, the sea down the right and a bite out of it at the corner
    dict(name="Curragh", par=4, W=240, line=[(80, 14), (92, 250), (146, 424)],
         fw=(36, 28), green=(16, 17, 15), bunkers=[(60, 240, 5), (126, 404, 4.5), (172, 416, 4)],
         sea=[('coast', [(186, 999), (184, 400), (150, 300), (172, 250), (176, 120), (200, 0)], [(240, 0), (240, 999)])],
         dunes=[(30, 260, 22, 70, 6)], gorse=6),
    # 5: a short par 4 through a gap in tall dunes
    dict(name="Fiddler's Gap", par=4, W=200, line=[(100, 14), (104, 180), (96, 306)],
         fw=(30, 24), green=(15, 16, 0), bunkers=[(74, 196, 5), (126, 210, 5), (82, 296, 4), (112, 318, 3.5)],
         dunes=[(36, 214, 34, 34, 9), (166, 222, 34, 32, 9), (40, 110, 22, 40, 5), (170, 300, 24, 30, 6)]),
    # 6: the drive carries an inlet coming in from the right
    dict(name="The Bawn", par=4, W=230, line=[(100, 14), (110, 260), (100, 446)],
         fw=(38, 32), green=(17, 17, 0), bunkers=[(146, 250, 5), (70, 284, 5), (84, 432, 4.5), (118, 460, 4)],
         sea=[('inlet', 196, 96, 140, 26, -4)], ahead=(22, 60), raised=4, sheep=[(30, 330), (36, 346)]),
    # 7: a par 3 along the cove, the sea close on the left of the green
    dict(name="Seal Cove", par=3, W=210, line=[(136, 14), (100, 186)],
         fw=(0, 0), green=(16, 16, -10), bunkers=[(128, 176, 4.5), (110, 206, 4)],
         sea=[('coast', [(40, 0), (58, 90), (52, 160), (64, 200), (60, 999)], [(0, 999), (0, 0)])],
         ahead=(14, 30), extra=30),
    # 8: the long one, dunes down both sides
    dict(name="The Long Reach", par=5, W=240, line=[(100, 14), (110, 270), (140, 430), (130, 568)],
         fw=(38, 34, 30), green=(18, 18, 0), bunkers=[(76, 258, 5), (142, 280, 5), (118, 350, 4.5), (170, 440, 5),
                                                     (110, 548, 4.5), (150, 584, 4)],
         sea=[('coast', [(224, 999), (230, 400), (226, 200), (240, 0)], [(240, 999)])],
         dunes=[(30, 300, 24, 90, 8), (40, 500, 22, 50, 6), (190, 150, 20, 50, 6)], gorse=6, sheep=[(36, 180)]),
    # 9: out to the point, the lighthouse beyond the green and the sea all round
    dict(name="Lantern", par=4, W=256, line=[(128, 14), (120, 262), (122, 408)],
         fw=(38, 30), green=(17, 16, 0), bunkers=[(88, 250, 5), (156, 270, 5), (96, 396, 4.5), (150, 404, 4)],
         sea=[('coast', [(0, 340), (40, 400), (80, 446), (130, 456), (190, 472), (256, 470)], [(256, 999), (0, 999)]),
              ('coast', [(256, 400), (232, 300), (240, 0)], [(256, 0)])],
         lighthouse=(156, 460), extra=40, ahead=(22, 50)),
    # 10: back from the point with the sea down the left
    dict(name="Keeper's Walk", par=4, W=240, line=[(140, 14), (132, 250), (112, 414)],
         fw=(38, 30), green=(17, 17, 0), bunkers=[(170, 238, 5), (100, 262, 5), (86, 402, 4.5), (140, 420, 4)],
         sea=[('coast', [(60, 0), (56, 200), (72, 330), (62, 999)], [(0, 999), (0, 0)])],
         dunes=[(210, 300, 22, 80, 7)], gorse=5),
    # 11: a short par 3 over a deep inlet, cliff to cliff
    dict(name="Puffin Ledge", par=3, W=200, line=[(112, 14), (100, 152)],
         fw=(0, 0), green=(15, 14, 10), bunkers=[(76, 150, 4), (118, 168, 3.5)],
         sea=[('inlet', 176, 90, 128, 35, 8), ('coast', [(200, 999), (174, 160), (186, 110)], [(200, 110)])],
         ahead=(12, 26), raised=5, rocks_extra=[(120, 90, 255), (150, 88, 230)]),
    # 12: a short par 4 to a green out on a promontory, the sea right and behind
    dict(name="Currach", par=4, W=220, line=[(76, 14), (92, 170), (126, 284)],
         fw=(32, 24), green=(15, 15, 20), bunkers=[(64, 176, 5), (110, 196, 4.5), (100, 280, 4)],
         sea=[('coast', [(0, 326), (40, 330), (100, 322), (150, 320), (164, 290), (176, 220), (220, 150)],
               [(220, 999), (0, 999)])],
         extra=40, ahead=(20, 46)),
    # 13: a par 5 with an inlet across in front of the second shot
    dict(name="Blackwater", par=5, W=240, line=[(110, 14), (118, 260), (130, 420), (118, 550)],
         fw=(38, 34, 30), green=(18, 17, 0), bunkers=[(150, 250, 5), (82, 270, 5), (96, 440, 5), (148, 532, 4.5),
                                                     (100, 560, 4)],
         sea=[('inlet', 30, 352, 150, 24, -6)], dunes=[(210, 400, 22, 80, 6)], sheep=[(220, 200), (214, 214)]),
    # 14: along the cliff with the sea down the right
    dict(name="Thrift Bank", par=4, W=230, line=[(100, 14), (110, 260), (96, 452)],
         fw=(38, 32), green=(17, 18, 0), bunkers=[(76, 248, 5), (140, 300, 5), (72, 442, 4.5), (120, 468, 4)],
         sea=[('coast', [(176, 999), (158, 400), (176, 290), (186, 140), (210, 0)], [(230, 0), (230, 999)])],
         dunes=[(30, 220, 24, 80, 6)], gorse=6),
    # 15: bending left along the shore, a cove to carry at the corner
    dict(name="Kelp Shore", par=4, W=240, line=[(170, 14), (156, 240), (90, 372)],
         fw=(36, 28), green=(16, 17, -15), bunkers=[(184, 228, 5), (126, 236, 4.5), (118, 368, 4)],
         sea=[('coast', [(30, 0), (34, 200), (96, 272), (52, 330), (46, 999)], [(0, 999), (0, 0)])],
         dunes=[(220, 300, 20, 70, 6)]),
    # 16: a par 3 from a high tee down to a green above the sea
    dict(name="Long Drop", par=3, W=200, line=[(100, 14), (104, 200)],
         fw=(0, 0), green=(17, 16, 0), bunkers=[(74, 192, 5), (132, 196, 4.5), (104, 222, 4)],
         sea=[('coast', [(0, 230), (60, 252), (130, 256), (200, 246)], [(200, 999), (0, 999)])],
         raised=4, extra=20, ahead=(16, 34)),
    # 17: the drive carries as much of an inlet on the right as you dare
    dict(name="The Leap", par=4, W=240, line=[(70, 14), (106, 250), (150, 440)],
         fw=(36, 30), green=(16, 18, 12), bunkers=[(64, 236, 5), (176, 410, 4.5), (130, 456, 4)],
         sea=[('inlet', 150, 130, 110, 22, 25)], ahead=(22, 56), gorse=6, raised=4),
    # 18: home past the dunes to the green under the cottages
    dict(name="Homeward", par=5, W=230, line=[(110, 14), (118, 262), (104, 430), (116, 540)],
         fw=(40, 36, 32), green=(19, 18, 0), bunkers=[(84, 248, 5), (146, 274, 5), (130, 462, 4.5), (96, 522, 4)],
         dunes=[(34, 160, 24, 70, 7), (200, 380, 20, 60, 5)], oob_right=(200, 260, 600),
         houses=[(80, 596, 0), (122, 602, 0), (160, 598, 0), (210, 360, 0), (212, 460, 0)], wall_x=197, wall_y=(260, 620),
         trees=[(60, 582), (190, 560)], extra=40),
]

TEES = ["Head", "Cliff", "Forward"]
# Each hole's map turned against the course in degrees, which moves the
# backdrop (tools/mkobj.py carrig_backdrop). Going out the far headland and
# its lighthouse are ahead; on the point the open Atlantic; coming back the
# mainland hills, with the sea on the seaward side.
TURNS = [35, 30, 20, 45, 30, 25, 15, 40, 95, 110, 100, -20, -10, -5, 5, -15, -30, -5]
PINS_ALONG = [(-0.45, -0.3), (0.4, -0.35), (0.35, 0.45), (-0.4, 0.4)]


def on_border(p, q, W, L):
    for k, lim in ((0, 0), (0, W), (1, 0), (1, L)):
        if abs(p[k] - lim) < 0.01 and abs(q[k] - lim) < 0.01:
            return True
    return False


def offset_line(pts, d, W, L):
    """The polyline moved d yards to its right (the land side)."""
    out = []
    n = len(pts)
    for i in range(n):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, n - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        m = math.hypot(dx, dy)
        nx, ny = dy / m, -dx / m
        out.append(clampxy((pts[i][0] + nx * d, pts[i][1] + ny * d), W, L))
    return out


def sea_shapes(sea, W, L, rng):
    """For each sea part: (the water polygon, the polygon reaching FACE
    yards further onto the land, the shore edges)."""
    out = []
    for s in sea:
        if s[0] == 'coast':
            pts, corners = s[1], s[2]
            corners = [clampxy(p, W, L) for p in corners]
            water = [clampxy(p, W, L) for p in pts] + corners
            face = offset_line(pts, FACE, W, L) + corners
            edges = list(zip(water[:len(pts) - 1], water[1:len(pts)]))
        else:
            _, cx, cy, rx, ry, turn = s
            r2 = np.random.default_rng(int(cx * 7 + cy))
            wob = r2.uniform(-0.07, 0.07, 28)
            pts = []
            fpts = []
            t = math.radians(turn)
            for i in range(28):
                a = 2 * math.pi * i / 28
                for lst, ex in ((pts, 0), (fpts, FACE)):
                    x, y = (rx + ex) * (1 + wob[i]) * math.cos(a), (ry + ex) * (1 + wob[i]) * math.sin(a)
                    x, y = x * math.cos(t) - y * math.sin(t), x * math.sin(t) + y * math.cos(t)
                    lst.append(clampxy((cx + x, cy + y), W, L))
            water, face = pts, fpts
            edges = [(pts[i], pts[(i + 1) % 28]) for i in range(28)]
        edges = [(p, q) for p, q in edges if not on_border(p, q, W, L)]
        out.append((water, face, edges))
    return out


def seg_dist(xs, ys, p, q):
    px, py = p
    qx, qy = q
    dx, dy = qx - px, qy - py
    m2 = dx * dx + dy * dy
    t = np.clip(((xs - px) * dx + (ys - py) * dy) / max(m2, 1e-9), 0, 1)
    return np.hypot(xs - (px + t * dx), ys - (py + t * dy))


def sea_dist(seas, xs, ys):
    """Yards from the nearest shore, below 0 in the sea; 999 with no sea."""
    d = np.full(xs.shape, 999.0)
    wet = np.zeros(xs.shape, dtype=bool)
    for water, _, edges in seas:
        wet |= inside(water, xs, ys)
        for p, q in edges:
            d = np.minimum(d, seg_dist(xs, ys, p, q))
    return np.where(wet, -d, d), wet


def make_hole(n, d, out):
    rng = np.random.default_rng(3000 + n)
    W, par, line = d["W"], d["par"], d["line"]
    gx, gy = line[-1]
    grx, gry, gturn = d["green"]
    L = int(gy + gry + 40 + d.get("extra", 0))
    if d.get("houses"):
        L = max(L, int(max(h[1] for h in d["houses"]) + 14))
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
    for px, py in PINS_ALONG:
        ox, oy = px * grx * 1.2, py * gry * 1.2
        lines.append("pin %s %s" % (fmt(gx + ox * math.cos(t) - oy * math.sin(t)),
                                    fmt(gy + ox * math.sin(t) + oy * math.cos(t))))
    lines.append("")

    # Rough and dune grass, first cut and fairway.
    for dx_, dy_, rx, ry, hh in d.get("dunes", []):
        shapes.append(("deep-rough", ellipse_pts(dx_, dy_, rx * 0.9, ry * 0.9, 0, 14, W, L, rng, 0.12)))
    tee_gap = 45 if par > 3 else 0
    green_front = length(line) - gry - 4
    if par > 3:
        hw = list(d["fw"])
        shapes.append(("first-cut", ribbon(line, tee_gap - 6, green_front + 6, hw, 5, rng, W, L)))
        shapes.append(("fairway", ribbon(line, tee_gap, green_front + 2, hw, 0, rng, W, L)))
    else:
        shapes.append(("first-cut", ellipse_pts(gx, gy - gry - 6, grx + 4, 12, gturn, 12, W, L)))

    # The cliff faces, then the sea.
    for water, face, _ in seas:
        shapes.append(("deep-rough", face))
    for water, face, _ in seas:
        shapes.append(("water", water))

    shapes.append(("fringe", ellipse_pts(gx, gy, grx + 3.5, gry + 3.5, gturn, 20, W, L)))
    shapes.append(("green", ellipse_pts(gx, gy, grx, gry, gturn, 20, W, L)))
    for bx, by, r in d["bunkers"]:
        shapes.append(("bunker", ellipse_pts(bx, by, r * rng.uniform(1.0, 1.3), r, rng.uniform(0, 180), 12, W, L,
                                             rng, 0.12)))
    for k, (x, y) in enumerate(tee_pts):
        hw = 7 if k == 0 else 5
        shapes.append(("tee", [clampxy((x - hw, y - 6), W, L), clampxy((x + hw, y - 6), W, L),
                               clampxy((x + hw, y + 6), W, L), clampxy((x - hw, y + 6), W, L)]))
    oobr = d.get("oob_right")
    if oobr:
        x0, y0, y1 = oobr
        shapes.append(("oob", [(x0, y0), (W, y0), (W, min(y1, L)), (x0, min(y1, L))]))

    # Heights on a fine grid first: the clifftop with humps and dunes, calmer
    # on fairways, greens tilted up to the back; lower towards the shore so
    # the sea shows over the edge, then down the face to the sea; tees level.
    fw_, fh_ = W // FINE + 1, L // FINE + 1
    ys, xs = np.mgrid[0:fh_, 0:fw_] * float(FINE)
    hum = vnoise((fh_, fw_), 9.0, rng) * 2.0 + vnoise((fh_, fw_), 4.0, rng) * 1.0 + vnoise((fh_, fw_), 18.0, rng) * 2.6
    z = TOP + hum
    for dx_, dy_, rx, ry, hh in d.get("dunes", []):
        r = np.hypot((xs - dx_) / rx, (ys - dy_) / ry)
        z += np.clip(1 - r, 0, 1) ** 1.3 * hh * (0.75 + 0.5 * (vnoise((fh_, fw_), 6.0, rng) + 0.5))
    smooth = TOP + vnoise((fh_, fw_), 30.0, rng) * 2.4
    fw_mask = np.zeros(z.shape, dtype=bool)
    for name, pts in shapes:
        if name in ("fairway", "first-cut", "tee"):
            fw_mask |= inside(pts, xs, ys)
    z = smooth + (z - smooth) * np.where(fw_mask, 0.45, 1.0)
    dist, wet = sea_dist(seas, xs, ys)

    def shore(a):
        s_ = np.clip((dist - FACE) / SHORE, 0, 1)
        s_ = s_ * s_ * (3 - 2 * s_)
        a = EDGE + (a - EDGE) * s_
        f = np.clip((dist - 2) / (FACE - 4), 0, 1)
        return a * (f * f * (3 - 2 * f))
    z = shore(z)
    smooth = shore(smooth)
    # the green: a plane through its middle's height, tilting up to the back
    gd = np.hypot((xs - gx) / (grx + 14), (ys - gy) / (gry + 14))
    gw = np.clip((1.3 - gd) / 0.5, 0, 1)
    g0 = smooth[int(gy / FINE), int(gx / FINE)]
    gz = g0 + (ys - gy) * 0.02 + (xs - gx) * 0.008 * rng.choice([-1, 1])
    z = z * (1 - gw) + gz * gw
    for k, (x, y) in enumerate(tee_pts):
        tw = np.clip(1.6 - np.hypot(xs - x, ys - y) / 14, 0, 1)
        lift = d.get("raised", 0) if k == 0 else d.get("raised", 0) * 0.5
        z = z * (1 - tw) + (smooth + 0.4 + lift) * tw
    z = np.where(wet, -0.2, z)
    # sample every CELL yards; near the sea the lowest point within 4 yards,
    # so the water stays flat
    hw_, hh_ = W // CELL + 2, L // CELL + 2
    zp = np.pad(z, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    dp = np.pad(dist, ((2, 2 + CELL), (2, 2 + CELL)), mode='edge')
    hm = np.zeros((hh_, hw_))
    k = CELL // FINE
    for j in range(hh_):
        for i in range(hw_):
            jj, ii = j * k + 2, i * k + 2
            near = dp[jj - 2:jj + 3, ii - 2:ii + 3].min() < FACE
            hm[j, i] = zp[jj - 2:jj + 3, ii - 2:ii + 3].min() if near else zp[jj, ii]
    grey = np.clip(np.round(hm / STEP), 0, 255).astype(np.uint8)
    Image.fromarray(grey[::-1]).save(out / ("hole-%02d.png" % n))

    def solid(x, y):
        for s, p in shapes:
            if s in ("first-cut", "fairway", "green", "fringe", "tee", "bunker", "water", "oob"):
                if inside(p, np.array(x), np.array(y)):
                    return True
        return False

    def sea_at(x, y):
        dd, _ = sea_dist(seas, np.array([x]), np.array([y]))
        return dd[0]

    def off_line(x, y):
        best = 1e9
        for k2 in range(0, int(length(line)), 10):
            (lx, ly), _ = walk(line, k2)
            best = min(best, math.hypot(x - lx, y - ly))
        return best

    # Rocks along the foot of each cliff, and boulders on the face.
    for water, face, edges in seas:
        for p, q in edges:
            seg = math.hypot(q[0] - p[0], q[1] - p[1])
            nx, ny = (q[1] - p[1]) / seg, -(q[0] - p[0]) / seg   # to the land
            at = rng.uniform(0, 8)
            while at < seg:
                tt = at / seg
                at += rng.uniform(7, 19)
                if rng.random() < 0.2:
                    continue
                off = rng.uniform(0.5, 6)
                x = p[0] + (q[0] - p[0]) * tt + nx * off
                y = p[1] + (q[1] - p[1]) * tt + ny * off
                if 0 < x < W and 0 < y < L and sea_at(x, y) > 0.3:
                    objects.append(("rock", x, y, 1, int(rng.integers(110, 255))))
                if rng.random() < 0.4:
                    x2 = p[0] + (q[0] - p[0]) * tt + nx * rng.uniform(6, 10)
                    y2 = p[1] + (q[1] - p[1]) * tt + ny * rng.uniform(6, 10)
                    if 0 < x2 < W and 0 < y2 < L and sea_at(x2, y2) > 4 and not solid(x2, y2):
                        objects.append(("rock", x2, y2, 0, int(rng.integers(60, 140))))
    for x, y, sc in d.get("rocks_extra", []):
        objects.append(("rock", x, y, 1, sc))

    # Marram on the dunes, gorse in clumps off the line, hawthorns and sheep.
    for dx_, dy_, rx, ry, hh in d.get("dunes", []):
        for kk in range(int(rx * ry / 120)):
            x = dx_ + rng.uniform(-rx, rx) * 0.8
            y = dy_ + rng.uniform(-ry, ry) * 0.8
            if 0 < x < W and 0 < y < L and not solid(x, y) and off_line(x, y) > 18 and sea_at(x, y) > FACE:
                objects.append(("grass", x, y, 0, int(rng.integers(70, 120))))
    clumps = d.get("gorse", 3 if par > 3 else 2)
    for c in range(clumps):
        for tries in range(40):
            cy = rng.uniform(60, L - 30)
            (lx, _), _ = walk(line, max(cy - 14, 0))
            cx = lx + rng.choice([-1, 1]) * rng.uniform(32, 60)
            if 4 < cx < W - 4 and not solid(cx, cy) and off_line(cx, cy) > 28 and sea_at(cx, cy) > FACE + 4:
                break
        else:
            continue
        for kk in range(int(rng.integers(5, 10))):
            x = cx + rng.normal(0, 6)
            y = cy + rng.normal(0, 8)
            if 2 < x < W - 2 and 2 < y < L - 2 and not solid(x, y) and off_line(x, y) > 22 and sea_at(x, y) > FACE:
                objects.append(("gorse" if kk % 3 else "bush", x, y, 0, int(rng.integers(60, 96))))
    for x, y in d.get("trees", []):
        objects.append(("tree", x, y, 0, int(rng.integers(56, 76))))
    for x, y in d.get("sheep", []):
        objects.append(("animal", x, y, 0, 64))
    for x, y, v in d.get("houses", []):
        objects.append(("house", x, y, v, 64))
    if d.get("wall_x"):
        y0, y1 = d.get("wall_y", (20, L - 10))
        for k2 in range(int((y1 - y0) / 4)):
            objects.append(("wall", d["wall_x"], y0 + k2 * 4 + 2, 0, 64))
    if d.get("lighthouse"):
        lx, ly = d["lighthouse"]
        objects.append(("landmark", lx, ly, 0, 64))
        for x, y in ((lx - 10, ly - 4), (lx + 9, ly + 2), (lx + 12, ly - 8)):
            objects.append(("rock", x, y, 0, 110))
    objects.sort(key=lambda o: (o[2], o[1]))

    for s, pts in shapes:
        lines.append("poly %s %s" % (s, pts_text(pts)))
    lines.append("")
    for typ, x, y, v, sc in objects:
        x, y = clampxy((x, y), W, L)
        lines.append("object %s %s %s %d %d" % (typ, fmt(x), fmt(y), v, sc))
    head = ["# Carrig Head, hole %d. Made by tools/mkcarrig.py; edit that and run it again." % n]
    (out / ("hole-%02d.txt" % n)).write_text("\n".join(head + lines) + "\n")
    return par, yards, [round(yards - a) for a in ahead]


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "courses/carrighead"
    out.mkdir(parents=True, exist_ok=True)
    (out / "course.txt").write_text(
        "# Carrig Head, an Irish clifftop links out along a headland and back.\n"
        "# Made by tools/mkcarrig.py.\n"
        "name Carrig Head\n" + "".join("tee %s\n" % t for t in TEES) +
        "holes %d\n" % len(HOLES) +
        "scenery 2         # the clifftop backdrop: the Atlantic, the far headland and its lighthouse\n"
        "wind 15           # off the Atlantic, harder than the usual 10\n"
        "terrain-mesh 11 putting bunkers\n")
    total, pars, tot_t = 0, 0, [0, 0]
    for n, d in enumerate(HOLES, 1):
        par, yards, others = make_hole(n, d, out)
        total += yards
        pars += par
        tot_t = [a + b for a, b in zip(tot_t, others)]
        print("%2d %-16s par %d %3d yd (%s)" % (n, d["name"], par, yards, ", ".join(str(o) for o in others)))
    print("par %d, %d yd (%s)" % (pars, total, ", ".join(str(o) for o in tot_t)))


if __name__ == "__main__":
    main()
