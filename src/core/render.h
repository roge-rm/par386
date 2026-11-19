/* The view from behind the ball: terrain, sky and backdrop, object sprites,
 * the pin, the golfer and the ball, drawn into a Surface at its own depth.
 * No platform headers, no floats. Art comes in as resource data made by
 * tools/mkobj.py (formats there), so the core reads it the same way on the
 * host and the Palm. */
#ifndef RENDER_H
#define RENDER_H

#include "gfx.h"
#include "hole.h"

/* Logical rows left at the bottom for the panel (play.c): the view is the
 * top lh - RENDER_STATUS rows (times the surface's scale). */
#define RENDER_STATUS     26
#define RENDER_TYPES      13    /* object types 0..12 (hole.h) */
#define RENDER_VARIANTS   2     /* sprite variants per type, picked by the object's variant byte */
#define RENDER_SIZES      2     /* sprite sizes per type, small then big */

typedef struct {
	const uint8_t *p;
	uint32_t len;               /* 0 if missing */
} RenderData;

/* Golfer poses: address, half way back, the top of the swing, the finish. */
enum { POSE_ADDRESS, POSE_BACK, POSE_TOP, POSE_FINISH, RENDER_POSES };

/* The pixels kept under the golfer, so his pose can change over a finished
 * scene: at most this many logical units square. */
#define RENDER_UNDER_UNITS 40

/* The golfer: his frames for the scale the scene is drawn at (20 px on 160
 * wide screens, 40 on 320), and where render_scene put him. The caller
 * keeps it for as long as the scene is shown, with under pointing at
 * render_under_size bytes (or 0: the pose can't change). */
typedef struct {
	RenderData pose[RENDER_POSES];
	uint8_t *under;             /* one byte a pixel, at the drawing scale */
	int16_t bx, by;             /* the ball point, drawing pixels */
	int16_t x, y, w, h;         /* the box kept in under, drawing pixels; w is 0 if none */
	uint8_t half;               /* drawn at half size and doubled (Surface.half) */
	uint8_t shown;              /* the pose drawn now */
	uint8_t ball;               /* 1 if the ball is drawn at his feet */
} RenderGolfer;

/* Spr8 resources by object type, variant and size, the golfer and the Bkd8
 * strip for the surface's scale. */
typedef struct {
	RenderData spr[RENDER_TYPES][RENDER_VARIANTS][RENDER_SIZES];
	RenderGolfer *golfer;       /* 0 for no golfer */
	RenderData backdrop;
} RenderArt;

typedef struct {
	const Hole *hole;
	const SurfaceGrid *grid;    /* the hole's surface grid */
	vec2 ball;                  /* the camera stands behind this point */
	angle aim;                  /* looking this way */
	uint8_t pin;                /* which of the hole's pins has the flag */
	uint8_t green;              /* 1 for the green view (render_green) */
} RenderView;

/* Stages timed in RenderStats.ticks. */
enum { RS_GATHER, RS_TERRAIN, RS_SKY, RS_OBJECTS, RS_GOLFER, RS_COUNT };

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
	uint32_t ticks[RS_COUNT];          /* clock ticks per stage (RS_*), if render_clock was given one */
} RenderStats;

typedef uint32_t (*RenderClock)(void);

/* Where render_map put things, in pixels, and the ball to pin distance.
 * A point (x, y) on the hole is at left + x / ypp, top + (length - y) / ypp. */
typedef struct {
	int16_t bx, by, px, py;
	uint16_t yards;
	int16_t left, top;
	fixed ypp;                  /* yards a pixel */
	fixed length;               /* the hole's length in yards */
} RenderMap;

/* The camera render_scene or render_green uses for a view, for putting
 * things over it. The green view looks down at pc, ps and shows heights zk
 * times as steep around zref. */
typedef struct {
	fixed camx, camy, camz;     /* yards */
	fixed zref;                 /* yards */
	int16_t fx, fy, rx, ry;     /* forward and right, 2.14 */
	int16_t pc, ps;             /* cosine and sine of the look down, 2.14 */
	int16_t f, cx, yh;          /* focal length, centre column and the row straight ahead in pixels */
	int16_t zk;                 /* height scale */
	int16_t back;               /* yards from the camera back to the ball, along the ground */
} RenderCam;

/* Bytes of work memory render_scene needs for this surface. */
uint32_t SEC_RENDER render_work_size(const Surface *s);
/* Bytes for RenderGolfer.under. */
uint32_t SEC_RENDER render_under_size(const Surface *s);
/* Puts pose over the golfer in a finished scene s (or its kept copy), from
 * the pixels kept under him, with the ball at his feet if ball isn't 0.
 * Sets *x, *y, *w, *h to the pixels it changed. Returns -1 if it can't
 * (nothing kept, or no such frame). Doesn't change g: the caller sets
 * g->shown and g->ball once every copy is done. */
int SEC_RENDER render_pose(Surface *s, const RenderGolfer *g, uint8_t pose, uint8_t ball,
                           int16_t *x, int16_t *y, int16_t *w, int16_t *h);
/* Draws the view. work must hold render_work_size bytes; stats may be 0.
 * Returns 0, or -1 if the surface isn't 160 or 320 wide. */
int SEC_RENDER render_scene(Surface *s, const RenderView *v, const RenderArt *a,
                            void *work, RenderStats *stats);
/* Sets the clock render_scene times its stages with, or 0 for none. */
void SEC_RENDER render_clock(RenderClock fn);
/* Draws the whole hole from above into the view area, scaled to fit with
 * the tee at the bottom: surfaces, trees as dots, the tee, the pin, the
 * ball and a line from the ball to the pin. Text is left to the caller.
 * Returns 0, or -1 if the surface isn't 160 or 320 wide. */
int SEC_RENDER render_map(Surface *s, const RenderView *v, RenderMap *out);
/* A part of the hole for render_map_box, in yards. */
typedef struct {
	fixed x0, y0, x1, y1;
} MapBox;
/* render_map framed on box (kept to its middle, as big as fits), or on the
 * whole hole if box is 0 or the hole fits as big. With a box there's no
 * ball and no line to the pin. */
int SEC_RENDER render_map_box(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out);
/* The green view for putting: the ground around the ball and the pin seen
 * from above and behind the ball, looking along v->aim, as a grid of
 * shaded cells with lines across the green, heights shown 6 times as
 * steep so the slope reads, the cup and the flag. The ball is left to the
 * caller. Returns 0, or -1 if the surface isn't 160 or 320 wide. */
int SEC_RENDER render_green(Surface *s, const RenderView *v);
/* The camera for the view on surface s: render_scene's, or render_green's
 * if v->green is set. */
void SEC_RENDER render_camera(const Surface *s, const RenderView *v, RenderCam *c);
/* The pixel the point (x, y) at height z (yards) shows at, and its depth.
 * Returns 0 if it's nearer than 1 yard or behind the camera. */
int SEC_RENDER render_point(const RenderCam *c, fixed x, fixed y, fixed z, int16_t *sx, int16_t *sy, fixed *depth);

#endif
