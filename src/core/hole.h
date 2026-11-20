/* Course and hole records (doc/course-format.md): parsing, the surface grid
 * and height lookups. World units are yards in 16.16, x across the hole and
 * y along it from the tee end. */
#ifndef HOLE_H
#define HOLE_H

#include "vec.h"

#define COURSE_VERSION    4      /* 1 to 3 are read too: scenery 0, the usual wind and green speed */
#define GREEN_USUAL       10     /* Course.green: the usual speed, also given by 0 */
#define HOLE_VERSION      1
#define COURSE_MAX_HOLES  36
#define COURSE_MAX_TEES   4
#define HOLE_PINS         4
#define HOLE_NAME_MAX     31
#define TEE_NAME_MAX      15
#define HOLE_MAX_WIDTH    256    /* yards */
#define HOLE_MAX_LENGTH   640
#define HOLE_MAX_HM_W     64
#define HOLE_MAX_HM_H     128
#define HOLE_MAX_SHAPES   128
#define HOLE_MAX_VERTS    64
#define HOLE_MAX_OBJECTS  1024

/* Surface types. Shapes are drawn in order and later ones win. */
enum {
	SURF_ROUGH, SURF_FIRST_CUT, SURF_FAIRWAY, SURF_DEEP_ROUGH, SURF_GREEN,
	SURF_FRINGE, SURF_BUNKER, SURF_WATER, SURF_PATH, SURF_OOB, SURF_TEE,
	SURF_COUNT
};

/* Object types. Unknown ones are kept so newer courses still load. */
enum {
	OBJ_TREE = 1, OBJ_PINE, OBJ_PALM, OBJ_BUSH, OBJ_ROCK, OBJ_HOUSE, OBJ_STAND,
	OBJ_WALL, OBJ_GORSE, OBJ_GRASS,
	OBJ_LANDMARK,       /* the course's tall landmark: a kirk, lighthouse, pagoda, etc. */
	OBJ_ANIMAL          /* the course's animals */
};

/* Errors from course_parse and hole_parse. */
enum {
	HOLE_OK, HOLE_ERR_SHORT, HOLE_ERR_LONG, HOLE_ERR_VERSION, HOLE_ERR_VALUE
};

typedef struct {
	char name[HOLE_NAME_MAX + 1];
	char tee_names[COURSE_MAX_TEES][TEE_NAME_MAX + 1];
	uint16_t yards[COURSE_MAX_HOLES];     /* from the longest tee */
	uint8_t par[COURSE_MAX_HOLES];
	uint8_t hole_count, tee_count;
	uint8_t scenery;    /* which sky and backdrop art, 0 the parkland one */
	uint8_t turn[COURSE_MAX_HOLES];       /* each hole's map turned against the course, 256 a turn */
	uint8_t wind;       /* average wind in mph, 0 for the usual 10 */
	uint8_t green;      /* green speed, 6 to 14, 0 for the usual GREEN_USUAL; higher rolls further */
} Course;

typedef struct {
	vec2 pos;
	uint16_t yards;
	uint8_t set;    /* index into Course.tee_names */
} HoleTee;

typedef struct {
	vec2 pos;
	uint8_t type, variant, scale;    /* scale: 64 is normal size */
} HoleObject;

/* Points into the record it was parsed from, which must stay put. */
typedef struct {
	const uint8_t *heights;    /* hm_w * hm_h bytes, row 0 at y = 0 */
	const uint8_t *shapes;
	const uint8_t *objects;
	fixed base, step;          /* height of byte 0, height per byte step, yards */
	vec2 aim;                  /* where the first shot aims */
	vec2 pins[HOLE_PINS];
	HoleTee tees[COURSE_MAX_TEES];
	uint16_t width, length;    /* yards */
	uint16_t object_count;
	uint16_t turn;             /* Course.turn << 8 for the backdrop; hole_parse leaves it 0, the caller sets it */
	uint8_t wind;              /* Course.wind, set by the caller as turn is */
	uint8_t green;             /* Course.green, set by the caller */
	uint8_t tee;               /* index into tees of the tee being played, set by hole_use_tee */
	uint8_t number, par, hm_w, hm_h, hm_shift, base_surface, tee_count, shape_count;
	char name[HOLE_NAME_MAX + 1];
} Hole;

/* The surface grid: 2 yard cells, 4 bits each, two to a byte with the even
 * column in the high nibble, row 0 at y = 0. */
#define SURF_CELL_SHIFT   1
#define SURF_ROW_MAX      ((HOLE_MAX_WIDTH / 2 + 1) / 2)

typedef struct {
	const uint8_t *cells;
	uint16_t w, h, stride;    /* cells across and along, bytes per row */
} SurfaceGrid;

typedef void (*SurfaceRowFn)(void *ctx, uint16_t row, const uint8_t *bytes, uint16_t len);

/* Both return HOLE_OK or an error and never read past len. */
int SEC_CORE course_parse(Course *c, const uint8_t *buf, uint32_t len);
int SEC_CORE hole_parse(Hole *h, const uint8_t *buf, uint32_t len);

/* Plays the hole from tee set set: its own tee, else the nearest longer one,
 * else the first. */
void SEC_CORE hole_use_tee(Hole *h, uint8_t set);

/* A putt of len yards from a to b as the length that rolls the same on a
 * green of the usual speed: only the part over the green is scaled by
 * the course's speed (8 points along the line). */
fixed SEC_CORE hole_putt_len(const Hole *h, const SurfaceGrid *g, vec2 a, vec2 b, fixed len);

/* Object i of the hole, i < object_count. */
void SEC_CORE hole_object(const Hole *h, uint16_t i, HoleObject *o);
/* Ground height in yards at (x, y), held level past the heightmap's edge. */
fixed SEC_CORE hole_height(const Hole *h, fixed x, fixed y);

/* Sets the grid size for the hole; cells stays as it was. */
void SEC_CORE surface_size(const Hole *h, SurfaceGrid *g);
/* Draws the shapes into the grid a row at a time, from row 0, through put.
 * row needs SURF_ROW_MAX bytes. */
void SEC_CORE surface_raster(const Hole *h, uint8_t *row, SurfaceRowFn put, void *ctx);
/* Surface at (x, y). Off the grid it's out of bounds, or water past a water cell on the edge. */
uint8_t SEC_CORE surface_at(const SurfaceGrid *g, fixed x, fixed y);

typedef struct {
	uint16_t entry, clear;     /* yards along the aim, clear is 0 when the hole ends in the hazard */
	uint8_t surface;
} HazardCarry;

/* Significant crossings along a line from the ball, sampled at one-yard intervals. */
uint8_t SEC_CORE hole_hazards(const Hole *h, const SurfaceGrid *g, vec2 ball, angle aim,
			    HazardCarry *out, uint8_t cap);

#endif
