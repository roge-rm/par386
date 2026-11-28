/* The view from behind the ball (render.h).
 *
 * The camera looks level along the aim line, so verticals stay vertical.
 * On longer shots it backs up and rises to show more ground ahead. Its
 * height puts the ball BALL_GAP rows above the bottom of the view, with
 * the golfer in the foreground at the sprite's size.
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
#include "ball.h"
#include "pal.h"
#include "paltab.h"
#include "profile.h"
#include "terra.h"

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
	int16_t *ybuf;
	uint8_t *prev;              /* surface of the last sample in each column */
	uint8_t *wet;               /* 1 in a column that has drawn far water: it isn't raised or probed again */
	int16_t *wbox;              /* each water shape's box in yards: x0, y0, x1, y1 */
	uint8_t nwbox;
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
	struct ContourWork *contours; /* optional projected shapes, after the normal work */
	struct MeshWork *mesh;       /* optional tiled terrain */
	TerraRow t;                 /* the terrain's depth step */
	TerraSky ts;                /* a sky row */
	RenderStats local;          /* the stats when the caller wants none */
	RenderView v;
	Surface s;                  /* the surface asked for */
	Surface d;                  /* the one drawn into: s, or its top left quarter at half size */
	const RenderArt *a;
	int32_t bh;                 /* the ground at the ball, heightmap byte steps */
	fixed back, z, zl;          /* camera setback and the terrain's next and last depth steps, yards */
	uint16_t step;
	uint8_t on;                 /* objects given their clip columns so far */
	uint8_t stage;              /* JS_* */
	uint8_t half;
	int16_t y, ymax, bw, bh2, btop, off;    /* sky: the next row, where the sky ends, the backdrop */
	uint32_t pos;               /* the backdrop's next packed row */
	int16_t k;                  /* objects still to draw */
} Job;

enum { JS_GATHER, JS_TERRAIN, JS_SKY, JS_OBJECTS, JS_GOLFER, JS_DOUBLE, JS_DONE, JS_MESH, JS_EDGES };

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
static __inline__ int32_t hb_sample(const Hole *h, fixed x, fixed y)
{
	fixed gx = x >> h->hm_shift, gy = y >> h->hm_shift;
	fixed mx = (fixed)(h->hm_w - 1) << 16, my = (fixed)(h->hm_h - 1) << 16;
	uint16_t i, j, i1, fx, fy, hw = h->hm_w;    /* 16 bits, or MULU16 isn't one mulu.w */
	const uint8_t *r0, *r1;
	int32_t t, b;

	if (gx < 0) gx = 0;
	if (gx > mx) gx = mx;
	if (gy < 0) gy = 0;
	if (gy > my) gy = my;
	i = (uint16_t)(gx >> 16); fx = (uint16_t)((gx >> 8) & 255);
	j = (uint16_t)(gy >> 16); fy = (uint16_t)((gy >> 8) & 255);
	i1 = gx < mx ? i + 1 : i;
	r0 = h->heights + MULU16(j, hw);
	r1 = gy < my ? r0 + hw : r0;
	t = ((int32_t)r0[i] << 8) + MULS16(r0[i1] - r0[i], fx);
	b = ((int32_t)r1[i] << 8) + MULS16(r1[i1] - r1[i], fx);
	return t + (MULS16((b - t) >> 1, fy) >> 7);
}

static int32_t SEC_RENDER hb_at(const Hole *h, fixed x, fixed y)
{
	return hb_sample(h, x, y);
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
	if (sc < 1 || sc > 2 || s->w < 160 * sc || s->w > (s->fullview ? 480 : 240 * sc) || (s->w & 15) ||
	    s->h < 160 * sc || s->h > 240 * sc) return -1;
	*vh = (int16_t)(RENDER_VIEW_H(s) * sc);
	return (int16_t)((*vh - (160 - RENDER_STATUS) * sc) / 2);
}

/* Give the landing area more room from 65 to 161 yards from the pin.
 * Leave room above the horizon for the tallest 40-row backdrop. */
static int16_t SEC_RENDER scene_frame(const Surface *s, const RenderView *v, int16_t *vh, fixed *back)
{
	vec2 pin = v->hole->pins[v->pin & 3];
	int16_t yards = (int16_t)(vec2_len(vec2_sub(pin, v->ball)) >> 16);
	int16_t raise = yards <= 65 ? 0 : yards >= 161 ? 8 : (yards - 65) / 12;

	*back = CAM_BACK + ((fixed)(raise / 2) << 16);
	return (int16_t)((HORIZON - raise) * s->scale + fit(s, vh));
}

/* The camera's height above the ball's ground, in yards, that puts the
 * ball BALL_GAP rows above the bottom of a view vh rows tall with the
 * horizon on row yh. */
static fixed SEC_RENDER cam_up(int16_t vh, int16_t yh, int16_t sc, fixed back)
{
	return (fixed)(vh - BALL_GAP * sc - yh) * back / (FOCAL * sc);
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

/* Work counts for the host tests; left out on the Palm, where they'd cost time in the inner loops. */
#ifdef __palmos__
#define COUNT(e)
#else
#define COUNT(e) (e)
#endif

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

/* Camera-space contours, 1/32 yard. Depth bounds let the march skip whole
 * shapes, then groups of eight consecutive edges within each shape. */
#define CONTOUR_BAND 8
#define SAND_RADIUS 64     /* two yards, in camera units */
typedef struct { int16_t right, depth; } ContourPoint;
typedef struct { int16_t lo, hi; } ContourBand;
typedef struct { int16_t right, depth; } SandNormal;
typedef struct {
	const uint8_t *samples;
	uint16_t x, y, w, h;
} SandTile;
typedef struct {
	uint16_t first, count, first_band, first_sand;
	ContourBand depth;
	uint8_t surface;
} ContourShape;
typedef struct ContourWork {
	ContourShape shape[VISUAL_SHAPE_MAX + 1];
	ContourPoint *points;
	ContourBand *bands;
	SandNormal *sand;
	SandTile *atlas;
	int16_t *crossings;
	uint8_t *row, *prior, *cut, *prior_y;
	uint8_t *relief, *prior_relief;
	int8_t *sand_light;
	int8_t sand_light_x[16], sand_light_y[16];
	int16_t sand_px, sand_first, sand_end;
	uint8_t count;
} ContourWork;

static int16_t SEC_DANA contour_depth(fixed v)
{
	int32_t q = (v + 1024) >> 11;
	return (int16_t)(q < -32768L ? -32768L : q > 32767L ? 32767L : q);
}

static void SEC_DANA contour_point(const R *r, ContourPoint *p, fixed x, fixed y)
{
	fixed dx = x - r->camx, dy = y - r->camy;
	p->depth = contour_depth(fix_mulq14(dx, r->fx) + fix_mulq14(dy, r->fy));
	p->right = contour_depth(fix_mulq14(dx, r->rx) + fix_mulq14(dy, r->ry));
}

static void SEC_DANA contour_bounds(ContourWork *c, ContourShape *s)
{
	const ContourPoint *p = c->points + s->first;
	ContourBand *b = c->bands + s->first_band;
	uint16_t i = 0;
	int16_t last = p[s->count - 1].depth;
	s->depth.lo = s->depth.hi = last;
	while (i < s->count) {
		uint16_t end = (uint16_t)(i + CONTOUR_BAND);
		if (end > s->count) end = s->count;
		b->lo = b->hi = last;
		for (; i < end; i++) {
			last = p[i].depth;
			if (last < b->lo) b->lo = last;
			if (last > b->hi) b->hi = last;
		}
		if (b->lo < s->depth.lo) s->depth.lo = b->lo;
		if (b->hi > s->depth.hi) s->depth.hi = b->hi;
		b++;
	}
}

static void SEC_DANA sand_normals(ContourWork *c, const ContourShape *s)
{
	const ContourPoint *p = c->points + s->first, *last = p + s->count - 1;
	SandNormal *n = c->sand + s->first_sand;
	uint16_t k;
	for (k = 0; k < s->count; k++, last = p++) {
		int32_t dx = (int32_t)p->right - last->right;
		int32_t dz = (int32_t)p->depth - last->depth;
		int32_t span = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);

		n[k].right = span ? (int16_t)(-dz * 16384L / span) : 0;
		n[k].depth = span ? (int16_t)(dx * 16384L / span) : 0;
	}
}

static inline uint8_t SEC_DANA sand_curve(uint16_t d)
{
	uint16_t eased = (uint16_t)((MULU16(d * d, 3 * SAND_RADIUS - 2 * d) + 512) >> 10);
	return (uint8_t)(eased < 255 ? eased : 255);
}

static void SEC_DANA baked_sand_span(ContourWork *c, const SandTile *tile, const TerraRow *t,
                                     int16_t first, int16_t end)
{
	uint16_t stride = tile->w * 2;
	int16_t start, col;
	fixed x, y;

	if (t->cw == 2) first = (int16_t)((first + 1) & ~1);
	start = (int16_t)(t->cw == 2 ? first >> 1 : first);
	x = t->px + fix_muli(t->dpxc, start) - ((fixed)tile->x << FIX_SHIFT);
	y = t->py + fix_muli(t->dpyc, start) - ((fixed)tile->y << FIX_SHIFT);
	for (col = first; col < end; col += t->cw, x += t->dpxc, y += t->dpyc) {
		uint16_t ix = (uint16_t)(x >> FIX_SHIFT), iy = (uint16_t)(y >> FIX_SHIFT);
		const uint8_t *p;
		int16_t a, b, d, light = 0;
		uint16_t fx, fy;

		/* A rounded contour sample can fall just outside its tile. */
		if (ix >= tile->w - 1 || iy >= tile->h - 1) {
			c->relief[col] = 0;
			c->sand_light[col] = 0;
			continue;
		}
		p = tile->samples + MULU16(iy, stride) + ix * 2;
		if (p[0] == SAND_RADIUS && p[2] == SAND_RADIUS &&
		    p[stride] == SAND_RADIUS && p[stride + 2] == SAND_RADIUS) {
			c->relief[col] = 255;
			c->sand_light[col] = 0;
			continue;
		}
		fx = (uint16_t)((x >> 9) & 127);
		fy = (uint16_t)((y >> 9) & 127);
		a = (int16_t)((int8_t)p[0] * 128 + MULS16((int8_t)p[2] - (int8_t)p[0], fx));
		b = (int16_t)((int8_t)p[stride] * 128 +
		              MULS16((int8_t)p[stride + 2] - (int8_t)p[stride], fx));
		d = (int16_t)((MULS16(a, 128) + MULS16(b - a, fy) + 8192) >> 14);
		if (d <= 0) {
			c->relief[col] = 0;
		} else if (d >= SAND_RADIUS) {
			c->relief[col] = 255;
		} else {
			uint8_t normal = p[1 + (fx >= 64 ? 2 : 0) + (fy >= 64 ? stride : 0)];
			int16_t slope = (int16_t)(d <= SAND_RADIUS / 2 ? d : SAND_RADIUS - d);
			light = (int16_t)(MULS16(slope, c->sand_light_x[normal >> 4] +
			                        c->sand_light_y[normal & 15]) >> 5);
			c->relief[col] = sand_curve((uint16_t)d);
		}
		c->sand_light[col] = (int8_t)light;
	}
}

/* Ranges contain columns whose centres are inside the projected polygon. */
static void SEC_DANA contour_paint(ContourWork *c, const ContourShape *s, const R *r,
                                   const TerraRow *t, int16_t depth)
{
	const ContourPoint *p = c->points + s->first;
	const ContourBand *b = c->bands + s->first_band;
	int16_t *xs = c->crossings;
	uint16_t start, n = 0;
	if (depth < s->depth.lo || depth >= s->depth.hi) return;
	for (start = 0; start < s->count; start += CONTOUR_BAND, b++) {
		uint16_t i = start, end = (uint16_t)(start + CONTOUR_BAND);
		const ContourPoint *last;
		if (depth < b->lo || depth >= b->hi) continue;
		if (end > s->count) end = s->count;
		last = &p[start ? start - 1 : s->count - 1];
		for (; i < end; last = &p[i++]) {
			const ContourPoint *at = &p[i];
			const ContourPoint *lo, *hi;
			int16_t run, dy, right, x;
			int32_t delta, x2;
			uint16_t k;
			if ((last->depth <= depth) == (at->depth <= depth)) continue;
			lo = last->depth < at->depth ? last : at;
			hi = last->depth < at->depth ? at : last;
			dy = (int16_t)(hi->depth - lo->depth);
			run = (int16_t)(hi->right - lo->right);
			delta = MULS16(run, depth - lo->depth);
			delta += delta < 0 ? -(dy >> 1) : dy >> 1;
			right = (int16_t)(lo->right + delta / dy);
			x2 = 2L * MULS16(right, r->f) - depth;
			if (x2 >= 0) x2 += 2L * depth - 1;
			x2 = r->cx + x2 / (2L * depth);
			x = (int16_t)(x2 < 0 ? 0 : x2 > r->w ? r->w : x2);
			k = n++;
			while (k && x < xs[k - 1]) {
				xs[k] = xs[k - 1];
				k--;
			}
			xs[k] = x;
		}
	}
	for (start = 0; start + 1 < n; start += 2) {
		int16_t x;
		for (x = xs[start]; x < xs[start + 1]; x++) c->row[x] = s->surface;
		if (s->surface == SURF_BUNKER && xs[start] < xs[start + 1]) {
			if (c->atlas) {
				baked_sand_span(c, c->atlas + s->first_sand, t, xs[start], xs[start + 1]);
			} else {
				if (xs[start] < c->sand_first) c->sand_first = xs[start];
				if (xs[start + 1] > c->sand_end) c->sand_end = xs[start + 1];
			}
		}
	}
}

static void SEC_DANA contour_row(ContourWork *c, const R *r, const TerraRow *t, fixed z)
{
	int16_t depth = contour_depth(z), x;
	uint8_t i;
	c->sand_first = r->w;
	c->sand_end = 0;
	for (x = 0; x < r->w; x++) c->row[x] = SURF_COUNT;
	for (i = 0; i < c->count; i++) contour_paint(c, &c->shape[i], r, t, depth);
}

/* The distance in a square around the segment is the maximum of the
 * distances outside its box and across its line. Camera units are 1/32 yd. */
static void SEC_DANA sand_edge(ContourWork *c, const R *r, const ContourPoint *a,
                               const ContourPoint *b, const SandNormal *n,
                               int16_t depth, int32_t col_step)
{
	int16_t zlo = a->depth < b->depth ? a->depth : b->depth;
	int16_t zhi = a->depth > b->depth ? a->depth : b->depth;
	int16_t dz = depth < zlo ? zlo - depth : depth > zhi ? depth - zhi : 0;
	int16_t left = a->right < b->right ? a->right : b->right;
	int16_t right = a->right > b->right ? a->right : b->right;
	int16_t first = c->sand_first, end = c->sand_end, x;
	int16_t slope = (int16_t)((n->right + n->depth) >> 9);
	int32_t projected, right_q, base;

	if (dz >= SAND_RADIUS) return;
	projected = r->cx + MULS16(left - SAND_RADIUS, r->f) / depth - 1;
	if (projected > first) {
		if (projected >= end) return;
		first = (int16_t)projected;
	}
	projected = r->cx + MULS16(right + SAND_RADIUS, r->f) / depth + 2;
	if (projected < end) {
		if (projected <= first) return;
		end = (int16_t)projected;
	}
	right_q = (first - r->cx) * col_step + (col_step >> 1);
	base = MULS16(n->depth, depth - a->depth);
	for (x = first; x < end; x++, right_q += col_step) {
		int16_t rx, dx, d;
		int32_t dot;
		if (c->row[x] != SURF_BUNKER || dz >= c->relief[x]) continue;
		rx = (int16_t)((right_q + 128) >> 8);
		dx = rx < left ? left - rx : rx > right ? rx - right : 0;
		d = dx > dz ? dx : dz;
		if (d >= c->relief[x]) continue;
		dot = MULS16(n->right, rx - a->right) + base;
		dx = (int16_t)(((dot < 0 ? -dot : dot) + 8192) >> 14);
		if (dx > d) d = dx;
		if (d < c->relief[x]) {
			c->relief[x] = (uint8_t)d;
			c->sand_light[x] = (int8_t)(dot < 0 ? slope : -slope);
		}
	}
}

static void SEC_DANA sand_row(ContourWork *c, const R *r, int16_t depth)
{
	int16_t x;
	uint8_t i;
	int32_t col_step;

	if (c->sand_first >= c->sand_end) return;
	col_step = ((int32_t)depth << 8) / r->f;
	for (x = c->sand_first; x < c->sand_end; x++) {
		c->relief[x] = SAND_RADIUS;
		c->sand_light[x] = 0;
	}
	for (i = 0; i < c->count; i++) {
		const ContourShape *s = &c->shape[i];
		const ContourPoint *p = c->points + s->first;
		const ContourBand *b = c->bands + s->first_band;
		uint16_t start;
		if (s->surface != SURF_BUNKER || depth < s->depth.lo || depth >= s->depth.hi) continue;
		for (start = 0; start < s->count; start += CONTOUR_BAND, b++) {
			uint16_t k, end;
			const ContourPoint *last;
			if (depth < b->lo - SAND_RADIUS || depth > b->hi + SAND_RADIUS) continue;
			end = (uint16_t)(start + CONTOUR_BAND);
			if (end > s->count) end = s->count;
			last = &p[start ? start - 1 : s->count - 1];
			for (k = start; k < end; last = &p[k++])
				sand_edge(c, r, last, &p[k], &c->sand[s->first_sand + k], depth, col_step);
		}
	}
	for (x = c->sand_first; x < c->sand_end; x++) {
		uint16_t d = c->relief[x];
		if (c->row[x] != SURF_BUNKER) continue;
		if (d == SAND_RADIUS) {
			c->relief[x] = 255;
		} else {
			int16_t slope = (int16_t)(d <= SAND_RADIUS / 2 ? d : SAND_RADIUS - d);
			c->relief[x] = sand_curve(d);
			c->sand_light[x] = (int8_t)(MULS16(slope, c->sand_light[x]) >> 5);
		}
	}
}

/* A point on the depth-clipped edge in 1/256 screen pixels. */
static int32_t SEC_DANA contour_screen_x(const R *r, const ContourPoint *a,
                                          const ContourPoint *b, int16_t depth)
{
	int32_t right = (int32_t)a->right * 64;

	if (depth != a->depth) {
		if (depth == b->depth) right = (int32_t)b->right * 64;
		else {
			int32_t t = ((int32_t)depth - a->depth) * 65536L /
			            ((int32_t)b->depth - a->depth);
			right += (((int32_t)b->right - a->right) * t) >> 10;
		}
	}
	/* The validated hole bounds keep right below 24,000 camera units
	 * before the extra six bits of precision. */
	return (int32_t)r->cx * 256 + right * (4L * r->f) / depth;
}

/* Perspective depth interpolates linearly along the projected edge. */
static int16_t SEC_DANA contour_weight(int16_t near_z, int16_t far_z, int16_t z)
{
	return (int16_t)(MULS16(z - near_z, far_z) * 256 / MULS16(far_z - near_z, z));
}

typedef struct {
	int16_t near_z, far_z;
	int16_t first, end;
	int16_t left, right;
} ContourCut;

static void SEC_DANA contour_cut_edge(ContourWork *c, const R *r,
                                     const ContourPoint *a, const ContourPoint *b,
                                     const ContourCut *bounds, uint8_t surface)
{
	int16_t near_z = bounds->near_z, far_z = bounds->far_z;
	int16_t lo = a->depth < b->depth ? a->depth : b->depth;
	int16_t hi = a->depth > b->depth ? a->depth : b->depth;
	int16_t u, v;
	int32_t xa, xb, width, first, end, x;
	uint8_t *cut = c->cut;

	if (hi < near_z || lo > far_z) return;
	if ((a->right < bounds->left && b->right < bounds->left) ||
	    (a->right > bounds->right && b->right > bounds->right)) return;
	if (lo < near_z) lo = near_z;
	if (hi > far_z) hi = far_z;
	xa = contour_screen_x(r, a, b, lo);
	xb = a->depth == b->depth ? contour_screen_x(r, b, a, hi) : contour_screen_x(r, a, b, hi);
	if (xa > xb) {
		int32_t swap = xa;
		int16_t z = lo;
		xa = xb;
		xb = swap;
		lo = hi;
		hi = z;
	}
	/* Half-open intervals count a shared vertex once. */
	first = (xa + 127) >> 8;
	end = (xb + 127) >> 8;
	if (first < bounds->first) first = bounds->first;
	if (end > bounds->end) end = bounds->end;
	if (first >= end) return;
	u = contour_weight(near_z, far_z, lo);
	v = contour_weight(near_z, far_z, hi);
	width = xb - xa;
	for (x = first; x < end; x++) {
		uint8_t near = c->prior[x], far = c->row[x];
		int32_t off;
		int16_t q;
		if (near == far || near >= SURF_COUNT || far >= SURF_COUNT ||
		    near == SURF_WATER || far == SURF_WATER ||
		    (surface != near && surface != far)) continue;
		off = x * 256 + 128 - xa;
		q = width > 8388607L
		    ? (int16_t)(u + ((off >> 4) * (v - u)) / (width >> 4))
		    : (int16_t)(u + (off * (v - u)) / width);
		if (q < 1) q = 1;
		if (q > 254) q = 254;
		if (!cut[x]) cut[x] = (uint8_t)q;
		else if (q - cut[x] > 1 || cut[x] - q > 1) cut[x] = 255;
	}
}

static void SEC_DANA contour_cuts(ContourWork *c, const R *r, fixed near, fixed far)
{
	ContourCut bounds;
	int16_t x, ray;
	uint16_t materials = 0;
	uint8_t i;

	bounds.near_z = contour_depth(near);
	bounds.far_z = contour_depth(far);
	bounds.first = r->w;
	bounds.end = 0;
	for (x = 0; x < r->w; x++) {
		uint8_t a = c->prior[x], b = c->row[x];
		c->cut[x] = 0;
		if (a == b || a >= SURF_COUNT || b >= SURF_COUNT || a == SURF_WATER || b == SURF_WATER) continue;
		if (!materials) bounds.first = x;
		bounds.end = x + 1;
		materials |= (1u << a) | (1u << b);
	}
	if (bounds.near_z >= bounds.far_z || !materials) return;
	/* Bound the changed-column rays throughout the strip. Four camera
	 * units cover clipping and projection rounding. */
	ray = (int16_t)(2 * (bounds.first - r->cx) + 1);
	bounds.left = (int16_t)(MULS16(ray, ray < 0 ? bounds.far_z : bounds.near_z) / (2 * r->f) - 4);
	ray = (int16_t)(2 * (bounds.end - 1 - r->cx) + 1);
	bounds.right = (int16_t)(MULS16(ray, ray < 0 ? bounds.near_z : bounds.far_z) / (2 * r->f) + 4);
	for (i = 1; i < c->count; i++) {
		const ContourShape *s = &c->shape[i];
		const ContourPoint *p = c->points + s->first;
		const ContourBand *b = c->bands + s->first_band;
		uint16_t start;
		if (!(materials & (1u << s->surface)) || s->depth.lo > bounds.far_z || s->depth.hi < bounds.near_z) continue;
		for (start = 0; start < s->count; start += CONTOUR_BAND, b++) {
			uint16_t k, end;
			const ContourPoint *last;
			if (b->lo > bounds.far_z || b->hi < bounds.near_z) continue;
			end = (uint16_t)(start + CONTOUR_BAND);
			if (end > s->count) end = s->count;
			last = &p[start ? start - 1 : s->count - 1];
			for (k = start; k < end; k++) {
				contour_cut_edge(c, r, last, &p[k], &bounds, s->surface);
				last = &p[k];
			}
		}
	}
	for (x = bounds.first; x < bounds.end; x++)
		if (c->cut[x] == 255) c->cut[x] = 0;
}

static void SEC_DANA contour_step(ContourWork *c, const R *r, TerraRow *t,
                                  fixed near, fixed far)
{
	if (t->cw == 1) {
		uint8_t *row = c->row;
		c->row = c->prior;
		c->prior = row;
		row = c->relief;
		c->relief = c->prior_relief;
		c->prior_relief = row;
	}
	contour_row(c, r, t, far);
	if (c->sand) sand_row(c, r, contour_depth(far));
	t->material = c->row;
	t->sand_relief = c->relief;
	t->prior_relief = c->prior_relief;
	t->sand_light = c->sand_light;
	t->cut = 0;
	if (t->cw == 1) {
		contour_cuts(c, r, near, far);
		t->cut = c->cut;
		t->prior_material = c->prior;
		t->prior_y = c->prior_y;
		t->prior_sand_px = c->sand_px;
		c->sand_px = t->sand_px;
	}
}

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

static void (*terra_fn)(uint8_t op, void *job);

static inline int SEC_DANA edge_blend_enabled(int colour)
{
#ifdef __palmos__
	return colour && terra_fn;
#else
	return colour;
#endif
}

static int SEC_DANA blend_edges(const R *r, uint8_t *marks, int16_t horizon,
                                int16_t *next, RenderClock clk, uint32_t until)
{
	TerraEdges t = {r->bits, marks, r->rb, r->w, r->vh, horizon, 0, 0};
	while (*next < r->vh) {
		t.y = (uint16_t)*next;
		t.end = (uint16_t)(*next + EDGE_ROWS);
		if (t.end > t.vh) t.end = t.vh;
		if (terra_fn) terra_fn(TERRA_EDGES, &t);
		else terra_edges(&t);
		*next = (int16_t)t.end;
		if (late(clk, until)) return 0;
	}
	return 1;
}

void render_terra_use(void (*fn)(uint8_t op, void *job))
{
	terra_fn = fn;
}

static int SEC_RENDER terrain(Job *j, RenderClock clk, uint32_t until)
{
	R *r = &j->r;
	TerraRow *t = &j->t;
	const Hole *h = r->h;
	const SurfaceGrid *g = r->g;
	fixed z = j->z, zl = j->zl;
	uint8_t on = j->on, ks = (uint8_t)(SEG_SHIFT + (r->scale > 1));
	int16_t sand_drop, water_drop, seg;
	uint16_t step = j->step;

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
		t->ak = (int16_t)(ca > 32767 ? 32767 : ca < -32767 ? -32767 : ca);
		t->bk = (int16_t)(cb > 32767 ? 32767 : cb < -32767 ? -32767 : cb);
		/* bunkers sit 0.4 yards down and water 0.25, for their lips */
		ca = fix_div(26214, h->step) >> 8;
		cb = fix_div(16384, h->step) >> 8;
		sand_drop = (int16_t)(ca > 30000 ? 30000 : ca);
		water_drop = (int16_t)(cb > 30000 ? 30000 : cb);
	}

	/* what stays the same for every depth step */
	t->bits = r->bits;
	t->heights = h->heights;
	t->cells = g->cells;
	t->ybuf = r->ybuf;
	t->prev = r->prev;
	t->wet = r->wet;
	t->bayer = bayer;
	t->noise = pal_noise;
	t->pat2 = pal_pat2;
	t->grey = pal_grey;
	t->camx = r->camx;
	t->camy = r->camy;
	t->camb = r->camb;
	t->mx = r->mx;
	t->my = r->my;
	t->rb = r->rb;
	t->w = r->w;
	t->yh = r->yh;
	t->hw = r->hw;
	t->hh = r->hh;
	t->gw = g->w;
	t->gh = g->h;
	t->gs = g->stride;
	t->stripe = j->contours ? 0 : r->lg == 3 ? 12 : 18;
	t->material = t->cut = t->prior_material = 0;
	t->prior_y = 0;
	t->prior_sand_px = 0;
	t->sand_relief = t->prior_relief = 0;
	t->sand_light = 0;
	t->lg = r->lg;
	t->scale = r->scale;
	t->shift = h->hm_shift;

	while (z < Z_FAR) {
		fixed zr, dpx, dpy, px, py, fz;
		int16_t cw, tq, hb;
		uint8_t kss;

		while (on < r->nobj && r->obj[on].depth <= z) snap(r, &r->obj[on++]);

		kz(r, r->stepf, (uint32_t)z, &t->m, &t->sh);
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
		t->px = t->spx = px;
		t->py = t->spy = py;
		t->dpxs = dpx * seg;
		t->dpys = dpy * seg;
		t->dpxc = dpx * cw;
		t->dpyc = dpy * cw;
		t->cw = cw;
		t->kss = kss;
		t->step = step;
		t->hl = fz < HAZE_LIFT ? 0 : fz >= HAZE_LIFT + 256 ? 32 : (int16_t)((fz - HAZE_LIFT) >> 3);
		t->hb = hb = fz < HAZE_MIX ? 0 : fz >= HAZE_MIX + 160 ? 16 : (int16_t)((fz - HAZE_MIX) / 10);
		/* at 2 bits, which pattern rows are haze at this depth, a bit each */
		t->hm = (uint8_t)((hb > haze_row[0]) | (hb > haze_row[1]) << 1 | (hb > haze_row[2]) << 2 | (hb > haze_row[3]) << 3);
		t->wmin = fz < WATER_FAR ? 0 : (int16_t)(WATER_MIN * r->scale);
		/* water is darker near, lighter far where it shows the sky, the
		 * lightest level (a pale sheen at 2 bits) from 132 yards */
		t->wlv = (int16_t)(24 + (fz > 132 ? 132 : (int16_t)fz) * 2 / 3);
		/* half way back to the last step, as a share of this depth, 2.14 */
		tq = 0;
		if (!j->contours && fz >= PROBE_Z && z > zl) {
			tq = (int16_t)(((z + zl) >> 1) / (z >> 14));
			r->st->divides++;
			if (!near_water(r, px, py, dpx, dpy, tq)) tq = 0;
		}
		t->tq = tq;
		t->ns = fz < 10 ? 0 : fz < 24 ? 1 : fz < 48 ? 2 : 8;
		if (j->contours && t->ns < 8) t->ns = r->lg < 2 ? 8 : t->ns + 2;
		{
			int32_t d = MULS16(sand_drop >> 2, t->m) >> t->sh, e = MULS16(water_drop >> 2, t->m) >> t->sh;
			t->sand_px = (int16_t)(d > 1000 ? 1000 : d);
			t->water_px = (int16_t)(e > 1000 ? 1000 : e);
		}
		r->st->steps++;
		r->st->samples += (r->w >> kss) + 1;

		/* every column at this depth */
		t->spans = t->pixels = 0;
		if (j->contours) contour_step(j->contours, r, t, zl, z);
		if (terra_fn) terra_fn(TERRA_ROW, t);
		else terra_row(t);
		COUNT(r->st->spans += t->spans);
		COUNT(r->st->pixels += t->pixels);

		/* Stop once nothing further can show: everything is filled up to where the highest ground could reach. */
		if (t->ymax <= 0) break;
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
	int16_t y, ye, y0 = j->y, ymax = j->ymax;
	TerraSky *ts = &j->ts;

	ts->bits = r->bits;
	ts->bk = bk->p;
	ts->row = r->row;
	/* The mesh draws its ground over the sky afterwards. */
	ts->ybuf = j->mesh && r->lg == 2 && !terra_fn ? 0 : r->ybuf;
	ts->bayer = bayer;
	ts->pat2 = pal_pat2;
	ts->grey = pal_grey;
	ts->len = bk->len;
	ts->pos = j->pos;
	ts->rb = r->rb;
	ts->w = r->w;
	ts->yh = r->yh;
	ts->off = j->off;
	ts->bw = j->bw;
	ts->bh = j->bh2;
	ts->btop = j->btop;
	ts->lg = r->lg;
	/* 8 rows between looks at the clock */
	for (y = y0; y < ymax; y = ye) {
		if (y > y0 && late(clk, until)) {
			j->y = y;
			j->pos = ts->pos;
			j->bw = ts->bw;
			return 0;
		}
		ye = (int16_t)((y + 8) & ~7);
		if (ye > ymax) ye = ymax;
		ts->y0 = y;
		ts->y1 = ye;
		ts->px = 0;
		if (terra_fn) terra_fn(TERRA_SKY, ts);
		else terra_sky(ts);
		COUNT(r->st->sky += ts->px);
	}
	return 1;
}

static void SEC_BALL draw_colour_obj(R *r, const RObj *o, uint32_t us, uint32_t vs)
{
	TerraObject t = {r->bits, o->px, r->pool + o->clip, us, vs, 0,
	                 r->rb, r->vh, o->x0, o->y0, o->c0, o->c1,
	                 o->dh, o->sw, o->sh, r->scale};
	terra_fn(TERRA_OBJECT, &t);
	r->st->sprite_px += t.pixels;
}

/* An object sprite or the pin, clipped by the y-buffer it was given. */
static void SEC_RENDER draw_obj(R *r, const RObj *o)
{
	int16_t x, y;
	uint32_t us = 0, vs = 0;

	if (o->clip == NO_CLIP) return;
	r->st->objects++;
	if (o->px) {
		us = ((uint32_t)o->sw << 16) / (uint16_t)o->dw;
		vs = ((uint32_t)o->sh << 16) / (uint16_t)o->dh;
		r->st->divides += 2;
	}
	if (terra_fn && r->lg == 3) {
		draw_colour_obj(r, o, us, vs);
		return;
	}
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

	/* Each doubled 4-bit pixel fills a byte in each destination row. */
	if (dbl && r->lg == 2) {
		p = r->bits + MULU16(2 * y, r->rb) + x;
		p[0] = p[r->rb] = (uint8_t)((v << 4) | v);
		return;
	}
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

static const uint8_t skin_ramp[SKIN_COLOURS - 1][PAL_SKIN_N] = {
	{ PAL_BARK + 1, PAL_BARK + 2, PAL_SKIN + 1, PAL_SKIN + 2, PAL_SKIN + 3, PAL_SKIN + 4 },
	{ PAL_BARK, PAL_BARK + 1, PAL_SKIN, PAL_BARK + 2, PAL_BARK + 3, PAL_SKIN + 2 }
};

static const uint8_t outfit_ramp[OUTFIT_COLOURS - 1][6] = {
	{ PAL_NAVY, PAL_NAVY + 1, PAL_NAVY + 2, PAL_NAVY + 3, PAL_NAVY + 4, PAL_NAVY + 5 },
	{ PAL_TREE + 5, PAL_TREE + 6, PAL_TREE + 7, PAL_TREE + 8, PAL_TREE + 9, PAL_TREE + 10 },
	{ PAL_RED, PAL_RED + 1, PAL_RED + 2, PAL_RED + 3, PAL_RED + 4, PAL_RED + 5 },
	{ PAL_GOLD, PAL_GOLD + 1, PAL_GOLD + 2, PAL_GOLD + 3, PAL_GOLD + 4, PAL_GOLD + 5 },
	{ PAL_GREY + 4, PAL_GREY + 5, PAL_GREY + 6, PAL_GREY + 7, PAL_CAP + 2, PAL_CAP + 3 },
	{ PAL_GREY, PAL_GREY, PAL_GREY + 1, PAL_GREY + 1, PAL_GREY + 2, PAL_GREY + 3 }
};

static void SEC_DANA outfit_shades(uint8_t *out, uint8_t count, uint8_t colour)
{
	uint8_t i;

	for (i = 0; i < count; i++)
		out[i] = colour && colour < OUTFIT_COLOURS ?
		         outfit_ramp[colour - 1][(i * 5 + (count - 1) / 2) / (count - 1)] : 0;
}

/* The optional trailer holds two clothing shade tags per byte, low first. */
static const uint8_t *SEC_DANA golfer_clothes(uint8_t *colours, const RenderGolfer *g,
                                              const RenderData *d, uint32_t pixels)
{
	if ((!g->shoes && !g->pants && !g->shirt && !g->hat) ||
	    d->len != 12 + pixels + (pixels + 1) / 2) return 0;
	colours[0] = 0;
	outfit_shades(colours + 1, 3, g->shoes);
	outfit_shades(colours + 4, 4, g->pants);
	outfit_shades(colours + 8, 4, g->shirt);
	outfit_shades(colours + 12, 4, g->hat);
	return d->p + 12 + pixels;
}

static int16_t SEC_RENDER golfer_x(const RenderGolfer *g, const RenderData *d, uint16_t w, int16_t bx)
{
	int16_t anchor = (int16_t)be16(d->p + 8);
	return (int16_t)(g->left ? bx + anchor - ((int16_t)w - 1) : bx - anchor);
}

/* Keep the shadow at the address pose's foot anchor during the swing. */
static void SEC_DANA golfer_contact(const R *r, const RenderGolfer *g, int16_t bx, int16_t by)
{
	const RenderData *d = &g->pose[POSE_ADDRESS];
	uint16_t w, h;
	int16_t dx, sx, sy, x, y, row, end;

	if (!sprite(d, &w, &h)) return;
	dx = (int16_t)be16(d->p + 4) - (int16_t)be16(d->p + 8);
	sx = bx + (g->left ? -dx : dx);
	sy = by + (int16_t)be16(d->p + 6) - (int16_t)be16(d->p + 10);
	for (row = -1; row <= r->scale; row++) {
		y = sy + row;
		if (y < 0 || y >= r->vh) continue;
		end = 4 * r->scale - (row != 0);
		for (x = sx - end; x <= sx + end; x++) {
			uint8_t *p, c, level, drop;
			if (x < 0 || x >= r->w) continue;
			if (r->lg == 1) {
				if (!row || !((x + y) & 1)) plot(r, x, y, PAL_BLACK, 0);
				continue;
			}
			p = pixp(r, x, y);
			c = *p;
			if (c < PAL_TERRAIN || c >= PAL_TERRAIN + PAL_TERRAIN_N) continue;
			level = (uint8_t)((c - PAL_TERRAIN) & (PAL_RAMP_N - 1));
			drop = !row && x > sx - end && x < sx + end ? 3 : 1;
			*p = (uint8_t)(c - (level < drop ? level : drop));
		}
	}
}

static const uint8_t golfer_ramps[][2] = {
	{ PAL_BARK, PAL_BARK_N }, { PAL_SKIN, PAL_SKIN_N }, { PAL_SHIRT, PAL_SHIRT_N },
	{ PAL_TROUSERS, PAL_TROUSERS_N }, { PAL_CAP, PAL_CAP_N }, { PAL_SHOES, PAL_SHOES_N },
	{ PAL_GREY, PAL_GREY_N }, { PAL_TREE, PAL_TREE_N }, { PAL_RED, PAL_RED_N },
	{ PAL_GOLD, PAL_GOLD_N }, { PAL_NAVY, PAL_NAVY_N }
};

/* Move a low-contrast edge along its own colour ramp. */
static uint8_t SEC_DANA golfer_edge_colour(uint8_t c, uint8_t ground)
{
	int16_t bg = pal_grey[ground], difference = pal_grey[c] - bg;
	uint8_t i;

	if (difference < -24 || difference > 24) return c;
	for (i = 0; i < sizeof golfer_ramps / sizeof golfer_ramps[0]; i++) {
		uint8_t lo = golfer_ramps[i][0], hi = (uint8_t)(lo + golfer_ramps[i][1] - 1);
		if (c < lo || c > hi) continue;
		if (2 * bg < pal_grey[lo] + pal_grey[hi]) {
			while (c != hi && pal_grey[c] - bg < 38) c++;
		} else {
			while (c != lo && bg - pal_grey[c] < 38) c--;
		}
		break;
	}
	return c;
}

/* Spr8 pose with its ball point on (bx, by). The ball itself stays put. */
static void SEC_DANA golfer(const R *r, const RenderGolfer *g, uint8_t pose, int16_t bx,
                            int16_t by, uint8_t ball, uint8_t dbl, uint8_t course)
{
	const RenderData *gd = &g->pose[pose];
	uint8_t colours[16];
	const uint8_t *shade = r->lg >= 2 && g->skin > SKIN_LIGHT && g->skin < SKIN_COLOURS ?
	                       skin_ramp[g->skin - 1] : 0;
	uint16_t w, h, i, j;
	const uint8_t *px = sprite(gd, &w, &h);

	if (px) {
		int16_t x0 = golfer_x(g, gd, w, bx), y0 = by - (int16_t)be16(gd->p + 10);
		int16_t step = g->left ? -1 : 1;
		/* the columns on screen */
		int16_t e = r->w - x0;
		uint16_t i0 = x0 < 0 ? (uint16_t)-x0 : 0, i1 = e < 0 ? 0 : e < (int16_t)w ? (uint16_t)e : w;
		const uint8_t *tags = golfer_clothes(colours, g, gd, MULU16(w, h));
		int edge = course;
		for (j = 0; j < h; j++, px += w) {
			int16_t y = y0 + (int16_t)j;
			int16_t k = g->left ? (int16_t)(w - 1 - i0) : (int16_t)i0;
			uint32_t tag_row = MULU16(j, w);
			uint8_t *row;
			if (y < 0 || y >= r->vh) continue;
			row = r->bits + MULU16(y, r->rb);
			for (i = i0; i < i1; i++, k += step) {
				int16_t x = x0 + (int16_t)i;
				uint8_t c = px[k];
				if (!c) continue;
				if (g->mesh_style) {
					int trousers = c >= PAL_TROUSERS && c < PAL_TROUSERS + PAL_TROUSERS_N;
					if (r->lg == 3 && trousers) c = (uint8_t)(PAL_NAVY + c - PAL_TROUSERS);
					else if (!course && r->lg == 1 && (!k || !px[k - 1]) &&
					         (trousers || (y > by - 19 && c >= PAL_GREY && c <= PAL_GREY + 2)) &&
					         (getpx(r, x, y) == 3 ||
					          (x - step >= 0 && x - step < r->w && getpx(r, x - step, y) == 3)))
						c = PAL_WHITE;
				}
				if (shade && c >= PAL_SKIN && c < PAL_SKIN + PAL_SKIN_N) c = shade[c - PAL_SKIN];
				if (tags && c) {
					uint32_t at = tag_row + (uint16_t)k;
					uint8_t packed = tags[at >> 1];
					uint8_t tag = (uint8_t)(at & 1 ? packed >> 4 : packed & 15);
					if (colours[tag]) c = colours[tag];
				}
				/* Work just inside the silhouette and skip fine features like the club. */
				if (edge && (px[k] < PAL_CLUB || px[k] >= PAL_CLUB + PAL_CLUB_N) &&
				    (((!k || !px[k - 1]) && k + 2 < w && px[k + 1] && px[k + 2]) ||
				     ((k + 1 == w || !px[k + 1]) && k >= 2 && px[k - 1] && px[k - 2]))) {
					uint8_t bg = getpx(r, (int16_t)(x << dbl), (int16_t)(y << dbl));
					if (r->lg == 3) c = golfer_edge_colour(c, bg);
					else if (r->lg == 1) {
						uint8_t ink = pixval(r, x, y, c);
						if (ink == bg || (ink | bg) < 2) {
							c = bg < 2 ? PAL_GREY + 2 : bg == 2 ? PAL_GREY + 7 : PAL_GREY + 6;
							if (bg == 2 && pixval(r, x, y, c) == 2) c = PAL_WHITE;
						}
					} else if (r->lg == 0 || fix_abs((int16_t)pixval(r, x, y, c) - bg) < 5) {
						c = bg < (r->lg == 0 ? 1 : 8) ? PAL_BLACK : PAL_WHITE;
					}
				}
				if (dbl) plot(r, x, y, c, 1);
				else put(r, row + ((uint16_t)x >> (3 - r->lg)), x, y, c);
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

void render_golfer_preview(const Surface *s, const RenderGolfer *g,
                           int16_t x, int16_t y, int16_t w, int16_t h)
{
	const RenderData *d = &g->pose[POSE_ADDRESS];
	uint16_t sw, sh;
	int16_t ax, ay, bx, by;
	R r;

	w *= s->scale;
	h *= s->scale;
	if (w <= 0 || h <= 0 || !sprite(d, &sw, &sh) || sw > (uint16_t)w || sh > (uint16_t)h) return;
	ax = (int16_t)be16(d->p + 8);
	ay = (int16_t)be16(d->p + 10);
	bx = s->ox + x * s->scale + (w - sw) / 2 + (g->left ? (int16_t)sw - 1 - ax : ax);
	by = s->oy + y * s->scale + (h - sh) / 2 + ay;
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.w = s->w;
	r.vh = s->h;
	r.lg = s->lg;
	r.scale = s->scale;
	golfer(&r, g, POSE_ADDRESS, bx, by, 0, 0, 0);
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
		ax = golfer_x(g, d, w, bx);
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
		if (!save && dbl && r->lg == 2) {
			/* Restore the doubled box without reading its destination. */
			for (y = g->y; y < g->y + g->h; y++) {
				uint8_t *p = r->bits + MULU16(2 * y, r->rb) + g->x, *q = p + r->rb;
				for (x = 0; x < g->w; x++) {
					uint8_t v = *u++;
					*p++ = *q++ = (uint8_t)((v << 4) | v);
				}
			}
			return;
		}
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
	uint32_t row_bytes = BKD_MAX * (half ? 1 : s->scale) + 16;

	if (edge_blend_enabled(s->depth == 8) && w * EDGE_MARK_ROWS > row_bytes) row_bytes = w * EDGE_MARK_ROWS;
	return ((sizeof(Job) + 3) & ~3UL) + w * 2 + w * 2 + WBOX_MAX * 8 + sizeof(RObj) * MAX_OBJ + w * POOL_COLS * 2 +
	       row_bytes;
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

/* Expand from the bottom right so the first source row can double in place. */
static void SEC_RENDER double_up(Surface *s)
{
	uint16_t n = (uint16_t)(((uint16_t)(s->w >> 1) << s->lg) >> 3), j;
	int16_t y;

	for (y = (int16_t)((s->h >> 1) - 1); y >= 0; y--) {
		const uint8_t *p = s->bits + MULU16(y, s->rowBytes) + n;
		uint8_t *q = s->bits + MULU16(2 * y + 1, s->rowBytes) + 2 * n;
		uint8_t *o = q - s->rowBytes;
		for (j = n; j; j--) {
			uint8_t b = *--p, a, c;
			switch (s->lg) {
			case 3:
				a = c = b;
				break;
			case 2:
				a = (uint8_t)((b & 0xF0) | (b >> 4));
				c = (uint8_t)((b & 0x0F) | (b << 4));
				break;
			case 1:
				a = (uint8_t)(((b >> 6) * 0x50) | (((b >> 4) & 3) * 5));
				c = (uint8_t)((((b >> 2) & 3) * 0x50) | ((b & 3) * 5));
				break;
			default:
				a = dbl1[b >> 4];
				c = dbl1[b & 15];
				break;
			}
			*--q = c; *--o = c;
			*--q = a; *--o = a;
		}
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
	golfer(&r, g, pose, g->bx, g->by, ball, d, 1);
	*ox = d ? g->x << 1 : g->x;
	*oy = d ? g->y << 1 : g->y;
	*ow = d ? g->w << 1 : g->w;
	*oh = d ? g->h << 1 : g->h;
	return 0;
}

#include "terrainmesh.inc"

int render_scene_begin(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats)
{
	Job *j = (Job *)work;
	R *r;
	uint8_t *wp;
	Surface *d, hs;
	int16_t x, y;
	fixed groundz;
	uint8_t half = (uint8_t)half_of(s, &hs);

	s->smooth_y1 = 0;
	if (fit(half ? &hs : s, &y) < 0) return -1;
	r = &j->r;
	j->s = *s;
	j->half = half;
	j->d = half ? hs : *s;
	d = &j->d;
	j->v = *v;
	j->a = a;
	j->contours = 0;
	j->mesh = 0;
	if (a->golfer) {
		a->golfer->half = j->half;
		a->golfer->mesh_style = 0;
	}
	r->st = stats ? stats : &j->local;
	{
		RenderStats z = { 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0 } };
		*r->st = z;
	}
	r->bits = d->bits;
	r->rb = d->rowBytes;
	r->w = d->w;
	r->scale = d->scale;
	r->yh = scene_frame(d, v, &r->vh, &j->back);
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
	r->camx = v->ball.x - mulq(j->back, r->fx);
	r->camy = v->ball.y - mulq(j->back, r->fy);
	j->bh = hb_at(r->h, v->ball.x, v->ball.y);
	groundz = r->h->base + fix_mul(j->bh << 8, r->h->step);
	r->camb = fix_div(groundz + cam_up(r->vh, r->yh, r->scale, j->back) - r->h->base, r->h->step) >> 8;
	r->stepf = (uint32_t)r->h->step * (uint32_t)r->f;
	j->stage = JS_GATHER;
	return 0;
}

/* One box for the base surface followed by the course's shapes in order. */
static uint32_t SEC_DANA contour_work(const Surface *s, const RenderView *v, Job *j)
{
	const uint8_t *shapes = v->hole->visual_shapes, *p;
	const uint8_t *atlas = v->hole->sand_atlas;
	uint16_t points = 4, bands = 1, widest = 4;
	uint16_t sand = v->hole->base_surface == SURF_BUNKER ? 4 : 0;
	uint8_t i, count, tiles = atlas ? *atlas++ : 0, relief;
	uint32_t offset, size;

	if (!shapes) return 0;
	count = *shapes++;
	p = shapes;
	for (i = 0; i < count; i++) {
		uint16_t n = (uint16_t)((p[1] << 8) | p[2]);
		if (n > widest) widest = n;
		points += n;
		bands += (n + CONTOUR_BAND - 1) / CONTOUR_BAND;
		if (!tiles && p[0] == SURF_BUNKER) sand += n;
		p += 3 + 4 * n;
	}
	relief = sand || tiles;
	offset = (render_work_size(s) + 7) & ~7UL;
	size = offset + sizeof(ContourWork) + tiles * sizeof(SandTile) + points * sizeof(ContourPoint) +
	       bands * sizeof(ContourBand) + sand * sizeof(SandNormal) +
	       widest * sizeof(int16_t) + (relief ? 8u : 5u) *
	       (s->half && s->scale == 2 ? s->w / 2 : s->w);
	if (j) {
		ContourWork *c = (ContourWork *)((uint8_t *)j + offset);
		const R *r = &j->r;
		fixed width = (fixed)v->hole->width << FIX_SHIFT;
		fixed length = (fixed)v->hole->length << FIX_SHIFT;
		int16_t col;
		c->atlas = tiles ? (SandTile *)(c + 1) : 0;
		c->points = (ContourPoint *)((uint8_t *)(c + 1) + tiles * sizeof(SandTile));
		c->bands = (ContourBand *)(c->points + points);
		c->sand = sand ? (SandNormal *)(c->bands + bands) : 0;
		c->crossings = (int16_t *)((SandNormal *)(c->bands + bands) + sand);
		c->row = (uint8_t *)(c->crossings + widest);
		c->prior = c->row + r->w;
		c->cut = c->prior + r->w;
		c->prior_y = c->cut + r->w;
		c->relief = relief ? c->prior_y + 2 * r->w : 0;
		c->prior_relief = relief ? c->relief + r->w : 0;
		c->sand_light = relief ? (int8_t *)(c->prior_relief + r->w) : 0;
		c->sand_px = 0;
		for (i = 0; i < tiles; i++) {
			SandTile *tile = &c->atlas[i];
			tile->x = be16(atlas);
			tile->y = be16(atlas + 2);
			tile->w = be16(atlas + 4);
			tile->h = be16(atlas + 6);
			tile->samples = atlas + 8;
			atlas = tile->samples + 2 * MULU16(tile->w, tile->h);
		}
		if (tiles) {
			for (i = 0; i < 16; i++) {
				int16_t direction = (int16_t)(i < 8 ? i : i - 16);
				c->sand_light_x[i] = (int8_t)(-MULS16(direction, r->fx + r->rx) / 3584);
				c->sand_light_y[i] = (int8_t)(-MULS16(direction, r->fy + r->ry) / 3584);
			}
		}
		for (col = 0; col < r->w; col++) c->row[col] = SURF_COUNT;
		c->count = (uint8_t)(count + 1);
		c->shape[0].first = c->shape[0].first_band = 0;
		c->shape[0].first_sand = 0;
		c->shape[0].surface = v->hole->base_surface;
		c->shape[0].count = 4;
		contour_point(r, &c->points[0], 0, 0);
		contour_point(r, &c->points[1], width, 0);
		contour_point(r, &c->points[2], width, length);
		contour_point(r, &c->points[3], 0, length);
		contour_bounds(c, &c->shape[0]);
		sand = 0;
		if (c->shape[0].surface == SURF_BUNKER) {
			sand_normals(c, &c->shape[0]);
			sand = 4;
		}
		p = shapes;
		points = 4;
		bands = 1;
		for (i = 1; i < c->count; i++) {
			ContourShape *shape = &c->shape[i];
			uint16_t k;
			shape->surface = p[0];
			shape->count = (uint16_t)((p[1] << 8) | p[2]);
			shape->first = points;
			shape->first_band = bands;
			shape->first_sand = sand;
			p += 3;
			for (k = 0; k < shape->count; k++, p += 4) {
				fixed x = (fixed)((p[0] << 8) | p[1]) << 12;
				fixed y = (fixed)((p[2] << 8) | p[3]) << 12;
				contour_point(r, &c->points[points++], x, y);
			}
			bands += (shape->count + CONTOUR_BAND - 1) / CONTOUR_BAND;
			contour_bounds(c, shape);
			if (shape->surface == SURF_BUNKER) {
				if (c->atlas) {
					sand++;
				} else {
					sand_normals(c, shape);
					sand += shape->count;
				}
			}
		}
		j->contours = c;
	}
	return size;
}

static uint32_t SEC_DANA scene_visual_work(const Surface *s, const RenderView *v, Job *j)
{
	Surface half;
	const Surface *draw = half_of(s, &half) ? &half : s;
	uint32_t size = mesh_work(draw, v, j);
	return size ? size : contour_work(s, v, j);
}

int terrain_mesh_parse(TerrainMesh *mesh, const Hole *hole, const uint8_t *data, uint32_t size)
{
	TerraMeshCheck t = {data, size, hole->width, hole->length, 0, 0, 0, 0};

	if (terra_fn) terra_fn(TERRA_MESH_CHECK, &t);
	else terra_mesh_check(&t);
	mesh->data = t.tiles ? data : 0;
	mesh->tiles = t.tiles;
	mesh->vertices = t.vertices;
	mesh->triangles = t.triangles;
	mesh->mow_phase = (uint8_t)t.mow_phase;
	return t.tiles != 0;
}

uint32_t render_scene_visual_size(const Surface *s, const RenderView *v)
{
	return scene_visual_work(s, v, 0);
}

int render_scene_visual_begin(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats)
{
	Job *j = (Job *)work;
	if (render_scene_begin(s, v, a, work, stats) < 0) return -1;
	scene_visual_work(s, v, j);
	if (terra_fn && j->mesh && s->depth == 8) s->smooth_y1 = j->r.vh;
	return 0;
}

int render_scene_visual(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats)
{
	if (render_scene_visual_begin(s, v, a, work, stats) < 0) return -1;
	render_scene_step(work, 0, 0);
	return 0;
}

static int16_t SEC_RENDER scene_ball_row(Job *j)
{
	R *r = &j->r;
	int16_t m;
	uint8_t sh;

	if (j->mesh) return j->mesh->ball_y;
	kz(r, r->stepf, (uint32_t)j->back, &m, &sh);
	return (int16_t)(r->yh + (MULS16(dz6(r->camb, j->bh), m) >> sh));
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
		if (j->stage == JS_MESH) {
			x = (int16_t)mesh_paint(j, clk, until, pal_pat2, bayer, pal_grey);
			t = lap(r->st, RS_TERRAIN, t);
			if (!x) {
				if (j->stage != JS_MESH) continue;
				return 0;
			}
			j->y = 0;
			j->stage = edge_blend_enabled(r->lg == 3) ? JS_EDGES : JS_OBJECTS;
			continue;
		}
		if (j->stage == JS_EDGES) {
			x = (int16_t)blend_edges(r, r->row, r->yh, &j->y, clk, until);
			t = lap(r->st, RS_TERRAIN, t);
			if (!x) return 0;
			j->stage = JS_OBJECTS;
			continue;
		}
		switch (j->stage) {
		case JS_GATHER:
			gather(r, &j->v, a);
			t = lap(r->st, RS_GATHER, t);
			if (j->mesh) {
				sky_start(j, &a->backdrop, (angle)(j->v.aim + j->v.hole->turn));
				j->ymax = r->yh; /* the terrain fill covers rows below the horizon */
				j->stage = JS_SKY;
				break;
			}
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
			j->stage = j->mesh ? JS_MESH : JS_OBJECTS;
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
			bx = j->mesh ? j->mesh->ball_x : r->cx;
			by = scene_ball_row(j);
			if (a->golfer) {
				RenderGolfer *g = a->golfer;
				golfer_box(r, g, bx, by);
				g->mesh_style = j->mesh != 0;
				if (j->mesh) {
					g->camera_raise = j->mesh->ball_raise;
					g->camera_shift = vec2_sub(j->mesh->address, j->v.ball);
					g->camera_back = (int16_t)(j->back >> FIX_SHIFT);
				}
				if (g->mesh_style) golfer_contact(r, g, bx, by);
				if (g->w && g->under) {
					under_io(r, g, 1, 0);
				} else {
					g->w = 0;
				}
				g->shown = POSE_ADDRESS;
				g->ball = 1;
				golfer(r, g, POSE_ADDRESS, bx, by, 1, 0, 1);
			}
			t = lap(r->st, RS_GOLFER, t);
			/* the status strip, black for now */
			for (y = r->vh; y < j->d.h; y++) {
				uint8_t *p = r->bits + MULU16(y, r->rb);
				for (x = 0; x < (int16_t)(((uint16_t)r->w << r->lg) >> 3); x++) p[x] = 0xFF;
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

enum { MAP_NORMAL, MAP_WIDE, MAP_SIDE_BASE };
static int SEC_RENDER map_rows(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1, const vec2 *target);
static int SEC_RENDER map_draw(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1, const vec2 *target, uint8_t mode);
static void SEC_RENDER gline(const R *r, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c);

int render_map(Surface *s, const RenderView *v, RenderMap *out)
{
	return render_map_box(s, v, 0, out);
}

int render_target_map(Surface *s, const RenderView *v, RenderMap *out, const vec2 *target, const MapBox *box)
{
	return map_rows(s, v, box, out, 0, 0x7FFF, target) < 0 ? -1 : 0;
}

int render_dana_map(Surface *s, const RenderView *v, RenderMap *out, const vec2 *target, const MapBox *box)
{
	if (!s->fullview) return -1;
	return map_draw(s, v, box, out, 0, 0x7FFF, target, MAP_WIDE) < 0 ? -1 : 0;
}

int render_map_box(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out)
{
	return render_map_rows(s, v, box, out, 0, 0x7FFF) < 0 ? -1 : 0;
}

int render_map_rows(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1)
{
	return map_rows(s, v, box, out, y0, y1, 0);
}

static int SEC_RENDER map_rows(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1, const vec2 *target)
{
	Surface h;
	int k;
	s->smooth_y1 = 0;
	if (!half_of(s, &h)) return map_draw(s, v, box, out, y0, y1, target, 0);
	/* at half size: rows of the half view, all doubled at the end */
	k = map_draw(&h, v, box, out, (int16_t)(y0 >> 1), y1 >= 0x7FFE ? 0x7FFF : (int16_t)((y1 + 1) >> 1), target, 0);
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

/* Sample the same surface grid as terra_map, with its rows along the
 * display's horizontal axis. The Dana draws straight into its centre pane. */
static void SEC_RENDER map_wide_terrain(const R *r, const SurfaceGrid *g, int16_t x0, int16_t y0,
                                        int16_t w, int16_t h, fixed along, fixed ypp, const uint8_t *col)
{
	uint16_t start[480];
	int16_t x, y, row_bytes = (int16_t)(((uint16_t)r->w << r->lg) >> 3);
	fixed across = ypp / 2;

	if (x0 < 0) {
		along -= (fixed)x0 * ypp;
		w += x0;
		x0 = 0;
	}
	if (y0 < 0) {
		across -= (fixed)y0 * ypp;
		h += y0;
		y0 = 0;
	}
	if (w > r->w - x0) w = (int16_t)(r->w - x0);
	if (h > r->vh - y0) h = (int16_t)(r->vh - y0);
	for (x = 0; x < w; x++, along += ypp) {
		uint16_t cy = (uint16_t)(along >> (16 + SURF_CELL_SHIFT));
		start[x] = (uint16_t)(g->stride * (cy < g->h ? cy : g->h - 1));
	}
	for (y = 0; y < r->vh; y++) {
		uint8_t *row = r->bits + MULU16(y, r->rb);
		for (x = 0; x < row_bytes; x++) row[x] = 0xFF;
		if (y >= y0 && y < y0 + h) {
			uint16_t cx = (uint16_t)(across >> (16 + SURF_CELL_SHIFT));
			const uint8_t *cell;
			across += ypp;
			if (cx >= g->w) cx = g->w - 1;
			cell = g->cells + (cx >> 1);
			for (x = 0; x < w; x++) {
				int16_t px = x0 + x;
				uint8_t v = cell[start[x]];
				v = (uint8_t)(cx & 1 ? v & 15 : v >> 4);
				put(r, row + ((uint16_t)px >> (3 - r->lg)), px, y, col[v < SURF_COUNT ? v : SURF_OOB]);
			}
		}
	}
}

static void SEC_RENDER map_dot(const R *r, uint8_t wide, int16_t x, int16_t y, int16_t n, uint8_t colour)
{
	if (wide) dot(r, r->w - y - n, x, n, colour);
	else dot(r, x, y, n, colour);
}

static void SEC_RENDER map_line(const R *r, uint8_t wide, int16_t x, int16_t y, int16_t x1, int16_t y1, uint8_t colour)
{
	if (wide) gline(r, r->w - 1 - y, x, r->w - 1 - y1, x1, colour);
	else gline(r, x, y, x1, y1, colour);
}

static void SEC_RENDER map_markers(const R *r, const RenderView *v, RenderMap *out,
                                    const vec2 *target, uint8_t wide, uint8_t boxed, int16_t sc)
{
	const Hole *h = v->hole;
	int16_t x, y;
	uint16_t i;

#define MX(wx) ((int16_t)(out->left + (wx) / out->ypp))
#define MY(wy) ((int16_t)(out->top + (out->length - (wy)) / out->ypp))
	out->bx = MX(v->ball.x);
	out->by = MY(v->ball.y);
	out->px = MX(h->pins[v->pin & 3].x);
	out->py = MY(h->pins[v->pin & 3].y);
	out->yards = (uint16_t)((vec2_len(vec2_sub(h->pins[v->pin & 3], v->ball)) + FIX_HALF) >> 16);
	if (target) {
		int16_t x1 = MX(target->x), y1 = MY(target->y);
		if (fix_abs(x1 - out->bx) < fix_abs(y1 - out->by)) {
			map_line(r, wide, out->bx - 1, out->by, x1 - 1, y1, PAL_BLACK);
			map_line(r, wide, out->bx + 1, out->by, x1 + 1, y1, PAL_BLACK);
		} else {
			map_line(r, wide, out->bx, out->by - 1, x1, y1 - 1, PAL_BLACK);
			map_line(r, wide, out->bx, out->by + 1, x1, y1 + 1, PAL_BLACK);
		}
		map_line(r, wide, out->bx, out->by, x1, y1, PAL_WHITE);
	} else if (!boxed) {
		int16_t dx = out->px - out->bx;
		int16_t dy = out->py - out->by;
		int16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
		int16_t n = ax > ay ? ax : ay, k;
		for (k = 2 * sc; k < n; k++) {
			x = (int16_t)(out->bx + (int16_t)((int32_t)dx * k / n));
			y = (int16_t)(out->by + (int16_t)((int32_t)dy * k / n));
			map_dot(r, wide, x, y, 1, ((k / (2 * sc)) & 1) ? PAL_WHITE : PAL_BLACK);
		}
	}
	/* the pin: a pole with a flag to its right */
	x = wide ? r->w - 1 - out->py : out->px;
	y = wide ? out->px : out->py;
	for (i = 0; i <= (uint16_t)(5 * sc); i++) dot(r, x, y - (int16_t)i, 1, PAL_BLACK);
	for (i = 1; i <= (uint16_t)(3 * sc); i++) {
		int16_t j;
		for (j = 0; j < 2 * sc; j++) dot(r, x + (int16_t)i, y - 5 * sc + j, 1, PAL_FLAG + 1);
	}
	/* the ball: white with a black ring */
	if (!boxed || target) {
		map_dot(r, wide, out->bx - sc, out->by - sc, 3 * sc, PAL_BLACK);
		map_dot(r, wide, out->bx, out->by, sc, PAL_WHITE);
	}
#undef MX
#undef MY
}

int render_side_map_base(Surface *s, const RenderView *v, RenderMap *out)
{
	return map_draw(s, v, 0, out, 0, 0x7FFF, 0, MAP_SIDE_BASE) < 0 ? -1 : 0;
}

void render_side_map_markers(Surface *s, const RenderView *v, RenderMap *out, const vec2 *target)
{
	R r;
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.w = s->w;
	r.vh = s->h;
	r.lg = s->lg;
	map_markers(&r, v, out, target, 0, 0, s->scale);
}

static int SEC_RENDER map_draw(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1, const vec2 *target, uint8_t mode)
{
	R r;
	const Hole *h = v->hole;
	const SurfaceGrid *g = v->grid;
	fixed ypp, a, b, x0;
	int16_t x, y, left, top, mw, mh, sc, ew, eh;
	uint16_t i;
	HoleObject o;
	uint8_t col[SURF_COUNT], xcell[480];
	uint8_t wide = mode == MAP_WIDE, banked = target || mode == MAP_SIDE_BASE;

	if (s->fullview) {
		if (s->scale != 1 || s->w < 16 || s->w > (int16_t)sizeof xcell || (s->w & 7) || s->h < 16) return -1;
		r.vh = s->h;
	} else if (fit(s, &r.vh) < 0) return -1;
	r.bits = s->bits;
	r.rb = s->rowBytes;
	r.w = s->w;
	sc = s->scale;
	r.lg = (uint8_t)(s->depth == 8 ? 3 : s->depth == 4 ? 2 : s->depth == 2 ? 1 : 0);
	ew = wide ? r.vh : r.w;
	eh = wide ? r.w : r.vh;

	/* yards per pixel, so the whole hole fits with a 2 pixel margin */
	a = ((fixed)h->width << 16) / (ew - 4 * sc);
	b = ((fixed)h->length << 16) / (eh - 4 * sc);
	ypp = a > b ? a : b;
	if (ypp < 1) ypp = 1;
	mw = (int16_t)(((fixed)h->width << 16) / ypp);
	mh = (int16_t)(((fixed)h->length << 16) / ypp);
	left = (ew - mw) / 2;
	top = (eh - mh) / 2;
	if (box) {
		/* the box in the middle, if that's bigger than the whole hole */
		fixed bw = box->x1 - box->x0, bh = box->y1 - box->y0, ybox;
		a = bw / (ew - 4 * sc);
		b = bh / (eh - 4 * sc);
		ybox = a > b ? a : b;
		if (bw > 0 && bh > 0 && ybox < ypp) {
			ypp = ybox < 1 ? 1 : ybox;
			mw = (int16_t)(((fixed)h->width << 16) / ypp);
			mh = (int16_t)(((fixed)h->length << 16) / ypp);
			left = (int16_t)((ew - bw / ypp) / 2 - box->x0 / ypp);
			top = (int16_t)((eh - bh / ypp) / 2 - (((fixed)h->length << 16) - box->y1) / ypp);
		}
	}

	/* surfaces at the middle of their ramps, out of bounds dark; off the hole is black (0xFF at every depth) */
	for (i = 0; i < SURF_COUNT; i++)
		col[i] = (uint8_t)(PAL_TERRAIN + i * PAL_RAMP_N + PAL_RAMP_N / 2);
	col[SURF_OOB] = PAL_TERRAIN + SURF_OOB * PAL_RAMP_N + 1;
	if (banked && r.lg < 2) col[SURF_WATER] = PAL_WATER + 7;
	if (wide) {
		fixed along = ((fixed)h->length << 16) - (fixed)(mh - 1) * ypp - ypp / 2;
		map_wide_terrain(&r, g, r.w - top - mh, left, mh, mw, along, ypp, col);
	} else {
		TerraMap t;

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
		t.bits = r.bits;
		t.cells = g->cells;
		t.xcell = xcell;
		t.col = col;
		t.bayer = bayer;
		t.pat2 = pal_pat2;
		t.grey = pal_grey;
		t.len = (fixed)h->length << 16;
		t.ypp = ypp;
		t.rb = r.rb;
		t.w = r.w;
		t.y0 = y0;
		t.y1 = y1;
		t.top = top;
		t.mh = mh;
		t.gs = g->stride;
		t.gh = g->h;
		t.lg = r.lg;
		if (y0 < y1) {
			if (terra_fn) terra_fn(TERRA_MAP, &t);
			else terra_map(&t);
		}
		if (y1 < r.vh) return 0;
	}

#define MX(wx) ((int16_t)(left + (wx) / ypp))
#define MY(wy) ((int16_t)(top + (((fixed)h->length << 16) - (wy)) / ypp))
	if (banked && h->shapes) {
		const uint8_t *p = h->shapes;
		uint8_t bank = r.lg < 2 ? PAL_BLACK : PAL_WATER + 1;
		/* Trace the banks at the map's scale, independent of the surface grid. */
		for (i = 0; i < h->shape_count; i++) {
			uint8_t n = p[1];
			if (p[0] == SURF_WATER) {
				const uint8_t *first = p + 2, *end = first + (uint16_t)n * 4, *vtx;
				int16_t x1 = MX((fixed)be16(end - 4) * 16384L);
				int16_t y1 = MY((fixed)be16(end - 2) * 16384L);
				for (vtx = first; vtx < end; vtx += 4) {
					x = MX((fixed)be16(vtx) * 16384L);
					y = MY((fixed)be16(vtx + 2) * 16384L);
					map_line(&r, wide, x1, y1, x, y, bank);
					x1 = x;
					y1 = y;
				}
			}
			p += 2 + (uint16_t)n * 4;
		}
	}
	/* trees, bushes and gorse as dark dots, rocks, walls and houses grey */
	for (i = 0; i < h->object_count; i++) {
		hole_object(h, i, &o);
		if (o.type == OBJ_ROCK || o.type == OBJ_WALL) map_dot(&r, wide, MX(o.pos.x), MY(o.pos.y), sc, PAL_ROCK + 3);
		else if (o.type == OBJ_HOUSE || o.type == OBJ_LANDMARK) map_dot(&r, wide, MX(o.pos.x) - sc, MY(o.pos.y) - sc, 2 * sc + 1, PAL_WALL + 2);
		else if ((o.type >= OBJ_TREE && o.type <= OBJ_BUSH) || o.type == OBJ_GORSE)
			map_dot(&r, wide, MX(o.pos.x) - (sc >> 1), MY(o.pos.y) - (sc >> 1), sc + 1, PAL_TREE + 1);
	}
	/* the tee: a white square with a black edge */
	x = MX(h->tees[h->tee].pos.x);
	y = MY(h->tees[h->tee].pos.y);
	map_dot(&r, wide, x - sc, y - sc, 3 * sc, PAL_BLACK);
	map_dot(&r, wide, x, y, sc, PAL_WHITE);

	out->left = left;
	out->top = top;
	out->ypp = ypp;
	out->length = (fixed)h->length << 16;
	if (mode != MAP_SIDE_BASE) map_markers(&r, v, out, target, wide, box != 0, sc);

#undef MX
#undef MY

	/* the status strip, black */
	for (y = r.vh; y < s->h; y++) {
		uint8_t *p = r.bits + MULU16(y, r.rb);
		for (x = 0; x < (int16_t)(((uint16_t)r.w << r.lg) >> 3); x++) p[x] = 0xFF;
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

/* The green camera backs away and rises as the putt gets longer. */
#define GREEN_PITCH 5825         /* 32 degrees */
#define GREEN_BACK  18022        /* 1.1, 2.14 */
#define GREEN_UP    11469        /* 0.7, 2.14 */
#define GREEN_ZK    3            /* height exaggeration */
#define GREEN_FOCAL 200
#define GREEN_ROW   97           /* the row straight ahead on 160 wide screens */
#define GREEN_NEAR  (4L << 16)
#define GREEN_FAR   (40L << 16)
#define GREEN_ROUGH_LIGHT LIGHT_FLAT

static fixed SEC_RENDER green_cam(const Surface *s, const RenderView *v, RenderCam *c)
{
	const Hole *h = v->hole;
	vec2 pin = h->pins[v->pin & 3];
	fixed len = vec2_len(vec2_sub(pin, v->ball)), back, zb;
	int16_t vh, extra = fit(s, &vh), tall = s->scale == 2 ? extra / 2 : extra;
	/* Keep the far ground in frame as the view gets taller. */
	angle pitch = (angle)(GREEN_PITCH + tall * 32);

	if (len < GREEN_NEAR) len = GREEN_NEAR;
	if (len > GREEN_FAR) len = GREEN_FAR;
	back = mulq(len, GREEN_BACK);
	c->fx = icos(v->aim);
	c->fy = isin(v->aim);
	c->rx = c->fy;
	c->ry = (int16_t)-c->fx;
	c->pc = icos(pitch);
	c->ps = isin(pitch);
	c->camx = v->ball.x - mulq(back, c->fx);
	c->camy = v->ball.y - mulq(back, c->fy);
	c->zref = ground_z(h, pin.x, pin.y);
	zb = ground_z(h, v->ball.x, v->ball.y);
	c->zk = GREEN_ZK;
	c->camz = c->zref + (zb - c->zref) * GREEN_ZK + mulq(len, GREEN_UP);
	c->f = (int16_t)(GREEN_FOCAL * s->scale);
	c->cx = s->w / 2;
	c->yh = (int16_t)(GREEN_ROW * s->scale + extra);
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
	uint16_t depth, hb;        /* depth in sixteenths of a yard */
	vec2 ground;
} GPt;

static __inline__ void SEC_RENDER guide_2bit(uint8_t *p, int16_t x, int16_t along)
{
	uint8_t dark = (uint8_t)(0x80 >> ((x & 3) << 1));

	/* Alternate pairs of black and dark-grey pixels over the turf dither. */
	if (along & 2) *p = (uint8_t)((*p & ~(dark >> 1)) | dark);
	else *p |= (uint8_t)(dark | (dark >> 1));
}

/* A one pixel line, clipped to the view. */
static void SEC_RENDER gline(const R *r, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c)
{
	int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
	int16_t sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1, e = dx - dy, e2, n;

	uint8_t fast = r->lg == 1 && c == PAL_BLACK;
	uint8_t grey_guide = r->lg == 1 && c == PAL_GREY + 6;

	if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || (x0 >= r->w && x1 >= r->w) || (y0 >= r->vh && y1 >= r->vh)) return;
	for (n = dx + dy + 1; n > 0; n--) {
		if (x0 >= 0 && x0 < r->w && y0 >= 0 && y0 < r->vh) {
			/* black on 2 bits is both bits set */
			if (fast) *(r->bits + MULU16(y0, r->rb) + (x0 >> 2)) |= (uint8_t)(0xC0 >> ((x0 & 3) << 1));
			else if (grey_guide) guide_2bit(r->bits + MULU16(y0, r->rb) + (x0 >> 2), x0, dx >= dy ? x0 : y0);
			else put(r, pixp(r, x0, y0), x0, y0, c);
		}
		if (x0 == x1 && y0 == y1) break;
		e2 = (int16_t)(2 * e);
		if (e2 > -dy) { e -= dy; x0 += sx; }
		if (e2 < dx) { e += dx; y0 += sy; }
	}
}

typedef struct PuttingCurves PuttingCurves;

/* The green view being drawn (render_green_begin and _step), at the start
 * of the work memory. */
typedef struct {
	uint16_t offset;           /* bytes from the first shape */
	uint8_t x0, y0, x1, y1;    /* quarter yards from the detail box's corner */
} GreenShape;

#define GREEN_DETAIL_Q 104     /* 26 yd either side of the cup */
#define GREEN_TILES    27      /* two-yard cells intersecting the detail box */
#define GREEN_STRIPES  8
#define STRIPE_EDGES   8

typedef struct {
	uint16_t key;              /* local shape index and world row */
	uint8_t count;             /* more than STRIPE_EDGES means scan the shape */
	uint8_t edge[STRIPE_EDGES];
} GreenStripe;

typedef struct {
	R r;
	RenderCam c;
	RenderView v;
	Surface s;                  /* the surface asked for */
	uint8_t half, guide, done, shape_count, sp_shift, refine;
	GPt rows[2][GRID_N], *far, *near;
	fixed len, sp, back, up;     /* putt length, cell size, camera setback and height */
	int16_t k, k0, k1, wide;    /* row to draw, the near and far bounds, and where to coarsen */
	int16_t detail_x, detail_y; /* origin of the detail box, quarter yards */
	uint32_t edges[GREEN_TILES]; /* cells touched by a polygon edge, one bit each */
	const uint8_t *mesh_rows, *mesh_runs;
	uint16_t mesh_x, mesh_y, mesh_w, mesh_h;
	uint8_t geometry, visual_mesh;
	GreenStripe stripes[GREEN_STRIPES];
	int32_t hbb;
	uint32_t green_past;        /* green half-edges along the near side of the last row */
	TerraTris tt;               /* triangles waiting to be filled */
	TerraTri tl[TERRA_TRIS_MAX];
	GreenShape shapes[HOLE_MAX_SHAPES];
	PuttingCurves *curves;
} GJob;

typedef struct {
	int16_t x, y;
	uint16_t depth;
} GreenProjected;

typedef struct {
	GreenProjected vertices[GREEN_VERT_MAX];
	uint16_t next[GREEN_TRI_MAX], bins[64];
} GreenWork;

/* An edge on a cell boundary marks both cells. */
static void SEC_RENDER green_edge(GJob *j, int16_t ax, int16_t ay, int16_t bx, int16_t by)
{
	int16_t left = (j->detail_x >> 3), top = (j->detail_y >> 3);
	int16_t x0 = (int16_t)((((ax < bx) ? ax : bx) - 1) >> 3) - left;
	int16_t x1 = (int16_t)(((ax > bx) ? ax : bx) >> 3) - left;
	int16_t y0 = (int16_t)((((ay < by) ? ay : by) - 1) >> 3) - top;
	int16_t y1 = (int16_t)(((ay > by) ? ay : by) >> 3) - top;
	uint32_t bits;

	if (x0 >= GREEN_TILES || x1 < 0 || y0 >= GREEN_TILES || y1 < 0) return;
	if (x0 < 0) x0 = 0;
	if (x1 >= GREEN_TILES) x1 = GREEN_TILES - 1;
	if (y0 < 0) y0 = 0;
	if (y1 >= GREEN_TILES) y1 = GREEN_TILES - 1;
	bits = ((2UL << x1) - 1) & ~((1UL << x0) - 1);
	for (; y0 <= y1; y0++) j->edges[y0] |= bits;
}

/* Close cells show the green edge. Cells farther away cover more ground. */
static fixed SEC_RENDER green_row(const GJob *j, int16_t k)
{
	if (k > j->wide) k = (int16_t)(j->wide + 2 * (k - j->wide));
	return (fixed)k * j->sp;
}

static fixed SEC_RENDER green_cell(const GJob *j, int16_t i, int16_t k)
{
	int16_t slot = (int16_t)(2 * i + 1);
	int16_t dist = slot < 0 ? (int16_t)-slot : slot;

	/* Spread the outer columns to reach the sides of the view. */
	if (dist > 11) dist = (int16_t)(11 + (dist - 11) * 3);
	if (k > j->wide) dist = (int16_t)(dist + dist * (k - j->wide) / 4);
	return (fixed)(slot < 0 ? -dist : dist) * (j->sp / 2);
}

/* Fills the triangles waiting in j->tl. */
static void SEC_RENDER tris_flush(GJob *j)
{
	if (!j->tt.n) return;
	if (terra_fn) terra_fn(TERRA_TRIS, &j->tt);
	else terra_tris(&j->tt);
	j->tt.n = 0;
}

/* Puts a triangle with surface ramp base at slope light lv in the list to fill, dithered. */
static void SEC_RENDER tri(GJob *j, const GPt *a, const GPt *b, const GPt *c, uint8_t base, int16_t lv)
{
	TerraTri *q;
	if (j->tt.n == TERRA_TRIS_MAX) tris_flush(j);
	q = &j->tl[j->tt.n++];
	q->x[0] = a->x;
	q->y[0] = a->y;
	q->x[1] = b->x;
	q->y[1] = b->y;
	q->x[2] = c->x;
	q->y[2] = c->y;
	q->base = base;
	q->lv = lv;
}

/* Build the painter order from depth buckets, projecting each vertex once. */
static int SEC_DANA green_geometry(GJob *j)
{
	GreenWork *work = (GreenWork *)(j + 1);
	const uint8_t *mesh = j->v.hole->green_geometry, *p = mesh + 4, *tris;
	const Hole *h = j->v.hole;
	uint16_t nv = be16(mesh), nt = be16(mesh + 2), i, group, visible = 0;

	for (i = 0; i < nv; i++, p += 6) {
		GreenProjected *v = &work->vertices[i];
		fixed depth, x = (fixed)be16(p) << 12, y = (fixed)be16(p + 2) << 12;
		fixed z = h->base + fix_mulf((fixed)be16(p + 4) * 256, h->step);
		v->depth = 0;
		if (render_point(&j->c, x, y, z, &v->x, &v->y, &depth)) {
			v->depth = (uint16_t)(1 + (depth >> 14));
			if (v->x < -4000) v->x = -4000;
			if (v->x > 4000) v->x = 4000;
			if (v->y < -4000) v->y = -4000;
			if (v->y > 4000) v->y = 4000;
		}
	}
	tris = p;
	for (i = 0; i < 64; i++) work->bins[i] = 0;
	for (i = 0; i < nt; i++, p += 7) {
		const GreenProjected *a = &work->vertices[be16(p)];
		const GreenProjected *b = &work->vertices[be16(p + 2)];
		const GreenProjected *c = &work->vertices[be16(p + 4)];
		if (!a->depth || !b->depth || !c->depth) continue;
		if ((a->x < 0 && b->x < 0 && c->x < 0) ||
		    (a->x >= j->r.w && b->x >= j->r.w && c->x >= j->r.w) ||
		    (a->y < 0 && b->y < 0 && c->y < 0) ||
		    (a->y >= j->r.vh && b->y >= j->r.vh && c->y >= j->r.vh)) continue;
		group = (a->depth + b->depth + c->depth) >> 4;
		if (group > 63) group = 63;
		work->next[i] = work->bins[group];
		work->bins[group] = i + 1;
		visible++;
	}
	if (!visible) return 0;
	for (group = 64; group; group--) {
		uint16_t link = work->bins[group - 1];
		while (link) {
			TerraTri *q;
			uint8_t sf, k;
			i = link - 1;
			p = tris + i * 7;
			sf = p[6] == SURF_OOB ? SURF_ROUGH : p[6];
			if (j->tt.n == TERRA_TRIS_MAX) tris_flush(j);
			q = &j->tl[j->tt.n++];
			for (k = 0; k < 3; k++) {
				const GreenProjected *point = &work->vertices[be16(p + 2 * k)];
				q->x[k] = point->x;
				q->y[k] = point->y;
			}
			q->base = PAL_TERRAIN + sf * PAL_RAMP_N;
			q->lv = sf == SURF_GREEN ? (j->r.lg < 2 ? 96 : 72) :
			        sf == SURF_FRINGE ? 46 : sf == SURF_ROUGH ? (GREEN_ROUGH_LIGHT / 16) * 16 : LIGHT_FLAT;
			link = work->next[i];
		}
	}
	tris_flush(j);
	return 1;
}

/* Keep local polygon bounds for the putting view. The two-yard lie grid
 * still drives play and covers the ground outside this part of the view. */
static void SEC_RENDER green_shapes(GJob *j)
{
	const Hole *h = j->v.hole;
	vec2 pin = h->pins[j->v.pin & 3];
	const uint8_t *p = h->shapes;
	int16_t x = (int16_t)((pin.x >> 14) - GREEN_DETAIL_Q);
	int16_t y = (int16_t)((pin.y >> 14) - GREEN_DETAIL_Q);
	int16_t side = 2 * GREEN_DETAIL_Q;
	uint8_t i;

	j->shape_count = 0;
	j->detail_x = x;
	j->detail_y = y;
	/* Partial cells at the box edge may have their grid sample outside it. */
	for (i = 0; i < GREEN_TILES; i++) j->edges[i] = 1UL | (1UL << (GREEN_TILES - 1));
	j->edges[0] = j->edges[GREEN_TILES - 1] = UINT32_MAX;
	for (i = 0; i < GREEN_STRIPES; i++) j->stripes[i].key = UINT16_MAX;
	for (i = 0; i < h->shape_count; i++) {
		const uint8_t *q, *end = p + 2 + p[1] * 4;
		int16_t x0 = 32767, y0 = 32767, x1 = -32767, y1 = -32767;
		for (q = p + 2; q < end; q += 4) {
			int16_t px = (int16_t)be16(q) - x, py = (int16_t)be16(q + 2) - y;
			if (px < x0) x0 = px;
			if (px > x1) x1 = px;
			if (py < y0) y0 = py;
			if (py > y1) y1 = py;
		}
		if (x0 < side && x1 > 0 && y0 < side && y1 > 0) {
			GreenShape *s = &j->shapes[j->shape_count++];
			int16_t ax = (int16_t)be16(end - 4), ay = (int16_t)be16(end - 2);
			s->offset = (uint16_t)(p - h->shapes);
			s->x0 = (uint8_t)(x0 < 0 ? 0 : x0);
			s->y0 = (uint8_t)(y0 < 0 ? 0 : y0);
			s->x1 = (uint8_t)(x1 > side ? side : x1);
			s->y1 = (uint8_t)(y1 > side ? side : y1);
			for (q = p + 2; q < end; q += 4) {
				int16_t bx = (int16_t)be16(q), by = (int16_t)be16(q + 2);
				green_edge(j, ax, ay, bx, by);
				ax = bx;
				ay = by;
			}
		}
		p = end;
	}
}

/* Mark tiles where a visual contour or the edge of its material box can cross. */
static void SEC_DANA green_visual_edges(GJob *j, const uint8_t *p)
{
	uint8_t count = *p++;
	int16_t x = j->mesh_x, y = j->mesh_y;
	int16_t right = (int16_t)(x + j->mesh_w), bottom = (int16_t)(y + j->mesh_h);

	green_edge(j, x, y, right, y);
	green_edge(j, x, bottom, right, bottom);
	green_edge(j, x, y, x, bottom);
	green_edge(j, right, y, right, bottom);
	while (count--) {
		const uint8_t *v = p + 3, *end = v + be16(p + 1) * 4;
		int16_t ax = (int16_t)(be16(end - 4) >> 2);
		int16_t ay = (int16_t)(be16(end - 2) >> 2);

		for (; v < end; v += 4) {
			int16_t bx = (int16_t)(be16(v) >> 2), by = (int16_t)(be16(v + 2) >> 2);
			green_edge(j, ax, ay, bx, by);
			ax = bx;
			ay = by;
		}
		p = end;
	}
}

/* Cache the edges that can cross a ray anywhere in this two-yard stripe. */
static const GreenStripe *SEC_RENDER green_stripe(GJob *j, const uint8_t *verts,
                                                   uint8_t n, uint8_t shape, uint16_t stripe)
{
	GreenStripe *s = &j->stripes[(stripe + 3 * shape) & (GREEN_STRIPES - 1)];
	uint16_t key = (uint16_t)(((uint16_t)shape << 9) | stripe);  /* up to 320 stripes */
	int16_t y0, y1, ay;
	uint8_t k;

	if (s->key == key) return s;
	s->key = key;
	s->count = 0;
	y0 = (int16_t)(stripe * 8);
	y1 = y0 + 7;
	ay = (int16_t)be16(verts + 4 * n - 2);
	for (k = 0; k < n; k++) {
		int16_t by = (int16_t)be16(verts + 4 * k + 2);
		if ((ay < by && ay <= y1 && by > y0) || (by < ay && by <= y1 && ay > y0)) {
			uint8_t e = s->count++;
			if (e == STRIPE_EDGES) break;
			s->edge[e] = k;
		}
		ay = by;
	}
	return s;
}

static inline uint8_t SEC_RENDER green_mesh_surface(const GJob *j, int16_t qx, int16_t qy)
{
	uint16_t mx = (uint16_t)(qx - j->mesh_x), my = (uint16_t)(qy - j->mesh_y);
	const uint8_t *run;
	uint16_t column, entry;

	if (mx >= j->mesh_w || my >= j->mesh_h) return SURF_COUNT;
	run = j->mesh_runs + be16(j->mesh_rows + 2 * my);
	column = (uint16_t)((mx << 4) | 15);
	while ((entry = be16(run)) <= column) run += 2;
	return (entry & 15) == SURF_OOB ? SURF_ROUGH : (uint8_t)(entry & 15);
}

static uint8_t SEC_RENDER green_surface(GJob *j, fixed x, fixed y)
{
	const Hole *h = j->v.hole;
	int16_t qx = (int16_t)(x >> 14), qy = (int16_t)(y >> 14);
	uint8_t sf;

	if (j->visual_mesh) {
		sf = green_mesh_surface(j, qx, qy);
		if (sf != SURF_COUNT) return sf;
	}
	qx -= j->detail_x;
	qy -= j->detail_y;
	if ((uint16_t)qx < 2 * GREEN_DETAIL_Q && (uint16_t)qy < 2 * GREEN_DETAIL_Q &&
	    j->shape_count && x >= 0 && x < (fixed)h->width * FIX_ONE &&
	    y >= 0 && y < (fixed)h->length * FIX_ONE &&
	    ((j->edges[(qy + (j->detail_y & 7)) >> 3] >> ((qx + (j->detail_x & 7)) >> 3)) & 1)) {
		int16_t sx = (int16_t)(x >> 11), sy = (int16_t)(y >> 11), row = sy >> 3;
		uint8_t i = j->shape_count;
		if (j->mesh_rows && !j->visual_mesh) {
			sf = green_mesh_surface(j, (int16_t)(sx >> 3), row);
			if (sf != SURF_COUNT) return sf;
		}
		while (i) {
			const GreenShape *s = &j->shapes[--i];
			const GreenStripe *stripe;
			const uint8_t *header, *v, *edges;
			uint8_t n, k, count, inside = 0;

			if (qx < s->x0 || qx >= s->x1 || qy < s->y0 || qy >= s->y1) continue;
			header = h->shapes + s->offset;
			sf = header[0];
			n = header[1];
			v = header + 2;
			stripe = green_stripe(j, v, n, i, (uint16_t)(row >> 3));
			edges = stripe->count <= STRIPE_EDGES ? stripe->edge : 0;
			count = edges ? stripe->count : n;
			for (k = 0; k < count; k++) {
				uint8_t e = edges ? edges[k] : k;
				const uint8_t *p = v + 4 * e, *prev = e ? p - 4 : v + 4 * n - 4;
				int16_t ay = (int16_t)be16(prev + 2), by = (int16_t)be16(p + 2);
				if ((ay <= row) != (by <= row)) {
					int16_t ax = (int16_t)be16(prev), bx = (int16_t)be16(p);
					int32_t a = MULS16(sx - ax * 8, by - ay);
					int32_t b = MULS16(sy - ay * 8, bx - ax);
					if (by > ay ? a < b : a > b) inside ^= 1;
				}
			}
			if (inside) return sf == SURF_OOB ? SURF_ROUGH : sf;
		}
		return h->base_surface == SURF_OOB ? SURF_ROUGH : h->base_surface;
	}
	sf = surface_at(j->v.grid, x, y);
	return sf >= SURF_COUNT || sf == SURF_OOB ? SURF_ROUGH : sf;
}

/* An entire mesh patch within the detail box needs no material samples
 * when its cells have no polygon edges. */
static int SEC_RENDER green_uniform(const GJob *j, const GPt *a, const GPt *b, const GPt *c, const GPt *d)
{
	int16_t x0, x1, y0, y1, x, y;
	const GPt *pts[4] = {a, b, c, d};
	uint32_t bits;
	uint8_t i;

	x0 = x1 = (int16_t)(a->ground.x >> 14);
	y0 = y1 = (int16_t)(a->ground.y >> 14);
	for (i = 1; i < 4; i++) {
		x = (int16_t)(pts[i]->ground.x >> 14);
		y = (int16_t)(pts[i]->ground.y >> 14);
		if (x < x0) x0 = x;
		if (x > x1) x1 = x;
		if (y < y0) y0 = y;
		if (y > y1) y1 = y;
	}
	if (x0 < 0 || y0 < 0 || x1 >= (int16_t)(j->v.hole->width * 4) ||
	    y1 >= (int16_t)(j->v.hole->length * 4)) return 0;
	if ((x0 >> 3) == (x1 >> 3) && (y0 >> 3) == (y1 >> 3) &&
	    (x1 < j->detail_x || x0 >= j->detail_x + 2 * GREEN_DETAIL_Q ||
	     y1 < j->detail_y || y0 >= j->detail_y + 2 * GREEN_DETAIL_Q)) {
		return !j->visual_mesh || x1 < j->mesh_x || x0 >= j->mesh_x + j->mesh_w ||
		       y1 < j->mesh_y || y0 >= j->mesh_y + j->mesh_h;
	}
	if (x0 < j->detail_x || y0 < j->detail_y || x1 >= j->detail_x + 2 * GREEN_DETAIL_Q ||
	    y1 >= j->detail_y + 2 * GREEN_DETAIL_Q) return 0;
	x = j->detail_x >> 3;
	x0 = (x0 >> 3) - x;
	x1 = (x1 >> 3) - x;
	y = j->detail_y >> 3;
	y0 = (y0 >> 3) - y;
	y1 = (y1 >> 3) - y;
	bits = ((2UL << x1) - 1) & ~((1UL << x0) - 1);
	for (; y0 <= y1; y0++)
		if (j->edges[y0] & bits) return 0;
	return 1;
}

static int16_t SEC_RENDER green_light(const GJob *j, uint8_t sf, int16_t lv)
{
	if (sf == SURF_GREEN && j->r.lg < 2 && lv < 96) lv = 96;
	else if (sf != SURF_GREEN) lv = (int16_t)((lv + 3 * LIGHT_FLAT) / 4);
	if (sf == SURF_FRINGE) lv -= 16;
	return lv;
}

static void SEC_RENDER green_patch(GJob *j, const GPt *f0, const GPt *f1,
                                   const GPt *n0, const GPt *n1, uint8_t sf, int16_t lv)
{
	uint8_t base = (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N);
	lv = green_light(j, sf, lv);
	tri(j, f0, f1, n1, base, lv);
	tri(j, f0, n1, n0, base, lv);
}

static GPt SEC_RENDER green_midpoint(const GPt *a, const GPt *b)
{
	GPt mid;
	uint32_t sum = (uint32_t)a->depth + b->depth;
	int16_t weight = (int16_t)(MULU16(a->depth, 256) / sum);
	mid.x = (int16_t)(b->x + (MULS16(a->x - b->x, weight) >> 8));
	mid.y = (int16_t)(b->y + (MULS16(a->y - b->y, weight) >> 8));
	mid.depth = (uint16_t)(sum >> 1);
	mid.hb = 0;
	mid.ground.x = (a->ground.x + b->ground.x) / 2;
	mid.ground.y = (a->ground.y + b->ground.y) / 2;
	return mid;
}

/* A single-corner transition follows the line between the adjacent quarter
 * centres instead of giving the minority surface a square end. */
static void SEC_RENDER green_corner(GJob *j, const GPt *tip, const GPt *a, const GPt *b,
                                    uint8_t sf, int16_t lv)
{
	uint8_t base = (uint8_t)(PAL_TERRAIN + sf * PAL_RAMP_N);
	tri(j, tip, a, b, base, green_light(j, sf, lv));
}

/* Locate the boundary along a projected edge after three halvings. */
static GPt SEC_RENDER green_crossing(GJob *j, const GPt *a, const GPt *b, uint8_t sf)
{
	GPt l = *a, r = *b;
	uint8_t i;
	for (i = 0; i < 3; i++) {
		GPt m = green_midpoint(&l, &r);
		if (green_surface(j, m.ground.x, m.ground.y) == sf) l = m;
		else r = m;
	}
	return green_midpoint(&l, &r);
}

/* Cut a single material edge across the patch. A saddle or a three-way
 * join uses the quarter samples instead. */
static uint8_t SEC_RENDER green_cut(GJob *j, const GPt **pts, uint8_t sf, uint8_t ink, int16_t lv)
{
	uint8_t i, mask = 0, n = 0, edges = 0;
	GPt crossing[2];
	const GPt *v[5];

	for (i = 0; i < 4; i++) {
		uint8_t at = green_surface(j, pts[i]->ground.x, pts[i]->ground.y);
		if (at == ink) mask |= (uint8_t)(1 << i);
		else if (at != sf) return 0;
	}
	if (mask == 0 || mask == 15 || mask == 5 || mask == 10) return 0;
	for (i = 0; i < 4; i++) {
		uint8_t next = (uint8_t)((i + 1) & 3);
		uint8_t a = (uint8_t)((mask >> i) & 1), b = (uint8_t)((mask >> next) & 1);
		if (a) v[n++] = pts[i];
		if (a != b) {
			crossing[edges] = green_crossing(j, pts[i], pts[next], a ? ink : sf);
			v[n++] = &crossing[edges++];
		}
	}
	for (i = 1; i + 1 < n; i++) green_corner(j, v[0], v[i], v[i + 1], ink, lv);
	return 1;
}

static uint8_t SEC_RENDER green_quad(GJob *j, const GPt *f0, const GPt *f1,
                                    const GPt *n0, const GPt *n1, uint8_t sf,
                                    int16_t lv, uint8_t refine)
{
	uint8_t fl = green_surface(j, (f0->ground.x * 2 + f1->ground.x + n0->ground.x) / 4,
	                            (f0->ground.y * 2 + f1->ground.y + n0->ground.y) / 4);
	uint8_t fr = green_surface(j, (f1->ground.x * 2 + f0->ground.x + n1->ground.x) / 4,
	                            (f1->ground.y * 2 + f0->ground.y + n1->ground.y) / 4);
	uint8_t nl = green_surface(j, (n0->ground.x * 2 + n1->ground.x + f0->ground.x) / 4,
	                            (n0->ground.y * 2 + n1->ground.y + f0->ground.y) / 4);
	uint8_t nr = green_surface(j, (n1->ground.x * 2 + n0->ground.x + f1->ground.x) / 4,
	                            (n1->ground.y * 2 + n0->ground.y + f1->ground.y) / 4);
	uint8_t green = (fl == SURF_GREEN) | ((fr == SURF_GREEN) << 1) |
	                ((nl == SURF_GREEN) << 2) | ((nr == SURF_GREEN) << 3);

	/* Fill between split edges which round to screen pixels. */
	green_patch(j, f0, f1, n0, n1, sf, lv);
	if (fl != sf || fr != sf || nl != sf || nr != sf) {
		GPt fm = green_midpoint(f0, f1), nm = green_midpoint(n0, n1);
		GPt l = green_midpoint(f0, n0), r = green_midpoint(f1, n1);
		if (!refine) {
			uint8_t ink = fl != sf ? fl : fr != sf ? fr : nl != sf ? nl : nr;
			const GPt *pts[] = { f0, f1, n1, n0 };
			if ((fl == sf || fl == ink) && (fr == sf || fr == ink) &&
			    (nl == sf || nl == ink) && (nr == sf || nr == ink) &&
			    green_cut(j, pts, sf, ink, lv)) return green;
		}
		if (refine || (fl != sf) + (fr != sf) + (nl != sf) + (nr != sf) != 1) {
			GPt m = green_midpoint(&l, &r);
			if (refine) {
				green_quad(j, f0, &fm, &l, &m, fl, lv, (uint8_t)(refine - 1));
				green_quad(j, &fm, f1, &m, &r, fr, lv, (uint8_t)(refine - 1));
				green_quad(j, &l, &m, n0, &nm, nl, lv, (uint8_t)(refine - 1));
				green_quad(j, &m, &r, &nm, n1, nr, lv, (uint8_t)(refine - 1));
			} else {
				if (fl != sf) green_patch(j, f0, &fm, &l, &m, fl, lv);
				if (fr != sf) green_patch(j, &fm, f1, &m, &r, fr, lv);
				if (nl != sf) green_patch(j, &l, &m, n0, &nm, nl, lv);
				if (nr != sf) green_patch(j, &m, &r, &nm, n1, nr, lv);
			}
		} else if (fl != sf) green_corner(j, f0, &fm, &l, fl, lv);
		else if (fr != sf) green_corner(j, f1, &r, &fm, fr, lv);
		else if (nl != sf) green_corner(j, n0, &l, &nm, nl, lv);
		else green_corner(j, n1, &nm, &r, nr, lv);
	}
	return green;
}

static int SEC_RENDER green_begin(Surface *s, const RenderView *v, void *work, uint8_t curves);

#include "puttingcurves.inc"

/* A grid edge can end halfway through a cell split at the green's edge. */
static void SEC_RENDER green_guide(const GJob *j, const GPt *a, const GPt *b, uint8_t halves)
{
	if (j->curves) {
		curve_guide(j, a, b);
		return;
	}
	if (halves == 3) gline(&j->r, a->x, a->y, b->x, b->y, j->guide);
	else {
		GPt m = green_midpoint(a, b);
		if (halves == 1) b = &m;
		else a = &m;
		gline(&j->r, a->x, a->y, b->x, b->y, j->guide);
	}
}

int render_green(Surface *s, const RenderView *v)
{
	GJob j;
	if (render_green_begin(s, v, &j) < 0) return -1;
	render_green_step(&j, 0, 0);
	return 0;
}

uint32_t SEC_DANA render_green_compiled_size(void)
{
	return sizeof(GJob) + sizeof(GreenWork);
}

int SEC_DANA render_green_compiled(Surface *s, const RenderView *v, void *work)
{
	GJob *j = work;
	if (render_green_begin(s, v, work) < 0) return -1;
	if (v->hole->green_geometry && green_geometry(j)) {
		j->geometry = 1;
		/* The compiled terrain supplies depth around the green. Only a few
		 * outside strips are needed to carry the coarse backdrop on. */
		j->k = j->k1 = (int16_t)(j->wide + 8);
	}
	render_green_step(work, 0, 0);
	return 0;
}

static int SEC_RENDER green_begin(Surface *s, const RenderView *v, void *work, uint8_t curves)
{
	GJob *j = (GJob *)work;
	Surface hs, *d;
	R *r;
	const Hole *h = v->hole;
	fixed len, sp;
	int16_t x, y;
	uint8_t half = (uint8_t)half_of(s, &hs);

	s->smooth_y1 = 0;
	d = half ? &hs : s;
	if (fit(d, &y) < 0) return -1;
	r = &j->r;
	j->s = *s;
	j->half = half;
	j->v = *v;
	j->done = 0;
	j->geometry = 0;
	j->curves = 0;
	fit(d, &r->vh);
	if (!curves && terra_fn && h->terrain_mesh.data && !half && d->depth == 8) s->smooth_y1 = r->vh;
	r->bits = d->bits;
	r->rb = d->rowBytes;
	r->w = d->w;
	r->lg = (uint8_t)(d->depth == 8 ? 3 : d->depth == 4 ? 2 : d->depth == 2 ? 1 : 0);
	j->guide = d->depth == 8 ? PAL_GREEN + 7 : d->depth == 2 ? PAL_GREY + 6 : PAL_BLACK;
	/* The small view keeps extra subdivision for putts played from off the green. */
	j->refine = 2;
	if (r->w <= 160) j->refine = surface_at(v->grid, v->ball.x, v->ball.y) == SURF_GREEN ? 0 : 1;
	j->len = len = green_cam(d, v, &j->c);
	j->back = mulq(len, GREEN_BACK);
	j->up = mulq(len, GREEN_UP);
	j->hbb = hb_at(h, v->ball.x, v->ball.y);
	j->mesh_rows = 0;
	j->visual_mesh = h->visual_shapes != 0 && h->green_mesh != 0;
	if (h->green_mesh) {
		const uint8_t *mesh = h->green_mesh;
		j->mesh_x = be16(mesh);
		j->mesh_y = be16(mesh + 2);
		j->mesh_w = be16(mesh + 4);
		j->mesh_h = be16(mesh + 6);
		j->mesh_rows = mesh + 8;
		j->mesh_runs = j->mesh_rows + 2 * (j->mesh_h + 1);
	}
	if (!curves) {
		green_shapes(j);
		if (j->visual_mesh) green_visual_edges(j, h->visual_shapes);
	}

	/* Rough backs the terrain wherever the ground mesh falls outside the view. */
	for (y = curves ? r->vh : 0; y < d->h; y++) {
		uint8_t colour = PAL_ROUGH + GREEN_ROUGH_LIGHT / 16;
		uint8_t *p = r->bits + MULU16(y, r->rb);
		if (r->lg == 1) colour = pal_pat2[((uint16_t)colour << 2) | (y & 3)];
		else if (r->lg == 2) {
			colour = (uint8_t)((255 - pal_grey[colour] + 8) / 17);
			colour |= (uint8_t)(colour << 4);
		} else if (r->lg == 0) colour = 0;
		if (y >= r->vh) colour = 0xff;
		for (x = 0; x < (int16_t)(((uint16_t)r->w << r->lg) >> 3); x++) p[x] = colour;
	}

	/* Cells of half a yard, 1, 2, 4 or 8 keep the view's cost bounded. */
	sp = FIX_HALF;
	j->sp_shift = FIX_SHIFT - 1;
	while (len * 2 > sp * 16) {
		sp <<= 1;
		j->sp_shift++;
	}
	j->sp = sp;
	j->k0 = (int16_t)-((len / 2 + sp - 1) / sp);
	if (d->h > 160 * d->scale) j->k0 -= (int16_t)((len / 4 + sp - 1) / sp);
	j->wide = (int16_t)((len * 3 / 2 + sp - 1) / sp);
	j->k1 = (int16_t)(j->wide + 14);
	j->k = j->k1;
	j->green_past = 0;
	j->tt.bits = r->bits;
	j->tt.tri = j->tl;
	j->tt.bayer = bayer;
	j->tt.pat2 = pal_pat2;
	j->tt.grey = pal_grey;
	j->tt.rb = r->rb;
	j->tt.w = r->w;
	j->tt.vh = r->vh;
	j->tt.n = 0;
	j->tt.lg = r->lg;
	j->tt.shade_grass = 0;
	j->tt.turf = 0;
	j->tt.noise = 0;
	j->tt.mow_phase = 0;
	j->tt.reflection = 0;
	j->tt.green_u = j->tt.green_v = 0;
	j->far = j->rows[0];
	j->near = j->rows[1];
	return 0;
}

int render_green_begin(Surface *s, const RenderView *v, void *work)
{
	return green_begin(s, v, work, 0);
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
	fixed len = j->len, a, up, depth, down;
	vec2 pin = h->pins[v->pin & 3];
	int16_t k, k0 = j->k0, k1 = j->k1, i, n = GRID_N / 2, px, py;
	int32_t hbb = j->hbb;

	if (j->done) return 1;
	if (j->curves && !curve_surfaces(j, clk, until)) return 0;
	/* each row of points from the far end, then the cells between it and the row past it */
	for (k = j->k; k >= k0; k--) {
		/* the points of row k, along the aim from the ball */
		fixed wx0, wy0, along_depth, along_down;
		a = green_row(j, k);
		wx0 = v->ball.x + mulq(a, c.fx);
		wy0 = v->ball.y + mulq(a, c.fy);
		along_depth = mulq(a + j->back, c.pc);
		along_down = mulq(a + j->back, c.ps);
		for (i = -n; i <= n; i++) {
			GPt *g = &near[i + n];
			fixed cx = green_cell(j, i, k);
			fixed wx = wx0 + mulq(cx, c.rx), wy = wy0 + mulq(cx, c.ry);
			int32_t hb = hb_at(h, wx, wy);
			g->hb = (uint16_t)hb;
			up = mulf((hb - hbb) * 256, h->step) * GREEN_ZK - j->up;
			depth = along_depth - mulq(up, c.ps);
			down = -(along_down + mulq(up, c.pc));
			if (depth < FIX_HALF) {
				g->x = NO_PT;
				continue;
			}
			g->depth = depth > 0x0FFFF000L ? 65535 : (uint16_t)(depth >> 12);
			g->ground = vec2_make(wx, wy);
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
			uint32_t green_far = 0, green_near = 0, sides_far = 0, sides_near = 0, across;
			fixed rowx = 0, rowy = 0;
			uint8_t light_shift = (uint8_t)(j->sp_shift - (k >= j->wide ? 6 : 8));
			int16_t light_round = (int16_t)((1U << light_shift) - 1);
			if (!j->curves) {
				fixed ca = (a + green_row(j, (int16_t)(k + 1))) / 2;
				rowx = v->ball.x + mulq(ca, c.fx);
				rowy = v->ball.y + mulq(ca, c.fy);
			}
			for (i = 0; i < 2 * n; i++) {
				GPt *f0 = &far[i], *f1 = &far[i + 1], *n0 = &near[i], *n1 = &near[i + 1];
				uint8_t green;
				if (f0->x == NO_PT || f1->x == NO_PT || n0->x == NO_PT || n1->x == NO_PT) continue;
				/* A cell entirely off one edge has no fill or guide lines to draw. */
				if ((f0->x < 0 && f1->x < 0 && n0->x < 0 && n1->x < 0) ||
				    (f0->x >= r->w && f1->x >= r->w && n0->x >= r->w && n1->x >= r->w) ||
				    (f0->y < 0 && f1->y < 0 && n0->y < 0 && n1->y < 0) ||
				    (f0->y >= r->vh && f1->y >= r->vh && n0->y >= r->vh && n1->y >= r->vh)) continue;
				if (j->curves) green = 15;
				else {
					int16_t col = (int16_t)(i - n);
					fixed cleft = green_cell(j, col, k);
					fixed cright = green_cell(j, (int16_t)(col + 1), (int16_t)(k + 1));
					fixed cc = (cleft + cright) / 2;
					fixed cx = rowx + mulq(cc, c.rx), cy = rowy + mulq(cc, c.ry);
					uint8_t sf = green_surface(j, cx, cy);
					if (j->geometry) {
						/* The compiled patch owns the foreground. Keep guides and
						 * the existing coarse ground beyond its bounds. */
						uint16_t mx = (uint16_t)((cx >> 14) - j->mesh_x);
						uint16_t my = (uint16_t)((cy >> 14) - j->mesh_y);
						if (sf != SURF_ROUGH && (mx >= j->mesh_w || my >= j->mesh_h))
							green_patch(j, f0, f1, n0, n1, sf, LIGHT_FLAT);
						green = sf == SURF_GREEN &&
						        green_surface(j, f0->ground.x, f0->ground.y) == SURF_GREEN &&
						        green_surface(j, f1->ground.x, f1->ground.y) == SURF_GREEN &&
						        green_surface(j, n0->ground.x, n0->ground.y) == SURF_GREEN &&
						        green_surface(j, n1->ground.x, n1->ground.y) == SURF_GREEN ? 15 : 0;
					} else {
						/* lit from the left and behind: the rise to the right and half the rise away, per cell */
						fixed rise = mulf((((int32_t)f1->hb + n1->hb - f0->hb - n0->hb) + (((int32_t)f0->hb + f1->hb - n0->hb - n1->hb) >> 1)) * 128, h->step);
						/* Match division's truncation toward zero on a downhill slope. */
						int32_t lv = LIGHT_FLAT + (((rise < 0 ? rise + light_round : rise) >> light_shift) * 4);
						if (sf == SURF_GREEN || ((sf == SURF_FRINGE || sf == SURF_FAIRWAY) && k < j->wide)) lv += (k & 1) ? 16 : 8;
						if (lv < 0) lv = 0;
						if (lv > PAL_RAMP_N * 16 - 16) lv = PAL_RAMP_N * 16 - 16;
						if (green_uniform(j, f0, f1, n0, n1)) {
							green_patch(j, f0, f1, n0, n1, sf, (int16_t)lv);
							green = sf == SURF_GREEN ? 15 : 0;
						} else green = green_quad(j, f0, f1, n0, n1, sf, (int16_t)lv, k < j->wide ? j->refine : 0);
					}
				}
				if (green) {
					uint8_t right = (uint8_t)(i & 1);
					uint32_t edge = 1UL << (i + right);
					green_far |= (uint32_t)(green & 3) << (i * 2);
					green_near |= (uint32_t)(green >> 2) << (i * 2);
					if ((green >> right) & 1) sides_far |= edge;
					if ((green >> right) & 4) sides_near |= edge;
				}
			}
			tris_flush(j);
			/* Guide lines at every other mesh edge, only on the green. */
			across = (k & 1) ? 0 : green_far | j->green_past;
			for (i = 0; across || sides_far || sides_near; i++) {
				uint8_t half = (uint8_t)((sides_far & 1) | ((sides_near & 1) << 1));
				if ((across & 3) && far[i].x != NO_PT && far[i + 1].x != NO_PT)
					green_guide(j, &far[i], &far[i + 1], (uint8_t)(across & 3));
				if (half && far[i].x != NO_PT && near[i].x != NO_PT)
					green_guide(j, &far[i], &near[i], half);
				across >>= 2;
				sides_far >>= 1;
				sides_near >>= 1;
			}
			j->green_past = green_near;
		}
		t = far;
		far = near;
		near = t;
		if (k > k0 && late(clk, until)) {
			j->k = (int16_t)(k - 1);
			j->far = far;
			j->near = near;
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
			    q < 9L * u * u ? PAL_BLACK : PAL_WHITE);
			}
		if (render_point(&c, pin.x, pin.y, ground_z(h, pin.x, pin.y) + pole, &tx, &top, &depth)) {
			if (r->lg < 2) gline(r, px + 1, py - u, tx + 1, top, PAL_WHITE);
			gline(r, px, py - u, tx, top, r->lg == 3 ? PAL_GREY + 7 : PAL_BLACK);
			for (jj = 0; jj < 4 * u; jj++)
				for (i = 1; i <= 6 * u; i++)
					if (tx + i >= 0 && tx + i < r->w && top + jj >= 0 && top + jj < r->vh) {
						int border = jj < u || jj >= 3 * u || i > 5 * u;
						uint8_t ink = (uint8_t)(r->lg < 2 ? (border ? PAL_BLACK : PAL_WHITE) :
						                        (border ? PAL_FLAG : PAL_FLAG + 2));
						put(r, pixp(r, tx + i, top + jj), tx + i, top + jj, ink);
					}
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
	fixed back;

	c->yh = scene_frame(s, v, &vh, &back);
	c->fx = icos(v->aim);
	c->fy = isin(v->aim);
	c->rx = c->fy;
	c->ry = (int16_t)-c->fx;
	c->camx = v->ball.x - mulq(back, c->fx);
	c->camy = v->ball.y - mulq(back, c->fy);
	c->f = (int16_t)(FOCAL * s->scale);
	c->cx = s->w / 2;
	c->camz = v->hole->base + fix_mul(bh << 8, v->hole->step) + cam_up(vh, c->yh, s->scale, back);
	c->pc = 16384;
	c->ps = 0;
	c->zref = 0;
	c->zk = 1;
	c->back = (int16_t)(back >> 16);
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
