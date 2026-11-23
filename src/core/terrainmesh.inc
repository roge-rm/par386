/* Packed full-shot terrain tiles. The draw shares the scene camera and sprite clip pool. */
#define MESH_OCC_BIAS 32L             /* one eighth of a yard in Q8 depth */
#define CONTACT_W 152
#define CONTACT_H 50
#define CONTACT_SHOES 80

static const uint8_t contact_patterns[] = {
	0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55,
	0xAA, 0xAA, 0xAA, 0xAA, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t contact_lies[] = { SURF_ROUGH, SURF_BUNKER, SURF_WATER, SURF_OOB };

typedef struct {
	int16_t depth;
	uint16_t index;
} MeshTileOrder;

typedef struct {
	int8_t x, y;              /* pixels from the ball to the bottom of a shoe */
} ShoeContact;

typedef struct MeshWork {
	MeshPoint *points;
	TerraTri *batch;              /* larger batch for detailed native draws */
	MeshTileOrder *order;
	uint16_t *next;
	uint8_t *outside;
	int8_t *light;
	uint16_t bins[MESH_TILE_BINS];
	int32_t origin_depth, origin_side;
	int32_t green_down;           /* camera above pin 0 in Q8 yards */
	fixed height, ball_raise, camera_base;
	vec2 address;
	uint8_t *contact;
	ShoeContact shoe[CONTACT_SHOES];
	int16_t green_u, green_v;     /* pin 0 in Q4 yards */
	int16_t ball_x, ball_y;
	uint16_t order_count, cursor, triangle;
	int16_t bin;
	uint8_t started, loaded, light_row, reframed, shoes;
	uint8_t native_tile;          /* all points are in front of the near plane */
	uint8_t has_bunker;           /* a visible face needs sand relief */
	uint8_t occludable[MAX_OBJ], oc_count;
} MeshWork;

#define TILE_U16(p) ((uint16_t)((uint16_t)(p)[0] << 8 | (p)[1]))
#define TILE_S16(p) ((int16_t)TILE_U16(p))

static uint32_t SEC_CARD mesh_offset(const uint8_t *p)
{
	return ((uint32_t)TILE_U16(p + 12) << 16) | TILE_U16(p + 14);
}

/* Area in Q12 for a Q4 edge and a ball position in Q8 yards. */
static int32_t mesh_ball_side(const uint8_t *a, const uint8_t *b, int32_t x, int32_t y)
{
	int16_t ax = TILE_S16(a), ay = TILE_S16(a + 2);
	return (TILE_S16(b) - (int32_t)ax) * (y - ay * 16L) -
	       (TILE_S16(b + 2) - (int32_t)ay) * (x - ax * 16L);
}

static fixed mesh_ball_height(const TerrainMesh *mesh, vec2 ball, fixed fallback)
{
	const uint8_t *tile = mesh->data + 2;
	int32_t x = ball.x >> 8, y = ball.y >> 8;
	uint16_t i;

	for (i = 0; i < mesh->tiles; i++, tile += 16) {
		const uint8_t *vertices, *face;
		uint16_t k, count;

		if (x < TILE_S16(tile) * 16L || x > TILE_S16(tile + 4) * 16L ||
		    y < TILE_S16(tile + 2) * 16L || y > TILE_S16(tile + 6) * 16L) continue;
		vertices = mesh->data + mesh_offset(tile);
		face = vertices + 6 * TILE_U16(tile + 8);
		count = TILE_U16(tile + 10);
		for (k = 0; k < count; k++, face += 5) {
			const uint8_t *a = vertices + 6 * face[0];
			const uint8_t *b = vertices + 6 * face[1];
			const uint8_t *c = vertices + 6 * face[2];
			int32_t ab = mesh_ball_side(a, b, x, y);
			int32_t bc = mesh_ball_side(b, c, x, y), ca, area;

			if ((ab < 0 && bc > 0) || (ab > 0 && bc < 0)) continue;
			ca = mesh_ball_side(c, a, x, y);
			if ((ca < 0 && (ab > 0 || bc > 0)) ||
			    (ca > 0 && (ab < 0 || bc < 0))) continue;
			area = ab + bc + ca;
			if (area) {
				int16_t za = TILE_S16(a + 4);
				return za * 256L + (fixed)(((int64_t)ca * (TILE_S16(b + 4) - za) +
				       (int64_t)ab * (TILE_S16(c + 4) - za)) * 256 / area);
			}
		}
	}
	return fallback;
}

static void SEC_BALL mesh_shoes(MeshWork *w, const RenderGolfer *g)
{
	const RenderData *pose;
	const uint8_t *pixels, *tags = 0;
	uint32_t count;
	uint16_t width, height;
	int16_t x, y, bx, by, top;

	w->shoes = 0;
	if (!g) return;
	pose = &g->pose[POSE_ADDRESS];
	pixels = sprite(pose, &width, &height);
	if (!pixels || width > CONTACT_SHOES || height > 127) return;
	bx = (int16_t)be16(pose->p + 8);
	by = (int16_t)be16(pose->p + 10);
	if (bx < 0 || bx >= width || by < 0 || by >= height) return;
	count = MULU16(width, height);
	if (pose->len == 12 + count + (count + 1) / 2) tags = pixels + count;
	top = (int16_t)(height - height / 4);
	for (x = 0; x < width; x++) {
		for (y = (int16_t)height - 1; y >= top; y--) {
			uint32_t at = MULU16(y, width) + x;
			ShoeContact *shoe;

			if (tags) {
				uint8_t value = tags[at >> 1], tag = (uint8_t)((at & 1 ? value >> 4 : value) & 15);
				if (tag < 1 || tag > 3) continue;
			} else if (pixels[at] < PAL_SHOES || pixels[at] >= PAL_SHOES + PAL_SHOES_N) continue;
			shoe = &w->shoe[w->shoes++];
			shoe->x = (int8_t)(g->left ? bx - x : x - bx);
			shoe->y = (int8_t)(y - by + 1);
			break;
		}
	}
}

/* Keep a dry address on the same surface when the stance crosses a bank.
 * The world ball remains the origin for rules and the flight path. */
static vec2 SEC_BALL mesh_dry_address(const Job *j, const MeshWork *w, fixed reach, fixed clearance)
{
	const R *r = &j->r;
	vec2 ball = j->v.ball, side, along, shoe[2];
	int16_t i, left = 0, right = 0, lower = 0, radius, x, y;
	uint8_t lie;
	const fixed step = FIX_ONE / 4;
	const fixed edge_room = 9L << FIX_SHIFT;
	fixed yard_per_pixel;

	if (!w->shoes || !r->g || !r->g->cells) return ball;
	if (clearance) goto shoreline;
	/* A bank close enough for a shoe to reach also touches these grid cells. */
	for (y = -1; y <= 1; y++)
		for (x = -1; x <= 1; x++)
			if (surface_at(r->g, ball.x + x * (2L << FIX_SHIFT),
			               ball.y + y * (2L << FIX_SHIFT)) == SURF_WATER) goto shoreline;
	return ball;

shoreline:
	lie = hole_surface(r->h, r->g, ball.x, ball.y);
	if (lie == SURF_WATER || lie == SURF_OOB || lie == SURF_TEE) return ball;
	for (i = 0; i < w->shoes; i++) {
		const ShoeContact *p = &w->shoe[i];
		if (!i || p->x < left) left = p->x;
		if (!i || p->x > right) right = p->x;
		if (p->y > lower) lower = p->y;
	}
	yard_per_pixel = j->back / r->f;
	side = vec2_make(mulq(yard_per_pixel, r->rx), mulq(yard_per_pixel, r->ry));
	along = vec2_make(mulq(yard_per_pixel, r->fx), mulq(yard_per_pixel, r->fy));
	shoe[0] = vec2_make(side.x * left - along.x * lower, side.y * left - along.y * lower);
	shoe[1] = vec2_make(side.x * right - along.x * lower, side.y * right - along.y * lower);
	for (radius = 0; radius <= reach / step; radius++) {
		for (y = -radius; y <= radius; y++) {
			int16_t extent = radius - (y < 0 ? -y : y);
			for (x = extent; x >= -extent; x -= extent ? 2 * extent : 1) {
				vec2 at = vec2_make(ball.x + x * step, ball.y + y * step);
				if (clearance &&
				    (at.x < edge_room || at.y < edge_room ||
				     at.x > ((fixed)r->h->width << FIX_SHIFT) - edge_room ||
				     at.y > ((fixed)r->h->length << FIX_SHIFT) - edge_room)) continue;
				/* Leave the real launch point ahead of the camera's near plane. */
				if (clearance &&
				    mulq(at.x - ball.x, r->fx) + mulq(at.y - ball.y, r->fy) >
				    j->back - FIX_ONE - FIX_ONE / 8) continue;
				if (radius && hole_surface(r->h, r->g, at.x, at.y) != lie) continue;
				for (i = 0; i < 2; i++)
					if (hole_surface(r->h, r->g, at.x + shoe[i].x, at.y + shoe[i].y) == SURF_WATER) break;
				if (i != 2) continue;
				if (clearance &&
				    (hole_surface(r->h, r->g, at.x - clearance, at.y) == SURF_WATER ||
				     hole_surface(r->h, r->g, at.x + clearance, at.y) == SURF_WATER ||
				     hole_surface(r->h, r->g, at.x, at.y - clearance) == SURF_WATER ||
				     hole_surface(r->h, r->g, at.x, at.y + clearance) == SURF_WATER)) continue;
				return at;
			}
		}
	}
	return clearance ? w->address : ball;
}

static vec2 SEC_BALL mesh_initial_address(const Job *j, MeshWork *w)
{
	mesh_shoes(w, j->a->golfer);
	return mesh_dry_address(j, w, FIX_ONE, 0);
}

static void mesh_set_camera(Job *j, MeshWork *w, vec2 address)
{
	R *r = &j->r;
	fixed original = r->h->base + fix_mul(j->bh << 8, r->h->step);
	fixed ground = mesh_ball_height(&r->h->terrain_mesh, address, original);
	fixed rise = cam_up(r->vh, r->yh, r->scale, j->back);

	w->address = address;
	r->camx = address.x - mulq(j->back, r->fx);
	r->camy = address.y - mulq(j->back, r->fy);
	w->height = ground + rise;
	w->ball_raise = w->height - w->camera_base;
	r->camb = fix_div(w->height - r->h->base, r->h->step) >> 8;
}

static uint32_t SEC_CARD mesh_work(const Surface *s, const RenderView *v, Job *j)
{
	const TerrainMesh *mesh = &v->hole->terrain_mesh;
	int16_t vh;
	uint32_t base, light_bytes;
	uint16_t contact_bytes, batch_bytes;
	MeshWork *w;

	if (!mesh->data || v->green || fit(s, &vh) < 0) return 0;
	base = (render_work_size(s) + 7) & ~7UL;
	light_bytes = s->scale == 2 && s->depth == 8 ? MULU16(v->hole->hm_w, v->hole->hm_h) : 0;
	contact_bytes = s->depth < 8 ? (CONTACT_W / 4) * CONTACT_H * s->scale * s->scale : 0;
	batch_bytes = light_bytes && terra_fn ? TERRA_MESH_TRIS * (sizeof(TerraTri) + sizeof(TerraTurf)) : 0;
	if (j) {
		uint16_t i;

		w = (MeshWork *)((uint8_t *)j + base);
		w->batch = batch_bytes ? (TerraTri *)(w + 1) : 0;
		for (i = 0; i < batch_bytes; i++) ((uint8_t *)w->batch)[i] = 0;
		w->points = (MeshPoint *)((uint8_t *)(w + 1) + batch_bytes);
		w->order = (MeshTileOrder *)(w->points + mesh->vertices);
		w->next = (uint16_t *)(w->order + mesh->tiles);
		w->outside = (uint8_t *)(w->next + mesh->triangles);
		w->light = light_bytes ? (int8_t *)(w->outside + mesh->vertices) : 0;
		w->contact = contact_bytes && j->a->golfer ? w->outside + mesh->vertices + light_bytes : 0;
		w->order_count = w->cursor = 0;
		w->started = w->loaded = 0;
		w->reframed = 0;
		w->light_row = 0;
		w->camera_base = j->r.h->base + fix_mul(j->bh << 8, j->r.h->step) +
		                 cam_up(j->r.vh, j->r.yh, j->r.scale, j->back);
		mesh_set_camera(j, w, mesh_initial_address(j, w));
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
	       light_bytes + contact_bytes + batch_bytes;
}

/* Replay just the triangles touching the address area so greyscale has
 * the same material information as colour. */
static void SEC_DANA mesh_contact_batch(const Job *j, const TerraTris *draw)
{
	TerraTri faces[TERRA_TRIS_MAX];
	TerraTris mask = *draw;
	int16_t width = CONTACT_W * j->r.scale, height = CONTACT_H * j->r.scale;
	int16_t left = j->r.cx - width / 2, top = j->r.vh - height;
	int16_t i, k;

	mask.bits = j->mesh->contact;
	mask.tri = faces;
	mask.pat2 = contact_patterns;
	mask.rb = width / 4;
	mask.w = width;
	mask.vh = height;
	mask.lg = 1;
	mask.turf = 0;
	mask.noise = 0;
	mask.reflection = 0;
	mask.n = 0;
	for (i = 0; i < draw->n; i++) {
		const TerraTri *from = draw->tri + i;
		TerraTri *to = faces + mask.n;
		if ((from->y[0] < top && from->y[1] < top && from->y[2] < top) ||
		    (from->x[0] < left && from->x[1] < left && from->x[2] < left) ||
		    (from->x[0] >= left + width && from->x[1] >= left + width &&
		     from->x[2] >= left + width)) continue;
		for (k = 0; k < 3; k++) {
			to->x[k] = from->x[k] - left;
			to->y[k] = from->y[k] - top;
		}
		to->base = from->base == PAL_SAND ? 1 : from->base == PAL_WATER ? 2 :
		           from->base == PAL_OOB ? 3 : 0;
		to->lv = 0;
		mask.n++;
	}
	if (mask.n) terra_tris(&mask);
}

static void SEC_CARD mesh_sand_light(const Hole *h, const MeshWork *w, MeshPoint *p)
{
	int32_t hb, cut;
	int16_t rim, light = p->light >> 2;

	if (p->sand != MESH_SAND_UNLIT) return;
	if (h->base_surface == SURF_BUNKER) {
		/* Desert sand follows the dune slopes without a bunker rim. */
		p->sand = (int8_t)(light * 2);
		return;
	}
	hb = hb_at(h, (fixed)p->u * 4096L, (fixed)p->v * 4096L);
	/* The rim shade fades over the first quarter yard of the baked cut. */
	cut = ((h->base + fix_mulf(hb * 256L, h->step)) >> 8) - (w->height >> 8) + p->down;
	rim = cut <= 0 ? 32 : cut >= 64 ? 0 : (int16_t)(32 - (cut >> 1));
	/* Lift distant sand and soften the rim as its projection gets smaller. */
	p->sand = (int8_t)(light - (rim * 3 >> 2) +
	                  (MULS16(rim + 16, mesh_depth_span(p->depth >> 8, 40, 64)) >> 7));
}

/* Camera dot product in Q18, matching Q4 vertices times Q14 directions. */
static int32_t SEC_CARD mesh_origin(const R *r, int16_t qx, int16_t qy)
{
	int32_t frac = MULS16(r->camx & 4095, qx) + MULS16(r->camy & 4095, qy);

	return MULS16(r->camx >> 12, qx) + MULS16(r->camy >> 12, qy) + (frac >> 12);
}

static void SEC_BALL mesh_order(Job *j)
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
	if ((int16_t)n == n) return MULS16(n, focal);
	return MULS16(n >> 16, focal) * 65536L + (int32_t)MULU16(n, focal);
}
#else
#define mesh_divide(n, d) ((n) / (d))
#define mesh_scale(n, focal) ((n) * (focal))
#endif

static void SEC_CARD mesh_screen(R *r, MeshPoint *p)
{
	int32_t x = mesh_scale(p->side, r->f), y = mesh_scale(p->down, r->f);
	int32_t depth = p->depth;

#ifdef __m68k__
	/* On closer tiles both projections usually fit a signed word quotient. */
	if (depth <= INT16_MAX) {
		int32_t limit = depth << 15;
		if (x >= -limit && x < limit && y >= -limit && y < limit) {
			int16_t d = (int16_t)depth;
			__asm__ ("divs.w %2,%0\n\tdivs.w %2,%1" : "+&d" (x), "+&d" (y) : "d" (d) : "cc");
			x = (int16_t)x;
			y = (int16_t)y;
		} else {
			x = mesh_divide(x, depth);
			y = mesh_divide(y, depth);
		}
	} else
#endif
	{
		x = mesh_divide(x, depth);
		y = mesh_divide(y, depth);
	}
	x += r->cx;
	y += r->yh;

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
			r->f, r->w, r->vh, nv, r->h->hm_w, r->h->hm_h, r->h->hm_shift, 0,
			w->next, w->bins, nt, 0
		};

		terra_fn(TERRA_MESH_PROJECT, &projection);
		r->st->divides += (uint16_t)projection.projected * 2;
		mesh_occ_candidates(j, nv, projection.low);
		w->native_tile = projection.low >= MESH_TILE_NEAR;
		w->has_bunker = projection.has_bunker;
		goto loaded;
	}
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
	w->native_tile = lo >= MESH_TILE_NEAR;
	w->has_bunker = 0;
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
#ifndef __palmos__
		if (q[3] == SURF_BUNKER) w->has_bunker = 1;
#endif
		bin = (uint16_t)((uint32_t)(a->depth + b->depth + c->depth - lo) >> shift);
		w->next[k] = w->bins[bin];
		w->bins[bin] = (uint16_t)(k + 1);
	}
#ifdef __palmos__
loaded:
#endif
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

static void SEC_DANA mesh_flush(Job *j, TerraTris *d)
{
	if (j && j->mesh->contact) mesh_contact_batch(j, d);
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
		if (material <= SURF_DEEP_ROUGH) {
			int16_t i;

			t->depth[0] = (int16_t)(a->depth >> 8);
			t->depth[1] = (int16_t)(b->depth >> 8);
			t->depth[2] = (int16_t)(c->depth >> 8);
			if (!d->shade_grass)
				for (i = 0; i < 3; i++)
					t->light[i] = mesh_grass_light(t->u[i], t->v[i], t->light[i], t->depth[i],
					                              d->green_u, d->green_v, material, light);
		} else if (material == SURF_GREEN || material == SURF_FRINGE) {
			int32_t down = j->mesh->green_down;
			t->light[0] = mesh_green_light(down, a->down, a->light);
			t->light[1] = mesh_green_light(down, b->down, b->light);
			t->light[2] = mesh_green_light(down, c->down, c->light);
		} else if (material == SURF_BUNKER) {
			t->light[0] = a->sand; t->light[1] = b->sand; t->light[2] = c->sand;
		} else {
			/* The tee pads recede once they are beyond the address view. */
			t->light[0] = (a->light >> 2) - mesh_depth_span(a->depth >> 8, 18, 32);
			t->light[1] = (b->light >> 2) - mesh_depth_span(b->depth >> 8, 18, 32);
			t->light[2] = (c->light >> 2) - mesh_depth_span(c->depth >> 8, 18, 32);
		}
		light = mesh_detail_level(material, light);
	}
	/* Taper mowing contrast where the bands project to only a few pixels. */
	if (material == SURF_FAIRWAY && !d->turf) {
		int32_t sum_yards = (a->depth + b->depth + c->depth) >> 8;
		int16_t taper = (int16_t)((sum_yards - 270) >> 5);
		if (taper > MOW_CONTRAST) taper = MOW_CONTRAST;
		if (taper > 0) light = (uint8_t)(light < MOW_MID ? light + taper : light - taper);
	}
	q->lv = light;
	if (d->n == (d->shade_grass ? TERRA_MESH_TRIS : TERRA_TRIS_MAX)) mesh_flush(j, d);
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

static void SEC_CARD mesh_reflection(const R *r, const uint8_t *grey)
{
	const uint8_t *low = r->bits + MULU16(r->yh - 4, r->rb);
	const uint8_t *high = r->bits + MULU16(r->yh - 24, r->rb);
	uint16_t x, left = 0, right, last = (uint16_t)(r->w - 1);
	uint16_t sum = (uint16_t)((grey[low[0]] + grey[high[0]]) * 8);

	/* Blur the horizon light across the surface before painting the ground. */
	for (right = 0; right < 8; right++)
		sum += grey[low[right]] + grey[high[right]];
	for (x = 0; x <= last; x++) {
		r->row[x] = (uint8_t)((sum + 16) >> 5);
		sum += grey[low[right]] + grey[high[right]];
		sum -= grey[low[left]] + grey[high[left]];
		if (x >= 8) left++;
		if (right < last) right++;
	}
}

static uint8_t mesh_contact_lie(const Job *j, int16_t x, int16_t y)
{
	const R *r = &j->r;
	uint8_t ink;

	if (x < 0 || x >= r->w || y < 0 || y >= r->vh) return SURF_OOB;
	if (j->mesh->contact) {
		int16_t width = CONTACT_W * r->scale, height = CONTACT_H * r->scale;
		x -= r->cx - width / 2;
		y -= r->vh - height;
		if (x < 0 || x >= width || y < 0 || y >= height) return SURF_OOB;
		ink = j->mesh->contact[MULU16(y, width / 4) + (x >> 2)];
		return contact_lies[(ink >> (6 - 2 * (x & 3))) & 3];
	} else {
		ink = r->bits[MULU16(y, r->rb) + x];
	}
	if (ink < PAL_TERRAIN || ink >= PAL_TERRAIN + PAL_TERRAIN_N) return SURF_OOB;
	return (uint8_t)((ink - PAL_TERRAIN) >> 3);
}

static int mesh_contact_matches(const Job *j, int16_t x, int16_t y, uint8_t lie)
{
	uint8_t ground = mesh_contact_lie(j, x, y);
	if (lie == SURF_BUNKER) return ground == SURF_BUNKER;
	return ground != SURF_BUNKER && ground != SURF_WATER && ground != SURF_OOB;
}

static int mesh_stance_dry(const Job *j, int16_t x, int16_t y, int16_t fx, int16_t fy)
{
	const MeshWork *w = j->mesh;
	const R *r = &j->r;
	uint8_t i;

	if (y + fy >= r->vh || y + fy < r->yh ||
	    mesh_contact_lie(j, x + fx, y + fy) == SURF_WATER) return 0;
	for (i = 0; i < w->shoes; i++) {
		int16_t sy = y + w->shoe[i].y;
		if (sy >= r->vh || sy < r->yh ||
		    mesh_contact_lie(j, x + w->shoe[i].x, sy) == SURF_WATER) return 0;
	}
	return 1;
}

/* Reconcile the drawn contact when rounding or an older mesh puts the
 * projected marker across an edge. The world position stays fixed. */
static int mesh_address(Job *j)
{
	const R *r = &j->r;
	MeshWork *w = j->mesh;
	const RenderGolfer *g = j->a->golfer;
	const uint8_t *pose;
	int16_t fx, fy, radius, dx, dy, step, limit = (w->reframed ? 44 : 27) * r->scale;
	int16_t above = (w->reframed ? 36 : 23) * r->scale;
	int16_t bx = r->cx, by = r->vh - BALL_GAP * r->scale;
	uint8_t lie;
	int found = 0;

	w->ball_x = bx;
	w->ball_y = by;
	if (!g || !r->g || !r->g->cells || !(pose = g->pose[POSE_ADDRESS].p) ||
	    g->pose[POSE_ADDRESS].len < 12) return 1;
	lie = hole_surface(r->h, r->g, j->v.ball.x, j->v.ball.y);
	if (lie == SURF_WATER || lie == SURF_OOB) return 1;
	fx = (int16_t)be16(pose + 4) - (int16_t)be16(pose + 8);
	if (g->left) fx = -fx;
	fy = (int16_t)be16(pose + 6) - (int16_t)be16(pose + 10);
	for (radius = 0; radius <= limit; radius++) {
		for (dy = -radius; dy <= radius; dy++) {
			int16_t y = by + dy;
			if (y < by - above || y >= r->vh - 2 * r->scale) continue;
			dx = radius - (dy < 0 ? -dy : dy);
			for (step = dx; step >= -dx; step -= dx ? 2 * dx : 1) {
				int16_t x = bx + step;
				if (!mesh_contact_matches(j, x, y, lie)) continue;
				if (!found) {
					w->ball_x = x;
					w->ball_y = y;
					found = 1;
				}
				if (!mesh_stance_dry(j, x, y, fx, fy)) continue;
				w->ball_x = x;
				w->ball_y = y;
				return 1;
			}
		}
	}
	return 0;
}

/* A steep bank can hide the lie. Try closer in, then find a wider place
 * for the stance if the bank is too narrow at that angle. */
static int mesh_reframe(Job *j)
{
	R *r = &j->r;
	MeshWork *w = j->mesh;
	fixed old = w->height;
	vec2 address = w->address;

	if (!w->reframed) j->back = 2L << FIX_SHIFT;
	else if (w->reframed == 1) {
		int16_t vh;
		fixed old_back = j->back;
		scene_frame(&j->d, &j->v, &vh, &j->back);
		address = mesh_dry_address(j, w, 8L << FIX_SHIFT, FIX_ONE);
		if (address.x == w->address.x && address.y == w->address.y) {
			j->back = old_back;
			return 0;
		}
	} else return 0;
	mesh_set_camera(j, w, address);
	if (w->light) w->green_down += (w->height - old) >> 8;
	w->reframed++;
	w->started = w->loaded = 0;
	w->order_count = w->cursor = 0;
	r->nobj = r->pool_used = 0;
	j->stage = JS_GATHER;
	return 1;
}

static int SEC_BALL mesh_lighting(MeshWork *w, const Hole *h, RenderClock clk, uint32_t until)
{
	while (w->light_row < h->hm_h) {
		uint16_t end = clk ? (w->light_row + 4) & ~3 : h->hm_h;
		TerraMeshLight t = {h->heights, w->light, (h->step * 3) >> h->hm_shift,
		                    h->hm_w, h->hm_h, w->light_row, end < h->hm_h ? end : h->hm_h};
		if (terra_fn) terra_fn(TERRA_MESH_LIGHT, &t);
		else terra_mesh_light(&t);
		w->light_row = (uint8_t)t.end;
		if (!(w->light_row & 3) && late(clk, until)) return 0;
	}
	return 1;
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
	uint16_t face_order[TERRA_MESH_TRIS];
	TerraTurf *detail = w->batch ? (TerraTurf *)(w->batch + TERRA_MESH_TRIS) : turf;
	TerraTris draw = { r->bits, w->batch ? w->batch : batch, bayer_tab, patterns, grey_tab,
	                   r->rb, r->w, r->vh, 0, r->lg, terra_fn && w->batch, w->light ? detail : 0,
	                   (r->lg == 1 || terra_fn || w->light) ? pal_noise : 0,
	                   r->h->terrain_mesh.mow_phase,
	                   r->lg == 3 && r->yh > 24 && (terra_fn || w->light) ? r->row : 0,
	                   w->light ? w->green_u : 0, w->light ? w->green_v : 0 };
	TerraMeshFaces native = { &draw, w->points, 0, face_order,
	                         w->light ? w->green_down : 0, 0, 0, w->next };
	const uint8_t *p;
	uint16_t k, steps = 0;

	if (!w->started) {
		if (w->contact) {
			uint16_t bytes = (CONTACT_W / 4) * CONTACT_H * r->scale * r->scale;
			for (k = 0; k < bytes; k++) w->contact[k] = 0xff;
		}
		if (draw.reflection) mesh_reflection(r, grey_tab);
		for (k = 0; k < r->nobj; k++) snap(r, &r->obj[k]);
		mesh_order(j);
		mesh_horizon(j, &draw);
		w->started = 1;
	}
	if (w->light && !mesh_lighting(w, r->h, clk, until)) goto pause;
	while (w->cursor < w->order_count) {
		if (!w->loaded) mesh_load(j);
		p = mesh_tile(j);
		native.faces = p;
		if (!clk && draw.shade_grass && w->bin == MESH_TILE_BINS - 1 &&
		    w->triangle == w->bins[w->bin] && w->native_tile && !w->oc_count && !w->has_bunker) {
			if (draw.n) mesh_flush(j, &draw);
			native.bins = w->bins;
			terra_fn(TERRA_MESH_FACES, &native);
			native.bins = 0;
			w->bin = -1;
		}
		while (w->bin >= 0) {
			while (w->triangle) {
				const uint8_t *q;
				MeshPoint *a, *b, *c;
				int clipped, indexed;

				k = (uint16_t)(w->triangle - 1);
				w->triangle = w->next[k];
				q = p + 5 * k;
				clipped = !w->native_tile &&
				          ((w->outside[q[0]] | w->outside[q[1]] | w->outside[q[2]]) & MESH_OUT_NEAR);
				indexed = draw.shade_grass && !clipped;
				/* ARM reads the projected points for an ordinary face. */
				if (indexed && !w->oc_count && q[3] != SURF_BUNKER) goto queue_face;
				a = w->points + q[0]; b = w->points + q[1]; c = w->points + q[2];
				if (w->light && q[3] == SURF_BUNKER) {
					mesh_sand_light(r->h, w, a);
					mesh_sand_light(r->h, w, b);
					mesh_sand_light(r->h, w, c);
				}
				if (indexed) {
					if (w->oc_count) mesh_occ_triangle(j, a, b, c);
queue_face:
					if (draw.n) mesh_flush(j, &draw);
					face_order[native.count++] = k;
					if (native.count == TERRA_MESH_TRIS) {
						terra_fn(TERRA_MESH_FACES, &native);
						native.count = 0;
					}
				} else {
					if (native.count) {
						terra_fn(TERRA_MESH_FACES, &native);
						native.count = 0;
					}
					if (clipped) mesh_clip(j, &draw, q);
					else mesh_draw(j, &draw, a, b, c, q[3], q[4]);
				}
				if ((++steps & 15) == 0 && clk && (int32_t)(clk() - until) >= 0) goto pause;
			}
			w->bin--;
			if (w->bin >= 0) w->triangle = w->bins[w->bin];
		}
		if (native.count) {
			terra_fn(TERRA_MESH_FACES, &native);
			native.count = 0;
		}
		w->loaded = 0;
		w->cursor++;
		if (clk && (int32_t)(clk() - until) >= 0) goto pause;
	}
	if (draw.n) mesh_flush(j, &draw);
	if (!mesh_address(j) && mesh_reframe(j)) return 0;
	return 1;
pause:
	if (native.count) terra_fn(TERRA_MESH_FACES, &native);
	if (draw.n) mesh_flush(j, &draw);
	return 0;
}
