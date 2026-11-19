/* The round: up to 4 players taking turns, whose turn it is, the honour on
 * the tee, each hole's strokes and the scorecard totals, the game mode's
 * scoring (match play, skins, best ball, the tournament field) and the
 * saved round's record. No platform headers.
 *
 * A player's ball, lie and strokes so far on the hole live here; play.c
 * plays the shots and writes the results back. */
#ifndef RULES_H
#define RULES_H

#include "hole.h"

#define RULES_PLAYERS     4
#define RULES_NAME_MAX    8      /* fits the status strip with the lie */
#define RULES_COURSE_MAX  31     /* the course database's name */

/* Game modes. Match play is 2 players, best ball 4 (players 1 and 2
 * against 3 and 4, each side's better score on a hole against the other's),
 * skins 2 to 4. A tournament is stroke play against a field of computer
 * golfers played out as scores. The range and the green are practice, one
 * player, no score. */
enum { MODE_STROKE, MODE_MATCH, MODE_SKINS, MODE_BEST, MODE_TOURN, MODE_RANGE, MODE_GREEN, MODE_COUNT };
#define MODE_PRACTICE(m) ((m) >= MODE_RANGE)

/* Who plays a slot: a person, or a computer golfer of a skill level. */
enum { CPU_HUMAN, CPU_EASY, CPU_FAIR, CPU_GOOD, CPU_PRO, CPU_LEVELS };

/* The tournament's computer field. */
#define RULES_FIELD 8

#define RULES_SAVE_VERSION 2
/* The most a saved round's record takes. */
#define RULES_SAVE_MAX    (2 + 32 + 4 + 9 + COURSE_MAX_HOLES + 3 + RULES_PLAYERS * (RULES_NAME_MAX + 1 + 12 + COURSE_MAX_HOLES))

typedef struct {
	vec2 ball;                          /* where it lies, once it's off the tee */
	uint8_t lie;                        /* LIE_* (ball.h) */
	uint8_t taken;                      /* strokes on this hole so far, penalties in */
	uint8_t holed;                      /* holed out or given on this hole */
	uint8_t cpu;                        /* CPU_*: a person or a computer golfer */
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
	uint8_t mode;                       /* MODE_* */
	uint16_t balls;                     /* practice: balls played so far */
	RoundPlayer pl[RULES_PLAYERS];
} Round;

/* A line of the tournament leaderboard. */
typedef struct {
	int16_t to_par;
	uint8_t who;        /* a player, or RULES_PLAYERS + the field golfer */
	uint8_t pos;        /* 1 for the lead; tied lines share it */
	uint8_t tied;
} BoardLine;

/* A new round on a course of holes holes with these pars, players named
 * "Player 1" and so on, everyone on the first tee in number order. */
void SEC_CORE rules_init(Round *r, const char *course, uint8_t players, uint8_t holes, const uint8_t *pars, uint32_t seed);
/* Sets the mode and who plays each slot (cpu has players entries, NULL
 * for all people). Computer golfers get their own names. Practice starts
 * on hole h (0-based). */
void SEC_CORE rules_mode(Round *r, uint8_t mode, const uint8_t *cpu, uint8_t h);
/* Whether the mode can be played by this many players. */
uint8_t SEC_CORE rules_mode_ok(uint8_t mode, uint8_t players);

/* Who plays next on this hole: first everyone yet to tee off, in the tee
 * order, then whoever is farthest from pin. -1 once everyone is holed. */
int SEC_CORE rules_next(const Round *r, vec2 pin);
/* Player i finished the hole in strokes. */
void SEC_CORE rules_hole_out(Round *r, uint8_t i, uint8_t strokes);
/* 1 once every player has holed out. */
int SEC_CORE rules_hole_over(const Round *r);
/* On to the next hole once this one is over: the honour goes to the lowest
 * score on it, ties keeping their order from this tee, and everyone goes
 * back to the tee. After the last hole the round is over, and a match is
 * over once one side is up by more holes than are left. */
void SEC_CORE rules_advance(Round *r);

/* Strokes over holes from to to - 1 (0-based) that player i has played. */
int16_t SEC_CORE rules_strokes(const Round *r, uint8_t i, uint8_t from, uint8_t to);
/* Par over holes from to to - 1. */
int16_t SEC_CORE rules_par(const Round *r, uint8_t from, uint8_t to);
/* Holes from to to - 1 that player i has played. */
uint8_t SEC_CORE rules_played(const Round *r, uint8_t i, uint8_t from, uint8_t to);
/* Strokes against par over the holes player i has played. */
int16_t SEC_CORE rules_to_par(const Round *r, uint8_t i);

/* Holes every player has finished. */
uint8_t SEC_CORE rules_done(const Round *r);

/* Match play and best ball: the side player i is on, 0 or 1. */
uint8_t SEC_CORE rules_side(const Round *r, uint8_t i);
/* Which side won hole h: 0 or 1, 2 for halved, 3 if it isn't finished. */
uint8_t SEC_CORE rules_hole_win(const Round *r, uint8_t h);
/* Holes side 0 is up over the finished holes (below 0 down), and in left
 * how many holes are still to play. */
int8_t SEC_CORE rules_match(const Round *r, uint8_t *left);

/* Skins: each player's skins won over the finished holes, and the skins
 * carried over to the next hole (ties carry). */
void SEC_CORE rules_skins(const Round *r, uint8_t *won, uint8_t *carry);

/* Tournament: field golfer g's strokes on hole h, the same for the same
 * round seed, and their name. */
uint8_t SEC_CORE rules_field_score(const Round *r, uint8_t g, uint8_t h);
const char *SEC_CORE rules_field_name(uint8_t g);
/* The leaderboard over the finished holes: the players and the field,
 * best first, in lines (RULES_PLAYERS + RULES_FIELD of them at most).
 * Returns how many. */
uint8_t SEC_CORE rules_board(const Round *r, BoardLine *lines);

/* A shot's own random seed from where the round is: the round's seed,
 * the hole, the player and the strokes so far. */
uint32_t SEC_CORE rules_shot_seed(const Round *r, uint8_t i);

/* The saved round's record, big-endian, starting with RULES_SAVE_VERSION.
 * pack returns its length (at most RULES_SAVE_MAX); unpack returns 0, or -1
 * for a record of the wrong version, length or with values out of range.
 * A version 1 record (before the modes) loads as stroke play with every
 * player a person. */
uint16_t SEC_CORE rules_pack(const Round *r, uint8_t *buf);
int SEC_CORE rules_unpack(Round *r, const uint8_t *buf, uint32_t len);

#endif
