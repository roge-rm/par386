/* Host tests for the game core. Build and run with `make host-test`. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fixed.h"
#include "vec.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static fixed F(double v) { return (fixed)(v * 65536.0 + (v < 0 ? -0.5 : 0.5)); }

static uint32_t seed = 1;
static uint32_t rnd32(void)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static double ang_rad(angle a) { return a * (2 * M_PI / 65536.0); }

/* Angle difference wrapped to -32768..32767. */
static int ang_diff(angle a, angle b) { return (int16_t)(uint16_t)(a - b); }

static void maths_tests(void)
{
	int i, worst;
	int32_t a, b;

	/* fix_mulf against the exact product, truncated toward zero, over |a * b| < 32768. */
	for (i = 0; i < 200000; i++) {
		int64_t p;
		a = (int32_t)rnd32() >> (rnd32() % 31);
		b = (int32_t)rnd32() >> (rnd32() % 31);
		p = (int64_t)a * b;
		if (p >= ((int64_t)1 << 47) || p <= -((int64_t)1 << 47)) continue;
		if (fix_mulf(a, b) != (fixed)(p / 65536)) { CHECK(fix_mulf(a, b) == (fixed)(p / 65536)); break; }
	}
	CHECK(fix_mulf(F(181), F(181)) == F(32761));
	CHECK(fix_mulf(F(-2.5), F(4)) == F(-10));
	/* fix_mulq14 against the exact product over all a but FIX_MIN, |q| <= 16384. */
	for (i = 0; i < 200000; i++) {
		int16_t q = (int16_t)((int32_t)(rnd32() % 32769) - 16384);
		a = (int32_t)rnd32() >> (rnd32() % 31);
		if (a == FIX_MIN) continue;
		if (fix_mulq14(a, q) != (fixed)((int64_t)a * q / 16384)) { CHECK(0); break; }
	}
	CHECK(fix_mulq14(FIX_MAX, 16384) == FIX_MAX);
	CHECK(fix_mulq14(-FIX_MAX, 16384) == -FIX_MAX);

	/* isqrt32 and fix_sqrt round down. */
	CHECK(isqrt32(0) == 0 && isqrt32(1) == 1 && isqrt32(3) == 1 && isqrt32(4) == 2);
	CHECK(isqrt32(0xFFFFFFFFu) == 65535);
	CHECK(isqrt32(65535u * 65535u) == 65535 && isqrt32(65535u * 65535u - 1) == 65534);
	for (i = 0; i < 100000; i++) {
		uint32_t v = rnd32() >> (rnd32() % 32), r = isqrt32(v);
		if (!((uint64_t)r * r <= v && ((uint64_t)r + 1) * (r + 1) > v)) { CHECK(0); break; }
	}
	CHECK(fix_sqrt(F(4)) == F(2) && fix_sqrt(F(2.25)) == F(1.5) && fix_sqrt(F(-1)) == 0);
	for (i = 0; i < 100000; i++) {
		uint32_t v = rnd32() >> (1 + rnd32() % 31);
		uint64_t r = (uint64_t)fix_sqrt((fixed)v), sq = (uint64_t)v << 16;
		if (!(r * r <= sq && (r + 1) * (r + 1) > sq)) { CHECK(0); break; }
	}

	/* Trig: every angle within 1 step of 2.14, and the exact quarter points. */
	worst = 0;
	for (i = 0; i < 65536; i++) {
		int es = abs(isin((angle)i) - (int)lround(sin(ang_rad(i)) * 16384));
		int ec = abs(icos((angle)i) - (int)lround(cos(ang_rad(i)) * 16384));
		if (es > worst) worst = es;
		if (ec > worst) worst = ec;
	}
	CHECK(worst <= 1);
	CHECK(isin(0) == 0 && isin(ANG_90) == 16384 && isin(ANG_180) == 0 && isin(ANG_270) == -16384);
	CHECK(icos(0) == 16384 && icos(ANG_180) == -16384);
	CHECK(fix_sin(ANG_90) == FIX_ONE);
	CHECK(abs(isin(0x2000) - 11585) <= 1);    /* 45 degrees */

	/* atan2 quadrants and axes. */
	CHECK(iatan2(0, 0) == 0);
	CHECK(iatan2(0, 5) == 0);
	CHECK(iatan2(5, 0) == ANG_90);
	CHECK(iatan2(0, -5) == ANG_180);
	CHECK(iatan2(-5, 0) == ANG_270);
	CHECK(iatan2(7, 7) == 0x2000);
	CHECK(iatan2(7, -7) == 0x6000);
	CHECK(iatan2(-7, -7) == 0xA000);
	CHECK(iatan2(-7, 7) == 0xE000);
	CHECK(iatan2(FIX_MAX, FIX_MAX) == 0x2000);
	CHECK(iatan2(FIX_MIN, 1) == ANG_270 || abs(ang_diff(iatan2(FIX_MIN, 1), ANG_270)) <= 1);
	/* Accuracy against atan2 over random points and sizes. */
	worst = 0;
	for (i = 0; i < 200000; i++) {
		int32_t y = (int32_t)rnd32() >> (rnd32() % 31), x = (int32_t)rnd32() >> (rnd32() % 31);
		double t = atan2((double)y, (double)x);
		angle want;
		int e;
		if (x == 0 && y == 0) continue;
		if (t < 0) t += 2 * M_PI;
		want = (angle)(uint32_t)lround(t * 65536 / (2 * M_PI));
		e = abs(ang_diff(iatan2(y, x), want));
		if (e > worst) worst = e;
	}
	printf("atan2 worst error %d (65536 a turn)\n", worst);
	CHECK(worst <= 3);
	/* atan2 undoes sin and cos. */
	for (i = 0; i < 65536; i += 7)
		if (abs(ang_diff(iatan2(fix_sin(i), fix_cos(i)), (angle)i)) > 3) { CHECK(0); break; }

	/* Vectors. */
	{
		vec2 v = vec2_make(F(3), F(4)), r;
		vec3 w = vec3_make(F(2), F(3), F(6)), n;
		CHECK(vec2_len(v) == F(5));
		CHECK(abs(vec2_len(vec2_make(F(300), F(400))) - F(500)) <= 64);
		CHECK(abs(vec2_len(vec2_make(F(-30000), F(1))) - F(30000)) <= 1024);
		CHECK(vec3_len(w) == F(7));
		CHECK(abs(vec3_len(vec3_make(F(-200), F(300), F(600))) - F(700)) <= 64);
		CHECK(vec2_dot(v, vec2_make(F(2), F(-1))) == F(2));
		CHECK(vec2_dot(vec2_make(F(30000), 0), vec2_make(F(30000), 0)) == FIX_MAX);
		CHECK(vec3_dot(w, w) == F(49));
		n = vec3_cross(vec3_make(FIX_ONE, 0, 0), vec3_make(0, FIX_ONE, 0));
		CHECK(n.x == 0 && n.y == 0 && n.z == FIX_ONE);
		n = vec3_norm(w);
		CHECK(abs(n.x - F(2.0 / 7)) <= 2 && abs(n.z - F(6.0 / 7)) <= 2);
		r = vec2_rotate(v, ANG_90);
		CHECK(abs(r.x - F(-4)) <= 8 && abs(r.y - F(3)) <= 8);
		r = vec2_rotate(vec2_make(F(100), 0), 0x2000);
		CHECK(abs(r.x - F(70.7107)) <= F(0.01) && abs(r.y - F(70.7107)) <= F(0.01));    /* 2.14 sines: 1 part in 16384 */
		CHECK(vec2_angle(vec2_make(F(-1), F(1))) == 0x6000);
		r = vec2_dir(ANG_180);
		CHECK(r.x == -FIX_ONE && r.y == 0);
		r = vec2_scale(vec2_add(v, vec2_sub(v, vec2_make(F(1), F(1)))), F(0.5));
		CHECK(r.x == F(2.5) && r.y == F(3.5));
	}
}

int main(void)
{
	maths_tests();

	CHECK(fix_from_int(3) == 3 * 65536);
	CHECK(fix_from_int(-3) == -3 * 65536);
	CHECK(fix_from_int(100000) == FIX_MAX);
	CHECK(fix_from_int(-100000) == FIX_MIN);

	CHECK(fix_mul(F(2.5), F(4)) == F(10));
	CHECK(fix_mul(F(-2.5), F(4)) == F(-10));
	CHECK(fix_mul(F(-1.5), F(-1.5)) == F(2.25));
	CHECK(fix_mul(F(0.5), F(0.5)) == F(0.25));
	CHECK(fix_mul(1, 1) == 0);               /* below one step */
	CHECK(fix_mul(32768, 1) == 1);           /* 0.5 step rounds up */
	CHECK(fix_mul(F(200), F(200)) == FIX_MAX);   /* saturates */
	CHECK(fix_mul(F(-200), F(200)) == FIX_MIN);

	CHECK(fix_div(F(10), F(4)) == F(2.5));
	CHECK(fix_div(F(-10), F(4)) == F(-2.5));
	CHECK(fix_div(F(10), F(-4)) == F(-2.5));
	CHECK(fix_div(FIX_ONE, F(3)) == 21845);  /* truncates toward zero */
	CHECK(fix_div(-FIX_ONE, F(3)) == -21845);
	CHECK(fix_div(F(5), 0) == FIX_MAX);
	CHECK(fix_div(F(-5), 0) == FIX_MIN);
	CHECK(fix_div(0, 0) == 0);

	CHECK(fix_to_int(F(2.4)) == 2);
	CHECK(fix_to_int(F(2.5)) == 3);
	CHECK(fix_to_int(F(-2.4)) == -2);
	CHECK(fix_to_int(F(-2.5)) == -3);
	CHECK(fix_to_int(FIX_MIN) == -32768);
	CHECK(fix_floor(F(2.9)) == 2);
	CHECK(fix_floor(F(-2.1)) == -3);

	CHECK(fix_abs(F(-1.25)) == F(1.25));
	CHECK(fix_abs(FIX_MIN) == FIX_MAX);

	CHECK(strcmp(par386_version(), "0.3.0") == 0);

	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
