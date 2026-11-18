/* The view from behind the ball: terrain, sky and backdrop, object sprites,
 * the pin, the golfer and the ball, drawn into a Surface at its own depth.
 * No platform headers, no floats. Art comes in as resource data made by
 * tools/mkobj.py (formats there), so the core reads it the same way on the
 * host and the Palm. */
#ifndef RENDER_H
#define RENDER_H

#include "gfx.h"
#include "hole.h"

/* Logical rows left at the bottom for the status bar: the view is the top
 * 160 - RENDER_STATUS rows (times the surface's scale). */
#define RENDER_STATUS     16
#define RENDER_TYPES      9     /* object types 0..8 (hole.h) */
#define RENDER_SIZES      2     /* sprite sizes per type, small then big */

typedef struct {
	const uint8_t *p;
	uint32_t len;               /* 0 if missing */
} RenderData;

/* Spr8 resources by object type and size, the golfer and the Bkd8 strip
 * for the surface's scale (golfer 20 px on 160 wide screens, 40 on 320). */
typedef struct {
	RenderData spr[RENDER_TYPES][RENDER_SIZES];
	RenderData golfer;
	RenderData backdrop;
} RenderArt;

typedef struct {
	const Hole *hole;
	const SurfaceGrid *grid;    /* the hole's surface grid */
	vec2 ball;                  /* the camera stands behind this point */
	angle aim;                  /* looking this way */
	uint8_t pin;                /* which of the hole's pins has the flag */
} RenderView;

/* Counts for working out the time on a slow CPU. */
typedef struct {
	uint32_t steps;             /* depth steps marched */
	uint32_t samples;           /* height samples, one per column per step */
	uint32_t spans;             /* samples that drew something */
	uint32_t pixels;            /* terrain pixels */
	uint32_t sky;               /* sky and backdrop pixels */
	uint32_t objects;           /* sprites drawn */
	uint32_t sprite_px;         /* sprite pixels looked at */
	uint32_t divides;
	uint32_t dropped;           /* objects left out: clip pool or object list full */
} RenderStats;

/* Bytes of work memory render_scene needs for this surface. */
uint32_t SEC_RENDER render_work_size(const Surface *s);
/* Draws the view. work must hold render_work_size bytes; stats may be 0.
 * Returns 0, or -1 if the surface isn't 160 or 320 wide. */
int SEC_RENDER render_scene(Surface *s, const RenderView *v, const RenderArt *a,
                            void *work, RenderStats *stats);

#endif
