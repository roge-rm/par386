/* Renderer test. Run by `make render-test`:
 *   rendertest COURSE.pdb ARTDIR OUTDIR
 * Draws the test hole from the tee, mid-fairway and near the green at
 * 160x160 2-bit and 320x320 8-bit, writes each as a PGM of pixel values to
 * OUTDIR (tools/refcmp.py turns them into PNGs and compares them with
 * host/ref/), checks the view has no gaps, that drawing is repeatable, and
 * that odd cameras, a hole with no objects and missing or bad art don't
 * crash or read out of range. Prints the work counts for the timing guess. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "pal.h"
#include "play.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define SENTINEL 253    /* a spare palette index (PAL_SPARE + 1) the renderer never writes */

static const char *outdir;

static uint8_t *load_file(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *d;
	long n;

	*len = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc(n ? n : 1);
	if (fread(d, 1, n, f) != (size_t)n) { free(d); fclose(f); return NULL; }
	fclose(f);
	*len = (uint32_t)n;
	return d;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* Record n of a PDB as its own exact-size copy, so ASan sees reads past its end. */
static uint8_t *pdb_record(const uint8_t *d, uint32_t n, int k, uint32_t *len)
{
	int count = d[76] << 8 | d[77];
	uint32_t a, b;
	uint8_t *r;

	if (k >= count) return NULL;
	a = be32(d + 78 + 8 * k);
	b = k + 1 < count ? be32(d + 78 + 8 * (k + 1)) : n;
	*len = b - a;
	r = malloc(*len);
	memcpy(r, d + a, *len);
	return r;
}

static uint8_t *grid;
static uint16_t grid_stride;

static void put_row(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len)
{
	(void)ctx;
	memcpy(grid + (uint32_t)row * grid_stride, bytes, len);
}

static RenderData art_file(const char *dir, const char *type, int id)
{
	char path[512];
	RenderData d;
	snprintf(path, sizeof path, "%s/%s-%d.bin", dir, type, id);
	d.p = load_file(path, &d.len);
	return d;
}

/* The shared sprites, the golfer's poses with room for the pixels under
 * him at any scale, and the first backdrop. */
static void art_load(RenderArt *a, const char *dir, int scale)
{
	int t, k;
	RenderGolfer *g = calloc(1, sizeof *g);
	memset(a, 0, sizeof *a);
	for (t = 0; t < RENDER_TYPES; t++)
		for (k = 0; k < RENDER_SIZES; k++)
			a->spr[t][0][k] = art_file(dir, "Spr8", 1000 + t * 10 + k);
	for (k = 0; k < RENDER_POSES; k++) g->pose[k] = art_file(dir, "Spr8", 2000 + 10 * k + scale - 1);
	g->under = malloc(RENDER_UNDER_UNITS * RENDER_UNDER_UNITS * 4);
	a->golfer = g;
	a->backdrop = art_file(dir, "Bkd8", 1000 + scale - 1);
}

static void art_free(RenderArt *a)
{
	int t, k;
	for (t = 0; t < RENDER_TYPES; t++)
		for (k = 0; k < RENDER_SIZES; k++)
			free((void *)a->spr[t][0][k].p);
	for (k = 0; k < RENDER_POSES; k++) free((void *)a->golfer->pose[k].p);
	free(a->golfer->under);
	free(a->golfer);
	free((void *)a->backdrop.p);
}


#define GUARD 64

/* A w x h surface with GUARD guard bytes either side of its pixels. */
static void surface_wh(Surface *s, int w, int h, int scale, int depth)
{
	uint8_t *b;
	memset(s, 0, sizeof *s);
	s->scale = (uint8_t)scale;
	s->depth = (uint8_t)depth;
	s->w = (int16_t)w;
	s->h = (int16_t)h;
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	b = malloc((size_t)s->rowBytes * s->h + 2 * GUARD);
	memset(b, 0xA5, (size_t)s->rowBytes * s->h + 2 * GUARD);
	s->bits = b + GUARD;
	gfx_init(s);
}

static void surface_d(Surface *s, int scale, int depth) { surface_wh(s, 160 * scale, 160 * scale, scale, depth); }

static void surface(Surface *s, int scale) { surface_d(s, scale, scale > 1 ? 8 : 2); }

/* Checks the guard bytes are untouched and frees the surface. */
static void sfree(Surface *s)
{
	uint8_t *b = s->bits - GUARD, *e = s->bits + (size_t)s->rowBytes * s->h;
	int i, bad = 0;
	for (i = 0; i < GUARD; i++) bad += (b[i] != 0xA5) + (e[i] != 0xA5);
	CHECK(bad == 0);
	if (bad) printf("  %d guard bytes written round a %dx%d %d-bit surface\n", bad, s->w, s->h, s->depth);
	free(b);
}

static uint8_t pixel_of(const uint8_t *bits, const Surface *s, int x, int y)
{
	const uint8_t *row = bits + (size_t)y * s->rowBytes;
	int d = s->depth, per = 8 / d;
	if (d == 8) return row[x];
	return (uint8_t)((row[x / per] >> (8 - d - d * (x % per))) & ((1 << d) - 1));
}

static uint8_t pixel(const Surface *s, int x, int y) { return pixel_of(s->bits, s, x, y); }

/* NAME-W.pgm (or NAME-WxH.pgm if it isn't square); grey screens keep their
 * depth in the maximum value, which tools/refcmp.py reads. */
static void save(const Surface *s, const char *name)
{
	char path[512];
	FILE *f;
	int x, y;

	if (s->w == s->h) snprintf(path, sizeof path, "%s/%s-%d.pgm", outdir, name, s->w);
	else snprintf(path, sizeof path, "%s/%s-%dx%d.pgm", outdir, name, s->w, s->h);
	f = fopen(path, "wb");
	if (!f) { CHECK(!"open pgm"); return; }
	fprintf(f, "P5\n%d %d\n%d\n", s->w, s->h, s->depth == 8 ? 255 : (1 << s->depth) - 1);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) fputc(pixel(s, x, y), f);
	fclose(f);
}

/* An 8-bit surface as 16-bit colour through gfx_to16, saved as NAME-W-16.ppm. */
static void save16(const Surface *s, const char *name)
{
	char path[512];
	FILE *f;
	int x, y;
	uint16_t rb = (uint16_t)(s->w * 2);
	uint8_t *b = malloc((size_t)rb * s->h + 2 * GUARD), *d = b + GUARD;

	memset(b, 0xA5, (size_t)rb * s->h + 2 * GUARD);
	/* in two pieces, to check the rectangle */
	gfx_to16(s, d, rb, 0, 0, s->w, s->h / 2, 0);
	gfx_to16(s, d, rb, 3, s->h / 2, s->w - 3, s->h - s->h / 2, 0);
	gfx_to16(s, d, rb, 0, s->h / 2, 3, s->h - s->h / 2, 0);
	for (x = 0; x < GUARD; x++) CHECK(b[x] == 0xA5 && d[(size_t)rb * s->h + x] == 0xA5);
	if (s->w == s->h) snprintf(path, sizeof path, "%s/%s-%d-16.ppm", outdir, name, s->w);
	else snprintf(path, sizeof path, "%s/%s-%dx%d-16.ppm", outdir, name, s->w, s->h);
	f = fopen(path, "wb");
	if (!f) { CHECK(!"open ppm"); free(b); return; }
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) {
			const uint8_t *q = d + (size_t)y * rb + 2 * x;
			uint16_t c = (uint16_t)(q[0] << 8 | q[1]);
			fputc((c >> 11) * 255 / 31, f);
			fputc(((c >> 5) & 63) * 255 / 63, f);
			fputc((c & 31) * 255 / 31, f);
		}
	fclose(f);
	free(b);
}

/* Draws one view; checks the 8-bit view has no pixel left unwritten. */
static int draw(Surface *s, const RenderView *v, const RenderArt *a, RenderStats *st)
{
	uint32_t n = render_work_size(s);
	uint8_t *work = malloc(n);
	int r, x, y, left = 0;

	memset(s->bits, s->depth == 8 ? SENTINEL : 0x55, (size_t)s->rowBytes * s->h);
	r = render_scene(s, v, a, work, st);
	free(work);
	if (s->depth == 8)
		for (y = 0; y < s->h; y++)
			for (x = 0; x < s->w; x++)
				if (pixel(s, x, y) == SENTINEL) left++;
	CHECK(left == 0);
	if (left) printf("  %d pixels left unwritten\n", left);
	return r;
}

/* The tee view, then the golfer's poses over it: each changes only the
 * box render_pose gives, and going back to address with the ball gives the
 * scene as drawn. Saves the top of the swing and the finish as NAME-top
 * and NAME-finish if name isn't 0. */
static void poses(const RenderView *v, const RenderArt *a, int scale, int depth, int half, const char *name)
{
	Surface s;
	uint8_t *first;
	size_t n;
	int16_t x, y, w, h;
	int k, xx, yy, out = 0;
	char nm[64];

	surface_d(&s, scale, depth);
	s.half = (uint8_t)half;
	n = (size_t)s.rowBytes * s.h;
	CHECK(draw(&s, v, a, NULL) == 0);
	CHECK(a->golfer->w > 0 && a->golfer->shown == POSE_ADDRESS && a->golfer->ball == 1);
	first = malloc(n);
	memcpy(first, s.bits, n);
	for (k = POSE_BACK; k <= POSE_FINISH; k++) {
		CHECK(render_pose(&s, a->golfer, (uint8_t)k, k != POSE_FINISH, &x, &y, &w, &h) == 0);
		/* nothing outside the box it gives */
		for (yy = 0; yy < s.h; yy++)
			for (xx = 0; xx < s.w; xx++)
				if ((xx < x || xx >= x + w || yy < y || yy >= y + h) && pixel(&s, xx, yy) != pixel_of(first, &s, xx, yy)) out++;
		if (name && k == POSE_TOP) { snprintf(nm, sizeof nm, "%s-top", name); save(&s, nm); }
		if (name && k == POSE_FINISH) { snprintf(nm, sizeof nm, "%s-finish", name); save(&s, nm); }
	}
	CHECK(out == 0);
	if (out) printf("  %d pixels changed outside the pose box\n", out);
	CHECK(memcmp(first, s.bits, n) != 0);
	CHECK(render_pose(&s, a->golfer, POSE_ADDRESS, 1, &x, &y, &w, &h) == 0);
	CHECK(memcmp(first, s.bits, n) == 0);
	CHECK(render_pose(&s, a->golfer, RENDER_POSES, 1, &x, &y, &w, &h) == -1);
	free(first);
	sfree(&s);
}

/* A clock for render_scene_step that moves on one a look. */
static uint32_t fake_t;
static uint32_t fake_clock(void) { return fake_t++; }

/* The scene drawn in slices (render_scene_begin and _step, a slice every
 * per looks at the clock) is byte for byte the one drawn in one go, the
 * golfer's box and the pixels kept under him too; at 8 bits the 16-bit
 * colour made from it as well. */
static void sliced(const RenderView *v, const RenderArt *a, int w, int h, int scale, int depth, int half, uint32_t per)
{
	Surface s, t;
	RenderGolfer g0 = *a->golfer, g1;
	RenderArt a1 = *a;
	RenderStats st0, st1;
	uint32_t un = 0, n;
	uint8_t *work, *u0 = 0;
	size_t bytes;
	int k, slices = 0;

	surface_wh(&s, w, h, scale, depth);
	surface_wh(&t, w, h, scale, depth);
	s.half = t.half = (uint8_t)half;
	bytes = (size_t)s.rowBytes * s.h;
	un = render_under_size(&s);
	n = render_work_size(&s);
	work = malloc(n);
	/* in one go, with the golfer's under buffer as the art gave it */
	memset(s.bits, 0x33, bytes);
	CHECK(render_scene(&s, v, a, work, &st0) == 0);
	g0 = *a->golfer;
	if (g0.under) {
		u0 = malloc(un);
		memcpy(u0, g0.under, un);
	}
	/* in slices, into a golfer of its own */
	g1 = *a->golfer;
	g1.under = malloc(un);
	memset(g1.under, 0x77, un);
	g1.w = 0;
	g1.shown = POSE_TOP;
	a1.golfer = &g1;
	memset(work, 0xEE, n);
	memset(t.bits, 0x33, bytes);
	CHECK(render_scene_begin(&t, v, &a1, work, &st1) == 0);
	do {
		k = render_scene_step(work, fake_clock, fake_t + per);
		CHECK(k >= 0);
		slices++;
	} while (k == 0 && slices < 100000);
	CHECK(k == 1 && render_scene_step(work, fake_clock, fake_t + per) == 1);
	CHECK(slices > (int)(40 / per));
	if (slices <= (int)(40 / per)) printf("  %d slices of %u\n", slices, (unsigned)per);
	CHECK(memcmp(s.bits, t.bits, bytes) == 0);
	CHECK(g1.x == g0.x && g1.y == g0.y && g1.w == g0.w && g1.h == g0.h && g1.bx == g0.bx && g1.by == g0.by);
	CHECK(g1.shown == g0.shown && g1.ball == g0.ball && g1.half == g0.half && g0.w > 0);
	if (u0) {
		/* the bytes of the box in use: whole bytes of each row, or a byte a pixel at half size */
		int lg = depth == 8 ? 3 : depth == 4 ? 2 : depth == 2 ? 1 : 0;
		uint32_t used = g0.half ? (uint32_t)g0.w * g0.h :
		                (uint32_t)((((g0.x + g0.w - 1) >> (3 - lg)) - (g0.x >> (3 - lg)) + 1) * g0.h);
		CHECK(used <= un && memcmp(u0, g1.under, used) == 0);
	}
	for (k = 0; k < RS_COUNT; k++) st0.ticks[k] = st1.ticks[k] = 0;
	CHECK(memcmp(&st0, &st1, sizeof st0) == 0);
	if (memcmp(s.bits, t.bits, bytes)) printf("  sliced %dx%d %d-bit half %d: pixels differ\n", w, h, depth, half);
	if (depth == 8) {
		uint16_t rb = (uint16_t)(w * 2);
		uint8_t *c0 = malloc((size_t)rb * h), *c1 = malloc((size_t)rb * h);
		gfx_to16(&s, c0, rb, 0, 0, s.w, s.h, 0);
		gfx_to16(&t, c1, rb, 0, 0, t.w, t.h, 0);
		CHECK(memcmp(c0, c1, (size_t)rb * h) == 0);
		free(c0);
		free(c1);
	}
	free(u0);
	free(g1.under);
	free(work);
	sfree(&s);
	sfree(&t);
}

/* The green view drawn in slices is byte for byte the one drawn in one go. */
static void sliced_green(const RenderView *v, int w, int h, int scale, int depth, int half)
{
	Surface s, t;
	size_t bytes;
	uint32_t n;
	uint8_t *work;
	int k, slices = 0;

	surface_wh(&s, w, h, scale, depth);
	surface_wh(&t, w, h, scale, depth);
	s.half = t.half = (uint8_t)half;
	bytes = (size_t)s.rowBytes * s.h;
	n = render_work_size(&s);
	work = malloc(n);
	memset(s.bits, 0x33, bytes);
	memset(t.bits, 0x44, bytes);
	memset(work, 0xEE, n);
	CHECK(render_green(&s, v) == 0);
	CHECK(render_green_begin(&t, v, work) == 0);
	do {
		k = render_green_step(work, fake_clock, fake_t + 1);
		slices++;
	} while (k == 0 && slices < 10000);
	CHECK(k == 1 && render_green_step(work, fake_clock, fake_t + 1) == 1 && slices > 5);
	CHECK(memcmp(s.bits, t.bits, bytes) == 0);
	if (memcmp(s.bits, t.bits, bytes)) printf("  sliced green %dx%d %d-bit half %d: pixels differ\n", w, h, depth, half);
	free(work);
	sfree(&s);
	sfree(&t);
}

/* Draws a 160 view at 8 bits only to check it has no gaps. */
static void gap160(const RenderView *v, const RenderArt *a)
{
	Surface s;
	surface_d(&s, 1, 8);
	CHECK(draw(&s, v, a, NULL) == 0);
	sfree(&s);
}

/* Draws a 160 view at another depth, so its pixel writes run under ASan. */
static void depth160(const RenderView *v, const RenderArt *a, int depth)
{
	Surface s;
	surface_d(&s, 1, depth);
	CHECK(draw(&s, v, a, NULL) == 0);
	sfree(&s);
}

/* Draws the hole map twice onto different fill values; any pixel that differs was never written. */
static int draw_map(Surface *s, const Play *p, int *unwritten)
{
	Surface t = *s;
	RenderMap m;
	size_t n = (size_t)s->rowBytes * s->h, i;
	int r;

	t.bits = malloc(n);
	memset(s->bits, 0x00, n);
	memset(t.bits, 0xFF, n);
	r = render_map(s, &p->rv, &m);
	render_map(&t, &p->rv, &m);
	*unwritten = 0;
	for (i = 0; i < n; i++) *unwritten += s->bits[i] != t.bits[i];
	free(t.bits);
	return r;
}

/* A copy of the first len bytes in a malloc of exactly len bytes. */
static const uint8_t *exact(const uint8_t *p, uint32_t len)
{
	uint8_t *c;
	if (!p) return NULL;
	c = malloc(len ? len : 1);
	memcpy(c, p, len);
	return c;
}

/* The tee view with its status strip, the map with its labels and the green
 * view from 7 yd short of the pin on one surface, saved as NAME-tee,
 * NAME-map and NAME-green (NAME empty for the size alone). The guard bytes
 * are checked when it's freed. */
static void views(Surface *s, const char *name, const RenderView *tee, const RenderArt *a, int with16)
{
	char t[64];
	Play pl;
	RenderView gv = *tee;
	int un;

	CHECK(draw(s, tee, a, NULL) == 0);
	play_init(&pl, tee->hole, tee->grid, 1);
	play_frame(&pl, s, NULL, 0);
	snprintf(t, sizeof t, "%stee", name);
	save(s, t);
	if (with16) save16(s, t);
	CHECK(draw_map(s, &pl, &un) == 0);
	CHECK(un == 0);
	play_map(s, &pl);
	play_status(s, &pl, NULL);
	snprintf(t, sizeof t, "%smap", name);
	save(s, t);
	gv.green = 1;
	gv.ball = vec2_make(tee->hole->pins[0].x - (2L << 16), tee->hole->pins[0].y - (7L << 16));
	gv.aim = iatan2(tee->hole->pins[0].y - gv.ball.y, tee->hole->pins[0].x - gv.ball.x);
	CHECK(render_green(s, &gv) == 0);
	snprintf(t, sizeof t, "%sgreen", name);
	save(s, t);
	if (with16) save16(s, t);
}

static fixed Y(double v) { return (fixed)(v * 65536.0); }

static angle toward(vec2 from, vec2 to) { return iatan2(to.y - from.y, to.x - from.x); }

int main(int argc, char **argv)
{
	uint8_t *pdb, *crec, *hrec, *bare;
	uint32_t n, clen, hlen, blen;
	Course c;
	Hole h, hb;
	SurfaceGrid g;
	uint8_t row[SURF_ROW_MAX];
	RenderArt art[2], none;
	RenderView v;
	RenderStats st;
	uint32_t dropped = 0;
	struct { const char *name; double x, y; int to_pin; } pos[3] = {
		{ "tee", 0, 0, 0 }, { "fairway", 64, 190, 1 }, { "approach", 124, 352, 1 }
	};
	int i, scale;

	if (argc != 4) {
		printf("usage: rendertest COURSE.pdb ARTDIR OUTDIR\n");
		return 2;
	}
	outdir = argv[3];
	pdb = load_file(argv[1], &n);
	if (!pdb || n < 78) { printf("FAIL can't read %s\n", argv[1]); return 1; }
	crec = pdb_record(pdb, n, 0, &clen);
	hrec = pdb_record(pdb, n, 1, &hlen);
	CHECK(crec && course_parse(&c, crec, clen) == HOLE_OK);
	CHECK(hrec && hole_parse(&h, hrec, hlen) == HOLE_OK);
	if (fails) return 1;
	surface_size(&h, &g);
	grid_stride = g.stride;
	grid = malloc((size_t)g.stride * g.h);
	g.cells = grid;
	surface_raster(&h, row, put_row, &g);
	art_load(&art[0], argv[2], 1);
	art_load(&art[1], argv[2], 2);
	for (i = 0; i < RENDER_POSES; i++) CHECK(art[0].golfer->pose[i].len && art[1].golfer->pose[i].len);
	CHECK(art[0].backdrop.len && art[1].backdrop.len);
	CHECK(art[0].spr[OBJ_TREE][0][0].len && art[0].spr[OBJ_PINE][0][1].len);

	pos[0].x = h.tees[0].pos.x / 65536.0;
	pos[0].y = h.tees[0].pos.y / 65536.0;
	v.hole = &h;
	v.grid = &g;
	v.pin = 0;
	v.green = 0;
	for (i = 0; i < 3; i++) {
		v.ball = vec2_make(Y(pos[i].x), Y(pos[i].y));
		v.aim = toward(v.ball, pos[i].to_pin ? h.pins[0] : h.aim);
		for (scale = 1; scale <= 2; scale++) {
			Surface s, s2;
			surface(&s, scale);
			surface(&s2, scale);
			CHECK(draw(&s, &v, &art[scale - 1], &st) == 0);
			printf("%s %d: %u steps, %u samples, %u spans, %u terrain px, %u sky px, %u objects, %u sprite px, %u divides\n",
			       pos[i].name, s.w, st.steps, st.samples, st.spans, st.pixels, st.sky, st.objects,
			       st.sprite_px, st.divides);
			printf("  %u objects dropped\n", st.dropped);
			dropped += st.dropped;
			save(&s, pos[i].name);
			if (scale == 1) gap160(&v, &art[0]);
			if (scale == 1 && i == 0) { depth160(&v, &art[0], 4); depth160(&v, &art[0], 1); }
			/* the same view again gives the same picture */
			CHECK(draw(&s2, &v, &art[scale - 1], NULL) == 0);
			CHECK(memcmp(s.bits, s2.bits, (size_t)s.rowBytes * s.h) == 0);
			sfree(&s);
			sfree(&s2);
		}
	}

	CHECK(dropped == 0);

	/* The golfer's poses over the tee view at each depth and in half size drawing. */
	v.ball = vec2_make(Y(pos[0].x), Y(pos[0].y));
	v.aim = toward(v.ball, h.aim);
	poses(&v, &art[0], 1, 2, 0, "tee");
	poses(&v, &art[1], 2, 8, 0, "tee");
	poses(&v, &art[0], 1, 1, 0, 0);
	poses(&v, &art[0], 1, 4, 0, 0);
	poses(&v, &art[0], 1, 8, 0, 0);
	poses(&v, &art[0], 2, 4, 1, "h4-tee");
	poses(&v, &art[0], 2, 2, 1, 0);

	/* Drawn in slices, the same as in one go: 160 and 320, half size, 2, 8 and 16 bits (8 turned to 16), tall. */
	{
		RenderView fv = v;
		int q;
		fv.ball = vec2_make(Y(pos[1].x), Y(pos[1].y));
		fv.aim = toward(fv.ball, h.pins[0]);
		for (q = 0; q < 2; q++) {
			const RenderView *w = q ? &fv : &v;
			sliced(w, &art[0], 160, 160, 1, 2, 0, 1);
			sliced(w, &art[0], 160, 160, 1, 2, 0, 7);
			sliced(w, &art[0], 160, 160, 1, 8, 0, 3);
			sliced(w, &art[0], 160, 160, 1, 1, 0, 3);
			sliced(w, &art[0], 160, 160, 1, 4, 0, 3);
			sliced(w, &art[1], 320, 320, 2, 8, 0, 1);
			sliced(w, &art[1], 320, 320, 2, 8, 0, 5);
			sliced(w, &art[0], 320, 320, 2, 2, 1, 1);
			sliced(w, &art[0], 320, 320, 2, 8, 1, 4);
			sliced(w, &art[0], 320, 320, 2, 4, 1, 2);
			sliced(w, &art[1], 320, 448, 2, 8, 0, 2);
		}
		fv.green = 1;
		fv.ball = vec2_make(h.pins[0].x - (2L << 16), h.pins[0].y - (7L << 16));
		fv.aim = toward(fv.ball, h.pins[0]);
		sliced_green(&fv, 160, 160, 1, 2, 0);
		sliced_green(&fv, 160, 160, 1, 1, 0);
		sliced_green(&fv, 320, 320, 2, 8, 0);
		sliced_green(&fv, 320, 320, 2, 4, 1);
		sliced_green(&fv, 320, 448, 2, 8, 0);
		fv.ball = vec2_make(h.pins[0].x - (12L << 16), h.pins[0].y - (25L << 16));
		fv.aim = toward(fv.ball, h.pins[0]);
		sliced_green(&fv, 160, 160, 1, 2, 0);
		sliced_green(&fv, 320, 320, 2, 2, 1);
	}

	/* Odd cameras: off the side looking out of bounds, past the far end, in a corner looking back. */
	{
		static const double odd[][3] = {
			{ -40, 200, 180 }, { 100, 700, 90 }, { 0, 0, 225 }, { 199, 439, 45 }, { 100, -30, 270 }
		};
		for (i = 0; i < 5; i++) {
			for (scale = 1; scale <= 2; scale++) {
				Surface s;
				surface(&s, scale);
				v.ball = vec2_make(Y(odd[i][0]), Y(odd[i][1]));
				v.aim = (angle)(odd[i][2] * 65536.0 / 360.0);
				v.pin = (uint8_t)i;
				CHECK(draw(&s, &v, &art[scale - 1], NULL) == 0);
				if (i == 0) save(&s, "outside");
				if (scale == 1) gap160(&v, &art[0]);
				sfree(&s);
			}
		}
		v.pin = 0;
	}

	/* The hole with its object list cut off, and with no art at all. */
	{
		uint32_t cut = (uint32_t)(h.objects - hrec) - 2;
		Surface s;
		blen = cut + 2;
		bare = malloc(blen);
		memcpy(bare, hrec, cut);
		bare[cut] = bare[cut + 1] = 0;
		CHECK(hole_parse(&hb, bare, blen) == HOLE_OK && hb.object_count == 0);
		v.hole = &hb;
		v.ball = h.tees[0].pos;
		v.aim = toward(v.ball, h.aim);
		memset(&none, 0, sizeof none);
		for (scale = 1; scale <= 2; scale++) {
			surface(&s, scale);
			CHECK(draw(&s, &v, &art[scale - 1], NULL) == 0);
			CHECK(draw(&s, &v, &none, NULL) == 0);
			sfree(&s);
		}
		v.hole = &h;
		free(bare);
	}

	/* A crowd of 120 small bushes nearer than a big tree 300 yards out: the
	 * object list keeps the ones biggest on screen, so the tree is drawn
	 * though bushes are left out. */
	{
		static uint8_t crowd[121 * 7];
		Hole hc = h;
		Surface s, s2;
		RenderStats sc;
		int k, q;
		for (k = 0; k < 121; k++) {
			uint8_t *p = crowd + 7 * k;
			int x = k < 120 ? 62 * 4 + (k % 12) * 20 - 110 : 62 * 4;
			int y = k < 120 ? (40 + (k / 12) * 8) * 4 : 320 * 4;
			p[0] = k < 120 ? OBJ_BUSH : OBJ_TREE;
			p[1] = 0;
			p[2] = k < 120 ? 64 : 255;
			p[3] = (uint8_t)(x >> 8); p[4] = (uint8_t)x;
			p[5] = (uint8_t)(y >> 8); p[6] = (uint8_t)y;
		}
		hc.objects = crowd;
		v.hole = &hc;
		v.ball = h.tees[0].pos;
		v.aim = toward(v.ball, vec2_make(Y(62), Y(320)));
		for (scale = 1; scale <= 2; scale++) {
			surface(&s, scale);
			surface(&s2, scale);
			hc.object_count = 121;
			CHECK(draw(&s, &v, &art[scale - 1], &sc) == 0);
			q = sc.dropped > 0;
			printf("crowd %d: %u objects, %u dropped\n", s.w, sc.objects, sc.dropped);
			hc.object_count = 120;
			CHECK(draw(&s2, &v, &art[scale - 1], NULL) == 0);
			CHECK(q && memcmp(s.bits, s2.bits, (size_t)s.rowBytes * s.h) != 0);
			sfree(&s);
			sfree(&s2);
		}
		v.hole = &h;
	}

	/* Bad art: short sprites and a backdrop cut off part way. */
	{
		RenderArt bad = art[0];
		RenderGolfer bg = *art[0].golfer;
		Surface s;
		int t, k;
		for (t = 0; t < RENDER_TYPES; t++) {
			bad.spr[t][0][0].len = bad.spr[t][0][0].len ? 13 : 0;
			bad.spr[t][0][1].len = bad.spr[t][0][1].len ? 11 : 0;
		}
		for (k = 0; k < RENDER_POSES; k++) bg.pose[k].len = 20;
		bad.golfer = &bg;
		bad.backdrop.len = bad.backdrop.len / 2;
		/* exact-size copies, so ASan sees any read past the shortened length */
		for (t = 0; t < RENDER_TYPES; t++)
			for (k = 0; k < RENDER_SIZES; k++)
				bad.spr[t][0][k].p = exact(bad.spr[t][0][k].p, bad.spr[t][0][k].len);
		for (k = 0; k < RENDER_POSES; k++) bg.pose[k].p = exact(bg.pose[k].p, bg.pose[k].len);
		bad.backdrop.p = exact(bad.backdrop.p, bad.backdrop.len);
		surface(&s, 1);
		v.ball = h.tees[0].pos;
		v.aim = toward(v.ball, h.aim);
		CHECK(draw(&s, &v, &bad, NULL) == 0);
		sfree(&s);
		for (t = 0; t < RENDER_TYPES; t++)
			for (k = 0; k < RENDER_SIZES; k++) free((void *)bad.spr[t][0][k].p);
		for (k = 0; k < RENDER_POSES; k++) free((void *)bg.pose[k].p);
		free((void *)bad.backdrop.p);
		/* a surface that isn't 160 or 320 wide is turned down */
		surface(&s, 1);
		s.w = 100;
		CHECK(render_scene(&s, &v, &art[0], NULL, NULL) == -1);
		sfree(&s);
	}

	/* The hole map at 160 2-bit and 320 8-bit, then the status strip and the map labels. */
	{
		Play pl;
		Surface s;
		int unwritten;
		play_init(&pl, &h, &g, 1);
		for (scale = 1; scale <= 2; scale++) {
			surface(&s, scale);
			CHECK(draw_map(&s, &pl, &unwritten) == 0);
			CHECK(unwritten == 0);
			if (unwritten) printf("  map %d: %d bytes left unwritten\n", s.w, unwritten);
			if (scale == 1) {
				play_status(&s, &pl, NULL);
				play_status(&s, &pl, "Info text");
				play_map(&s, &pl);
			}
			save(&s, "map");
			sfree(&s);
		}
		surface(&s, 2);
		play_status(&s, &pl, NULL);
		play_map(&s, &pl);
		sfree(&s);
	}

	/* The green view from 7 yd short of the pin, at both sizes and every depth,
	 * then odd spots: on the pin, from the tee, off the hole's corner. */
	{
		RenderView gv = v;
		static const double odd[][2] = { { 0, 0 }, { 60, 12 }, { -30, -30 }, { 230, 470 } };
		int k, greens;
		gv.green = 1;
		gv.pin = 0;
		gv.ball = vec2_make(h.pins[0].x - Y(2), h.pins[0].y - Y(7));
		gv.aim = toward(gv.ball, h.pins[0]);
		for (scale = 1; scale <= 2; scale++) {
			Surface s, s2;
			int x, y;
			surface(&s, scale);
			surface(&s2, scale);
			memset(s.bits, s.depth == 8 ? SENTINEL : 0x55, (size_t)s.rowBytes * s.h);
			CHECK(render_green(&s, &gv) == 0);
			CHECK(render_green(&s2, &gv) == 0);
			CHECK(memcmp(s.bits, s2.bits, (size_t)s.rowBytes * s.h) == 0);
			greens = 0;
			if (s.depth == 8)
				for (y = 0; y < s.h; y++)
					for (x = 0; x < s.w; x++) {
						uint8_t c = pixel(&s, x, y);
						CHECK(c != SENTINEL);
						greens += c >= PAL_GREEN && c < PAL_GREEN + PAL_RAMP_N;
					}
			if (s.depth == 8) {
				printf("green view %d: %d%% green\n", s.w, greens * 100 / (s.w * s.h));
				CHECK(greens > s.w * s.h / 2);
			}
			save(&s, "green");
			sfree(&s);
			sfree(&s2);
		}
		/* the aim turned well off the pin both ways: nothing written outside the bitmap */
		for (k = -1; k <= 1; k += 2) {
			int g, a;
			for (a = 1; a <= 8; a++) {
				Surface s;
				size_t n;
				uint8_t *raw, *keep;
				RenderView ov = gv;
				ov.aim = (angle)(gv.aim + k * 1500 * a);
				surface(&s, 1);
				n = (size_t)s.rowBytes * s.h;
				raw = malloc(n + 128);
				memset(raw, 0xA5, n + 128);
				keep = s.bits;
				s.bits = raw + 64;
				CHECK(render_green(&s, &ov) == 0);
				for (g = 0; g < 64; g++) CHECK(raw[g] == 0xA5 && raw[64 + n + g] == 0xA5);
				s.bits = keep;
				free(raw);
				sfree(&s);
			}
		}
		for (k = 4; k >= 1; k >>= 2) {
			Surface s;
			surface_d(&s, 1, k);
			CHECK(render_green(&s, &gv) == 0);
			sfree(&s);
		}
		for (k = 0; k < 4; k++) {
			Surface s;
			gv.ball = k == 0 ? h.pins[0] : vec2_make(Y(odd[k][0]), Y(odd[k][1]));
			gv.aim = toward(gv.ball, h.pins[0]);
			surface(&s, 1);
			CHECK(render_green(&s, &gv) == 0);
			sfree(&s);
			surface(&s, 2);
			CHECK(render_green(&s, &gv) == 0);
			sfree(&s);
		}
		{
			Surface s;
			surface(&s, 1);
			s.w = 100;
			CHECK(render_green(&s, &gv) == -1);
			sfree(&s);
		}
	}

	/* Every depth and size: 4 and 1 bit at 160 (d4-, d1-), the 8-bit views as
	 * 16-bit colour, the tall 320 x 480 and the landscape 480 x 320 screens. */
	{
		Surface s;
		v.ball = h.tees[0].pos;
		v.aim = toward(v.ball, h.aim);
		v.pin = 0;
		v.green = 0;
		surface_d(&s, 1, 4);
		views(&s, "d4-", &v, &art[0], 0);
		sfree(&s);
		surface_d(&s, 1, 1);
		views(&s, "d1-", &v, &art[0], 0);
		sfree(&s);
		surface_d(&s, 1, 8);
		views(&s, "d8-", &v, &art[0], 1);
		sfree(&s);
		surface_d(&s, 2, 8);
		views(&s, "d8-", &v, &art[1], 1);
		sfree(&s);
		surface_wh(&s, 320, 480, 2, 8);
		views(&s, "", &v, &art[1], 1);
		sfree(&s);
		surface_wh(&s, 480, 320, 2, 8);
		views(&s, "", &v, &art[1], 1);
		sfree(&s);
		/* and every depth at those sizes, for the guard bytes */
		for (i = 1; i <= 4; i <<= 1) {
			surface_wh(&s, 320, 480, 2, i);
			views(&s, "x-", &v, &art[1], 0);
			sfree(&s);
			surface_wh(&s, 480, 320, 2, i);
			views(&s, "x-", &v, &art[1], 0);
			sfree(&s);
		}
		/* Half size at 320 (Surface.half, slow 68k devices): the views drawn at
		 * 160 and doubled, with scale 1 art; the scene, map and green view
		 * match the 160 ones doubled pixel for pixel. */
		{
			static const uint8_t hd[4] = { 1, 2, 4, 8 };
			int k, x, y, bad;
			for (k = 0; k < 4; k++) {
				Surface a, b;
				RenderView gv = v;
				RenderMap ma, mb;
				int view;
				surface_d(&a, 1, hd[k]);
				surface_d(&b, 2, hd[k]);
				b.half = 1;
				CHECK(render_work_size(&b) == render_work_size(&a));
				gv.green = 1;
				gv.ball = vec2_make(h.pins[0].x - (2L << 16), h.pins[0].y - (7L << 16));
				gv.aim = iatan2(h.pins[0].y - gv.ball.y, h.pins[0].x - gv.ball.x);
				for (view = 0; view < 3; view++) {
					if (view == 0) {
						CHECK(draw(&a, &v, &art[0], NULL) == 0);
						CHECK(draw(&b, &v, &art[0], NULL) == 0);
					} else if (view == 1) {
						CHECK(render_map(&a, &v, &ma) == 0);
						CHECK(render_map(&b, &v, &mb) == 0);
						CHECK(mb.bx == 2 * ma.bx && mb.py == 2 * ma.py && mb.left == 2 * ma.left && mb.yards == ma.yards);
					} else {
						CHECK(render_green(&a, &gv) == 0);
						CHECK(render_green(&b, &gv) == 0);
					}
					bad = 0;
					for (y = 0; y < 320; y++)
						for (x = 0; x < 320; x++) bad += pixel(&b, x, y) != pixel(&a, x / 2, y / 2);
					CHECK(bad == 0);
					if (bad) printf("  half %d-bit view %d: %d pixels differ\n", hd[k], view, bad);
				}
				/* the framed map drawn in slices of 7 rows is the same as in one go, at full and half size */
				for (view = 0; view < 2; view++) {
					Surface *o = view ? &b : &a, c = *o;
					MapBox bx;
					int16_t y0 = 0;
					int done = 0;
					size_t n = (size_t)o->rowBytes * o->h;
					bx.x0 = 0;
					bx.y0 = 0;
					bx.x1 = ((fixed)h.width << 16) / 2;
					bx.y1 = ((fixed)h.length << 16) / 2;
					c.bits = malloc(n);
					memset(c.bits, 0x5A, n);
					CHECK(render_map_box(o, &v, &bx, &ma) == 0);
					while (!done && y0 < 1000) {
						done = render_map_rows(&c, &v, &bx, &mb, y0, (int16_t)(y0 + 7));
						CHECK(done >= 0);
						y0 = (int16_t)(y0 + 7);
					}
					CHECK(done == 1 && memcmp(c.bits, o->bits, n) == 0 && ma.ypp == mb.ypp && ma.left == mb.left && ma.top == mb.top);
					free(c.bits);
				}
				sfree(&a);
				sfree(&b);
			}
			/* with the status strip and overlays, at 4, 2 and 8 bits (h4-, h2-, h8-) */
			for (k = 1; k < 4; k++) {
				char t[8];
				surface_d(&s, 2, hd[k]);
				s.half = 1;
				snprintf(t, sizeof t, "h%d-", hd[k]);
				views(&s, t, &v, &art[0], 0);
				sfree(&s);
			}
			surface_wh(&s, 320, 480, 2, 4);
			s.half = 1;
			views(&s, "hx-", &v, &art[0], 0);
			sfree(&s);
			surface_wh(&s, 480, 320, 2, 1);
			s.half = 1;
			views(&s, "hx-", &v, &art[0], 0);
			sfree(&s);
		}
		/* too big or not a multiple of 16 across */
		surface_wh(&s, 328, 320, 2, 8);
		CHECK(render_scene(&s, &v, &art[1], NULL, NULL) == -1);
		sfree(&s);
		surface_wh(&s, 520, 320, 2, 8);
		CHECK(render_scene(&s, &v, &art[1], NULL, NULL) == -1);
		sfree(&s);
	}

	{
		Surface s;
		surface(&s, 1);
		printf("work memory: %u bytes at 160, ", render_work_size(&s));
		sfree(&s);
		surface(&s, 2);
		printf("%u at 320, ", render_work_size(&s));
		sfree(&s);
		surface_wh(&s, 480, 320, 2, 8);
		printf("%u at 480\n", render_work_size(&s));
		sfree(&s);
	}

	art_free(&art[0]);
	art_free(&art[1]);
	free(crec);
	free(hrec);
	free(pdb);
	free(grid);
	printf("render-test: %d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
