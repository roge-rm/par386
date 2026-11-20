/* The renderer's hot loops (render.c): a depth step of the terrain march,
 * a batch of the green view's triangles, a row of sky and the hole map's
 * rows. The same C is
 * built for the 68k (core segment), the host and as ARM code for OS 5
 * (src/arm/terraarm.c), so it has no tables or globals of its own: the
 * palette tables and every buffer come in the job.
 *
 * On the 68k each job is also the ARM code's argument, read there big
 * endian at the TR_*, TT_*, TS_* and TM_* byte offsets, so keep the layouts:
 * 4-byte fields, then 2-byte, then bytes. Fields after the last offset
 * are the host's only. */
#ifndef TERRA_H
#define TERRA_H

#include <stdint.h>
#include "section.h"
#include "fixed.h"

/* What the ARM entry is asked to run, the first 2 bytes of its argument
 * block, a pointer to the job in the next 4. */
enum { TERRA_ROW, TERRA_TRIS, TERRA_SKY, TERRA_MAP };

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
#define TR_SIZE 144

/* A triangle of the green view: corners, ramp base and slope light. */
typedef struct {
	int16_t x[3], y[3];
	int16_t base, lv;
} TerraTri;

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
#define TT_SIZE 30

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

void SEC_CORE terra_row(TerraRow *t);
void SEC_CORE terra_tris(const TerraTris *t);
void SEC_CORE terra_sky(TerraSky *t);
void SEC_CORE terra_map(const TerraMap *t);

/* a / b rounded toward zero, by shifts: the ARM has no divide and the blob
 * can't call a library. b is not 0. */
static __inline__ int32_t terra_div(int32_t a, int32_t b)
{
	uint32_t ua = a < 0 ? 0u - (uint32_t)a : (uint32_t)a, ub = b < 0 ? 0u - (uint32_t)b : (uint32_t)b, q = 0, r = 0;
	int16_t i;
	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((ua >> i) & 1);
		if (r >= ub) {
			r -= ub;
			q |= 1UL << i;
		}
	}
	return (a < 0) != (b < 0) ? -(int32_t)q : (int32_t)q;
}

#endif
