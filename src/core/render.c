/* The view from behind the ball (render.h).
 *
 * Camera: CAM_BACK yards behind the ball along the aim line, looking level
 * with the view shifted down so the horizon sits at row HORIZON (no pitch,
 * so verticals stay vertical). Its height puts the ball BALL_GAP rows above
 * the bottom of the view: about 4 yards on a square screen, more on a tall
 * one, where the horizon moves down by half the extra rows. That stands the
 * golfer in the foreground, 40 px tall on 160 wide screens, the size of the
 * sprite, with the hole in front of him.
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

#define CAM_BACK   (7L << 16)         /* yards behind the ball */
#define BALL_GAP   7                  /* rows from the ball to the bottom of the view on 160 wide screens */
#define FOCAL      140                /* pixels per unit of x / depth on 160 wide screens */
#define HORIZON    52                 /* horizon row on 160 wide screens */
#define Z_NEAR     (2L << 16)
#define Z_FAR      (800L << 16)
#define OBJ_NEAR   (4L << 16)         /* objects nearer than this are skipped */
#define MAX_OBJ    64
#define POOL_COLS  8                  /* clip columns kept per screen column */
#define BKD_MAX    1024               /* widest backdrop row on 160 wide screens, twice that on 320 */
#define BKD_TURN   968                /* backdrop pixels a whole turn on 160 wide screens, twice that on 320 */
#define ANIMAL_MIN 4                  /* animals are drawn at least this tall, times the scale */
#define LIGHT_FLAT 62                 /* ramp position of flat ground, 16 per level */

/* Object heights in 1/16 yards at scale 64, by type. */
static const uint16_t obj_h16[RENDER_TYPES] = {
	0, 192, 224, 208, 35, 21, 128, 80, 24, 24, 14, 256, 24
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
	uint8_t *wet;               /* 1 in a column that has drawn far water: it isn't raised or probed again */
	int16_t *wbox;              /* each water shape's box in yards: x0, y0, x1, y1 */
	uint8_t nwbox;
	int16_t hl, wmin, tq;       /* this step's haze lift, far water rows and probe share (2.14), kept here to free registers */
	RObj *obj;
	int16_t *pool;
	uint8_t *row;
	uint16_t pool_used, pool_size;
	uint8_t nobj;
	RenderStats *st;
} R;

/* A scene being drawn (render_scene_begin and _step), at the start of the
 * work memory: where each stage got to. */
typedef struct {
	R r;
	RenderStats local;          /* the stats when the caller wants none */
	RenderView v;
	Surface s;                  /* the surface asked for */
	Surface d;                  /* the one drawn into: s, or its top left quarter at half size */
	const RenderArt *a;
	int32_t bh;                 /* the ground at the ball, heightmap byte steps */
	fixed z, zl;                /* terrain: the next depth step and the last */
	uint16_t step;
	uint8_t on;                 /* objects given their clip columns so far */
	uint8_t stage;              /* JS_* */
	uint8_t half;
	int16_t y, ymax, bw, bh2, btop, off;    /* sky: the next row, where the sky ends, the backdrop */
	uint32_t pos;               /* the backdrop's next packed row */
	int16_t k;                  /* objects still to draw */
} Job;

enum { JS_GATHER, JS_TERRAIN, JS_SKY, JS_OBJECTS, JS_GOLFER, JS_DOUBLE, JS_DONE };

/* Whether a slice's time is up. */
static int SEC_RENDER late(RenderClock clk, uint32_t until)
{
	return clk && (int32_t)(clk() - until) >= 0;
}

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

/* Checks the surface fits the renderer: 160 to 240 units across and down,
 * a multiple of 16 pixels across. Sets the view's height, all but the
 * status strip, and returns half of how much taller it is than on a square
 * screen: the horizon moves down by that, so the sky and the ground in
 * front both grow. Returns -1 if it doesn't fit. */
static int16_t SEC_RENDER fit(const Surface *s, int16_t *vh)
{
	int16_t sc = s->scale;
	if (sc < 1 || sc > 2 || s->w < 160 * sc || s->w > 240 * sc || (s->w & 15) ||
	    s->h < 160 * sc || s->h > 240 * sc) return -1;
	*vh = (int16_t)(s->h - RENDER_STATUS * sc);
	return (int16_t)((*vh - (160 - RENDER_STATUS) * sc) / 2);
}

/* The camera's height above the ball's ground, in yards, that puts the
 * ball BALL_GAP rows above the bottom of a view vh rows tall with the
 * horizon on row yh. */
static fixed SEC_RENDER cam_up(int16_t vh, int16_t yh, int16_t sc)
{
	return (fixed)(vh - BALL_GAP * sc - yh) * CAM_BACK / (FOCAL * sc);
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
		/* the 2-bit pattern, made for screens where only black shows:
		 * black stays, dark grey is black on one pixel in 4, the rest white */
		v = (uint8_t)((pal_pat2[((uint16_t)c << 2) | (y & 3)] >> (6 - ((x & 3) << 1))) & 3);
		m = (uint8_t)(0x80 >> (x & 7));
		if (v == 3 || (v == 2 && (x & y & 1))) *p |= m;
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

/* 1 if object a matters less than b: shorter on screen, or as tall and
 * further. The pin never does. */
static int SEC_RENDER lesser(const RObj *a, const RObj *b)
{
	if (!b->px) return 1;
	if (!a->px) return 0;
	return a->dh < b->dh || (a->dh == b->dh && a->depth > b->depth);
}

/* Puts an object in the list, nearest first. When it's full the object
 * that is smallest on screen is left out, so nothing near or large goes. */
static void SEC_RENDER add_obj(R *r, const RObj *o)
{
	uint8_t k;
	if (r->nobj == MAX_OBJ) {
		uint8_t i, s = 0;
		r->st->dropped++;
		for (i = 1; i < MAX_OBJ; i++)
			if (lesser(&r->obj[i], &r->obj[s])) s = i;
		if (!lesser(&r->obj[s], o)) return;
		for (i = s; i < MAX_OBJ - 1; i++) r->obj[i] = r->obj[i + 1];
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
	uint8_t vr;

	for (i = 0; i < h->object_count; i++) {
		hole_object(h, i, &ho);
		if (ho.type >= RENDER_TYPES || !obj_h16[ho.type]) continue;
		/* the object's variant, or the first if the art has no such variant */
		vr = ho.variant < RENDER_VARIANTS ? ho.variant : 0;
		px = sprite(&a->spr[ho.type][vr][0], &sw, &sh);
		px2 = sprite(&a->spr[ho.type][vr][1], &sw2, &sh2);
		if (!px && !px2 && vr) {
			px = sprite(&a->spr[ho.type][0][0], &sw, &sh);
			px2 = sprite(&a->spr[ho.type][0][1], &sw2, &sh2);
		}
		if (!px && !px2) continue;
		if (!project(r, ho.pos, &o.depth, &sx)) continue;
		/* height in pixels: 1/16 yards times focal over depth in 1/16 yards */
		hp = (int32_t)((MULU16(obj_h16[ho.type], ho.scale) >> 6) * r->f) / (o.depth >> 12);
		r->st->divides++;
		/* animals are small: kept to a size that shows as far as they'd be a pixel */
		if (ho.type == OBJ_ANIMAL && hp >= 1 && hp < ANIMAL_MIN * r->scale) hp = ANIMAL_MIN * r->scale;
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
	c = (hb > ((tb * 7 + 3) & 15)) ? (uint8_t)(PAL_HAZE + l) : (uint8_t)(base + l); \
} while (0)

/* The terrain, front to back. At each depth step the heights and slope light
 * are worked out every 4 columns (8 on 320 screens), twice that from LONG_Z,
 * and drawn in between from straight lines. The surface is looked up in every column near the camera and every
 * other column from HALF_Z on, where spans are a pixel or two tall. */
#define SEG_SHIFT 2
#define HALF_Z    32       /* yards */
#define LONG_Z    64
#define NEAR_Z    14L      /* yards; nearer, 160 wide screens step z / 11 */
/* Haze: ground past HAZE_LIFT yards is drawn lighter in its own ramp, up to
 * 2 levels; past HAZE_MIX it goes over to the haze ramp, all of it by
 * HAZE_MIX + 160. At 2 bits it goes over a row at a time (haze_row), so
 * it reads as bands rather than speckles. */
#define HAZE_LIFT 180
#define HAZE_MIX  400
/* Water past WATER_FAR yards is at least WATER_MIN rows tall (times the
 * scale), light with a dark line along each bank, and from PROBE_Z the
 * surface half a step nearer is looked at too, so a creek between two
 * steps isn't missed. */
#define WATER_FAR 48
#define WATER_MIN 3
#define PROBE_Z   64
#define WBOX_MAX  8           /* water shapes with their own box; more share the last */

/* The 2-bit pattern rows in the order they go over to haze. */
static const uint8_t haze_row[4] = { 2, 10, 6, 14 };

/* Whether the band from the probe row to this step's row can reach a
 * water shape's box: the row starts at (px, py) and moves (dpx, dpy) a
 * column, the probe row is tq (2.14) of the way from the camera. */
static int SEC_RENDER near_water(const R *r, fixed px, fixed py, fixed dpx, fixed dpy, int16_t tq)
{
	fixed ex = px + dpx * r->w, ey = py + dpy * r->w;
	int16_t x[4], y[4], x0, y0, x1, y1, k;
	const int16_t *b = r->wbox;

	x[0] = (int16_t)(px >> 16);
	y[0] = (int16_t)(py >> 16);
	x[1] = (int16_t)(ex >> 16);
	y[1] = (int16_t)(ey >> 16);
	x[2] = (int16_t)((r->camx + mulq(px - r->camx, tq)) >> 16);
	y[2] = (int16_t)((r->camy + mulq(py - r->camy, tq)) >> 16);
	x[3] = (int16_t)((r->camx + mulq(ex - r->camx, tq)) >> 16);
	y[3] = (int16_t)((r->camy + mulq(ey - r->camy, tq)) >> 16);
	x0 = x1 = x[0];
	y0 = y1 = y[0];
	for (k = 1; k < 4; k++) {
		if (x[k] < x0) x0 = x[k];
		if (x[k] > x1) x1 = x[k];
		if (y[k] < y0) y0 = y[k];
		if (y[k] > y1) y1 = y[k];
	}
	/* a cell's slack each way */
	for (k = 0; k < r->nwbox; k++, b += 4)
		if (x1 >= b[0] - 2 && x0 <= b[2] + 2 && y1 >= b[1] - 2 && y0 <= b[3] + 2) return 1;
	return 0;
}

/* Boxes round the hole's water shapes, in yards, into r->wbox. */
static void SEC_RENDER water_boxes(R *r, const Hole *h)
{
	const uint8_t *p = h->shapes;
	uint8_t i, j, n;
	r->nwbox = 0;
	if (h->base_surface == SURF_WATER) {
		r->wbox[0] = r->wbox[1] = 0;
		r->wbox[2] = (int16_t)h->width;
		r->wbox[3] = (int16_t)h->length;
		r->nwbox = 1;
	}
	if (!p) return;
	for (i = 0; i < h->shape_count; i++, p += 2 + (uint16_t)n * 4) {
		int16_t *b;
		n = p[1];
		if (p[0] != SURF_WATER) continue;
		b = r->wbox + 4 * (r->nwbox < WBOX_MAX ? r->nwbox : WBOX_MAX - 1);
		if (r->nwbox < WBOX_MAX) {
			b[0] = b[1] = 32767;
			b[2] = b[3] = -32767;
			r->nwbox++;
		}
		for (j = 0; j < n; j++) {
			const uint8_t *v = p + 2 + j * 4;
			int16_t x = (int16_t)(be16(v) >> 2), y = (int16_t)(be16(v + 2) >> 2);
			if (x < b[0]) b[0] = x;
			if (y < b[1]) b[1] = y;
			if (x + 1 > b[2]) b[2] = (int16_t)(x + 1);
			if (y + 1 > b[3]) b[3] = (int16_t)(y + 1);
		}
	}
}

/* The surface at (sx, sy), 16.16 yards; off the hole it's out of bounds,
 * or water where a water edge carries on. */
static uint8_t SEC_RENDER surf_at(const SurfaceGrid *g, fixed sx, fixed sy)
{
	int16_t cx = (int16_t)(sx >> (16 + SURF_CELL_SHIFT)), cy = (int16_t)(sy >> (16 + SURF_CELL_SHIFT));
	uint8_t c, off = 0, surf;
	if (sx < 0) { cx = 0; off = 1; }
	else if ((uint16_t)cx >= g->w) { cx = (int16_t)(g->w - 1); off = 1; }
	if (sy < 0) { cy = 0; off = 1; }
	else if ((uint16_t)cy >= g->h) { cy = (int16_t)(g->h - 1); off = 1; }
	c = g->cells[MULU16(cy, g->stride) + ((uint16_t)cx >> 1)];
	surf = (cx & 1) ? (c & 15) : (c >> 4);
	if (surf >= SURF_COUNT || (off && surf != SURF_WATER)) surf = SURF_OOB;
	return surf;
}

/* Bits for a sample of water past WATER_FAR: found only by the probe, and
 * with a dark line along its bank. */
#define WF_CAP  1
#define WF_EDGE 2
#define WF_FAR  4    /* any water past WATER_FAR */

/* Whether the probe point, r->tq of the way from the camera to (px, py), is water. */
static int SEC_RENDER probe_water(const R *r, fixed px, fixed py)
{
	return surf_at(r->g, r->camx + mulq(px - r->camx, r->tq), r->camy + mulq(py - r->camy, r->tq)) == SURF_WATER;
}

/* A bank of far water: a dark line along the bottom row of the span ys..ye
 * in column xx, and xx + 1 if pair. */
static void SEC_RENDER bank_line(R *r, int16_t xx, int16_t ys, int16_t ye, uint8_t pair)
{
	uint8_t c = r->lg == 3 ? (uint8_t)(PAL_TERRAIN + SURF_WATER * PAL_RAMP_N) : (uint8_t)PAL_BLACK, *p;
	int16_t yy;
	for (yy = (int16_t)(ye - r->scale); yy < ye; yy++) {
		if (yy < ys) continue;
		p = r->bits + MULU16(yy, r->rb) + ((uint16_t)xx >> (3 - r->lg));
		put(r, p, xx, yy, c);
		if (pair) put(r, p, xx + 1, yy, c);
	}
}

/* A span past WATER_FAR in column xx, and xx + 1 if pair, from y to ye:
 * far water (WF_FAR) is kept r->wmin rows tall from its near bank, taking
 * them from the ground beyond, and water found only by the probe is that
 * tall rather than the whole step; WF_EDGE puts a dark line along the
 * bottom. Drawn as the terrain loop would with no noise. */
static void SEC_RENDER far_span(R *r, int16_t xx, int16_t y, int16_t ye, uint8_t pair,
                                int16_t lv, uint8_t base, int16_t hb, uint8_t hm, uint8_t wf)
{
	uint16_t rb = r->rb;
	int16_t yy;
	uint8_t *p;

	if (wf & WF_FAR) {
		if (!r->wet[xx] && ((wf & WF_CAP) || ye - y < r->wmin)) {
			y = (int16_t)(ye - r->wmin);
			if (y < 0) y = 0;
		}
		r->wet[xx] = 1;
		if (pair) r->wet[xx + 1] = 1;
	}
	COUNT(r->st->spans++);
	COUNT(r->st->pixels += (ye - y) * (pair ? 2 : 1));
	r->ybuf[xx] = y;
	if (pair) r->ybuf[xx + 1] = y;
	p = r->bits + MULU16(y, rb) + ((uint16_t)xx >> (3 - r->lg));
	if (r->lg == 1) {
		uint8_t mk = (uint8_t)((pair ? 0xF0 : 0xC0) >> ((xx & 3) << 1)), nm = (uint8_t)~mk;
		const uint8_t *pb, *ph;
		int16_t l = (lv + 8) >> 4;
		if (l < 0) l = 0;
		if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
		pb = pal_pat2 + ((uint16_t)(base + l) << 2);
		ph = pal_pat2 + ((uint16_t)(PAL_HAZE + l) << 2);
		for (yy = y; yy < ye; yy++, p += rb) {
			uint8_t q = (uint8_t)(yy & 3);
			*p = (uint8_t)((*p & nm) | (((hm >> q) & 1 ? ph : pb)[q] & mk));
		}
	} else {
		uint8_t dith = r->lg == 3;
		for (yy = y; yy < ye; yy++, p += rb) {
			uint8_t tb = bayer[((yy & 3) << 2) | (xx & 3)], c;
			int16_t l = (lv + (dith ? tb : 8)) >> 4;
			if (l < 0) l = 0;
			if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
			c = (hb > ((tb * 7 + 3) & 15)) ? (uint8_t)(PAL_HAZE + l) : (uint8_t)(base + l);
			if (dith) *p = c;
			else put(r, p, xx, yy, c);
		}
	}
	if (wf & WF_EDGE) bank_line(r, xx, y, ye, pair);
}

static int SEC_RENDER terrain(Job *j, RenderClock clk, uint32_t until)
{
	R *r = &j->r;
	const Hole *h = r->h;
	const SurfaceGrid *g = r->g;
	fixed z = j->z, zl = j->zl;
	uint8_t on = j->on, dith = r->lg == 3, ks = (uint8_t)(SEG_SHIFT + (r->scale > 1));
	int16_t sand_drop, water_drop, stripe = dith ? 12 : 18, seg;
	uint16_t step = j->step, rb = r->rb, gw = g->w, gh = g->h, gs = g->stride;
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
		int16_t m, x, xe, hb, ns, ymax, ya, la, yn, ln, sand_px, water_px, cw, tq;
		uint8_t hm;
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
		r->hl = fz < HAZE_LIFT ? 0 : fz >= HAZE_LIFT + 256 ? 32 : (int16_t)((fz - HAZE_LIFT) >> 3);
		hb = fz < HAZE_MIX ? 0 : fz >= HAZE_MIX + 160 ? 16 : (int16_t)((fz - HAZE_MIX) / 10);
		/* at 2 bits, which pattern rows are haze at this depth, a bit each */
		hm = (uint8_t)((hb > haze_row[0]) | (hb > haze_row[1]) << 1 | (hb > haze_row[2]) << 2 | (hb > haze_row[3]) << 3);
		r->wmin = fz < WATER_FAR ? 0 : (int16_t)(WATER_MIN * r->scale);
		/* half way back to the last step, as a share of this depth, 2.14 */
		tq = 0;
		if (fz >= PROBE_Z && z > zl) {
			tq = (int16_t)(((z + zl) >> 1) / (z >> 14));
			r->st->divides++;
			if (!near_water(r, px, py, dpx, dpy, tq)) tq = 0;
		}
		r->tq = tq;
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
		la += r->hl;
		for (x = 0; x < r->w; ya = yn, la = ln) {
			int16_t yd, ld;
			spx += dpxs;
			spy += dpys;
			sample(r, spx, spy, m, sh, &yn, &ln);
			ln += r->hl;
			yd = (int16_t)(((yn - ya) >> kss) * cw);
			ld = (int16_t)(((ln - la) >> kss) * cw);
			for (xe = x + seg; x < xe; x += cw, px += dpxc, py += dpyc, ya += yd, la += ld) {
				int16_t yb = ybuf[x], y = ya >> 4, k;
				uint8_t surf, prev, wf;

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
						/* off the hole: out of bounds, or the sea carrying on past a water edge */
						uint8_t c;
						if (sx < 0) cx = 0;
						else if ((uint16_t)cx >= gw) cx = (int16_t)(gw - 1);
						if (sy < 0) cy = 0;
						else if ((uint16_t)cy >= gh) cy = (int16_t)(gh - 1);
						c = cells[MULU16(cy, gs) + ((uint16_t)cx >> 1)];
						surf = (cx & 1) ? (c & 15) : (c >> 4);
						if (surf != SURF_WATER) surf = SURF_OOB;
					} else {
						uint8_t c = cells[MULU16(cy, gs) + ((uint16_t)cx >> 1)];
						surf = (cx & 1) ? (c & 15) : (c >> 4);
						if (surf >= SURF_COUNT) surf = SURF_OOB;
					}
				}
				/* water found only by the probe is drawn wmin rows tall, not the whole step */
				wf = 0;
				if (tq && surf != SURF_WATER && !r->wet[x] && probe_water(r, px, py)) {
					surf = SURF_WATER;
					wf = WF_CAP;
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
						if (prev != SURF_BUNKER) lv = 4 + r->hl;    /* the near lip's shadow */
						break;
					case SURF_WATER:
						/* darker near, lighter far where it shows the sky, the
						 * lightest level (a pale sheen at 2 bits) from 132 yards */
						lv = 24 + (fz > 132 ? 132 : (int16_t)fz) * 2 / 3 + r->hl;
						if (r->wmin) wf |= WF_FAR;
						if (prev != SURF_WATER) {
							if (r->wmin) wf |= WF_EDGE;       /* far: a dark line along the near bank */
							else lv = 0;                      /* near: the bank's shadow */
						}
						break;
					default:
						if (prev == SURF_BUNKER) lv += 24;   /* the far lip faces the camera */
						if (prev == SURF_WATER && r->wmin) wf |= WF_EDGE;    /* and the far bank of water */
						break;
					}
					if (cw > 1) prevs[x + 1] = surf;

					/* the span in each column this sample covers */
					for (k = 0; k < cw; k++) {
						int16_t xx = x + k, ye = ybuf[xx];
						uint8_t *p, pair = 0;
						if (y >= ye) continue;
						if (wf) {
							/* far water or its bank, drawn on its own to keep this loop short */
							if (!k && cw > 1 && r->lg == 1 && ybuf[xx + 1] == ye) {
								pair = 1;
								k++;
							}
							far_span(r, xx, y, ye, pair, lv, base, hb, hm, wf);
							continue;
						}
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
								if (l0 >= 8 && l0 <= PAL_RAMP_N * 16 - 8) {
									/* the noise (-8 to 7) can't take the level off the ramp: no clamps */
									const int8_t *nzp = nzc + ((y & 15) << 4), *nze = nzc + 256;
									for (yy = y; yy < ye; yy++, p += rb) {
										int16_t l = (l0 + (*nzp >> ns)) >> 4;
										*p = (uint8_t)((*p & nm) | (pat[(l << 2) | (yy & 3)] & mk));
										nzp += 16;
										if (nzp >= nze) nzp -= 256;
									}
								} else for (yy = y; yy < ye; yy++, p += rb) {
									int16_t l = (l0 + (nzc[(yy & 15) << 4] >> ns)) >> 4;
									if (l < 0) l = 0;
									if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
									*p = (uint8_t)((*p & nm) | (pat[(l << 2) | (yy & 3)] & mk));
								}
							} else if (hm == 0 || hm == 15) {
								const uint8_t *pat;
								int16_t l = (lv + 8) >> 4;
								if (l < 0) l = 0;
								if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
								pat = pal_pat2 + ((uint16_t)((hm ? PAL_HAZE : base) + l) << 2);
								for (yy = y; yy < ye; yy++, p += rb)
									*p = (uint8_t)((*p & nm) | (pat[yy & 3] & mk));
							} else {
								/* haze a whole pattern row at a time */
								const uint8_t *pb, *ph;
								int16_t l = (lv + 8) >> 4;
								if (l < 0) l = 0;
								if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
								pb = pal_pat2 + ((uint16_t)(base + l) << 2);
								ph = pal_pat2 + ((uint16_t)(PAL_HAZE + l) << 2);
								for (yy = y; yy < ye; yy++, p += rb) {
									uint8_t q = (uint8_t)(yy & 3);
									*p = (uint8_t)((*p & nm) | (((hm >> q) & 1 ? ph : pb)[q] & mk));
								}
							}
						} else {
							for (yy = y; yy < ye; yy++, p += rb) {
								uint8_t c;
								SHADE(c, xx);
								put(r, p, xx, yy, c);
							}
						}
						/* a bank of far water: a dark line along the span's bottom row */
					}
				}
			}
		}

		/* Stop once nothing further can show: everything is filled up to where the highest ground could reach. */
		if (ymax <= 0) break;
		step++;
		zl = z;
		/* z / 16 near (z / 24 on 320 screens, z / 11 under NEAR_Z on 160 ones),
		 * 3 to 7 rows a step from the bottom of the view to 14 yards, then
		 * z / 21 from 32 yards and z / 11 from 160, where a step is under a row. */
		if (z < (32L << 16)) z += r->scale > 1 ? (z >> 5) + (z >> 7) : z < (NEAR_Z << 16) ? (z >> 4) + (z >> 5) : (z >> 4);
		else z += z < (160L << 16) ? (z >> 5) + (z >> 6) : (z >> 4) + (z >> 5);
		if (late(clk, until) && z < Z_FAR) {
			/* time's up: the march goes on from here next call */
			j->z = z;
			j->zl = zl;
			j->on = on;
			j->step = step;
			return 0;
		}
	}
	while (on < r->nobj) snap(r, &r->obj[on++]);
	return 1;
}
#undef SHADE

/* Puts the part of a PackBits run over k..e that's in lo..hi into out:
 * the literal bytes from src, or the byte *src repeated (rep). */
static void SEC_RENDER put_run(uint8_t *out, const uint8_t *src, uint8_t rep, uint16_t k, uint16_t e,
                               uint16_t lo, uint16_t hi)
{
	uint16_t a = k > lo ? k : lo, b = e < hi ? e : hi;
	if (rep) while (a < b) out[a++] = *src;
	else for (; a < b; a++) out[a] = src[a - k];
}

/* One PackBits row of n bytes from d at *pos, putting in out only the
 * bytes from lo to hi (hi may be past n, carrying on from 0). Returns -1
 * if cut short. */
static int SEC_RENDER unpack_row(const uint8_t *d, uint32_t len, uint32_t *pos, uint8_t *out,
                                 uint16_t n, uint16_t lo, uint16_t hi)
{
	uint32_t i = *pos;
	uint16_t k = 0, wrap = hi > n ? (uint16_t)(hi - n) : 0;

	if (hi > n) hi = n;
	while (k < n) {
		int8_t c;
		uint16_t cnt, e;
		if (i >= len) return -1;
		c = (int8_t)d[i++];
		if (c == -128) continue;
		cnt = (uint16_t)(c >= 0 ? c + 1 : 1 - c);
		e = (uint16_t)(k + cnt);
		if (e > n || (c >= 0 ? i + cnt > len : i >= len)) return -1;
		if (e > lo && k < hi) put_run(out, d + i, c < 0, k, e, lo, hi);
		if (k < wrap) put_run(out, d + i, c < 0, k, e, 0, wrap);
		i += c >= 0 ? cnt : 1;
		k = e;
	}
	*pos = i;
	return 0;
}

/* Sky, the backdrop strip and far haze above the terrain in each column. */
/* Where the sky ends and where the backdrop goes, for sky. */
static void SEC_RENDER sky_start(Job *j, const RenderData *bk, angle aim)
{
	R *r = &j->r;
	int16_t x, ymax = 0, bw = 0, bh = 0, btop = 0, off = 0;

	for (x = 0; x < r->w; x++)
		if (r->ybuf[x] > ymax) ymax = r->ybuf[x];
	if (bk->p && bk->len >= 4) {
		bw = (int16_t)be16(bk->p);
		bh = (int16_t)be16(bk->p + 2);
		if (bw <= 0 || bw > BKD_MAX * r->scale || bh <= 0 || bh > r->vh) bw = bh = 0;
		/* a whole turn is BKD_TURN pixels, a narrower strip repeats, and
		 * turning left moves it right */
		off = (int16_t)(((uint32_t)(uint16_t)(0u - aim) * (uint32_t)(BKD_TURN * r->scale)) >> 16);
		while (off >= bw && bw) off -= bw;
		btop = r->yh + 2 * r->scale - bh;
	}
	j->y = 0;
	j->ymax = ymax;
	j->bw = bw;
	j->bh2 = bh;
	j->btop = btop;
	j->off = off;
	j->pos = 4;
}

/* The sky rows from j->y down to where the ground starts, 8 rows between
 * looks at the clock. Returns 1 when they're done. */
static int SEC_RENDER sky(Job *j, const RenderData *bk, RenderClock clk, uint32_t until)
{
	R *r = &j->r;
	int16_t x, y, y0 = j->y, ymax = j->ymax, bw = j->bw, bh = j->bh2, btop = j->btop, off = j->off;
	uint32_t pos = j->pos;
	uint8_t dith = r->lg == 3;

	for (y = y0; y < ymax; y++) {
		uint8_t *row = 0, *p;
		int16_t sv = y < r->yh ? (int16_t)((int32_t)y * (16 * (PAL_SKY_N - 1)) / r->yh) : 16 * (PAL_SKY_N - 1);
		int16_t sx;
		if (y > y0 && !(y & 7) && late(clk, until)) {
			j->y = y;
			j->pos = pos;
			j->bw = bw;
			return 0;
		}
		if (bw && y >= btop && y < btop + bh) {
			if (unpack_row(bk->p, bk->len, &pos, r->row, (uint16_t)bw, (uint16_t)(bw > r->w ? off : 0), (uint16_t)(bw > r->w ? off + r->w : bw)) == 0) row = r->row;
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
	return 1;
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

/* The value pixel (x, y) gets for palette index c, as put writes it. */
static uint8_t SEC_RENDER pixval(const R *r, int16_t x, int16_t y, uint8_t c)
{
	uint8_t b = 0;
	put(r, &b, x, y, c);
	switch (r->lg) {
	case 3: return b;
	case 2: return (uint8_t)(x & 1 ? b & 15 : b >> 4);
	case 1: return (uint8_t)((b >> (6 - ((x & 3) << 1))) & 3);
	default: return (uint8_t)((b >> (7 - (x & 7))) & 1);
	}
}

static uint8_t SEC_RENDER getpx(const R *r, int16_t x, int16_t y)
{
	uint8_t b = *pixp(r, x, y);
	switch (r->lg) {
	case 3: return b;
	case 2: return (uint8_t)(x & 1 ? b & 15 : b >> 4);
	case 1: return (uint8_t)((b >> (6 - ((x & 3) << 1))) & 3);
	default: return (uint8_t)((b >> (7 - (x & 7))) & 1);
	}
}

/* Writes pixel value v at (x, y), or at the 2 x 2 pixels (2x, 2y) if dbl. */
static void SEC_RENDER setpx(const R *r, int16_t x, int16_t y, uint8_t v, uint8_t dbl)
{
	int16_t i, j, n = dbl ? 2 : 1;
	uint8_t *p, m, sh;

	if (dbl) { x <<= 1; y <<= 1; }
	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++) {
			int16_t px = x + i;
			p = r->bits + MULU16(y + j, r->rb) + ((uint16_t)px >> (3 - r->lg));
			switch (r->lg) {
			case 3: *p = v; break;
			case 2: sh = (uint8_t)(px & 1 ? 0 : 4); m = (uint8_t)(15 << sh); *p = (uint8_t)((*p & ~m) | (v << sh)); break;
			case 1: sh = (uint8_t)(6 - ((px & 3) << 1)); m = (uint8_t)(3 << sh); *p = (uint8_t)((*p & ~m) | (v << sh)); break;
			default: sh = (uint8_t)(7 - (px & 7)); m = (uint8_t)(1 << sh); *p = (uint8_t)((*p & ~m) | (v << sh)); break;
			}
		}
}

/* Palette index c at (x, y) in the view: put, or doubled with dbl. */
static void SEC_RENDER plot(const R *r, int16_t x, int16_t y, uint8_t c, uint8_t dbl)
{
	if (x < 0 || x >= r->w || y < 0 || y >= r->vh) return;
	if (dbl) setpx(r, x, y, pixval(r, x, y, c), 1);
	else put(r, pixp(r, x, y), x, y, c);
}

/* Spr8 frame d with its ball point on (bx, by), and the ball if ball isn't 0. */
static void SEC_RENDER golfer(const R *r, const RenderData *gd, int16_t bx, int16_t by, uint8_t ball, uint8_t dbl)
{
	uint16_t w, h, i, j;
	const uint8_t *px = sprite(gd, &w, &h);

	if (px) {
		int16_t x0 = bx - (int16_t)be16(gd->p + 8), y0 = by - (int16_t)be16(gd->p + 10);
		/* the columns on screen */
		int16_t e = r->w - x0;
		uint16_t i0 = x0 < 0 ? (uint16_t)-x0 : 0, i1 = e < 0 ? 0 : e < (int16_t)w ? (uint16_t)e : w;
		for (j = 0; j < h; j++, px += w) {
			int16_t y = y0 + (int16_t)j;
			uint8_t *row;
			if (y < 0 || y >= r->vh) continue;
			if (dbl) {
				for (i = i0; i < i1; i++)
					if (px[i]) plot(r, x0 + (int16_t)i, y, px[i], 1);
				continue;
			}
			row = r->bits + MULU16(y, r->rb);
			for (i = i0; i < i1; i++) {
				int16_t x = x0 + (int16_t)i;
				if (px[i]) put(r, row + ((uint16_t)x >> (3 - r->lg)), x, y, px[i]);
			}
		}
	}
	if (!ball) return;
	/* the ball: white, with a dark pixel under it on 160 wide screens so it shows on light grass */
	for (j = 0; j < r->scale; j++)
		for (i = 0; i < r->scale; i++) plot(r, bx + (int16_t)i, by - (int16_t)j, PAL_WHITE, dbl);
	if (r->scale == 1) plot(r, bx, by + 1, PAL_GREY + 1, dbl);
	/* On 4 greys only black stands out: the ball is 2 pixels wide with black
	 * either side and under it. */
	if (r->lg == 1 && r->scale == 1)
		for (i = 0; i < 5; i++) plot(r, bx + ball_dx[i], by + ball_dy[i], i ? PAL_BLACK : PAL_WHITE, dbl);
}

/* The box round every pose and the ball, kept to the view and to the
 * under buffer's size; w is 0 if there's nothing to keep. */
static void SEC_RENDER golfer_box(const R *r, RenderGolfer *g, int16_t bx, int16_t by)
{
	int16_t x0 = bx - 2, y0 = by - 2, x1 = bx + 4, y1 = by + 3, n = (int16_t)(RENDER_UNDER_UNITS * r->scale);
	uint16_t w, h, k;

	for (k = 0; k < RENDER_POSES; k++) {
		const RenderData *d = &g->pose[k];
		int16_t ax, ay;
		if (!sprite(d, &w, &h)) continue;
		ax = bx - (int16_t)be16(d->p + 8);
		ay = by - (int16_t)be16(d->p + 10);
		if (ax < x0) x0 = ax;
		if (ay < y0) y0 = ay;
		if (ax + (int16_t)w > x1) x1 = ax + (int16_t)w;
		if (ay + (int16_t)h > y1) y1 = ay + (int16_t)h;
	}
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > r->w) x1 = r->w;
	if (y1 > r->vh) y1 = r->vh;
	g->bx = bx;
	g->by = by;
	g->x = x0;
	g->y = y0;
	g->w = x1 > x0 && y1 > y0 && x1 - x0 <= n && y1 - y0 <= n ? x1 - x0 : 0;
	g->h = y1 - y0;
}

/* Keeps the pixels in the golfer's box in g->under (save) or puts them
 * back. Drawn at full size it keeps whole bytes of each row, so the pixels
 * either side of the box in the same bytes go back as they were drawn;
 * at half size (dbl) one byte a pixel, put back doubled. */
static void SEC_RENDER under_io(const R *r, const RenderGolfer *g, uint8_t save, uint8_t dbl)
{
	uint8_t *u = g->under;
	int16_t x, y;

	if (g->half) {
		for (y = g->y; y < g->y + g->h; y++)
			for (x = g->x; x < g->x + g->w; x++) {
				if (save) *u = getpx(r, x, y);
				else setpx(r, x, y, *u, dbl);
				u++;
			}
		return;
	}
	{
		uint8_t k = (uint8_t)(3 - r->lg);
		uint16_t b0 = (uint16_t)g->x >> k, n = (uint16_t)((((uint16_t)(g->x + g->w - 1)) >> k) - b0 + 1), i;
		for (y = g->y; y < g->y + g->h; y++, u += n) {
			uint8_t *p = r->bits + MULU16(y, r->rb) + b0;
			if (save) for (i = 0; i < n; i++) u[i] = p[i];
			else for (i = 0; i < n; i++) p[i] = u[i];
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

uint32_t render_under_size(const Surface *s)
{
	uint32_t n = (uint32_t)RENDER_UNDER_UNITS * (s->half && s->scale == 2 ? 1 : s->scale);
	return n * n;
}

uint32_t render_work_size(const Surface *s)
{
	int half = s->half && s->scale == 2;
	uint32_t w = (uint32_t)(half ? s->w >> 1 : s->w);
	return ((sizeof(Job) + 3) & ~3UL) + w * 2 + w * 2 + WBOX_MAX * 8 + sizeof(RObj) * MAX_OBJ + w * POOL_COLS * 2 +
	       BKD_MAX * (half ? 1 : s->scale) + 16;
}

/* Half size drawing (Surface.half): a surface over the top left quarter of
 * s's pixels at scale 1, drawn into and then doubled in place by double_up. */
static int SEC_RENDER half_of(const Surface *s, Surface *h)
{
	if (!s->half || s->scale != 2) return 0;
	*h = *s;
	h->w = (int16_t)(s->w >> 1);
	h->h = (int16_t)(s->h >> 1);
	h->scale = 1;
	h->half = 0;
	h->lw = h->w;
	h->lh = h->h;
	h->ox = h->oy = 0;
	return 1;
}

/* 4 pixels of 1 bit, doubled across. */
static const uint8_t dbl1[16] = {
	0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F, 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
};

/* Doubles the top left quarter of s across and down to fill it. Row y goes
 * into row 2y + 1, then that is copied to row 2y; working from the bottom
 * up, both rows are past every row still to be read. */
static void SEC_RENDER double_up(Surface *s)
{
	uint16_t n = (uint16_t)(((uint16_t)(s->w >> 1) << s->lg) >> 3), j;
	int16_t y;

	for (y = (int16_t)((s->h >> 1) - 1); y >= 0; y--) {
		const uint8_t *p = s->bits + MULU16(y, s->rowBytes);
		uint8_t *q = s->bits + MULU16(2 * y + 1, s->rowBytes), *o = q - s->rowBytes;
		for (j = 0; j < n; j++) {
			uint8_t b = p[j];
			switch (s->lg) {
			case 3:
				q[0] = q[1] = b;
				break;
			case 2:
				q[0] = (uint8_t)((b & 0xF0) | (b >> 4));
				q[1] = (uint8_t)((b & 0x0F) | (b << 4));
				break;
			case 1:
				q[0] = (uint8_t)(((b >> 6) * 0x50) | (((b >> 4) & 3) * 5));
				q[1] = (uint8_t)((((b >> 2) & 3) * 0x50) | ((b & 3) * 5));
				break;
			default:
				q[0] = dbl1[b >> 4];
				q[1] = dbl1[b & 15];
				break;
			}
			q += 2;
		}
		q -= n << 1;
		for (j = 0; j < (uint16_t)(n << 1); j++) o[j] = q[j];
	}
}

int render_scene(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats)
{
	if (render_scene_begin(s, v, a, work, stats) < 0) return -1;
	return render_scene_step(work, 0, 0) < 0 ? -1 : 0;
}

int render_pose(Surface *s, const RenderGolfer *g, uint8_t pose, uint8_t ball,
                int16_t *ox, int16_t *oy, int16_t *ow, int16_t *oh)
{
	R r;
	int16_t vh;
	uint8_t d = g->half;
	const uint8_t *u = g->under;
	uint16_t w, h;

	if (!g->w || !u || pose >= RENDER_POSES || !sprite(&g->pose[pose], &w, &h) || fit(s, &vh) < 0) return -1;
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.lg = (uint8_t)(s->depth == 8 ? 3 : s->depth == 4 ? 2 : s->depth == 2 ? 1 : 0);
	r.scale = (uint8_t)(d ? 1 : s->scale);
	r.w = d ? s->w >> 1 : s->w;
	r.vh = d ? vh >> 1 : vh;
	if (g->x + g->w > r.w || g->y + g->h > r.vh) return -1;
	under_io(&r, g, 0, d);
	golfer(&r, &g->pose[pose], g->bx, g->by, ball, d);
	*ox = d ? g->x << 1 : g->x;
	*oy = d ? g->y << 1 : g->y;
	*ow = d ? g->w << 1 : g->w;
	*oh = d ? g->h << 1 : g->h;
	return 0;
}

int render_scene_begin(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats)
{
	Job *j = (Job *)work;
	R *r;
	uint8_t *wp;
	Surface *d, hs;
	int16_t x, y;
	fixed groundz;
	uint8_t half = (uint8_t)half_of(s, &hs);

	if (fit(half ? &hs : s, &y) < 0) return -1;
	r = &j->r;
	j->s = *s;
	j->half = half;
	j->d = half ? hs : *s;
	d = &j->d;
	j->v = *v;
	j->a = a;
	if (a->golfer) a->golfer->half = j->half;
	fit(d, &r->vh);
	r->st = stats ? stats : &j->local;
	{
		RenderStats z = { 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0 } };
		*r->st = z;
	}
	r->bits = d->bits;
	r->rb = d->rowBytes;
	r->w = d->w;
	r->scale = d->scale;
	r->yh = (int16_t)(HORIZON * d->scale + fit(d, &y));
	r->f = (int16_t)(FOCAL * d->scale);
	r->cx = d->w / 2;
	r->lg = (uint8_t)(d->depth == 8 ? 3 : d->depth == 4 ? 2 : d->depth == 2 ? 1 : 0);
	r->h = v->hole;
	r->g = v->grid;
	r->hw = r->h->hm_w;
	r->hh = r->h->hm_h;
	r->mx = (fixed)(r->hw - 1) << 16;
	r->my = (fixed)(r->hh - 1) << 16;

	/* work after the job: y-buffer, last surfaces, objects, clip pool, a backdrop row; ints first for alignment */
	wp = (uint8_t *)work + ((sizeof(Job) + 3) & ~3UL);
	r->ybuf = (int16_t *)wp;
	wp += (uint32_t)r->w * 2;
	r->pool = (int16_t *)wp;
	r->pool_size = (uint16_t)(r->w * POOL_COLS);
	wp += (uint32_t)r->pool_size * 2;
	r->obj = (RObj *)wp;
	wp += sizeof(RObj) * MAX_OBJ;
	r->prev = wp;
	wp += r->w;
	r->wet = wp;
	wp += r->w;
	r->wbox = (int16_t *)wp;
	wp += WBOX_MAX * 8;
	r->row = wp;
	r->pool_used = 0;
	r->nobj = 0;
	water_boxes(r, v->hole);
	for (x = 0; x < r->w; x++) {
		r->ybuf[x] = r->vh;
		r->prev[x] = SURF_COUNT;
		r->wet[x] = 0;
	}

	/* camera */
	r->fx = icos(v->aim);
	r->fy = isin(v->aim);
	r->rx = r->fy;
	r->ry = (int16_t)-r->fx;
	r->camx = v->ball.x - mulq(CAM_BACK, r->fx);
	r->camy = v->ball.y - mulq(CAM_BACK, r->fy);
	j->bh = hb_at(r->h, v->ball.x, v->ball.y);
	groundz = r->h->base + fix_mul(j->bh << 8, r->h->step);
	r->camb = fix_div(groundz + cam_up(r->vh, r->yh, r->scale) - r->h->base, r->h->step) >> 8;
	r->stepf = (uint32_t)r->h->step * (uint32_t)r->f;
	j->stage = JS_GATHER;
	return 0;
}

int render_scene_step(void *work, RenderClock clk, uint32_t until)
{
	Job *j = (Job *)work;
	R *r = &j->r;
	const RenderArt *a = j->a;
	int16_t x, y, bx, by;
	uint32_t t = lap(r->st, 0, 0);

	/* a stage at a time, the terrain, sky and objects in pieces, until the time's up */
	do {
		switch (j->stage) {
		case JS_GATHER:
			gather(r, &j->v, a);
			t = lap(r->st, RS_GATHER, t);
			j->z = j->zl = near_start(r);
			j->on = 0;
			j->step = 0;
			j->stage = JS_TERRAIN;
			break;
		case JS_TERRAIN:
			x = (int16_t)terrain(j, clk, until);
			t = lap(r->st, RS_TERRAIN, t);
			if (!x) return 0;
			sky_start(j, &a->backdrop, (angle)(j->v.aim + j->v.hole->turn));
			j->stage = JS_SKY;
			break;
		case JS_SKY:
			x = (int16_t)sky(j, &a->backdrop, clk, until);
			t = lap(r->st, RS_SKY, t);
			if (!x) return 0;
			j->k = r->nobj;
			j->stage = JS_OBJECTS;
			break;
		case JS_OBJECTS:
			/* back to front */
			while (j->k > 0) {
				draw_obj(r, &r->obj[--j->k]);
				if (j->k > 0 && late(clk, until)) break;
			}
			t = lap(r->st, RS_OBJECTS, t);
			if (j->k > 0) return 0;
			j->stage = JS_GOLFER;
			break;
		case JS_GOLFER:
			{
				int16_t m;
				uint8_t sh;
				kz(r, r->stepf, (uint32_t)CAM_BACK, &m, &sh);
				bx = r->cx;
				by = (int16_t)(r->yh + (MULS16(dz6(r->camb, j->bh), m) >> sh));
			}
			if (a->golfer) {
				RenderGolfer *g = a->golfer;
				golfer_box(r, g, bx, by);
				if (g->w && g->under) {
					under_io(r, g, 1, 0);
				} else {
					g->w = 0;
				}
				g->shown = POSE_ADDRESS;
				g->ball = 1;
				golfer(r, &g->pose[POSE_ADDRESS], bx, by, 1, 0);
			}
			t = lap(r->st, RS_GOLFER, t);
			/* the status strip, black for now */
			for (y = r->vh; y < j->d.h; y++) {
				uint8_t *p = r->bits + MULU16(y, r->rb);
				for (x = 0; x < (int16_t)r->rb; x++) p[x] = 0xFF;
			}
			j->stage = j->half ? JS_DOUBLE : JS_DONE;
			break;
		case JS_DOUBLE:
			double_up(&j->s);
			j->stage = JS_DONE;
			break;
		default:
			return 1;
		}
	} while (j->stage != JS_DONE && !late(clk, until));
	return j->stage == JS_DONE;
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

static int SEC_RENDER map_draw(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1);

int render_map(Surface *s, const RenderView *v, RenderMap *out)
{
	return render_map_box(s, v, 0, out);
}

int render_map_box(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out)
{
	return render_map_rows(s, v, box, out, 0, 0x7FFF) < 0 ? -1 : 0;
}

int render_map_rows(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1)
{
	Surface h;
	int k;
	if (!half_of(s, &h)) return map_draw(s, v, box, out, y0, y1);
	/* at half size: rows of the half view, all doubled at the end */
	k = map_draw(&h, v, box, out, (int16_t)(y0 >> 1), y1 >= 0x7FFE ? 0x7FFF : (int16_t)((y1 + 1) >> 1));
	if (k <= 0) return k;
	double_up(s);
	/* where things are on s */
	out->bx = (int16_t)(out->bx * 2);
	out->by = (int16_t)(out->by * 2);
	out->px = (int16_t)(out->px * 2);
	out->py = (int16_t)(out->py * 2);
	out->left = (int16_t)(out->left * 2);
	out->top = (int16_t)(out->top * 2);
	out->ypp >>= 1;
	return 1;
}

static int SEC_RENDER map_draw(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1)
{
	R r;
	const Hole *h = v->hole;
	const SurfaceGrid *g = v->grid;
	fixed ypp, a, b, x0, wy;
	int16_t x, y, left, top, mw, mh, sc;
	uint16_t i;
	HoleObject o;
	uint8_t col[SURF_COUNT], xcell[480];

	if (fit(s, &r.vh) < 0) return -1;
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.w = s->w;
	sc = s->scale;
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
	if (box) {
		/* the box in the middle, if that's bigger than the whole hole */
		fixed bw = box->x1 - box->x0, bh = box->y1 - box->y0, ybox;
		a = bw / (r.w - 4 * sc);
		b = bh / (r.vh - 4 * sc);
		ybox = a > b ? a : b;
		if (bw > 0 && bh > 0 && ybox < ypp) {
			ypp = ybox < 1 ? 1 : ybox;
			mw = (int16_t)(((fixed)h->width << 16) / ypp);
			mh = (int16_t)(((fixed)h->length << 16) / ypp);
			left = (int16_t)((r.w - bw / ypp) / 2 - box->x0 / ypp);
			top = (int16_t)((r.vh - bh / ypp) / 2 - (((fixed)h->length << 16) - box->y1) / ypp);
		}
	}

	/* surfaces at the middle of their ramps, out of bounds dark; off the hole is black (0xFF at every depth) */
	for (i = 0; i < SURF_COUNT; i++)
		col[i] = (uint8_t)(PAL_TERRAIN + i * PAL_RAMP_N + PAL_RAMP_N / 2);
	col[SURF_OOB] = PAL_TERRAIN + SURF_OOB * PAL_RAMP_N + 1;
	/* the grid column under each screen column, 0xFF off the hole */
	x0 = (ypp >> 1) + (left < 0 ? (fixed)-left * ypp : 0);
	for (x = 0; x < r.w; x++) {
		xcell[x] = 0xFF;
		if (x >= left && x < left + mw) {
			int32_t cx = x0 >> (16 + SURF_CELL_SHIFT);
			xcell[x] = (uint8_t)(cx >= g->w ? g->w - 1 : cx);
			x0 += ypp;
		}
	}
	if (y0 < 0) y0 = 0;
	if (y1 > r.vh) y1 = r.vh;
	for (y = y0; y < y1; y++) {
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
	if (y1 < r.vh) return 0;

#define MX(wx) ((int16_t)(left + (wx) / ypp))
#define MY(wy) ((int16_t)(top + (((fixed)h->length << 16) - (wy)) / ypp))
	/* trees, bushes and gorse as dark dots, rocks, walls and houses grey */
	for (i = 0; i < h->object_count; i++) {
		hole_object(h, i, &o);
		if (o.type == OBJ_ROCK || o.type == OBJ_WALL) dot(&r, MX(o.pos.x), MY(o.pos.y), sc, PAL_ROCK + 3);
		else if (o.type == OBJ_HOUSE || o.type == OBJ_LANDMARK) dot(&r, MX(o.pos.x) - sc, MY(o.pos.y) - sc, 2 * sc + 1, PAL_WALL + 2);
		else if ((o.type >= OBJ_TREE && o.type <= OBJ_BUSH) || o.type == OBJ_GORSE)
			dot(&r, MX(o.pos.x) - (sc >> 1), MY(o.pos.y) - (sc >> 1), sc + 1, PAL_TREE + 1);
	}
	/* the tee: a white square with a black edge */
	x = MX(h->tees[h->tee].pos.x);
	y = MY(h->tees[h->tee].pos.y);
	dot(&r, x - sc, y - sc, 3 * sc, PAL_BLACK);
	dot(&r, x, y, sc, PAL_WHITE);

	/* the aim line from the ball to the pin, dashed black and white; not on a framed map */
	out->bx = MX(v->ball.x);
	out->by = MY(v->ball.y);
	out->px = MX(h->pins[v->pin & 3].x);
	out->py = MY(h->pins[v->pin & 3].y);
	out->yards = (uint16_t)((vec2_len(vec2_sub(h->pins[v->pin & 3], v->ball)) + FIX_HALF) >> 16);
	out->left = left;
	out->top = top;
	out->ypp = ypp;
	out->length = (fixed)h->length << 16;
	if (!box) {
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
	if (!box) {
		dot(&r, out->bx - sc, out->by - sc, 3 * sc, PAL_BLACK);
		dot(&r, out->bx, out->by, sc, PAL_WHITE);
	}
#undef MX
#undef MY

	/* the status strip, black */
	for (y = r.vh; y < s->h; y++) {
		uint8_t *p = r.bits + MULU16(y, r.rb);
		for (x = 0; x < (int16_t)r.rb; x++) p[x] = 0xFF;
	}
	return 1;
}

/* a * b as fix_mulf, here so the green view doesn't call out of its segment. */
static fixed SEC_RENDER mulf(fixed a, fixed b)
{
	uint32_t ua, ub, r;
	int neg = 0;

	if (a < 0) { ua = 0u - (uint32_t)a; neg = 1; } else ua = (uint32_t)a;
	if (b < 0) { ub = 0u - (uint32_t)b; neg ^= 1; } else ub = (uint32_t)b;
	r = (MULU16(ua >> 16, ub >> 16) << 16)
	    + MULU16(ua >> 16, ub) + MULU16(ua, ub >> 16)
	    + (MULU16(ua, ub) >> 16);
	return neg ? -(fixed)r : (fixed)r;
}

/* Ground height in yards. */
static fixed SEC_RENDER ground_z(const Hole *h, fixed x, fixed y)
{
	return h->base + mulf(hb_at(h, x, y) * 256, h->step);
}

/* The green view's camera. Everything scales with the putt's length (at
 * least 4 and at most 40 yards), so the ball always shows low in the middle
 * and the pin a third of the way down: GREEN_BACK of it behind the ball,
 * GREEN_UP of it above, looking down by GREEN_PITCH. */
#define GREEN_PITCH 6300         /* 34.6 degrees */
#define GREEN_BACK  14746        /* 0.9, 2.14 */
#define GREEN_ZK    6            /* heights shown this many times as steep */
#define GREEN_FOCAL 200
#define GREEN_ROW   72           /* the row straight ahead on 160 wide screens */
#define GREEN_NEAR  (4L << 16)
#define GREEN_FAR   (40L << 16)

static fixed SEC_RENDER green_cam(const Surface *s, const RenderView *v, RenderCam *c)
{
	const Hole *h = v->hole;
	vec2 pin = h->pins[v->pin & 3];
	fixed len = vec2_len(vec2_sub(pin, v->ball)), back, zb;
	int16_t vh;

	if (len < GREEN_NEAR) len = GREEN_NEAR;
	if (len > GREEN_FAR) len = GREEN_FAR;
	back = mulq(len, GREEN_BACK);
	c->fx = icos(v->aim);
	c->fy = isin(v->aim);
	c->rx = c->fy;
	c->ry = (int16_t)-c->fx;
	c->pc = icos(GREEN_PITCH);
	c->ps = isin(GREEN_PITCH);
	c->camx = v->ball.x - mulq(back, c->fx);
	c->camy = v->ball.y - mulq(back, c->fy);
	c->zref = ground_z(h, pin.x, pin.y);
	zb = ground_z(h, v->ball.x, v->ball.y);
	c->zk = GREEN_ZK;
	c->camz = c->zref + (zb - c->zref) * GREEN_ZK + len;
	c->f = (int16_t)(GREEN_FOCAL * s->scale);
	c->cx = s->w / 2;
	c->yh = (int16_t)(GREEN_ROW * s->scale + fit(s, &vh));
	c->back = (int16_t)(back >> 16);
	return len;
}

static int16_t SEC_RENDER persp(fixed v, int16_t f, fixed d);

/* One row of grid points: screen position (x is NO_PT if it's behind the
 * camera) and ground height in heightmap byte steps, 8.8. */
#define NO_PT  (-32768)
#define GRID_N 17
typedef struct {
	int16_t x, y;
	uint16_t hb;
} GPt;

/* Fills a triangle with surface ramp base at slope light lv, dithered. */
static void SEC_RENDER tri(const R *r, const GPt *a, const GPt *b, const GPt *c, uint8_t base, int16_t lv)
{
	const GPt *t;
	int32_t xl, xr, dl, dr, d2;
	int16_t y, y0, ym, y1, x, xa, xb;

	/* a at the top, c at the bottom */
	if (b->y < a->y) { t = a; a = b; b = t; }
	if (c->y < a->y) { t = a; a = c; c = t; }
	if (c->y < b->y) { t = b; b = c; c = t; }
	y0 = a->y; ym = b->y; y1 = c->y;
	if (y1 <= y0 || y1 < 0 || y0 >= r->vh) return;
	d2 = ((int32_t)(c->x - a->x) * 65536L) / (y1 - y0);
	xl = (int32_t)a->x * 65536L;
	xr = xl;
	dl = ym > y0 ? ((int32_t)(b->x - a->x) * 65536L) / (ym - y0) : 0;
	dr = d2;
	/* rows above the view only move the edges on */
	y = y0;
	if (y < 0) {
		int16_t k = (int16_t)-y0, ks = k < ym - y0 ? k : (int16_t)(ym - y0);
		xl += dl * ks;
		xr += dr * k;
		if (ym < 0 && c->y > ym) {
			dl = ((int32_t)(c->x - b->x) * 65536L) / (c->y - ym);
			xl = (int32_t)b->x * 65536L + dl * (-ym);
		}
		y = 0;
	}
	if (y1 > r->vh) y1 = r->vh;
	for (; y < y1; y++) {
		if (y == ym) {
			/* the short side turns at b */
			xl = (int32_t)b->x * 65536L;
			dl = ((int32_t)(c->x - b->x) * 65536L) / (c->y - ym);
		}
		if (y >= 0 && y < r->vh) {
			uint8_t *row = r->bits + MULU16(y, r->rb);
			xa = (int16_t)((xl + 32768L) >> 16);
			xb = (int16_t)((xr + 32768L) >> 16);
			if (xa > xb) { x = xa; xa = xb; xb = x; }
			if (xa < 0) xa = 0;
			if (xb > r->w - 1) xb = (int16_t)(r->w - 1);
			if (r->lg == 1 && xa <= xb) {
				/* 2 bits: the level's pattern row, whole bytes in the middle */
				uint8_t pat = pal_pat2[((uint16_t)(base + ((lv + 8) >> 4)) << 2) | (y & 3)], m;
				uint8_t *p = row + (xa >> 2), *pe = row + (xb >> 2);
				m = (uint8_t)(0xFF >> ((xa & 3) << 1));
				if (p == pe) m &= (uint8_t)(0xFF << ((3 - (xb & 3)) << 1));
				*p = (uint8_t)((*p & ~m) | (pat & m));
				if (p != pe) {
					for (p++; p < pe; p++) *p = pat;
					m = (uint8_t)(0xFF << ((3 - (xb & 3)) << 1));
					*p = (uint8_t)((*p & ~m) | (pat & m));
				}
			} else {
				for (x = xa; x <= xb; x++) {
					int16_t l = (int16_t)((lv + bayer[((y & 3) << 2) | (x & 3)]) >> 4);
					if (l > PAL_RAMP_N - 1) l = PAL_RAMP_N - 1;
					put(r, row + ((uint16_t)x >> (3 - r->lg)), x, y, (uint8_t)(base + l));
				}
			}
		}
		xl += dl;
		xr += dr;
	}
}

/* A one pixel line, clipped to the view. */
static void SEC_RENDER gline(const R *r, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c)
{
	int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int16_t sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1, e = dx - dy, e2, n;

	uint8_t fast = r->lg == 1 && c == PAL_BLACK;

	if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || (x0 >= r->w && x1 >= r->w) || (y0 >= r->vh && y1 >= r->vh)) return;
	for (n = dx + dy + 1; n > 0; n--) {
		if (x0 >= 0 && x0 < r->w && y0 >= 0 && y0 < r->vh) {
			/* black on 2 bits is both bits set */
			if (fast) *(r->bits + MULU16(y0, r->rb) + (x0 >> 2)) |= (uint8_t)(0xC0 >> ((x0 & 3) << 1));
			else put(r, pixp(r, x0, y0), x0, y0, c);
		}
		if (x0 == x1 && y0 == y1) break;
		e2 = (int16_t)(2 * e);
		if (e2 > -dy) { e -= dy; x0 += sx; }
		if (e2 < dx) { e += dx; y0 += sy; }
	}
}

/* The green view being drawn (render_green_begin and _step), at the start
 * of the work memory. */
typedef struct {
	R r;
	RenderCam c;
	RenderView v;
	Surface s;                  /* the surface asked for */
	uint8_t half, line, done;
	GPt rows[2][GRID_N], *far, *near;
	fixed len, sp;
	int16_t k, k0, k1, n;       /* the next row of cells, the first and last, and the cells either side */
	int32_t hbb;
	uint32_t past;              /* cells with lines in the row of cells past this one */
} GJob;

int render_green(Surface *s, const RenderView *v)
{
	GJob j;
	if (render_green_begin(s, v, &j) < 0) return -1;
	render_green_step(&j, 0, 0);
	return 0;
}

int render_green_begin(Surface *s, const RenderView *v, void *work)
{
	GJob *j = (GJob *)work;
	Surface hs, *d;
	R *r;
	const Hole *h = v->hole;
	fixed len, sp;
	int16_t x, y, n;
	uint8_t half = (uint8_t)half_of(s, &hs);

	d = half ? &hs : s;
	if (fit(d, &y) < 0) return -1;
	r = &j->r;
	j->s = *s;
	j->half = half;
	j->v = *v;
	j->done = 0;
	fit(d, &r->vh);
	r->bits = d->bits;
	r->rb = d->rowBytes;
	r->w = d->w;
	r->lg = (uint8_t)(d->depth == 8 ? 3 : d->depth == 4 ? 2 : d->depth == 2 ? 1 : 0);
	j->line = d->depth == 8 ? PAL_GREEN : PAL_BLACK;
	j->len = len = green_cam(d, v, &j->c);
	j->hbb = hb_at(h, v->ball.x, v->ball.y);

	/* black, then the grid over it */
	for (y = 0; y < d->h; y++) {
		uint8_t *p = r->bits + MULU16(y, r->rb);
		for (x = 0; x < (int16_t)r->rb; x++) p[x] = 0xFF;
	}

	/* cells of half a yard, 1, 2, 4 or 8, so there are at most 16 along, from
	 * 0.4 of the length behind the ball to 2.6 past it, and as many across;
	 * the ball and the pin's line sit in the middle of a cell */
	sp = FIX_HALF;
	while (len * 3 > sp * 16) sp <<= 1;
	j->sp = sp;
	j->k0 = (int16_t)-((len * 2 / 5 + sp - 1) / sp);
	j->k1 = (int16_t)((len * 13 / 5 + sp - 1) / sp);
	n = (int16_t)((len * 7 / 5 + sp - 1) / sp);
	if (n > GRID_N / 2) n = GRID_N / 2;
	j->n = n;
	j->k = j->k1;
	j->past = 0;
	j->far = j->rows[0];
	j->near = j->rows[1];
	return 0;
}

int render_green_step(void *work, RenderClock clk, uint32_t until)
{
	GJob *j = (GJob *)work;
	R *r = &j->r;
	const RenderView *v = &j->v;
	const Hole *h = v->hole;
	const RenderCam *cp = &j->c;
	RenderCam c = *cp;
	GPt *far = j->far, *near = j->near, *t;
	fixed len = j->len, sp = j->sp, a, up, depth, down;
	vec2 pin = h->pins[v->pin & 3];
	int16_t k, k0 = j->k0, k1 = j->k1, i, n = j->n, px, py;
	int32_t hbb = j->hbb;
	uint32_t lined, past = j->past;
	uint8_t line = j->line;

	if (j->done) return 1;
	/* each row of points from the far end, then the cells between it and the row past it */
	for (k = j->k; k >= k0; k--) {
		/* the points of row k, along the aim from the ball */
		a = (fixed)k * sp;
		for (i = -n; i <= n; i++) {
			GPt *g = &near[i + n];
			fixed cx = (fixed)i * sp + sp / 2;
			fixed wx = v->ball.x + mulq(a, c.fx) + mulq(cx, c.rx), wy = v->ball.y + mulq(a, c.fy) + mulq(cx, c.ry);
			int32_t hb = hb_at(h, wx, wy);
			fixed along = a + ((fixed)c.back << 16);
			g->hb = (uint16_t)hb;
			up = mulf((hb - hbb) * 256, h->step) * GREEN_ZK - len;
			depth = mulq(along, c.pc) - mulq(up, c.ps);
			down = -(mulq(along, c.ps) + mulq(up, c.pc));
			if (depth < FIX_HALF) {
				g->x = NO_PT;
				continue;
			}
			g->x = persp(cx, c.f, depth);
			g->y = persp(down, c.f, depth);
			if (g->x > 4000) g->x = 4000;
			if (g->x < -4000) g->x = -4000;
			if (g->y > 4000) g->y = 4000;
			if (g->y < -4000) g->y = -4000;
			g->x += c.cx;
			g->y += c.yh;
		}
		if (k < k1) {
			/* the cells between this row and the one past it, left to right, then
			 * the lines round the ones on the green or fringe, so no fill covers them */
			lined = 0;
			for (i = 0; i < 2 * n; i++) {
				GPt *f0 = &far[i], *f1 = &far[i + 1], *n0 = &near[i], *n1 = &near[i + 1];
				fixed ca = a + sp / 2, cc = (fixed)(i - n) * sp + sp, rise;
				uint8_t sf;
				int16_t lv;
				if (f0->x == NO_PT || f1->x == NO_PT || n0->x == NO_PT || n1->x == NO_PT) continue;
				sf = surface_at(v->grid, v->ball.x + mulq(ca, c.fx) + mulq(cc, c.rx), v->ball.y + mulq(ca, c.fy) + mulq(cc, c.ry));
				if (sf >= SURF_COUNT) sf = SURF_OOB;
				/* lit from the left and behind: the rise to the right and half the rise away, per cell */
				rise = mulf((((int32_t)f1->hb + n1->hb - f0->hb - n0->hb) + (((int32_t)f0->hb + f1->hb - n0->hb - n1->hb) >> 1)) * 128, h->step);
				lv = (int16_t)(LIGHT_FLAT + ((((rise * 256) / (sp >> 8)) >> 4) * 64 >> 8));
				if (lv < 0) lv = 0;
				if (lv > PAL_RAMP_N * 16 - 16) lv = PAL_RAMP_N * 16 - 16;
				tri(r, f0, f1, n1, (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N), lv);
				tri(r, f0, n1, n0, (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N), lv);
				if (sf == SURF_GREEN || sf == SURF_FRINGE) lined |= 1UL << i;
			}
			/* each edge once: across the far side if this cell or the one past it
			 * is lined (the fill just covered it), along the sides, and the near
			 * side on the nearest row */
			for (i = 0; i <= 2 * n; i++) {
				uint32_t here = lined | (lined << 1);
				if (i < 2 * n && ((lined | past) & (1UL << i)) && far[i].x != NO_PT && far[i + 1].x != NO_PT)
					gline(r, far[i].x, far[i].y, far[i + 1].x, far[i + 1].y, line);
				if ((here & (1UL << i)) && far[i].x != NO_PT && near[i].x != NO_PT)
					gline(r, far[i].x, far[i].y, near[i].x, near[i].y, line);
				if (k == k0 && i < 2 * n && (lined & (1UL << i)))
					gline(r, near[i].x, near[i].y, near[i + 1].x, near[i + 1].y, line);
			}
			past = lined;
		}
		t = far;
		far = near;
		near = t;
		if (k > k0 && late(clk, until)) {
			j->k = (int16_t)(k - 1);
			j->far = far;
			j->near = near;
			j->past = past;
			return 0;
		}
	}

	/* the cup, black in a white lip, and the pin in it: a pole with the flag to its right */
	if (render_point(&c, pin.x, pin.y, ground_z(h, pin.x, pin.y), &px, &py, &depth)) {
		int16_t u = j->half ? 1 : j->s.scale, top, tx, jj;
		/* 2.4 yards, or less on a short putt so the flag stays in view; heights show GREEN_ZK times */
		fixed pole = len / 3 < (fixed)(FIX_ONE * 12 / 5) ? len / 3 : (fixed)(FIX_ONE * 12 / 5);
		pole /= GREEN_ZK;
		for (jj = -2 * u; jj < 2 * u; jj++)
			for (i = -4 * u; i < 4 * u; i++) {
				int16_t ax = i < 0 ? -i - 1 : i, ay = jj < 0 ? -jj - 1 : jj;
				/* an ellipse twice as wide as tall: the lip, then the hole a pixel in */
				int32_t q = MULS16(ax, ax) + 4 * MULS16(ay, ay), rr = 16L * u * u;
				if (q >= rr || px + i < 0 || px + i >= r->w || py + jj < 0 || py + jj >= r->vh) continue;
				put(r, pixp(r, px + i, py + jj), px + i, py + jj,
				    MULS16(ax + u, ax + u) + 4 * MULS16(ay + u, ay + u) < rr ? PAL_BLACK : PAL_WHITE);
			}
		if (render_point(&c, pin.x, pin.y, ground_z(h, pin.x, pin.y) + pole, &tx, &top, &depth)) {
			gline(r, px, py - u, tx, top, line == PAL_GREEN ? PAL_GREY + 7 : PAL_BLACK);
			for (jj = 0; jj < 4 * u; jj++)
				for (i = 1; i <= 6 * u; i++)
					if (tx + i >= 0 && tx + i < r->w && top + jj >= 0 && top + jj < r->vh)
						put(r, pixp(r, tx + i, top + jj), tx + i, top + jj,
						    (uint8_t)(jj >= 3 * u || i == 6 * u ? PAL_FLAG : PAL_FLAG + 2));
		}
	}
	if (j->half) double_up(&j->s);
	j->done = 1;
	return 1;
}

static void SEC_RENDER scene_cam(const Surface *s, const RenderView *v, RenderCam *c)

{
	int16_t vh;
	int32_t bh = hb_at(v->hole, v->ball.x, v->ball.y);

	c->fx = icos(v->aim);
	c->fy = isin(v->aim);
	c->rx = c->fy;
	c->ry = (int16_t)-c->fx;
	c->camx = v->ball.x - mulq(CAM_BACK, c->fx);
	c->camy = v->ball.y - mulq(CAM_BACK, c->fy);
	c->f = (int16_t)(FOCAL * s->scale);
	c->cx = s->w / 2;
	c->yh = (int16_t)(HORIZON * s->scale + fit(s, &vh));
	c->camz = v->hole->base + fix_mul(bh << 8, v->hole->step) + cam_up(vh, c->yh, s->scale);
	c->pc = 16384;
	c->ps = 0;
	c->zref = 0;
	c->zk = 1;
	c->back = (int16_t)(CAM_BACK >> 16);
}

void render_camera(const Surface *s, const RenderView *v, RenderCam *c)
{
	if (v->green) green_cam(s, v, c);
	else scene_cam(s, v, c);
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
	fixed dx = x - c->camx, dy = y - c->camy, side, along, up, down;
	int32_t px, py;

	/* along the ground and up, then turned by the look down */
	along = mulq(dx, c->fx) + mulq(dy, c->fy);
	up = c->zref + (z - c->zref) * c->zk - c->camz;
	*depth = mulq(along, c->pc) - mulq(up, c->ps);
	down = -(mulq(along, c->ps) + mulq(up, c->pc));
	if (*depth < FIX_ONE) return 0;
	side = mulq(dx, c->rx) + mulq(dy, c->ry);
	if (side > (fixed)(4000L << 16) || side < -(fixed)(4000L << 16)) return 0;
	px = (int32_t)c->cx + persp(side, c->f, *depth);
	py = (int32_t)c->yh + persp(down, c->f, *depth);
	/* clamped so callers can add sizes without overflowing 16 bits */
	*sx = (int16_t)(px > 1000 ? 1000 : px < -1000 ? -1000 : px);
	*sy = (int16_t)(py > 1000 ? 1000 : py < -1000 ? -1000 : py);
	return 1;
}
