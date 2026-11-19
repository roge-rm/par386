#include "hole.h"

/* Record data is big-endian and read a byte at a time, so it works on any
 * CPU and alignment. A reader stops at the end of the buffer and remembers it. */
typedef struct {
	const uint8_t *p;
	uint32_t len, pos;
	int err;
} Reader;

static uint8_t SEC_CORE rd8(Reader *r)
{
	if (r->pos + 1 > r->len) {
		r->err = HOLE_ERR_SHORT;
		return 0;
	}
	return r->p[r->pos++];
}

static uint16_t SEC_CORE rd16(Reader *r)
{
	uint16_t v;
	if (r->pos + 2 > r->len) {
		r->err = HOLE_ERR_SHORT;
		return 0;
	}
	v = (uint16_t)((r->p[r->pos] << 8) | r->p[r->pos + 1]);
	r->pos += 2;
	return v;
}

/* Skips n bytes and returns where they start, or 0 if they aren't all there. */
static const uint8_t *SEC_CORE rdskip(Reader *r, uint32_t n)
{
	const uint8_t *s;
	if (r->err || n > r->len - r->pos) {
		r->err = HOLE_ERR_SHORT;
		return 0;
	}
	s = r->p + r->pos;
	r->pos += n;
	return s;
}

/* A length byte then that many text bytes, copied with a closing nul. */
static int SEC_CORE rdname(Reader *r, char *out, uint8_t min, uint8_t max)
{
	uint8_t n = rd8(r), i;
	const uint8_t *s;

	if (r->err) return 0;
	if (n < min || n > max) return 0;
	s = rdskip(r, n);
	if (!s) return 0;
	for (i = 0; i < n; i++) {
		if (s[i] < 0x20 || s[i] == 0x7F) return 0;
		out[i] = (char)s[i];
	}
	out[n] = 0;
	return 1;
}

/* Quarter yards to 16.16 yards. */
#define QY(v) ((fixed)(v) << 14)

static int SEC_CORE finish(Reader *r, int ok)
{
	if (r->err) return r->err;
	if (!ok) return HOLE_ERR_VALUE;
	if (r->pos != r->len) return HOLE_ERR_LONG;
	return HOLE_OK;
}

int course_parse(Course *c, const uint8_t *buf, uint32_t len)
{
	Reader r;
	uint8_t i;
	int ok = 1;
	uint16_t v;

	r.p = buf;
	r.len = len;
	r.pos = 0;
	r.err = 0;
	v = rd16(&r);
	if (v < 1 || v > COURSE_VERSION) return r.err ? r.err : HOLE_ERR_VERSION;
	c->hole_count = rd8(&r);
	c->tee_count = rd8(&r);
	if (c->hole_count < 1 || c->hole_count > COURSE_MAX_HOLES) ok = 0;
	if (c->tee_count < 1 || c->tee_count > COURSE_MAX_TEES) ok = 0;
	if (ok && !rdname(&r, c->name, 1, HOLE_NAME_MAX)) ok = 0;
	for (i = 0; ok && i < c->tee_count; i++)
		if (!rdname(&r, c->tee_names[i], 1, TEE_NAME_MAX)) ok = 0;
	for (i = 0; ok && i < c->hole_count; i++) {
		c->par[i] = rd8(&r);
		c->yards[i] = rd16(&r);
		if (c->par[i] < 3 || c->par[i] > 6) ok = 0;
	}
	c->scenery = 0;
	c->wind = 0;
	for (i = 0; i < COURSE_MAX_HOLES; i++) c->turn[i] = 0;
	if (ok && v >= 2) {
		c->scenery = rd8(&r);
		for (i = 0; i < c->hole_count; i++) c->turn[i] = rd8(&r);
	}
	if (ok && v >= 3) {
		c->wind = rd8(&r);
		if (c->wind > 40) ok = 0;
	}
	return finish(&r, ok);
}

/* Reads a point in quarter yards, checking it's on the hole. */
static vec2 SEC_CORE rdpoint(Reader *r, const Hole *h, int *ok)
{
	uint16_t x = rd16(r), y = rd16(r);
	if (x > h->width * 4 || y > h->length * 4) *ok = 0;
	return vec2_make(QY(x), QY(y));
}

int hole_parse(Hole *h, const uint8_t *buf, uint32_t len)
{
	Reader r;
	uint16_t i, j, n;
	int ok = 1;

	r.p = buf;
	r.len = len;
	r.pos = 0;
	r.err = 0;
	if (rd16(&r) != HOLE_VERSION) return r.err ? r.err : HOLE_ERR_VERSION;
	h->turn = 0;
	h->wind = 0;
	h->number = rd8(&r);
	h->par = rd8(&r);
	h->width = rd16(&r);
	h->length = rd16(&r);
	h->hm_w = rd8(&r);
	h->hm_h = rd8(&r);
	h->hm_shift = rd8(&r);
	h->base_surface = rd8(&r);
	h->base = (fixed)(int16_t)rd16(&r) * 4096;   /* 1/16 yards */
	h->step = (fixed)rd16(&r) << 4;              /* 1/4096 yards */
	if (r.err) return r.err;
	if (h->number < 1 || h->par < 3 || h->par > 6) ok = 0;
	if (h->width < 16 || h->width > HOLE_MAX_WIDTH) ok = 0;
	if (h->length < 16 || h->length > HOLE_MAX_LENGTH) ok = 0;
	if (h->hm_shift < 1 || h->hm_shift > 5 || h->base_surface >= SURF_COUNT) ok = 0;
	if (h->hm_w < 2 || h->hm_w > HOLE_MAX_HM_W || h->hm_h < 2 || h->hm_h > HOLE_MAX_HM_H) ok = 0;
	if (ok && (((uint16_t)(h->hm_w - 1) << h->hm_shift) < h->width
	           || ((uint16_t)(h->hm_h - 1) << h->hm_shift) < h->length)) ok = 0;
	if (!ok) return HOLE_ERR_VALUE;
	h->aim = rdpoint(&r, h, &ok);
	if (!rdname(&r, h->name, 0, HOLE_NAME_MAX)) ok = 0;

	h->tee_count = rd8(&r);
	if (h->tee_count < 1 || h->tee_count > COURSE_MAX_TEES) ok = 0;
	for (i = 0; ok && i < h->tee_count; i++) {
		h->tees[i].set = rd8(&r);
		h->tees[i].pos = rdpoint(&r, h, &ok);
		h->tees[i].yards = rd16(&r);
		if (h->tees[i].set >= COURSE_MAX_TEES) ok = 0;
	}
	for (i = 0; ok && i < HOLE_PINS; i++)
		h->pins[i] = rdpoint(&r, h, &ok);
	if (!ok) return finish(&r, ok);

	h->heights = rdskip(&r, (uint32_t)h->hm_w * h->hm_h);

	h->shape_count = rd8(&r);
	if (h->shape_count > HOLE_MAX_SHAPES) ok = 0;
	h->shapes = r.p + r.pos;
	for (i = 0; ok && !r.err && i < h->shape_count; i++) {
		if (rd8(&r) >= SURF_COUNT) ok = 0;
		n = rd8(&r);
		if (n < 3 || n > HOLE_MAX_VERTS) ok = 0;
		for (j = 0; ok && !r.err && j < n; j++)
			rdpoint(&r, h, &ok);
	}

	h->object_count = rd16(&r);
	if (h->object_count > HOLE_MAX_OBJECTS) ok = 0;
	h->objects = r.p + r.pos;
	for (i = 0; ok && !r.err && i < h->object_count; i++) {
		if (rd8(&r) == 0) ok = 0;
		rd8(&r);
		rd8(&r);
		rdpoint(&r, h, &ok);
	}
	return finish(&r, ok);
}

/* Big-endian 16 bits from data already checked by hole_parse. */
#define GET16(p) ((uint16_t)(((p)[0] << 8) | (p)[1]))

void hole_object(const Hole *h, uint16_t i, HoleObject *o)
{
	const uint8_t *p = h->objects + (uint32_t)i * 7;
	o->type = p[0];
	o->variant = p[1];
	o->scale = p[2];
	o->pos = vec2_make(QY(GET16(p + 3)), QY(GET16(p + 5)));
}

/* Clamps a 16.16 grid coordinate to 0..cells-1 and splits it into a cell and an 8 bit fraction. */
static uint16_t SEC_CORE grid_pos(fixed g, uint8_t cells, uint16_t *frac)
{
	fixed top = (fixed)(cells - 1) << 16;
	if (g <= 0) g = 0;
	if (g >= top) g = top;
	*frac = (uint16_t)((g >> 8) & 255);
	return (uint16_t)(g >> 16);
}

fixed hole_height(const Hole *h, fixed x, fixed y)
{
	uint16_t fx, fy, i, j, i1, j1, hw = h->hm_w;    /* 16 bits, or MULU16 isn't one mulu.w */
	const uint8_t *r0, *r1;
	int32_t t, b, v;

	i = grid_pos(x >> h->hm_shift, h->hm_w, &fx);
	j = grid_pos(y >> h->hm_shift, h->hm_h, &fy);
	i1 = i + 1 < h->hm_w ? i + 1 : i;
	j1 = j + 1 < h->hm_h ? j + 1 : j;
	r0 = h->heights + MULU16(j, hw);
	r1 = h->heights + MULU16(j1, hw);
	/* Bytes are 0..255, so t and b are 8.8 and v is 8.16. */
	t = ((int32_t)r0[i] << 8) + MULS16(r0[i1] - r0[i], fx);
	b = ((int32_t)r1[i] << 8) + MULS16(r1[i1] - r1[i], fx);
	v = (t << 8) + (b - t) * (int32_t)fy;
	return h->base + fix_mulf(v, h->step);
}

void surface_size(const Hole *h, SurfaceGrid *g)
{
	g->w = (h->width + 1) >> SURF_CELL_SHIFT;
	g->h = (h->length + 1) >> SURF_CELL_SHIFT;
	g->stride = (g->w + 1) >> 1;
}

static void SEC_CORE set_cell(uint8_t *row, uint16_t col, uint8_t t)
{
	uint8_t *b = row + (col >> 1);
	if (col & 1) *b = (uint8_t)((*b & 0xF0) | t);
	else *b = (uint8_t)((*b & 0x0F) | (t << 4));
}

void surface_raster(const Hole *h, uint8_t *row, SurfaceRowFn put, void *ctx)
{
	SurfaceGrid g;
	int16_t xs[HOLE_MAX_VERTS];
	uint16_t r, i, j, k, n, col, end;
	uint8_t fill = (uint8_t)(h->base_surface * 0x11);

	surface_size(h, &g);
	for (r = 0; r < g.h; r++) {
		/* Sample at the cell centres, in quarter yards: 2 yard cells are 8 apart. */
		int32_t sy = (int32_t)r * 8 + 4;
		const uint8_t *p = h->shapes;

		for (i = 0; i < g.stride; i++) row[i] = fill;
		for (i = 0; i < h->shape_count; i++) {
			uint8_t type = p[0];
			const uint8_t *v = p + 2;
			n = p[1];
			p += 2 + (uint16_t)n * 4;

			/* Where each edge crosses the row, counting an edge's low end only. */
			k = 0;
			for (j = 0; j < n; j++) {
				const uint8_t *a = v + j * 4, *b = v + ((j + 1 == n) ? 0 : j + 1) * 4;
				int32_t ax = GET16(a), ay = GET16(a + 2), bx = GET16(b), by = GET16(b + 2), x;
				if ((ay <= sy) == (by <= sy)) continue;
				x = ax + (sy - ay) * (bx - ax) / (by - ay);
				/* insertion sort, n is small */
				{
					uint16_t m = k++;
					while (m > 0 && xs[m - 1] > x) {
						xs[m] = xs[m - 1];
						m--;
					}
					xs[m] = (int16_t)x;
				}
			}
			/* Fill cells whose centre is inside each pair: xa <= col * 8 + 4 < xb. */
			for (j = 0; j + 1 < k; j += 2) {
				col = (uint16_t)((xs[j] + 3) >> 3);
				end = (uint16_t)((xs[j + 1] + 3) >> 3);
				if (end > g.w) end = g.w;
				for (; col < end; col++) set_cell(row, col, type);
			}
		}
		put(ctx, r, row, g.stride);
	}
}

uint8_t surface_at(const SurfaceGrid *g, fixed x, fixed y)
{
	int32_t cx = x >> (16 + SURF_CELL_SHIFT), cy = y >> (16 + SURF_CELL_SHIFT);
	uint8_t b, s, off = 0;

	/* past the edge it's out of bounds, unless the edge is water: the sea carries on */
	if (x < 0) { cx = 0; off = 1; }
	if (y < 0) { cy = 0; off = 1; }
	if (cx >= g->w) { cx = g->w - 1; off = 1; }
	if (cy >= g->h) { cy = g->h - 1; off = 1; }
	b = g->cells[MULU16(cy, g->stride) + (cx >> 1)];
	s = (cx & 1) ? (b & 15) : (b >> 4);
	if (off && s != SURF_WATER) return SURF_OOB;
	return s;
}
