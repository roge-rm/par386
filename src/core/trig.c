#include "trig.h"
#include "trigtab.h"

int16_t isin(angle a)
{
	uint16_t off = a & 0x3FFF, i, f;
	int16_t s;

	if (a & 0x4000) off = 0x4000 - off;    /* second and fourth quarters run backwards */
	i = off >> 6;
	f = off & 63;
	s = sin_tab[i];
	if (f) s += (int16_t)((MULS16(sin_tab[i + 1] - s, f) + 32) >> 6);
	return (a & 0x8000) ? -s : s;
}

int16_t icos(angle a)
{
	return isin((angle)(a + ANG_90));
}

/* atan of num / den as an angle, for 0 <= num <= den, den > 0. */
static uint16_t SEC_CORE atan_ratio(uint32_t num, uint32_t den)
{
	uint32_t r;
	uint16_t i, f, t;

	while (den >= 0x10000) {
		den >>= 1;
		num >>= 1;
	}
	r = (num << 15) / den;    /* 0 to 32768 */
	i = (uint16_t)(r >> 8);
	f = (uint16_t)(r & 255);
	t = atan_tab[i];
	if (f) t += (uint16_t)((MULU16(atan_tab[i + 1] - t, f) + 128) >> 8);
	return t;
}

angle iatan2(int32_t y, int32_t x)
{
	uint32_t ax = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
	uint32_t ay = y < 0 ? 0u - (uint32_t)y : (uint32_t)y;
	uint16_t t;

	if (ax == 0 && ay == 0) return 0;
	if (ay <= ax) t = atan_ratio(ay, ax);
	else t = 0x4000 - atan_ratio(ax, ay);
	if (x < 0) t = 0x8000 - t;
	if (y < 0) t = (uint16_t)(0u - t);
	return (angle)t;
}
