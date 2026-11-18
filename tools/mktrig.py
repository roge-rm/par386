#!/usr/bin/env python3
# Writes src/core/trigtab.h: the quarter-wave sine table and the atan table
# that src/core/trig.c includes. Run it from the repo root after changing it,
# the output is committed.
import math
import os

QUARTER = 256    # sine steps per quarter turn
ATAN = 128       # atan steps for a ratio of 0 to 1


def rows(vals, per=12):
    out = []
    for i in range(0, len(vals), per):
        out.append("\t" + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    return "\n".join(out)


def main():
    sin = [round(math.sin(i * math.pi / 2 / QUARTER) * 16384) for i in range(QUARTER + 1)]
    # Binary angle: 8192 is 45 degrees.
    atan = [round(math.atan(i / ATAN) * 32768 / math.pi) for i in range(ATAN + 1)]
    path = os.path.join(os.path.dirname(__file__), "..", "src", "core", "trigtab.h")
    with open(path, "w") as f:
        f.write("/* Made by tools/mktrig.py, don't edit. Only trig.c includes it. */\n")
        f.write("#define SIN_STEPS %d\n#define ATAN_STEPS %d\n\n" % (QUARTER, ATAN))
        f.write("/* sin over a quarter turn, 2.14 (16384 = 1). */\n")
        f.write("static const int16_t sin_tab[%d] = {\n%s\n};\n\n" % (QUARTER + 1, rows(sin)))
        f.write("/* atan(i / %d) as a binary angle (65536 = full turn). */\n" % ATAN)
        f.write("static const uint16_t atan_tab[%d] = {\n%s\n};\n" % (ATAN + 1, rows(atan)))


main()
