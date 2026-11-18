#include "fixed.h"

static fixed SEC_CORE saturate(int64_t v)
{
	if (v > FIX_MAX) return FIX_MAX;
	if (v < FIX_MIN) return FIX_MIN;
	return (fixed)v;
}

fixed fix_from_int(int v)
{
	return saturate((int64_t)v * FIX_ONE);
}

int fix_to_int(fixed a)
{
	if (a >= 0) return (int)(((int64_t)a + FIX_HALF) >> FIX_SHIFT);
	return -(int)(((int64_t)-(int64_t)a + FIX_HALF) >> FIX_SHIFT);
}

int fix_floor(fixed a)
{
	return (int)(a >> FIX_SHIFT);
}

fixed fix_mul(fixed a, fixed b)
{
	int64_t p = (int64_t)a * b;
	/* Round half up on the shifted product. */
	return saturate((p + FIX_HALF) >> FIX_SHIFT);
}

fixed fix_div(fixed a, fixed b)
{
	if (b == 0) return a > 0 ? FIX_MAX : a < 0 ? FIX_MIN : 0;
	return saturate(((int64_t)a * FIX_ONE) / b);
}

fixed fix_abs(fixed a)
{
	return a < 0 ? (a == FIX_MIN ? FIX_MAX : -a) : a;
}

const char *SEC_CORE par386_version(void)
{
	return PAR386_VERSION;
}

fixed fix_mulf(fixed a, fixed b)
{
	uint32_t ua, ub, r;
	int neg = 0;

	if (a < 0) { ua = 0u - (uint32_t)a; neg = 1; } else ua = (uint32_t)a;
	if (b < 0) { ub = 0u - (uint32_t)b; neg ^= 1; } else ub = (uint32_t)b;
	/* Every partial product is part of the result, so none can overflow while the result fits. */
	r = (MULU16(ua >> 16, ub >> 16) << 16)
	    + MULU16(ua >> 16, ub) + MULU16(ua, ub >> 16)
	    + (MULU16(ua, ub) >> 16);
	return neg ? -(fixed)r : (fixed)r;
}

fixed fix_mulq14(fixed a, int16_t q)
{
	uint32_t ua, r;
	uint16_t uq;
	int neg = 0;

	if (a < 0) { ua = 0u - (uint32_t)a; neg = 1; } else ua = (uint32_t)a;
	if (q < 0) { uq = (uint16_t)-q; neg ^= 1; } else uq = (uint16_t)q;
	/* (hi << 16) * q >> 14 is exact, so only the low half gets truncated. */
	r = (MULU16(ua >> 16, uq) << 2) + (MULU16(ua, uq) >> 14);
	return neg ? -(fixed)r : (fixed)r;
}

uint16_t isqrt32(uint32_t v)
{
	uint32_t root = 0, bit = (uint32_t)1 << 30;

	while (bit > v) bit >>= 2;
	while (bit) {
		if (v >= root + bit) {
			v -= root + bit;
			root = (root >> 1) + bit;
		} else {
			root >>= 1;
		}
		bit >>= 2;
	}
	return (uint16_t)root;
}

fixed fix_sqrt(fixed a)
{
	/* Bit by bit on a << 16 without 64 bits: two remainder bits come in each step. */
	uint32_t root = 0, rem = 0, lo, test;
	int i;

	if (a <= 0) return 0;
	lo = (uint32_t)a;
	for (i = 0; i < 24; i++) {
		rem = (rem << 2) | (lo >> 30);
		lo <<= 2;
		root <<= 1;
		test = (root << 1) + 1;
		if (rem >= test) {
			rem -= test;
			root++;
		}
	}
	return (fixed)root;
}
