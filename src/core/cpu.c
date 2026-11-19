#include "cpu.h"
#include "rules.h"

/* Skill levels, CPU_EASY to CPU_PRO: how far off each part of the swing
 * can be (power and snap in 1/256, aim in binary angle units, 182 a
 * degree), the chance in 256 of a duffed shot, and how much of the wind
 * and the green's slope they allow for, in quarters. */
typedef struct {
	uint8_t power, snap;
	uint16_t aim;
	uint8_t putt, putt_aim, duff, read;
} Skill;

static const Skill skills[4] = {
	{ 40, 110, 455, 34, 200, 8, 0 },
	{ 24, 64, 270, 22, 140, 3, 2 },
	{ 14, 34, 150, 13, 90, 0, 3 },
	{ 8, 16, 90, 8, 60, 0, 4 },
};

/* Carry out of 256 of full power at power 0.2, 0.3 ... 1.0 (physics-test's
 * flat fairway, about the same for every club). */
static const uint8_t carry_at[9] = { 9, 22, 40, 64, 92, 128, 169, 212, 255 };

/* G over the green's rolling slow-down: a putt rolls this many yards
 * further for every yard it climbs, and drifts this many yards per yard
 * of putt for a slope of 1 across it. */
#define SLOPE_K 11

static uint32_t SEC_CORE next(uint32_t *s)
{
	*s = *s * 1664525UL + 1013904223UL;
	return *s >> 24;
}

/* A draw from -w to w, most often near 0. */
static int32_t SEC_CORE tri(uint32_t *s, int32_t w)
{
	int32_t a = (int32_t)next(s) + (int32_t)next(s) - 255;
	return a * w / 255;
}

/* The power that carries a share of the club's full carry (1/256). */
static fixed SEC_CORE power_for(int32_t share)
{
	uint8_t k;
	if (share >= 255) return FIX_ONE;
	if (share <= 0) return FIX_ONE / 10;
	for (k = 0; k < 8 && carry_at[k + 1] < share; k++) {}
	if (share <= carry_at[0]) return FIX_ONE / 5 * share / carry_at[0];
	/* between 0.2 + k / 10 and the next tenth */
	return (fixed)(FIX_ONE / 5 + (int32_t)k * (FIX_ONE / 10) +
	               (FIX_ONE / 10) * (share - carry_at[k]) / (carry_at[k + 1] - carry_at[k]));
}

/* Whether a ball landing at p would be lost: water or out of bounds. */
static uint8_t SEC_CORE wet(const CpuView *v, angle a, int32_t yards)
{
	fixed d = (fixed)yards << 16;
	uint8_t s = surface_at(v->grid, v->ball.x + fix_mulq14(d, icos(a)), v->ball.y + fix_mulq14(d, isin(a)));
	return s == SURF_WATER || s == SURF_OOB;
}

static void SEC_CORE putt(const CpuView *v, const Skill *k, uint32_t *rs, CpuShot *o)
{
	vec2 d = vec2_sub(v->pin, v->ball), m;
	fixed len = vec2_len(d), dz, side, want;
	angle a = vec2_angle(d), l = (angle)(a + ANG_90);
	vec2 off = vec2_make(fix_mulq14(FIX_ONE, icos(l)), fix_mulq14(FIX_ONE, isin(l)));

	/* the slope as the green view shows it: up or down to the cup, and across the line half way */
	dz = hole_height(v->hole, v->pin.x, v->pin.y) - hole_height(v->hole, v->ball.x, v->ball.y);
	m = vec2_make(v->ball.x + (d.x >> 1), v->ball.y + (d.y >> 1));
	side = (hole_height(v->hole, m.x + off.x, m.y + off.y) - hole_height(v->hole, m.x - off.x, m.y - off.y)) >> 1;
	/* a foot past the cup, plus the climb */
	want = len + FIX_ONE / 3 + ((dz * SLOPE_K) >> 2) * k->read;
	if (want < FIX_ONE / 2) want = FIX_ONE / 2;
	/* higher on the left breaks it right: aim left, 10430 angle units a radian */
	side = ((((side * SLOPE_K) >> 6) * 10430L) >> 10) * k->read >> 2;
	if (side > 1820) side = 1820;
	if (side < -1820) side = -1820;
	a = (angle)(a + (int16_t)side);
	o->club = CLUB_PT;
	o->shape = SHAPE_NORMAL;
	o->snap = 0;
	o->power = fix_div(want, v->range > 0 ? v->range : (fixed)BALL_PUTT_MAX << 16);
	if (o->power > FIX_ONE) o->power = FIX_ONE;
	o->power += (o->power >> 8) * tri(rs, k->putt);
	o->aim = (angle)(a + tri(rs, k->putt_aim));
}

void cpu_shot(const CpuView *v, uint8_t skill, uint32_t seed, CpuShot *o)
{
	const Skill *k = &skills[(skill >= CPU_EASY && skill <= CPU_PRO ? skill : CPU_FAIR) - CPU_EASY];
	uint32_t rs = seed;
	vec2 d = vec2_sub(v->to, v->ball);
	fixed pin = vec2_len(vec2_sub(v->pin, v->ball));
	angle a = vec2_angle(d), rel;
	int32_t yards, head, cross, want, c, roll;
	uint8_t club, tries, shape = SHAPE_NORMAL;

	if (v->lie == LIE_GREEN || v->lie == LIE_FRINGE) {
		putt(v, k, &rs, o);
		return;
	}
	yards = (pin + FIX_HALF) >> 16;
	/* the wind against the shot and across it, in mph (across: above 0 blows it left) */
	rel = (angle)(v->wind_dir - a);
	head = -((int32_t)v->wind_mph * icos(rel)) >> 14;
	cross = ((int32_t)v->wind_mph * isin(rel)) >> 14;
	/* about 1 yd in 130 a mph into the wind, half that with it */
	want = yards + (yards * (head > 0 ? head : head / 2) / 130) * k->read / 4;
	if (want < 1) want = 1;
	club = ball_pick((uint16_t)want, v->lie);
	/* the better ones keep it under a strong headwind or hold it against a crosswind */
	if (skill >= CPU_GOOD) {
		if (head >= 10 && club >= CLUB_3I && club <= CLUB_9I) {
			uint8_t p;
			for (p = CLUB_3I; p <= CLUB_9I; p++)
				if (ball_shape_carry(p, v->lie, SHAPE_PUNCH) <= want) break;
			if (p <= CLUB_9I) {
				club = p;
				shape = SHAPE_PUNCH;
			}
		} else if (club <= CLUB_5W && (cross >= 10 || cross <= -10)) {
			shape = cross > 0 ? SHAPE_FADE : SHAPE_DRAW;
		}
	}
	/* the aim off into a crosswind, about a sixth of a degree a mph */
	a = (angle)(a - (int16_t)(cross * 28 * k->read / 4));
	c = ball_shape_carry(club, v->lie, shape);
	roll = club < CLUB_PW ? c / 16 : 1;
	/* from the fairway on, short of what's lost: water or out of bounds where it would land */
	if (skill >= CPU_FAIR) {
		for (tries = 0; tries < 3 && club < CLUB_LW && wet(v, a, want < c ? want - roll : c); tries++) {
			club++;
			shape = SHAPE_NORMAL;
			c = ball_carry(club, v->lie);
			roll = club < CLUB_PW ? c / 16 : 1;
			want = c + roll;
		}
	}
	o->club = club;
	o->shape = shape;
	o->power = c > 0 ? power_for((want - roll) * 256 / c) : FIX_ONE;
	o->power += (o->power >> 8) * tri(&rs, k->power);
	if (o->power > FIX_ONE) o->power = FIX_ONE;
	if (o->power < FIX_ONE / 20) o->power = FIX_ONE / 20;
	o->snap = (fixed)tri(&rs, k->snap) * 256;
	if (next(&rs) < k->duff) o->snap = next(&rs) & 1 ? FIX_ONE * 3 / 4 : -FIX_ONE * 3 / 4;
	o->aim = (angle)(a + tri(&rs, k->aim));
}
