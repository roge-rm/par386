/* The ARM code for OS 5, one `armc' resource called through PceNativeCall:
 * gfx_to16 and gfx_to16_mask's loop (src/core/to16.c) as native code. The
 * 68k side passes its To16 struct, which is big endian, so the entry reads
 * each field byte by byte. Built for the host too, for the tests.
 *
 * The resource is a flat blob run where it lies: no globals, no tables
 * reached by address and no library calls (the Makefile checks the
 * relocations). to16arm_entry has to come first (src/arm/armc.ld). */
#include "to16.h"
#include "to16arm.h"

static uint32_t be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

unsigned long TO16ARM_ENTRY to16arm_entry(const void *emul, void *user, void *call68k)
{
	const uint8_t *b = (const uint8_t *)user;
	To16 a;

	(void)emul;
	(void)call68k;
	a.bits = (const uint8_t *)(uintptr_t)be32(b + TO16_BITS);
	a.d = (uint8_t *)(uintptr_t)be32(b + TO16_D);
	a.pal = (const uint16_t *)(uintptr_t)be32(b + TO16_PAL);
	a.dith = (const uint8_t *)(uintptr_t)be32(b + TO16_DITH);
	a.srb = be16(b + TO16_SRB);
	a.drb = be16(b + TO16_DRB);
	a.x0 = (int16_t)be16(b + TO16_X0);
	a.y0 = (int16_t)be16(b + TO16_Y0);
	a.x1 = (int16_t)be16(b + TO16_X1);
	a.y1 = (int16_t)be16(b + TO16_Y1);
	a.le = b[TO16_LE];
	a.mask = b[TO16_MASK];
	a.lo = b[TO16_LO];
	a.hi = b[TO16_HI];
	a.width = (int16_t)be16(b + TO16_WIDTH);
	a.smooth_y1 = (int16_t)be16(b + TO16_SMOOTH_Y1);
	to16_run(&a);
	return 0;
}
