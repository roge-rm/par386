"""Keep one terrain mesh per hole in a compiled Par 386 course."""

import os
import struct
import sys

import terrainmesh
from coursec import CourseError, Cursor, fail, read_pdb, write_pdb


def compact(pdb, out):
    name, kind, creator, records = read_pdb(pdb)
    if kind != b"Crse" or creator != b"P386" or not records:
        fail(pdb, "isn't a Par 386 course")
    _, holes, _ = Cursor(records[0]).take("HBB")
    try:
        meshes = terrainmesh.record_indices(records, holes)
    except ValueError as e:
        fail(pdb, "terrain mesh: %s" % e)
    table = records[holes + 1] if len(records) > holes + 1 else b""
    if not meshes or not table.startswith(terrainmesh.GRID_MANIFEST):
        fail(pdb, "rebuild the course with lie grids first")
    grids = struct.unpack_from(">%dH" % (holes * 3), table, 4)[2::3]
    indices, extras = [], []
    for (detailed, small), grid in zip(meshes, grids):
        mesh = small or detailed
        mesh_index = 0
        if mesh:
            mesh_index = holes + 2 + len(extras)
            extras.append(records[mesh])
        # Both render modes fall back to the first slot when the second is empty.
        indices.extend((mesh_index, 0, holes + 2 + len(extras)))
        extras.append(records[grid])
    table = terrainmesh.GRID_MANIFEST + struct.pack(">%dH" % len(indices), *indices)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    write_pdb(out, name, records[:holes + 1] + [table] + extras)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: compact_course.py INPUT.pdb OUTPUT.pdb")
    try:
        compact(sys.argv[1], sys.argv[2])
    except (CourseError, OSError, struct.error) as e:
        sys.exit(f"compact_course: {e}")
