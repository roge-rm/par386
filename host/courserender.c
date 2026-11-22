/* Draws every hole of a course for looking at. Run by `make course-render`:
 *   courserender COURSE.pdb ARTDIR OUTDIR [HOLE]
 * Per hole: the tee view, the landing area (the aim point, looking at the
 * pin), near the green (40 yd short of the pin) and the hole map, at
 * 160x160 2-bit and 320x320 8-bit, as PGMs of pixel values (tools/refcmp.py
 * makes PNGs of them). Checks each view has no unwritten pixel, that the
 * pins are on the green and the tees on a tee, and prints the work counts
 * of each tee view. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"
#include "pal.h"

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

static uint8_t *pdb_record(const uint8_t *d, uint32_t n, int k, uint32_t *len)
{
	int count = d[76] << 8 | d[77];
	uint32_t a, b;
	uint8_t *r;

	if (k < 0 || k >= count || 78 + 8 * (uint32_t)count > n) return NULL;
	a = be32(d + 78 + 8 * k);
	b = k + 1 < count ? be32(d + 78 + 8 * (k + 1)) : n;
	if (a < 78 + 8 * (uint32_t)count || a >= b || b > n) return NULL;
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

static void art_load(RenderArt *a, const char *dir, int scale, int scenery)
{
	int t, v, k;
	RenderGolfer *g = calloc(1, sizeof *g);
	memset(a, 0, sizeof *a);
	/* the course style's sprites, else the shared ones, as main.c picks them */
	for (t = 0; t < RENDER_TYPES; t++)
		for (v = 0; v < RENDER_VARIANTS; v++)
			for (k = 0; k < RENDER_SIZES; k++) {
				if (scenery) a->spr[t][v][k] = art_file(dir, "Spr8", 3000 + 200 * scenery + 10 * t + 2 * v + k);
				if (!a->spr[t][v][k].p && !v) a->spr[t][v][k] = art_file(dir, "Spr8", 1000 + t * 10 + k);
			}
	for (k = 0; k < RENDER_POSES; k++) g->pose[k] = art_file(dir, "Spr8", 2000 + 10 * k + scale - 1);
	a->golfer = g;
	a->backdrop = art_file(dir, "Bkd8", 1000 + 10 * scenery + scale - 1);
}

static void surface(Surface *s, int scale)
{
	memset(s, 0, sizeof *s);
	s->scale = (uint8_t)scale;
	s->depth = scale > 1 ? 8 : 2;
	s->w = s->h = (int16_t)(160 * scale);
	s->rowBytes = (uint16_t)((s->w * s->depth + 15) / 16 * 2);
	s->bits = malloc((size_t)s->rowBytes * s->h);
	gfx_init(s);
}

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

static angle toward(vec2 from, vec2 to) { return iatan2(to.y - from.y, to.x - from.x); }

int main(int argc, char **argv)
{
	uint8_t *pdb, *crec, *hrec, *manifest;
	uint32_t n, clen, hlen, manifest_len = 0, total = 0;
	Course c;
	Hole h;
	SurfaceGrid g;
	uint8_t row[SURF_ROW_MAX];
	RenderArt art[2];
	RenderView v = {0};
	RenderStats st;
	int k, i, scale, first = 1, last, yards = 0, par = 0;
	uint16_t records;

	if (argc != 4 && argc != 5) {
		printf("usage: courserender COURSE.pdb ARTDIR OUTDIR [HOLE]\n");
		return 2;
	}
	outdir = argv[3];
	pdb = load_file(argv[1], &n);
	if (!pdb || n < 78) { printf("FAIL can't read %s\n", argv[1]); return 1; }
	crec = pdb_record(pdb, n, 0, &clen);
	CHECK(crec && course_parse(&c, crec, clen) == HOLE_OK);
	if (fails) return 1;
	last = c.hole_count;
	if (argc == 5) {
		char *end;
		long wanted = strtol(argv[4], &end, 10);
		if (*end || wanted < 1 || wanted > last) {
			fputs("hole number out of range\n", stderr);
			return 2;
		}
		first = last = (int)wanted;
	}
	records = (uint16_t)((pdb[76] << 8) | pdb[77]);
	manifest = pdb_record(pdb, n, c.hole_count + 1, &manifest_len);
	art_load(&art[0], argv[2], 1, c.scenery);
	art_load(&art[1], argv[2], 2, c.scenery);
	CHECK(art[0].backdrop.len && art[1].backdrop.len);
	printf("%s: %d holes, scenery %d\n", c.name, c.hole_count, c.scenery);

	for (k = first; k <= last; k++) {
		vec2 spot[3];
		uint8_t *mesh[2] = {0};
		TerrainMesh quality[2] = {{0}};
		uint32_t mesh_len;
		uint16_t mesh_index;
		static const char *names[3] = { "tee", "landing", "green" };
		hrec = pdb_record(pdb, n, k, &hlen);
		CHECK(hrec && hole_parse(&h, hrec, hlen) == HOLE_OK);
		if (!hrec || fails) return 1;
		for (scale = 1; scale <= 2; scale++) {
			mesh_index = course_terrain_record(manifest, manifest_len, c.hole_count, records,
			                                   (uint16_t)k, scale == 1);
			if (!mesh_index) continue;
			mesh[scale - 1] = pdb_record(pdb, n, mesh_index, &mesh_len);
			CHECK(mesh[scale - 1] && terrain_mesh_parse(&quality[scale - 1], &h, mesh[scale - 1], mesh_len));
		}
		h.turn = (uint16_t)(c.turn[k - 1] << 8);
		total += hlen;
		yards += c.yards[k - 1];
		par += c.par[k - 1];
		surface_size(&h, &g);
		grid_stride = g.stride;
		grid = malloc((size_t)g.stride * g.h);
		g.cells = grid;
		surface_raster(&h, row, put_row, &g);
		for (i = 0; i < HOLE_PINS; i++)
			CHECK(surface_at(&g, h.pins[i].x, h.pins[i].y) == SURF_GREEN);
		for (i = 0; i < h.tee_count; i++)
			CHECK(surface_at(&g, h.tees[i].pos.x, h.tees[i].pos.y) == SURF_TEE);
		printf("hole %2d %-16s par %d %3u yd, record %u bytes, %u objects, %u shapes, heightmap %ux%u\n",
		       k, h.name, h.par, h.tees[0].yards, hlen, h.object_count, h.shape_count, h.hm_w, h.hm_h);

		spot[0] = h.tees[0].pos;
		spot[1] = h.aim;
		{
			/* 40 yards short of pin 0 on the line from the aim point */
			vec2 p = h.pins[0], a = h.par == 3 ? h.tees[0].pos : h.aim;
			double dx = (p.x - a.x) / 65536.0, dy = (p.y - a.y) / 65536.0, d = sqrt(dx * dx + dy * dy);
			spot[2] = vec2_make(p.x - (fixed)(dx * 40 / d * 65536), p.y - (fixed)(dy * 40 / d * 65536));
		}
		v.hole = &h;
		v.grid = &g;
		v.pin = 0;
		v.green = 0;
		for (i = 0; i < 3; i++) {
			char name[64];
			if (i == 1 && h.par == 3) continue;
			v.ball = spot[i];
			v.aim = toward(v.ball, i == 0 ? h.aim : h.pins[0]);
			if (i == 0 && h.par == 3) v.aim = toward(v.ball, h.pins[0]);
			for (scale = 1; scale <= 2; scale++) {
				Surface s;
				uint32_t visual, wn;
				uint8_t *work;
				int x, y, left = 0;
				surface(&s, scale);
				h.terrain_mesh = quality[scale - 1];
				visual = h.terrain_mesh.data ? render_scene_visual_size(&s, &v) : 0;
				wn = visual ? visual : render_work_size(&s);
				work = malloc(wn);
				memset(s.bits, s.depth == 8 ? SENTINEL : 0x55, (size_t)s.rowBytes * s.h);
				memset(&st, 0, sizeof st);
				CHECK((visual ? render_scene_visual(&s, &v, &art[scale - 1], work, &st) :
				                render_scene(&s, &v, &art[scale - 1], work, &st)) == 0);
				free(work);
				if (s.depth == 8)
					for (y = 0; y < s.h; y++)
						for (x = 0; x < s.w; x++)
							if (pixel(&s, x, y) == SENTINEL) left++;
				CHECK(left == 0);
				if (scale == 1)
					printf("  %-7s %u steps, %u spans, %u terrain px, %u objects, %u sprite px, %u dropped\n",
					       names[i], st.steps, st.spans, st.pixels, st.objects, st.sprite_px, st.dropped);
				snprintf(name, sizeof name, "h%02d-%s", k, names[i]);
				save(&s, name);
				free(s.bits);
			}
		}
		for (scale = 1; scale <= 2; scale++) {
			Surface s;
			RenderMap m;
			char name[64];
			surface(&s, scale);
			v.ball = h.tees[0].pos;
			CHECK(render_map(&s, &v, &m) == 0);
			snprintf(name, sizeof name, "h%02d-map", k);
			save(&s, name);
			free(s.bits);
		}
		free(grid);
		free(mesh[0]);
		free(mesh[1]);
		free(hrec);
	}
	printf("par %d, %d yd, hole records %u bytes, course %u bytes\n", par, yards, total, total + clen);
	for (scale = 0; scale < 2; scale++) {
		int t, v, z;
		for (t = 0; t < RENDER_TYPES; t++)
			for (v = 0; v < RENDER_VARIANTS; v++)
				for (z = 0; z < RENDER_SIZES; z++)
					free((void *)art[scale].spr[t][v][z].p);
		for (z = 0; z < RENDER_POSES; z++) free((void *)art[scale].golfer->pose[z].p);
		free(art[scale].golfer);
		free((void *)art[scale].backdrop.p);
	}
	free(crec);
	free(manifest);
	free(pdb);
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
