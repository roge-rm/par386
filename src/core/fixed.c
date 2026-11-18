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
