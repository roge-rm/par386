/* The round: up to 4 players taking turns at stroke play, whose turn it
 * is, the honour on the tee, each hole's strokes and the scorecard totals,
 * and the saved round's record. No platform headers.
 *
 * A player's ball, lie and strokes so far on the hole live here; play.c
 * plays the shots and writes the results back. */
#ifndef RULES_H
#define RULES_H

#include "hole.h"

#define RULES_PLAYERS     4
#define RULES_NAME_MAX    8      /* fits the status strip with the lie */
#define RULES_COURSE_MAX  31     /* the course database's name */

#define RULES_SAVE_VERSION 1
/* The most a saved round's record takes. */
#define RULES_SAVE_MAX    (2 + 32 + 4 + 9 + COURSE_MAX_HOLES + RULES_PLAYERS * (RULES_NAME_MAX + 1 + 11 + COURSE_MAX_HOLES))

typedef struct {
	vec2 ball;                          /* where it lies, once it's off the tee */
	uint8_t lie;                        /* LIE_* (ball.h) */
	uint8_t taken;                      /* strokes on this hole so far, penalties in */
	uint8_t holed;                      /* holed out or given on this hole */
	char name[RULES_NAME_MAX + 1];
	uint8_t score[COURSE_MAX_HOLES];    /* strokes per hole, 0 not played yet */
} RoundPlayer;

typedef struct {
	char course[RULES_COURSE_MAX + 1];  /* the course database's name */
	uint32_t seed;                      /* the wind for each hole comes from it */
	uint8_t players, holes;
	uint8_t hole;                       /* 0-based, the hole being played */
	uint8_t over;                       /* every hole is done */
	uint8_t turn;                       /* whose shot it is */
	uint8_t order[RULES_PLAYERS];       /* the order on this tee, the honour first */
	uint8_t pars[COURSE_MAX_HOLES];
	RoundPlayer pl[RULES_PLAYERS];
} Round;

/* A new round on a course of holes holes with these pars, players named
 * "Player 1" and so on, everyone on the first tee in number order. */
void SEC_CORE rules_init(Round *r, const char *course, uint8_t players, uint8_t holes, const uint8_t *pars, uint32_t seed);
/* Who plays next on this hole: first everyone yet to tee off, in the tee
 * order, then whoever is farthest from pin. -1 once everyone is holed. */
int SEC_CORE rules_next(const Round *r, vec2 pin);
/* Player i finished the hole in strokes. */
void SEC_CORE rules_hole_out(Round *r, uint8_t i, uint8_t strokes);
/* 1 once every player has holed out. */
int SEC_CORE rules_hole_over(const Round *r);
/* On to the next hole once this one is over: the honour goes to the lowest
 * score on it, ties keeping their order from this tee, and everyone goes
 * back to the tee. After the last hole the round is over. */
void SEC_CORE rules_advance(Round *r);

/* Strokes over holes from to to - 1 (0-based) that player i has played. */
int16_t SEC_CORE rules_strokes(const Round *r, uint8_t i, uint8_t from, uint8_t to);
/* Par over holes from to to - 1. */
int16_t SEC_CORE rules_par(const Round *r, uint8_t from, uint8_t to);
/* Holes from to to - 1 that player i has played. */
uint8_t SEC_CORE rules_played(const Round *r, uint8_t i, uint8_t from, uint8_t to);
/* Strokes against par over the holes player i has played. */
int16_t SEC_CORE rules_to_par(const Round *r, uint8_t i);

/* The saved round's record, big-endian, starting with RULES_SAVE_VERSION.
 * pack returns its length (at most RULES_SAVE_MAX); unpack returns 0, or -1
 * for a record of the wrong version, length or with values out of range. */
uint16_t SEC_CORE rules_pack(const Round *r, uint8_t *buf);
int SEC_CORE rules_unpack(Round *r, const uint8_t *buf, uint32_t len);

#endif
