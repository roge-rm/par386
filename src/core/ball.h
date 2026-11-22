/* Ball physics: the clubs, lies, wind, the flight, bounces and roll, water
 * and out of bounds. Fixed point, no floats, no platform headers, and the
 * same inputs always give the same shot.
 *
 * Units: positions in yards (16.16), x across the hole and y along it as in
 * hole.h, z the height in yards; velocities in yards a second, spin in
 * thousands of rpm, time in steps of 1/32 s. */
#ifndef BALL_H
#define BALL_H

#include "hole.h"

#ifdef __palmos__
#define SEC_BALL __attribute__((section("ball")))
#else
#define SEC_BALL
#endif

#define BALL_STEP_SHIFT  5      /* 32 steps a second */
#define BALL_FLY_EVERY   2      /* a path point every 2 steps in the air (1/16 s) */
#define BALL_ROLL_EVERY  4      /* and every 4 on the ground (1/8 s) */
#define BALL_PATH_MAX    192
#define BALL_PUTT_MAX    45     /* yards, the putter's longest range */
#define BALL_PUTT_GRADE  11     /* yards of flat-green pace per yard of rise */

enum {
	CLUB_DR, CLUB_3W, CLUB_5W, CLUB_3I, CLUB_4I, CLUB_5I, CLUB_6I, CLUB_7I,
	CLUB_8I, CLUB_9I, CLUB_PW, CLUB_GW, CLUB_SW, CLUB_LW, CLUB_PT, CLUB_COUNT
};

enum {
	LIE_TEE, LIE_FAIRWAY, LIE_FIRST_CUT, LIE_ROUGH, LIE_DEEP_ROUGH, LIE_BUNKER,
	LIE_PLUGGED, LIE_FRINGE, LIE_GREEN, LIE_PATH, LIE_COUNT
};

/* Shot shapes, picked on the aim screen. Normal is the plain shot; draw
 * starts right and curves left, fade the other way; high and low change the
 * height; punch is low and short with more roll and less wind; open is a
 * high, soft chip. Not every club has every shape (ball_shape_ok). */
enum { SHAPE_NORMAL, SHAPE_DRAW, SHAPE_FADE, SHAPE_HIGH, SHAPE_LOW, SHAPE_PUNCH, SHAPE_OPEN, SHAPE_COUNT };

/* How a shot ended. */
enum { SHOT_OK, SHOT_WATER, SHOT_OOB, SHOT_HOLED };

#define BALL_CUP_R  3867        /* the cup's radius, 2.125 inches, in 16.16 yards */

/* A point on the ball's path, in 1/16 yards. */
typedef struct { int16_t x, y, z; } BallPt;

typedef struct {
	const Hole *hole;
	const SurfaceGrid *grid;
	vec2 pos;          /* where the ball is */
	vec2 pin;
	vec2 wind;         /* yards a second, the way it blows */
	angle aim;
	fixed power;       /* 0 to 1.0 */
	fixed snap;        /* -1.0 to 1.0: above 0 early (a slice, curving right), below 0 late (a hook) */
	fixed range;       /* putter: yards it rolls at full power on a flat green, 0 for BALL_PUTT_MAX */
	uint8_t club, lie;
	uint8_t mishit;    /* the meter ran out */
	uint8_t shape;     /* SHAPE_*, normal for the putter */
} ShotIn;

typedef struct {
	vec2 start, land, stop;    /* stop is where the next shot is played from, after any drop */
	fixed carry, total;        /* yards from start to the first landing and to where it stopped */
	uint8_t result;            /* SHOT_* */
	uint8_t lie;               /* the lie at stop */
	uint8_t land_surf;         /* surface at the first landing */
	uint8_t lip;               /* it rolled over the cup too fast and lipped out */
	uint16_t steps;            /* how long it moved, in steps */
	uint8_t n, fly_n;          /* path points, and how many of them are in the air */
	uint8_t land_pt, lip_pt;   /* first landing and first lip-out points, 0 if absent */
	BallPt path[BALL_PATH_MAX];
} Shot;

/* Plays a shot from in to the end. The putter only rolls, at a speed that
 * takes it in->power times in->range on a flat green. A ball rolling over
 * the cup drops if it's slow enough for how near the middle it goes, or
 * lips out; one that stops over the cup drops. */
void SEC_BALL ball_shot(const ShotIn *in, Shot *out);

/* Dry ground back on the line from the cup through the water crossing. */
vec2 SEC_BALL ball_water_relief(const Hole *h, const SurfaceGrid *g, vec2 pin, vec2 cross, vec2 fallback);

/* The lie for a surface the ball stopped on (bunkers give LIE_BUNKER). */
uint8_t SEC_BALL ball_lie(uint8_t surface);
/* Short names for the status strip, at most 8 characters. */
const char *SEC_BALL ball_lie_name(uint8_t lie);
const char *SEC_BALL ball_club_name(uint8_t club);
/* Full-power carry in yards on flat ground with no wind from the lie. 0 for the putter. */
uint16_t SEC_BALL ball_carry(uint8_t club, uint8_t lie);
/* ball_carry with a shape picked, as near as the flight gives it. */
uint16_t SEC_BALL ball_shape_carry(uint8_t club, uint8_t lie, uint8_t shape);
/* The longest club whose carry from the lie doesn't go past yards; the
 * driver only from the tee, never the putter. */
uint8_t SEC_BALL ball_pick(uint16_t yards, uint8_t lie);

/* Whether a club can play a shape: the putter only normal, punch from the
 * 3 iron down, open from the 9 iron down. */
uint8_t SEC_BALL ball_shape_ok(uint8_t club, uint8_t shape);
/* The next shape the club can play after shape, round to normal. */
uint8_t SEC_BALL ball_shape_next(uint8_t club, uint8_t shape);
/* Short names for the strip, at most 5 characters; normal is "Shape". */
const char *SEC_BALL ball_shape_name(uint8_t shape);

/* Wind for a hole from the round's seed: 0 to twice avg mph (avg 0 is 10),
 * from any direction. dir is the way it blows. */
void SEC_BALL ball_wind(uint32_t seed, uint8_t hole, uint8_t avg, uint8_t *mph, angle *dir);
/* The wind as yards a second. */
vec2 SEC_BALL ball_wind_vec(uint8_t mph, angle dir);

#endif
