/* The ARM entries (src/arm/) built for the host. Each gfx_to16 and
 * gfx_to16_mask job is written out as the 68k passes it (big endian,
 * 32-bit pointers, the palette low byte first) and run through
 * to16arm_entry, and the output must match the plain path byte for byte,
 * and an old style loop written here. The renderer's terrain steps, green
 * triangles, sky rows and map rows go through terraarm_entry the same way, with the
 * y-buffer and triangle list big endian, and the scenes, green views and
 * maps must match the plain path's. Memory the jobs point at is mapped under
 * 4 GB so their pointers fit.
 *
 * Usage: armtest COURSE.pdb ARTDIR */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "gfx.h"
#include "to16.h"
#include "to16arm.h"
#include "terra.h"
#include "terraarm.h"
#include "render.h"
#include "trig.h"
#include "pal.h"

static int checks, fails;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define W 163
#define H 37
#define DRB (W * 2 + 6)
#define LOW_SIZE (32 << 20)

static uint8_t *low, *lowp;     /* memory under 4 GB */
static uint8_t *palle, *dith;   /* the ARM side's palette and dither table, in low */
static uint8_t blk[TO16_SIZE];
static int jobs;

static void *lowalloc(size_t n)
{
	void *p = lowp;
	lowp += (n + 15) & ~(size_t)15;
	if (lowp > low + LOW_SIZE) {
		printf("FAIL low memory\n");
		exit(1);
	}
	return p;
}

static void put32(uint8_t *p, const void *v)
{
	uintptr_t u = (uintptr_t)v;
	if (u >> 32) {
		printf("FAIL pointer over 4 GB %p\n", v);
		exit(1);
	}
	p[0] = (uint8_t)(u >> 24);
	p[1] = (uint8_t)(u >> 16);
	p[2] = (uint8_t)(u >> 8);
	p[3] = (uint8_t)u;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

/* What arm.c does on the 68k: the job as it lies in 68k memory, pal swapped
 * for the low byte first copy, then the native call. */
static void via_entry(const struct To16Job *a)
{
	CHECK(memcmp(a->dith, dith, 16) == 0);
	put32(blk + TO16_BITS, a->bits);
	put32(blk + TO16_D, a->d);
	put32(blk + TO16_PAL, palle);
	put32(blk + TO16_DITH, dith);
	put16(blk + TO16_SRB, a->srb);
	put16(blk + TO16_DRB, a->drb);
	put16(blk + TO16_X0, (uint16_t)a->x0);
	put16(blk + TO16_Y0, (uint16_t)a->y0);
	put16(blk + TO16_X1, (uint16_t)a->x1);
	put16(blk + TO16_Y1, (uint16_t)a->y1);
	blk[TO16_LE] = a->le;
	blk[TO16_MASK] = a->mask;
	blk[TO16_LO] = a->lo;
	blk[TO16_HI] = a->hi;
	put16(blk + TO16_WIDTH, (uint16_t)a->width);
	put16(blk + TO16_SMOOTH_Y1, (uint16_t)a->smooth_y1);
	to16arm_entry(NULL, blk, NULL);
	jobs++;
}

/* The conversion as a plain loop over the rectangle. */
static void old_style(const Surface *s, uint8_t *d, int x0, int y0, int x1, int y1, int le, int lo, int hi)
{
	static const uint8_t dv[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	const uint16_t *pal = gfx_pal565();
	int x, y;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++) {
			int t = dv[((y & 3) << 2) | (x & 3)];
			uint16_t c = pal[s->bits[y * s->rowBytes + x]];
			if (t < lo || t >= hi) continue;
			d[y * DRB + 2 * x] = (uint8_t)(le ? c : c >> 8);
			d[y * DRB + 2 * x + 1] = (uint8_t)(le ? c >> 8 : c);
		}
}

static const int16_t rects[][4] = {
	{ 0, 0, W, H }, { 3, 5, 17, 9 }, { 1, 1, 1, 1 }, { W - 3, H - 2, 5, 5 }, { -4, -3, 9, 8 },
	{ 7, 0, 2, H }, { 0, 11, W, 1 }, { 50, 20, 0, 4 }, { 2, 2, W - 5, H - 3 },
};
static const uint8_t steps[][2] = { { 0, 16 }, { 0, 2 }, { 2, 16 }, { 5, 10 }, { 14, 16 }, { 3, 4 }, { 7, 7 } };

static uint16_t rgb_at(const uint8_t *d, int x, int y, int le)
{
	const uint8_t *p = d + y * DRB + x * 2;
	return (uint16_t)(le ? (p[1] << 8 | p[0]) : (p[0] << 8 | p[1]));
}

static uint16_t blended(uint16_t a, uint16_t b)
{
	unsigned r = (5 * (a >> 11) + 3 * (b >> 11) + 4) / 8;
	unsigned g = (5 * ((a >> 5) & 63) + 3 * ((b >> 5) & 63) + 4) / 8;
	unsigned bl = (5 * (a & 31) + 3 * (b & 31) + 4) / 8;
	return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void terrain_edges(Surface *s, uint8_t *plain, uint8_t *full, uint8_t *steps_out)
{
	const uint16_t *pal = gfx_pal565();
	const uint8_t grass = PAL_FAIRWAY + 4, sand = PAL_SAND + 4;
	uint16_t a = pal[grass], b = pal[sand], water = pal[PAL_WATER + 4];
	uint16_t expected = blended(a, b);
	int le, y;
	uint8_t step;

	memset(s->bits, grass, (size_t)s->rowBytes * H);
	for (y = 0; y < H; y++) memset(s->bits + y * s->rowBytes + 80, PAL_WATER + 4, W - 80);
	for (y = 25; y <= 27; y++) memset(s->bits + y * s->rowBytes + 40, sand, 5);
	s->bits[9 * s->rowBytes + 10] = sand - 1;
	s->bits[10 * s->rowBytes + 9] = sand;
	s->bits[4 * s->rowBytes + 5] = sand;
	s->bits[5 * s->rowBytes + 4] = sand;
	s->bits[5 * s->rowBytes + 5] = PAL_SKIN + 2;
	s->bits[19 * s->rowBytes + 20] = grass - 1;
	s->bits[20 * s->rowBytes + 19] = grass - 1;
	s->bits[21] = sand;
	s->bits[s->rowBytes + 20] = sand;
	s->bits[(H - 3) * s->rowBytes + 25] = sand;
	s->bits[(H - 2) * s->rowBytes + 24] = sand;
	s->smooth_y1 = H - 2;
	for (le = 0; le < 2; le++) {
		memset(plain, 0, (size_t)DRB * H);
		memset(full, 0, (size_t)DRB * H);
		memset(steps_out, 0, (size_t)DRB * H);
		gfx_to16_use(NULL);
		gfx_to16(s, plain, DRB, 0, 0, W, H, le);
		gfx_to16_use(via_entry);
		gfx_to16(s, full, DRB, 0, 0, W, H, le);
		for (step = 0; step < 16; step += 2)
			gfx_to16_mask(s, steps_out, DRB, 0, 0, W, H, le, step, step + 2);
		CHECK(memcmp(full, steps_out, (size_t)DRB * H) == 0);
		CHECK(rgb_at(plain, 10, 10, le) == a && rgb_at(full, 10, 10, le) == expected && a != expected);
		CHECK(rgb_at(full, 5, 5, le) == pal[PAL_SKIN + 2]);
		CHECK(rgb_at(full, 79, 10, le) == blended(a, water));
		CHECK(rgb_at(full, 80, 10, le) == blended(water, a));
		CHECK(rgb_at(full, 42, 24, le) == expected);
		CHECK(rgb_at(full, 42, 25, le) == blended(b, a));
		CHECK(rgb_at(full, 42, 26, le) == b);
		CHECK(rgb_at(full, 20, 20, le) == a);
		CHECK(rgb_at(full, 20, 0, le) == a);
		CHECK(rgb_at(full, 25, H - 2, le) == a);
		gfx_to16(s, plain, DRB, 10, 10, 1, 1, le);
		CHECK(rgb_at(plain, 10, 10, le) == expected);
	}
	s->smooth_y1 = 0;
	gfx_to16_use(NULL);
}

/* The renderer's loops through terraarm_entry. */

static int tjobs[4];

/* p, or a copy of n bytes of it under 4 GB (the renderer's const tables
 * are in the program's image), made once. */
static const void *lowtab(const void *p, size_t n)
{
	static const void *src[16];
	static void *dst[16];
	static int ns;
	int i;
	if (!((uintptr_t)p >> 32)) return p;
	for (i = 0; i < ns; i++)
		if (src[i] == p) return dst[i];
	if (ns == 16) {
		printf("FAIL too many tables\n");
		exit(1);
	}
	src[ns] = p;
	dst[ns] = lowalloc(n);
	memcpy(dst[ns], p, n);
	return dst[ns++];
}

/* n 16-bit values to big endian and back (the same swap on the host). */
static void swap_be(int16_t *v, int n)
{
	uint8_t *p = (uint8_t *)v, c;
	int i;
	for (i = 0; i < n; i++, p += 2) {
		c = p[0];
		p[0] = p[1];
		p[1] = c;
	}
}

static uint8_t *tblk, *tlist, *tturf, *tuser;   /* in low memory */

static void call_entry(uint8_t op, const uint8_t *b)
{
	uint8_t *u = tuser;
	u[0] = 0;
	u[1] = op;
	put32(u + 2, b);
	terraarm_entry(NULL, u, NULL);
	tjobs[op]++;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* What arm.c does on the 68k: the job as it lies in 68k memory, behind its op. */
static void via_terra(uint8_t op, void *job)
{
	uint8_t *b = tblk;
	int i, k;

	if (op == TERRA_ROW) {
		TerraRow *t = job;
		put32(b + TR_BITS, t->bits);
		put32(b + TR_HEIGHTS, t->heights);
		put32(b + TR_CELLS, t->cells);
		put32(b + TR_YBUF, t->ybuf);
		put32(b + TR_PREV, t->prev);
		put32(b + TR_WET, t->wet);
		put32(b + TR_BAYER, lowtab(t->bayer, 16));
		put32(b + TR_NOISE, lowtab(t->noise, 256));
		put32(b + TR_PAT2, lowtab(t->pat2, 1024));
		put32(b + TR_GREY, lowtab(t->grey, 256));
#define V32(o, v) put32(b + (o), (const void *)(uintptr_t)(uint32_t)(v))
		V32(TR_CAMX, t->camx);
		V32(TR_CAMY, t->camy);
		V32(TR_CAMB, t->camb);
		V32(TR_MX, t->mx);
		V32(TR_MY, t->my);
		V32(TR_PX, t->px);
		V32(TR_PY, t->py);
		V32(TR_DPXC, t->dpxc);
		V32(TR_DPYC, t->dpyc);
		V32(TR_SPX, t->spx);
		V32(TR_SPY, t->spy);
		V32(TR_DPXS, t->dpxs);
		V32(TR_DPYS, t->dpys);
		put16(b + TR_RB, t->rb);
		put16(b + TR_W, (uint16_t)t->w);
		put16(b + TR_YH, (uint16_t)t->yh);
		put16(b + TR_HW, t->hw);
		put16(b + TR_HH, t->hh);
		put16(b + TR_GW, t->gw);
		put16(b + TR_GH, t->gh);
		put16(b + TR_GS, t->gs);
		put16(b + TR_AK, (uint16_t)t->ak);
		put16(b + TR_BK, (uint16_t)t->bk);
		put16(b + TR_HL, (uint16_t)t->hl);
		put16(b + TR_WMIN, (uint16_t)t->wmin);
		put16(b + TR_TQ, (uint16_t)t->tq);
		put16(b + TR_M, (uint16_t)t->m);
		put16(b + TR_CW, (uint16_t)t->cw);
		put16(b + TR_NS, (uint16_t)t->ns);
		put16(b + TR_SAND, (uint16_t)t->sand_px);
		put16(b + TR_WATER, (uint16_t)t->water_px);
		put16(b + TR_HB, (uint16_t)t->hb);
		put16(b + TR_STEP, t->step);
		put16(b + TR_STRIPE, (uint16_t)t->stripe);
		put16(b + TR_WLV, (uint16_t)t->wlv);
		put16(b + TR_YMAX, 0x5A5A);
		b[TR_LG] = t->lg;
		b[TR_SCALE] = t->scale;
		b[TR_SHIFT] = t->shift;
		b[TR_SH] = t->sh;
		b[TR_KSS] = t->kss;
		b[TR_HM] = t->hm;
		put32(b + TR_MATERIAL, t->material);
		put32(b + TR_CUT, t->cut);
		put32(b + TR_PRIOR_MATERIAL, t->prior_material);
		put32(b + TR_PRIOR_Y, t->prior_y);
		V32(TR_PRIOR_SAND, t->prior_sand_px);
		put32(b + TR_SAND_RELIEF, t->sand_relief);
		put32(b + TR_SAND_LIGHT, t->sand_light);
		put32(b + TR_PRIOR_RELIEF, t->prior_relief);
		swap_be(t->ybuf, t->w);
		call_entry(op, b);
		swap_be(t->ybuf, t->w);
		t->ymax = (int16_t)((b[TR_YMAX] << 8) | b[TR_YMAX + 1]);
	} else if (op == TERRA_TRIS) {
		const TerraTris *t = job;
		CHECK(t->n > 0 && t->n <= TERRA_TRIS_MAX);
		for (i = 0; i < t->n; i++) {
			uint8_t *q = tlist + 16 * i;
			for (k = 0; k < 3; k++) {
				put16(q + 2 * k, (uint16_t)t->tri[i].x[k]);
				put16(q + 6 + 2 * k, (uint16_t)t->tri[i].y[k]);
			}
			put16(q + 12, (uint16_t)t->tri[i].base);
			put16(q + 14, (uint16_t)t->tri[i].lv);
			if (t->turf) {
				q = tturf + TT_TURF_SIZE * i;
				for (k = 0; k < 3; k++) {
					put16(q + 2 * k, (uint16_t)t->turf[i].u[k]);
					put16(q + 6 + 2 * k, (uint16_t)t->turf[i].v[k]);
					put16(q + 12 + 2 * k, (uint16_t)t->turf[i].light[k]);
					put16(q + 18 + 2 * k, (uint16_t)t->turf[i].depth[k]);
				}
			}
		}
		put32(b + TT_BITS, t->bits);
		put32(b + TT_TRI, tlist);
		put32(b + TT_BAYER, lowtab(t->bayer, 16));
		put32(b + TT_PAT2, lowtab(t->pat2, 1024));
		put32(b + TT_GREY, lowtab(t->grey, 256));
		put16(b + TT_RB, t->rb);
		put16(b + TT_W, (uint16_t)t->w);
		put16(b + TT_VH, (uint16_t)t->vh);
		put16(b + TT_N, (uint16_t)t->n);
		b[TT_LG] = t->lg;
		put32(b + TT_TURF, t->turf ? tturf : NULL);
		put32(b + TT_NOISE, lowtab(t->noise, 256));
		put16(b + TT_MOW_PHASE, (uint16_t)t->mow_phase);
		call_entry(op, b);
	} else if (op == TERRA_SKY) {
		TerraSky *t = job;
		put32(b + TS_BITS, t->bits);
		put32(b + TS_BK, t->bk);
		put32(b + TS_ROW, t->row);
		put32(b + TS_YBUF, t->ybuf);
		put32(b + TS_BAYER, lowtab(t->bayer, 16));
		put32(b + TS_PAT2, lowtab(t->pat2, 1024));
		put32(b + TS_GREY, lowtab(t->grey, 256));
		V32(TS_LEN, t->len);
		V32(TS_POS, t->pos);
		put16(b + TS_RB, t->rb);
		put16(b + TS_W, (uint16_t)t->w);
		put16(b + TS_Y0, (uint16_t)t->y0);
		put16(b + TS_Y1, (uint16_t)t->y1);
		put16(b + TS_YH, (uint16_t)t->yh);
		put16(b + TS_OFF, (uint16_t)t->off);
		put16(b + TS_BW, (uint16_t)t->bw);
		put16(b + TS_BH, (uint16_t)t->bh);
		put16(b + TS_BTOP, (uint16_t)t->btop);
		b[TS_LG] = t->lg;
		swap_be((int16_t *)t->ybuf, t->w);
		call_entry(op, b);
		swap_be((int16_t *)t->ybuf, t->w);
		t->pos = rd32(b + TS_POS);
		t->bw = (int16_t)((b[TS_BW] << 8) | b[TS_BW + 1]);
	} else if (op == TERRA_MAP) {
		const TerraMap *t = job;
		/* xcell and col are on the caller's stack */
		uint8_t *xc = lowalloc(480), *col = lowalloc(16);
		memcpy(xc, t->xcell, (size_t)t->w);
		memcpy(col, t->col, SURF_COUNT);
		put32(b + TM_BITS, t->bits);
		put32(b + TM_CELLS, t->cells);
		put32(b + TM_XCELL, xc);
		put32(b + TM_COL, col);
		put32(b + TM_BAYER, lowtab(t->bayer, 16));
		put32(b + TM_PAT2, lowtab(t->pat2, 1024));
		put32(b + TM_GREY, lowtab(t->grey, 256));
		V32(TM_LEN, t->len);
		V32(TM_YPP, t->ypp);
		put16(b + TM_RB, t->rb);
		put16(b + TM_W, (uint16_t)t->w);
		put16(b + TM_Y0, (uint16_t)t->y0);
		put16(b + TM_Y1, (uint16_t)t->y1);
		put16(b + TM_TOP, (uint16_t)t->top);
		put16(b + TM_MH, (uint16_t)t->mh);
		put16(b + TM_GS, t->gs);
		put16(b + TM_GH, t->gh);
		b[TM_LG] = t->lg;
		call_entry(op, b);
	} else {
		CHECK(0);
	}
}

static uint8_t *load_low(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *d;
	long n;

	*len = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = lowalloc(n ? (size_t)n : 1);
	if (fread(d, 1, (size_t)n, f) != (size_t)n) d = NULL;
	fclose(f);
	*len = (uint32_t)n;
	return d;
}


static uint8_t *tgrid;
static uint16_t tstride;

static void grid_row(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len)
{
	(void)ctx;
	memcpy(tgrid + (uint32_t)row * tstride, bytes, len);
}

static fixed Y(double v) { return (fixed)(v * 65536.0); }

/* A clock that's always up, so each slice does one piece. */
static uint32_t tclock;
static uint32_t tick(void) { return ++tclock; }

/* A w x h surface in low memory, its pixels set to junk. */
static void lowsurf(Surface *s, int w, int h, int scale, int depth)
{
	size_t i, n;
	memset(s, 0, sizeof *s);
	s->scale = (uint8_t)scale;
	s->depth = (uint8_t)depth;
	s->w = (int16_t)w;
	s->h = (int16_t)h;
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	n = (size_t)s->rowBytes * s->h;
	s->bits = lowalloc(n);
	for (i = 0; i < n; i++) s->bits[i] = (uint8_t)(i * 29 + 3);
	gfx_init(s);
}

/* The view v drawn plainly and through the entry must match, as must the
 * green view from there. */
static void both_ways(const RenderView *v, const RenderArt *a, int w, int h, int scale, int depth, int half)
{
	Surface s1, s2;
	void *work;
	size_t n;

	lowsurf(&s1, w, h, scale, depth);
	lowsurf(&s2, w, h, scale, depth);
	s1.half = s2.half = (uint8_t)half;
	n = (size_t)s1.rowBytes * s1.h;
	work = lowalloc(render_work_size(&s1) + 4096);

	render_terra_use(NULL);
	CHECK(render_scene(&s1, v, a, work, NULL) == 0);
	render_terra_use(via_terra);
	CHECK(render_scene(&s2, v, a, work, NULL) == 0);
	render_terra_use(NULL);
	CHECK(memcmp(s1.bits, s2.bits, n) == 0);
	if (memcmp(s1.bits, s2.bits, n)) printf("  scene %dx%d %d-bit half %d differs\n", w, h, depth, half);
	/* and in slices, a piece a call */
	render_terra_use(via_terra);
	CHECK(render_scene_begin(&s2, v, a, work, NULL) == 0);
	{
		int k = 0;
		while (!render_scene_step(work, tick, tclock) && k < 100000) k++;
	}
	render_terra_use(NULL);
	CHECK(memcmp(s1.bits, s2.bits, n) == 0);
	if (memcmp(s1.bits, s2.bits, n)) printf("  sliced scene %dx%d %d-bit half %d differs\n", w, h, depth, half);

	CHECK(render_green_begin(&s1, v, work) == 0);
	render_green_step(work, 0, 0);
	render_terra_use(via_terra);
	CHECK(render_green_begin(&s2, v, work) == 0);
	render_green_step(work, 0, 0);
	render_terra_use(NULL);
	CHECK(memcmp(s1.bits, s2.bits, n) == 0);
	if (memcmp(s1.bits, s2.bits, n)) printf("  green %dx%d %d-bit half %d differs\n", w, h, depth, half);

	/* the hole map, whole and zoomed to a box round the ball, and a few rows of it */
	if (!half) {
		RenderMap m1, m2;
		MapBox box;
		box.x0 = v->ball.x - (40L << 16);
		box.y0 = v->ball.y - (30L << 16);
		box.x1 = v->ball.x + (40L << 16);
		box.y1 = v->ball.y + (90L << 16);
		CHECK(render_map(&s1, v, &m1) == 0);
		render_terra_use(via_terra);
		CHECK(render_map(&s2, v, &m2) == 0);
		render_terra_use(NULL);
		CHECK(memcmp(s1.bits, s2.bits, n) == 0 && memcmp(&m1, &m2, sizeof m1) == 0);
		CHECK(render_map_box(&s1, v, &box, &m1) == 0);
		render_terra_use(via_terra);
		CHECK(render_map_box(&s2, v, &box, &m2) == 0);
		CHECK(render_map_rows(&s2, v, &box, &m2, 7, 30) == 0);
		render_terra_use(NULL);
		CHECK(render_map_rows(&s1, v, &box, &m1, 7, 30) == 0);
		CHECK(memcmp(s1.bits, s2.bits, n) == 0);
		if (memcmp(s1.bits, s2.bits, n)) printf("  map %dx%d %d-bit differs\n", w, h, depth);
	}
}

static void terra_tests(const char *pdbpath, const char *artdir)
{
	uint8_t *pdb, *hrec, row[SURF_ROW_MAX];
	uint32_t n, a, e, len;
	Hole h;
	SurfaceGrid g;
	RenderArt art[2];
	RenderView v;
	char path[512];
	int i, k, count;
	static const double pos[][4] = { { -1, -1, 0 }, { 64, 190, 1 }, { 124, 352, 1 }, { 120, 395, 1 } };
	static const int32_t divs[][2] = {
		{ 0, 1 }, { 7, 2 }, { -7, 2 }, { 7, -2 }, { -7, -2 }, { 262144000, 3 }, { -262144000, 7 },
		{ 65536, -65535 }, { 2147483647, 1 }, { -2147483647, 2 }, { 123456789, 4096 }, { -5, 6 },
	};

	/* terra_div rounds toward zero as / does */
	for (i = 0; i < (int)(sizeof divs / sizeof divs[0]); i++) CHECK(terra_div(divs[i][0], divs[i][1]) == divs[i][0] / divs[i][1]);
	{
		uint32_t x = 12345;
		for (i = 0; i < 100000; i++) {
			int32_t p, q;
			x = x * 1103515245u + 12345u;
			p = (int32_t)x >> (x & 15);
			x = x * 1103515245u + 12345u;
			q = (int32_t)(x >> 8) >> (x & 31);
			if (!q) q = 1;
			if (terra_div(p, q) != p / q) {
				CHECK(0);
				break;
			}
		}
	}

	tblk = lowalloc(TR_SIZE + TT_SIZE + TS_SIZE + TM_SIZE);
	tlist = lowalloc(TERRA_TRIS_MAX * 16);
	tturf = lowalloc(TERRA_TRIS_MAX * TT_TURF_SIZE);
	tuser = lowalloc(6);
	/* the test course's hole 1 and its surface grid, in low memory */
	pdb = load_low(pdbpath, &n);
	if (!pdb || n < 94) {
		printf("FAIL can't read %s\n", pdbpath);
		fails++;
		return;
	}
	count = pdb[76] << 8 | pdb[77];
	a = rd32(pdb + 78 + 8);
	e = count > 2 ? rd32(pdb + 78 + 16) : n;
	len = e - a;
	hrec = lowalloc(len);
	memcpy(hrec, pdb + a, len);
	CHECK(hole_parse(&h, hrec, len) == HOLE_OK);
	if (fails) return;
	surface_size(&h, &g);
	tstride = g.stride;
	tgrid = lowalloc((size_t)g.stride * g.h);
	g.cells = tgrid;
	surface_raster(&h, row, grid_row, &g);

	/* no sprites; the backdrop, so the sky rows have one */
	for (k = 0; k < 2; k++) {
		memset(&art[k], 0, sizeof art[k]);
		snprintf(path, sizeof path, "%s/Bkd8-%d.bin", artdir, 1000 + k);
		art[k].backdrop.p = load_low(path, &art[k].backdrop.len);
		CHECK(art[k].backdrop.p && art[k].backdrop.len);
	}

	v.hole = &h;
	v.grid = &g;
	v.pin = 0;
	v.green = 0;
	for (i = 0; i < (int)(sizeof pos / sizeof pos[0]); i++) {
		if (pos[i][0] < 0) v.ball = h.tees[0].pos;
		else v.ball = vec2_make(Y(pos[i][0]), Y(pos[i][1]));
		{
			vec2 to = pos[i][2] ? h.pins[0] : h.aim;
			v.aim = iatan2(to.y - v.ball.y, to.x - v.ball.x);
		}
		both_ways(&v, &art[0], 160, 160, 1, 2, 0);
		both_ways(&v, &art[0], 160, 160, 1, 1, 0);
		both_ways(&v, &art[0], 160, 160, 1, 4, 0);
		both_ways(&v, &art[0], 160, 160, 1, 8, 0);
		both_ways(&v, &art[1], 320, 320, 2, 8, 0);
		both_ways(&v, &art[1], 320, 448, 2, 8, 0);
		both_ways(&v, &art[0], 320, 320, 2, 4, 1);
		both_ways(&v, &art[1], 480, 320, 2, 8, 0);
	}
	CHECK(tjobs[TERRA_ROW] > 0 && tjobs[TERRA_TRIS] > 0 && tjobs[TERRA_SKY] > 0 && tjobs[TERRA_MAP] > 0);
	printf("armtest: %d terrain steps, %d triangle batches, %d sky slices, %d map slices through the entry\n",
	       tjobs[TERRA_ROW], tjobs[TERRA_TRIS], tjobs[TERRA_SKY], tjobs[TERRA_MAP]);
}

int main(int argc, char **argv)
{
	Surface s;
	uint8_t *d1, *d2, *d3;
	size_t r, k, i;
	int le;

	if (argc != 3) {
		printf("usage: armtest COURSE.pdb ARTDIR\n");
		return 2;
	}
	low = mmap(NULL, LOW_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (low == MAP_FAILED) {
		printf("FAIL mmap\n");
		return 1;
	}
	lowp = low;
	palle = lowalloc(512);
	dith = lowalloc(16);
	{
		static const uint8_t dv[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
		memcpy(dith, dv, 16);
	}
	to16_pal_le(palle, gfx_pal565());
	for (i = 0; i < 256; i++) CHECK((palle[2 * i] | palle[2 * i + 1] << 8) == gfx_pal565()[i]);

	memset(&s, 0, sizeof s);
	s.w = W;
	s.h = H;
	s.depth = 8;
	s.scale = 1;
	s.rowBytes = W + 1;
	s.bits = lowalloc((size_t)s.rowBytes * H);
	gfx_init(&s);
	for (i = 0; i < (size_t)s.rowBytes * H; i++) s.bits[i] = (uint8_t)(i * 37 + 11 + (i >> 7));
	d1 = lowalloc((size_t)DRB * H);
	d2 = lowalloc((size_t)DRB * H);
	d3 = lowalloc((size_t)DRB * H);

	for (le = 0; le < 2; le++)
		for (r = 0; r < sizeof rects / sizeof rects[0]; r++)
			for (k = 0; k < sizeof steps / sizeof steps[0]; k++) {
				const int16_t *q = rects[r];
				uint8_t lo = steps[k][0], hi = steps[k][1];
				for (i = 0; i < (size_t)DRB * H; i++) d1[i] = d2[i] = d3[i] = (uint8_t)(i * 13 + 5);
				gfx_to16_use(NULL);
				if (lo == 0 && hi == 16 && (r & 1)) gfx_to16(&s, d1, DRB, q[0], q[1], q[2], q[3], (uint8_t)le);
				else gfx_to16_mask(&s, d1, DRB, q[0], q[1], (int16_t)(q[0] + q[2]), (int16_t)(q[1] + q[3]), (uint8_t)le, lo, hi);
				gfx_to16_use(via_entry);
				if (lo == 0 && hi == 16 && (r & 1)) gfx_to16(&s, d2, DRB, q[0], q[1], q[2], q[3], (uint8_t)le);
				else gfx_to16_mask(&s, d2, DRB, q[0], q[1], (int16_t)(q[0] + q[2]), (int16_t)(q[1] + q[3]), (uint8_t)le, lo, hi);
				gfx_to16_use(NULL);
				old_style(&s, d3, q[0], q[1], q[0] + q[2], q[1] + q[3], le, lo, hi);
				CHECK(memcmp(d1, d2, (size_t)DRB * H) == 0);
				CHECK(memcmp(d1, d3, (size_t)DRB * H) == 0);
			}
	/* the job count: every rectangle with something in it, both byte orders */
	CHECK(jobs == 2 * 7 * 8);
	terrain_edges(&s, d1, d2, d3);
	terra_tests(argv[1], argv[2]);
	printf("armtest: %d checks, %d failed, %d jobs through the entry\n", checks, fails, jobs);
	munmap(low, LOW_SIZE);
	return fails != 0;
}
