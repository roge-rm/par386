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
#define RENDER_VIEW_H(s)  ((s)->lh - ((s)->fullview ? 0 : RENDER_STATUS))
#define RENDER_TYPES      13    /* object types 0..12 (hole.h) */
#define RENDER_VARIANTS   2     /* sprite variants per type, picked by the object's variant byte */
#define RENDER_SIZES      2     /* sprite sizes per type, small then big */

typedef struct {
	const uint8_t *p;
	uint32_t len;               /* 0 if missing */
} RenderData;

/* Golfer poses in sprite resource order. */
enum { POSE_ADDRESS, POSE_BACK, POSE_TOP, POSE_FINISH, POSE_IMPACT, RENDER_POSES };

/* The pixels kept under the golfer, so his pose can change over a finished
 * scene: at most this many logical units square. */
#define RENDER_UNDER_UNITS 56

/* The golfer: his frames for the scale the scene is drawn at (40 px on 160
 * wide screens, 80 on 320), and where render_scene put him. The caller
 * keeps it for as long as the scene is shown, with under pointing at
 * render_under_size bytes (or 0: the pose can't change). */
typedef struct {
	RenderData pose[RENDER_POSES];
	uint8_t *under;             /* one byte a pixel, at the drawing scale */
	fixed camera_raise;         /* offset from the heightmap camera, in yards */
	vec2 camera_shift;          /* offset for the dry address, in yards */
	int16_t camera_back;        /* actual mesh camera setback, in yards */
	int16_t bx, by;             /* the ball point, drawing pixels */
	int16_t x, y, w, h;         /* the box kept in under, drawing pixels; w is 0 if none */
	uint8_t half;               /* drawn at half size and doubled (Surface.half) */
	uint8_t shown;              /* the pose drawn now */
	uint8_t ball;               /* 1 if the ball is drawn at his feet */
	uint8_t left;               /* reflect the pose about the ball point */
	uint8_t skin;               /* SKIN_* from profile.h */
	uint8_t mesh_style;         /* contrast for the full-shot mesh */
	uint8_t shoes, pants, shirt, hat;  /* OUTFIT_* from profile.h */
} RenderGolfer;

/* Centre the address pose in a logical rectangle. The Surface origin is
 * the same as for UI controls. */
void SEC_CARD render_golfer_preview(const Surface *s, const RenderGolfer *g,
                                    int16_t x, int16_t y, int16_t w, int16_t h);

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
	uint8_t player;             /* whose turn the view shows */
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
/* render_scene in slices, to draw the next scene while something else
 * shows. render_scene_begin sets the draw up in work (render_work_size
 * bytes; v is copied, s's pixels, the hole, the grid, a and what it points
 * to have to stay put until it's done). Each render_scene_step then draws
 * on, stage by stage and a depth step, 8 sky rows or an object at a time,
 * until clock() reaches until (at least one piece a call), or to the end
 * if clock is 0. Returns 1 once the scene is as render_scene draws it, 0
 * while there's more. begin returns -1 if s won't do. */
int SEC_RENDER render_scene_begin(Surface *s, const RenderView *v, const RenderArt *a, void *work, RenderStats *stats);
int SEC_RENDER render_scene_step(void *work, RenderClock clock, uint32_t until);
/* Validate a packed terrain record. On success mesh points into data.
 * The caller keeps that record in place for as long as the hole uses it. */
int SEC_RENDER terrain_mesh_parse(TerrainMesh *mesh, const Hole *hole, const uint8_t *data, uint32_t size);
/* Work for an optional terrain mesh or visual contours, including the
 * usual scene work. Returns 0 when neither is available. */
uint32_t SEC_RENDER render_scene_visual_size(const Surface *s, const RenderView *v);
int SEC_RENDER render_scene_visual_begin(Surface *s, const RenderView *v, const RenderArt *a,
                                       void *work, RenderStats *stats);
int SEC_RENDER render_scene_visual(Surface *s, const RenderView *v, const RenderArt *a,
                                 void *work, RenderStats *stats);
/* Sets the clock render_scene times its stages with, or 0 for none. */
void SEC_RENDER render_clock(RenderClock fn);
/* The terrain's depth steps, the green's triangles and the sky rows go
 * through fn from now on instead of terra.h's loops, or back to them with
 * NULL: the platform's faster code, given the same job (TERRA_* op). */
void SEC_RENDER render_terra_use(void (*fn)(uint8_t op, void *job));
/* Draws the whole hole from above into the view area, scaled to fit with
 * the tee at the bottom: surfaces, trees as dots, the tee, the pin, the
 * ball and a line from the ball to the pin. Text is left to the caller.
 * Returns 0, or -1 if the surface isn't 160 or 320 wide. */
int SEC_RENDER render_map(Surface *s, const RenderView *v, RenderMap *out);
/* A part of the hole for render_map_box, in yards. */
typedef struct {
	fixed x0, y0, x1, y1;
} MapBox;
/* The aiming map with a line to the target. With a box, frame that area. */
int SEC_RENDER render_target_map(Surface *s, const RenderView *v, RenderMap *out, const vec2 *target, const MapBox *box);
/* The Dana's aiming map, tee on the left. RenderMap keeps its normal
 * coordinates: on screen, (x, y) is (s->w - 1 - y, x). */
int SEC_RENDER render_dana_map(Surface *s, const RenderView *v, RenderMap *out, const vec2 *target, const MapBox *box);
/* render_map framed on box (kept to its middle, as big as fits), or on the
 * whole hole if box is 0 or the hole fits as big. With a box there's no
 * ball and no line to the pin. */
int SEC_RENDER render_map_box(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out);
/* render_map_box in slices: the ground of view rows y0 to y1 (not y1, in
 * pixels of s), and once y1 reaches the view's height the rest (objects,
 * pin, strip, and the doubling at half size). Returns 1 when that's done,
 * 0 for a slice, -1 if s won't do. At half size the slices go into the
 * top left quarter until the end, so s isn't to be shown before then. */
int SEC_RENDER render_map_rows(Surface *s, const RenderView *v, const MapBox *box, RenderMap *out, int16_t y0, int16_t y1);
/* The green view for putting: the ground around the ball and the pin seen
 * from above and behind the ball, looking along v->aim, as a grid of
 * shaded cells with lines across the green, heights shown 6 times as
 * steep so the slope reads, the cup and the flag. The ball is left to the
 * caller. Returns 0, or -1 if the surface isn't 160 or 320 wide. */
int SEC_RENDER render_green(Surface *s, const RenderView *v);
/* Experimental synchronous draw. Work has render_green_compiled_size bytes. */
uint32_t SEC_DANA render_green_compiled_size(void);
int SEC_DANA render_green_compiled(Surface *s, const RenderView *v, void *work);
/* render_green in slices, as render_scene_begin and _step: work holds
 * render_work_size bytes, s's pixels, the hole and the grid stay put until
 * it's done, a row of cells at a time. */
int SEC_RENDER render_green_begin(Surface *s, const RenderView *v, void *work);
int SEC_RENDER render_green_step(void *work, RenderClock clock, uint32_t until);
/* The curved putting view uses the same step function. Size is zero on
 * unsupported surfaces or holes without putting contours. */
uint32_t SEC_CARD render_green_curves_size(const Surface *s, const RenderView *v);
int SEC_CARD render_green_curves_begin(Surface *s, const RenderView *v, void *work);
/* The camera for the view on surface s: render_scene's, or render_green's
 * if v->green is set. */
void SEC_RENDER render_camera(const Surface *s, const RenderView *v, RenderCam *c);
/* The pixel the point (x, y) at height z (yards) shows at, and its depth.
 * Returns 0 if it's nearer than 1 yard or behind the camera. */
int SEC_RENDER render_point(const RenderCam *c, fixed x, fixed y, fixed z, int16_t *sx, int16_t *sy, fixed *depth);

#endif
