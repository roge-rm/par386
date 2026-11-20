/* The ARM 16-bit conversion's entry (src/arm/to16arm.c) built for the host:
 * each gfx_to16 and gfx_to16_mask job is written out as the 68k passes it
 * (big endian, 32-bit pointers, the palette low byte first) and run through
 * to16arm_entry, and the output must match the plain path byte for byte,
 * and an old style loop written here. Memory the job points at is mapped
 * under 4 GB so its pointers fit. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "gfx.h"
#include "to16.h"
#include "to16arm.h"

static int checks, fails;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define W 163
#define H 37
#define DRB (W * 2 + 6)
#define LOW_SIZE (1 << 20)

static uint8_t *low, *lowp;     /* memory under 4 GB */
static uint8_t *palle, *dith;   /* the ARM side's palette and dither table, in low */
static uint8_t blk[TO16_SIZE];
static int jobs;

static void *lowalloc(size_t n)
{
	void *p = lowp;
	lowp += (n + 15) & ~(size_t)15;
	if (lowp > low + LOW_SIZE) {
		printf("FAIL low memory\n");
		exit(1);
	}
	return p;
}

static void put32(uint8_t *p, const void *v)
{
	uintptr_t u = (uintptr_t)v;
	if (u >> 32) {
		printf("FAIL pointer over 4 GB\n");
		exit(1);
	}
	p[0] = (uint8_t)(u >> 24);
	p[1] = (uint8_t)(u >> 16);
	p[2] = (uint8_t)(u >> 8);
	p[3] = (uint8_t)u;
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

/* What arm.c does on the 68k: the job as it lies in 68k memory, pal swapped
 * for the low byte first copy, then the native call. */
static void via_entry(const struct To16Job *a)
{
	CHECK(memcmp(a->dith, dith, 16) == 0);
	put32(blk + TO16_BITS, a->bits);
	put32(blk + TO16_D, a->d);
	put32(blk + TO16_PAL, palle);
	put32(blk + TO16_DITH, dith);
	put16(blk + TO16_SRB, a->srb);
	put16(blk + TO16_DRB, a->drb);
	put16(blk + TO16_X0, (uint16_t)a->x0);
	put16(blk + TO16_Y0, (uint16_t)a->y0);
	put16(blk + TO16_X1, (uint16_t)a->x1);
	put16(blk + TO16_Y1, (uint16_t)a->y1);
	blk[TO16_LE] = a->le;
	blk[TO16_MASK] = a->mask;
	blk[TO16_LO] = a->lo;
	blk[TO16_HI] = a->hi;
	to16arm_entry(NULL, blk, NULL);
	jobs++;
}

/* The conversion as a plain loop over the rectangle. */
static void old_style(const Surface *s, uint8_t *d, int x0, int y0, int x1, int y1, int le, int lo, int hi)
{
	static const uint8_t dv[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	const uint16_t *pal = gfx_pal565();
	int x, y;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > s->w) x1 = s->w;
	if (y1 > s->h) y1 = s->h;
	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++) {
			int t = dv[((y & 3) << 2) | (x & 3)];
			uint16_t c = pal[s->bits[y * s->rowBytes + x]];
			if (t < lo || t >= hi) continue;
			d[y * DRB + 2 * x] = (uint8_t)(le ? c : c >> 8);
			d[y * DRB + 2 * x + 1] = (uint8_t)(le ? c >> 8 : c);
		}
}

static const int16_t rects[][4] = {
	{ 0, 0, W, H }, { 3, 5, 17, 9 }, { 1, 1, 1, 1 }, { W - 3, H - 2, 5, 5 }, { -4, -3, 9, 8 },
	{ 7, 0, 2, H }, { 0, 11, W, 1 }, { 50, 20, 0, 4 }, { 2, 2, W - 5, H - 3 },
};
static const uint8_t steps[][2] = { { 0, 16 }, { 0, 2 }, { 2, 16 }, { 5, 10 }, { 14, 16 }, { 3, 4 }, { 7, 7 } };

int main(void)
{
	Surface s;
	uint8_t *d1, *d2, *d3;
	size_t r, k, i;
	int le;

	low = mmap(NULL, LOW_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (low == MAP_FAILED) {
		printf("FAIL mmap\n");
		return 1;
	}
	lowp = low;
	palle = lowalloc(512);
	dith = lowalloc(16);
	{
		static const uint8_t dv[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
		memcpy(dith, dv, 16);
	}
	to16_pal_le(palle, gfx_pal565());
	for (i = 0; i < 256; i++) CHECK((palle[2 * i] | palle[2 * i + 1] << 8) == gfx_pal565()[i]);

	memset(&s, 0, sizeof s);
	s.w = W;
	s.h = H;
	s.depth = 8;
	s.scale = 1;
	s.rowBytes = W + 1;
	s.bits = lowalloc((size_t)s.rowBytes * H);
	gfx_init(&s);
	for (i = 0; i < (size_t)s.rowBytes * H; i++) s.bits[i] = (uint8_t)(i * 37 + 11 + (i >> 7));
	d1 = lowalloc((size_t)DRB * H);
	d2 = lowalloc((size_t)DRB * H);
	d3 = lowalloc((size_t)DRB * H);

	for (le = 0; le < 2; le++)
		for (r = 0; r < sizeof rects / sizeof rects[0]; r++)
			for (k = 0; k < sizeof steps / sizeof steps[0]; k++) {
				const int16_t *q = rects[r];
				uint8_t lo = steps[k][0], hi = steps[k][1];
				for (i = 0; i < (size_t)DRB * H; i++) d1[i] = d2[i] = d3[i] = (uint8_t)(i * 13 + 5);
				gfx_to16_use(NULL);
				if (lo == 0 && hi == 16 && (r & 1)) gfx_to16(&s, d1, DRB, q[0], q[1], q[2], q[3], (uint8_t)le);
				else gfx_to16_mask(&s, d1, DRB, q[0], q[1], (int16_t)(q[0] + q[2]), (int16_t)(q[1] + q[3]), (uint8_t)le, lo, hi);
				gfx_to16_use(via_entry);
				if (lo == 0 && hi == 16 && (r & 1)) gfx_to16(&s, d2, DRB, q[0], q[1], q[2], q[3], (uint8_t)le);
				else gfx_to16_mask(&s, d2, DRB, q[0], q[1], (int16_t)(q[0] + q[2]), (int16_t)(q[1] + q[3]), (uint8_t)le, lo, hi);
				gfx_to16_use(NULL);
				old_style(&s, d3, q[0], q[1], q[0] + q[2], q[1] + q[3], le, lo, hi);
				CHECK(memcmp(d1, d2, (size_t)DRB * H) == 0);
				CHECK(memcmp(d1, d3, (size_t)DRB * H) == 0);
			}
	/* the job count: every rectangle with something in it, both byte orders */
	CHECK(jobs == 2 * 7 * 8);
	printf("armtest: %d checks, %d failed, %d jobs through the entry\n", checks, fails, jobs);
	munmap(low, LOW_SIZE);
	return fails != 0;
}
