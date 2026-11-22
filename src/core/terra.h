/* The renderer's hot loops (render.c): a depth step of the terrain march,
 * a batch of green triangles or putting spans, a row of sky and the hole
 * map's rows. The same C is
 * built for the 68k (core segment), the host and as ARM code for OS 5
 * (src/arm/terraarm.c), so it has no tables or globals of its own: the
 * palette tables and every buffer come in the job.
 *
 * On the 68k each job is also the ARM code's argument, read there big
 * endian at the TR_*, TT_*, TS_*, TM_* and TC_* byte offsets, so keep the layouts:
 * 4-byte fields, then 2-byte, then bytes. Fields after the last offset
 * are the host's only. */
#ifndef TERRA_H
#define TERRA_H

#include <stdint.h>
#include "section.h"
#include "fixed.h"

/* What the ARM entry is asked to run, the first 2 bytes of its argument
 * block, a pointer to the job in the next 4. */
enum { TERRA_ROW, TERRA_TRIS, TERRA_SKY, TERRA_MAP, TERRA_CURVE, TERRA_MESH_PROJECT };

/* One depth step of the terrain march, every column (render.c terrain). */
typedef struct TerraRow {
	uint8_t *bits;              /* 0: the surface's pixels */
	const uint8_t *heights;     /* 4: the heightmap */
	const uint8_t *cells;       /* 8: the surface grid, 2 cells a byte */
	int16_t *ybuf;              /* 12: the y-buffer, w columns */
	uint8_t *prev;              /* 16: surface of the last sample in each column */
	uint8_t *wet;               /* 20: columns that have drawn far water */
	const uint8_t *bayer;       /* 24: 4x4 ordered dither values, row by row */
	const int8_t *noise;        /* 28: pal_noise */
	const uint8_t *pat2;        /* 32: pal_pat2 */
	const uint8_t *grey;        /* 36: pal_grey */
	fixed camx, camy;           /* 40, 44: the camera, yards */
	int32_t camb;               /* 48: camera height in heightmap byte steps, 8.8 */
	fixed mx, my;               /* 52, 56: heightmap's last cell, 16.16 cells */
	fixed px, py;               /* 60, 64: the ground under column 0's centre */
	fixed dpxc, dpyc;           /* 68, 72: px, py a sample column on */
	fixed spx, spy;             /* 76, 80: the first height sample */
	fixed dpxs, dpys;           /* 84, 88: from one height sample to the next */
	uint16_t rb;                /* 92: bytes a row */
	int16_t w, yh;              /* 94, 96: columns, horizon row */
	uint16_t hw, hh;            /* 98, 100: heightmap size */
	uint16_t gw, gh, gs;        /* 102 to 106: surface grid size and bytes a row */
	int16_t ak, bk;             /* 108, 110: slope light per gradient step across and along */
	int16_t hl, wmin, tq;       /* 112 to 116: haze lift, far water rows, probe share (2.14) */
	int16_t m;                  /* 118: heights project as m >> sh at this depth */
	int16_t cw;                 /* 120: columns a surface lookup covers, 1 or 2 */
	int16_t ns;                 /* 122: near noise shift, 8 for none */
	int16_t sand_px, water_px;  /* 124, 126: rows bunkers and water sit down */
	int16_t hb;                 /* 128: haze, 0 to 16 */
	uint16_t step;              /* 130: depth steps so far */
	int16_t stripe;             /* 132: mowing band light */
	int16_t wlv;                /* 134: water's light at this depth, before the haze lift */
	int16_t ymax;               /* 136: out: the lowest y-buffer row seen */
	uint8_t lg;                 /* 138: log2 of the bits a pixel, 0 to 3 */
	uint8_t scale;              /* 139 */
	uint8_t shift;              /* 140: heightmap cell size, yards as a shift */
	uint8_t sh;                 /* 141 */
	uint8_t kss;                /* 142: height samples every 1 << kss columns */
	uint8_t hm;                 /* 143: at 2 bits, the pattern rows that are haze, a bit each */
	const uint8_t *material;    /* 144: projected materials, SURF_COUNT uses the grid, or null */
	const uint8_t *cut;         /* 148: fraction from prior depth to cut, in 1/256, or 0 */
	const uint8_t *prior_material; /* 152: projected materials at the prior depth */
	uint8_t *prior_y;           /* 156: prior projected heights, big endian signed 1/16 pixels */
	int32_t prior_sand_px;      /* 160: bunker setback at the prior depth, pixels */
	const uint8_t *sand_relief; /* 164: sand depth per column, in 1/256 of the setback */
	const int8_t *sand_light;   /* 168: sand slope light per column, in ramp units */
	const uint8_t *prior_relief; /* 172: sand depth fractions at the prior strip */
	uint32_t spans, pixels;     /* host only: work counts added to */
} TerraRow;

#define TR_BITS 0
#define TR_HEIGHTS 4
#define TR_CELLS 8
#define TR_YBUF 12
#define TR_PREV 16
#define TR_WET 20
#define TR_BAYER 24
#define TR_NOISE 28
#define TR_PAT2 32
#define TR_GREY 36
#define TR_CAMX 40
#define TR_CAMY 44
#define TR_CAMB 48
#define TR_MX 52
#define TR_MY 56
#define TR_PX 60
#define TR_PY 64
#define TR_DPXC 68
#define TR_DPYC 72
#define TR_SPX 76
#define TR_SPY 80
#define TR_DPXS 84
#define TR_DPYS 88
#define TR_RB 92
#define TR_W 94
#define TR_YH 96
#define TR_HW 98
#define TR_HH 100
#define TR_GW 102
#define TR_GH 104
#define TR_GS 106
#define TR_AK 108
#define TR_BK 110
#define TR_HL 112
#define TR_WMIN 114
#define TR_TQ 116
#define TR_M 118
#define TR_CW 120
#define TR_NS 122
#define TR_SAND 124
#define TR_WATER 126
#define TR_HB 128
#define TR_STEP 130
#define TR_STRIPE 132
#define TR_WLV 134
#define TR_YMAX 136
#define TR_LG 138
#define TR_SCALE 139
#define TR_SHIFT 140
#define TR_SH 141
#define TR_KSS 142
#define TR_HM 143
#define TR_MATERIAL 144
#define TR_CUT 148
#define TR_PRIOR_MATERIAL 152
#define TR_PRIOR_Y 156
#define TR_PRIOR_SAND 160
#define TR_SAND_RELIEF 164
#define TR_SAND_LIGHT 168
#define TR_PRIOR_RELIEF 172
#define TR_SIZE 176

/* A triangle of the green view: corners, ramp base and slope light. */
typedef struct {
	int16_t x[3], y[3];
	int16_t base, lv;
} TerraTri;

typedef struct {
	int16_t u[3], v[3];          /* world coordinates, sixteenths of a yard */
	int16_t light[3];           /* slope light added to the triangle ramp level */
	int16_t depth[3];           /* camera depth in yards for the fairway */
} TerraTurf;

#define TERRA_TRIS_MAX 8

/* Fills n triangles of tri, dithered (render.c's green view). */
typedef struct TerraTris {
	uint8_t *bits;              /* 0 */
	const TerraTri *tri;        /* 4: each 16 bytes, 2-byte fields in order */
	const uint8_t *bayer;       /* 8 */
	const uint8_t *pat2;        /* 12 */
	const uint8_t *grey;        /* 16 */
	uint16_t rb;                /* 20 */
	int16_t w, vh;              /* 22, 24: columns and rows of the view */
	int16_t n;                  /* 26: up to TERRA_TRIS_MAX */
	uint8_t lg;                 /* 28 */
	uint8_t pad;
	const TerraTurf *turf;      /* 30: optional parallel vertex data for 8-bit turf */
	const int8_t *noise;        /* 34: signed 16 x 16 grain tile */
	int16_t mow_phase;          /* 38: fairway band origin in yards */
} TerraTris;

#define TT_BITS 0
#define TT_TRI 4
#define TT_BAYER 8
#define TT_PAT2 12
#define TT_GREY 16
#define TT_RB 20
#define TT_W 22
#define TT_VH 24
#define TT_N 26
#define TT_LG 28
#define TT_TURF 30
#define TT_NOISE 34
#define TT_MOW_PHASE 38
#define TT_SIZE 40
#define TT_TURF_SIZE 24

/* The baked fairway shade encodes the 12-yard band's polarity. */
#define MOW_MID 67                 /* matches tools/terrainmesh.py */
#define MOW_CONTRAST 13

/* Rows y0 to y1 of sky over the columns above the y-buffer, with the
 * backdrop strip unpacked a row at a time (render.c sky). */
typedef struct TerraSky {
	uint8_t *bits;              /* 0 */
	const uint8_t *bk;          /* 4: the backdrop, PackBits rows */
	uint8_t *row;               /* 8: room for a backdrop row */
	const int16_t *ybuf;        /* 12 */
	const uint8_t *bayer;       /* 16 */
	const uint8_t *pat2;        /* 20 */
	const uint8_t *grey;        /* 24 */
	uint32_t len;               /* 28: the backdrop's bytes */
	uint32_t pos;               /* 32: in and out: its next row */
	uint16_t rb;                /* 36 */
	int16_t w, y0, y1, yh;      /* 38 to 44 */
	int16_t off;                /* 46: the backdrop column at x 0 */
	int16_t bw;                 /* 48: in and out: its width, 0 for none or once it's found cut short */
	int16_t bh, btop;           /* 50, 52: its rows and top row */
	uint8_t lg;                 /* 54 */
	uint8_t pad;
	uint32_t px;                /* host only: sky pixels added to */
} TerraSky;

#define TS_BITS 0
#define TS_BK 4
#define TS_ROW 8
#define TS_YBUF 12
#define TS_BAYER 16
#define TS_PAT2 20
#define TS_GREY 24
#define TS_LEN 28
#define TS_POS 32
#define TS_RB 36
#define TS_W 38
#define TS_Y0 40
#define TS_Y1 42
#define TS_YH 44
#define TS_OFF 46
#define TS_BW 48
#define TS_BH 50
#define TS_BTOP 52
#define TS_LG 54
#define TS_SIZE 56

/* Rows y0 to y1 of the hole map (render.c map_draw): each pixel the
 * colour of the surface under it, black off the hole. */
typedef struct TerraMap {
	uint8_t *bits;              /* 0 */
	const uint8_t *cells;       /* 4: the surface grid */
	const uint8_t *xcell;       /* 8: the grid column under each screen column, 0xFF off the hole */
	const uint8_t *col;         /* 12: each surface's colour */
	const uint8_t *bayer;       /* 16 */
	const uint8_t *pat2;        /* 20 */
	const uint8_t *grey;        /* 24 */
	fixed len;                  /* 28: the hole's length, yards */
	fixed ypp;                  /* 32: yards a pixel */
	uint16_t rb;                /* 36 */
	int16_t w, y0, y1;          /* 38, 40, 42 */
	int16_t top, mh;            /* 44, 46: the hole's rows */
	uint16_t gs, gh;            /* 48, 50: grid bytes a row and rows */
	uint8_t lg;                 /* 52 */
	uint8_t pad;
} TerraMap;

#define TM_BITS 0
#define TM_CELLS 4
#define TM_XCELL 8
#define TM_COL 12
#define TM_BAYER 16
#define TM_PAT2 20
#define TM_GREY 24
#define TM_LEN 28
#define TM_YPP 32
#define TM_RB 36
#define TM_W 38
#define TM_Y0 40
#define TM_Y1 42
#define TM_TOP 44
#define TM_MH 46
#define TM_GS 48
#define TM_GH 50
#define TM_LG 52
#define TM_SIZE 54

/* A batch of 8-bit putting spans. Each command has big-endian y, x0, x1,
 * then one surface byte. Light samples are 16 pixels apart. */
#define CURVE_LIGHT_SHIFT 4
#define CURVE_LIGHT_STRIDE 21
#define CURVE_SPAN_BYTES 7
typedef struct TerraCurveShade {
	uint8_t *bits;
	const int8_t *light;
	const uint8_t *bayer, *spans;
	uint16_t rb, count;
	const int8_t *noise;
} TerraCurveShade;

#define TC_BITS 0
#define TC_LIGHT 4
#define TC_BAYER 8
#define TC_SPANS 12
#define TC_RB 16
#define TC_COUNT 18
#define TC_NOISE 20
#define TC_SIZE 24

/* Full-shot mesh points, in the camera's Q8 coordinates and screen pixels. */
#define MESH_TILE_NEAR 256L
#define MESH_TILE_COORD 8191
#define MESH_SAND_UNLIT 127

enum { MESH_OUT_LEFT = 1, MESH_OUT_RIGHT = 2, MESH_OUT_TOP = 4,
       MESH_OUT_BOTTOM = 8, MESH_OUT_NEAR = 16 };

typedef struct {
	int32_t side, down, depth;
	int16_t x, y, u, v;
	int8_t light, sand;
} MeshPoint;

typedef struct {
	const uint8_t *vertices;    /* 0: big-endian x, y, height triplets */
	MeshPoint *points;          /* 4 */
	uint8_t *outside;           /* 8 */
	const int8_t *light;         /* 12: optional slope grid */
	int32_t origin_side, origin_depth, height; /* 16, 20, 24 */
	int32_t low, high;           /* 28, 32: out, point depth range */
	int16_t fx, fy, rx, ry;     /* 36, 38, 40, 42 */
	int16_t cx, yh, f, w, vh;   /* 44, 46, 48, 50, 52 */
	uint16_t count;             /* 54 */
	uint8_t light_w, light_h, light_shift; /* 56, 57, 58 */
	uint8_t projected;          /* 59: out, number on or beyond the near plane */
} TerraMeshProject;

#define TMP_VERTICES 0
#define TMP_POINTS 4
#define TMP_OUTSIDE 8
#define TMP_LIGHT 12
#define TMP_ORIGIN_SIDE 16
#define TMP_ORIGIN_DEPTH 20
#define TMP_HEIGHT 24
#define TMP_LOW 28
#define TMP_HIGH 32
#define TMP_FX 36
#define TMP_FY 38
#define TMP_RX 40
#define TMP_RY 42
#define TMP_CX 44
#define TMP_YH 46
#define TMP_F 48
#define TMP_W 50
#define TMP_VH 52
#define TMP_COUNT 54
#define TMP_LIGHT_W 56
#define TMP_LIGHT_H 57
#define TMP_LIGHT_SHIFT 58
#define TMP_PROJECTED 59

#define MP_SIDE 0
#define MP_DOWN 4
#define MP_DEPTH 8
#define MP_X 12
#define MP_Y 14
#define MP_U 16
#define MP_V 18
#define MP_LIGHT 20
#define MP_SAND 21
#define MP_SIZE 22

static __inline__ int8_t mesh_light_sample(const int8_t *light, uint16_t w, uint16_t h,
                                           uint8_t shift, int16_t x, int16_t y)
{
	int32_t gx = ((int32_t)x * 16) >> shift, gy = ((int32_t)y * 16) >> shift;
	int32_t xmax = (int32_t)(w - 1) * 256 - 1, ymax = (int32_t)(h - 1) * 256 - 1;
	uint16_t fx, fy;
	int32_t a, b;
	const int8_t *near, *far;

	gx = gx < 0 ? 0 : gx > xmax ? xmax : gx;
	gy = gy < 0 ? 0 : gy > ymax ? ymax : gy;
	fx = (uint16_t)(gx & 255); fy = (uint16_t)(gy & 255);
	near = light + MULU16(gy >> 8, w) + (gx >> 8);
	far = near + w;
	a = (int32_t)near[0] * 256 + MULS16(near[1] - near[0], fx);
	b = (int32_t)far[0] * 256 + MULS16(far[1] - far[0], fx);
	return (int8_t)((a + (MULS16(b - a, fy) >> 8) + 128) >> 8);
}

void SEC_CORE terra_row(TerraRow *t);
void SEC_CARD terra_tris(const TerraTris *t);
void SEC_CORE terra_sky(TerraSky *t);
void SEC_CORE terra_map(const TerraMap *t);
void SEC_CORE terra_curve_shade(const TerraCurveShade *t);

/* a / b rounded toward zero, by shifts: the ARM has no divide and the blob
 * can't call a library. b is not 0. */
static __inline__ int32_t terra_div(int32_t a, int32_t b)
{
	uint32_t ua = a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
	uint32_t ub = b < 0 ? 0u - (uint32_t)b : (uint32_t)b;
	uint32_t bit = 1, q = 0;

	if (ua < ub) return 0;
	/* Start at the highest quotient bit that can fit. */
	while (ub <= (ua >> 8)) { ub <<= 8; bit <<= 8; }
	while (ub <= (ua >> 1)) { ub <<= 1; bit <<= 1; }
	do {
		if (ua >= ub) {
			ua -= ub;
			q |= bit;
		}
		ub >>= 1;
		bit >>= 1;
	} while (bit);
	return (int32_t)((a < 0) != (b < 0) ? 0u - q : q);
}

#endif
