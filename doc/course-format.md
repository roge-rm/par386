# Course format

## World units

A world unit is 1 yard, held as 16.16 fixed point. Golf distances are in yards, and squared speeds and distances of a ball in flight (80 yd/s squared is 6400) stay inside 16.16, where feet would overflow.

Each hole has its own flat map: x runs across from the left edge, y runs along the hole from the tee end, both from 0. A hole is at most 256 yards wide and 640 long. Heights are in yards too.

Angles are 16 bits, 65536 to a full turn, 0 along +x and turning toward +y.

## Database

One Palm database per course: type `Crse`, creator `P386`, name `Par386 <Course>` (31 characters at most). Record 0 is the course header, record N is hole N. All numbers are big-endian and read a byte at a time.

Courses can append a terrain directory after the last hole, followed by the full-shot mesh records. Older readers can skip this directory but still need to support the hole record version.

Points in records are in quarter yards (u16), and must lie on the hole (0 to width × 4, 0 to length × 4).

### Full-shot terrain

Use `terrain-mesh N` in course.txt to bake detailed and compact meshes for hole N from its shapes and heightmap. An optional visual contour file supplies the full-shot outlines at sixteenth-yard precision. The compiler also bakes putting curves from the gameplay shapes around the pins. A dump saves the meshes and curves as sidecars for an exact rebuild.

Use `terrain-mesh N putting` to share the green, fringe and water curves with both mesh qualities. Each curve replaces its corresponding shape in course order. The compact mesh tries simplifying those outlines within one eighth of a yard to keep the draw fast.

Use `terrain-mesh N putting bunkers` to share the green, fringe and bunker curves near the pins and keep the authored water outlines. The mesh also rounds the fairway and first cut and starts the mowing pattern at the front of the fairway.

Use `terrain-mesh N putting dunes` for links holes with dunes along the left edge. The mesh samples the existing heights more closely and uses stronger shading around the dunes.

Mark the near-green visual polygons with `poly SURFACE putting` to share the course's putting contours with the detailed mesh. The compiler inserts the putting contours in gameplay order at the first marked polygon. It uses the marked polygons as written for the compact mesh and for builds without putting curves.

The `MSH2` directory stores two u16 record indices per hole, detailed then compact. Nonzero indices start at N + 2, where N is the number of holes, and run in directory order. A zero detailed index means the hole has no mesh. A zero compact index uses the detailed mesh. Courses with only detailed meshes use the older `MSH1` directory, which stores one index per hole.

Each mesh record is at most 60,000 bytes. Its u16 tile count precedes the tile directory. Each tile entry has four s16 bounds `x0, y0, x1, y1`, u16 vertex and triangle counts and a u32 byte offset from the start of the record. Coordinates are in sixteenths of a yard. The tile payload has vertices followed by triangles.

| Bytes | What |
|---|---|
| s16 × 3 per vertex | x and y in sixteenths of a yard, then elevation in 1/256 yard |
| u8 × 5 per triangle | three tile-local vertex indices, surface, light level (0 to 112) |

On Palm, 68k devices use the compact mesh and ARM devices use the detailed mesh. ARM's 16-bit display conversion blends the pixels on either side of terrain boundaries. The full-shot renderer uses the regular terrain path if the selected mesh is unavailable, invalid, or its working memory cannot be allocated. Putting uses the curves in the hole record when present.

### Course header (record 0)

| Bytes | What |
|---|---|
| u16 | version, 1 to 4 |
| u8 | holes, 1 to 36 |
| u8 | tee sets, 1 to 4 |
| name | course name, 1 to 31 characters |
| name × tee sets | tee set names, 1 to 15 characters each |
| per hole: u8, u16 | par, yards from the longest tee |
| u8 | version 2 and up: scenery and object art (0 parkland, 1 links, 2 Irish clifftop, 3 Japanese seaside, 4 Australian sandbelt, 5 South African bush, 6 Canadian Rockies, 7 English heathland, 8 Gulf desert, 9 Algarve hillside) |
| u8 × holes | version 2 and up: each hole's map turned against the course, 256 a turn, which moves the backdrop |
| u8 | version 3 and up: the average wind in mph, 0 to 40, 0 for the usual 10. Each hole's wind is 0 to twice that |
| u8 | version 4 only: green speed, 6 to 14, 0 for the usual 10. The green's roll slows by the usual amount times 10 over it, so 13 rolls about a third further |

A name is a length byte and that many Latin-1 characters, no nul.

### Hole (records 1 to N)

| Bytes | What |
|---|---|
| u16 | version, 1 to 6 |
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
| bytes | versions 2 and 3: compiled putting materials |
| bytes | version 3: compiled putting geometry |
| bytes | versions 4 and 5: visual contours and optional putting materials |
| bytes | version 5: baked sand tiles |
| bytes | version 6: putting curves |

The heightmap must cover the hole: (points − 1) × spacing is at least the width and length. Heights between points are bilinear and held level past the edge.

The test hole is 2816 bytes. A 32 × 64 heightmap is 2 KB of that, and it doesn't pack down much, so records aren't compressed. The loader rejects a record that is short, long or has any value out of range.

Players pick a tee set for a round. A hole is played from its tee of that set, or if it has none from the nearest longer set's tee, or else its first tee.

Surface types: 0 rough, 1 first cut, 2 fairway, 3 deep rough, 4 green, 5 fringe, 6 bunker, 7 water, 8 cart path, 9 out of bounds, 10 tee box.

Object types: 1 tree, 2 pine, 3 palm, 4 bush, 5 rock, 6 house, 7 stand (a grandstand), 8 wall (a dry-stone section 6 yd long at scale 64), 9 gorse, 10 grass (dune grass), 11 landmark (16 yd high: the course's kirk, lighthouse, pagoda, windmill, baobab or hotel), 12 animal (1.5 yd: sheep, kangaroo, crane, crocodile, hippo or elk). Other numbers load and are left for later.

Each scenery number has its own sprites for some types (tools/mkstyle.py lists them), in variant 0 and sometimes 1, such as the cherry and the maple for Sakura Bay trees; types it has none for use the shared art, and a variant it has no art for draws as variant 0. Scenery 1 to 9 each have their own backdrop.

### Putting curves

Version 6 adds a u8 count, then a u8 gameplay shape index, u16 point count and u16 x, y pairs for each contour. Coordinates are in sixteenth yards. Each contour closes from its final point to its first. Indices are in ascending order and refer to green, fringe, bunker or water shapes. A contour has 3 to 128 points and the hole has at most 1024 contour points. The record fits within 16 KB.

The putting renderer and lie grid use each curve in place of its indexed shape. The original shapes stay in the record. A dump writes the curve bytes to `hole-NN.curves` and lists them as `putting-curves N FILE` in course.txt.

### Compiled putting materials

Versions 2 and 3 include these rows. Versions 4 and 5 can include them after their visual contours. They supply colours to the putting renderer. Ball lies and physics use the two-yard surface grid.

| Bytes | What |
|---|---|
| u16 × 4 | x and y of the lower-left corner, then width and height, all in quarter yards |
| u16 × (height + 1) | byte offsets from the first run, starting at 0 and ending just past the last run |
| per run: u16 | exclusive end column in the high 12 bits, surface type in the low 4 bits |

The box reaches 26 yards around the pin positions and stops at the hole bounds. Surfaces are painted at quarter-yard cell centres using the same even-odd rule and shape order as the surface grid. Each row covers the whole box width.

### Compiled putting geometry

Version 3 adds an indexed triangle mesh after the material rows. The mesh follows the course outlines within the same box. A direct putting draw projects the mesh and draws the guide lines over it. Ahead draws and low-memory draws use the material rows. The experimental mesh uses flat lighting and omits triangles that cross the camera's near plane.

| Bytes | What |
|---|---|
| u16, u16 | number of vertices (1 to 512) and triangles (1 to 768) |
| per vertex: u16 × 3 | x and y in sixteenth yards from the hole origin, then the interpolated heightmap value in 8.8 fixed point |
| per triangle: u16 × 3, u8 | zero-based vertex indices, then surface type |

### Visual contours

Versions 4 and 5 append visual contours after the objects. Later polygons paint over earlier ones. The original shapes still supply the two-yard grid for play.

Full-shot views sample these contours at the terrain's depth steps. The original renderer is used if there is too little work memory. Putting views use the compiled materials when present.

| Bytes | What |
|---|---|
| u8 | number of polygons, 1 to 32 |
| per polygon: u8, u16 | surface type and vertex count, at least 3 vertices per polygon and at most 768 in total |
| per vertex: u16 × 2 | x and y in sixteenth yards from the hole origin |
| u16, bytes | size and contents of the compiled putting materials, or zero size when the rows do not fit |

### Baked sand tiles

Version 5 adds one tile for each visual bunker, in contour order. Each tile covers its contour and samples the slope at one-yard intervals. Baked sand uses outlined bunkers over another base surface. Use a single merged outline per visible bunker. Overlapping bunker outlines or later shapes painted over the sand can leave relief seams.

| Bytes | What |
|---|---|
| u8 | number of tiles, at least 1 |
| per tile: u16 × 4 | x and y of the first sample in yards, then columns and rows, each at least 2 |
| per sample: s8, u8 | signed distance in 1/32 yards, positive inside, from −64 to 64, then inward normal with x in the high nibble and y in the low nibble, each signed from −7 to 7 |

Samples run across x first, starting at the lowest y. The full-shot renderer interpolates the distance for the sand floor and uses the nearest normal for lighting.

## Surface grid

When a hole loads, its shapes are drawn into a grid of 2 yard cells, 4 bits each (two to a byte, even column in the high nibble), row 0 at y = 0. Shapes are filled even-odd at the cell centres, in record order, so later shapes win. The biggest hole makes a 128 × 320 cell grid, 20 KB, written a row at a time so the Palm can keep it in a storage record. Off the grid counts as out of bounds, except past a water cell on the edge, where the water carries on (the sea off a clifftop hole).

## Course source

`tools/coursec.py build COURSEDIR OUT.pdb` compiles a course folder, and `coursec.py dump PDB OUTDIR` writes one back out. `make courses` builds every folder in `courses/`.

For the putting renderer prototype, use `tools/coursec.py build --green-mesh COURSEDIR OUT.pdb`. Holes stay at version 1 if the extra rows would put the record over 16 KB. Dumping writes the original shapes and heightmaps. Build the dump with `--green-mesh` again to recreate the compiled rows.

Build with `--green-geometry` to add the triangle mesh. Holes that exceed the mesh limits or run out of record space use the material rows when they fit.

Use `tools/coursec.py build --visual-hole N VISUAL.txt COURSEDIR OUT.pdb` to add the visual contours to one hole. Only the `poly` lines in the visual file are read. Outlines over the vertex budget are simplified, keeping finer detail around sand and the green. Dump writes the contours to `hole-NN-visual.txt`, which can be passed to `--visual-hole` to rebuild the record.

Use `--baked-sand N VISUAL.txt` in place of `--visual-hole N VISUAL.txt` to include the sand tiles. Rebuild a version 5 dump with `--baked-sand` to recreate its tiles.

`course.txt`:

```
name Test
tee Blue          # tee sets, longest first
tee White
holes 18          # reads hole-01.txt to hole-18.txt
scenery 1         # optional, makes a version 2 header
wind 15           # optional, average mph, makes a version 3 header
green 13          # optional, green speed, makes a version 4 header
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
poly fairway 56,51 67,49 76,52 ...
rect tee 52 6 68 22
ellipse bunker 38 236 7 10 [points] [degrees]
object pine 16 60 [variant] [scale]
```

Surface names: rough, first-cut, fairway, deep-rough, green, fringe, bunker, water, path, oob, tee. Object names: tree, pine, palm, bush, rock, house, stand, wall, gorse, grass, landmark, animal, or a number.

Auld Kirk (`courses/auldkirk/`) is made by `tools/mkauldkirk.py` (`make auldkirk`), which writes the hole files and heightmaps from a table of centre lines, greens and features. Carrig Head, Sakura Bay, Banksia Heath, Crocodile Bend and Cascade Springs are made the same way by `tools/mkcarrig.py`, `tools/mksakura.py`, `tools/mkbanksia.py`, `tools/mkcroc.py` and `tools/mkcascade.py`.

Run `python3 tools/mkexpansion.py` for Bracken Mere, Saffron Dunes and Falesia Grove. Pass a folder name such as `saffrondunes` to regenerate just that course.

The heightmap's top row is the far end of the hole, so the PNG looks like a course map.
