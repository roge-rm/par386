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

#define SENTINEL 250    /* a spare palette index the renderer never writes */

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

static void art_load(RenderArt *a, const char *dir, int scale)
{
	int t, k;
	memset(a, 0, sizeof *a);
	for (t = 0; t < RENDER_TYPES; t++)
		for (k = 0; k < RENDER_SIZES; k++)
			a->spr[t][k] = art_file(dir, "Spr8", 1000 + t * 10 + k);
	a->golfer = art_file(dir, "Spr8", 2000 + scale - 1);
	a->backdrop = art_file(dir, "Bkd8", 1000 + scale - 1);
}

static void art_free(RenderArt *a)
{
	int t, k;
	for (t = 0; t < RENDER_TYPES; t++)
		for (k = 0; k < RENDER_SIZES; k++)
			free((void *)a->spr[t][k].p);
	free((void *)a->golfer.p);
	free((void *)a->backdrop.p);
}

static void surface_d(Surface *s, int scale, int depth)
{
	memset(s, 0, sizeof *s);
	s->scale = (uint8_t)scale;
	s->depth = (uint8_t)depth;
	s->w = s->h = (int16_t)(160 * scale);
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	s->bits = malloc((size_t)s->rowBytes * s->h);
	gfx_init(s);
}

static void surface(Surface *s, int scale) { surface_d(s, scale, scale > 1 ? 8 : 2); }

static uint8_t pixel(const Surface *s, int x, int y)
{
	const uint8_t *row = s->bits + (size_t)y * s->rowBytes;
	if (s->depth == 8) return row[x];
	return (uint8_t)((row[x >> 2] >> (6 - 2 * (x & 3))) & 3);
}

static void save(const Surface *s, const char *name)
{
	char path[512];
	FILE *f;
	int x, y;

	snprintf(path, sizeof path, "%s/%s-%d.pgm", outdir, name, s->w);
	f = fopen(path, "wb");
	if (!f) { CHECK(!"open pgm"); return; }
	fprintf(f, "P5\n%d %d\n%d\n", s->w, s->h, s->depth == 8 ? 255 : 3);
	for (y = 0; y < s->h; y++)
		for (x = 0; x < s->w; x++) fputc(pixel(s, x, y), f);
	fclose(f);
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

/* Draws a 160 view at 8 bits only to check it has no gaps. */
static void gap160(const RenderView *v, const RenderArt *a)
{
	Surface s;
	surface_d(&s, 1, 8);
	CHECK(draw(&s, v, a, NULL) == 0);
	free(s.bits);
}

/* Draws a 160 view at another depth, so its pixel writes run under ASan. */
static void depth160(const RenderView *v, const RenderArt *a, int depth)
{
	Surface s;
	surface_d(&s, 1, depth);
	CHECK(draw(&s, v, a, NULL) == 0);
	free(s.bits);
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
	CHECK(art[0].golfer.len && art[1].golfer.len && art[0].backdrop.len && art[1].backdrop.len);
	CHECK(art[0].spr[OBJ_TREE][0].len && art[0].spr[OBJ_PINE][1].len);

	pos[0].x = h.tees[0].pos.x / 65536.0;
	pos[0].y = h.tees[0].pos.y / 65536.0;
	v.hole = &h;
	v.grid = &g;
	v.pin = 0;
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
			free(s.bits);
			free(s2.bits);
		}
	}

	CHECK(dropped == 0);

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
				free(s.bits);
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
			free(s.bits);
		}
		v.hole = &h;
		free(bare);
	}

	/* Bad art: short sprites and a backdrop cut off part way. */
	{
		RenderArt bad = art[0];
		Surface s;
		int t, k;
		for (t = 0; t < RENDER_TYPES; t++) {
			bad.spr[t][0].len = bad.spr[t][0].len ? 13 : 0;
			bad.spr[t][1].len = bad.spr[t][1].len ? 11 : 0;
		}
		bad.golfer.len = 20;
		bad.backdrop.len = bad.backdrop.len / 2;
		/* exact-size copies, so ASan sees any read past the shortened length */
		for (t = 0; t < RENDER_TYPES; t++)
			for (k = 0; k < RENDER_SIZES; k++)
				bad.spr[t][k].p = exact(bad.spr[t][k].p, bad.spr[t][k].len);
		bad.golfer.p = exact(bad.golfer.p, bad.golfer.len);
		bad.backdrop.p = exact(bad.backdrop.p, bad.backdrop.len);
		surface(&s, 1);
		v.ball = h.tees[0].pos;
		v.aim = toward(v.ball, h.aim);
		CHECK(draw(&s, &v, &bad, NULL) == 0);
		free(s.bits);
		for (t = 0; t < RENDER_TYPES; t++)
			for (k = 0; k < RENDER_SIZES; k++) free((void *)bad.spr[t][k].p);
		free((void *)bad.golfer.p);
		free((void *)bad.backdrop.p);
		/* a surface that isn't 160 or 320 wide is turned down */
		surface(&s, 1);
		s.w = 100;
		CHECK(render_scene(&s, &v, &art[0], NULL, NULL) == -1);
		free(s.bits);
	}

	/* The hole map at 160 2-bit and 320 8-bit, then the status strip and the map labels. */
	{
		Play pl;
		Surface s;
		int unwritten;
		play_init(&pl, &h, &g);
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
			free(s.bits);
		}
		surface(&s, 2);
		play_status(&s, &pl, NULL);
		play_map(&s, &pl);
		free(s.bits);
	}

	{
		Surface s;
		surface(&s, 1);
		printf("work memory: %u bytes at 160, ", render_work_size(&s));
		free(s.bits);
		surface(&s, 2);
		printf("%u at 320\n", render_work_size(&s));
		free(s.bits);
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
