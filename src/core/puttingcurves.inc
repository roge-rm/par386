/* Project the course contours and fill them in course order. The small
 * lighting grid is shared by all materials and interpolated in screen space. */
#define CURVE_LIGHT_ROWS 31
#define CURVE_SPAN_BATCH 48
#define CURVE_EDGES (HOLE_CURVE_VERT_MAX * 3 / 2)
#define CURVE_GREEN_RUNS 4
#define CURVE_GREEN_STRIDE (1 + 2 * CURVE_GREEN_RUNS)

typedef struct { fixed side, down, depth; } CurveCameraPoint;
typedef struct { int32_t x, y; } CurveScreenPoint;
typedef struct {
	int32_t x, step;          /* 24.8 screen x at the next pixel centre */
	int16_t lo, hi;
} CurveEdge;

struct PuttingCurves {
	const uint8_t *contour, *shape;
	int16_t *green_rows;
	uint8_t remaining, shape_index, material, phase;
	int16_t y, end, light_row, edge_row;
	int16_t green_left, green_right, green_top, green_bottom;
	uint16_t edges, next, active;
	CurveEdge edge[CURVE_EDGES];
	uint8_t order[CURVE_EDGES];
	int16_t crosses[CURVE_EDGES];
	int8_t light[CURVE_LIGHT_ROWS * CURVE_LIGHT_STRIDE];
	TerraCurveShade shade;
	uint8_t spans[CURVE_SPAN_BATCH * CURVE_SPAN_BYTES];
};

static fixed SEC_CARD curve_ground_z(const Hole *h, fixed x, fixed y)
{
	uint32_t step = (uint32_t)h->step;
	int32_t hb = hb_sample(h, x, y);
	/* Course records store positive height steps in 1/4096 yard units. */
	if ((uint32_t)hb <= UINT16_MAX && step <= 0xffff0UL && !(step & 15))
		return h->base + (fixed)(MULU16(hb, step >> 4) >> 4);
	return h->base + mulf(hb * 256, h->step);
}

static CurveCameraPoint SEC_CARD curve_camera_point(const GJob *j, const uint8_t *p, uint8_t shift)
{
	const RenderCam *c = &j->c;
	fixed x = (fixed)be16(p) << shift, y = (fixed)be16(p + 2) << shift;
	fixed dx = x - c->camx, dy = y - c->camy;
	fixed along = mulq(dx, c->fx) + mulq(dy, c->fy);
	fixed up = c->zref + (curve_ground_z(j->v.hole, x, y) - c->zref) * c->zk - c->camz;
	CurveCameraPoint a;
	a.side = mulq(dx, c->rx) + mulq(dy, c->ry);
	a.down = -(mulq(along, c->ps) + mulq(up, c->pc));
	a.depth = mulq(along, c->pc) - mulq(up, c->ps);
	return a;
}

static CurveCameraPoint SEC_CARD curve_near(CurveCameraPoint a, CurveCameraPoint b)
{
	int32_t n = FIX_ONE - a.depth, d = b.depth - a.depth;
	int16_t t;

	while (d > 0x1ffffL) {
		n >>= 1;
		d >>= 1;
	}
	t = (int16_t)(n * 16384L / d);
	a.side += mulq(b.side - a.side, t);
	a.down += mulq(b.down - a.down, t);
	a.depth = FIX_ONE;
	return a;
}

static __inline__ int32_t SEC_CARD curve_project_axis(int32_t n, int32_t depth)
{
#ifdef __m68k__
	/* The short divide is enough for projections within 2,048 pixels. */
	if (depth <= 32767 && n >= -(depth << 15) && n < (depth << 15)) {
		int16_t d = (int16_t)depth;
		__asm__ ("divs.w %1,%0" : "+d" (n) : "d" (d) : "cc");
		return (int16_t)n;
	}
#endif
	return n / depth;
}

static CurveScreenPoint SEC_CARD curve_project(const RenderCam *c, CurveCameraPoint a)
{
	CurveScreenPoint p;
	int32_t f = (int32_t)c->f * 16;
	int32_t d = a.depth >> 8;
	p.x = (int32_t)c->cx * 16 + curve_project_axis((a.side >> 8) * f, d);
	p.y = (int32_t)c->yh * 16 + curve_project_axis((a.down >> 8) * f, d);
	return p;
}

static void SEC_CARD curve_edge(PuttingCurves *w, const R *r, CurveScreenPoint a, CurveScreenPoint b)
{
	int32_t lo, hi;
	CurveEdge e;
	uint16_t at;
	if (a.y > b.y) {
		CurveScreenPoint t = a;
		a = b;
		b = t;
	}
	/* A scanline covers pixels whose centres fall between the two ends. */
	lo = (a.y + 7) >> 4;
	hi = (b.y + 7) >> 4;
	if (lo < 0) lo = 0;
	if (hi > r->vh) hi = r->vh;
	if (lo >= hi) return;
	e.step = (b.x - a.x) * 256 / (b.y - a.y);
	e.x = a.x * 16 + (e.step * (lo * 16 + 8 - a.y)) / 16;
	e.lo = (int16_t)lo;
	e.hi = (int16_t)hi;
	if (hi > w->end) w->end = (int16_t)hi;
	at = w->edges++;
	while (at && w->edge[at - 1].lo > lo) {
		w->edge[at] = w->edge[at - 1];
		at--;
	}
	w->edge[at] = e;
}

static void SEC_CARD curve_polygon(GJob *j, const uint8_t *p, uint16_t n, uint8_t shift)
{
	PuttingCurves *w = j->curves;
	const uint8_t *end = p + n * 4;
	CurveCameraPoint a = curve_camera_point(j, end - 4, shift);
	CurveScreenPoint first = {0, 0}, last = {0, 0};
	uint8_t started = 0;
	w->edges = w->end = w->next = w->active = 0;
	for (; p < end; p += 4) {
		CurveCameraPoint b = curve_camera_point(j, p, shift);
		uint8_t ain = a.depth >= FIX_ONE, bin = b.depth >= FIX_ONE;
		if (ain != bin) {
			CurveScreenPoint c = curve_project(&j->c, ain ? curve_near(b, a) : curve_near(a, b));
			if (started) curve_edge(w, &j->r, last, c);
			else first = c;
			last = c;
			started = 1;
		}
		if (bin) {
			CurveScreenPoint c = curve_project(&j->c, b);
			if (started) curve_edge(w, &j->r, last, c);
			else first = c;
			last = c;
			started = 1;
		}
		a = b;
	}
	if (started) curve_edge(w, &j->r, last, first);
	w->y = w->edges ? w->edge[0].lo : 0;
}

/* A ray advances in quarter yards. Split the product into two word
 * multiplies and round toward zero as with signed division. */
static __inline__ fixed SEC_CARD curve_ray_step(fixed delta, uint16_t at)
{
	uint32_t fraction = MULU16(delta, at);
	fixed whole = MULS16(delta >> 16, at) * (FIX_ONE / 4);

	return whole + (fixed)(fraction >> 2) + (delta < 0 && (fraction & 3));
}

static void SEC_BALL curve_light_native(const RenderCam *c, const Hole *h, int8_t *out,
                                       int16_t cols, fixed dx, fixed dy, fixed dz, fixed z)
{
	TerraCurveLight t;

	t.out = out;
	t.heights = h->heights;
	t.camx = c->camx;
	t.camy = c->camy;
	t.z = z;
	t.zref = c->zref;
	t.base = h->base;
	t.dx = dx;
	t.dy = dy;
	t.dz = dz;
	t.f = c->f;
	t.cx = c->cx;
	t.rx = c->rx;
	t.ry = c->ry;
	t.cols = (uint16_t)cols;
	t.step = (uint16_t)(h->step >> 4);
	t.hw = h->hm_w;
	t.hh = h->hm_h;
	t.shift = h->hm_shift;
	terra_fn(TERRA_CURVE_LIGHT, &t);
}

static void SEC_CARD curve_light_row(const GJob *j, int8_t *out, int16_t cols, int16_t y)
{
	const RenderCam *c = &j->c;
	const Hole *h = j->v.hole;
	fixed sy = (int32_t)(y - c->yh) * FIX_ONE / c->f;
	fixed along = (fixed)c->pc * 4 - mulq(sy, c->ps);
	fixed forward_x = mulq(along, c->fx), forward_y = mulq(along, c->fy);
	fixed dz = (-(fixed)c->ps * 4 - mulq(sy, c->pc)) / c->zk;
	fixed z = c->zref + (c->camz - c->zref) / c->zk;
	int16_t x = 0;

	if (terra_fn && (uint32_t)h->step <= 0xffff0UL && !(h->step & 15)) {
		curve_light_native(c, h, out, cols, forward_x, forward_y, dz, z);
		return;
	}
	for (; cols; cols--, out++, x += 1 << CURVE_LIGHT_SHIFT) {
		fixed sx = (int32_t)(x - c->cx) * FIX_ONE / c->f;
		fixed dx = forward_x + mulq(sx, c->rx), dy = forward_y + mulq(sx, c->ry);
		fixed wx, wy, rise, level;
		uint16_t lo = 4, hi = 360;
		int16_t light;

		/* Quarter-yard ray steps bracket the ground, out to 90 yards. */
		while (hi - lo > 1) {
			uint16_t mid = (uint16_t)((lo + hi) / 2);
			wx = c->camx + curve_ray_step(dx, mid);
			wy = c->camy + curve_ray_step(dy, mid);
			if (z + curve_ray_step(dz, mid) > curve_ground_z(h, wx, wy)) lo = mid;
			else hi = mid;
		}
		wx = c->camx + curve_ray_step(dx, hi);
		wy = c->camy + curve_ray_step(dy, hi);
		rise = (curve_ground_z(h, wx + 2 * FIX_ONE, wy) - curve_ground_z(h, wx - 2 * FIX_ONE, wy)) * 3 / 4;
		rise += (curve_ground_z(h, wx, wy + 2 * FIX_ONE) - curve_ground_z(h, wx, wy - 2 * FIX_ONE)) / 2;
		level = curve_ground_z(h, wx, wy) - c->zref;
		light = (int16_t)((level * 2 - rise * 12) >> FIX_SHIFT);
		*out = (int8_t)(light < -20 ? -20 : light > 20 ? 20 : light);
	}
}

static __inline__ int16_t SEC_CARD curve_light_y(const int8_t *p, int16_t yf)
{
	return (int16_t)(p[0] * 16 + MULS16(p[CURVE_LIGHT_STRIDE] - p[0], yf));
}

static __inline__ int16_t SEC_BALL curve_grass_y(const int8_t *near, const int8_t *far,
                                                uint16_t node, int16_t yf)
{
	int16_t a = near[node];
	return (int16_t)(a * 16 + MULS16(far[node] - a, yf));
}

static __inline__ void SEC_CARD curve_packed_edge(uint8_t *p, uint8_t ink, int16_t x, int16_t end)
{
	uint8_t mask = (uint8_t)((255U >> ((x & 3) * 2)) & (255U << (((4 - end) & 3) * 2)));
	*p = (uint8_t)((*p & ~mask) | (ink & mask));
}

static void SEC_CARD curve_flush(PuttingCurves *w)
{
	if (!w->shade.count) return;
	if (terra_fn) terra_fn(TERRA_CURVE, &w->shade);
	else terra_curve_shade(&w->shade);
	w->shade.count = 0;
}

static void SEC_BALL curve_grey_span(GJob *j, uint8_t *row, int16_t x, int16_t end)
{
	PuttingCurves *w = j->curves;
	uint8_t sf = w->material, base = (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N);
	int16_t cell = (int16_t)(x >> CURVE_LIGHT_SHIFT), yf = (int16_t)(w->y & 15);
	const int8_t *samples = w->light + MULU16(w->y >> CURVE_LIGHT_SHIFT, CURVE_LIGHT_STRIDE) + cell;
	uint16_t band = (uint16_t)(w->y >> CURVE_LIGHT_SHIFT);
	const int8_t *near = w->shade.noise + ((band & 15) << 4);
	const int8_t *far = w->shade.noise + (((band + 1) & 15) << 4);
	int16_t mid = sf == SURF_GREEN ? 96 : sf == SURF_FRINGE ? 48 : 64;
	int16_t run = x, a = curve_light_y(samples, yf);
	int grass = sf < SURF_GREEN || sf == SURF_TEE;
	uint8_t ink;

	if (sf == SURF_GREEN) a /= 2;
	if (grass) a += curve_grass_y(near, far, cell & 15, yf);
	if (x & 15) {
		int16_t b = curve_light_y(samples + 1, yf);
		if (sf == SURF_GREEN) b /= 2;
		if (grass) b += curve_grass_y(near, far, (cell + 1) & 15, yf);
		/* Only the first cell can start partway across. */
		a = (int16_t)((a * 16 + MULS16(b - a, x & 15)) >> 4);
	}
	ink = (uint8_t)(base + ((int16_t)(mid * 16 + a) >> 8));
	for (x = (int16_t)((cell + 1) << CURVE_LIGHT_SHIFT); x < end; x += 1 << CURVE_LIGHT_SHIFT) {
		uint8_t shade;
		a = curve_light_y(++samples, yf);
		if (sf == SURF_GREEN) a /= 2;
		if (grass) a += curve_grass_y(near, far, ++cell & 15, yf);
		shade = (uint8_t)(base + ((int16_t)(mid * 16 + a) >> 8));
		if (shade != ink) {
			terra_grey_span(row, pal_pat2, pal_grey, bayer, j->r.lg, ink, run, x, w->y);
			run = x;
			ink = shade;
		}
	}
	terra_grey_span(row, pal_pat2, pal_grey, bayer, j->r.lg, ink, run, end, w->y);
}

static void SEC_BALL curve_span(GJob *j, int16_t x, int16_t end)
{
	PuttingCurves *w = j->curves;
	const R *r = &j->r;
	uint8_t *row = r->bits + MULU16(w->y, r->rb);
	uint8_t sf = w->material, base = (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N);
	int16_t mid = sf == SURF_FRINGE ? 48 : 64;
	if (x >= end) return;
	if (r->lg == 3) {
		uint8_t *cmd = w->spans + w->shade.count * CURVE_SPAN_BYTES;
		cmd[0] = (uint8_t)(w->y >> 8);
		cmd[1] = (uint8_t)w->y;
		cmd[2] = (uint8_t)(x >> 8);
		cmd[3] = (uint8_t)x;
		cmd[4] = (uint8_t)(end >> 8);
		cmd[5] = (uint8_t)end;
		cmd[6] = sf;
		if (++w->shade.count == CURVE_SPAN_BATCH) curve_flush(w);
		return;
	}
	if (r->lg != 1) {
		curve_grey_span(j, row, x, end);
		return;
	}
	if (sf == SURF_WATER) {
		terra_water_packed(row, w->shade.noise, x, end, w->y);
		return;
	}
	{
		int16_t cell = (int16_t)(x >> CURVE_LIGHT_SHIFT), yf = (int16_t)(w->y & 15);
		const int8_t *samples = w->light + MULU16(w->y >> CURVE_LIGHT_SHIFT, CURVE_LIGHT_STRIDE) + cell;
		const uint8_t *pattern = pal_pat2 + ((uint16_t)base << 2) + (w->y & 3);
		int16_t a = curve_light_y(samples, yf);
		int green = sf == SURF_GREEN;
		int grass = sf < SURF_GREEN || sf == SURF_TEE;
		int16_t mid_light;
		uint16_t band = (uint16_t)(w->y >> 4);
		const int8_t *near = w->shade.noise + ((band & 15) << 4);
		const int8_t *far = w->shade.noise + (((band + 1) & 15) << 4);
		if (grass) a += curve_grass_y(near, far, cell & 15, yf);
		if (green) {
			mid = 96;
			a /= 2;
		}
		mid_light = (int16_t)(mid * 256);
		while (x < end) {
			int16_t stop = (int16_t)((cell + 1) << CURVE_LIGHT_SHIFT);
			int16_t b = curve_light_y(++samples, yf), light, step;
			uint8_t *p = row + (x >> 2);
			if (stop > end) stop = end;
			if (green) b /= 2;
			if (grass) {
				uint16_t node = (uint16_t)((cell + 1) & 15);
				b += curve_grass_y(near, far, node, yf);
			}
			step = (int16_t)(b - a);
			light = (int16_t)(mid_light + a * 16);
			if (x & 15) light = (int16_t)(light + MULS16(step, x & 15));
			if (x & 3) {
				int16_t last = (int16_t)((x + 4) & ~3);
				if (last > stop) last = stop;
				curve_packed_edge(p++, pattern[(light >> 12) * 4], x, last);
				light += step * (last - x);
				x = last;
			}
			step *= 4;
			for (; x + 4 <= stop; x += 4, light += step)
				*p++ = pattern[(light >> 12) * 4];
			if (x < stop) curve_packed_edge(p, pattern[(light >> 12) * 4], x, stop);
			x = stop;
			cell++;
			a = b;
		}
	}
}

static void SEC_CARD curve_track_green(int16_t *row, int16_t lo, int16_t hi, int green)
{
	int16_t runs[2 * (CURVE_GREEN_RUNS + 1)];
	uint8_t i, n = 0, added = 0;
	if (row[0] > 2 * CURVE_GREEN_RUNS) return;
	for (i = 1; i <= row[0]; i += 2) {
		int16_t a = row[i], b = row[i + 1];
		if (green) {
			if (hi < a && !added) {
				runs[n++] = lo;
				runs[n++] = hi;
				added = 1;
			}
			if (lo <= b && hi >= a) {
				if (a < lo) lo = a;
				if (b > hi) hi = b;
				continue;
			}
		} else if (lo < b && hi > a) {
			if (a < lo) {
				runs[n++] = a;
				runs[n++] = lo;
			}
			if (b > hi) {
				runs[n++] = hi;
				runs[n++] = b;
			}
			continue;
		}
		runs[n++] = a;
		runs[n++] = b;
	}
	if (green && !added) {
		runs[n++] = lo;
		runs[n++] = hi;
	}
	row[0] = n;
	if (n <= 2 * CURVE_GREEN_RUNS)
		for (i = 0; i < n; i++) row[i + 1] = runs[i];
}

static void SEC_CARD curve_fill_row(GJob *j)
{
	PuttingCurves *w = j->curves;
	uint16_t i, kept = 0, count = 0;
	while (w->next < w->edges && w->edge[w->next].lo == w->y)
		w->order[w->active++] = (uint8_t)w->next++;
	for (i = 0; i < w->active; i++) {
		uint8_t at = w->order[i];
		CurveEdge *e = &w->edge[at];
		int32_t x;
		uint16_t n;
		if (w->y >= e->hi) continue;
		w->order[kept++] = at;
		x = (e->x + 127) >> 8;
		e->x += e->step;
		if (x < 0) x = 0;
		if (x > j->r.w) x = j->r.w;
		n = count++;
		while (n && w->crosses[n - 1] > x) {
			w->crosses[n] = w->crosses[n - 1];
			n--;
		}
		w->crosses[n] = (int16_t)x;
	}
	w->active = kept;
	for (i = 0; i + 1 < count; i += 2) {
		int16_t lo = w->crosses[i], hi = w->crosses[i + 1];
		if (lo == hi) continue;
		if (w->green_rows)
			curve_track_green(w->green_rows + MULU16(w->y, CURVE_GREEN_STRIDE), lo, hi, w->material == SURF_GREEN);
		curve_span(j, lo, hi);
	}
}

static void SEC_CARD curve_green_bounds(PuttingCurves *w, const R *r)
{
	const int16_t *row = w->green_rows;
	int16_t y;

	w->green_left = r->w;
	w->green_top = r->vh;
	w->green_right = w->green_bottom = 0;
	for (y = 0; y < r->vh; y++, row += CURVE_GREEN_STRIDE) {
		uint8_t n = row[0];
		if (!n || n > 2 * CURVE_GREEN_RUNS) continue;
		if (w->green_left > row[1]) w->green_left = row[1];
		if (w->green_right < row[n]) w->green_right = row[n];
		if (w->green_top > y) w->green_top = y;
		w->green_bottom = (int16_t)(y + 1);
	}
}

static int SEC_CARD curve_covers_hazard(const Hole *h, uint8_t index)
{
	const SurfaceBounds *shape = &h->shape_bounds[index];
	uint8_t n;

	for (n = 0; n < h->hazard_count && h->hazard_shapes[n] < index; n++) {
		const SurfaceBounds *hazard = &h->shape_bounds[h->hazard_shapes[n]];
		if (shape->x0 <= hazard->x1 && hazard->x0 <= shape->x1 &&
		    shape->y0 <= hazard->y1 && hazard->y0 <= shape->y1) return 1;
	}
	return 0;
}

static int SEC_CARD curve_surfaces(GJob *j, RenderClock clk, uint32_t until)
{
	const Hole *h = j->v.hole;
	PuttingCurves *w = j->curves;
	if (!w->phase) {
		int16_t rows = (int16_t)((j->r.vh - 1) >> CURVE_LIGHT_SHIFT) + 2;
		int16_t cols = (int16_t)((j->r.w - 1) >> CURVE_LIGHT_SHIFT) + 2;
		while (w->light_row < rows) {
			curve_light_row(j, w->light + w->light_row * CURVE_LIGHT_STRIDE,
			                cols, (int16_t)(w->light_row << CURVE_LIGHT_SHIFT));
			w->light_row++;
			if (late(clk, until)) return 0;
		}
		w->phase = 1;
	}
	if (w->phase == 1) {
		while (w->y < j->r.vh) {
			if (w->green_rows)
				w->green_rows[MULU16(w->y, CURVE_GREEN_STRIDE)] = w->material == SURF_GREEN ? 2 : 0;
			if (w->green_rows && w->material == SURF_GREEN) {
				int16_t *row = w->green_rows + MULU16(w->y, CURVE_GREEN_STRIDE);
				row[1] = 0;
				row[2] = j->r.w;
			}
			curve_span(j, 0, j->r.w);
			w->y++;
			if (!(w->y & 7) && late(clk, until)) return 0;
		}
		w->phase = 2;
	}
	while (w->phase == 2) {
		while (w->y < w->end) {
			curve_fill_row(j);
			w->y++;
			if (!(w->y & 7) && late(clk, until)) return 0;
		}
		if (w->shape_index == h->shape_count) {
			curve_flush(w);
			if (w->green_rows) curve_green_bounds(w, &j->r);
			w->phase = 3;
			break;
		}
		w->material = w->shape[0];
		if (w->remaining && w->contour[0] == w->shape_index) {
			uint16_t n = be16(w->contour + 1);
			curve_polygon(j, w->contour + 3, n, 12);
			w->contour += 3 + n * 4;
			w->remaining--;
		} else if ((w->material >= SURF_GREEN && w->material <= SURF_WATER) ||
		           (h->base_surface >= SURF_GREEN && h->base_surface <= SURF_WATER) ||
		           curve_covers_hazard(h, w->shape_index)) {
			curve_polygon(j, w->shape + 2, w->shape[1], 14);
		}
		w->shape += 2 + w->shape[1] * 4;
		w->shape_index++;
		if (late(clk, until)) return 0;
	}
	if (edge_blend_enabled(j->r.lg == 3) &&
	    !blend_edges(&j->r, (uint8_t *)(w + 1), -1, &w->edge_row, clk, until))
		return 0;
	return 1;
}

static void SEC_CARD curve_guide(const GJob *j, const GPt *a, const GPt *b)
{
	const R *r = &j->r;
	const PuttingCurves *w = j->curves;
	const int16_t *runs = w->green_rows;
	int16_t x = a->x, y = a->y;
	int16_t dx = b->x > x ? b->x - x : x - b->x, dy = b->y > y ? b->y - y : y - b->y;
	int16_t sx = b->x > x ? 1 : -1, sy = b->y > y ? 1 : -1, e = dx - dy;
	int16_t steps = (int16_t)((dx >= dy ? dx : dy) + 1);

	if (runs) {
		int16_t lo, hi;
		if (w->green_top >= w->green_bottom ||
		    (x < w->green_left && b->x < w->green_left) ||
		    (x >= w->green_right && b->x >= w->green_right) ||
		    (y < w->green_top && b->y < w->green_top) ||
		    (y >= w->green_bottom && b->y >= w->green_bottom)) return;
		/* Skip along the major axis, keeping Bresenham's original tie rule. */
		if (dx >= dy) {
			lo = (int16_t)(sx > 0 ? w->green_left - x : x - w->green_right + 1);
			hi = (int16_t)(sx > 0 ? w->green_right - x : x - w->green_left + 1);
			if (lo > 0) {
				int16_t rise = (int16_t)((MULU16(lo, dy) + (dx - 1) / 2) / (uint16_t)dx);
				x += sx * lo;
				y += sy * rise;
				e = (int16_t)(e - MULS16(dy, lo) + MULS16(dx, rise));
			}
		} else {
			lo = (int16_t)(sy > 0 ? w->green_top - y : y - w->green_bottom + 1);
			hi = (int16_t)(sy > 0 ? w->green_bottom - y : y - w->green_top + 1);
			if (lo > 0) {
				int16_t run = (int16_t)((MULU16(lo, dx) + (dy - 1) / 2) / (uint16_t)dy);
				x += sx * run;
				y += sy * lo;
				e = (int16_t)(e + MULS16(dx, lo) - MULS16(dy, run));
			}
		}
		if (steps > hi) steps = hi;
		if (lo > 0) steps -= lo;
	}
	for (; steps > 0; steps--) {
		int16_t e2;
		if (x >= 0 && x < r->w && y >= 0 && y < r->vh) {
			if (runs) {
				const int16_t *row = runs + MULU16(y, CURVE_GREEN_STRIDE);
				uint8_t i;
				if (row[0] <= 2 * CURVE_GREEN_RUNS)
					for (i = 1; i <= row[0]; i += 2)
						if (x >= row[i] && x < row[i + 1]) {
							int16_t along = dx >= dy ? x : y;
							if (r->lg == 1) {
								uint8_t *p = r->bits + MULU16(y, r->rb) + (x >> 2);
								guide_2bit(p, x, along);
							} else if (!(along & 1)) plot(r, x, y, j->guide, 0);
							break;
						}
			} else {
				uint8_t *p = r->bits + MULU16(y, r->rb) + x;
				if (*p >= PAL_GREEN && *p < PAL_GREEN + PAL_RAMP_N) *p = j->guide;
			}
		}
		e2 = (int16_t)(2 * e);
		if (e2 > -dy) { e -= dy; x += sx; }
		if (e2 < dx) { e += dx; y += sy; }
	}
}

uint32_t SEC_CARD render_green_curves_size(const Surface *s, const RenderView *v)
{
	Surface half;
	int16_t h;
	const Surface *draw = half_of(s, &half) ? &half : s;

	if (!v->green || !v->hole->putting_curves || fit(draw, &h) < 0) return 0;
	return sizeof(GJob) + sizeof(PuttingCurves) +
	       (draw->depth < 8 ? (uint32_t)sizeof(int16_t) * MULU16(h, CURVE_GREEN_STRIDE) :
	        edge_blend_enabled(draw->depth == 8) ? MULU16(draw->w, EDGE_MARK_ROWS) : 0);
}

int SEC_CARD render_green_curves_begin(Surface *s, const RenderView *v, void *work)
{
	GJob *j = work;
	PuttingCurves *w;
	if (!work || !render_green_curves_size(s, v) || green_begin(s, v, work, 1) < 0) return -1;
	j->curves = w = (PuttingCurves *)(j + 1);
	w->contour = v->hole->putting_curves + 1;
	w->remaining = v->hole->putting_curves[0];
	w->shape = v->hole->shapes;
	w->shape_index = w->phase = 0;
	w->material = v->hole->base_surface;
	if (w->material < SURF_GREEN || w->material > SURF_WATER) w->material = SURF_ROUGH;
	w->y = w->end = w->light_row = w->edge_row = 0;
	w->shade.bits = j->r.bits;
	w->shade.light = w->light;
	w->shade.bayer = bayer;
	w->shade.spans = w->spans;
	w->shade.rb = j->r.rb;
	w->shade.count = 0;
	w->shade.noise = pal_noise;
	w->green_rows = j->r.lg < 3 ? (int16_t *)(w + 1) : 0;
	if (s->depth == 8) j->guide = PAL_GREEN + 6;
	return 0;
}
