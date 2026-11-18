/* The view from behind the ball (render.h).
 *
 * Camera: CAM_BACK yards behind the ball along the aim line and CAM_UP above
 * it, looking level with the view shifted down so the horizon sits at row
 * HORIZON (no pitch, so verticals stay vertical). That puts the ball about
 * 18 degrees below the eye, low in the middle column, and makes the golfer
 * at the ball 20 px tall on 160 wide screens, the size of the sprite.
 *
 * Terrain is marched front to back in depth steps, every column at each
 * step, with a y-buffer per column (voxel-space style). Each sample fills
 * from its row down to the last row drawn in that column, so the first
 * sample on screen fills the bottom with no gap. Then the sky and backdrop
 * go in above the terrain, then the objects back to front, each clipped per
 * column by the y-buffer as it was when the march passed its depth, then the
 * golfer and the ball.
 *
 * Colours are master palette indices (pal.h). A pixel goes through put():
 * 8 bits write the index, 2 bits a pattern per index made for the IIIx
 * greys, 4 and 1 bits an ordered dither of the index's grey.
 *
 * Speed, for the 68000: heights and light use 16x16 multiplies only, and
 * there's one divide per depth step and a few per object, none per pixel.
 * Big tables are const (paltab.h) and sprites stay in their resources. */
#include "render.h"
#include "pal.h"
#include "paltab.h"

#define CAM_BACK   (14L << 16)        /* yards behind the ball */
#define CAM_UP     (9L << 15)         /* 4.5 yards above it */
#define FOCAL      140                /* pixels per unit of x / depth on 160 wide screens */
#define HORIZON    62                 /* horizon row on 160 wide screens */
#define Z_NEAR     (2L << 16)
#define Z_FAR      (800L << 16)
#define OBJ_NEAR   (4L << 16)         /* objects nearer than this are skipped */
#define MAX_OBJ    48
#define POOL_COLS  8                  /* clip columns kept per screen column */
#define BKD_MAX    1024               /* widest backdrop row */
#define LIGHT_FLAT 62                 /* ramp position of flat ground, 16 per level */

/* Object heights in 1/16 yards at scale 64, by type. */
static const uint16_t obj_h16[RENDER_TYPES] = {
	0, 192, 224, 208, 35, 21, 128, 80, 24
};

static const uint8_t bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

typedef struct {
	fixed depth;
	const uint8_t *px;          /* sprite pixels, 0 for the pin */
	int16_t x0, y0, dw, dh;     /* scaled sprite on screen, may run off it */
	uint16_t sw, sh;            /* source size; for the pin, flag height in sh */
	int16_t c0, c1;             /* screen columns it covers */
	uint16_t clip;              /* offset into the pool, NO_CLIP if none */
} RObj;

#define NO_CLIP 0xFFFF

typedef struct {
	uint8_t *bits;
	uint16_t rb;
	int16_t w, vh, yh, f, cx;
	uint8_t lg, scale;
	const Hole *h;
	const SurfaceGrid *g;
	fixed camx, camy;
	int32_t camb;               /* camera height in heightmap byte steps, 8.8 */
	int16_t fx, fy, rx, ry;     /* forward and right, 2.14 */
	uint32_t stepf;             /* yards per byte step (16.16) times the focal length */
	int16_t *ybuf;
	uint8_t *prev;              /* surface of the last sample in each column */
	RObj *obj;
	int16_t *pool;
	uint8_t *row;
	uint16_t pool_used, pool_size;
	uint8_t nobj;
	RenderStats *st;
} R;

/* a * q / 16384 for a 2.14 q, as fix_mulq14 but in this segment. */
static fixed SEC_RENDER mulq(fixed a, int16_t q)
{
	uint32_t ua, r;
	uint16_t uq;
	int neg = 0;

	if (a < 0) { ua = 0u - (uint32_t)a; neg = 1; } else ua = (uint32_t)a;
	if (q < 0) { uq = (uint16_t)-q; neg ^= 1; } else uq = (uint16_t)q;
	r = (MULU16(ua >> 16, uq) << 2) + (MULU16(ua, uq) >> 14);
	return neg ? -(fixed)r : (fixed)r;
}

/* k = q / d as m >> sh with m under 2^15, for projecting heights at a depth:
 * rows below the horizon = (byte steps * 64) * m >> sh. */
static void SEC_RENDER kz(R *r, uint32_t q, uint32_t d, int16_t *m, uint8_t *sh)
{
	int16_t s = 6;
	uint32_t v;

	r->st->divides++;
	if (!q || !d) { *m = 0; *sh = 0; return; }
	while (q < 0x40000000UL) { q <<= 1; s++; }
	while (d >= 0x20000UL) { d >>= 1; s++; }
	while (d < 0x10000UL) { d <<= 1; s--; }
	v = (q / d) >> 1;          /* 2^12 to 2^14 */
	s--;
	while (s > 30) { v >>= 1; s--; }
	if (s < 0) { v = 16383; s = 0; }
	*m = (int16_t)v;
	*sh = (uint8_t)s;
}

/* Ground height at (x, y) in byte steps, 8.8, bilinear and held level past the edge. */
static int32_t SEC_RENDER hb_at(const Hole *h, fixed x, fixed y)
{
	fixed gx = x >> h->hm_shift, gy = y >> h->hm_shift;
	fixed mx = (fixed)(h->hm_w - 1) << 16, my = (fixed)(h->hm_h - 1) << 16;
	uint16_t i, j, i1, j1, fx, fy, hw = h->hm_w;    /* 16 bits, or MULU16 isn't one mulu.w */
	const uint8_t *r0, *r1;
	int32_t t, b;

	if (gx < 0) gx = 0;
	if (gx > mx) gx = mx;
	if (gy < 0) gy = 0;
	if (gy > my) gy = my;
	i = (uint16_t)(gx >> 16); fx = (uint16_t)((gx >> 8) & 255);
	j = (uint16_t)(gy >> 16); fy = (uint16_t)((gy >> 8) & 255);
	i1 = i + 1 < h->hm_w ? i + 1 : i;
	j1 = j + 1 < h->hm_h ? j + 1 : j;
	r0 = h->heights + MULU16(j, hw);
	r1 = h->heights + MULU16(j1, hw);
	t = ((int32_t)r0[i] << 8) + MULS16(r0[i1] - r0[i], fx);
	b = ((int32_t)r1[i] << 8) + MULS16(r1[i1] - r1[i], fx);
	return t + (MULS16((b - t) >> 1, fy) >> 7);
}

/* Camera height minus a height, both 8.8 byte steps, as 1/64 steps clamped to 16 bits. */
static inline int16_t SEC_RENDER dz6(int32_t camb, int32_t hb)
{
	int32_t d = (camb - hb) >> 2;
	if (d > 32767) d = 32767;
	if (d < -32768) d = -32768;
	return (int16_t)d;
}

/* Writes palette index c at (x, y); p points at the byte holding the pixel. */
static void SEC_RENDER put(const R *r, uint8_t *p, int16_t x, int16_t y, uint8_t c)
{
	uint8_t m, v;
	uint16_t g;

	switch (r->lg) {
	case 3:
		*p = c;
		break;
	case 1:
		m = (uint8_t)(0xC0 >> ((x & 3) << 1));
		*p = (uint8_t)((*p & ~m) | (pal_pat2[((uint16_t)c << 2) | (y & 3)] & m));
		break;
	case 2:
		g = (uint16_t)(255 - pal_grey[c]);
		v = (uint8_t)((MULU16(g, 15) + (bayer[((y & 3) << 2) | (x & 3)] << 4)) >> 8);
		if (x & 1) *p = (uint8_t)((*p & 0xF0) | v);
		else *p = (uint8_t)((*p & 0x0F) | (v << 4));
		break;
	default:
		g = (uint16_t)(255 - pal_grey[c]);
		m = (uint8_t)(0x80 >> (x & 7));
		if (g > (bayer[((y & 3) << 2) | (x & 3)] << 4) + 8) *p |= m;
		else *p &= (uint8_t)~m;
		break;
	}
}

static uint8_t *SEC_RENDER pixp(const R *r, int16_t x, int16_t y)
{
	return r->bits + MULU16(y, r->rb) + ((uint16_t)x >> (3 - r->lg));
}

static uint16_t SEC_RENDER be16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

/* Checks a Spr8 resource and returns its pixels, or 0. */
static const uint8_t *SEC_RENDER sprite(const RenderData *d, uint16_t *w, uint16_t *h)
{
	if (!d->p || d->len < 12) return 0;
	*w = be16(d->p);
	*h = be16(d->p + 2);
	if (!*w || !*h || d->len < 12 + (uint32_t)MULU16(*w, *h)) return 0;
	return d->p + 12;
}

/* Takes the y-buffer under object k as its clip, if the pool has room. */
static void SEC_RENDER snap(R *r, RObj *o)
{
	int16_t x, n = o->c1 - o->c0;
	if (n <= 0) return;
	if (r->pool_used + n > r->pool_size) { r->st->dropped++; return; }
	o->clip = r->pool_used;
	for (x = 0; x < n; x++) r->pool[o->clip + x] = r->ybuf[o->c0 + x];
	r->pool_used += n;
}

/* Puts an object in the list, nearest first, keeping the MAX_OBJ nearest. */
static void SEC_RENDER add_obj(R *r, const RObj *o)
{
	uint8_t k;
	if (r->nobj == MAX_OBJ) {
		r->st->dropped++;
		if (o->depth >= r->obj[MAX_OBJ - 1].depth) return;
		r->nobj--;
	}
	k = r->nobj++;
	while (k > 0 && r->obj[k - 1].depth > o->depth) {
		r->obj[k] = r->obj[k - 1];
		k--;
	}
	r->obj[k] = *o;
}

/* Camera-space depth and screen column of a point; returns 0 if it's too near or far off the side. */
static int SEC_RENDER project(const R *r, vec2 p, fixed *depth, int16_t *sx)
{
	fixed dx = p.x - r->camx, dy = p.y - r->camy, side;
	int32_t x;

	*depth = mulq(dx, r->fx) + mulq(dy, r->fy);
	side = mulq(dx, r->rx) + mulq(dy, r->ry);
	if (*depth < OBJ_NEAR || *depth > Z_FAR) return 0;
	x = (side >> 8) * r->f / (*depth >> 8);
	r->st->divides++;
	if (x < -4 * r->w || x > 4 * r->w) return 0;
	*sx = (int16_t)(r->cx + x);
	return 1;
}

/* Screen row of the ground at p, seen at depth. */
static int16_t SEC_RENDER ground_row(R *r, vec2 p, fixed depth)
{
	int16_t m;
	uint8_t sh;
	int32_t y;

	kz(r, r->stepf, (uint32_t)depth, &m, &sh);
	y = r->yh + (MULS16(dz6(r->camb, hb_at(r->h, p.x, p.y)), m) >> sh);
	if (y > 32000) y = 32000;
	if (y < -32000) y = -32000;
	return (int16_t)y;
}

/* Projects the hole's objects and the pin, keeping the ones in view. */
static void SEC_RENDER gather(R *r, const RenderView *v, const RenderArt *a)
{
	const Hole *h = r->h;
	HoleObject ho;
	RObj o;
	uint16_t i, sw, sh, sw2, sh2;
	int16_t sx, base;
	const uint8_t *px, *px2;
	int32_t hp;

	for (i = 0; i < h->object_count; i++) {
		hole_object(h, i, &ho);
		if (ho.type >= RENDER_TYPES || !obj_h16[ho.type]) continue;
		px = sprite(&a->spr[ho.type][0], &sw, &sh);
		px2 = sprite(&a->spr[ho.type][1], &sw2, &sh2);
		if (!px && !px2) continue;
		if (!project(r, ho.pos, &o.depth, &sx)) continue;
		/* height in pixels: 1/16 yards times focal over depth in 1/16 yards */
		hp = (int32_t)((MULU16(obj_h16[ho.type], ho.scale) >> 6) * r->f) / (o.depth >> 12);
		r->st->divides++;
		if (hp < 2 || hp > 4 * r->vh) continue;
		if (px2 && (!px || hp > sh + (sh >> 2))) {
			px = px2;
			sw = sw2;
			sh = sh2;
		}
		o.px = px;
		o.sw = sw;
		o.sh = sh;
		o.dh = (int16_t)hp;
		o.dw = (int16_t)((int32_t)sw * hp / sh);
		if (o.dw < 1) o.dw = 1;
		{
			const uint8_t *hd = px - 12;
			int16_t ax = (int16_t)((int32_t)be16(hd + 4) * hp / sh);
			int16_t ay = (int16_t)((int32_t)be16(hd + 6) * hp / sh);
			r->st->divides += 3;
			base = ground_row(r, ho.pos, o.depth);
			o.x0 = sx - ax;
			o.y0 = base - ay;
		}
		o.c0 = o.x0 < 0 ? 0 : o.x0;
		o.c1 = o.x0 + o.dw > r->w ? r->w : o.x0 + o.dw;
		if (o.c0 >= o.c1 || o.y0 >= r->vh || o.y0 + o.dh <= 0) continue;
		o.clip = NO_CLIP;
		add_obj(r, &o);
	}

	/* The pin: a pole 2.4 yards tall with a flag 0.7 by 0.45 yards at the top, never too small to see. */
	if (project(r, v->hole->pins[v->pin & 3], &o.depth, &sx)) {
		int16_t pw = r->scale, fh, fw;
		hp = (int32_t)(38 * r->f) / (o.depth >> 12);
		fw = (int16_t)((int32_t)(11 * r->f) / (o.depth >> 12));
		fh = (int16_t)((int32_t)(7 * r->f) / (o.depth >> 12));
		r->st->divides += 3;
		if (hp < 4 * r->scale) hp = 4 * r->scale;
		if (hp > 2 * r->vh) hp = 2 * r->vh;
		if (fw < 3 * r->scale) fw = 3 * r->scale;
		if (fh < 2 * r->scale) fh = 2 * r->scale;
		if (fw > r->w) fw = r->w;
		if (fh > r->vh) fh = r->vh;
		base = ground_row(r, v->hole->pins[v->pin & 3], o.depth);
		o.px = 0;
		o.x0 = sx;
		o.y0 = (int16_t)(base - hp);
		o.dh = (int16_t)hp;
		o.dw = pw + fw;
		o.sw = pw;
		o.sh = (uint16_t)fh;
		o.c0 = o.x0 < 0 ? 0 : o.x0;
		o.c1 = o.x0 + o.dw > r->w ? r->w : o.x0 + o.dw;
		o.clip = NO_CLIP;
		if (o.c0 < o.c1 && o.y0 < r->vh) add_obj(r, &o);
	}
}

/* Where the march can start: the depth at which the highest ground within
 * 12 yards of the camera would reach the bottom row. Nothing nearer shows. */
static fixed SEC_RENDER near_start(const R *r)
{
	const Hole *h = r->h;
	int32_t i0 = (r->camx - (12L << 16)) >> (16 + h->hm_shift), i1 = ((r->camx + (12L << 16)) >> (16 + h->hm_shift)) + 1;
	int32_t j0 = (r->camy - (12L << 16)) >> (16 + h->hm_shift), j1 = ((r->camy + (12L << 16)) >> (16 + h->hm_shift)) + 1;
	int32_t i, j, diff;
	uint8_t top = 0;
	fixed z;

	if (i0 < 0) i0 = 0;
	if (j0 < 0) j0 = 0;
	if (i1 > h->hm_w - 1) i1 = h->hm_w - 1;
	if (j1 > h->hm_h - 1) j1 = h->hm_h - 1;
	if (i0 > i1) i0 = i1;
	if (j0 > j1) j0 = j1;
	for (j = j0; j <= j1; j++)
		for (i = i0; i <= i1; i++)
			if (h->heights[j * h->hm_w + i] > top) top = h->heights[j * h->hm_w + i];
	diff = r->camb - ((int32_t)top << 8);
	if (diff <= 0) return Z_NEAR;
	if (diff > 0x7FFFFF) diff = 0x7FFFFF;
	/* yards below the camera, times the focal length over the rows below the horizon, less a little */
	z = fix_mul(diff << 8, h->step) * r->f / (r->vh - r->yh);
	z -= z >> 4;
	if (z < Z_NEAR) z = Z_NEAR;
	if (z > (10L << 16)) z = 10L << 16;
	return z;
}

/* The terrain, front to back. */
static void SEC_RENDER terrain(R *r)
{
	const Hole *h = r->h;
	const SurfaceGrid *g = r->g;
	fixed z = near_start(r), mx = (fixed)(h->hm_w - 1) << 16, my = (fixed)(h->hm_h - 1) << 16;
	uint8_t on = 0, dith = r->lg == 3;
	uint16_t hw = h->hm_w, hh = h->hm_h;    /* 16 bits, or MULU16 isn't one mulu.w */
	int16_t ak, bk, sand_drop, water_drop, stripe = dith ? 12 : 18;
	uint16_t step = 0;

	/* Slope light: sun from the left, up and behind the camera, (-0.55, 0.62, -0.56)
	 * in (right, up, forward). Light = 0.62 + 0.55 * rise to the right + 0.56 * rise
	 * ahead, times 4 so gentle shapes show, in ramp steps of 16. Gradients come
	 * in 1/16 byte steps per heightmap cell. */
	{
		int32_t ca = (MULS16(r->rx, 9011) + MULS16(r->fx, 9175)) >> 14;
		int32_t cb = (MULS16(r->ry, 9011) + MULS16(r->fy, 9175)) >> 14;
		/* 2.14 times 140 levels per unit light times 4 times 256 / 16384 */
		ca = fix_mul((fixed)((ca * 35) >> 2), h->step) >> h->hm_shift;
		cb = fix_mul((fixed)((cb * 35) >> 2), h->step) >> h->hm_shift;
		ak = (int16_t)(ca > 32767 ? 32767 : ca < -32767 ? -32767 : ca);
		bk = (int16_t)(cb > 32767 ? 32767 : cb < -32767 ? -32767 : cb);
		/* bunkers sit 0.4 yards down and water 0.25, for their lips */
		ca = fix_div(26214, h->step) >> 8;
		cb = fix_div(16384, h->step) >> 8;
		sand_drop = (int16_t)(ca > 30000 ? 30000 : ca);
		water_drop = (int16_t)(cb > 30000 ? 30000 : cb);
	}

	while (z < Z_FAR) {
		fixed zr, dpx, dpy, px, py, fz;
		int16_t m, x, hz, ns, ymax;
		uint8_t sh;

		while (on < r->nobj && r->obj[on].depth <= z) snap(r, &r->obj[on++]);

		kz(r, r->stepf, (uint32_t)z, &m, &sh);
		zr = z / r->f;
		r->st->divides++;
		dpx = mulq(zr, r->rx);
		dpy = mulq(zr, r->ry);
		px = r->camx + mulq(z, r->fx) - dpx * r->cx + (dpx >> 1);
		py = r->camy + mulq(z, r->fy) - dpy * r->cx + (dpy >> 1);
		fz = z >> 16;
		hz = fz < 140 ? 0 : fz > 620 ? 12 : (int16_t)((fz - 140) / 40);
		ns = fz < 10 ? 0 : fz < 24 ? 1 : fz < 48 ? 2 : 8;
		ymax = 0;
		r->st->steps++;
		r->st->samples += r->w;

		for (x = 0; x < r->w; x++, px += dpx, py += dpy) {
			fixed gx = px >> h->hm_shift, gy = py >> h->hm_shift;
			uint16_t i, j, i1, j1, fx, fy;
			const uint8_t *r0, *r1;
			int32_t t, b, hb, y;
			uint8_t surf, prev;
			int16_t yb = r->ybuf[x];

			if (yb > ymax) ymax = yb;
			if (gx < 0) gx = 0;
			if (gx > mx) gx = mx;
			if (gy < 0) gy = 0;
			if (gy > my) gy = my;
			i = (uint16_t)(gx >> 16); fx = (uint16_t)((gx >> 8) & 255);
			j = (uint16_t)(gy >> 16); fy = (uint16_t)((gy >> 8) & 255);
			i1 = i + 1 < hw ? i + 1 : i;
			j1 = j + 1 < hh ? j + 1 : j;
			r0 = h->heights + MULU16(j, hw);
			r1 = h->heights + MULU16(j1, hw);
			t = ((int32_t)r0[i] << 8) + MULS16(r0[i1] - r0[i], fx);
			b = ((int32_t)r1[i] << 8) + MULS16(r1[i1] - r1[i], fx);
			hb = t + (MULS16((b - t) >> 1, fy) >> 7);

			/* Surface, with the lookup shaken by up to half a yard near the camera so cell edges break up. */
			{
				fixed sx = px, sy = py;
				int32_t cx, cy;
				if (fz < 48) {
					sx += (fixed)pal_noise[(x * 7 + step * 13) & 255] * 4096;
					sy += (fixed)pal_noise[(x * 11 + step * 5 + 101) & 255] * 4096;
				}
				cx = sx >> (16 + SURF_CELL_SHIFT);
				cy = sy >> (16 + SURF_CELL_SHIFT);
				if (sx < 0 || sy < 0 || cx >= g->w || cy >= g->h) {
					surf = SURF_OOB;
				} else {
					uint8_t c = g->cells[MULU16(cy, g->stride) + (cx >> 1)];
					surf = (cx & 1) ? (c & 15) : (c >> 4);
					if (surf >= SURF_COUNT) surf = SURF_OOB;
				}
			}
			prev = r->prev[x];
			r->prev[x] = surf;
			if (surf == SURF_BUNKER) hb -= sand_drop;
			else if (surf == SURF_WATER) hb -= water_drop;

			y = r->yh + (MULS16(dz6(r->camb, hb), m) >> sh);
			if (y >= yb) continue;
			if (y < 0) y = 0;

			{
				int16_t lv, gxs, gys, yy;
				uint8_t base = (uint8_t)(PAL_TERRAIN + surf * PAL_RAMP_N), *p;
				int16_t sx0 = r0[i1] - r0[i], sx1 = r1[i1] - r1[i];

				gxs = (int16_t)(sx0 * 16 + (MULS16(sx1 - sx0, fy) >> 4));
				gys = (int16_t)((b - t) >> 4);
				lv = LIGHT_FLAT + (int16_t)((MULS16(gxs, ak) + MULS16(gys, bk)) >> 12);
				switch (surf) {
				case SURF_FAIRWAY:
					lv += ((py >> 19) & 1) ? stripe : -stripe;    /* 8 yard mowing bands */
					break;
				case SURF_GREEN:
					lv += ((py >> 18) & 1) ? stripe : -stripe;    /* 4 yard bands */
					break;
				case SURF_TEE:
					lv += ((py >> 17) & 1) ? stripe : -stripe;
					break;
				case SURF_BUNKER:
					lv += 16;
					if (prev != SURF_BUNKER) lv = 4;      /* the near lip's shadow */
					break;
				case SURF_WATER:
					/* darker near, lighter far where it shows the sky */
					lv = 24 + (fz > 96 ? 96 : (int16_t)fz) * 2 / 3;
					if (prev != SURF_WATER) lv = 0;       /* the near bank */
					break;
				default:
					if (prev == SURF_BUNKER) lv += 24;   /* the far lip faces the camera */
					break;
				}
				r->st->spans++;
				p = pixp(r, x, (int16_t)y);
				/* The shade of each pixel: the level dithered (8 bits only), grass
				 * noise near the camera, and haze dithered in with distance. */
#define SHADE(c) do { \
					uint8_t tb = bayer[((yy & 3) << 2) | (x & 3)]; \
					int16_t l = lv + (dith ? tb : 8); \
					if (ns < 8) { \
						int16_t nz = pal_noise[((yy & 15) << 4) | (x & 15)]; \
						l += (dith ? nz * 3 : nz) >> ns; \
					} \
					l >>= 4; \
					if (l < 0) l = 0; \
					if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1; \
					c = (hz > ((tb * 7 + 3) & 15)) ? (uint8_t)(PAL_HAZE + l) : (uint8_t)(base + l); \
				} while (0)
				/* 8 and 2 bits get their own loops so the write is inline */
				if (r->lg == 3) {
					for (yy = (int16_t)y; yy < yb; yy++, p += r->rb) {
						uint8_t c;
						SHADE(c);
						*p = c;
					}
				} else if (r->lg == 1) {
					uint8_t m = (uint8_t)(0xC0 >> ((x & 3) << 1)), nm = (uint8_t)~m;
					for (yy = (int16_t)y; yy < yb; yy++, p += r->rb) {
						uint8_t c;
						SHADE(c);
						*p = (uint8_t)((*p & nm) | (pal_pat2[((uint16_t)c << 2) | (yy & 3)] & m));
					}
				} else {
					for (yy = (int16_t)y; yy < yb; yy++, p += r->rb) {
						uint8_t c;
						SHADE(c);
						put(r, p, x, yy, c);
					}
				}
#undef SHADE
				r->st->pixels += yb - y;
				r->ybuf[x] = (int16_t)y;
			}
		}

		/* Stop once nothing further can show: everything is filled up to where the highest ground could reach. */
		if (ymax <= 0) break;
		step++;
		/* About z / 32 near (z / 26 on 160 wide screens, about 1.5 rows a step),
		 * z / 21 from 32 yards and z / 11 from 160. */
		if (z < (32L << 16)) z += r->scale > 1 ? (z >> 5) : (z >> 5) + (z >> 7);
		else z += z < (160L << 16) ? (z >> 5) + (z >> 6) : (z >> 4) + (z >> 5);
	}
	while (on < r->nobj) snap(r, &r->obj[on++]);
}

/* One PackBits row of n bytes from d at *pos into out. Returns -1 if cut short. */
static int SEC_RENDER unpack_row(const uint8_t *d, uint32_t len, uint32_t *pos, uint8_t *out, uint16_t n)
{
	uint32_t i = *pos;
	uint16_t k = 0;

	while (k < n) {
		int8_t c;
		uint16_t cnt;
		if (i >= len) return -1;
		c = (int8_t)d[i++];
		if (c == -128) continue;
		if (c >= 0) {
			cnt = (uint16_t)(c + 1);
			if (i + cnt > len || k + cnt > n) return -1;
			while (cnt--) out[k++] = d[i++];
		} else {
			cnt = (uint16_t)(1 - c);
			if (i >= len || k + cnt > n) return -1;
			while (cnt--) out[k++] = d[i];
			i++;
		}
	}
	*pos = i;
	return 0;
}

/* Sky, the backdrop strip and far haze above the terrain in each column. */
static void SEC_RENDER sky(R *r, const RenderData *bk, angle aim)
{
	int16_t x, y, ymax = 0, bw = 0, bh = 0, btop = 0, off = 0;
	uint32_t pos = 4;
	uint8_t dith = r->lg == 3;

	for (x = 0; x < r->w; x++)
		if (r->ybuf[x] > ymax) ymax = r->ybuf[x];
	if (bk->p && bk->len >= 4) {
		bw = (int16_t)be16(bk->p);
		bh = (int16_t)be16(bk->p + 2);
		if (bw <= 0 || bw > BKD_MAX || bh <= 0 || bh > r->vh) bw = bh = 0;
		/* the strip repeats twice a turn and turning left moves it right */
		off = (int16_t)(((uint32_t)(uint16_t)(0u - aim) * (uint32_t)(2 * bw)) >> 16);
		while (off >= bw && bw) off -= bw;
		btop = r->yh + 2 * r->scale - bh;
	}
	for (y = 0; y < ymax; y++) {
		uint8_t *row = 0, *p;
		int16_t sv = y < r->yh ? (int16_t)((int32_t)y * (16 * (PAL_SKY_N - 1)) / r->yh) : 16 * (PAL_SKY_N - 1);
		int16_t sx;
		if (bw && y >= btop && y < btop + bh) {
			if (unpack_row(bk->p, bk->len, &pos, r->row, (uint16_t)bw) == 0) row = r->row;
			else bw = 0;
		}
		p = pixp(r, 0, y);
		sx = off;
		for (x = 0; x < r->w; x++) {
			uint8_t c = 0;
			if (y < r->ybuf[x]) {
				if (row) c = row[sx];
				if (!c) {
					if (y <= r->yh) {
						int16_t l = (sv + (dith ? bayer[((y & 3) << 2) | (x & 3)] : 8)) >> 4;
						c = (uint8_t)(PAL_SKY + (l > PAL_SKY_N - 1 ? PAL_SKY_N - 1 : l));
					} else {
						c = PAL_HAZE + 4;
					}
				}
				put(r, p + ((uint16_t)x >> (3 - r->lg)), x, y, c);
				r->st->sky++;
			}
			if (++sx >= bw) sx = 0;
		}
	}
}

/* An object sprite or the pin, clipped by the y-buffer it was given. */
static void SEC_RENDER draw_obj(R *r, const RObj *o)
{
	int16_t x, y;

	if (o->clip == NO_CLIP) return;
	r->st->objects++;
	if (!o->px) {
		/* pin: pole sw wide in light grey, flag to its right in red */
		for (x = o->c0; x < o->c1; x++) {
			int16_t clip = r->pool[o->clip + x - o->c0], ye, ys = o->y0 < 0 ? 0 : o->y0;
			int16_t pole = x - o->x0 < (int16_t)o->sw;
			uint8_t *p;
			ye = pole ? o->y0 + o->dh : o->y0 + (int16_t)o->sh;
			if (ye > clip) ye = clip;
			if (ye > r->vh) ye = r->vh;
			p = pixp(r, x, ys);
			for (y = ys; y < ye; y++, p += r->rb)
				put(r, p, x, y, pole ? (uint8_t)(PAL_GREY + 7) : (uint8_t)(PAL_FLAG + 2 - (y - o->y0 >= (int16_t)o->sh - r->scale)));
		}
		return;
	}
	{
		uint32_t us = ((uint32_t)o->sw << 16) / (uint16_t)o->dw, vs = ((uint32_t)o->sh << 16) / (uint16_t)o->dh;
		r->st->divides += 2;
		for (x = o->c0; x < o->c1; x++) {
			int16_t clip = r->pool[o->clip + x - o->c0], ys = o->y0 < 0 ? 0 : o->y0, ye = o->y0 + o->dh;
			uint16_t u = (uint16_t)(((uint32_t)(x - o->x0) * us) >> 16);
			uint32_t v;
			const uint8_t *col;
			uint8_t *p;
			if (ye > clip) ye = clip;
			if (ye > r->vh) ye = r->vh;
			if (ys >= ye) continue;
			if (u >= o->sw) u = o->sw - 1;
			col = o->px + u;
			v = (uint32_t)(ys - o->y0) * vs;
			p = pixp(r, x, ys);
			for (y = ys; y < ye; y++, p += r->rb, v += vs) {
				uint16_t sv = (uint16_t)(v >> 16);
				uint8_t c;
				if (sv >= o->sh) break;
				c = col[MULU16(sv, o->sw)];
				if (c) put(r, p, x, y, c);
			}
			r->st->sprite_px += ye - ys;
		}
	}
}

/* The golfer at address, his ball point on (bx, by), and the ball. */
static void SEC_RENDER golfer(R *r, const RenderData *gd, int16_t bx, int16_t by)
{
	uint16_t w, h, i, j;
	const uint8_t *px = sprite(gd, &w, &h);

	if (px) {
		int16_t x0 = bx - (int16_t)be16(gd->p + 8), y0 = by - (int16_t)be16(gd->p + 10);
		for (j = 0; j < h; j++) {
			int16_t y = y0 + (int16_t)j;
			if (y < 0 || y >= r->vh) continue;
			for (i = 0; i < w; i++) {
				int16_t x = x0 + (int16_t)i;
				uint8_t c = px[MULU16(j, w) + i];
				if (c && x >= 0 && x < r->w) put(r, pixp(r, x, y), x, y, c);
			}
		}
	}
	/* the ball: white, with a dark pixel under it on 160 wide screens so it shows on light grass */
	for (j = 0; j < r->scale; j++)
		for (i = 0; i < r->scale; i++) {
			int16_t x = bx + (int16_t)i, y = by - (int16_t)j;
			if (x >= 0 && x < r->w && y >= 0 && y < r->vh) put(r, pixp(r, x, y), x, y, PAL_WHITE);
		}
	if (r->scale == 1 && by + 1 < r->vh && by + 1 >= 0 && bx >= 0 && bx < r->w)
		put(r, pixp(r, bx, by + 1), bx, by + 1, PAL_GREY + 1);
}

uint32_t render_work_size(const Surface *s)
{
	uint32_t w = (uint32_t)s->w;
	return w * 2 + w + sizeof(RObj) * MAX_OBJ + w * POOL_COLS * 2 + BKD_MAX + 16;
}

int render_scene(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats)
{
	R r;
	RenderStats local;
	uint8_t *wp = (uint8_t *)work;
	int16_t x, y, bx, by;
	fixed groundz;
	int32_t bh;

	if (s->scale < 1 || s->scale > 2 || s->w != 160 * s->scale || s->h < 160 * s->scale) return -1;
	r.st = stats ? stats : &local;
	{
		RenderStats z = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		*r.st = z;
	}
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.w = s->w;
	r.scale = s->scale;
	r.vh = (int16_t)((160 - RENDER_STATUS) * s->scale);
	r.yh = (int16_t)(HORIZON * s->scale);
	r.f = (int16_t)(FOCAL * s->scale);
	r.cx = s->w / 2;
	r.lg = (uint8_t)(s->depth == 8 ? 3 : s->depth == 4 ? 2 : s->depth == 2 ? 1 : 0);
	r.h = v->hole;
	r.g = v->grid;

	/* work: y-buffer, last surfaces, objects, clip pool, a backdrop row; ints first for alignment */
	r.ybuf = (int16_t *)wp;
	wp += (uint32_t)r.w * 2;
	r.pool = (int16_t *)wp;
	r.pool_size = (uint16_t)(r.w * POOL_COLS);
	wp += (uint32_t)r.pool_size * 2;
	r.obj = (RObj *)wp;
	wp += sizeof(RObj) * MAX_OBJ;
	r.prev = wp;
	wp += r.w;
	r.row = wp;
	r.pool_used = 0;
	r.nobj = 0;
	for (x = 0; x < r.w; x++) {
		r.ybuf[x] = r.vh;
		r.prev[x] = SURF_COUNT;
	}

	/* camera */
	r.fx = icos(v->aim);
	r.fy = isin(v->aim);
	r.rx = r.fy;
	r.ry = (int16_t)-r.fx;
	r.camx = v->ball.x - mulq(CAM_BACK, r.fx);
	r.camy = v->ball.y - mulq(CAM_BACK, r.fy);
	bh = hb_at(r.h, v->ball.x, v->ball.y);
	groundz = r.h->base + fix_mul(bh << 8, r.h->step);
	r.camb = fix_div(groundz + CAM_UP - r.h->base, r.h->step) >> 8;
	r.stepf = (uint32_t)r.h->step * (uint32_t)r.f;

	gather(&r, v, a);
	terrain(&r);
	sky(&r, &a->backdrop, v->aim);
	for (x = r.nobj; x > 0; x--) draw_obj(&r, &r.obj[x - 1]);

	{
		int16_t m;
		uint8_t sh;
		kz(&r, r.stepf, (uint32_t)CAM_BACK, &m, &sh);
		bx = r.cx;
		by = (int16_t)(r.yh + (MULS16(dz6(r.camb, bh), m) >> sh));
	}
	golfer(&r, &a->golfer, bx, by);

	/* the status strip, black for now */
	for (y = r.vh; y < s->h; y++) {
		uint8_t *p = r.bits + MULU16(y, r.rb);
		for (x = 0; x < (int16_t)r.rb; x++) p[x] = 0xFF;
	}
	return 0;
}
