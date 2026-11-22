#!/usr/bin/env python3
# Par 386 course compiler. The formats are in doc/course-format.md.
#
#   coursec.py build COURSEDIR OUT.pdb   compile course.txt and hole-NN.txt into a PDB
#   coursec.py build --green-mesh COURSEDIR OUT.pdb   add compiled putting materials
#   coursec.py build --green-geometry COURSEDIR OUT.pdb   add indexed putting triangles
#   coursec.py build --visual-hole N VISUAL.txt COURSEDIR OUT.pdb   add visual contours to one hole
#   coursec.py build --baked-sand N VISUAL.txt COURSEDIR OUT.pdb   add visual contours with sand slopes
#   coursec.py dump PDB OUTDIR           write a PDB back out as text and PNGs
#
# Needs Pillow for the heightmap PNGs.
import math
import os
import struct
import sys
import time
from bisect import bisect_left, bisect_right
from decimal import Decimal

from PIL import Image
import terrainmesh

COURSE_VERSION = 1    # 2 with a scenery line, 3 with a wind line, 4 with a green line
HOLE_VERSION = 1
HOLE_MESH_VERSION = 2
HOLE_GEOMETRY_VERSION = 3
HOLE_VISUAL_VERSION = 4
HOLE_SAND_VERSION = 5
HOLE_CURVE_VERSION = 6
MAX_RECORD = 16384
PUTTING_CURVE_RADIUS = 55 * 4  # quarter yards beyond the pins
MAX_CURVE_VERTS = 128
MAX_CURVE_VERTS_TOTAL = 1024
GREEN_MESH_RADIUS = 26 * 4    # quarter yards around each pin
MAX_GREEN_VERTICES = 512
MAX_GREEN_TRIANGLES = 768
MAX_VISUAL_SHAPES = 32
MAX_VISUAL_VERTICES = 768
MAX_HOLES = 36
MAX_TEES = 4
PINS = 4
NAME_MAX = 31
TEE_NAME_MAX = 15
MAX_WIDTH = 256
MAX_LENGTH = 640
MAX_HM_W = 64
MAX_HM_H = 128
MAX_SHAPES = 128
MAX_VERTS = 64
MAX_OBJECTS = 1024

SURFACES = ["rough", "first-cut", "fairway", "deep-rough", "green", "fringe",
            "bunker", "water", "path", "oob", "tee"]
CURVE_SURFACES = range(SURFACES.index("green"), SURFACES.index("water") + 1)
OBJECTS = {"tree": 1, "pine": 2, "palm": 3, "bush": 4, "rock": 5, "house": 6,
           "stand": 7, "wall": 8, "gorse": 9, "grass": 10, "landmark": 11, "animal": 12}
OBJECT_NAMES = {v: k for k, v in OBJECTS.items()}

PALM_EPOCH = 2082844800    # seconds from 1904 to 1970


class CourseError(Exception):
    pass


def fail(where, msg):
    raise CourseError("%s: %s" % (where, msg))


def read_lines(path):
    """Yields (line number, words) with comments gone and indented lines joined to the one before."""
    out = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.split("#", 1)[0].rstrip()
            if not line.strip():
                continue
            if line[0] in " \t" and out:
                out[-1][1].extend(line.split())
            else:
                out.append((n, line.split()))
    return out


def number(where, s):
    try:
        return float(s)
    except ValueError:
        fail(where, "not a number: %s" % s)


def quarter(where, s, top):
    """Yards to quarter yards, checked to be within 0..top yards."""
    q = round(number(where, s) * 4)
    if q < 0 or q > top * 4:
        fail(where, "%s is off the hole (0 to %d)" % (s, top))
    return q


def name_bytes(where, s, lo, hi):
    b = s.encode("latin-1")
    if not lo <= len(b) <= hi or any(c < 0x20 or c == 0x7F for c in b):
        fail(where, "name must be %d to %d plain characters: %r" % (lo, hi, s))
    return bytes([len(b)]) + b


def read_course(folder):
    path = os.path.join(folder, "course.txt")
    course = {"name": None, "tees": [], "holes": None, "scenery": None, "wind": None, "green": None,
              "terrain_meshes": {}, "putting_curves": {}, "terrain_default": None}
    for n, w in read_lines(path):
        where = "%s:%d" % (path, n)
        key, rest = w[0], " ".join(w[1:])
        if key == "name":
            course["name"] = rest
        elif key == "tee":
            course["tees"].append(rest)
        elif key == "holes":
            course["holes"] = int(number(where, rest))
        elif key == "scenery":
            course["scenery"] = int(number(where, rest))
            if not 0 <= course["scenery"] <= 255:
                fail(where, "scenery 0 to 255")
        elif key == "wind":
            course["wind"] = int(number(where, rest))
            if not 0 <= course["wind"] <= 40:
                fail(where, "wind 0 to 40 mph")
        elif key == "green":
            course["green"] = int(number(where, rest))
            if course["green"] and not 6 <= course["green"] <= 14:
                fail(where, "green speed 6 to 14, or 0 for the usual 10")
        elif key == "terrain-mesh":
            sources = w[2:]
            if sources[:1] == ["putting"]:
                options = sources[1:]
                valid_sources = len(options) == len(set(options)) and set(options) <= {"dunes", "bunkers", "water"}
            else:
                valid_sources = len(sources) <= 1 or (
                    len(sources) == 2 and (all(p.endswith(".mesh") for p in sources) or
                                          (sources[0].endswith(".txt") and sources[1] == "hazards")))
            if w[1:2] == ["all"]:
                if not valid_sources or sources[:1] != ["putting"]:
                    fail(where, "terrain-mesh all putting [bunkers] [water] [dunes]")
                if course["terrain_default"] is not None:
                    fail(where, "duplicate default terrain mesh")
                course["terrain_default"] = sources
                continue
            if len(w) < 2 or not w[1].isascii() or not w[1].isdigit() or not valid_sources:
                fail(where, "terrain-mesh HOLE [putting [bunkers] [water] [dunes] or VISUAL.txt [hazards] or DETAILED.mesh [COMPACT.mesh]]")
            hole = int(w[1])
            if hole in course["terrain_meshes"]:
                fail(where, "duplicate terrain mesh for hole %d" % hole)
            course["terrain_meshes"][hole] = sources
        elif key == "putting-curves":
            if len(w) != 3 or not w[1].isascii() or not w[1].isdigit():
                fail(where, "putting-curves HOLE FILE")
            hole = int(w[1])
            if hole in course["putting_curves"]:
                fail(where, "duplicate putting curves for hole %d" % hole)
            course["putting_curves"][hole] = w[2]
        else:
            fail(where, "unknown line %r" % key)
    if not course["name"] or not course["tees"] or not course["holes"]:
        fail(path, "needs name, tee and holes lines")
    if not 1 <= course["holes"] <= MAX_HOLES:
        fail(path, "1 to %d holes" % MAX_HOLES)
    if len(course["tees"]) > MAX_TEES:
        fail(path, "at most %d tees" % MAX_TEES)
    if any(not 1 <= n <= course["holes"] for n in course["terrain_meshes"]):
        fail(path, "terrain mesh names a hole outside the course")
    if any(not 1 <= n <= course["holes"] for n in course["putting_curves"]):
        fail(path, "putting curves name a hole outside the course")
    if course["terrain_default"] is not None:
        for n in range(1, course["holes"] + 1):
            course["terrain_meshes"].setdefault(n, course["terrain_default"])
    return course


def ellipse(where, args, hole):
    if len(args) not in (4, 5, 6):
        fail(where, "ellipse needs cx cy rx ry [points] [degrees]")
    cx, cy, rx, ry = (number(where, a) for a in args[:4])
    count = int(args[4]) if len(args) > 4 else 12
    turn = math.radians(number(where, args[5])) if len(args) > 5 else 0.0
    pts = []
    for i in range(count):
        a = 2 * math.pi * i / count
        x, y = rx * math.cos(a), ry * math.sin(a)
        x, y = x * math.cos(turn) - y * math.sin(turn), x * math.sin(turn) + y * math.cos(turn)
        pts.append((min(max(cx + x, 0), hole["width"]), min(max(cy + y, 0), hole["length"])))
    return [(round(x * 4), round(y * 4)) for x, y in pts]


SINGLE = ("name", "par", "size", "heightmap", "height", "base", "aim", "turn")
SHAPES = ("poly", "rect", "ellipse")


def hole_lines(folder, name, depth=0):
    """A hole file's lines as (where, words). A first line "from FILE" starts
    from another hole file: a name, par, size, heightmap, height, base or aim
    line here takes the place of the other's, tee, pin and object lines here
    take the place of all of its of that kind, and shapes here go after its
    shapes, so they can paint over them."""
    path = os.path.join(folder, name)
    lines = [("%s:%d" % (path, n), w) for n, w in read_lines(path)]
    if not lines or lines[0][1][0] != "from":
        return lines
    where, w = lines[0]
    if len(w) != 2:
        fail(where, "from FILE")
    if depth > 4:
        fail(where, "from goes round in a loop")
    base, own = hole_lines(folder, w[1], depth + 1), lines[1:]
    have = set(l[1][0] for l in own)
    keep = []
    for l in base:
        k = l[1][0]
        if k in SHAPES or k not in have:
            keep.append(l)
    shapes = [l for l in own if l[1][0] in SHAPES]
    # the base's shapes, then this file's, then everything else
    out = [l for l in keep if l[1][0] in SHAPES] + shapes
    out += [l for l in keep if l[1][0] not in SHAPES] + [l for l in own if l[1][0] not in SHAPES]
    return out


def read_hole(folder, number_, course):
    path = os.path.join(folder, "hole-%02d.txt" % number_)
    h = {"name": "", "par": None, "width": None, "length": None, "heightmap": None,
         "base": 0.0, "step": 1 / 16, "surface": 0, "aim": None, "tees": [], "pins": [],
         "shapes": [], "objects": [], "turn": None}
    lines = hole_lines(folder, "hole-%02d.txt" % number_)
    # Size first, as points are checked against it.
    for where, w in lines:
        if w[0] == "size":
            if len(w) != 3:
                fail(where, "size WIDTH LENGTH")
            h["width"], h["length"] = int(number(path, w[1])), int(number(path, w[2]))
    if h["width"] is None:
        fail(path, "needs a size line")
    if not 16 <= h["width"] <= MAX_WIDTH or not 16 <= h["length"] <= MAX_LENGTH:
        fail(path, "size must be 16 to %d across and 16 to %d along" % (MAX_WIDTH, MAX_LENGTH))
    W, L = h["width"], h["length"]

    def point(where, xs, ys):
        return (quarter(where, xs, W), quarter(where, ys, L))

    for where, w in lines:
        key, args = w[0], w[1:]
        if key == "size":
            continue
        elif key == "name":
            h["name"] = " ".join(args)
        elif key == "par":
            h["par"] = int(number(where, args[0]))
        elif key == "heightmap":
            h["heightmap"] = (os.path.join(folder, args[0]), int(number(where, args[1])))
        elif key == "height":
            h["base"], h["step"] = number(where, args[0]), number(where, args[1])
        elif key == "base":
            if args[0] not in SURFACES:
                fail(where, "unknown surface %r" % args[0])
            h["surface"] = SURFACES.index(args[0])
        elif key == "aim":
            h["aim"] = point(where, args[0], args[1])
        elif key == "turn":
            h["turn"] = round(number(where, args[0]) * 256 / 360) % 256
        elif key == "tee":
            if args[0] not in course["tees"]:
                fail(where, "tee %r isn't in course.txt" % args[0])
            yards = int(number(where, args[3])) if len(args) > 3 else None
            h["tees"].append([course["tees"].index(args[0]), point(where, args[1], args[2]), yards])
        elif key == "pin":
            h["pins"].append(point(where, args[0], args[1]))
        elif key in ("poly", "rect", "ellipse"):
            if not args or args[0] not in SURFACES:
                fail(where, "unknown surface %r" % (args[0] if args else ""))
            surf, rest = SURFACES.index(args[0]), args[1:]
            if key == "poly":
                pts = []
                for p in rest:
                    xy = p.split(",")
                    if len(xy) != 2:
                        fail(where, "points are x,y: %r" % p)
                    pts.append(point(where, xy[0], xy[1]))
            elif key == "rect":
                if len(rest) != 4:
                    fail(where, "rect needs x0 y0 x1 y1")
                x0, y0 = point(where, rest[0], rest[1])
                x1, y1 = point(where, rest[2], rest[3])
                pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
            else:
                pts = ellipse(where, rest, h)
            if not 3 <= len(pts) <= MAX_VERTS:
                fail(where, "a shape has 3 to %d points" % MAX_VERTS)
            h["shapes"].append((surf, pts))
        elif key == "object":
            t = OBJECTS.get(args[0])
            if t is None:
                t = int(number(where, args[0]))
            if not 1 <= t <= 255:
                fail(where, "object type 1 to 255")
            variant = int(args[3]) if len(args) > 3 else 0
            scale = int(args[4]) if len(args) > 4 else 64
            if not (0 <= variant <= 255 and 1 <= scale <= 255):
                fail(where, "variant 0 to 255, scale 1 to 255")
            h["objects"].append((t, variant, scale, point(where, args[1], args[2])))
        else:
            fail(where, "unknown line %r" % key)

    if h["par"] not in (3, 4, 5, 6):
        fail(path, "par 3 to 6")
    if not 1 <= len(h["tees"]) <= MAX_TEES:
        fail(path, "1 to %d tees" % MAX_TEES)
    if len(h["pins"]) != PINS:
        fail(path, "needs %d pins" % PINS)
    if h["aim"] is None:
        fail(path, "needs an aim line")
    if h["heightmap"] is None:
        fail(path, "needs a heightmap line")
    if len(h["shapes"]) > MAX_SHAPES or len(h["objects"]) > MAX_OBJECTS:
        fail(path, "too many shapes or objects")
    # Tee yardage without a number: tee to aim point to the middle of the pins.
    gx = sum(p[0] for p in h["pins"]) / PINS
    gy = sum(p[1] for p in h["pins"]) / PINS
    for t in h["tees"]:
        if t[2] is None:
            (tx, ty), (ax, ay) = t[1], h["aim"]
            t[2] = round((math.hypot(ax - tx, ay - ty) + math.hypot(gx - ax, gy - ay)) / 4)
    return h


def load_heightmap(where, path, cell, W, L):
    if cell not in (2, 4, 8, 16, 32):
        fail(where, "heightmap cell must be 2, 4, 8, 16 or 32 yards")
    img = Image.open(path).convert("L")
    w, h = img.size
    if not (2 <= w <= MAX_HM_W and 2 <= h <= MAX_HM_H):
        fail(path, "heightmap is %dx%d, at most %dx%d" % (w, h, MAX_HM_W, MAX_HM_H))
    if (w - 1) * cell < W or (h - 1) * cell < L:
        fail(path, "heightmap %dx%d at %d yards doesn't cover %dx%d" % (w, h, cell, W, L))
    # The image's top row is the far end of the hole; records start at y = 0.
    img = img.transpose(Image.FLIP_TOP_BOTTOM)
    return w, h, bytes(img.getdata())


def green_bounds(h):
    px, py = zip(*h["pins"])
    return (max(0, min(px) - GREEN_MESH_RADIUS), max(0, min(py) - GREEN_MESH_RADIUS),
            min(h["width"] * 4, max(px) + GREEN_MESH_RADIUS),
            min(h["length"] * 4, max(py) + GREEN_MESH_RADIUS))


def green_mesh_record(h, budget, unit=1):
    """Pack quarter-yard putting rows. Shape coordinates use `unit` steps per quarter yard."""
    x0, y0, x1, y1 = green_bounds(h)
    w, height = x1 - x0, y1 - y0
    base = bytes([h["surface"]]) * w
    shapes = []
    for surface, points in h["shapes"]:
        xs, ys = zip(*points)
        if max(xs) <= x0 * unit or min(xs) >= x1 * unit or max(ys) <= y0 * unit or min(ys) >= y1 * unit:
            continue
        edges = []
        for (ax, ay), (bx, by) in zip(points[-1:] + points[:-1], points):
            if ay > by:
                ax, ay, bx, by = bx, by, ax, ay
            if ay < by:
                edges.append((ax, ay, bx - ax, by - ay))
        shapes.append((bytes([surface]), min(ys), max(ys), edges))

    offsets = bytearray(b"\0\0")
    runs = bytearray()
    overhead = 8 + 2 * (height + 1)
    if overhead + 2 * height > budget:
        return b""
    for y in range(y0, y1):
        row = bytearray(base)
        sample_y = (2 * y + 1) * unit
        for surface, low, high, edges in shapes:
            if not 2 * low <= sample_y < 2 * high:
                continue
            crossings = []
            for ax, ay, dx, dy in edges:
                if 2 * ay <= sample_y < 2 * (ay + dy):
                    # ceil(crossing - 1/2), at a sample y + 1/2.
                    numerator = (2 * ax - unit) * dy + (sample_y - 2 * ay) * dx
                    crossings.append(-(-numerator // (2 * dy * unit)))
            crossings.sort()
            for left, right in zip(crossings[::2], crossings[1::2]):
                lo, hi = max(0, left - x0), min(w, right - x0)
                if lo < hi:
                    row[lo:hi] = surface * (hi - lo)
        previous = row[0]
        for x, surface in enumerate(row[1:], 1):
            if surface != previous:
                runs += struct.pack(">H", (x << 4) | previous)
                previous = surface
        runs += struct.pack(">H", (w << 4) | previous)
        if overhead + len(runs) > budget:
            return b""
        offsets += struct.pack(">H", len(runs))
    return struct.pack(">HHHH", x0, y0, w, height) + offsets + runs


def green_slabs(h, cell):
    """Join visible, straight-sided spans between contour events."""
    x0, y0, x1, y1 = green_bounds(h)
    # Edge entries are (bottom y, top y, x slope, x intercept, shape index).
    edges = [(y0, y1, 0, x0, -1), (y0, y1, 0, x1, -1)]
    surfaces = []
    height_cuts = set(range(y0 + (-y0 % (cell * 4)), y1, cell * 4))
    cuts = {y0, y1} | height_cuts
    for shape, points in h["shapes"]:
        xs, ys = zip(*points)
        if max(xs) <= x0 or min(xs) >= x1 or max(ys) <= y0 or min(ys) >= y1:
            continue
        surfaces.append(shape)
        for (ax, ay), (bx, by) in zip(points[-1:] + points[:-1], points):
            if ay > by:
                ax, ay, bx, by = bx, by, ax, ay
            if ay == by or by <= y0 or ay >= y1:
                continue
            slope = (bx - ax) / (by - ay)
            edges.append((ay, by, slope, ax - slope * ay, len(surfaces) - 1))
            cuts.update(v for v in (ay, by) if y0 < v < y1)
    # Edges can change order inside a slab, including where they enter the box.
    for i, (a0, a1, am, ab, _) in enumerate(edges):
        for b0, b1, bm, bb, _ in edges[:i]:
            if am == bm:
                continue
            y = (bb - ab) / (am - bm)
            if max(y0, a0, b0) < y < min(y1, a1, b1) and x0 - 1e-8 <= am * y + ab <= x1 + 1e-8:
                cuts.add(y)
    ordered = sorted(cuts)
    pending = {}
    spans = []
    for lo, hi in zip(ordered, ordered[1:]):
        mid = (lo + hi) / 2
        crossings = sorted((m * mid + b, i, shape) for i, (p, q, m, b, shape) in enumerate(edges)
                           if p <= mid < q)
        active = set()
        visible = []
        for (x, i, shape), (right, j, _) in zip(crossings, crossings[1:]):
            if shape >= 0:
                active.symmetric_difference_update((shape,))
            if right <= x + 1e-8 or x < x0 - 1e-8 or right > x1 + 1e-8:
                continue
            surf = surfaces[max(active)] if active else h["surface"]
            if visible and visible[-1][2] == surf:
                visible[-1] = (visible[-1][0], j, surf)
            else:
                visible.append((i, j, surf))
        next_spans = {}
        for key in visible:
            next_spans[key] = pending.pop(key, lo) if lo not in height_cuts else lo
        spans.extend((key, start, lo) for key, start in pending.items())
        pending = next_spans
    spans.extend((key, start, y1) for key, start in pending.items())
    return edges, spans


def green_geometry_record(h, cell, hm_w, hm_h, heights, budget):
    edges, spans = green_slabs(h, cell)
    x0, _, x1, _ = green_bounds(h)

    def height(x, y):
        gx, gy = x * 16 // cell, y * 16 // cell
        i, fx = divmod(gx, 256)
        j, fy = divmod(gy, 256)
        ni, nj = min(i + 1, hm_w - 1), min(j + 1, hm_h - 1)
        a, b = heights[j * hm_w + i], heights[nj * hm_w + i]
        t = a * 256 + (heights[j * hm_w + ni] - a) * fx
        u = b * 256 + (heights[nj * hm_w + ni] - b) * fx
        return t + (((u - t) >> 1) * fy >> 7)

    def at(edge, y):
        _, _, slope, intercept, _ = edges[edge]
        return max(x0, min(x1, slope * y + intercept))

    # Wider quads cost less where contours are dense, while the slab heights
    # still follow heightmap rows.
    for spacing in (32, 48, 64, 96):
        vertices, index, triangles = [], {}, []

        def vertex(x, y):
            point = (x, y)
            if point not in index:
                index[point] = len(vertices)
                vertices.append((*point, height(*point)))
            return index[point]

        def triangle(a, b, c, surf):
            ax, ay, _ = vertices[a]
            bx, by, _ = vertices[b]
            cx, cy, _ = vertices[c]
            if (bx - ax) * (cy - ay) != (by - ay) * (cx - ax):
                triangles.append((a, b, c, surf))

        # Both spans at a horizontal join need the same vertices and heights.
        rows, patches = {}, []
        for (left, right, surf), lo, hi in spans:
            ylo, yhi = round(lo * 4), round(hi * 4)
            if ylo == yhi:
                continue
            a, b = at(left, lo), at(right, lo)
            c, d = at(left, hi), at(right, hi)
            b, d = max(a, b), max(c, d)
            parts = max(1, math.ceil(max(b - a, d - c) / spacing))
            for yl, start, end in ((ylo, a, b), (yhi, c, d)):
                xs = rows.setdefault(yl, set())
                xs.update(round((start + (end - start) * k / parts) * 4) for k in range(parts + 1))
            patches.append((ylo, yhi, round(a * 4), round(b * 4), round(c * 4), round(d * 4), surf))
        rows = {y: sorted(xs) for y, xs in rows.items()}
        for ylo, yhi, a, b, c, d, surf in patches:
            lower, upper = rows[ylo], rows[yhi]
            lower = lower[bisect_left(lower, a):bisect_right(lower, b)]
            upper = upper[bisect_left(upper, c):bisect_right(upper, d)]
            i = j = 0
            va, vc = vertex(lower[i], ylo), vertex(upper[j], yhi)
            # Advance along whichever edge has the next closer vertex.
            while i + 1 < len(lower) or j + 1 < len(upper):
                if j + 1 == len(upper) or (i + 1 < len(lower) and
                        (lower[i + 1] - a) * (d - c) <= (upper[j + 1] - c) * (b - a)):
                    i += 1
                    vb = vertex(lower[i], ylo)
                    triangle(va, vb, vc, surf)
                    va = vb
                else:
                    j += 1
                    vd = vertex(upper[j], yhi)
                    triangle(va, vd, vc, surf)
                    vc = vd
        size = 4 + 6 * len(vertices) + 7 * len(triangles)
        if 0 < len(vertices) <= MAX_GREEN_VERTICES and 0 < len(triangles) <= MAX_GREEN_TRIANGLES and size <= budget:
            return (struct.pack(">HH", len(vertices), len(triangles)) +
                    b"".join(struct.pack(">HHH", *v) for v in vertices) +
                    b"".join(struct.pack(">HHHB", *t) for t in triangles))
    return b""


def segment_distance_squared(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    px, py = p[0] - a[0], p[1] - a[1]
    length = dx * dx + dy * dy
    t = max(0, min(1, (px * dx + py * dy) / length)) if length else 0
    return (px - t * dx) ** 2 + (py - t * dy) ** 2


def simplify_outline(points, tolerance):
    # Split the closed path at two distant points, then shorten each arc.
    opposite = max(range(1, len(points)), key=lambda i: segment_distance_squared(points[i], points[0], points[0]))
    path = points + points[:1]
    keep = {0, opposite}
    pending = [(0, opposite), (opposite, len(points))]
    while pending:
        lo, hi = pending.pop()
        if hi - lo < 2:
            continue
        farthest = max(range(lo + 1, hi), key=lambda i: segment_distance_squared(path[i], path[lo], path[hi]))
        if segment_distance_squared(path[farthest], path[lo], path[hi]) > tolerance * tolerance:
            keep.add(farthest)
            pending.extend(((lo, farthest), (farthest, hi)))
    return [points[i] for i in sorted(keep)] if len(keep) >= 3 else points


def read_visual_shapes(path, h, putting_slots=None):
    shapes = []
    for n, words in read_lines(path):
        if words[0] != "poly":
            continue
        where = "%s:%d" % (path, n)
        marked = words[2:3] == ["putting"]
        start = 3 if marked else 2
        if len(words) < start + 3 or words[1] not in SURFACES:
            fail(where, "poly SURFACE needs at least three x,y points")
        surface = SURFACES.index(words[1])
        if marked and surface not in CURVE_SURFACES:
            fail(where, "only green, fringe, bunker and water outlines can use putting curves")
        if marked and putting_slots is not None:
            putting_slots.append(len(shapes))
        points = []
        for pair in words[start:]:
            xy = pair.split(",")
            if len(xy) != 2:
                fail(where, "expected an x,y point")
            x, y = (number(where, a) for a in xy)
            if not (0 <= x <= h["width"] and 0 <= y <= h["length"]):
                fail(where, "point is off the hole: %s" % pair)
            if not points or points[-1] != (x, y):
                points.append((x, y))
        if points[-1] == points[0]:
            points.pop()
        if len(set(points)) < 3:
            fail(where, "needs three distinct points")
        shapes.append((surface, points))
    if not 1 <= len(shapes) <= MAX_VISUAL_SHAPES:
        fail(path, "needs 1 to %d visual polygons" % MAX_VISUAL_SHAPES)

    simplify = sum(len(points) for _, points in shapes) > MAX_VISUAL_VERTICES
    detailed = {SURFACES.index(name) for name in ("green", "fringe", "bunker")}
    quantized = []
    for surface, points in shapes:
        if simplify:
            points = simplify_outline(points, 0.04 if surface in detailed else 0.20)
        outline = []
        for x, y in points:
            point = round(x * 16), round(y * 16)
            if not outline or point != outline[-1]:
                outline.append(point)
        if outline[-1] == outline[0]:
            outline.pop()
        if len(set(outline)) < 3:
            fail(path, "visual polygon collapses at sixteenth-yard resolution")
        quantized.append((surface, outline))
    if sum(len(points) for _, points in quantized) > MAX_VISUAL_VERTICES:
        fail(path, "visual polygons exceed %d vertices after simplification" % MAX_VISUAL_VERTICES)
    return quantized


def sand_sample(x, y, edges, inward):
    inside = False
    closest = 32
    nx = ny = 0
    for ax, ay, bx, by, left, bottom, right, top, normal_x, normal_y in edges:
        dx, dy = x - ax, y - ay
        dot = normal_x * dx + normal_y * dy
        if (ay > y) != (by > y) and (dot > 0 if by > ay else dot < 0):
            inside = not inside
        distance = max(left - x, bottom - y, x - right, y - top, abs(dot))
        if distance < closest:
            closest, nx, ny = distance, normal_x, normal_y
            side = (1 if dot > 0 else -1) if dot else inward

    # The contours use sixteenth yards. The atlas has one more bit of precision.
    distance = round(closest * 2)
    if distance >= 64:
        return (64 if inside else 192), 0
    if closest:
        side *= 1 if inside else -1
    sx = round(nx * side * 7)
    sy = round(ny * side * 7)
    return ((distance if inside else -distance) & 255), ((sx & 15) << 4) | (sy & 15)


def sand_atlas(path, h, shapes, budget):
    bunker = SURFACES.index("bunker")
    if h["surface"] == bunker:
        fail(path, "baked sand requires outlined bunkers over a different base surface")
    tiles = [points for surface, points in shapes if surface == bunker]
    if not tiles:
        fail(path, "baked sand requires at least one visual bunker")
    out = bytearray([len(tiles)])
    for points in tiles:
        x0 = max(0, min(x for x, _ in points) // 16 - 1)
        y0 = max(0, min(y for _, y in points) // 16 - 1)
        x1 = min(h["width"], (max(x for x, _ in points) + 15) // 16 + 1)
        y1 = min(h["length"], (max(y for _, y in points) + 15) // 16 + 1)
        w, height = x1 - x0 + 1, y1 - y0 + 1
        if len(out) + 8 + 2 * w * height > budget:
            fail(path, "baked sand puts the hole record over 16 KB")
        edges, area = [], 0
        for (ax, ay), (bx, by) in zip(points, points[1:] + points[:1]):
            dx, dy = bx - ax, by - ay
            normal = abs(dx) + abs(dy)
            if not normal:
                continue
            area += ax * by - bx * ay
            edges.append((ax, ay, bx, by, min(ax, bx), min(ay, by), max(ax, bx), max(ay, by),
                          dy / normal, -dx / normal))
        inward = -1 if area > 0 else 1
        out += struct.pack(">HHHH", x0, y0, w, height)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                out.extend(sand_sample(x * 16, y * 16, edges, inward))
    return bytes(out)


def visual_record(path, h, budget, baked_sand=False):
    shapes = read_visual_shapes(path, h)
    out = bytearray([len(shapes)])
    for surface, points in shapes:
        out += struct.pack(">BH", surface, len(points))
        out += b"".join(struct.pack(">HH", x, y) for x, y in points)
    budget -= len(out) + 2
    if budget < 0:
        fail(path, "visual polygons put the hole record over 16 KB")
    atlas = sand_atlas(path, h, shapes, budget) if baked_sand else b""
    mesh = green_mesh_record(dict(h, shapes=shapes), budget - len(atlas), unit=4)
    out += struct.pack(">H", len(mesh)) + mesh + atlas
    print("Visual contours: %d polygons, %d vertices, %d bytes in putting rows" %
          (len(shapes), sum(len(points) for _, points in shapes), len(mesh)))
    if atlas:
        print("Baked sand: %d tiles, %d bytes" % (atlas[0], len(atlas)))
    return bytes(out)


def curve_points(shape, steps, width, length):
    points = []
    for i, b in enumerate(shape):
        a, c, d = shape[i - 1], shape[(i + 1) % len(shape)], shape[(i + 2) % len(shape)]
        for k in range(steps):
            t = k / steps
            u = 1 - t
            # A closed cubic through the authored points. The tangents limit overshoot.
            x, y = (round(4 * (u * u * (1 + 2 * t) * b[j] + t * t * (3 - 2 * t) * c[j]
                              + .4 * (t * u * u * (c[j] - a[j]) - t * t * u * (d[j] - b[j]))))
                    for j in range(2))
            point = (min(max(0, x), width * 16), min(max(0, y), length * 16))
            if not points or points[-1] != point:
                points.append(point)
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    return points


def bake_putting_curves(h, budget, surfaces, shared_hazards=False):
    shapes = []
    for i, (surface, points) in enumerate(h["shapes"]):
        hazard = shared_hazards and surface in (terrainmesh.SAND, terrainmesh.WATER)
        if surface not in surfaces and not hazard:
            continue
        xs, ys = zip(*points)
        near_pin = any(min(xs) - PUTTING_CURVE_RADIUS <= x <= max(xs) + PUTTING_CURVE_RADIUS and
                       min(ys) - PUTTING_CURVE_RADIUS <= y <= max(ys) + PUTTING_CURVE_RADIUS for x, y in h["pins"])
        if near_pin or hazard:
            shapes.append((i, points, near_pin, hazard))
    budget = min((budget - 1 - 3 * len(shapes)) // 4, MAX_CURVE_VERTS_TOTAL)
    for steps in range(4 if shared_hazards else 6, 0 if shared_hazards else 1, -1):
        if not shared_hazards and sum(min(steps, MAX_CURVE_VERTS // len(points)) * len(points)
                                      for _, points, _, _ in shapes) > budget:
            continue
        outlines = []
        for i, points, near_pin, hazard in shapes:
            samples = min(steps if near_pin else 2, MAX_CURVE_VERTS // len(points))
            outline = curve_points(points, samples, h["width"], h["length"])
            if shared_hazards:
                outline = simplify_outline(outline, 4 if hazard else 2)
            if len(outline) >= 3:
                outlines.append((i, outline))
        if sum(len(outline) for _, outline in outlines) <= budget:
            break
    else:
        return b""
    curves = [struct.pack(">BH", i, len(points)) + b"".join(struct.pack(">HH", *p) for p in points)
              for i, points in outlines]
    return bytes([len(curves)]) + b"".join(curves) if curves else b""


def read_putting_curves(data, h, indexed=False):
    c = Cursor(data)
    count = c.take("B")
    if not 1 <= count <= len(h["shapes"]):
        raise CourseError("invalid contour count")
    prev, total = -1, 0
    shapes = []
    for _ in range(count):
        i, size = c.take("BH")
        total += size
        if (not prev < i < len(h["shapes"]) or h["shapes"][i][0] not in CURVE_SURFACES or
                not 3 <= size <= MAX_CURVE_VERTS or total > MAX_CURVE_VERTS_TOTAL):
            raise CourseError("invalid contour index or point count")
        prev = i
        points = []
        for _ in range(size):
            x, y = c.take("HH")
            if x > h["width"] * 16 or y > h["length"] * 16:
                raise CourseError("contour point is off the hole")
            points.append((x, y))
        shape = h["shapes"][i][0], points
        shapes.append((i, *shape) if indexed else shape)
    if c.p != len(data):
        raise CourseError("data after the last contour")
    return shapes


def hole_record(folder, n, h, green_mesh=False, green_geometry=False, visual=None, baked_sand=False,
                putting_curves=False, record_curves=None, curve_surfaces=CURVE_SURFACES,
                shared_hazards=False):
    path = os.path.join(folder, "hole-%02d.txt" % n)
    hm_path, cell = h["heightmap"]
    hm_w, hm_h, heights = load_heightmap(path, hm_path, cell, h["width"], h["length"])
    base = round(h["base"] * 16)
    step = round(h["step"] * 4096)
    if not -32768 <= base <= 32767 or not 0 <= step <= 65535:
        fail(path, "height base or step out of range")
    out = struct.pack(">HBBHHBBBBhHHH", HOLE_VERSION, n, h["par"], h["width"], h["length"],
                      hm_w, hm_h, cell.bit_length() - 1, h["surface"], base, step, *h["aim"])
    out += name_bytes(path, h["name"], 0, NAME_MAX)
    out += bytes([len(h["tees"])])
    for s, (x, y), yards in h["tees"]:
        out += struct.pack(">BHHH", s, x, y, yards)
    for x, y in h["pins"]:
        out += struct.pack(">HH", x, y)
    out += heights
    out += bytes([len(h["shapes"])])
    for surf, pts in h["shapes"]:
        out += bytes([surf, len(pts)]) + b"".join(struct.pack(">HH", x, y) for x, y in pts)
    out += struct.pack(">H", len(h["objects"]))
    for t, v, s, (x, y) in h["objects"]:
        out += struct.pack(">BBBHH", t, v, s, x, y)
    if visual:
        extra = visual_record(visual, h, MAX_RECORD - len(out), baked_sand)
        return struct.pack(">H", HOLE_SAND_VERSION if baked_sand else HOLE_VISUAL_VERSION) + out[2:] + extra
    if green_mesh or green_geometry:
        mesh = green_mesh_record(h, MAX_RECORD - len(out))
        if mesh:
            out = struct.pack(">H", HOLE_MESH_VERSION) + out[2:] + mesh
            if green_geometry:
                geometry = green_geometry_record(h, cell, hm_w, hm_h, heights, MAX_RECORD - len(out))
                if geometry:
                    out = struct.pack(">H", HOLE_GEOMETRY_VERSION) + out[2:] + geometry
    elif putting_curves:
        if putting_curves is True:
            curves = bake_putting_curves(h, MAX_RECORD - len(out), curve_surfaces, shared_hazards)
        else:
            path = os.path.join(folder, putting_curves)
            try:
                with open(path, "rb") as f:
                    curves = f.read(MAX_RECORD + 1)
                read_putting_curves(curves, h)
            except (CourseError, OSError, struct.error) as e:
                fail(path, "putting curves: %s" % e)
        if curves:
            out = struct.pack(">H", HOLE_CURVE_VERSION) + out[2:] + curves
            if record_curves is not None:
                record_curves[n] = curves
    return out


def course_record(course, holes):
    turns = any(h["turn"] is not None for h in holes)
    version = 2 if course["scenery"] is not None or turns else COURSE_VERSION
    if course["wind"] is not None:
        version = 3
    if course["green"] is not None:
        version = 4
    out = struct.pack(">HBB", version, course["holes"], len(course["tees"]))
    if "|" in course["name"]:
        fail("course.txt", "course name can't contain '|': %r" % course["name"])
    out += name_bytes("course.txt", course["name"], 1, NAME_MAX)
    for t in course["tees"]:
        out += name_bytes("course.txt", t, 1, TEE_NAME_MAX)
    for h in holes:
        out += struct.pack(">BH", h["par"], max(t[2] for t in h["tees"]))
    if version >= 2:
        out += bytes([course["scenery"] or 0] + [h["turn"] or 0 for h in holes])
    if version >= 3:
        out += bytes([course["wind"] or 0])
    if version >= 4:
        out += bytes([course["green"]])
    return out


def write_pdb(path, name, records, kind=b"Crse", creator=b"P386"):
    when = int(os.environ.get("SOURCE_DATE_EPOCH", time.time())) + PALM_EPOCH
    nb = name.encode("latin-1")
    if len(nb) > 31:
        raise CourseError("database name too long: %s" % name)
    head = struct.pack(">32sHHIIIIII4s4sIIH", nb, 0x0008, 1, when, when, 0, 0, 0, 0,
                       kind, creator, len(records) + 1, 0, len(records))
    offset = len(head) + 8 * len(records) + 2
    index = b""
    for i, r in enumerate(records):
        index += struct.pack(">IB", offset, 0) + (i + 1).to_bytes(3, "big")
        offset += len(r)
    with open(path, "wb") as f:
        f.write(head + index + b"\0\0" + b"".join(records))


def read_pdb(path):
    data = open(path, "rb").read()
    name = data[:32].split(b"\0", 1)[0].decode("latin-1")
    kind, creator = data[60:64], data[64:68]
    count = struct.unpack(">H", data[76:78])[0]
    offs = [struct.unpack(">I", data[78 + 8 * i:82 + 8 * i])[0] for i in range(count)]
    offs.append(len(data))
    return name, kind, creator, [data[offs[i]:offs[i + 1]] for i in range(count)]


def terrain_mesh_records(folder, n, h, sources, curves=None, require_compact=False):
    where = os.path.join(folder, "hole-%02d.txt" % n)
    source = sources[0] if sources else None
    shared_putting = source == "putting"
    shared_bunkers = shared_putting and "bunkers" in sources[1:]
    shared_water = shared_putting and "water" in sources[1:]
    dunes = shared_putting and "dunes" in sources[1:]
    if shared_putting:
        source = None
    shared_hazards = bool(curves) and (not source or sources[1:] == ["hazards"])
    path = os.path.join(folder, source) if source else where
    try:
        meshes = []
        if source and source.endswith(".mesh"):
            for sidecar in sources:
                path = os.path.join(folder, sidecar)
                with open(path, "rb") as f:
                    data = f.read(terrainmesh.MAX_BYTES + 1)
                terrainmesh.validate(data, h["width"], h["length"])
                meshes.append(data)
        else:
            putting_slots = []
            shapes = read_visual_shapes(path, h, putting_slots) if source else [
                (sf, [(x * 4, y * 4) for x, y in points]) for sf, points in h["shapes"]
            ]
            indexed = {i: (sf, points) for i, sf, points in read_putting_curves(curves, h, indexed=True)} if curves else {}
            if shared_putting and not indexed:
                fail(path, "terrain mesh needs a putting green, fringe or water contour")
            if not source:
                shapes = [indexed.get(i, shape) for i, shape in enumerate(shapes)]
            putting = tuple(indexed) if not source else ()
            if putting_slots and curves:
                shared = list(indexed.values())
                slots = set(putting_slots)
                if shared_hazards:
                    # Include the course hazards away from the green.
                    hazard_types = {terrainmesh.SAND, terrainmesh.WATER}
                    shared = [indexed.get(i, (sf, [(x * 4, y * 4) for x, y in points]))
                              for i, (sf, points) in enumerate(h["shapes"])
                              if i in indexed or sf in hazard_types]
                    slots.update(i for i, (sf, _) in enumerate(shapes) if sf in hazard_types)
                first = min(slots)
                shapes = shapes[:first] + shared + [
                    shape for i, shape in enumerate(shapes[first:], first) if i not in slots
                ]
                putting = range(first, first + len(shared))
            hm_path, cell = h["heightmap"]
            nx, ny, heights = load_heightmap(where, hm_path, cell, h["width"], h["length"])
            sand = ()
            if h["surface"] != terrainmesh.SAND and any(sf == terrainmesh.SAND for sf, _ in shapes):
                atlas = sand_atlas(path, h, shapes, terrainmesh.MAX_BYTES)
                sand = terrainmesh.sand_samples(atlas)
            for compact in (False, True):
                try:
                    data = terrainmesh.bake_record(
                        h["width"], h["length"], nx, ny, cell, h["surface"],
                        round(h["base"] * 16) / 16, round(h["step"] * 4096) / 4096,
                        heights, shapes, sand, compact=compact, putting=putting,
                        dunes=dunes, round_mown=shared_bunkers, round_natural=shared_water,
                        shared_hazards=shared_hazards and (not source or bool(putting_slots)))
                except (ValueError, struct.error) as e:
                    if not compact or require_compact:
                        raise
                    print("%s: compact terrain mesh skipped: %s" % (path, e), file=sys.stderr)
                else:
                    meshes.append(data)
    except (OSError, ValueError, struct.error) as e:
        fail(path, "terrain mesh: %s" % e)
    for kind, data in zip(("detailed", "compact"), meshes):
        print("%s terrain mesh on hole %d: %d bytes" % (kind.capitalize(), n, len(data)))
    return meshes


def putting_surfaces(sources):
    if sources[:1] == ["putting"] and "bunkers" in sources[1:] and "water" not in sources[1:]:
        return range(terrainmesh.GREEN, terrainmesh.WATER)
    return CURVE_SURFACES


def build(folder, out, green_mesh=False, green_geometry=False, visual_hole=None, baked_sand=False):
    course = read_course(folder)
    if visual_hole and not 1 <= visual_hole[0] <= course["holes"]:
        fail(folder, "visual hole number out of range")
    terrain_sources = course["terrain_meshes"]
    default = course["terrain_default"]
    if default is not None and (green_mesh or green_geometry or visual_hole):
        # Implicit meshes use the same hole record slot as the experimental views.
        visual_number = visual_hole[0] if visual_hole else 0
        terrain_sources = {
            n: sources for n, sources in terrain_sources.items()
            if sources is not default or (not (green_mesh or green_geometry) and n != visual_number)
        }
    holes = [read_hole(folder, n, course) for n in range(1, course["holes"] + 1)]
    records = [course_record(course, holes)]
    baked = {n for n, sources in terrain_sources.items() if not sources or not sources[0].endswith(".mesh")}
    shared = {n for n in baked if not terrain_sources[n] or terrain_sources[n][0] == "putting" or
              terrain_sources[n][1:] == ["hazards"]}
    curves = dict.fromkeys(baked, True)
    curves.update(course["putting_curves"])
    record_curves = {}
    records += [hole_record(folder, n, h, green_mesh, green_geometry,
                            visual_hole[1] if visual_hole and n == visual_hole[0] else None, baked_sand,
                            curves.get(n, False), record_curves,
                            curve_surfaces=putting_surfaces(terrain_sources.get(n, ())),
                            shared_hazards=n in shared)
                for n, h in enumerate(holes, 1)]
    for i, r in enumerate(records):
        if len(r) > MAX_RECORD:
            raise CourseError("record %d is %d bytes, over 16 KB" % (i, len(r)))
    if terrain_sources:
        indices = [0] * (2 * len(holes))
        meshes = []
        for n, sources in sorted(terrain_sources.items()):
            for quality, data in enumerate(terrain_mesh_records(folder, n, holes[n - 1], sources,
                                                               record_curves.get(n),
                                                               sources is default)):
                indices[2 * (n - 1) + quality] = len(records) + 1 + len(meshes)
                meshes.append(data)
        if any(indices[1::2]):
            magic = terrainmesh.QUALITY_MANIFEST
        else:
            magic = terrainmesh.MANIFEST
            indices = indices[::2]
        records.append(magic + struct.pack(">%dH" % len(indices), *indices))
        records.extend(meshes)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    write_pdb(out, "Par386 " + course["name"], records)
    print("%s: %d holes, %d bytes in records, sizes %s" % (out, len(holes), sum(len(r) for r in records),
                                                     " ".join(str(len(r)) for r in records[:len(holes) + 1])))
    if green_mesh or green_geometry:
        print("Compiled putting materials on %d of %d holes" %
              (sum(r[1] >= HOLE_MESH_VERSION for r in records[1:len(holes) + 1]), len(holes)))
    if green_geometry:
        print("Compiled putting geometry on %d of %d holes" %
              (sum(r[1] == HOLE_GEOMETRY_VERSION for r in records[1:len(holes) + 1]), len(holes)))


def yd(q, d=4):
    """Exact decimal text for q / d, where d is a power of two."""
    s = format(Decimal(q) / Decimal(d), "f")
    return s.rstrip("0").rstrip(".") if "." in s else s


class Cursor:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, fmt):
        v = struct.unpack_from(">" + fmt, self.d, self.p)
        self.p += struct.calcsize(">" + fmt)
        return v if len(v) > 1 else v[0]

    def name(self):
        n = self.take("B")
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s


def dump(pdb, outdir):
    name, kind, creator, records = read_pdb(pdb)
    if kind != b"Crse" or creator != b"P386":
        raise CourseError("%s isn't a Par 386 course" % pdb)
    os.makedirs(outdir, exist_ok=True)
    c = Cursor(records[0])
    version, nholes, ntees = c.take("HBB")
    cname = c.name()
    tees = [c.name() for _ in range(ntees)]
    c.take("BH" * nholes)
    turns = [None] * nholes
    try:
        meshes = terrainmesh.record_indices(records, nholes)
    except ValueError as e:
        fail(pdb, "terrain mesh: %s" % e)
    with open(os.path.join(outdir, "course.txt"), "w") as f:
        f.write("name %s\n" % cname)
        for t in tees:
            f.write("tee %s\n" % t)
        f.write("holes %d\n" % nholes)
        if version >= 2:
            f.write("scenery %d\n" % c.take("B"))
            turns = [c.take("B") for _ in range(nholes)]
        if version >= 3:
            f.write("wind %d\n" % c.take("B"))
        if version >= 4:
            f.write("green %d\n" % c.take("B"))
        for n, (detailed, compact) in enumerate(meshes, 1):
            names = []
            for index, suffix in ((detailed, ""), (compact, "-compact")):
                if not index:
                    continue
                filename = "hole-%02d%s.mesh" % (n, suffix)
                names.append(filename)
                with open(os.path.join(outdir, filename), "wb") as mesh:
                    mesh.write(records[index])
            if names:
                f.write("terrain-mesh %d %s\n" % (n, " ".join(names)))
    for n in range(1, nholes + 1):
        c = Cursor(records[n])
        (version, num, par, W, L, hm_w, hm_h, shift, surf, base, step,
         ax, ay) = c.take("HBBHHBBBBhHHH")
        if not HOLE_VERSION <= version <= HOLE_CURVE_VERSION:
            raise CourseError("hole %d: unsupported version %d" % (n, version))
        hname = c.name()
        lines = ["name %s" % hname, "par %d" % par, "size %d %d" % (W, L),
                 "heightmap hole-%02d.png %d" % (n, 1 << shift),
                 "height %s %s" % (yd(base, 16), yd(step, 4096)),
                 "base %s" % SURFACES[surf], "aim %s %s" % (yd(ax), yd(ay))]
        if turns[n - 1] is not None:
            lines.append("turn %s" % yd(turns[n - 1] * 360, 256))
        for _ in range(c.take("B")):
            s, x, y, yards = c.take("BHHH")
            lines.append("tee %s %s %s %d" % (tees[s], yd(x), yd(y), yards))
        for _ in range(PINS):
            x, y = c.take("HH")
            lines.append("pin %s %s" % (yd(x), yd(y)))
        heights = c.d[c.p:c.p + hm_w * hm_h]
        c.p += hm_w * hm_h
        img = Image.frombytes("L", (hm_w, hm_h), heights).transpose(Image.FLIP_TOP_BOTTOM)
        img.save(os.path.join(outdir, "hole-%02d.png" % n))
        shapes = []
        for _ in range(c.take("B")):
            s, count = c.take("BB")
            pts = [c.take("HH") for _ in range(count)]
            shapes.append((s, pts))
            lines.append("poly %s" % SURFACES[s])
            for i in range(0, count, 6):
                lines.append("    " + " ".join("%s,%s" % (yd(x), yd(y)) for x, y in pts[i:i + 6]))
        for _ in range(c.take("H")):
            t, v, s, x, y = c.take("BBBHH")
            lines.append("object %s %s %s %d %d" % (OBJECT_NAMES.get(t, str(t)), yd(x), yd(y), v, s))
        with open(os.path.join(outdir, "hole-%02d.txt" % n), "w") as f:
            f.write("\n".join(lines) + "\n")
        if version in (HOLE_VISUAL_VERSION, HOLE_SAND_VERSION):
            path = os.path.join(outdir, "hole-%02d-visual.txt" % n)
            with open(path, "w") as f:
                for _ in range(c.take("B")):
                    s, count = c.take("BH")
                    points = [c.take("HH") for _ in range(count)]
                    f.write("poly %s\n" % SURFACES[s])
                    for i in range(0, count, 6):
                        f.write("    " + " ".join("%s,%s" % (yd(x, 16), yd(y, 16)) for x, y in points[i:i + 6]) + "\n")
        if version == HOLE_CURVE_VERSION:
            curves = c.d[c.p:]
            read_putting_curves(curves, {"shapes": shapes, "width": W, "length": L})
            filename = "hole-%02d.curves" % n
            with open(os.path.join(outdir, filename), "wb") as f:
                f.write(curves)
            with open(os.path.join(outdir, "course.txt"), "a") as f:
                f.write("putting-curves %d %s\n" % (n, filename))
    print("%s: dumped %d holes to %s" % (pdb, nholes, outdir))


def main():
    try:
        if len(sys.argv) == 4 and sys.argv[1] == "build":
            build(sys.argv[2], sys.argv[3])
        elif len(sys.argv) == 5 and sys.argv[1:3] == ["build", "--green-mesh"]:
            build(sys.argv[3], sys.argv[4], green_mesh=True)
        elif len(sys.argv) == 5 and sys.argv[1:3] == ["build", "--green-geometry"]:
            build(sys.argv[3], sys.argv[4], green_geometry=True)
        elif len(sys.argv) == 7 and sys.argv[1] == "build" and sys.argv[2] in ("--visual-hole", "--baked-sand"):
            try:
                n = int(sys.argv[3])
            except ValueError:
                fail(sys.argv[2], "hole number must be an integer")
            build(sys.argv[5], sys.argv[6], visual_hole=(n, sys.argv[4]), baked_sand=sys.argv[2] == "--baked-sand")
        elif len(sys.argv) == 4 and sys.argv[1] == "dump":
            dump(sys.argv[2], sys.argv[3])
        else:
            print("usage: coursec.py build [--green-mesh|--green-geometry|--visual-hole N VISUAL.txt|--baked-sand N VISUAL.txt]"
                  " COURSEDIR OUT.pdb | dump PDB OUTDIR", file=sys.stderr)
            return 2
    except CourseError as e:
        print("coursec:", e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
