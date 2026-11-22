#!/usr/bin/env python3
"""Build the hole sources for Bracken Mere, Saffron Dunes and Falesia Grove.

The routing tables give centre lines, ground heights at their stations,
fairway half widths and hazards in yards. Optional hazards list the surface,
centre, half sizes and angle. The last pair of bunker positions is measured
from the green centre. Edit the tables and run this script to change a hole.
"""
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

from mkauldkirk import clampxy, ellipse_pts, fmt, length, pts_text, ribbon, vnoise, walk
from mksakura import line_param, smoothstep

ROOT = Path(__file__).resolve().parent.parent
CELL = 8
STEP = 0.125
WIDTH = 244
TEES = ("Medal", "Club", "Forward")


def hole(name, par, xs, ys, rises, widths, greenside, hazards=(), green=(18, 17, 0)):
    line = list(zip(xs, (18, *ys)))
    return dict(name=name, par=par, line=line, rises=rises, widths=widths, greenside=greenside,
                hazards=hazards, green=green)


def sand(x, y, rx, ry, angle=0):
    return ("bunker", x, y, rx, ry, angle)


def pool(x, y, rx, ry, angle=0):
    return ("water", x, y, rx, ry, angle)


HEATH = [
    hole("Birch Gate", 4, (105, 119, 128), (240, 383), (9, 7, 9), (27, 23), ((-21, -9),), (sand(145, 235, 11, 16, -22),)),
    hole("Heather Rise", 4, (136, 117, 89), (235, 363), (8, 10, 14), (24, 21), ((20, 6), (-17, -14)),
         (sand(96, 226, 13, 9, 18),), (16, 17, -15)),
    hole("The Warren", 3, (111, 137), (172,), (10, 8), (23,), ((-16, -15), (21, 3)), green=(19, 15, 20)),
    hole("Ridge Walk", 5, (92, 103, 148, 130), (242, 418, 532), (9, 10, 6, 8), (29, 22, 26), ((24, -6),),
         (sand(126, 246, 12, 9), sand(130, 382, 9, 19, -20))),
    hole("Two Hollows", 4, (135, 130, 104), (232, 407), (12, 7, 9), (26, 24), ((-22, 0), (15, -16)),
         (sand(147, 245, 10, 16), sand(104, 211, 10, 7))),
    hole("Mere Corner", 4, (105, 137, 150), (246, 362), (11, 7, 9), (26, 23), ((21, -4),),
         (pool(112, 302, 21, 36, -37), sand(161, 247, 8, 13))),
    hole("Foxglove", 3, (104, 91), (219,), (11, 13), (23,), ((-22, 5), (17, -8)),
         (sand(109, 194, 9, 8, -25),), (20, 15, -28)),
    hole("King's Ride", 5, (136, 123, 87, 94), (236, 403, 530), (9, 8, 11, 13), (27, 24, 22), ((-20, -9),),
         (sand(103, 253, 10, 15, 10), sand(110, 405, 10, 14, -25))),
    hole("Beech End", 4, (101, 122, 140), (260, 434), (9, 7, 10), (23, 24), ((-17, -15), (22, 8)),
         (sand(145, 252, 12, 20, -14),), (20, 18, 12)),
    hole("Saddle", 4, (115, 123, 111), (213, 342), (10, 8, 10), (23, 26), ((19, -12),),
         (sand(98, 203, 12, 7), sand(150, 229, 10, 10))),
    hole("Three Birches", 4, (104, 127, 157), (230, 394), (8, 13, 8), (25, 24), ((21, 4), (-20, -10)),
         (sand(148, 219, 12, 15, -18),)),
    hole("Shallow Dene", 3, (109, 121), (151,), (11, 7), (25,), ((-19, -11), (21, -8)),
         (pool(154, 112, 16, 23, -22),), (16, 14, 4)),
    hole("Keeper's Way", 4, (139, 117, 91), (238, 421), (11, 9, 12), (25, 27), ((-20, 7), (22, -7)),
         (sand(95, 230, 9, 15, 5), sand(138, 264, 11, 10))),
    hole("Cloud Bank", 5, (97, 111, 149, 132), (243, 419, 550), (8, 12, 8, 10), (30, 22, 24), ((-21, -9), (22, 7)),
         (sand(138, 232, 11, 15), sand(123, 409, 12, 8, 10))),
    hole("Blackthorn", 4, (143, 120, 129), (236, 372), (13, 8, 8), (22, 25), ((-21, 5),),
         (sand(99, 212, 13, 8), sand(151, 350, 8, 11, 30))),
    hole("Teal Pool", 3, (101, 125), (188,), (10, 9), (23,), ((22, 4),),
         (pool(101, 143, 31, 17, -10),), (20, 14, 18)),
    hole("Gorse Reach", 4, (101, 108, 138), (250, 413), (9, 11, 13), (25, 25), ((-19, -11), (23, 4)),
         (sand(84, 244, 11, 15), sand(136, 259, 10, 9))),
    hole("Bracken House", 5, (140, 127, 101, 120), (249, 424, 542), (12, 9, 8, 12), (29, 25, 23), ((-20, 8), (21, -6)),
         (sand(102, 234, 10, 14, 12), sand(126, 422, 11, 10, -10))),
]

DESERT = [
    hole("Dune Gate", 4, (119, 119, 139), (240, 382), (12, 9, 10), (28, 25), ((-24, -4),),
         (sand(97, 220, 14, 25, 12),)),
    hole("Caravan", 5, (103, 115, 151, 151), (242, 410, 539), (11, 13, 8, 9), (28, 24, 28), ((19, -12),),
         (sand(142, 250, 20, 9, 30), sand(124, 389, 14, 21, -12))),
    hole("Mirage", 3, (103, 136), (161,), (11, 8), (24,), ((20, -7),),
         (pool(108, 121, 33, 17, -23),), (19, 17, 12)),
    hole("Windward", 4, (132, 111, 92), (233, 425), (11, 9, 14), (26, 24), ((-21, -7), (18, 9)),
         (sand(133, 215, 11, 17, -14), sand(89, 270, 12, 15))),
    hole("Long Shade", 4, (101, 129, 157), (212, 329), (12, 10, 10), (26, 21), ((22, -8),),
         (pool(120, 284, 23, 17, -25), sand(152, 215, 11, 12))),
    hole("Saffron Bank", 4, (116, 109, 124), (230, 394), (12, 8, 12), (24, 27), ((-18, -10),),
         (sand(91, 207, 16, 20), sand(134, 253, 12, 24, -15))),
    hole("Palm Well", 3, (113, 103), (196,), (13, 9), (25,), ((-21, -8), (15, -13)),
         (pool(140, 147, 21, 24, 8),), (20, 18, -15)),
    hole("Sand Sea", 5, (141, 125, 88, 113), (241, 417, 556), (13, 10, 8, 11), (27, 25, 25), ((-20, -9),),
         (sand(146, 234, 20, 12), sand(118, 402, 18, 9, 17))),
    hole("The Cistern", 4, (105, 129, 149), (233, 390), (11, 9, 10), (25, 24), ((22, -4),),
         (pool(111, 327, 23, 29, -32), sand(151, 252, 12, 10))),
    hole("Red Gully", 4, (146, 118, 99), (252, 427), (14, 8, 11), (30, 24), ((-20, -10),),
         (sand(162, 204, 16, 20), sand(86, 272, 16, 10, 27))),
    hole("Salt Shore", 3, (94, 127), (225,), (12, 8), (25,), ((19, -14),),
         (pool(96, 177, 28, 28, -13), sand(156, 190, 8, 17, -24)), (23, 16, -18)),
    hole("Trade Wind", 4, (138, 133, 155), (218, 345), (9, 13, 11), (24, 25), ((-20, 0), (21, -8)),
         (sand(109, 226, 16, 10, 19),)),
    hole("Dry Wash", 5, (106, 105, 138, 145), (224, 392, 526), (11, 10, 14, 10), (27, 26, 23), ((22, -8),),
         (sand(138, 218, 17, 10), sand(102, 384, 17, 20, 30))),
    hole("Date Line", 4, (126, 107, 126), (238, 400), (10, 12, 10), (26, 23), ((-19, -12), (20, -8)),
         (sand(81, 243, 12, 22, -10),)),
    hole("Oasis Fork", 4, (117, 112, 149), (237, 388), (12, 10, 9), (27, 24), ((21, -6),),
         (pool(146, 261, 18, 41, -29), sand(83, 221, 13, 11))),
    hole("Still Water", 3, (117, 99), (173,), (12, 9), (25,), ((-21, 4),),
         (pool(137, 130, 28, 19, 20),), (18, 15, 26)),
    hole("High Dune", 4, (108, 137, 123), (244, 409), (10, 12, 16), (28, 24), ((-19, -9), (20, 7)),
         (sand(159, 234, 11, 22, -15),)),
    hole("Last Lantern", 5, (138, 116, 87, 111), (240, 405, 543), (13, 10, 12, 11), (28, 25, 25), ((-16, -15),),
         (sand(141, 225, 11, 22, 25), pool(123, 459, 21, 25, -35))),
]

GROVE = [
    hole("Cork Gate", 4, (133, 117, 103), (242, 382), (15, 9, 7), (27, 26), ((-20, 0), (18, -10)),
         (sand(142, 235, 10, 14),)),
    hole("Stone Fold", 4, (104, 127, 141), (232, 398), (8, 12, 15), (27, 23), ((-20, -7), (21, -6)),
         (sand(151, 227, 11, 15, 30),)),
    hole("Olive Court", 3, (126, 111), (174,), (12, 14), (23,), ((-22, 2), (20, -11)), green=(17, 15, 14)),
    hole("White Track", 5, (98, 112, 147, 130), (239, 411, 544), (12, 9, 13, 11), (28, 24, 27), ((22, -8),),
         (sand(138, 235, 12, 11), sand(116, 373, 13, 13, -25))),
    hole("Down to the Sea", 4, (139, 121, 95), (231, 373), (18, 11, 6), (25, 24), ((-20, -7), (21, 5)),
         (sand(96, 241, 10, 18),)),
    hole("Ravine", 3, (102, 119), (195,), (16, 10), (25,), ((22, -9),),
         (sand(106, 139, 42, 16, 14),), (21, 16, 4)),
    hole("Terrace", 4, (99, 117, 138), (234, 410), (7, 12, 17), (27, 24), ((-19, -10),),
         (sand(142, 227, 13, 15, -22), sand(103, 326, 16, 8))),
    hole("The Millrace", 5, (145, 131, 101, 115), (245, 416, 548), (11, 8, 8, 12), (28, 25, 24), ((23, -8),),
         (pool(152, 351, 19, 46, 18), sand(82, 406, 10, 14))),
    hole("Veranda", 4, (100, 118, 127), (228, 364), (8, 10, 14), (24, 23), ((-21, 3), (17, -15)),
         (sand(94, 229, 12, 9),)),
    hole("Juniper Walk", 4, (131, 114, 102), (238, 390), (12, 10, 8), (27, 26), ((-22, -8),),
         (sand(90, 230, 11, 14, -15),)),
    hole("Red Rock", 4, (101, 116, 151), (233, 402), (13, 8, 12), (28, 24), ((21, -8), (-20, 2)),
         (sand(140, 219, 12, 20, 25),)),
    hole("Pine Perch", 3, (121, 104), (227,), (10, 14), (26,), ((-21, -7), (18, -11)),
         (sand(135, 196, 11, 10),), (20, 19, -19)),
    hole("Hillside Road", 5, (139, 124, 103, 112), (243, 419, 539), (16, 11, 8, 9), (28, 25, 26),
         ((-21, -8), (19, 7)), (sand(146, 217, 12, 19), sand(79, 386, 11, 14, 25))),
    hole("Cistern Bend", 4, (105, 124, 140), (218, 340), (10, 8, 10), (25, 24), ((23, -8),),
         (pool(102, 285, 16, 27, -17), sand(145, 213, 12, 10))),
    hole("Three Walls", 4, (136, 128, 94), (249, 421), (9, 13, 16), (24, 25), ((-21, -4), (19, -13)),
         (sand(103, 241, 10, 13, 12), sand(145, 283, 11, 16, -23))),
    hole("Orchard Gap", 3, (114, 135), (156,), (14, 10), (23,), ((18, -14), (-20, 3)), green=(18, 15, 24)),
    hole("Grove Edge", 4, (126, 111, 123), (236, 397), (13, 9, 12), (24, 23), ((-21, -8),),
         (sand(138, 216, 13, 13), sand(88, 262, 10, 11))),
    hole("Casa Alta", 5, (105, 121, 153, 128), (257, 435, 548), (10, 13, 7, 14), (29, 26, 27), ((20, -10),),
         (sand(146, 242, 13, 15, 17), sand(127, 425, 12, 10, -15))),
]

COURSES = {
    "brackenmere": ("Bracken Mere", 7, 11, 10, HEATH),
    "saffrondunes": ("Saffron Dunes", 8, 12, 11, DESERT),
    "falesiagrove": ("Falesia Grove", 9, 9, 10, GROVE),
}
CURVED_HOLES = {"brackenmere": 3, "saffrondunes": 8, "falesiagrove": 5}


def shape(cx, cy, rx, ry, turn, limit, rng, n=20, wobble=0.11):
    return ellipse_pts(cx, cy, rx, ry, turn, n, WIDTH, limit, rng, wobble)


def marks(n, style, line, total, limit, hazards, rng):
    """Place vegetation clear of the cut and low ground close to water."""
    sx, sy = line[-1]
    sample_count = 10 if style == 8 else 25
    for k in range(sample_count):
        d = rng.uniform(35, total + 15)
        (x, y), (dx, dy) = walk(line, d)
        side = -1 if k & 1 else 1
        margin = rng.uniform(35, 88) * side
        x, y = x + dy * margin, y - dx * margin
        if not (10 < x < WIDTH - 10 and 14 < y < limit - 10):
            continue
        if any(math.hypot((x - hx) / (rx + 3), (y - hy) / (ry + 3)) <= 1.1
               for surface, hx, hy, rx, ry, _ in hazards if surface == "water"):
            continue
        if style == 7:
            typ = "bush" if k % 3 else "tree"
        elif style == 8:
            typ = "rock" if k % 3 == 0 else "palm"
        else:
            typ = "tree" if k % 3 else "pine"
        yield typ, x, y, int(rng.integers(40, 77))
    # A sparse group at the tee sets the scene before the first landing.
    typ = "palm" if style == 8 else "tree"
    tx, ty = line[0]
    for k, (dx, dy) in enumerate(((-42, 45), (44, 62), (-53, 67))):
        yield typ, tx + dx, ty + dy, 64 - 4 * k
    if style == 7:
        for dx, dy, size in ((-31, 34, 72), (-33, 39, 65), (36, 44, 70), (39, 48, 60)):
            yield "bush", tx + dx, ty + dy, size
    for surface, x, y, rx, ry, _ in hazards:
        if style == 8 and surface == "water":
            yield "palm", min(WIDTH - 8, x + rx + 6), min(limit - 8, y + ry + 4), 68
    if n in (9, 18):
        yield "house", min(WIDTH - 20, sx + 30), sy + 27, 82
    if style == 9 and n in (2, 7, 15):
        for y in range(int(sy) - 50, int(sy) - 10, 7):
            yield "wall", sx - 31, y, 64


def ground(style, line, rises, green, tees, hazards, limit, rng):
    """Smooth grades through the fairway with flatter tees and greens."""
    width_cells = math.ceil(WIDTH / CELL) + 1
    length_cells = math.ceil(limit / CELL) + 1
    yy, xx = np.mgrid[:length_cells, :width_cells] * CELL
    along, across = line_param(line, xx, yy)
    stations = [length(line[:i + 1]) for i in range(len(line))]
    ht = np.interp(along, stations, rises)
    gx, gy = line[-1]
    gyards = math.hypot(*green[:2])
    broad = vnoise(xx.shape, 5, rng)
    detail = vnoise(xx.shape, 2, rng)
    outer = smoothstep((across - 19) / 40)
    if style == 7:
        ht += outer * (1.6 + 5.5 * broad) + 0.8 * detail
    elif style == 8:
        ht += outer * (3.1 + 6.8 * broad + 1.8 * np.sin(0.06 * yy + 0.07 * xx)) + 0.6 * detail
    else:
        ht += outer * (6.6 * broad + np.clip((xx - gx) / 20, -3, 3)) + 1.1 * detail
    for surface, x, y, rx, ry, angle in hazards:
        a = math.radians(angle)
        dx, dy = xx - x, yy - y
        dd = np.hypot((dx * math.cos(a) + dy * math.sin(a)) / (rx + 3),
                      (-dx * math.sin(a) + dy * math.cos(a)) / (ry + 3))
        ht -= (2.5 if surface == "water" else 1.4) * smoothstep((1.3 - dd) * 2.0)
    # Keep distortion out of the putting surface while blending its shoulders.
    plane = rises[-1] + (gy - yy) * 0.014 + (gx - xx) * (0.008 if style == 9 else 0.004)
    blend = smoothstep((np.hypot(xx - gx, yy - gy) - gyards * 0.75) / 14)
    ht = plane * (1 - blend) + ht * blend
    for x, y in tees:
        blend = smoothstep((np.hypot(xx - x, yy - y) - 7) / 10)
        ht = np.interp(math.hypot(x - line[0][0], y - line[0][1]), stations, rises) * (1 - blend) + ht * blend
    return np.clip(np.rint(ht / STEP), 0, 255).astype(np.uint8)[::-1]


def write_hole(folder, n, style, spec):
    rng = np.random.default_rng(style * 1000 + n)
    line, par = spec["line"], spec["par"]
    gx, gy = line[-1]
    limit = min(640, gy + 59)
    total = length(line)
    grx, gry, turn = spec["green"]
    angle = math.radians(turn)
    aim_x, aim_y = line[1]
    lines = [
        f"name {spec['name']}", f"par {par}", f"size {WIDTH} {limit}",
        f"heightmap hole-{n:02d}.png {CELL}", f"height 0 {STEP}",
        "base bunker" if style == 8 else "base deep-rough",
        f"aim {aim_x} {aim_y}", f"turn {(n * 53 + style * 23) % 360}", "",
    ]
    tees = []
    for k, d in enumerate((0, 16, 32) if par == 3 else (0, 25, 53)):
        (x, y), (dx, dy) = walk(line, d)
        offset = (0, -7, 8)[k] * (-1 if n % 2 else 1)
        x, y = clampxy((x + dy * offset, y - dx * offset), WIDTH, limit)
        tees.append((x, y))
        lines.append(f"tee {TEES[k]} {fmt(x)} {fmt(y)} {round(total - d)}")
    for x, y in ((-0.4, -0.34), (0.38, -0.3), (0.34, 0.4), (-0.38, 0.35)):
        dx, dy = x * grx, y * gry
        lines.append(f"pin {fmt(gx + dx * math.cos(angle) - dy * math.sin(angle))} "
                     f"{fmt(gy + dx * math.sin(angle) + dy * math.cos(angle))}")
    lines.append("")
    widths = spec["widths"]
    shapes = []
    if style == 8:
        # Irrigated tees and landing areas have separate edges in the waste sand.
        tx, ty = line[0]
        _, fy = tees[-1]
        shapes.append(("rough", shape(tx, (ty + fy) / 2, 18, (fy - ty) / 2 + 9,
                                      turn / 3, limit, rng, n=18)))
        if par > 3:
            shapes.append(("rough", ribbon(line, 56, total + 12, widths, 9, rng, WIDTH, limit)))
    else:
        shapes.append(("rough", ribbon(line, 0, total + 12, widths, 18, rng, WIDTH, limit)))
    shapes.append(("rough", shape(gx, gy, grx + 14, gry + 13, turn, limit, rng, n=18)))
    if par > 3:
        # Three nested cuts preserve a useful target at each bend.
        for surface, extra in (("first-cut", 4), ("fairway", 0)):
            shapes.append((surface, ribbon(line, 61 - extra, total - gry + extra, widths, extra, rng, WIDTH, limit)))
    else:
        shapes.append(("fairway", shape(gx, gy - gry - 8, 19, 15, turn, limit, rng, n=16)))
    shapes.extend([
        ("fringe", shape(gx, gy, grx + 3, gry + 3, turn, limit, rng, wobble=0.03)),
        ("green", shape(gx, gy, grx, gry, turn, limit, rng, wobble=0.03)),
    ])
    hazards = list(spec["hazards"])
    for i, (dx, dy) in enumerate(spec["greenside"]):
        hazards.append(sand(gx + dx, gy + dy, 7 + (n + i) % 4, 5 + i, (42 + n * 13 + i * 50) % 180))
    for surface, x, y, rx, ry, tr in hazards:
        shapes.append((surface, shape(x, y, rx, ry, tr, limit, rng, n=24 if surface == "water" else 16,
                                      wobble=0.13 if surface == "water" else 0.19)))
    for surface, points in shapes:
        lines.append(f"poly {surface} {pts_text(points)}")
    for x, y in tees:
        lines.append(f"rect tee {fmt(x - 6)} {fmt(y - 5)} {fmt(x + 6)} {fmt(y + 5)}")
    for typ, x, y, size in marks(n, style, line, total, limit, hazards, rng):
        lines.append(f"object {typ} {fmt(x)} {fmt(y)} 0 {size}")
    Image.fromarray(ground(style, line, spec["rises"], spec["green"], tees, hazards, limit, rng)).save(
        folder / f"hole-{n:02d}.png")
    (folder / f"hole-{n:02d}.txt").write_text("\n".join(lines) + "\n")


def main():
    for slug in sys.argv[1:] or COURSES:
        name, scenery, wind, green, holes = COURSES[slug]
        folder = ROOT / "courses" / slug
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "course.txt").write_text(
            f"name {name}\n" + "".join(f"tee {tee}\n" for tee in TEES)
            + f"holes {len(holes)}\nscenery {scenery}\nwind {wind}\ngreen {green}\n"
            + "terrain-mesh all putting bunkers water\n"
            + f"terrain-mesh {CURVED_HOLES[slug]} putting bunkers\n")
        for n, spec in enumerate(holes, 1):
            write_hole(folder, n, scenery, spec)


if __name__ == "__main__":
    main()
