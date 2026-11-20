/* Ball physics (ball.h).
 *
 * Flight: semi-implicit Euler in steps of 1/32 s with gravity, drag on the
 * speed through the air (the ball's velocity less the wind) and Magnus lift
 * from the spin. Backspin lifts at right angles to the air velocity in its
 * vertical plane, side spin pushes sideways: above 0 to the right of the
 * flight (a slice), below 0 to the left (a hook). Spin dies away slowly in
 * the air and mostly on each bounce.
 *
 * Landing: a bounce table by surface gives how much of the fall comes back
 * up, how much of the speed along the ground is kept and how much the
 * backspin takes off. Once a bounce is too low to count the ball rolls,
 * slowed by the surface and pulled down the slope of the heightmap, until
 * it's too slow to move and the slope can't start it again.
 *
 * The putter starts the ball rolling at the speed that takes it the asked
 * distance on a flat green. A rolling ball is checked against the cup each
 * step along the line it moved: it drops if it's slow enough for how near
 * the middle it passes, otherwise it lips out, turned aside and slowed.
 *
 * Every product is 16x16 multiplies only, square roots are whole-number
 * ones on 8.8, there's one 32-bit divide a step and the ground is only
 * looked at once the ball is below the hole's highest point. */
#include "ball.h"

#define STEP_S   BALL_STEP_SHIFT
#define YD(v)    ((fixed)((v) * 65536L))
#define G        703201L          /* 10.73 yards a second squared */
#define KD       288L             /* drag: 0.0044 per yard */
#define KL       4194L            /* lift: 0.064 per thousand rpm */
#define KCL      341L             /* lift at most 0.0052 per yard times the speed (a lift coefficient of 0.3) */
#define WIND_K   (FIX_ONE * 3 / 5) /* the wind the ball feels, of the wind at 10 metres */
#define DECAY    82L              /* spin lost a step in the air, 4 % a second */
#define ROLL_VZ  YD(1.5)          /* a bounce slower than this up turns into a roll */
#define STOP_V   (YD(1) / 8)      /* below this along the ground it may stop */
#define MAX_STEPS (32 * 40)
#define PUSH     364              /* 2 degrees of push or pull at full snap error */
#define CUP_R    (BALL_CUP_R >> 6) /* the cup's radius in 1/1024 yards */
#define VCAP     YD(1.6)          /* fastest a ball drops through the middle of the cup */
#define VSLOW    YD(0.3)          /* slow enough to drop off the very edge */
#define DROP_Z   YD(0.08)         /* how far it shows down in the cup */

/* Clubs: ball speed in 1/8 yards a second, launch in 1/4 degrees, backspin
 * and side spin at full snap error in 1/16 thousands of rpm, and the
 * full-power carry in yards (measured by physics-test). */
typedef struct {
	uint16_t speed;
	uint8_t launch, spin, side;
	uint16_t carry;
	char name[3];
} Club;

static const Club clubs[CLUB_COUNT] = {
	{ 602, 44, 44, 20, 240, "Dr" },
	{ 529, 40, 56, 20, 220, "3W" },
	{ 489, 46, 66, 20, 205, "5W" },
	{ 463, 46, 72, 22, 191, "3i" },
	{ 442, 50, 78, 22, 180, "4i" },
	{ 425, 54, 86, 22, 170, "5i" },
	{ 406, 60, 96, 22, 160, "6i" },
	{ 384, 68, 108, 22, 149, "7i" },
	{ 361, 76, 120, 20, 137, "8i" },
	{ 338, 88, 134, 20, 125, "9i" },
	{ 315, 100, 144, 18, 114, "PW" },
	{ 293, 110, 152, 16, 101, "GW" },
	{ 267, 122, 160, 14, 87, "SW" },
	{ 232, 136, 168, 12, 68, "LW" },
	{ 60, 0, 0, 0, 0, "Pt" },
};

/* Lies: ball speed and backspin out of 256, the carry they give in percent
 * of a clean lie (measured), and the name for the status strip. */
typedef struct {
	uint16_t speed, spin;
	uint8_t carry;
	char name[9];
} Lie;

static const Lie lies[LIE_COUNT] = {
	{ 256, 256, 100, "Tee" },
	{ 256, 256, 100, "Fairway" },
	{ 248, 210, 94, "1st cut" },
	{ 240, 150, 88, "Rough" },
	{ 212, 100, 70, "Deep rgh" },
	{ 232, 160, 82, "Bunker" },
	{ 140, 60, 27, "Plugged" },
	{ 252, 230, 97, "Fringe" },
	{ 256, 256, 100, "Green" },
	{ 250, 220, 95, "Path" },
};

/* Landing by surface (hole.h order): bounce up and speed kept out of 256,
 * yards a second taken off per thousand rpm of backspin, in 1/16, how fast
 * a spun ball may come back, and the rolling slow-down in yards a second
 * squared, in 1/16. */
typedef struct {
	uint8_t up, keep, bite, back;
	uint16_t roll;
} Bounce;

static const Bounce bounces[SURF_COUNT] = {
	{ 40, 100, 8, 0, 144 },     /* rough */
	{ 56, 130, 14, 0, 80 },     /* first cut */
	{ 64, 150, 20, 0, 56 },     /* fairway */
	{ 24, 60, 4, 0, 240 },      /* deep rough */
	{ 64, 150, 32, 24, 15 },    /* green */
	{ 64, 150, 24, 8, 32 },     /* fringe */
	{ 12, 40, 0, 0, 320 },      /* bunker */
	{ 0, 0, 0, 0, 320 },        /* water */
	{ 120, 210, 6, 0, 20 },     /* path */
	{ 40, 100, 8, 0, 144 },     /* out of bounds */
	{ 64, 150, 20, 0, 56 },     /* tee */
};

/* Shapes: launch angle change in 1/4 degrees for a club that launches at
 * 16 degrees (in proportion for others, so wedges change most), ball speed and backspin
 * change out of 256, side spin in 1/64 of the club's side spin at full
 * snap error (below 0 curves left), the start line pushed right in 1/4
 * degrees for a club with the driver's side spin (less for less), and the
 * wind the ball feels out of 256. */
typedef struct {
	int8_t launch, speed, spin, side, push;
	uint8_t wind;
	char name[6];
} Shape;

static const Shape shapes[SHAPE_COUNT] = {
	{ 0, 0, 0, 0, 0, 0, "Shape" },
	{ -2, 2, -8, -12, 6, 0, "Draw" },
	{ 2, -4, 8, 12, -6, 0, "Fade" },
	{ 12, -6, 10, 0, 0, 0, "High" },
	{ -12, 0, -12, 0, 0, 0, "Low" },
	{ -24, -12, -127, 0, 0, 140, "Punch" },
	{ 16, -12, 50, 0, 0, 0, "Open" },
};

static const uint8_t surf_lie[SURF_COUNT] = {
	LIE_ROUGH, LIE_FIRST_CUT, LIE_FAIRWAY, LIE_DEEP_ROUGH, LIE_GREEN, LIE_FRINGE,
	LIE_BUNKER, LIE_ROUGH, LIE_PATH, LIE_ROUGH, LIE_TEE
};

uint8_t ball_lie(uint8_t surface)
{
	return surface < SURF_COUNT ? surf_lie[surface] : LIE_ROUGH;
}

const char *ball_lie_name(uint8_t lie)
{
	return lies[lie < LIE_COUNT ? lie : LIE_ROUGH].name;
}

const char *ball_club_name(uint8_t club)
{
	return clubs[club < CLUB_COUNT ? club : CLUB_PT].name;
}

uint16_t ball_carry(uint8_t club, uint8_t lie)
{
	if (club >= CLUB_COUNT || lie >= LIE_COUNT) return 0;
	return (uint16_t)((uint32_t)clubs[club].carry * lies[lie].carry / 100);
}

/* Carry change in yards from a shape, by club (not the putter), draw to
 * open, measured by physics-test on a flat fairway with no wind. Only for
 * the panel and the aim marker; the flight doesn't use it. */
static const int8_t shape_carry[CLUB_COUNT - 1][SHAPE_COUNT - 1] = {
	{ -2, 1, 5, -15, 0, 0 },
	{ 0, -2, -2, -8, 0, 0 },
	{ 1, -3, -3, -4, 0, 0 },
	{ 2, -4, -3, -4, -76, 0 },
	{ 1, -4, -3, -4, -64, 0 },
	{ 2, -3, -2, -4, -49, 0 },
	{ 2, -3, -2, -4, -35, 0 },
	{ 2, -3, -2, -6, -28, 0 },
	{ 1, -3, -3, -6, -27, 0 },
	{ 1, -3, -2, -5, -25, -6 },
	{ 0, -4, -4, -5, -24, -8 },
	{ 1, -2, -3, -4, -21, -7 },
	{ 1, -2, -4, -4, -18, -7 },
	{ 2, -1, -3, -2, -15, -7 },
};

uint16_t ball_shape_carry(uint8_t club, uint8_t lie, uint8_t shape)
{
	int16_t c;
	if (club >= CLUB_PT || lie >= LIE_COUNT) return 0;
	c = (int16_t)clubs[club].carry;
	if (shape && ball_shape_ok(club, shape)) c += shape_carry[club][shape - 1];
	return (uint16_t)((uint32_t)c * lies[lie].carry / 100);
}

uint8_t ball_pick(uint16_t yards, uint8_t lie)
{
	uint8_t c = lie == LIE_TEE ? CLUB_DR : CLUB_3W;
	for (; c < CLUB_LW; c++)
		if (ball_carry(c, lie) <= yards) break;
	return c;
}

uint8_t ball_shape_ok(uint8_t club, uint8_t shape)
{
	if (shape == SHAPE_NORMAL) return 1;
	if (club >= CLUB_PT || shape >= SHAPE_COUNT) return 0;
	if (shape == SHAPE_PUNCH) return club >= CLUB_3I;
	if (shape == SHAPE_OPEN) return club >= CLUB_9I;
	return 1;
}

uint8_t ball_shape_next(uint8_t club, uint8_t shape)
{
	uint8_t i;
	for (i = 0; i < SHAPE_COUNT; i++) {
		shape = (uint8_t)((shape + 1) % SHAPE_COUNT);
		if (ball_shape_ok(club, shape)) break;
	}
	return shape;
}

const char *ball_shape_name(uint8_t shape)
{
	return shapes[shape < SHAPE_COUNT ? shape : SHAPE_NORMAL].name;
}

void ball_wind(uint32_t seed, uint8_t hole, uint8_t avg, uint8_t *mph, angle *dir)
{
	uint32_t r = seed * 1103515245UL + (uint32_t)hole * 2654435761UL + 12345UL;
	uint8_t a, b, n = (uint8_t)(avg ? 2 * avg + 1 : 21);
	r ^= r >> 15;
	r *= 2246822519UL;
	r ^= r >> 13;
	a = (uint8_t)((r >> 8) % n);
	b = (uint8_t)((r >> 16) % n);
	/* the mean of two draws, so calm and gentle days come up more than 20 mph */
	*mph = (uint8_t)((a + b + 1) / 2);
	*dir = (angle)(r * 40503UL >> 16);
}

vec2 ball_wind_vec(uint8_t mph, angle dir)
{
	fixed s = (fixed)mph * 32040L;    /* 0.4889 yards a second per mph */
	return vec2_make(fix_mulq14(s, icos(dir)), fix_mulq14(s, isin(dir)));
}

/* a * b as fix_mulf, here so the flight loop doesn't call out of its segment. */
static fixed SEC_BALL mul(fixed a, fixed b)
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

static uint16_t SEC_BALL u8_8(fixed a)
{
	uint32_t u = a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
	u >>= 8;
	return (uint16_t)(u > 0xFFFF ? 0xFFFF : u);
}

/* sqrt(x^2 + y^2 + z^2) to 1/256, for parts under 256. */
static fixed SEC_BALL len(fixed x, fixed y, fixed z)
{
	uint16_t a = u8_8(x), b = u8_8(y), c = u8_8(z);
	return (fixed)isqrt32(MULU16(a, a) + MULU16(b, b) + MULU16(c, c)) << 8;
}

/* a / b as 16.16 for |a| < 128.0 and b > 1/256. */
static fixed SEC_BALL ratio(fixed a, fixed b)
{
	return (a * 256) / (b >> 8);
}

static uint8_t SEC_BALL surf(const ShotIn *in, fixed x, fixed y)
{
	uint8_t s = surface_at(in->grid, x, y);
	return s < SURF_COUNT ? s : SURF_OOB;
}

static void SEC_BALL rec(Shot *o, fixed x, fixed y, fixed z)
{
	BallPt *p;
	if (o->n >= BALL_PATH_MAX) o->n = BALL_PATH_MAX - 1;
	p = &o->path[o->n++];
	p->x = (int16_t)(x >> 12);
	p->y = (int16_t)(y >> 12);
	p->z = (int16_t)(z >> 12);
}

/* Unit vector from a toward b, or 0 if they're within a yard. */
static vec2 SEC_BALL unit(vec2 a, vec2 b, fixed *l)
{
	vec2 d = vec2_sub(b, a);
	*l = vec2_len(d);
	if (*l < YD(1)) return vec2_make(0, 0);
	return vec2_make(fix_div(d.x, *l), fix_div(d.y, *l));
}

/* Drops a ball that went in the water at wet, last on dry ground at dry:
 * first back along the ball's line to where it crossed into the water,
 * then on the line from the pin through that point, going back from the
 * pin until the ground is dry and in bounds. */
static vec2 SEC_BALL drop(const ShotIn *in, vec2 wet, vec2 dry)
{
	vec2 d, p, cross = dry;
	fixed l;
	int16_t k;
	uint8_t s;

	d = unit(wet, dry, &l);
	for (k = 1; k <= (int16_t)(l >> 16); k++) {
		p = vec2_make(wet.x + d.x * k, wet.y + d.y * k);
		if (surf(in, p.x, p.y) != SURF_WATER) {
			cross = p;
			break;
		}
	}
	d = unit(in->pin, cross, &l);
	if (l < YD(1)) return in->pos;
	for (k = 1; k <= 120; k++) {
		p = vec2_make(cross.x + d.x * k, cross.y + d.y * k);
		s = surf(in, p.x, p.y);
		if (s != SURF_WATER && s != SURF_OOB) return p;
	}
	return in->pos;
}

/* Whether the ball moving from (x0, y0) to (x1, y1) passes within the cup's
 * radius of the pin, nearest to it on the way. If it does, q is the pin less the nearest point and e
 * how far that is, both in 1/1024 yards, and side is above 0 if the pin is
 * on the left of the line. */
static uint8_t SEC_BALL cup(vec2 pin, fixed x0, fixed y0, fixed x1, fixed y1, int16_t *qx, int16_t *qy, int16_t *e, int32_t *side)
{
	int16_t cx, cy, dx, dy, t;
	int32_t dd, dot, e2;

	if (x1 - pin.x > YD(1) || pin.x - x1 > YD(1) || y1 - pin.y > YD(1) || pin.y - y1 > YD(1)) return 0;
	cx = (int16_t)((pin.x - x0) >> 6);
	cy = (int16_t)((pin.y - y0) >> 6);
	dx = (int16_t)((x1 - x0) >> 6);
	dy = (int16_t)((y1 - y0) >> 6);
	dd = MULS16(dx, dx) + MULS16(dy, dy);
	dot = MULS16(cx, dx) + MULS16(cy, dy);
	/* still coming nearer at the end of the step: wait for the step that passes it */
	if (dot >= dd && dd) return 0;
	t = dd == 0 || dot <= 0 ? 0 : (int16_t)((dot << 8) / dd);
	*qx = (int16_t)(cx - (MULS16(dx, t) >> 8));
	*qy = (int16_t)(cy - (MULS16(dy, t) >> 8));
	e2 = MULS16(*qx, *qx) + MULS16(*qy, *qy);
	if (e2 >= (int32_t)CUP_R * CUP_R) return 0;
	*e = (int16_t)isqrt32((uint32_t)e2);
	*side = MULS16(dx, cy) - MULS16(dy, cx);
	return 1;
}

/* The highest ground on the hole, so the flight needn't look at the
 * ground while it's above it. */
static fixed SEC_BALL top(const Hole *h)
{
	uint16_t i, n = MULU16(h->hm_w, h->hm_h);
	uint8_t m = 0;
	for (i = 0; i < n; i++)
		if (h->heights[i] > m) m = h->heights[i];
	return h->base + mul((fixed)m << 16, h->step) + FIX_ONE;
}

void ball_shot(const ShotIn *in, Shot *o)
{
	const Club *c = &clubs[in->club < CLUB_COUNT ? in->club : CLUB_PT];
	const Lie *l = &lies[in->lie < LIE_COUNT ? in->lie : LIE_ROUGH];
	const Hole *h = in->hole;
	fixed x = in->pos.x, y = in->pos.y, z = hole_height(h, x, y), high = top(h);
	fixed vx, vy, vz, wb, ws, sp, power = in->power, snap = in->snap, gx = 0, gy = 0;
	fixed wx = mul(in->wind.x, WIND_K), wy = mul(in->wind.y, WIND_K);
	vec2 dry = in->pos;
	angle dir;
	int16_t elev;
	uint16_t step;
	fixed gdec = (fixed)bounces[SURF_GREEN].roll << 12;
	uint8_t rolling = 0, landed = 0, s = surf(in, x, y), holed = 0, cool = 0;

	/* the course's green speed: the green's slow-down times the usual over it */
	if (h->green && h->green != GREEN_USUAL) gdec = gdec * GREEN_USUAL / h->green;
	if (power < 0) power = 0;
	if (power > FIX_ONE) power = FIX_ONE;
	if (snap > FIX_ONE) snap = FIX_ONE;
	if (snap < -FIX_ONE) snap = -FIX_ONE;
	if (in->mishit && snap == 0) snap = -FIX_ONE;

	o->start = o->land = o->stop = in->pos;
	o->carry = o->total = 0;
	o->result = SHOT_OK;
	o->n = o->fly_n = 0;
	o->steps = 0;
	o->land_surf = s;
	o->lip = 0;
	o->land_pt = o->lip_pt = 0;

	/* launch; the putter at the speed that rolls power times the range on a flat green */
	if (c->launch == 0) {
		fixed range = in->range > 0 && in->range < ((fixed)BALL_PUTT_MAX << 16) ? in->range : (fixed)BALL_PUTT_MAX << 16;
		/* plus half a step's slowing, which the steps lose against the exact roll */
		sp = fix_sqrt(mul((fixed)bounces[SURF_GREEN].roll << 13, mul(power, range)));
		if (sp) sp += (fixed)bounces[SURF_GREEN].roll << (12 - STEP_S - 1);
	} else {
		sp = mul((fixed)c->speed << 13, power);
	}
	sp = (sp >> 8) * l->speed;
	wb = (fixed)c->spin << 12;
	wb = (wb >> 8) * l->spin;
	wb = mul(wb, FIX_HALF + (power >> 1));
	ws = mul((fixed)c->side << 12, snap);
	elev = (int16_t)(((int32_t)c->launch * 65536L) / (4 * 360));
	if (in->mishit) {
		sp = mul(sp, YD(0.7));
		wb >>= 1;
		ws *= 2;
		elev -= 728;
	}
	dir = (angle)(in->aim - (int16_t)(mul((fixed)PUSH << 16, snap) / 65536L));
	if (in->shape && in->shape < SHAPE_COUNT && c->launch && ball_shape_ok(in->club, in->shape)) {
		const Shape *f = &shapes[in->shape];
		sp = (sp >> 8) * (256 + f->speed);
		wb = (wb >> 8) * (256 + f->spin);
		ws += (((fixed)c->side << 12) >> 6) * f->side;
		elev += (int16_t)(((int32_t)f->launch * c->launch * 1024L) / (4 * 360));
		dir = (angle)(dir - (int16_t)(((int32_t)f->push * c->side * 65536L) / (4 * 360 * 20)));
		if (f->wind) {
			wx = (wx >> 8) * f->wind;
			wy = (wy >> 8) * f->wind;
		}
	}
	if (elev < 0) elev = 0;
	{
		fixed hs = fix_mulq14(sp, icos((angle)elev));
		vx = fix_mulq14(hs, icos(dir));
		vy = fix_mulq14(hs, isin(dir));
		vz = fix_mulq14(sp, isin((angle)elev));
	}
	rec(o, x, y, z);
	if (c->launch == 0) {
		rolling = landed = 1;
		vz = 0;
		o->fly_n = 1;
	}

	for (step = 1; step <= MAX_STEPS; step++) {
		fixed g;
		if (!rolling) {
			fixed ax, ay, az, k, vax = vx - wx, vay = vy - wy, hz;
			k = mul(KD, len(vax, vay, vz));
			ax = -mul(k, vax);
			ay = -mul(k, vay);
			az = -mul(k, vz) - G;
			/* lift from the spin and the ball's own speed; the wind only adds to the drag */
			hz = len(vx, vy, 0);
			if (hz > YD(0.5)) {
				fixed lb = mul(KL, wb), ls = mul(KL, ws), cap = mul(KCL, len(hz, vz, 0));
				fixed inv = (fixed)((1UL << 24) / (uint32_t)(hz >> 8)), q;
				if (lb > cap) lb = cap;
				if (ls > cap) ls = cap;
				if (ls < -cap) ls = -cap;
				q = mul(mul(lb, vz), inv);
				ax -= mul(q, vx);
				ay -= mul(q, vy);
				az += mul(lb, hz);
				ax += mul(ls, vy);
				ay -= mul(ls, vx);
			}
			vx += ax >> STEP_S;
			vy += ay >> STEP_S;
			vz += az >> STEP_S;
			x += vx >> STEP_S;
			y += vy >> STEP_S;
			z += vz >> STEP_S;
			wb -= mul(wb, DECAY);
			ws -= mul(ws, DECAY);
			if (z > high || z > (g = hole_height(h, x, y))) {
				if (step % BALL_FLY_EVERY == 0) rec(o, x, y, z);
				continue;
			}
			/* a landing */
			z = g;
			s = surf(in, x, y);
			rec(o, x, y, z);
			if (!landed) {
				landed = 1;
				o->land_pt = o->n - 1;
				o->land = vec2_make(x, y);
				o->land_surf = s;
				o->carry = vec2_len(vec2_sub(o->land, o->start));
			}
			if (s == SURF_WATER || s == SURF_OOB) break;
			dry = vec2_make(x, y);
			{
				const Bounce *b = &bounces[s];
				fixed vh = len(vx, vy, 0), nh, up = ((-vz) >> 8) * b->up;
				if (s == SURF_BUNKER && ((-vz) >> 8) * 10 > (vh >> 8) * 7) {
					/* into sand steeper than 35 degrees: it plugs */
					o->fly_n = o->n;
					o->stop = vec2_make(x, y);
					o->lie = LIE_PLUGGED;
					goto done;
				}
				nh = (vh >> 8) * b->keep - mul(wb, (fixed)b->bite << 12);
				if (nh < -((fixed)b->back << 12)) nh = -((fixed)b->back << 12);
				if (vh > YD(0.1)) {
					fixed f = ratio(nh, vh);
					vx = mul(vx, f);
					vy = mul(vy, f);
				} else {
					vx = vy = 0;
				}
				wb = mul(wb, YD(0.4));
				ws = mul(ws, YD(0.3));
				if (up < ROLL_VZ) {
					rolling = 1;
					vz = 0;
					o->fly_n = o->n;
				} else {
					vz = up;
				}
			}
			continue;
		}

		/* rolling: slowed by the surface, pulled down the slope (the slope looked at every 1/16 s) */
		{
			fixed dec = (fixed)bounces[s].roll << 12, v = len(vx, vy, 0), pull;
			if (s == SURF_GREEN) dec = gdec;
			if (step & 1 || rolling == 1) {
				gx = (hole_height(h, x + FIX_ONE, y) - hole_height(h, x - FIX_ONE, y)) >> 1;
				gy = (hole_height(h, x, y + FIX_ONE) - hole_height(h, x, y - FIX_ONE)) >> 1;
				rolling = 2;
			}
			pull = mul(G, len(gx * 16, gy * 16, 0) >> 4);
			if (v <= STOP_V && pull <= dec) {
				/* stopped over the cup: it drops */
				int16_t ex = (int16_t)((x - in->pin.x) >> 6), ey = (int16_t)((y - in->pin.y) >> 6);
				if (ex > -CUP_R && ex < CUP_R && ey > -CUP_R && ey < CUP_R &&
				    MULS16(ex, ex) + MULS16(ey, ey) < (int32_t)CUP_R * CUP_R) {
					rec(o, x, y, z);
					holed = 1;
				}
				break;
			}
			if (v > (dec >> STEP_S)) {
				fixed f = ratio(v - (dec >> STEP_S), v);
				vx = mul(vx, f);
				vy = mul(vy, f);
			} else {
				vx = vy = 0;
			}
			vx -= mul(G, gx) >> STEP_S;
			vy -= mul(G, gy) >> STEP_S;
			x += vx >> STEP_S;
			y += vy >> STEP_S;
			if (cool) {
				cool--;
			} else {
				int16_t qx, qy, e;
				int32_t sd;
				if (cup(in->pin, x - (vx >> STEP_S), y - (vy >> STEP_S), x, y, &qx, &qy, &e, &sd)) {
					/* the slowest drop at the edge, VCAP through the middle half */
					fixed vmax = e <= CUP_R / 2 ? VCAP : mul(VCAP, ((fixed)(CUP_R - e) << 17) / CUP_R);
					if (vmax < VSLOW) vmax = VSLOW;
					if (v <= vmax) {
						x = in->pin.x - ((fixed)qx * 64);
						y = in->pin.y - ((fixed)qy * 64);
						rec(o, x, y, hole_height(h, x, y));
						holed = 1;
						break;
					} else {
						/* a lip-out: turned away from the middle, more off the edge, and slowed */
						angle a = (angle)(15 * 182 + (int16_t)((int32_t)e * (40 * 182) / CUP_R));
						fixed k = YD(0.6), nx;
						int16_t ca, sa;
						if (v > 3 * VCAP) {
							a = 5 * 182;
							k = YD(0.9);
						}
						if (sd > 0) a = (angle)-a;
						ca = icos(a);
						sa = isin(a);
						nx = fix_mulq14(vx, ca) - fix_mulq14(vy, sa);
						vy = fix_mulq14(vx, sa) + fix_mulq14(vy, ca);
						vx = mul(nx, k);
						vy = mul(vy, k);
						if (!o->lip) o->lip_pt = o->n < BALL_PATH_MAX ? o->n : BALL_PATH_MAX - 1;
						o->lip = 1;
						cool = 8;
					}
				}
			}
			z = hole_height(h, x, y);
			if ((s = surf(in, x, y)) == SURF_WATER || s == SURF_OOB) break;
			dry = vec2_make(x, y);
			if (step % BALL_ROLL_EVERY == 0) rec(o, x, y, z);
		}
	}
	if (!o->fly_n) o->fly_n = o->n;
	if (holed) {
		/* down into the cup */
		x = in->pin.x;
		y = in->pin.y;
		z = hole_height(h, x, y) - DROP_Z;
		s = surf(in, x, y);
	}
	rec(o, x, y, z);
	if (o->lip_pt >= o->n) o->lip_pt = o->n - 1;
	if (!landed) {
		o->land = vec2_make(x, y);
		o->land_surf = s;
		o->carry = vec2_len(vec2_sub(o->land, o->start));
	}
	o->stop = vec2_make(x, y);
	o->lie = ball_lie(s);
	if (s == SURF_WATER) {
		o->result = SHOT_WATER;
		o->stop = drop(in, vec2_make(x, y), dry);
		o->lie = ball_lie(surf(in, o->stop.x, o->stop.y));
	} else if (s == SURF_OOB) {
		o->result = SHOT_OOB;
		o->stop = in->pos;
		o->lie = in->lie;
	} else if (holed) {
		o->result = SHOT_HOLED;
	}
done:
	o->steps = step > MAX_STEPS ? MAX_STEPS : step;
	o->total = vec2_len(vec2_sub(o->result == SHOT_OK ? o->stop : vec2_make(x, y), o->start));
}
