#!/usr/bin/env python3
# Checks the relocations in the Palm objects for references that prc-tools
# links without complaint but that read or jump to the wrong place on the
# device. Exits 1 and lists them if there are any.
#
# prc-tools gcc puts initialised const data in .text, the first code
# segment, but reaches anything it doesn't see defined in the same file
# (an extern const) or anything given a section attribute through A5, as
# if it were in the data segment. It also calls a function in another code
# segment with a plain pc-relative branch unless the declaration carries
# that segment's section attribute. All of these link silently.
#
# Rules checked, for every relocation inside a code section:
#   END16/END32 (A5-relative): the target must not be in a code section.
#   DISP16/DISP32 (pc-relative): the target must be in the same section.
#   32 (absolute, used as an offset from a segment's base): the target must
#      be in a code section.
# Symbols defined in no object come from libgcc or libc, which are in .text.
#
# Usage: segcheck.py OBJ.o ...
import re
import subprocess
import sys

OBJDUMP = "m68k-palmos-objdump"


def run(*args):
    return subprocess.run([OBJDUMP] + list(args), check=True,
                          capture_output=True, text=True).stdout


def sections(obj):
    """Returns ([names by 1-based index], {name: is_code})."""
    names, code = [None], {}
    lines = run("-h", obj).splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"\s*(\d+)\s+(\S+)\s+[0-9a-f]+", line)
        if m:
            name = m.group(2)
            flags = lines[i + 1] if i + 1 < len(lines) else ""
            names.append(name)
            code[name] = "CODE" in flags
    return names, code


def symbols(obj, names):
    """Returns {symbol: (section name or None, is_global_def)}."""
    syms = {}
    for line in run("-t", obj).splitlines():
        m = re.match(r"\[\s*\d+\]\(sec\s+(-?\d+)\).*\(scl\s+(\d+)\).*0x([0-9a-f]+) (\S+)$", line)
        if not m:
            continue
        sec, scl, value, name = int(m.group(1)), int(m.group(2)), int(m.group(3), 16), m.group(4)
        secname = names[sec] if 0 < sec < len(names) else None
        if sec == 0 and scl == 2 and value:
            secname = ".bss"    # a common symbol (uninitialised global), linked into .bss
        syms[name] = (secname, scl == 2 and secname is not None)
    return syms


def relocs(obj):
    out, sec = [], None
    for line in run("-r", obj).splitlines():
        m = re.match(r"RELOCATION RECORDS FOR \[(.*)\]:", line)
        if m:
            sec = m.group(1)
            continue
        m = re.match(r"([0-9a-f]+)\s+(\S+)\s+(\S+)$", line)
        if m and sec:
            target = re.split(r"[+-]0x", m.group(3))[0]
            out.append((sec, int(m.group(1), 16), m.group(2), target))
    return out


def main():
    objs = sys.argv[1:]
    info = {}
    global_sec, code_secs = {}, set([".text"])
    for obj in objs:
        names, code = sections(obj)
        syms = symbols(obj, names)
        info[obj] = (names, code, syms)
        code_secs.update(n for n, c in code.items() if c)
        for name, (sec, glob) in syms.items():
            if glob:
                global_sec[name] = sec
    errors = []
    for obj in objs:
        names, code, syms = info[obj]
        for sec, off, typ, target in relocs(obj):
            if not code.get(sec):
                continue
            if target.startswith("__text__"):
                tsec = ".bss"   # segment base pointers, made by multigen
            elif target in code or target in (".data", ".bss"):
                tsec = target
            elif target in syms and syms[target][0] is not None:
                tsec = syms[target][0]
            else:
                tsec = global_sec.get(target, ".text")
            tcode = tsec in code_secs
            where = "%s %s+0x%x" % (obj, sec, off)
            if typ.startswith("END") and tcode:
                errors.append("%s: %s reaches %s (in code %s) through A5" % (where, typ, target, tsec))
            elif typ.startswith("DISP") and tsec != sec:
                errors.append("%s: %s to %s in %s from %s" % (where, typ, target, tsec, sec))
            elif typ == "32" and not tcode:
                errors.append("%s: absolute %s to %s in %s" % (where, typ, target, tsec))
    for e in errors:
        print("segcheck:", e)
    if errors:
        print("segcheck: %d bad references. Const tables must be static to their file, "
              "and calls into another segment need that segment's attribute on the declaration."
              % len(errors))
        return 1
    return 0


sys.exit(main())
