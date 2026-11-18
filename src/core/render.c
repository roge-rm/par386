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

/* The ball's second pixel and its black edge on 4 greys, from the ball point. */
static const int8_t ball_dx[5] = { 1, -1, 2, 0, 1 }, ball_dy[5] = { 0, 0, 0, 1, 1 };

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
	fixed mx, my;               /* heightmap's last cell, 16.16 cells */
	uint16_t hw, hh;            /* heightmap size; 16 bits, or MULU16 isn't one mulu.w */
	int16_t ak, bk;             /* slope light per gradient step across and along */
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

/* Height and light at a point, for the terrain march: the row the ground
 * projects to in 1/16 pixels (clamped to 16 bits) and its slope light in
 * ramp steps of 16. m and sh project at this step's depth (kz). */
static void SEC_RENDER sample(const R *r, fixed px, fixed py, int16_t m, uint8_t sh, int16_t *y16, int16_t *lv)
{
	const Hole *h = r->h;
	fixed gx = px >> h->hm_shift, gy = py >> h->hm_shift;
	uint16_t i, j, i1, j1, fx, fy, hw = r->hw;
	const uint8_t *r0, *r1;
	int32_t t, b, hb, y;
	int16_t sx0, sx1, gxs, gys;

	if (gx < 0) gx = 0;
	if (gx > r->mx) gx = r->mx;
	if (gy < 0) gy = 0;
	if (gy > r->my) gy = r->my;
	i = (uint16_t)(gx >> 16); fx = (uint16_t)((gx >> 8) & 255);
	j = (uint16_t)(gy >> 16); fy = (uint16_t)((gy >> 8) & 255);
	i1 = i + 1 < hw ? i + 1 : i;
	j1 = j + 1 < r->hh ? j + 1 : j;
	r0 = h->heights + MULU16(j, hw);
	r1 = h->heights + MULU16(j1, hw);
	sx0 = r0[i1] - r0[i];
	sx1 = r1[i1] - r1[i];
	t = ((int32_t)r0[i] << 8) + MULS16(sx0, fx);
	b = ((int32_t)r1[i] << 8) + MULS16(sx1, fx);
	hb = t + (MULS16((b - t) >> 1, fy) >> 7);

	/* gradients in 1/16 byte steps per heightmap cell */
	gxs = (int16_t)(sx0 * 16 + (MULS16(sx1 - sx0, fy) >> 4));
	gys = (int16_t)((b - t) >> 4);
	*lv = (int16_t)(LIGHT_FLAT + ((MULS16(gxs, r->ak) + MULS16(gys, r->bk)) >> 12));

	y = MULS16(dz6(r->camb, hb), m);
	if (sh >= 4) y >>= sh - 4;
	else if (y > 0x3FFFFFL) y = 0x3FFFFFFL;
	else if (y < -0x3FFFFFL) y = -0x3FFFFFFL;
	else y <<= 4 - sh;
	y += (int32_t)r->yh << 4;
	if (y > 16000) y = 16000;
	if (y < -16000) y = -16000;
	*y16 = (int16_t)y;
}

/* Work counts for the host tests; left out on the Palm, where they'd cost time in the inner loops. */
#ifdef __palmos__
#define COUNT(e)
#else
#define COUNT(e) (e)
#endif

/* One column's span of ground from row y down to row yb - 1, shaded from
 * level lv (ramp steps of 16) on ramp base. */
#define SHADE(c, xx) do { \
	uint8_t tb = bayer[((yy & 3) << 2) | (xx & 3)]; \
	int16_t l = lv + (dith ? tb : 8); \
	if (ns < 8) { \
		int16_t nz = pal_noise[((yy & 15) << 4) | (xx & 15)]; \
		l += (dith ? nz * 3 : nz) >> ns; \
	} \
	l >>= 4; \
	if (l < 0) l = 0; \
	if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1; \
	c = (hz > ((tb * 7 + 3) & 15)) ? (uint8_t)(PAL_HAZE + l) : (uint8_t)(base + l); \
} while (0)

/* The terrain, front to back. At each depth step the heights and slope light
 * are worked out every 4 columns (8 on 320 screens), twice that from LONG_Z,
 * and drawn in between from straight lines. The surface is looked up in every column near the camera and every
 * other column from HALF_Z on, where spans are a pixel or two tall. */
#define SEG_SHIFT 2
#define HALF_Z    32       /* yards */
#define LONG_Z    64

static void SEC_RENDER terrain(R *r)
{
	const Hole *h = r->h;
	const SurfaceGrid *g = r->g;
	fixed z = near_start(r);
	uint8_t on = 0, dith = r->lg == 3, ks = (uint8_t)(SEG_SHIFT + (r->scale > 1));
	int16_t sand_drop, water_drop, stripe = dith ? 12 : 18, seg;
	uint16_t step = 0, rb = r->rb, gw = g->w, gh = g->h, gs = g->stride;
	int16_t *ybuf = r->ybuf;
	uint8_t *prevs = r->prev;
	const uint8_t *cells = g->cells;

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
		r->ak = (int16_t)(ca > 32767 ? 32767 : ca < -32767 ? -32767 : ca);
		r->bk = (int16_t)(cb > 32767 ? 32767 : cb < -32767 ? -32767 : cb);
		/* bunkers sit 0.4 yards down and water 0.25, for their lips */
		ca = fix_div(26214, h->step) >> 8;
		cb = fix_div(16384, h->step) >> 8;
		sand_drop = (int16_t)(ca > 30000 ? 30000 : ca);
		water_drop = (int16_t)(cb > 30000 ? 30000 : cb);
	}

	while (z < Z_FAR) {
		fixed zr, dpx, dpy, px, py, spx, spy, dpxs, dpys, dpxc, dpyc, fz;
		int16_t m, x, xe, hz, ns, ymax, ya, la, yn, ln, sand_px, water_px, cw;
		uint8_t sh, kss;

		while (on < r->nobj && r->obj[on].depth <= z) snap(r, &r->obj[on++]);

		kz(r, r->stepf, (uint32_t)z, &m, &sh);
		zr = z / r->f;
		r->st->divides++;
		dpx = mulq(zr, r->rx);
		dpy = mulq(zr, r->ry);
		px = r->camx + mulq(z, r->fx) - dpx * r->cx + (dpx >> 1);
		py = r->camy + mulq(z, r->fy) - dpy * r->cx + (dpy >> 1);
		fz = z >> 16;
		cw = fz < HALF_Z ? 1 : 2;
		/* segments twice as wide from LONG_Z, still about a heightmap cell to 200 yards */
		kss = (uint8_t)(ks + (fz >= LONG_Z));
		seg = (int16_t)(1 << kss);
		dpxs = dpx * seg;
		dpys = dpy * seg;
		spx = px;
		spy = py;
		dpxc = dpx * cw;
		dpyc = dpy * cw;
		hz = fz < 140 ? 0 : fz > 620 ? 12 : (int16_t)((fz - 140) / 40);
		ns = fz < 10 ? 0 : fz < 24 ? 1 : fz < 48 ? 2 : 8;
		{
			int32_t d = MULS16(sand_drop >> 2, m) >> sh, e = MULS16(water_drop >> 2, m) >> sh;
			sand_px = (int16_t)(d > 1000 ? 1000 : d);
			water_px = (int16_t)(e > 1000 ? 1000 : e);
		}
		ymax = 0;
		r->st->steps++;
		r->st->samples += (r->w >> kss) + 1;

		sample(r, spx, spy, m, sh, &ya, &la);
		for (x = 0; x < r->w; ya = yn, la = ln) {
			int16_t yd, ld;
			spx += dpxs;
			spy += dpys;
			sample(r, spx, spy, m, sh, &yn, &ln);
			yd = (int16_t)(((yn - ya) >> kss) * cw);
			ld = (int16_t)(((ln - la) >> kss) * cw);
			for (xe = x + seg; x < xe; x += cw, px += dpxc, py += dpyc, ya += yd, la += ld) {
				int16_t yb = ybuf[x], y = ya >> 4, k;
				uint8_t surf, prev;

				if (cw > 1 && ybuf[x + 1] > yb) yb = ybuf[x + 1];
				if (yb > ymax) ymax = yb;
				/* lowered ground (sand, water) only goes further down, so this hides it too */
				if (y >= yb) continue;

				/* Surface, with the lookup shaken by up to half a yard near the camera so cell edges break up. */
				{
					fixed sx = px, sy = py;
					int16_t cx, cy;
					if (ns < 8) {
						sx += (fixed)pal_noise[(x * 7 + step * 13) & 255] * 4096;
						sy += (fixed)pal_noise[(x * 11 + step * 5 + 101) & 255] * 4096;
					}
					cx = (int16_t)(sx >> (16 + SURF_CELL_SHIFT));
					cy = (int16_t)(sy >> (16 + SURF_CELL_SHIFT));
					if (sx < 0 || sy < 0 || (uint16_t)cx >= gw || (uint16_t)cy >= gh) {
						surf = SURF_OOB;
					} else {
						uint8_t c = cells[MULU16(cy, gs) + ((uint16_t)cx >> 1)];
						surf = (cx & 1) ? (c & 15) : (c >> 4);
						if (surf >= SURF_COUNT) surf = SURF_OOB;
					}
				}
				prev = prevs[x];
				prevs[x] = surf;
				if (surf == SURF_BUNKER) {
					y += sand_px;
					if (y >= yb) continue;
				} else if (surf == SURF_WATER) {
					y += water_px;
					if (y >= yb) continue;
				}
				if (y < 0) y = 0;

				{
					int16_t lv = la, yy;
					uint8_t base = (uint8_t)(PAL_TERRAIN + surf * PAL_RAMP_N);

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
					if (cw > 1) prevs[x + 1] = surf;

					/* the span in each column this sample covers */
					for (k = 0; k < cw; k++) {
						int16_t xx = x + k, ye = ybuf[xx];
						uint8_t *p, pair = 0;
						if (y >= ye) continue;
						COUNT(r->st->spans++);
						COUNT(r->st->pixels += ye - y);
						ybuf[xx] = y;
						/* at 2 bits a pair of columns with the same span is one byte's job */
						if (!k && cw > 1 && r->lg == 1 && ybuf[xx + 1] == ye) {
							pair = 1;
							k++;
							ybuf[xx + 1] = y;
							COUNT(r->st->pixels += ye - y);
						}
						p = r->bits + MULU16(y, rb) + ((uint16_t)xx >> (3 - r->lg));
						if (r->lg == 3) {
							for (yy = y; yy < ye; yy++, p += rb) {
								uint8_t c;
								SHADE(c, xx);
								*p = c;
							}
						} else if (r->lg == 1) {
							/* 2 bits: the level isn't dithered, so a span is one colour
							 * a row of the pattern, but for noise near and haze far */
							uint8_t mk = (uint8_t)((pair ? 0xF0 : 0xC0) >> ((xx & 3) << 1)), nm = (uint8_t)~mk;
							if (ns < 8) {
								const int8_t *nzc = pal_noise + (xx & 15);
								const uint8_t *pat = pal_pat2 + ((uint16_t)base << 2);
								int16_t l0 = lv + 8;
								for (yy = y; yy < ye; yy++, p += rb) {
									int16_t l = (l0 + (nzc[(yy & 15) << 4] >> ns)) >> 4;
									if (l < 0) l = 0;
									if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
									*p = (uint8_t)((*p & nm) | (pat[(l << 2) | (yy & 3)] & mk));
								}
							} else if (!hz) {
								const uint8_t *pat;
								int16_t l = (lv + 8) >> 4;
								if (l < 0) l = 0;
								if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
								pat = pal_pat2 + ((uint16_t)(base + l) << 2);
								for (yy = y; yy < ye; yy++, p += rb)
									*p = (uint8_t)((*p & nm) | (pat[yy & 3] & mk));
							} else {
								uint8_t pat[4], q;
								int16_t l = (lv + 8) >> 4;
								if (l < 0) l = 0;
								if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
								for (q = 0; q < 4; q++) {
									uint8_t tb = bayer[(q << 2) | (xx & 3)];
									uint8_t c = (uint8_t)(hz > ((tb * 7 + 3) & 15) ? PAL_HAZE + l : base + l);
									pat[q] = (uint8_t)(pal_pat2[((uint16_t)c << 2) | q] & mk);
								}
								for (yy = y; yy < ye; yy++, p += rb)
									*p = (uint8_t)((*p & nm) | pat[yy & 3]);
							}
						} else {
							for (yy = y; yy < ye; yy++, p += rb) {
								uint8_t c;
								SHADE(c, xx);
								put(r, p, xx, yy, c);
							}
						}
					}
				}
			}
		}

		/* Stop once nothing further can show: everything is filled up to where the highest ground could reach. */
		if (ymax <= 0) break;
		step++;
		/* z / 16 near (z / 24 on 320 screens), 3 to 6 rows a step from the
		 * bottom of the view to 14 yards, then z / 21 from 32 yards and z / 11
		 * from 160, where a step is under a row. */
		if (z < (32L << 16)) z += r->scale > 1 ? (z >> 5) + (z >> 7) : (z >> 4);
		else z += z < (160L << 16) ? (z >> 5) + (z >> 6) : (z >> 4) + (z >> 5);
	}
	while (on < r->nobj) snap(r, &r->obj[on++]);
}
#undef SHADE

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
		if (r->lg == 1) {
			/* 2 bits: the sky is one colour a row, so its pattern byte goes in
			 * whole where all 4 pixels are sky and there's no backdrop */
			uint8_t c = y <= r->yh ? (uint8_t)(PAL_SKY + ((sv + 8) >> 4)) : (uint8_t)(PAL_HAZE + 4);
			const uint8_t *pat = pal_pat2 + (y & 3);
			uint8_t b = pat[(uint16_t)c << 2];
			const int16_t *yb = r->ybuf;
			for (x = 0; x < r->w; x += 4, yb += 4, p++) {
				if (!row && y < yb[0] && y < yb[1] && y < yb[2] && y < yb[3]) {
					*p = b;
					COUNT(r->st->sky += 4);
				} else {
					uint8_t k, v = *p, mk = 0xC0;
					for (k = 0; k < 4; k++, mk >>= 2) {
						if (y < yb[k]) {
							uint8_t pc = b;
							if (row && row[sx]) pc = pat[(uint16_t)row[sx] << 2];
							v = (uint8_t)((v & ~mk) | (pc & mk));
							COUNT(r->st->sky++);
						}
						if (++sx >= bw) sx = 0;
					}
					*p = v;
					continue;
				}
				sx += 4;
				while (sx >= bw && bw) sx -= bw;
			}
			continue;
		}
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
				if (r->lg == 3) p[x] = c;
				else put(r, p + ((uint16_t)x >> (3 - r->lg)), x, y, c);
				COUNT(r->st->sky++);
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
	/* On 4 greys only black stands out: the ball is 2 pixels wide with black
	 * either side and under it. */
	if (r->lg == 1 && r->scale == 1 && by >= 0 && by < r->vh) {
		for (i = 0; i < 5; i++) {
			int16_t x = bx + ball_dx[i], y = by + ball_dy[i];
			if (x >= 0 && x < r->w && y < r->vh) put(r, pixp(r, x, y), x, y, i ? PAL_BLACK : PAL_WHITE);
		}
	}
}

static RenderClock clock_fn;

void render_clock(RenderClock fn)
{
	clock_fn = fn;
}

/* Adds the ticks since t to stage k and returns the clock now. */
static uint32_t SEC_RENDER lap(RenderStats *st, uint8_t k, uint32_t t)
{
	uint32_t now;
	if (!clock_fn) return 0;
	now = clock_fn();
	if (t) st->ticks[k] += now - t;
	return now;
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
	uint32_t t;

	if (s->scale < 1 || s->scale > 2 || s->w != 160 * s->scale || s->h < 160 * s->scale) return -1;
	r.st = stats ? stats : &local;
	{
		RenderStats z = { 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0 } };
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
	r.hw = r.h->hm_w;
	r.hh = r.h->hm_h;
	r.mx = (fixed)(r.hw - 1) << 16;
	r.my = (fixed)(r.hh - 1) << 16;

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

	t = lap(r.st, 0, 0);
	gather(&r, v, a);
	t = lap(r.st, RS_GATHER, t);
	terrain(&r);
	t = lap(r.st, RS_TERRAIN, t);
	sky(&r, &a->backdrop, v->aim);
	t = lap(r.st, RS_SKY, t);
	for (x = r.nobj; x > 0; x--) draw_obj(&r, &r.obj[x - 1]);
	t = lap(r.st, RS_OBJECTS, t);

	{
		int16_t m;
		uint8_t sh;
		kz(&r, r.stepf, (uint32_t)CAM_BACK, &m, &sh);
		bx = r.cx;
		by = (int16_t)(r.yh + (MULS16(dz6(r.camb, bh), m) >> sh));
	}
	golfer(&r, &a->golfer, bx, by);
	lap(r.st, RS_GOLFER, t);

	/* the status strip, black for now */
	for (y = r.vh; y < s->h; y++) {
		uint8_t *p = r.bits + MULU16(y, r.rb);
		for (x = 0; x < (int16_t)r.rb; x++) p[x] = 0xFF;
	}
	return 0;
}

/* A filled square of n pixels, clipped to the view. */
static void SEC_RENDER dot(const R *r, int16_t x, int16_t y, int16_t n, uint8_t c)
{
	int16_t i, j;
	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++) {
			int16_t px = x + i, py = y + j;
			if (px >= 0 && px < r->w && py >= 0 && py < r->vh) put(r, pixp(r, px, py), px, py, c);
		}
}

int render_map(Surface *s, const RenderView *v, RenderMap *out)
{
	R r;
	const Hole *h = v->hole;
	const SurfaceGrid *g = v->grid;
	fixed ypp, a, b, x0, wy;
	int16_t x, y, left, top, mw, mh, sc;
	uint16_t i;
	HoleObject o;
	uint8_t col[SURF_COUNT], xcell[320];

	if (s->scale < 1 || s->scale > 2 || s->w != 160 * s->scale || s->h < 160 * s->scale) return -1;
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.w = s->w;
	sc = s->scale;
	r.vh = (int16_t)((160 - RENDER_STATUS) * sc);
	r.lg = (uint8_t)(s->depth == 8 ? 3 : s->depth == 4 ? 2 : s->depth == 2 ? 1 : 0);

	/* yards per pixel, so the whole hole fits with a 2 pixel margin */
	a = ((fixed)h->width << 16) / (r.w - 4 * sc);
	b = ((fixed)h->length << 16) / (r.vh - 4 * sc);
	ypp = a > b ? a : b;
	if (ypp < 1) ypp = 1;
	mw = (int16_t)(((fixed)h->width << 16) / ypp);
	mh = (int16_t)(((fixed)h->length << 16) / ypp);
	left = (r.w - mw) / 2;
	top = (r.vh - mh) / 2;

	/* surfaces at the middle of their ramps, out of bounds dark; off the hole is black (0xFF at every depth) */
	for (i = 0; i < SURF_COUNT; i++)
		col[i] = (uint8_t)(PAL_TERRAIN + i * PAL_RAMP_N + PAL_RAMP_N / 2);
	col[SURF_OOB] = PAL_TERRAIN + SURF_OOB * PAL_RAMP_N + 1;
	/* the grid column under each screen column, 0xFF off the hole */
	x0 = ypp >> 1;
	for (x = 0; x < r.w; x++) {
		xcell[x] = 0xFF;
		if (x >= left && x < left + mw) {
			int32_t cx = x0 >> (16 + SURF_CELL_SHIFT);
			xcell[x] = (uint8_t)(cx >= g->w ? g->w - 1 : cx);
			x0 += ypp;
		}
	}
	for (y = 0; y < r.vh; y++) {
		uint8_t *p = pixp(&r, 0, y);
		int32_t cy;
		const uint8_t *cells;
		if (y < top || y >= top + mh) {
			for (x = 0; x < (int16_t)r.rb; x++) p[x] = 0xFF;
			continue;
		}
		/* the far end at the top; sample each pixel's centre */
		wy = ((fixed)h->length << 16) - (fixed)(y - top) * ypp - (ypp >> 1);
		cy = wy >> (16 + SURF_CELL_SHIFT);
		if (cy < 0) cy = 0;
		if (cy >= g->h) cy = g->h - 1;
		cells = g->cells + MULU16(cy, g->stride);
		if (r.lg == 1) {
			/* 2 bits: this row of each surface's pattern, a byte at a time */
			uint8_t pr[SURF_COUNT + 1];
			for (i = 0; i < SURF_COUNT; i++) pr[i] = pal_pat2[((uint16_t)col[i] << 2) | (y & 3)];
			pr[SURF_COUNT] = 0xFF;
			for (x = 0; x < r.w; x += 4, p++) {
				uint8_t k, v = 0;
				if (xcell[x] == 0xFF && xcell[x + 3] == 0xFF) {
					*p = 0xFF;
					continue;
				}
				for (k = 0; k < 4; k++) {
					uint8_t cx = xcell[x + k], t = SURF_COUNT;
					if (cx != 0xFF) {
						t = cells[cx >> 1];
						t = (cx & 1) ? (t & 15) : (t >> 4);
						if (t >= SURF_COUNT) t = SURF_OOB;
					}
					v |= (uint8_t)(pr[t] & (0xC0 >> (k << 1)));
				}
				*p = v;
			}
			continue;
		}
		for (x = 0; x < r.w; x++) {
			uint8_t c = PAL_BLACK, cx = xcell[x];
			if (cx != 0xFF) {
				uint8_t t = cells[cx >> 1];
				t = (cx & 1) ? (t & 15) : (t >> 4);
				c = col[t < SURF_COUNT ? t : SURF_OOB];
			}
			if (r.lg == 3) p[x] = c;
			else put(&r, p + ((uint16_t)x >> (3 - r.lg)), x, y, c);
		}
	}

#define MX(wx) ((int16_t)(left + (wx) / ypp))
#define MY(wy) ((int16_t)(top + (((fixed)h->length << 16) - (wy)) / ypp))
	/* trees and bushes as dark dots, rocks grey */
	for (i = 0; i < h->object_count; i++) {
		hole_object(h, i, &o);
		if (o.type == OBJ_ROCK) dot(&r, MX(o.pos.x), MY(o.pos.y), sc, PAL_ROCK + 3);
		else if (o.type >= OBJ_TREE && o.type <= OBJ_BUSH) dot(&r, MX(o.pos.x) - (sc >> 1), MY(o.pos.y) - (sc >> 1), sc + 1, PAL_TREE + 1);
	}
	/* the tee: a white square with a black edge */
	x = MX(h->tees[0].pos.x);
	y = MY(h->tees[0].pos.y);
	dot(&r, x - sc, y - sc, 3 * sc, PAL_BLACK);
	dot(&r, x, y, sc, PAL_WHITE);

	/* the aim line from the ball to the pin, dashed black and white */
	out->bx = MX(v->ball.x);
	out->by = MY(v->ball.y);
	out->px = MX(h->pins[v->pin & 3].x);
	out->py = MY(h->pins[v->pin & 3].y);
	out->yards = (uint16_t)((vec2_len(vec2_sub(h->pins[v->pin & 3], v->ball)) + FIX_HALF) >> 16);
	out->left = left;
	out->top = top;
	out->ypp = ypp;
	out->length = (fixed)h->length << 16;
	{
		int16_t dx = out->px - out->bx, dy = out->py - out->by;
		int16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
		int16_t n = ax > ay ? ax : ay, k;
		for (k = 2 * sc; k < n; k++) {
			x = (int16_t)(out->bx + (int16_t)((int32_t)dx * k / n));
			y = (int16_t)(out->by + (int16_t)((int32_t)dy * k / n));
			dot(&r, x, y, 1, ((k / (2 * sc)) & 1) ? PAL_WHITE : PAL_BLACK);
		}
	}
	/* the pin: a pole with a flag to its right */
	x = out->px;
	y = out->py;
	for (i = 0; i <= (uint16_t)(5 * sc); i++) dot(&r, x, y - (int16_t)i, 1, PAL_BLACK);
	for (i = 1; i <= (uint16_t)(3 * sc); i++) {
		int16_t j;
		for (j = 0; j < 2 * sc; j++) dot(&r, x + (int16_t)i, y - 5 * sc + j, 1, PAL_FLAG + 1);
	}
	/* the ball: white with a black ring */
	dot(&r, out->bx - sc, out->by - sc, 3 * sc, PAL_BLACK);
	dot(&r, out->bx, out->by, sc, PAL_WHITE);
#undef MX
#undef MY

	/* the status strip, black */
	for (y = r.vh; y < s->h; y++) {
		uint8_t *p = r.bits + MULU16(y, r.rb);
		for (x = 0; x < (int16_t)r.rb; x++) p[x] = 0xFF;
	}
	return 0;
}

void render_camera(const Surface *s, const RenderView *v, RenderCam *c)
{
	int32_t bh = hb_at(v->hole, v->ball.x, v->ball.y);

	c->fx = icos(v->aim);
	c->fy = isin(v->aim);
	c->rx = c->fy;
	c->ry = (int16_t)-c->fx;
	c->camx = v->ball.x - mulq(CAM_BACK, c->fx);
	c->camy = v->ball.y - mulq(CAM_BACK, c->fy);
	c->camz = v->hole->base + fix_mul(bh << 8, v->hole->step) + CAM_UP;
	c->f = (int16_t)(FOCAL * s->scale);
	c->cx = s->w / 2;
	c->yh = (int16_t)(HORIZON * s->scale);
}

/* v * f / d for d over 1 yard, clamped to the int16 range. */
static int16_t SEC_RENDER persp(fixed v, int16_t f, fixed d)
{
	int32_t r = (v >> 8) * f / (d >> 8);
	if (r > 32000) r = 32000;
	if (r < -32000) r = -32000;
	return (int16_t)r;
}

int render_point(const RenderCam *c, fixed x, fixed y, fixed z, int16_t *sx, int16_t *sy, fixed *depth)
{
	fixed dx = x - c->camx, dy = y - c->camy, side;
	int32_t px, py;

	*depth = mulq(dx, c->fx) + mulq(dy, c->fy);
	if (*depth < FIX_ONE) return 0;
	side = mulq(dx, c->rx) + mulq(dy, c->ry);
	if (side > (fixed)(4000L << 16) || side < -(fixed)(4000L << 16)) return 0;
	px = (int32_t)c->cx + persp(side, c->f, *depth);
	py = (int32_t)c->yh + persp(c->camz - z, c->f, *depth);
	/* clamped so callers can add sizes without overflowing 16 bits */
	*sx = (int16_t)(px > 1000 ? 1000 : px < -1000 ? -1000 : px);
	*sy = (int16_t)(py > 1000 ? 1000 : py < -1000 ? -1000 : py);
	return 1;
}
