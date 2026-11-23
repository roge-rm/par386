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

/* Big-endian 16 bits from data with checked length. */
#define GET16(p) ((uint16_t)(((p)[0] << 8) | (p)[1]))

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
	c->green = 0;
	for (i = 0; i < COURSE_MAX_HOLES; i++) c->turn[i] = 0;
	if (ok && v >= 2) {
		c->scenery = rd8(&r);
		for (i = 0; i < c->hole_count; i++) c->turn[i] = rd8(&r);
	}
	if (ok && v >= 3) {
		c->wind = rd8(&r);
		if (c->wind > 40) ok = 0;
	}
	if (ok && v >= 4) {
		c->green = rd8(&r);
		if (c->green && (c->green < 6 || c->green > 14)) ok = 0;
	}
	return finish(&r, ok);
}

uint16_t course_terrain_record(const uint8_t *buf, uint32_t len, uint8_t holes,
                                uint16_t records, uint16_t hole, int kind)
{
	uint16_t i, stride, detailed, small, grid, index = 0, next = (uint16_t)(holes + 2);
	int has_compact = 0;

	if (!buf || !hole || hole > holes || holes > COURSE_MAX_HOLES ||
	    len < 4 || records <= next ||
	    buf[0] != 'M' || buf[1] != 'S' || buf[2] != 'H') return 0;
	if (buf[3] == '1') stride = 2;
	else if (buf[3] == '2') stride = 4;
	else if (buf[3] == '3') stride = 6;
	else return 0;
	if (len != 4 + (uint32_t)stride * holes) return 0;
	for (i = 1, buf += 4; i <= holes; i++, buf += stride) {
		detailed = GET16(buf);
		small = stride >= 4 ? GET16(buf + 2) : 0;
		grid = stride == 6 ? GET16(buf + 4) : 0;
		if (detailed && detailed != next++) return 0;
		if (small) {
			if (!detailed || small != next++) return 0;
			has_compact = 1;
		}
		if (stride == 6 && grid != next++) return 0;
		if (i == hole) {
			if (kind == COURSE_TERRAIN_GRID) index = grid;
			else index = kind == COURSE_TERRAIN_COMPACT && small ? small : detailed;
		}
	}
	return next == records && (stride != 4 || has_compact) ? index : 0;
}

void hole_use_tee(Hole *h, uint8_t set)
{
	uint8_t i, best = 0xFF;
	for (i = 0; i < h->tee_count; i++)
		if (h->tees[i].set <= set && (best == 0xFF || h->tees[i].set > h->tees[best].set)) best = i;
	h->tee = best == 0xFF ? 0 : best;
}

fixed hole_putt_len(const Hole *h, const SurfaceGrid *g, vec2 a, vec2 b, fixed len)
{
	fixed dx = (b.x - a.x) >> 4, dy = (b.y - a.y) >> 4, on;
	uint8_t k, n = 0;

	if (!h->green || h->green == GREEN_USUAL) return len;
	for (k = 1; k < 16; k += 2)
		if (hole_surface(h, g, a.x + dx * k, a.y + dy * k) == SURF_GREEN) n++;
	on = (len >> 3) * n;
	return len - on + on / h->green * GREEN_USUAL;
}

/* Reads a point in quarter yards, checking it's on the hole. */
static vec2 SEC_CORE rdpoint(Reader *r, const Hole *h, int *ok)
{
	uint16_t x = rd16(r), y = rd16(r);
	if (x > h->width * 4 || y > h->length * 4) *ok = 0;
	return vec2_make(QY(x), QY(y));
}

static int SEC_CORE read_green_mesh(Reader *r, Hole *h)
{
	const uint8_t *mesh = r->p + r->pos, *rows, *runs;
	uint16_t x = rd16(r), y = rd16(r), w = rd16(r), height = rd16(r);
	uint16_t size, row, start = 0;

	if (r->err || !w || !height || x >= h->width * 4 || y >= h->length * 4 ||
	    w > h->width * 4 - x || height > h->length * 4 - y) return 0;
	rows = rdskip(r, ((uint32_t)height + 1) * 2);
	if (!rows) return 0;
	size = GET16(rows + height * 2);
	runs = rdskip(r, size);
	if (!runs || GET16(rows)) return 0;
	for (row = 0; row < height; row++) {
		uint16_t stop = GET16(rows + (row + 1) * 2), column = 0;
		const uint8_t *p, *end;

		if (stop > size || stop <= start || (stop & 1)) return 0;
		p = runs + start;
		end = runs + stop;
		for (; p < end; p += 2) {
			uint16_t run = GET16(p), next = run >> 4;
			if (next <= column || next > w || (run & 15) >= SURF_COUNT) return 0;
			column = next;
		}
		if (column != w) return 0;
		start = stop;
	}
	h->green_mesh = mesh;
	return 1;
}

static int SEC_DANA read_green_geometry(Reader *r, Hole *h)
{
	const uint8_t *data = r->p + r->pos, *p;
	uint16_t nv = rd16(r), nt = rd16(r), i;
	if (!nv || nv > GREEN_VERT_MAX || !nt || nt > GREEN_TRI_MAX) return 0;
	p = rdskip(r, (uint32_t)nv * 6 + (uint32_t)nt * 7);
	if (!p) return 0;
	for (i = 0; i < nv; i++, p += 6)
		if (GET16(p) > h->width * 16 || GET16(p + 2) > h->length * 16 || GET16(p + 4) > 65280U) return 0;
	for (i = 0; i < nt; i++, p += 7)
		if (GET16(p) >= nv || GET16(p + 2) >= nv || GET16(p + 4) >= nv || p[6] >= SURF_COUNT) return 0;
	h->green_geometry = data;
	return 1;
}

static int SEC_DANA read_visual_shapes(Reader *r, Hole *h)
{
	const uint8_t *data = r->p + r->pos, *p;
	uint8_t count = rd8(r);
	uint16_t remaining = VISUAL_VERT_MAX, n, i;
	Reader mesh;

	if (!count || count > VISUAL_SHAPE_MAX) return 0;
	while (count--) {
		if (rd8(r) >= SURF_COUNT) return 0;
		n = rd16(r);
		if (n < 3 || n > remaining) return 0;
		remaining -= n;
		p = rdskip(r, (uint32_t)n * 4);
		if (!p) return 0;
		for (i = 0; i < n; i++, p += 4)
			if (GET16(p) > h->width * 16 || GET16(p + 2) > h->length * 16) return 0;
	}
	n = rd16(r);
	if (r->err) return 0;
	if (n) {
		mesh.p = rdskip(r, n);
		mesh.len = n;
		mesh.pos = 0;
		mesh.err = 0;
		if (!mesh.p || !read_green_mesh(&mesh, h) || finish(&mesh, 1) != HOLE_OK) return 0;
	}
	h->visual_shapes = data;
	return 1;
}

static int SEC_DANA read_sand_atlas(Reader *r, Hole *h)
{
	const uint8_t *atlas = r->p + r->pos, *shape = h->visual_shapes + 1;
	uint8_t count = rd8(r), shapes = h->visual_shapes[0];

	if (!count || count > shapes || h->base_surface == SURF_BUNKER || r->len > 16384UL) return 0;
	while (shapes--) {
		uint16_t n = GET16(shape + 1);
		if (shape[0] == SURF_BUNKER) {
			const uint8_t *p;
			uint16_t x = rd16(r), y = rd16(r), w = rd16(r), height = rd16(r), k;
			uint32_t cells;
			if (r->err || !count || x >= h->width || y >= h->length || w < 2 || height < 2 ||
			    w - 1 > h->width - x || height - 1 > h->length - y) return 0;
			count--;
			for (k = 0, p = shape + 3; k < n; k++, p += 4)
				if (GET16(p) < x * 16 || GET16(p) > (x + w - 1) * 16 ||
				    GET16(p + 2) < y * 16 || GET16(p + 2) > (y + height - 1) * 16) return 0;
			cells = (uint32_t)w * height;
			p = rdskip(r, cells * 2);
			if (!p) return 0;
			for (; cells; cells--, p += 2) {
				int8_t distance = (int8_t)p[0];
				if (distance < -64 || distance > 64 || (p[1] >> 4) == 8 || (p[1] & 15) == 8) return 0;
			}
		}
		shape += 3 + 4 * n;
	}
	if (count) return 0;
	h->sand_atlas = atlas;
	return 1;
}

static void SEC_CORE bounds_add(SurfaceBounds *b, uint16_t x, uint16_t y)
{
	if (x < b->x0) b->x0 = x;
	if (y < b->y0) b->y0 = y;
	if (x > b->x1) b->x1 = x;
	if (y > b->y1) b->y1 = y;
}

static inline int SEC_DANA hazard_surface(uint8_t surface)
{
	return surface == SURF_BUNKER || surface == SURF_WATER;
}

static int SEC_DANA read_putting_curves(Reader *r, Hole *h)
{
	const uint8_t *curves = r->p + r->pos, *shape = h->shapes;
	SurfaceBounds green = {UINT16_MAX, UINT16_MAX, 0, 0};
	SurfaceBounds hazards = green;
	uint16_t total = 0, next = 0;
	uint8_t count = rd8(r), i;
	int hazard_base = hazard_surface(h->base_surface);
	if (!count || count > h->shape_count || r->len > 16384) return 0;
	for (i = 0; i < count && !r->err; i++) {
		SurfaceBounds bounds = {UINT16_MAX, UINT16_MAX, 0, 0};
		uint8_t index = rd8(r);
		uint16_t n = rd16(r), j;
		if (index < next || index >= h->shape_count || n < 3 || n > HOLE_CURVE_VERT_MAX ||
		    n + total > HOLE_CURVE_VERT_TOTAL) return 0;
		while (next < index) {
			shape += 2 + (uint16_t)shape[1] * 4;
			next++;
		}
		if (shape[0] < SURF_GREEN || shape[0] > SURF_WATER) return 0;
		shape += 2 + (uint16_t)shape[1] * 4;
		next++;
		total += n;
		for (j = 0; j < n; j++) {
			uint16_t x = rd16(r), y = rd16(r);
			if (x > (h->width << 4) || y > (h->length << 4)) return 0;
			bounds_add(&bounds, x >> (4 + SURF_CELL_SHIFT), y >> (4 + SURF_CELL_SHIFT));
		}
		h->shape_bounds[index] = bounds;
	}
	h->hazard_count = 0;
	for (i = 0, shape = h->shapes; i < h->shape_count; i++, shape += 2 + (uint16_t)shape[1] * 4) {
		const SurfaceBounds *b = &h->shape_bounds[i];
		if (shape[0] == SURF_GREEN || shape[0] == SURF_FRINGE) {
			bounds_add(&green, b->x0, b->y0);
			bounds_add(&green, b->x1, b->y1);
		}
		/* On a sand or water base, the land shapes can define a bank too. */
		if (hazard_base || hazard_surface(shape[0])) {
			h->hazard_shapes[h->hazard_count++] = i;
			bounds_add(&hazards, b->x0, b->y0);
			bounds_add(&hazards, b->x1, b->y1);
		}
	}
	h->green_bounds = green;
	h->hazard_bounds = hazards;
	h->putting_curves = curves;
	return 1;
}

int hole_parse(Hole *h, const uint8_t *buf, uint32_t len)
{
	Reader r;
	uint16_t i, j, n, version;
	int ok = 1;

	r.p = buf;
	r.len = len;
	r.pos = 0;
	r.err = 0;
	version = rd16(&r);
	if (version < 1 || version > HOLE_VERSION) return r.err ? r.err : HOLE_ERR_VERSION;
	h->green_mesh = 0;
	h->green_geometry = 0;
	h->visual_shapes = 0;
	h->sand_atlas = 0;
	h->putting_curves = 0;
	h->terrain_mesh.data = 0;
	h->terrain_mesh.tiles = h->terrain_mesh.vertices = h->terrain_mesh.triangles = 0;
	h->terrain_mesh.mow_phase = 0;
	h->turn = 0;
	h->wind = 0;
	h->green = 0;
	h->tee = 0;
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
		SurfaceBounds bounds = {UINT16_MAX, UINT16_MAX, 0, 0};
		if (rd8(&r) >= SURF_COUNT) ok = 0;
		n = rd8(&r);
		if (n < 3 || n > HOLE_MAX_VERTS) ok = 0;
		for (j = 0; ok && !r.err && j < n; j++) {
			vec2 point = rdpoint(&r, h, &ok);
			if (version == 6)
				bounds_add(&bounds, point.x >> (16 + SURF_CELL_SHIFT), point.y >> (16 + SURF_CELL_SHIFT));
		}
		if (version == 6) h->shape_bounds[i] = bounds;
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
	if (ok && !r.err && (version == 4 || version == 5)) ok = read_visual_shapes(&r, h);
	if (ok && !r.err && version == 5) ok = read_sand_atlas(&r, h);
	if (ok && !r.err && (version == 2 || version == 3)) ok = read_green_mesh(&r, h);
	if (ok && !r.err && version == 3) ok = read_green_geometry(&r, h);
	if (ok && !r.err && version == 6) ok = read_putting_curves(&r, h);
	return finish(&r, ok);
}

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

typedef struct {
	uint16_t offset, low, high;   /* source and quarter-yard y bounds */
} ShapeRows;

/* Ordinary shape offsets fit in 15 bits. Curves use 13 bits plus their surface. */
#define CURVE_OFFSET 0x8000
#define CURVE_SURFACE_SHIFT 13
#define CURVE_OFFSET_MASK 0x1fff

static void SEC_DANA shape_rows(const Hole *h, ShapeRows *rows)
{
	const uint8_t *p = h->shapes, *curve = h->putting_curves;
	uint8_t left = curve ? *curve++ : 0;
	uint16_t i;

	for (i = 0; i < h->shape_count; i++) {
		const uint8_t *v = p + 2, *end = v + (uint16_t)p[1] * 4;
		uint16_t low, high, unit_shift = 0, type = p[0];
		rows[i].offset = (uint16_t)(p - h->shapes);
		p = end;
		if (left && *curve == i) {
			uint16_t count = GET16(curve + 1);
			rows[i].offset = (uint16_t)(CURVE_OFFSET | ((type - SURF_GREEN) << CURVE_SURFACE_SHIFT) |
			                            (curve - h->putting_curves));
			v = curve + 3;
			curve = end = v + count * 4;
			left--;
			unit_shift = 2;
		}
		low = high = GET16(v + 2);
		for (v += 4; v < end; v += 4) {
			uint16_t y = GET16(v + 2);
			if (y < low) low = y;
			if (y > high) high = y;
		}
		rows[i].low = (uint16_t)((low + ((1 << unit_shift) - 1)) >> unit_shift);
		rows[i].high = (uint16_t)((high + ((1 << unit_shift) - 1)) >> unit_shift);
	}
}

static inline int16_t SEC_DANA edge_x(const uint8_t *a, const uint8_t *b,
                                     int16_t ay, int16_t by, int16_t sy, int ceil_x)
{
	int16_t ax = GET16(a), dx = (int16_t)GET16(b) - ax, dy = by - ay;
	int32_t product = MULS16(sy - ay, dx), delta = product;

#ifdef __m68k__
	/* A crossing lies on the edge. The quotient fits in the width of the hole. */
	__asm__ ("divs.w %1,%0" : "+d" (delta) : "d" (dy) : "cc");
#else
	delta /= dy;
#endif
	if (ceil_x) {
		int32_t remainder = product - MULS16((int16_t)delta, dy);
		if (dy > 0 ? remainder > 0 : remainder < 0) delta++;
	}
	return (int16_t)(ax + (int16_t)delta);
}

static inline void SEC_DANA fill_cells(uint8_t *row, uint16_t col, uint16_t end, uint8_t type)
{
	uint8_t *p;
	uint8_t high = type << 4, both = high | type;

	if (col >= end) return;
	p = row + (col >> 1);
	if (col & 1) {
		*p = (uint8_t)((*p & 0xF0) | type);
		p++;
	}
	for (; p < row + (end >> 1); p++) *p = both;
	if (end & 1) *p = (uint8_t)((*p & 0x0F) | high);
}

void surface_raster(const Hole *h, uint8_t *row, SurfaceRowFn put, void *ctx)
{
	SurfaceGrid g;
	ShapeRows rows[HOLE_MAX_SHAPES];
	int16_t xs[HOLE_CURVE_VERT_MAX];
	uint16_t r, i, j, k, col, end;
	uint8_t fill = (uint8_t)(h->base_surface * 0x11);

	surface_size(h, &g);
	shape_rows(h, rows);
	for (r = 0; r < g.h; r++) {
		/* Sample at the cell centres, in quarter yards: 2 yard cells are 8 apart. */
		int16_t sy = (int16_t)(r * 8 + 4);

		for (i = 0; i < g.stride; i++) row[i] = fill;
		for (i = 0; i < h->shape_count; i++) {
			const uint8_t *p, *a, *b, *stop;
			uint16_t offset, points;
			int curved;
			int16_t ay, y = sy;
			uint8_t type;

			if (sy < rows[i].low || sy >= rows[i].high) continue;
			offset = rows[i].offset;
			curved = (offset & CURVE_OFFSET) != 0;
			if (curved) {
				p = h->putting_curves + (offset & CURVE_OFFSET_MASK);
				type = (uint8_t)(((offset >> CURVE_SURFACE_SHIFT) & 3) + SURF_GREEN);
				points = GET16(p + 1);
				b = p + 3;
				y = (int16_t)(sy * 4);
			} else {
				p = h->shapes + offset;
				type = p[0];
				points = p[1];
				b = p + 2;
			}
			stop = b + points * 4;
			a = stop - 4;
			ay = GET16(a + 2);

			/* Where each edge crosses the row, counting an edge's low end only. */
			k = 0;
			for (; b < stop; b += 4) {
				int16_t by = GET16(b + 2);
				if ((ay <= y) != (by <= y)) {
					int16_t x = edge_x(a, b, ay, by, y, curved);
					uint16_t m = k++;
					while (m > 0 && xs[m - 1] > x) {
						xs[m] = xs[m - 1];
						m--;
					}
					xs[m] = x;
				}
				a = b;
				ay = by;
			}
			/* Fill cells whose centre falls inside each pair of crossings. */
			for (j = 0; j + 1 < k; j += 2) {
				col = curved ? (uint16_t)((xs[j] + 15) >> 5) : (uint16_t)((xs[j] + 3) >> 3);
				end = curved ? (uint16_t)((xs[j + 1] + 15) >> 5) : (uint16_t)((xs[j + 1] + 3) >> 3);
				if (end > g.w) end = g.w;
				fill_cells(row, col, end, type);
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

static int SEC_CARD contour_contains(const uint8_t *p, uint16_t n, uint8_t shift, fixed x, fixed y)
{
	const uint8_t *end = p + n * 4, *prev = end - 4;
	int16_t ay = GET16(prev + 2), row = (int16_t)(y >> shift);
	int inside = 0;

	for (; p < end; p += 4) {
		int16_t by = GET16(p + 2);
		if ((ay <= row) != (by <= row)) {
			int16_t ax = GET16(prev), bx = GET16(p);
			int64_t a = (int64_t)(x - ((fixed)ax << shift)) * (by - ay);
			int64_t b = (int64_t)(y - ((fixed)ay << shift)) * (bx - ax);
			if (by > ay ? a < b : a > b) inside ^= 1;
		}
		prev = p;
		ay = by;
	}
	return inside;
}

static inline int SEC_CARD bounds_contains(const SurfaceBounds *b, uint16_t x, uint16_t y)
{
	return x >= b->x0 && y >= b->y0 && x <= b->x1 && y <= b->y1;
}

static int SEC_CARD exact_cell(const Hole *h, uint16_t x, uint16_t y)
{
	uint8_t n;

	if (bounds_contains(&h->green_bounds, x, y)) return 1;
	if (bounds_contains(&h->hazard_bounds, x, y))
		for (n = 0; n < h->hazard_count; n++)
			if (bounds_contains(&h->shape_bounds[h->hazard_shapes[n]], x, y)) return 1;
	return 0;
}

static uint8_t SEC_CARD contour_surface(const Hole *h, fixed x, fixed y, uint16_t cx, uint16_t cy)
{
	const uint8_t *shape, *curve;
	uint8_t i, left, surface;

	/* A cell can cross a contour even when its centre is on the other side.
	 * Use the same shape order and contour replacements as the renderer. */
	shape = h->shapes;
	curve = h->putting_curves;
	left = *curve++;
	surface = h->base_surface;
	for (i = 0; i < h->shape_count; i++) {
		uint8_t type = shape[0], shift = 14;
		uint16_t points = shape[1];
		const uint8_t *p = shape + 2;
		shape = p + points * 4;
		if (left && *curve == i) {
			points = GET16(curve + 1);
			p = curve + 3;
			curve = p + points * 4;
			left--;
			shift = 12;
		}
		if (type != surface && bounds_contains(&h->shape_bounds[i], cx, cy) &&
		    contour_contains(p, points, shift, x, y)) surface = type;
	}
	return surface;
}

uint8_t hole_surface(const Hole *h, const SurfaceGrid *g, fixed x, fixed y)
{
	uint16_t cx, cy;
	fixed xmax, ymax;
	uint8_t surface;
	int off;

	if (!h->putting_curves) return surface_at(g, x, y);
	xmax = (fixed)h->width << FIX_SHIFT;
	ymax = (fixed)h->length << FIX_SHIFT;
	off = x < 0 || y < 0 || x >= xmax || y >= ymax;
	if (off) {
		/* Just inside the boundary, where a wet edge continues into the sea. */
		x = x < 0 ? 0 : x >= xmax ? xmax - 1 : x;
		y = y < 0 ? 0 : y >= ymax ? ymax - 1 : y;
	}
	cx = (uint16_t)(x >> (FIX_SHIFT + SURF_CELL_SHIFT));
	cy = (uint16_t)(y >> (FIX_SHIFT + SURF_CELL_SHIFT));
	surface = exact_cell(h, cx, cy) ? contour_surface(h, x, y, cx, cy) : surface_at(g, x, y);
	return off && surface != SURF_WATER ? SURF_OOB : surface;
}

uint8_t hole_hazards(const Hole *h, const SurfaceGrid *g, vec2 ball, angle aim, HazardCarry *out, uint8_t cap)
{
	vec2 step = vec2_dir(aim);
	fixed xmax = (fixed)h->width << 16, ymax = (fixed)h->length << 16;
	uint16_t yards = 0, first = 0;
	uint8_t count = 0, prev = SURF_ROUGH;

	if (!g || !g->cells) return 0;
	while (count < cap && ball.x >= 0 && ball.x < xmax && ball.y >= 0 && ball.y < ymax) {
		uint8_t s = hole_surface(h, g, ball.x, ball.y);
		if (s != SURF_WATER && s != SURF_BUNKER && s != SURF_OOB) s = SURF_ROUGH;
		if (s != prev) {
			if (prev != SURF_ROUGH && yards - first >= 2) {
				out[count].entry = first;
				out[count].clear = yards;
				out[count++].surface = prev;
			}
			first = yards;
			prev = s;
		}
		ball.x += step.x;
		ball.y += step.y;
		yards++;
	}
	if (count < cap && prev != SURF_ROUGH) {
		out[count].entry = first;
		out[count].clear = 0;
		out[count++].surface = prev;
	}
	return count;
}
