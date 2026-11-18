/* Binary angles and trig. 65536 = full turn, 0 points along +x and angles
 * turn toward +y. Sines are 2.14 (16384 = 1) for use with fix_mulq14. */
#ifndef TRIG_H
#define TRIG_H

#include "fixed.h"

typedef uint16_t angle;

#define ANG_90  ((angle)0x4000)
#define ANG_180 ((angle)0x8000)
#define ANG_270 ((angle)0xC000)

/* Sine and cosine as 2.14, within 1 step of the true value. */
int16_t SEC_CORE isin(angle a);
int16_t SEC_CORE icos(angle a);
/* The same as 16.16. */
#define fix_sin(a) ((fixed)isin(a) * 4)
#define fix_cos(a) ((fixed)icos(a) * 4)

/* Angle of the point (x, y) from the origin, in any matching units. (0, 0) gives 0. */
angle SEC_CORE iatan2(int32_t y, int32_t x);

#endif
