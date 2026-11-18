/* 16.16 fixed point. No floats and no platform headers. */
#ifndef FIXED_H
#define FIXED_H

#include <stdint.h>
#include "section.h"

typedef int32_t fixed;

#define FIX_SHIFT 16
#define FIX_ONE   ((fixed)1 << FIX_SHIFT)
#define FIX_HALF  (FIX_ONE >> 1)

#define FIX_MAX   INT32_MAX
#define FIX_MIN   INT32_MIN

#define PAR386_VERSION "0.0.1"

fixed SEC_CORE fix_from_int(int v);
/* Rounds to the nearest whole number, halves away from zero. */
int SEC_CORE fix_to_int(fixed a);
/* Rounds toward minus infinity. */
int SEC_CORE fix_floor(fixed a);
/* a * b, rounded to the nearest step, results past the range saturate. */
fixed SEC_CORE fix_mul(fixed a, fixed b);
/* a / b, rounded toward zero. A zero b gives FIX_MAX or FIX_MIN by the sign of a (0 for 0). */
fixed SEC_CORE fix_div(fixed a, fixed b);
fixed SEC_CORE fix_abs(fixed a);

/* The game version as text. */
const char *SEC_CORE par386_version(void);

#endif
