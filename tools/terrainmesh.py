#!/usr/bin/env python3
"""Bake material contours and a heightmap into a tiled terrain mesh."""
from bisect import bisect_left, bisect_right
from collections import Counter
from functools import lru_cache
from math import floor, hypot
from statistics import median
from struct import pack, unpack_from

EPS = 1e-8
ROUGH = 0
FIRST_CUT = 1
FAIRWAY = 2
DEEP_ROUGH = 3
GREEN = 4
FRINGE = 5
SAND = 6
WATER = 7
OUT = 9
APRON = 96
DUNE_REACH = 56
UNITS = 16
MOW_YARDS = 12
MOW_MID = 67
MOW_CONTRAST = 13
MAX_BYTES = 60000
MANIFEST = b"MSH1"
QUALITY_MANIFEST = b"MSH2"
GRID_MANIFEST = b"MSH3"


def corners(width, length, outlines, col_step, row_step, green_step):
    rows = {-APRON, 0, *range(row_step, length, row_step), length, length + APRON}
    # Keep the slope around the green in smaller tiles.
    for sf, polygon in outlines:
        if sf in (GREEN, FRINGE):
            lo = int(min(y for _, y in polygon)) // green_step * green_step - green_step
            hi = int(max(y for _, y in polygon)) + 2 * green_step
            rows.update(y for y in range(lo, hi, green_step) if 0 < y < length)
    return [-APRON, 0, *range(col_step, width, col_step), width, width + APRON], sorted(rows)


def sweep(outlines, xs, ys, length, dense, grass_height_step, mow_origin, dune_step, flat_water=False):
    """Overlay shape contours and tile cuts into non-overlapping trapezoids."""
    edges = [(ys[0], ys[-1], 0, x, -1) for x in xs]
    sand_step = 24 if outlines[0][0] == SAND else 12
    sand_height_cuts = set(range(0, length + 1, sand_step))
    grass_height_cuts = set(range(0, length + 1, grass_height_step))
    mowing_cuts = set()
    for sf, poly in outlines:
        if sf == FAIRWAY:
            y = [p[1] for p in poly]
            first = ((int(min(y)) - mow_origin) // MOW_YARDS + 1) * MOW_YARDS + mow_origin
            mowing_cuts.update(range(first, int(max(y)) + 1, MOW_YARDS))
    dune_cuts = set(range(0, length + 1, dune_step)) if dune_step else set()
    cuts = set(ys) | sand_height_cuts | grass_height_cuts | mowing_cuts | dune_cuts
    joins = []
    for i, (_, poly) in enumerate(outlines):
        for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
            if ay > by:
                ax, ay, bx, by = bx, by, ax, ay
            if ay == by:
                continue
            m = (bx - ax) / (by - ay)
            edges.append((ay, by, m, ax - m * ay, i))
            cuts.update((ay, by))

    for i, (a0, a1, am, ab, _) in enumerate(edges):
        for j, (b0, b1, bm, bb, _) in enumerate(edges[:i]):
            if abs(am - bm) < EPS:
                continue
            y = (bb - ab) / (am - bm)
            if max(a0, b0) - EPS <= y <= min(a1, b1) + EPS:
                if xs[0] <= am * y + ab <= xs[-1] and ys[0] <= y <= ys[-1]:
                    cuts.add(y)
                    joins.append((round(y * UNITS), i, j))

    sand_cuts = set()
    # Only outlined hazards need the close cuts used for lips and shores.
    hazards = (SAND,) if flat_water else (SAND, WATER)
    for sf, poly in outlines[1:]:
        if sf in hazards:
            yp = [p[1] for p in poly]
            sand_cuts.update(range(int(min(yp)) + 1, int(max(yp)), dense))
    cuts.update(sand_cuts)
    ordered = sorted(cuts)
    pending = {}
    result = []
    for lo, hi in zip(ordered, ordered[1:]):
        if hi - lo < EPS:
            continue
        mid = (lo + hi) * 0.5
        ty = bisect_right(ys, mid) - 1
        crossings = sorted((m * mid + b, i, sf) for i, (a, z, m, b, sf) in
                           enumerate(edges) if a <= mid < z)
        active = set()
        visible = []
        for (left, i, shape), (right, j, _) in zip(crossings, crossings[1:]):
            if shape >= 0:
                active.symmetric_difference_update((shape,))
            if right - left < EPS:
                continue
            tx = bisect_right(xs, (left + right) * 0.5) - 1
            if not 0 <= tx < len(xs) - 1:
                continue
            sf = outlines[max(active)][0] if active else OUT
            if visible and visible[-1][2:] == (sf, tx, ty):
                visible[-1] = (visible[-1][0], j, sf, tx, ty)
            else:
                visible.append((i, j, sf, tx, ty))

        upcoming = {}
        mowing_row = round(lo)
        on_mowing_cut = mowing_row in mowing_cuts and abs(lo - mowing_row) < EPS
        for key in visible:
            sf, tx, ty = key[2:]
            height_cuts = sand_height_cuts if sf in hazards else grass_height_cuts
            if ((lo in height_cuts and 0 < ty < len(ys) - 2) or
                (lo in sand_cuts and sf in hazards) or
                (lo in dune_cuts and sf in (OUT, DEEP_ROUGH) and 0 <= xs[tx] < DUNE_REACH) or
                (on_mowing_cut and sf == FAIRWAY)):
                upcoming[key] = lo
            else:
                upcoming[key] = pending.pop(key, lo)
        result.extend((key, start, lo) for key, start in pending.items())
        pending = upcoming
    result.extend((key, start, ordered[-1]) for key, start in pending.items())
    return edges, result, joins


def turn(a, b, c):
    return (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])


def triangulate(poly):
    """Keep all side samples, including collinear points on shared sides."""
    poly = list(dict.fromkeys(poly))
    while len(poly) > 3:
        ears = []
        for i, b in enumerate(poly):
            a, c = poly[i - 1], poly[(i + 1) % len(poly)]
            area = turn(a, b, c)
            if area <= 0:
                continue
            if any(turn(a, b, p) >= 0 and turn(b, c, p) >= 0 and turn(c, a, p) >= 0
                   for p in poly if p not in (a, b, c)):
                continue
            ears.append((area, i))
        if not ears:
            if any(turn(a, b, c) != 0 for a, b, c in zip(poly, poly[1:], poly[2:])):
                raise ValueError(f"non-simple strip: {poly}")
            return
        _, i = max(ears)
        yield poly[i - 1], poly[i], poly[(i + 1) % len(poly)]
        del poly[i]
    if len(poly) == 3 and turn(*poly):
        yield tuple(poly)


def tessellate(edges, spans, joins, xs, ys, step, sand_step, water_step):
    rows, sides, patches = {}, {}, []
    xsq = [x * UNITS for x in xs]
    ysq = [y * UNITS for y in ys]

    aliases = {}
    group = {}
    for i, (_, _, slope, x, _) in enumerate(edges):
        if not slope and x in xs:
            aliases[i] = xs.index(x)

    def edge_x(edge, y):
        _, _, m, b, _ = edges[edge]
        return round(m * y + b * UNITS)

    def representative(key):
        while key in group:
            key = group[key]
        return key

    for y, a, b in joins:
        x, z = representative((y, aliases.get(a, a))), representative((y, aliases.get(b, b)))
        if x != z:
            # A tile boundary owns its exact x coordinate.
            group[max(x, z)] = min(x, z)

    def at(edge, y, tx):
        _, key = representative((y, aliases.get(edge, edge)))
        return max(xsq[tx], min(xsq[tx + 1], edge_x(key, y)))

    for (left, right, sf, tx, ty), lo, hi in spans:
        y0, y1 = round(lo * UNITS), round(hi * UNITS)
        if y0 == y1:
            continue
        a, b = at(left, y0, tx), at(right, y0, tx)
        c, d = at(left, y1, tx), at(right, y1, tx)
        # Tiny intersection wedges sometimes collapse onto one edge on rounding.
        if b < a:
            a = b = (a + b) // 2
        if d < c:
            c = d = (c + d) // 2
        left, right = aliases.get(left, left), aliases.get(right, right)
        for y, l, r in ((y0, a, b), (y1, c, d)):
            row = rows.setdefault(y, set())
            row.update((l, r))
            spacing = sand_step if sf == SAND else water_step if sf == WATER else step
            if tx in (0, len(xs) - 2) and ty in (0, len(ys) - 2):
                spacing = APRON
            spacing *= UNITS
            row.update(range((l // spacing + 1) * spacing, r, spacing))
            sides.setdefault((tx, left), {})[y] = l
            sides.setdefault((tx, right), {})[y] = r
            # The sentinel edge separating two x tiles is shared by both.
            if left == tx:
                sides.setdefault((tx - 1, left), {})[y] = xsq[tx]
            if right == tx + 1:
                sides.setdefault((tx + 1, right), {})[y] = xsq[tx + 1]
        patches.append((tx, ty, y0, y1, a, b, c, d, sf, left, right))

    rows = {y: sorted(points) for y, points in rows.items()}
    sides = {edge: sorted(points.items()) for edge, points in sides.items()}
    result = {}
    for tx, ty, y0, y1, a, b, c, d, sf, left, right in patches:
        near, far = rows[y0], rows[y1]
        bottom = [(x, y0) for x in near[bisect_left(near, a):bisect_right(near, b)]]
        top = [(x, y1) for x in reversed(far[bisect_left(far, c):bisect_right(far, d)])]
        east = [(x, y) for y, x in sides[tx, right] if y0 < y < y1]
        west = [(x, y) for y, x in reversed(sides[tx, left]) if y0 < y < y1]
        bbox = xsq[tx], ysq[ty], xsq[tx + 1], ysq[ty + 1]
        result.setdefault(bbox, []).extend((tri, sf) for tri in triangulate(bottom + east + top + west))
    return result


def simplify_outline(poly, tolerance):
    def simplify(points):
        if len(points) < 3:
            return points
        (ax, ay), (bx, by) = points[0], points[-1]
        dx, dy = bx - ax, by - ay
        span = dx * dx + dy * dy
        errors = []
        for i, (x, y) in enumerate(points[1:-1], 1):
            t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / span)) if span else 0
            errors.append((hypot(x - ax - t * dx, y - ay - t * dy), i))
        error, i = max(errors)
        if error <= tolerance:
            return [points[0], points[-1]]
        return simplify(points[:i + 1])[:-1] + simplify(points[i:])

    a = min(range(len(poly)), key=lambda i: poly[i])
    loop = poly[a:] + poly[:a]
    b = max(range(len(loop)), key=lambda i: loop[i])
    reduced = simplify(loop[:b+1])[:-1] + simplify(loop[b:] + loop[:1])[:-1]
    return reduced if len(reduced) >= 3 else poly


def round_cut(poly, tolerance, bounds=None):
    """Round each corner locally, within the two neighbouring edges."""
    rounded = []
    for a, b, c in zip(poly[-1:] + poly[:-1], poly, poly[1:] + poly[:1]):
        start, end = hypot(a[0] - b[0], a[1] - b[1]), hypot(c[0] - b[0], c[1] - b[1])
        if not start or not end or (bounds and (b[0] in (0, bounds[0]) or b[1] in (0, bounds[1]))):
            rounded.append(b)
            continue
        reach = min(2.5, start / 4, end / 4)
        head = tuple(b[i] + (a[i] - b[i]) * reach / start for i in range(2))
        tail = tuple(b[i] + (c[i] - b[i]) * reach / end for i in range(2))
        rounded.extend(tuple((1-t)**2 * head[i] + 2*t*(1-t) * b[i] + t*t * tail[i]
                             for i in range(2)) for t in (0, .25, .5, .75, 1))
    return simplify_outline(rounded, tolerance)


def snap_shallow_edges(outlines, width, length):
    """Keep shallow hazard crossings from merging along a quantised row."""
    hazards = []
    for surface, points in outlines:
        if surface in (SAND, WATER):
            xs, ys = zip(*points)
            hazards.append((min(xs), max(xs), min(ys), max(ys)))
    if not hazards:
        return outlines

    result = [outlines[0]]
    for surface, points in outlines[1:]:
        adjusted = list(points)
        for i, ((ax, ay), (bx, by)) in enumerate(zip(points, points[1:] + points[:1])):
            rise = abs(by - ay)
            if rise == 0 or rise > 3 / 8 or abs(bx - ax) <= 32 * rise:
                continue
            left, right = sorted((ax, bx))
            low, high = sorted((ay, by))
            if not any(left <= x1 and x0 <= right and low <= y1 and y0 <= high
                       for x0, x1, y0, y1 in hazards):
                continue
            # Hold vertices at the course boundary to keep the apron joined.
            fixed_a = ax in (0, width) or ay in (0, length)
            fixed_b = bx in (0, width) or by in (0, length)
            if fixed_b or (not fixed_a and ay > by):
                if not fixed_a:
                    adjusted[i] = (ax, by)
            else:
                adjusted[(i + 1) % len(points)] = (bx, ay)
        result.append((surface, adjusted))
    return result


def bilinear(samples, w, h, x, y):
    x = min(max(x, 0), w - 1)
    y = min(max(y, 0), h - 1)
    ix, iy = min(int(x), w - 2), min(int(y), h - 2)
    sx, sy = x - ix, y - iy
    a = samples[iy * w + ix] * (1 - sx) + samples[iy * w + ix + 1] * sx
    b = samples[(iy + 1) * w + ix] * (1 - sx) + samples[(iy + 1) * w + ix + 1] * sx
    return a * (1 - sy) + b * sy


def water_apron(outlines, width, length):
    """Carry water past the four course edges into the viewing apron."""
    if not any(sf == WATER for sf, _ in outlines):
        return
    dimensions = (width, length)
    for axis, size in enumerate(dimensions):
        along = 1 - axis
        extent = dimensions[along]
        for edge, outside in ((0, -APRON), (size, size + APRON)):
            events = {}
            for i, (_, poly) in enumerate(outlines):
                for a, b in zip(poly, poly[1:] + poly[:1]):
                    if a[axis] == edge == b[axis] and a[along] != b[along]:
                        events.setdefault(a[along], []).append(i)
                        events.setdefault(b[along], []).append(i)
            active = set()
            merged = []
            cuts = sorted(events)
            for start, end in zip(cuts, cuts[1:]):
                for i in events[start]:
                    active.symmetric_difference_update((i,))
                if active and outlines[max(active)][0] == WATER:
                    if merged and start == merged[-1][1]:
                        merged[-1][1] = end
                    else:
                        merged.append([start, end])
            for start, end in merged:
                start = -APRON if start == 0 else start
                end = extent + APRON if end == extent else end
                quad = [(edge, start), (outside, start), (outside, end), (edge, end)]
                yield WATER, [(y, x) for x, y in quad] if axis else quad


def water_levels(groups, terrain, bounds=None):
    """Keep each connected body level and share its shore with the land mesh."""
    parents = {}
    edges = Counter()

    def root(p):
        while p != parents[p]:
            parents[p] = parents[parents[p]]
            p = parents[p]
        return p

    for faces in groups.values():
        for (a, b, c), sf in faces:
            if sf != WATER:
                continue
            for p in (a, b, c):
                parents.setdefault(p, p)
            parents[root(b)] = root(a)
            parents[root(c)] = root(a)
            for pair in ((a, b), (b, c), (c, a)):
                edges[tuple(sorted(pair))] += 1
    banks = {}
    for (a, b), count in edges.items():
        if count == 1:
            banks.setdefault(root(a), set()).update((a, b))
    levels = {}
    for p, shore in banks.items():
        on_course = [v for v in shore if bounds and 0 <= v[0] <= bounds[0] * UNITS
                     and 0 <= v[1] <= bounds[1] * UNITS]
        levels[p] = median(terrain(x / UNITS, y / UNITS) for x, y in (on_course or shore))
    return {p: levels[root(p)] for p in parents}


def bake(width, length, nx, ny, cell, base, z0, slope, heights, shapes, sand, compact=False,
         layout=None, putting=(), putting_tolerance=0, dunes=False, round_mown=False,
         round_natural=False, grass_spacing=0, natural_tolerance=None, round_mown_edges=True,
         shared_hazards=False):
    col_step, row_step, dense, first_cut_tol = layout or (
        (64, 54, 8, 0.5) if compact else (44, 36, 4, 0.5))
    boundary = [(0, 0), (width, 0), (width, length), (0, length)]
    outlines = [(base, boundary)]
    hazards = []
    for i, (sf, polygon) in enumerate(shapes):
        points = [(x / UNITS, y / UNITS) for x, y in polygon]
        preserve = False
        if shared_hazards:
            xs, ys = zip(*polygon)
            left, right, low, high = min(xs), max(xs), min(ys), max(ys)
            preserve = base in (SAND, WATER) or any(
                left <= x1 and x0 <= right and low <= y1 and y0 <= high
                for x0, x1, y0, y1 in hazards)
            if sf in (SAND, WATER):
                preserve = True
                hazards.append((left, right, low, high))
        if preserve:
            # Later materials can form part of a hazard's edge.
            pass
        elif round_mown and round_mown_edges and sf in (FIRST_CUT, FAIRWAY):
            points = round_cut(points, first_cut_tol if compact or grass_spacing else 0.3)
        elif (round_natural and natural_tolerance != 0 and i not in putting
              and sf in (ROUGH, DEEP_ROUGH, SAND, WATER)):
            tolerance = (0.45 if compact else 0.18) if natural_tolerance is None else natural_tolerance
            points = round_cut(points, tolerance, (width, length))
        else:
            tolerance = (putting_tolerance if i in putting else
                         0.18 if sf in (GREEN, FRINGE, SAND, WATER) else
                         first_cut_tol if sf == FIRST_CUT else 0.5)
            points = simplify_outline(points, tolerance)
        outlines.append((sf, points))
    if shared_hazards:
        outlines = snap_shallow_edges(outlines, width, length)
    if round_mown:
        mow_origin = min((y // UNITS for sf, poly in shapes if sf == FAIRWAY for _, y in poly),
                         default=0)
    else:
        mow_origin = floor(min((y for sf, poly in outlines if sf == FAIRWAY for _, y in poly),
                               default=0)) - 1
    green_step = 6 if base == SAND else 9
    xs, ys = corners(width, length, outlines, col_step, row_step, green_step)
    grass_height_step, spacing = (72, 64) if compact else (36, 32)
    if grass_spacing:
        grass_height_step = max(grass_height_step, grass_spacing)
        spacing = max(spacing, grass_spacing)
    sand_spacing = (24 if compact else 16) if base == SAND else dense
    dune_step = 0
    if dunes:
        # Sample the beach and both dune crests along the left edge.
        xs = sorted(set(xs) | {x for x in (8, 24, 40, 48) if x < width})
        dune_step = 32 if compact else 24
    apron = list(water_apron(outlines, width, length)) if round_natural or shared_hazards else []
    outlines.extend(apron)
    water_spacing = 32 if round_natural else dense
    groups = tessellate(*sweep(outlines, xs, ys, length, dense, grass_height_step,
                               mow_origin, dune_step, flat_water=round_natural),
                       xs, ys, spacing, sand_spacing, water_spacing)

    @lru_cache(maxsize=None)
    def terrain(x, y):
        return z0 + slope * bilinear(heights, nx, ny, x / cell, y / cell)

    water = water_levels(groups, terrain, (width, length) if apron else None)
    shared_sand = any(shapes[i][0] == SAND for i in putting)
    # Shared vertices follow the heightmap where a green or fringe covers sand.
    green_turf = {p for faces in groups.values() for face, sf in faces
                  if sf in (GREEN, FRINGE) for p in face}

    @lru_cache(maxsize=None)
    def ground(x, y):
        height = terrain(x, y)
        drop = 0
        for x0, y0, w, h, samples in sand:
            if x0 <= x <= x0 + w - 1 and y0 <= y <= y0 + h - 1:
                d = min(max(bilinear(samples, w, h, x - x0, y - y0) / 64, 0), 1)
                cut = 0.4 * d * d * (3 - 2 * d)
                if shared_sand:
                    # The gameplay outlines can overlap within one bunker.
                    drop = max(drop, cut)
                else:
                    height -= cut
        return height - drop

    def directional(height, x, y, reach):
        gx = height(x + reach, y) - height(x - reach, y)
        gy = height(x, y + reach) - height(x, y - reach)
        return (1.15 * gx + 0.75 * gy) * (0.75 / reach)

    green_edge = [p for sf, poly in outlines if sf == GREEN for p in poly]
    green_level = sum(terrain(x, y) for x, y in green_edge) / len(green_edge) if green_edge else z0

    @lru_cache(maxsize=None)
    def green_relief(x, y):
        # The wide height gradient shows the rise when the putting slope is gentle.
        level = terrain(x, y) - green_level
        return 28 * level + 58 * directional(terrain, x, y, 4)

    @lru_cache(maxsize=None)
    def sand_slope(x, y):
        # Hold the long slope and soften the light change at the shallow lip.
        return directional(terrain, x, y, 3) * 0.6 + directional(ground, x, y, 3) * 0.4

    @lru_cache(maxsize=None)
    def sand_distance(x, y):
        # Take the nearest unbroken lip where two sand atlas tiles overlap.
        return max((bilinear(samples, w, h, x - x0, y - y0) / 32
                    for x0, y0, w, h, samples in sand
                    if x0 <= x <= x0 + w - 1 and y0 <= y <= y0 + h - 1), default=-2)

    @lru_cache(maxsize=None)
    def sand_lip(x, y):
        distance = max(0, sand_distance(x, y))
        width = 2.0
        if distance >= width:
            return 0
        u = 1 - distance / width
        dx = sand_distance(x + 0.5, y) - sand_distance(x - 0.5, y)
        dy = sand_distance(x, y + 0.5) - sand_distance(x, y - 0.5)
        length = hypot(dx, dy)
        direction = (0.84 * dx + 0.55 * dy) / length if length else 0
        return u * u * (3 - 2 * u) * (0.52 + 0.48 * max(0, direction))

    def light(tri, sf):
        x, y = (sum(p[i] for p in tri) / (3 * UNITS) for i in range(2))
        if sf == WATER:
            return 72
        if sf == SAND:
            if base == SAND:
                dune = directional(terrain, x, y, 6)
                return round(max(36, min(112, 80 + dune * 88)))
            edge = sum(sand_slope(u / UNITS, v / UNITS) for u, v in tri) / 3
            shade = 80 + 47 * (sand_slope(x, y) + edge)
            # Sample inside each face to keep the shadow local to the lip.
            lip = 2 * sand_lip(x, y) + sum(sand_lip(0.25 * x + 0.75 * u / UNITS,
                                                 0.25 * y + 0.75 * v / UNITS) for u, v in tri)
            return round(max(46, min(105, max(64, shade) - 6.8 * lip)))
        if sf in (GREEN, FRINGE):
            relief = (3 * green_relief(x, y) +
                      sum(green_relief(u / UNITS, v / UNITS) for u, v in tri)) / 6
            if sf == GREEN:
                return round(max(48, min(86, 63 + 1.8 * relief)))
            return round(max(44, min(70, 53 + 0.75 * relief)))
        if dunes and sf == OUT and 0 <= x < DUNE_REACH and 0 <= y < length:
            return round(max(18, min(112, 68 + directional(ground, x, y, 4) * 112)))
        sun = directional(ground, x, y, 4 if compact else 0.75)
        if sf == FAIRWAY:
            dark = int((y - mow_origin) // MOW_YARDS) & 1
            band = -MOW_CONTRAST if dark else MOW_CONTRAST
            shade = round(max(42, min(93, MOW_MID + band + sun * (30 if compact else 10))))
            # Keep the baked shade on its band's side of MOW_MID.
            return min(shade, MOW_MID - 1) if dark else max(shade, MOW_MID)
        if sf == FIRST_CUT:
            return round(max(47, min(77, 62 + sun * (32 if compact else 16))))
        if sf == ROUGH:
            return round(max(55, min(83, 70 + sun * (32 if compact else 16))))
        if sf == DEEP_ROUGH:
            return round(max(84, min(108, 94 + sun * (32 if compact else 16))))
        return round(max(47, min(82, 62 + sun * 16)))

    result = []
    for bbox, faces in sorted(groups.items()):
        if not faces:
            continue
        verts, indices, tris = [], {}, []
        for face, sf in faces:
            vi = []
            for x, y in face:
                if (x, y) not in indices:
                    indices[x, y] = len(verts)
                    z = water.get((x, y))
                    if z is None:
                        height = terrain if (x, y) in green_turf else ground
                        z = height(x / UNITS, y / UNITS)
                    verts.append((x, y, round(z * 256)))
                vi.append(indices[x, y])
            tris.append((*vi, sf, light(face, sf)))
        result.append((bbox, verts, tris))
    return result


def encode(tiles):
    if len(tiles) > 512:
        raise ValueError("more than 512 tiles")
    offset = 2 + 16 * len(tiles)
    header = bytearray(pack(">H", len(tiles)))
    body = bytearray()
    for box, verts, tris in tiles:
        if not 1 <= len(verts) <= 255 or not 1 <= len(tris) <= 512:
            raise ValueError(f"tile {box} needs {len(verts)} vertices and {len(tris)} triangles")
        header += pack(">hhhhHHI", *box, len(verts), len(tris), offset + len(body))
        body += b"".join(pack(">hhh", *v) for v in verts)
        body += b"".join(pack(">BBBBB", *t) for t in tris)
    return header + body


class MeshEdgeError(ValueError):
    """A tile face has an unmatched edge."""


def match_collinear_edges(tiles):
    """Split a long side where its neighbours already share collinear vertices."""
    uses = Counter()
    for _, vertices, triangles in tiles:
        for a, b, c, _, _ in triangles:
            for i, j in ((a, b), (b, c), (c, a)):
                uses[tuple(sorted((vertices[i], vertices[j])))] += 1
    ends = {}
    for (a, b), count in uses.items():
        if count == 1:
            ends.setdefault(a, set()).add(b)

    cuts = {}
    for a, targets in ends.items():
        for b in targets:
            side = []
            p = a
            while inside := [q for q in ends.get(p, ()) if p < q < b and not turn(a, b, q)]:
                p = min(inside)
                side.append(p)
            if side and b in ends.get(p, ()):
                cuts[a, b] = side
                cuts[b, a] = side[::-1]
    if not cuts:
        raise MeshEdgeError("no matching subdivisions for terrain edges")

    for box, vertices, triangles in tiles:
        pool = list(vertices)
        indices = {p: i for i, p in enumerate(pool)}
        result = []
        for a, b, c, surface, shade in triangles:
            face = [vertices[i] for i in (a, b, c)]
            outline = []
            for u, v in zip(face, face[1:] + face[:1]):
                outline.extend((u, *cuts.get((u, v), ())))
            if len(outline) == 3:
                result.append((a, b, c, surface, shade))
                continue
            for point in outline:
                if point not in indices:
                    indices[point] = len(pool)
                    pool.append(point)
            result.extend((*map(indices.__getitem__, tri), surface, shade) for tri in triangulate(outline))
        yield box, pool, result


def bake_record(width, length, nx, ny, cell, base, z0, slope, heights, shapes, sand,
                compact=False, putting=(), dunes=False, round_mown=False, round_natural=False,
                shared_hazards=False):
    """Pack a validated mesh, retrying crowded tiles and thin first-cut wedges."""
    layouts = ((60, 46, 8, 0.5), (64, 54, 8, 0.75)) if compact else (
        (44, 36, 5, 0.5), (42, 36, 5, 0.5), (44, 36, 5, 0.75))
    first_error = None
    tolerances = (0,)
    if putting:
        tolerances = (1 / 8, 1 / 16, 0) if compact else (0, 1 / 16, 1 / 8)
    attempts = [(layout, tolerance, 0, None, True) for tolerance in tolerances for layout in (None, *layouts)]
    if round_natural or shared_hazards:
        # Reduce long hazard strips and subdivide busy greens before giving up their outlines.
        layouts = ((50, 48, 10, 0.65), (42, 48, 12, 0.65), (34, 54, 14, 0.8)) if compact else (
            (42, 42, 6, 0.45), (36, 48, 8, 0.55), (32, 54, 10, 0.65), (30, 60, 12, 0.8))
        tolerances = (1 / 8, 1 / 16) if compact else (1 / 16, 1 / 8)
        spacing = 64 if compact else 48
        attempts = [(None, tolerances[0], 0, None, True)] + [
            (layout, tolerance, spacing, natural_tolerance, True)
            for natural_tolerance in (None, 0.55, 0)
            for tolerance in tolerances for layout in layouts
        ] + attempts
        # Preserve the authored mown edge when added curves form narrow crossings.
        attempts += [(None, tolerance, 0, None, False) for tolerance in tolerances] + [
            ((36, 48, 8, 0.55), 1 / 8, spacing, None, False)
        ]
    if shared_hazards:
        attempts += [
            (layout, 1 / 8, 128, 0, False)
            for layout in ((42, 42, 16, 0.65), (32, 42, 24, 0.75), (28, 32, 24, 0.8))
        ]
    for layout, tolerance, grass_spacing, natural_tolerance, round_mown_edges in attempts:
        try:
            tiles = bake(width, length, nx, ny, cell, base, z0, slope, heights, shapes,
                         sand, compact=compact, layout=layout, putting=putting,
                         putting_tolerance=tolerance, dunes=dunes, round_mown=round_mown,
                         round_natural=round_natural, grass_spacing=grass_spacing,
                         natural_tolerance=natural_tolerance, round_mown_edges=round_mown_edges,
                         shared_hazards=shared_hazards)
            data = encode(tiles)
            try:
                validate(data, width, length)
            except MeshEdgeError:
                if not (round_natural or shared_hazards):
                    raise
                data = encode(list(match_collinear_edges(tiles)))
                validate(data, width, length)
            if tolerance:
                print("Shared putting contours simplified by up to %.3g yards" % tolerance)
            return data
        except ValueError as error:
            if first_error is None:
                first_error = error
    raise first_error


def sand_samples(atlas):
    """Read height samples from the course compiler's temporary bunker atlas."""
    tiles = []
    at = 1
    for _ in range(atlas[0]):
        x, y, w, h = unpack_from(">HHHH", atlas, at)
        at += 8
        samples = atlas[at:at + 2 * w * h:2]
        tiles.append((x, y, w, h, [v if v < 128 else v - 256 for v in samples]))
        at += 2 * w * h
    return tiles


def record_indices(records, holes):
    """Read the optional table after the ordinary hole records."""
    table = records[holes + 1] if len(records) > holes + 1 else b""
    magic = table[:4]
    stride = {MANIFEST: 1, QUALITY_MANIFEST: 2, GRID_MANIFEST: 3}.get(magic)
    if stride is None:
        return ()
    count = holes * stride
    if len(table) != 4 + 2 * count:
        raise ValueError("wrong manifest size")
    indices = unpack_from(">%dH" % count, table, 4)
    used = [i for i in indices if i]
    if not used or used != list(range(holes + 2, len(records))):
        raise ValueError("invalid mesh record indices")
    if magic == MANIFEST:
        return [(i, 0) for i in indices]
    if magic == GRID_MANIFEST and not all(indices[2::stride]):
        raise ValueError("missing lie-grid record")
    pairs = list(zip(indices[::stride], indices[1::stride]))
    if magic == QUALITY_MANIFEST and not any(compact for _, compact in pairs):
        raise ValueError("quality manifest has no compact meshes")
    if any(compact and not detailed for detailed, compact in pairs):
        raise ValueError("compact mesh without detailed mesh")
    return pairs


def validate(data, width, length):
    """Check the bounded record and conforming surface, including from a dump."""
    if len(data) < 2 or len(data) > MAX_BYTES:
        raise ValueError(f"record must be 2 to {MAX_BYTES} bytes")
    count, = unpack_from(">H", data)
    offset = 2 + 16 * count
    if not 1 <= count <= 512 or offset > len(data):
        raise ValueError("invalid tile count")
    edges = Counter()
    area = 0
    xmin = ymin = -APRON * UNITS
    xmax, ymax = (width + APRON) * UNITS, (length + APRON) * UNITS
    for tile in range(count):
        x0, y0, x1, y1, nv, nt, start = unpack_from(">hhhhHHI", data, 2 + 16 * tile)
        end = start + 6 * nv + 5 * nt
        if (start != offset or end > len(data) or not 3 <= nv <= 255 or not 1 <= nt <= 512 or
                not xmin <= x0 < x1 <= xmax or not ymin <= y0 < y1 <= ymax):
            raise ValueError(f"invalid tile {tile}")
        verts = [unpack_from(">hhh", data, start + 6 * i) for i in range(nv)]
        if any(not (x0 <= x <= x1 and y0 <= y <= y1) for x, y, _ in verts):
            raise ValueError(f"vertex outside tile {tile}")
        for k in range(start + 6 * nv, end, 5):
            i, j, l, sf, shade = data[k:k + 5]
            if max(i, j, l) >= nv or sf >= 11 or shade > 112:
                raise ValueError(f"invalid face in tile {tile}")
            a, b, c = verts[i], verts[j], verts[l]
            cross = turn(a, b, c)
            if cross <= 0:
                raise ValueError(f"flat or reversed face in tile {tile}")
            area += cross
            for edge in ((a, b), (b, c), (c, a)):
                edges[tuple(sorted(edge))] += 1
        offset = end
    if offset != len(data):
        raise ValueError("extra bytes after tiles")
    for (a, b), uses in edges.items():
        outer = (a[0] == b[0] and a[0] in (xmin, xmax)) or (a[1] == b[1] and a[1] in (ymin, ymax))
        if uses != (1 if outer else 2):
            raise MeshEdgeError(f"nonmatching edge at {a[:2]} to {b[:2]}")
    if area != 2 * (xmax - xmin) * (ymax - ymin):
        raise ValueError("mesh does not fill the terrain bounds")
