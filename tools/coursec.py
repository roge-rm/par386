#!/usr/bin/env python3
# Par 386 course compiler. The formats are in doc/course-format.md.
#
#   coursec.py build COURSEDIR OUT.pdb   compile course.txt and hole-NN.txt into a PDB
#   coursec.py dump PDB OUTDIR           write a PDB back out as text and PNGs
#
# Needs Pillow for the heightmap PNGs.
import math
import os
import struct
import sys
import time
from decimal import Decimal

from PIL import Image

COURSE_VERSION = 1    # 2 with a scenery line
HOLE_VERSION = 1
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
OBJECTS = {"tree": 1, "pine": 2, "palm": 3, "bush": 4, "rock": 5, "house": 6,
           "stand": 7, "wall": 8, "gorse": 9, "grass": 10}
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
    course = {"name": None, "tees": [], "holes": None, "scenery": None}
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
        else:
            fail(where, "unknown line %r" % key)
    if not course["name"] or not course["tees"] or not course["holes"]:
        fail(path, "needs name, tee and holes lines")
    if not 1 <= course["holes"] <= MAX_HOLES:
        fail(path, "1 to %d holes" % MAX_HOLES)
    if len(course["tees"]) > MAX_TEES:
        fail(path, "at most %d tees" % MAX_TEES)
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


def hole_record(folder, n, h):
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
    return out


def course_record(course, holes):
    turns = any(h["turn"] is not None for h in holes)
    version = 2 if course["scenery"] is not None or turns else COURSE_VERSION
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


def build(folder, out):
    course = read_course(folder)
    holes = [read_hole(folder, n, course) for n in range(1, course["holes"] + 1)]
    records = [course_record(course, holes)]
    records += [hole_record(folder, n, h) for n, h in enumerate(holes, 1)]
    for i, r in enumerate(records):
        if len(r) > 16384:
            raise CourseError("record %d is %d bytes, over 16 KB" % (i, len(r)))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    write_pdb(out, "Par386 " + course["name"], records)
    print("%s: %d holes, %d bytes in records, sizes %s" % (out, len(holes), sum(len(r) for r in records),
                                                     " ".join(str(len(r)) for r in records)))


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
    with open(os.path.join(outdir, "course.txt"), "w") as f:
        f.write("name %s\n" % cname)
        for t in tees:
            f.write("tee %s\n" % t)
        f.write("holes %d\n" % nholes)
        if version >= 2:
            f.write("scenery %d\n" % c.take("B"))
            turns = [c.take("B") for _ in range(nholes)]
    for n in range(1, nholes + 1):
        c = Cursor(records[n])
        (version, num, par, W, L, hm_w, hm_h, shift, surf, base, step,
         ax, ay) = c.take("HBBHHBBBBhHHH")
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
        for _ in range(c.take("B")):
            s, count = c.take("BB")
            pts = [c.take("HH") for _ in range(count)]
            lines.append("poly %s" % SURFACES[s])
            for i in range(0, count, 6):
                lines.append("    " + " ".join("%s,%s" % (yd(x), yd(y)) for x, y in pts[i:i + 6]))
        for _ in range(c.take("H")):
            t, v, s, x, y = c.take("BBBHH")
            lines.append("object %s %s %s %d %d" % (OBJECT_NAMES.get(t, str(t)), yd(x), yd(y), v, s))
        with open(os.path.join(outdir, "hole-%02d.txt" % n), "w") as f:
            f.write("\n".join(lines) + "\n")
    print("%s: dumped %d holes to %s" % (pdb, nholes, outdir))


def main():
    try:
        if len(sys.argv) == 4 and sys.argv[1] == "build":
            build(sys.argv[2], sys.argv[3])
        elif len(sys.argv) == 4 and sys.argv[1] == "dump":
            dump(sys.argv[2], sys.argv[3])
        else:
            print("usage: coursec.py build COURSEDIR OUT.pdb | dump PDB OUTDIR", file=sys.stderr)
            return 2
    except CourseError as e:
        print("coursec:", e, file=sys.stderr)
        return 1
    return 0


sys.exit(main())
