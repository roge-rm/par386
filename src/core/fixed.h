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

#define PAR386_VERSION "0.2.0"

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

/* 16x16 bit products that compile to one mulu.w or muls.w on the 68000. */
#define MULU16(a, b) ((uint32_t)(uint16_t)(a) * (uint16_t)(b))
#define MULS16(a, b) ((int32_t)(int16_t)(a) * (int16_t)(b))

/* Fast paths for per-frame code: no 64-bit library calls, only 16x16 multiplies. */
/* a * b truncated toward zero, exact while |a * b| < 32768.0, no saturation. */
fixed SEC_CORE fix_mulf(fixed a, fixed b);
/* a * q / 16384 truncated toward zero (q is 2.14, e.g. a sine), for any a but FIX_MIN and |q| <= 16384. */
fixed SEC_CORE fix_mulq14(fixed a, int16_t q);
/* a * n for a whole number n, exact while |a * n| < 32768.0. */
#define fix_muli(a, n) ((fixed)(a) * (int32_t)(n))

/* Square root of a 32-bit whole number, rounded down. */
uint16_t SEC_CORE isqrt32(uint32_t v);
/* Square root of a 16.16 number, rounded down to the step. Negative gives 0. */
fixed SEC_CORE fix_sqrt(fixed a);

/* The game version as text. */
const char *SEC_CORE par386_version(void);

#endif
