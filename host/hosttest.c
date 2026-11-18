/* Host tests for the game core. Build and run with `make host-test`. */
#include <stdio.h>
#include <string.h>
#include "fixed.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static fixed F(double v) { return (fixed)(v * 65536.0 + (v < 0 ? -0.5 : 0.5)); }

int main(void)
{
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

	CHECK(strcmp(par386_version(), "0.0.1") == 0);

	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
