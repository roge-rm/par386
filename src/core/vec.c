#include "vec.h"

vec2 vec2_make(fixed x, fixed y)
{
	vec2 r;
	r.x = x;
	r.y = y;
	return r;
}

vec2 vec2_add(vec2 a, vec2 b) { return vec2_make(a.x + b.x, a.y + b.y); }
vec2 vec2_sub(vec2 a, vec2 b) { return vec2_make(a.x - b.x, a.y - b.y); }
vec2 vec2_scale(vec2 v, fixed s) { return vec2_make(fix_mulf(v.x, s), fix_mulf(v.y, s)); }

static fixed SEC_CORE sat_add(fixed d, fixed e)
{
	if (e > 0 && d > FIX_MAX - e) return FIX_MAX;
	if (e < 0 && d < FIX_MIN - e) return FIX_MIN;
	return d + e;
}

fixed vec2_dot(vec2 a, vec2 b)
{
	return sat_add(fix_mul(a.x, b.x), fix_mul(a.y, b.y));
}

/* Square root of x*x + y*y + z*z for magnitudes, scaled down first so the
 * squares stay well inside 16.16 (each part under 64.0). */
static fixed SEC_CORE len3(uint32_t x, uint32_t y, uint32_t z)
{
	int s = 0;
	fixed r;

	while ((x | y | z) >= 0x400000) {
		x >>= 1;
		y >>= 1;
		z >>= 1;
		s++;
	}
	r = fix_sqrt(fix_mulf((fixed)x, (fixed)x) + fix_mulf((fixed)y, (fixed)y)
	             + fix_mulf((fixed)z, (fixed)z));
	return s ? (fixed)((uint32_t)r << s) : r;
}

static uint32_t SEC_CORE uabs(fixed a)
{
	return a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
}

fixed vec2_len(vec2 v)
{
	return len3(uabs(v.x), uabs(v.y), 0);
}

vec2 vec2_rotate(vec2 v, angle a)
{
	int16_t s = isin(a), c = icos(a);
	return vec2_make(fix_mulq14(v.x, c) - fix_mulq14(v.y, s),
	                 fix_mulq14(v.x, s) + fix_mulq14(v.y, c));
}

vec2 vec2_dir(angle a)
{
	return vec2_make(fix_cos(a), fix_sin(a));
}

angle vec2_angle(vec2 v)
{
	return iatan2(v.y, v.x);
}

vec3 vec3_make(fixed x, fixed y, fixed z)
{
	vec3 r;
	r.x = x;
	r.y = y;
	r.z = z;
	return r;
}

vec3 vec3_add(vec3 a, vec3 b) { return vec3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
vec3 vec3_sub(vec3 a, vec3 b) { return vec3_make(a.x - b.x, a.y - b.y, a.z - b.z); }

vec3 vec3_scale(vec3 v, fixed s)
{
	return vec3_make(fix_mulf(v.x, s), fix_mulf(v.y, s), fix_mulf(v.z, s));
}

fixed vec3_dot(vec3 a, vec3 b)
{
	return sat_add(sat_add(fix_mul(a.x, b.x), fix_mul(a.y, b.y)), fix_mul(a.z, b.z));
}

vec3 vec3_cross(vec3 a, vec3 b)
{
	return vec3_make(fix_mul(a.y, b.z) - fix_mul(a.z, b.y),
	                 fix_mul(a.z, b.x) - fix_mul(a.x, b.z),
	                 fix_mul(a.x, b.y) - fix_mul(a.y, b.x));
}

fixed vec3_len(vec3 v)
{
	return len3(uabs(v.x), uabs(v.y), uabs(v.z));
}

vec3 vec3_norm(vec3 v)
{
	fixed l = vec3_len(v);
	if (l == 0) return vec3_make(0, 0, 0);
	return vec3_make(fix_div(v.x, l), fix_div(v.y, l), fix_div(v.z, l));
}
