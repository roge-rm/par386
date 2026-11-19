# Course format

## World units

A world unit is 1 yard, held as 16.16 fixed point. Golf distances are in yards, and squared speeds and distances of a ball in flight (80 yd/s squared is 6400) stay inside 16.16, where feet would overflow.

Each hole has its own flat map: x runs across from the left edge, y runs along the hole from the tee end, both from 0. A hole is at most 256 yards wide and 640 long. Heights are in yards too.

Angles are 16 bits, 65536 to a full turn, 0 along +x and turning toward +y.

## Database

One Palm database per course: type `Crse`, creator `P386`, name `Par386 <Course>` (31 characters at most). Record 0 is the course header, record N is hole N. All numbers are big-endian and read a byte at a time.

Points in records are in quarter yards (u16), and must lie on the hole (0 to width × 4, 0 to length × 4).

### Course header (record 0)

| Bytes | What |
|---|---|
| u16 | version, 1, 2 or 3 |
| u8 | holes, 1 to 36 |
| u8 | tee sets, 1 to 4 |
| name | course name, 1 to 31 characters |
| name × tee sets | tee set names, 1 to 15 characters each |
| per hole: u8, u16 | par, yards from the longest tee |
| u8 | version 2 and up: scenery, the sky, backdrop and object art (0 parkland, 1 links, 2 Irish clifftop, 3 Japanese seaside, 4 Australian sandbelt, 5 South African bush, 6 Canadian Rockies) |
| u8 × holes | version 2 and up: each hole's map turned against the course, 256 a turn, which moves the backdrop |
| u8 | version 3 only: the average wind in mph, 0 to 40, 0 for the usual 10. Each hole's wind is 0 to twice that |

A name is a length byte and that many Latin-1 characters, no nul.

### Hole (records 1 to N)

| Bytes | What |
|---|---|
| u16 | version, 1 |
| u8 | hole number |
| u8 | par, 3 to 6 |
| u16, u16 | width and length in yards |
| u8, u8 | heightmap points across and along (2 to 64, 2 to 128) |
| u8 | log2 of the yards between heightmap points (1 to 5) |
| u8 | surface type under everything |
| s16 | height of byte 0, 1/16 yards |
| u16 | height per byte step, 1/4096 yards |
| point | aim point for the first shot |
| name | hole name, 0 to 31 characters |
| u8 | tees, 1 to 4, then per tee: u8 tee set, point, u16 yards |
| point × 4 | pin positions |
| bytes | heightmap, across × along, row 0 at y = 0 |
| u8 | shapes, then per shape: u8 surface, u8 points (3 to 64), points |
| u16 | objects, then per object: u8 type, u8 variant, u8 scale (64 is normal), point |

The heightmap must cover the hole: (points − 1) × spacing is at least the width and length. Heights between points are bilinear and held level past the edge.

The test hole is 2816 bytes. A 32 × 64 heightmap is 2 KB of that, and it doesn't pack down much, so records aren't compressed. The loader rejects a record that is short, long or has any value out of range.

Surface types: 0 rough, 1 first cut, 2 fairway, 3 deep rough, 4 green, 5 fringe, 6 bunker, 7 water, 8 cart path, 9 out of bounds, 10 tee box.

Object types: 1 tree, 2 pine, 3 palm, 4 bush, 5 rock, 6 house, 7 stand (a grandstand), 8 wall (a dry-stone section 6 yd long at scale 64), 9 gorse, 10 grass (dune grass), 11 landmark (16 yd high: the course's kirk, lighthouse, pagoda, windmill, baobab or hotel), 12 animal (1.5 yd: sheep, kangaroo, crane, crocodile, hippo or elk). Other numbers load and are left for later.

Each scenery number has its own sprites for some types (tools/mkstyle.py lists them), in variant 0 and sometimes 1, such as the cherry and the maple for Sakura Bay trees; types it has none for use the shared art, and a variant it has no art for draws as variant 0. Scenery 1 to 3 have their own backdrop; 4 to 6 have object art but no backdrop yet.

## Surface grid

When a hole loads, its shapes are drawn into a grid of 2 yard cells, 4 bits each (two to a byte, even column in the high nibble), row 0 at y = 0. Shapes are filled even-odd at the cell centres, in record order, so later shapes win. The biggest hole makes a 128 × 320 cell grid, 20 KB, written a row at a time so the Palm can keep it in a storage record. Off the grid counts as out of bounds, except past a water cell on the edge, where the water carries on (the sea off a clifftop hole).

## Course source

`tools/coursec.py build COURSEDIR OUT.pdb` compiles a course folder, and `coursec.py dump PDB OUTDIR` writes one back out. `make courses` builds every folder in `courses/`.

`course.txt`:

```
name Test
tee Blue          # tee sets, longest first
tee White
holes 18          # reads hole-01.txt to hole-18.txt
scenery 1         # optional, makes a version 2 header
wind 15           # optional, average mph, makes a version 3 header
```

`hole-NN.txt`, yards with decimals allowed, `#` starts a comment and an indented line carries on the one above:

```
name Elbow Pond
par 4
size 200 440                  # width, length
heightmap hole-01.png 8       # greyscale PNG, yards between pixels
height 0 0.0625               # yards at black, yards per grey step
base rough                    # surface where no shape is
aim 62 240
turn 90                       # optional, degrees, makes a version 2 header
tee Blue 60 12 [yards]        # yards left out: tee to aim to the middle of the pins
pin 140 405                   # four of these
poly fairway 50,50 78,50 84,120 ...
rect tee 52 6 68 22
ellipse bunker 38 236 7 10 [points] [degrees]
object pine 16 60 [variant] [scale]
```

Surface names: rough, first-cut, fairway, deep-rough, green, fringe, bunker, water, path, oob, tee. Object names: tree, pine, palm, bush, rock, house, stand, wall, gorse, grass, landmark, animal, or a number.

Auld Kirk (`courses/auldkirk/`) is made by `tools/mkauldkirk.py` (`make auldkirk`), which writes the hole files and heightmaps from a table of centre lines, greens and features. Carrig Head and Sakura Bay are made the same way by `tools/mkcarrig.py` and `tools/mksakura.py`.

The heightmap's top row is the far end of the hole, so the PNG looks like a course map.
