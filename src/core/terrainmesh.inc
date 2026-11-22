/* Packed full-shot terrain tiles. The draw shares the scene camera and sprite clip pool. */
#define MESH_TILE_BINS 32
#define MESH_OCC_BIAS 32L             /* one eighth of a yard in Q8 depth */
#define MOW_PHASES 0xffffffUL

typedef struct {
	int16_t depth;
	uint16_t index;
} MeshTileOrder;

typedef struct MeshWork {
	MeshPoint *points;
	MeshTileOrder *order;
	uint16_t *next;
	uint8_t *outside;
	int8_t *light;
	uint16_t bins[MESH_TILE_BINS];
	int32_t origin_depth, origin_side;
	int32_t green_down;           /* camera above pin 0 in Q8 yards */
	fixed height;
	int16_t green_u, green_v;     /* pin 0 in Q4 yards */
	uint16_t order_count, cursor, triangle;
	int16_t bin;
	uint8_t started, loaded, light_row;
	uint8_t occludable[MAX_OBJ], oc_count;
} MeshWork;

#define TILE_U16(p) ((uint16_t)((uint16_t)(p)[0] << 8 | (p)[1]))
#define TILE_S16(p) ((int16_t)TILE_U16(p))

static uint32_t SEC_CARD mesh_offset(const uint8_t *p)
{
	return ((uint32_t)TILE_U16(p + 12) << 16) | TILE_U16(p + 14);
}

/* Find the 24-yard cycle that agrees with the baked dark and light faces. */
static uint32_t SEC_CARD mesh_mow_phases(uint32_t phases, const uint8_t *vertices,
                                       const uint8_t *face)
{
	int32_t sum = TILE_S16(vertices + face[0] * 6 + 2) +
	              (int32_t)TILE_S16(vertices + face[1] * 6 + 2) +
	              TILE_S16(vertices + face[2] * 6 + 2);
	/* A face centre is in one 12-yard band. The bias keeps the apron positive. */
	uint16_t row = (uint16_t)((sum + 768L * 48) / 48);
	uint16_t start = (uint16_t)((row + (face[4] < MOW_MID ? 1 : 13)) % 24);

	return phases & ((0xfffUL << start) | (0xfffUL >> (24 - start)));
}

static int SEC_CARD mesh_parse(TerrainMesh *mesh, const Hole *hole, const uint8_t *data, uint32_t size)
{
	const uint8_t *d, *p, *vertices;
	uint32_t end, phases = MOW_PHASES;
	uint16_t count, i, k, nv, nt, mv = 0, mt = 0;
	int16_t xmax = (int16_t)((hole->width + 128) * 16);
	int16_t ymax = (int16_t)((hole->length + 128) * 16);

	mesh->data = 0;
	mesh->tiles = mesh->vertices = mesh->triangles = 0;
	mesh->mow_phase = 0;
	if (!data || size < 2 || size > 60000) return 0;
	count = TILE_U16(data);
	end = 2 + 16UL * count;
	if (!count || count > 512 || size < end) return 0;
	for (i = 0, d = data + 2; i < count; i++, d += 16) {
		int16_t x0 = TILE_S16(d), y0 = TILE_S16(d + 2);
		int16_t x1 = TILE_S16(d + 4), y1 = TILE_S16(d + 6);
		uint32_t bytes;

		nv = TILE_U16(d + 8);
		nt = TILE_U16(d + 10);
		bytes = 6UL * nv + 5UL * nt;
		if (x0 < -128*16 || y0 < -128*16 || x1 > xmax || y1 > ymax ||
		    x0 >= x1 || y0 >= y1 || nv < 3 || nv > 255 || !nt || nt > 512 ||
		    mesh_offset(d) != end || bytes > size - end) return 0;
		vertices = p = data + end;
		end += bytes;
		for (k = 0; k < nv; k++, p += 6) {
			int16_t x = TILE_S16(p), y = TILE_S16(p + 2);
			if (x < x0 || x > x1 || y < y0 || y > y1) return 0;
		}
		for (k = 0; k < nt; k++, p += 5) {
			if (p[0] >= nv || p[1] >= nv || p[2] >= nv ||
			    p[0] == p[1] || p[1] == p[2] || p[2] == p[0] ||
			    p[3] >= SURF_COUNT || p[4] > (PAL_RAMP_N - 1) * 16) return 0;
			if (p[3] == SURF_FAIRWAY && (phases & (phases - 1)))
				phases = mesh_mow_phases(phases, vertices, p);
		}
		if (mv < nv) mv = nv;
		if (mt < nt) mt = nt;
	}
	if (end != size) return 0;
	mesh->data = data;
	mesh->tiles = count;
	mesh->vertices = mv;
	mesh->triangles = mt;
	while (phases && !(phases & 1)) {
		mesh->mow_phase++;
		phases >>= 1;
	}
	return 1;
}

static const TerrainMesh *SEC_CARD mesh_pick(const Surface *s, const RenderView *v)
{
	const TerrainMesh *mesh = &v->hole->terrain_mesh;

	if (!mesh->data || v->green || s->half) return 0;
	if (s->scale == 1 && s->w == 160 && s->h == 160 && (s->depth == 2 || s->depth == 8))
		return mesh;
	if (s->scale == 2 && s->w == 320 && (s->h == 320 || s->h == 448) && s->depth == 8)
		return mesh;
	return 0;
}

static uint32_t SEC_CARD mesh_work(const Surface *s, const RenderView *v, Job *j)
{
	const TerrainMesh *mesh = mesh_pick(s, v);
	uint32_t base, light_bytes;
	MeshWork *w;

	if (!mesh) return 0;
	base = (render_work_size(s) + 7) & ~7UL;
	light_bytes = s->scale == 2 ? MULU16(v->hole->hm_w, v->hole->hm_h) : 0;
	if (j) {
		w = (MeshWork *)((uint8_t *)j + base);
		w->points = (MeshPoint *)(w + 1);
		w->order = (MeshTileOrder *)(w->points + mesh->vertices);
		w->next = (uint16_t *)(w->order + mesh->tiles);
		w->outside = (uint8_t *)(w->next + mesh->triangles);
		w->light = light_bytes ? (int8_t *)(w->outside + mesh->vertices) : 0;
		w->order_count = w->cursor = 0;
		w->started = w->loaded = 0;
		w->light_row = 0;
		w->height = j->r.h->base + fix_mul(j->bh << 8, j->r.h->step) +
		            cam_up(j->r.vh, j->r.yh, j->r.scale, j->back);
		if (light_bytes) {
			const Hole *h = v->hole;
			int32_t hb = hb_at(h, h->pins[0].x, h->pins[0].y);
			w->green_down = (w->height - h->base - fix_mulf(hb * 256L, h->step)) >> 8;
			w->green_u = (int16_t)(h->pins[0].x >> 12);
			w->green_v = (int16_t)(h->pins[0].y >> 12);
		}
		j->mesh = w;
	}
	return base + sizeof(MeshWork) + (uint32_t)mesh->vertices * sizeof(MeshPoint) +
	       (uint32_t)mesh->tiles * sizeof(MeshTileOrder) + mesh->triangles * 2UL + mesh->vertices +
	       light_bytes;
}

/* One slope sample per heightmap node, lit from the lower left of the hole. */
static void SEC_CARD mesh_light_row(const Hole *h, int8_t *light, uint8_t j)
{
	uint16_t w = h->hm_w, x;
	const uint8_t *row = h->heights + MULU16(j, w);
	const uint8_t *near = j ? row - w : row;
	const uint8_t *far = j + 1 < h->hm_h ? row + w : row;
	int32_t gain = (h->step * 3) >> h->hm_shift;

	light += MULU16(j, w);
	for (x = 0; x < w; x++) {
		int16_t gx = row[x + 1 < w ? x + 1 : x] - row[x ? x - 1 : x];
		int16_t gy = far[x] - near[x];
		int32_t value = ((gx * 3 + gy * 2) * gain) >> FIX_SHIFT;
		light[x] = (int8_t)(value < -24 ? -24 : value > 24 ? 24 : value);
	}
}

static __inline__ int16_t SEC_CARD mesh_depth_span(const MeshPoint *p, int16_t start, int16_t span)
{
	int32_t yards = (p->depth >> 8) - start;

	return yards <= 0 ? 0 : yards >= span ? span : (int16_t)yards;
}

static void SEC_CARD mesh_sand_light(const Hole *h, const MeshWork *w, MeshPoint *p)
{
	int32_t hb, cut;
	int16_t rim;

	if (p->sand != MESH_SAND_UNLIT) return;
	if (h->base_surface == SURF_BUNKER) {
		/* Desert sand follows the dune slopes without a bunker rim. */
		p->sand = (int8_t)(p->light * 2);
		return;
	}
	hb = hb_at(h, (fixed)p->u * 4096L, (fixed)p->v * 4096L);
	/* The rim shade fades over the first quarter yard of the baked cut. */
	cut = ((h->base + fix_mulf(hb * 256L, h->step)) >> 8) - (w->height >> 8) + p->down;
	rim = cut <= 0 ? 32 : cut >= 64 ? 0 : (int16_t)(32 - (cut >> 1));
	/* Lift distant sand and soften the rim as its projection gets smaller. */
	p->sand = (int8_t)(p->light - (rim * 3 >> 2) +
	                  (MULS16(rim + 16, mesh_depth_span(p, 40, 64)) >> 7));
}

static __inline__ int16_t SEC_CARD mesh_green_light(const MeshWork *w, const MeshPoint *p)
{
	int32_t shade = ((w->green_down - p->down) >> 3) + p->light;
	return (int16_t)(shade < -18 ? -18 : shade > 18 ? 18 : shade);
}

static int16_t SEC_CARD mesh_grass_light(const MeshWork *w, const MeshPoint *p, int16_t shade)
{
	int16_t dx = p->u - w->green_u, dy = p->v - w->green_v;
	/* Slope contrast gains up to three quarters from 32 to 128 yards. */
	int16_t slope = (int16_t)(p->light +
	                         ((MULS16(p->light, mesh_depth_span(p, 32, 96)) + 64) >> 7));
	int16_t near;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	/* Bring the grass tones closer together within 24 yards of the green,
	 * then fade back to the regular course shading over the next 32 yards. */
	near = (int16_t)(56 * 16 - (dx > dy ? dx : dy));
	if (near <= 0) return slope;
	if (near >= 32 * 16) return (int16_t)(slope + shade);
	return (int16_t)(slope + (MULS16(shade, near) >> 9));
}

static __inline__ int16_t SEC_CARD mesh_mow_light(const MeshWork *w, const MeshPoint *p, int16_t dark)
{
	/* Match the fade at shared vertices within each 12-yard band. */
	int16_t fade = (int16_t)((mesh_depth_span(p, 90, 139) * 3) >> 5);

	return (int16_t)(mesh_grass_light(w, p, -8) + (dark ? fade : -fade));
}

/* Camera dot product in Q18, matching Q4 vertices times Q14 directions. */
static int32_t SEC_CARD mesh_origin(const R *r, int16_t qx, int16_t qy)
{
	int32_t frac = MULS16(r->camx & 4095, qx) + MULS16(r->camy & 4095, qy);

	return MULS16(r->camx >> 12, qx) + MULS16(r->camy >> 12, qy) + (frac >> 12);
}

static void SEC_CARD mesh_order(Job *j)
{
	const R *r = &j->r;
	MeshWork *w = j->mesh;
	const uint8_t *d = j->r.h->terrain_mesh.data + 2;
	int16_t i, k, x0, x1, y0, y1, key;
	int32_t z, side, far, limit;
	uint16_t dx, dy, fx, fy, rx, ry;

	w->origin_depth = mesh_origin(r, r->fx, r->fy);
	w->origin_side = mesh_origin(r, r->rx, r->ry);
	fx = r->fx < 0 ? -r->fx : r->fx; fy = r->fy < 0 ? -r->fy : r->fy;
	rx = r->rx < 0 ? -r->rx : r->rx; ry = r->ry < 0 ? -r->ry : r->ry;

	for (i = 0; i < r->h->terrain_mesh.tiles; i++, d += 16) {
		x0 = TILE_S16(d); y0 = TILE_S16(d + 2);
		x1 = TILE_S16(d + 4); y1 = TILE_S16(d + 6);
		dx = (uint16_t)(x1 - x0); dy = (uint16_t)(y1 - y0);
		z = (MULS16(x0 + x1, r->fx) + MULS16(y0 + y1, r->fy) - 2 * w->origin_depth) >> 11;
		far = z + ((MULU16(dx, fx) + MULU16(dy, fy)) >> 11) + 4;
		if (far < MESH_TILE_NEAR) continue;
		side = (MULS16(x0 + x1, r->rx) + MULS16(y0 + y1, r->ry) - 2 * w->origin_side) >> 11;
		/* Allow one pixel beyond the frustum for rounded screen coordinates. */
		limit = ((MULU16(dx, rx) + MULU16(dy, ry)) >> 11) + 4 +
		        (far * (r->cx + 1) + r->f - 1) / r->f;
		if (side < -limit || side > limit) continue;
		key = (int16_t)(z >> 4);
		k = w->order_count++;
		while (k && w->order[k - 1].depth < key) {
			w->order[k] = w->order[k - 1];
			k--;
		}
		w->order[k].depth = key;
		w->order[k].index = (uint16_t)i;
	}
}

#ifdef __m68k__
static __inline__ uint16_t SEC_CARD mesh_divu(uint32_t n, uint16_t d)
{
	__asm__ ("divu.w %1,%0" : "+d" (n) : "d" (d) : "cc");
	return (uint16_t)n;
}

/* Use the word divide where its quotient fits. Correct the rounded divisor
 * against the original numerator for distances beyond 255 yards. */
static int32_t SEC_CARD mesh_divide(int32_t numerator, int32_t depth)
{
	uint32_t n = numerator < 0 ? 0u - (uint32_t)numerator : (uint32_t)numerator;
	uint32_t d = (uint32_t)depth, q, product;

	if (d <= 65535 && (n >> 16) < d) {
		q = mesh_divu(n, (uint16_t)d);
	} else if (d > 65535 && d <= 262143 && (n >> 18) < (d >> 2)) {
		q = mesh_divu(n >> 2, (uint16_t)(d >> 2));
		product = MULU16(q, d) + (MULU16(q, d >> 16) << 16);
		while (product > n) { product -= d; q--; }
	} else {
		return numerator / depth;
	}
	return numerator < 0 ? -(int32_t)q : (int32_t)q;
}

/* A pair of word multiplies for the 32-bit projection numerator. */
static __inline__ int32_t SEC_CARD mesh_scale(int32_t n, uint16_t focal)
{
	return MULS16(n >> 16, focal) * 65536L + (int32_t)MULU16(n, focal);
}
#else
#define mesh_divide(n, d) ((n) / (d))
#define mesh_scale(n, focal) ((n) * (focal))
#endif

static void SEC_CARD mesh_screen(R *r, MeshPoint *p)
{
	int32_t x = r->cx + mesh_divide(mesh_scale(p->side, r->f), p->depth);
	int32_t y = r->yh + mesh_divide(mesh_scale(p->down, r->f), p->depth);

	p->x = (int16_t)(x < -MESH_TILE_COORD ? -MESH_TILE_COORD : x > MESH_TILE_COORD ? MESH_TILE_COORD : x);
	p->y = (int16_t)(y < -MESH_TILE_COORD ? -MESH_TILE_COORD : y > MESH_TILE_COORD ? MESH_TILE_COORD : y);
	r->st->divides += 2;
}

static const uint8_t *SEC_CARD mesh_tile(const Job *j)
{
	const uint8_t *d = j->r.h->terrain_mesh.data + 2 + 16 * j->mesh->order[j->mesh->cursor].index;
	return j->r.h->terrain_mesh.data + mesh_offset(d) + TILE_U16(d + 8) * 6;
}

/* Skip objects beyond this tile's horizontal and vertical screen bounds. */
static void SEC_CARD mesh_occ_candidates(Job *j, uint16_t nv, int32_t depth)
{
	const R *r = &j->r;
	MeshWork *w = j->mesh;
	const MeshPoint *p;
	const RObj *o;
	int16_t left = r->w, right = -1, top = r->vh;
	uint16_t i;

	w->oc_count = 0;
	if (!r->nobj || (r->obj[r->nobj - 1].depth >> 8) - MESH_OCC_BIAS <= depth) return;
	if (depth < MESH_TILE_NEAR) {
		/* Clipping at the camera plane can extend the projected bounds. */
		left = top = 0;
		right = r->w - 1;
	} else {
		for (i = 0, p = w->points; i < nv; i++, p++) {
			if (p->x < left) left = p->x;
			if (p->x > right) right = p->x;
			if (p->y < top) top = p->y;
		}
	}
	for (i = 0, o = r->obj; i < r->nobj; i++, o++)
		if (o->clip != NO_CLIP && (o->depth >> 8) - MESH_OCC_BIAS > depth &&
		    left < o->c1 && right >= o->c0 && top + 1 < o->y0 + o->dh)
			w->occludable[w->oc_count++] = (uint8_t)i;
}

static void SEC_CARD mesh_load(Job *j)
{
	R *r = &j->r;
	MeshWork *w = j->mesh;
	const uint8_t *d = j->r.h->terrain_mesh.data + 2 + 16 * w->order[w->cursor].index;
	const uint8_t *p = j->r.h->terrain_mesh.data + mesh_offset(d), *q;
	uint16_t nv = TILE_U16(d + 8), nt = TILE_U16(d + 10), k, bin;
	MeshPoint *a, *b, *c;
	int32_t lo = 0, hi = 0;
	int16_t x, y;
	uint32_t span;
	uint8_t shift = 0;

#ifdef __palmos__
	if (terra_fn) {
		TerraMeshProject projection = {
			p, w->points, w->outside, w->light, w->origin_side, w->origin_depth,
			w->height >> 8, 0, 0, r->fx, r->fy, r->rx, r->ry, r->cx, r->yh,
			r->f, r->w, r->vh, nv, r->h->hm_w, r->h->hm_h, r->h->hm_shift, 0
		};

		terra_fn(TERRA_MESH_PROJECT, &projection);
		lo = projection.low;
		hi = projection.high;
		r->st->divides += (uint16_t)projection.projected * 2;
		p += nv * 6;
	} else
#endif
	for (k = 0, a = w->points; k < nv; k++, a++, p += 6) {
		x = TILE_S16(p); y = TILE_S16(p + 2);
		if (w->light) {
			a->u = x;
			a->v = y;
			a->light = mesh_light_sample(w->light, r->h->hm_w, r->h->hm_h,
			                            r->h->hm_shift, x, y);
			a->sand = MESH_SAND_UNLIT;
		}
		a->depth = (MULS16(x, r->fx) + MULS16(y, r->fy) - w->origin_depth) >> 10;
		a->side = (MULS16(x, r->rx) + MULS16(y, r->ry) - w->origin_side) >> 10;
		a->down = (w->height >> 8) - TILE_S16(p + 4);
		if (!k || a->depth < lo) lo = a->depth;
		if (!k || a->depth > hi) hi = a->depth;
		if (a->depth < MESH_TILE_NEAR) w->outside[k] = MESH_OUT_NEAR;
		else {
			mesh_screen(r, a);
			w->outside[k] = (a->x < 0 ? MESH_OUT_LEFT : a->x >= r->w ? MESH_OUT_RIGHT : 0) |
			                (a->y < 0 ? MESH_OUT_TOP : a->y >= r->vh ? MESH_OUT_BOTTOM : 0);
		}
	}
	mesh_occ_candidates(j, nv, lo);
	span = 3 * (uint32_t)(hi - lo);
	while ((span >> shift) >= MESH_TILE_BINS) shift++;
	lo *= 3;
	for (k = 0; k < MESH_TILE_BINS; k++) w->bins[k] = 0;
	for (k = nt, q = p + nt * 5; k;) {
		uint8_t oa, ob, oc;

		q -= 5;
		k--;
		/* The triangle cannot reach the view if all three points miss the same edge. */
		oa = w->outside[q[0]]; ob = w->outside[q[1]]; oc = w->outside[q[2]];
		if (oa & ob & oc) continue;
		a = w->points + q[0]; b = w->points + q[1]; c = w->points + q[2];
		if (!((oa | ob | oc) & MESH_OUT_NEAR) &&
		    a->y == b->y && b->y == c->y) continue;
		if (w->light && q[3] == SURF_BUNKER) {
			mesh_sand_light(r->h, w, a);
			mesh_sand_light(r->h, w, b);
			mesh_sand_light(r->h, w, c);
		}
		bin = (uint16_t)((uint32_t)(a->depth + b->depth + c->depth - lo) >> shift);
		w->next[k] = w->bins[bin];
		w->bins[bin] = (uint16_t)(k + 1);
	}
	w->bin = MESH_TILE_BINS - 1;
	w->triangle = w->bins[w->bin];
	w->loaded = 1;
}

/* Always interpolate from the lower depth for shared edges. */
static int16_t SEC_CARD mesh_cross(R *r, MeshPoint *out,
                                      const MeshPoint *a, const MeshPoint *b, int32_t depth)
{
	const MeshPoint *from = a->depth < b->depth ? a : b;
	const MeshPoint *to = from == a ? b : a;
	int32_t f = (depth - from->depth) * 4096L / (to->depth - from->depth);

	out->side = from->side + (to->side - from->side) * f / 4096L;
	out->down = from->down + (to->down - from->down) * f / 4096L;
	out->depth = depth;
	r->st->divides++;
	mesh_screen(r, out);
	return a == from ? (int16_t)f : (int16_t)(4096 - f);
}

static void SEC_DANA mesh_flush(TerraTris *d)
{
	if (terra_fn) terra_fn(TERRA_TRIS, d);
	else terra_tris(d);
	d->n = 0;
}

/* Lower the clip only where this edge covers an object column. */
static void SEC_CARD mesh_occ_edge(R *r, const RObj *o, const MeshPoint *a,
                                   const MeshPoint *b)
{
	int16_t left, right, x, row;
	int32_t y, step = 0;
	int16_t *clip;

	if (a->x > b->x) { const MeshPoint *p = a; a = b; b = p; }
	left = a->x < o->c0 ? o->c0 : a->x;
	right = b->x >= o->c1 ? o->c1 - 1 : b->x;
	if (left > right) return;
	y = (int32_t)a->y * 65536L;
	if (a->x != b->x) {
		step = ((int32_t)b->y - a->y) * 65536L / (b->x - a->x);
		y += (left - a->x) * step;
		r->st->divides++;
	} else if (b->y < a->y) y = (int32_t)b->y * 65536L;
	clip = r->pool + o->clip + left - o->c0;
	for (x = left; x <= right; x++, clip++, y += step) {
		/* Round to the visible side and allow a row for raster rounding. */
		row = (int16_t)(((y + 65535L) >> 16) + 1);
		if (row < *clip) *clip = row < 0 ? 0 : row;
	}
}

static void SEC_CARD mesh_occ_triangle(Job *j, const MeshPoint *a, const MeshPoint *b,
                                       const MeshPoint *c)
{
	R *r = &j->r;
	MeshWork *w = j->mesh;
	const MeshPoint *tri[3] = { a, b, c };
	int16_t left = a->x < b->x ? a->x : b->x;
	int16_t right = a->x > b->x ? a->x : b->x;
	int16_t top = a->y < b->y ? a->y : b->y;
	int32_t near = a->depth < b->depth ? a->depth : b->depth;
	int32_t far = a->depth > b->depth ? a->depth : b->depth;
	uint16_t i;

	if (c->x < left) left = c->x;
	if (c->x > right) right = c->x;
	if (c->y < top) top = c->y;
	if (c->depth < near) near = c->depth;
	if (c->depth > far) far = c->depth;
	for (i = 0; i < w->oc_count; i++) {
		const RObj *o = r->obj + w->occludable[i];
		int32_t stop = (o->depth >> 8) - MESH_OCC_BIAS;

		if (stop <= near || left >= o->c1 || right < o->c0 ||
		    top + 1 >= o->y0 + o->dh) continue;
		if (far <= stop) {
			mesh_occ_edge(r, o, a, b);
			mesh_occ_edge(r, o, b, c);
			mesh_occ_edge(r, o, c, a);
		} else {
			/* Keep only the part of the triangle in front of this sprite. */
			MeshPoint crosses[2];
			const MeshPoint *p[4], *u = tri[2], *v;
			uint16_t k, n = 0, nc = 0;

			for (k = 0; k < 3; k++, u = v) {
				v = tri[k];
				if ((u->depth <= stop) != (v->depth <= stop)) {
					mesh_cross(r, &crosses[nc], u, v, stop);
					p[n++] = &crosses[nc++];
				}
				if (v->depth <= stop) p[n++] = v;
			}
			for (k = 0; k < n; k++) mesh_occ_edge(r, o, p[k], p[(k + 1 == n) ? 0 : k + 1]);
		}
	}
}

static void SEC_CARD mesh_draw(Job *j, TerraTris *d, const MeshPoint *a, const MeshPoint *b,
                                     const MeshPoint *c, uint8_t material, uint8_t light)
{
	TerraTri *q;

	if (j && j->mesh->oc_count) mesh_occ_triangle(j, a, b, c);
	q = (TerraTri *)d->tri + d->n++;
	q->x[0] = a->x; q->y[0] = a->y;
	q->x[1] = b->x; q->y[1] = b->y;
	q->x[2] = c->x; q->y[2] = c->y;
	q->base = (int16_t)(PAL_TERRAIN + material * PAL_RAMP_N);
	if (d->turf && (material <= SURF_BUNKER || material == SURF_TEE)) {
		TerraTurf *t = (TerraTurf *)d->turf + (d->n - 1);
		t->u[0] = a->u; t->v[0] = a->v; t->light[0] = a->light;
		t->u[1] = b->u; t->v[1] = b->v; t->light[1] = b->light;
		t->u[2] = c->u; t->v[2] = c->v; t->light[2] = c->light;
		if (material == SURF_FAIRWAY) {
			int16_t dark = light < MOW_MID;
			t->depth[0] = (int16_t)(a->depth >> 8);
			t->depth[1] = (int16_t)(b->depth >> 8);
			t->depth[2] = (int16_t)(c->depth >> 8);
			t->light[0] = mesh_mow_light(j->mesh, a, dark);
			t->light[1] = mesh_mow_light(j->mesh, b, dark);
			t->light[2] = mesh_mow_light(j->mesh, c, dark);
			light = dark ? MOW_MID - MOW_CONTRAST : MOW_MID + MOW_CONTRAST;
		} else if (material <= SURF_DEEP_ROUGH) {
			int16_t shade = material == SURF_ROUGH ? 8 : material == SURF_FIRST_CUT ? -14 : 14;
			t->light[0] = mesh_grass_light(j->mesh, a, shade);
			t->light[1] = mesh_grass_light(j->mesh, b, shade);
			t->light[2] = mesh_grass_light(j->mesh, c, shade);
			light = material == SURF_ROUGH ? 70 : material == SURF_FIRST_CUT ? 62 : 94;
		} else if (material == SURF_GREEN || material == SURF_FRINGE) {
			t->light[0] = mesh_green_light(j->mesh, a);
			t->light[1] = mesh_green_light(j->mesh, b);
			t->light[2] = mesh_green_light(j->mesh, c);
			light = material == SURF_GREEN ? 64 : 53;
		} else if (material == SURF_BUNKER) {
			t->light[0] = a->sand; t->light[1] = b->sand; t->light[2] = c->sand;
			light = 80;
		} else {
			/* The tee pads recede once they are beyond the address view. */
			t->light[0] -= mesh_depth_span(a, 18, 32);
			t->light[1] -= mesh_depth_span(b, 18, 32);
			t->light[2] -= mesh_depth_span(c, 18, 32);
			light = 62;
		}
	}
	/* Taper mowing contrast where the bands project to only a few pixels. */
	if (material == SURF_FAIRWAY && !d->turf) {
		int32_t sum_yards = (a->depth + b->depth + c->depth) >> 8;
		int16_t taper = (int16_t)((sum_yards - 270) >> 5);
		if (taper > MOW_CONTRAST) taper = MOW_CONTRAST;
		if (taper > 0) light = (uint8_t)(light < MOW_MID ? light + taper : light - taper);
	}
	q->lv = light;
	if (d->n == TERRA_TRIS_MAX) mesh_flush(d);
}

static void SEC_CARD mesh_emit(Job *j, TerraTris *d, const MeshPoint *a, const MeshPoint *b,
                                     const MeshPoint *c, uint8_t material, uint8_t light)
{
	if ((a->y == b->y && b->y == c->y) ||
	    (a->x < 0 && b->x < 0 && c->x < 0) ||
	    (a->x >= d->w && b->x >= d->w && c->x >= d->w) ||
	    (a->y < 0 && b->y < 0 && c->y < 0) ||
	    (a->y >= d->vh && b->y >= d->vh && c->y >= d->vh)) return;
	mesh_draw(j, d, a, b, c, material, light);
}

static void SEC_CARD mesh_clip(Job *j, TerraTris *d, const uint8_t *q)
{
	const MeshPoint *v[3], *p[4], *a, *b;
	MeshPoint cross[2];
	uint16_t i, n = 0, k = 0;
	uint8_t ina, inb;

	v[0] = j->mesh->points + q[0];
	v[1] = j->mesh->points + q[1];
	v[2] = j->mesh->points + q[2];
	a = v[2];
	ina = a->depth >= MESH_TILE_NEAR;
	for (i = 0; i < 3; i++) {
		b = v[i];
		inb = b->depth >= MESH_TILE_NEAR;
		if (ina != inb) {
			int16_t t = mesh_cross(&j->r, &cross[k], a, b, MESH_TILE_NEAR);
			if (d->turf && (q[3] <= SURF_BUNKER || q[3] == SURF_TEE)) {
				cross[k].u = (int16_t)(a->u + (MULS16(b->u - a->u, t) >> 12));
				cross[k].v = (int16_t)(a->v + (MULS16(b->v - a->v, t) >> 12));
				cross[k].light = (int8_t)(a->light + ((MULS16(b->light - a->light, t) + 2048) >> 12));
				if (q[3] == SURF_BUNKER)
					cross[k].sand = (int8_t)(a->sand + ((MULS16(b->sand - a->sand, t) + 2048) >> 12));
			}
			p[n++] = &cross[k++];
		}
		if (inb) p[n++] = b;
		a = b;
		ina = inb;
	}
	if (n >= 3) mesh_emit(j, d, p[0], p[1], p[2], q[3], q[4]);
	if (n == 4) mesh_emit(j, d, p[0], p[2], p[3], q[3], q[4]);
}

static void SEC_CARD mesh_horizon(Job *j, TerraTris *d)
{
	const R *r = &j->r;
	MeshPoint p[4];

	p[0].x = p[3].x = 0;
	p[1].x = p[2].x = r->w;
	p[0].y = p[1].y = r->yh;
	p[2].y = p[3].y = r->vh;
	mesh_emit(0, d, &p[0], &p[1], &p[2], SURF_OOB, 62);
	mesh_emit(0, d, &p[0], &p[2], &p[3], SURF_OOB, 62);
}

/* A tile and its depth lists stay live across time slices. */
static int SEC_CARD mesh_paint(Job *j, RenderClock clk, uint32_t until,
                                    const uint8_t *patterns, const uint8_t *bayer_tab,
                                    const uint8_t *grey_tab)
{
	R *r = &j->r;
	MeshWork *w = j->mesh;
	TerraTri batch[TERRA_TRIS_MAX];
	TerraTurf turf[TERRA_TRIS_MAX] = {{{0}, {0}, {0}, {0}}};
	TerraTris draw = { r->bits, batch, bayer_tab, patterns, grey_tab,
	                   r->rb, r->w, r->vh, 0, r->lg, 0, w->light ? turf : 0, pal_noise,
	                   r->h->terrain_mesh.mow_phase };
	const uint8_t *p;
	uint16_t k, steps = 0;

	if (!w->started) {
		for (k = 0; k < r->nobj; k++) snap(r, &r->obj[k]);
		mesh_order(j);
		mesh_horizon(j, &draw);
		w->started = 1;
	}
	while (w->light && w->light_row < r->h->hm_h) {
		mesh_light_row(r->h, w->light, w->light_row++);
		if (!(w->light_row & 3) && late(clk, until)) goto pause;
	}
	while (w->cursor < w->order_count) {
		if (!w->loaded) mesh_load(j);
		p = mesh_tile(j);
		while (w->bin >= 0) {
			while (w->triangle) {
				const uint8_t *q;
				const MeshPoint *a, *b, *c;

				k = (uint16_t)(w->triangle - 1);
				w->triangle = w->next[k];
				q = p + 5 * k;
				a = w->points + q[0]; b = w->points + q[1]; c = w->points + q[2];
				if (a->depth < MESH_TILE_NEAR || b->depth < MESH_TILE_NEAR || c->depth < MESH_TILE_NEAR)
					mesh_clip(j, &draw, q);
				else mesh_draw(j, &draw, a, b, c, q[3], q[4]);
				if ((++steps & 15) == 0 && clk && (int32_t)(clk() - until) >= 0) goto pause;
			}
			w->bin--;
			if (w->bin >= 0) w->triangle = w->bins[w->bin];
		}
		w->loaded = 0;
		w->cursor++;
		if (clk && (int32_t)(clk() - until) >= 0) goto pause;
	}
	if (draw.n) mesh_flush(&draw);
	return 1;
pause:
	if (draw.n) mesh_flush(&draw);
	return 0;
}
