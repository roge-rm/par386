/* Round scores by course, tee and span, and tournament finishes by course.
 * The platform keeps both in one record (big-endian). */
#ifndef RECORDS_H
#define RECORDS_H

#include "rules.h"

#define REC_MAX     36
#define REC_VERSION 3
#define REC_ITEM    (RULES_COURSE_MAX + 1 + 1 + 1 + 2 + 2 + RULES_NAME_MAX + 1 + 1)
#define EVENT_MAX   12
#define EVENT_ITEM  (RULES_COURSE_MAX + 1 + 1)
#define EVENT_TIED  0x80
#define EVENT_PLACE 0x7F
#define REC_SAVE_MAX (3 + REC_MAX * REC_ITEM + 1 + EVENT_MAX * EVENT_ITEM)

typedef struct {
	char course[RULES_COURSE_MAX + 1];  /* the course database's name */
	uint8_t tee, holes, span;
	uint16_t strokes;
	int16_t to_par;
	char name[RULES_NAME_MAX + 1];
} CourseRecord;

typedef struct {
	char course[RULES_COURSE_MAX + 1];
	uint8_t finish;                      /* place with EVENT_TIED for a tie */
} EventRecord;

typedef struct {
	uint8_t n, events;
	CourseRecord rec[REC_MAX];
	EventRecord event[EVENT_MAX];
} Records;

void SEC_CORE rec_init(Records *rs);
/* The record for a course, tee and span (SPAN_*), or NULL. */
const CourseRecord *SEC_CORE rec_find(const Records *rs, const char *course, uint8_t tee, uint8_t span);
/* Puts a finished round in, played from tee: each person who played every
 * hole with fewer strokes than the record (or the first time) sets it.
 * Returns how many records were set. Records are kept oldest first; with
 * every slot taken a new course replaces the oldest. */
uint8_t SEC_CORE rec_round(Records *rs, const Round *r, uint8_t tee);
/* The event name for a course, with or without the database prefix. */
const char *SEC_MENU rec_event_name(const char *course);
/* The best finish (place and tie flag), or 0 if no event has been played. */
uint8_t SEC_RENDER rec_best_finish(const Records *rs, const char *course);
/* Stores a better finish by a human after both rounds. */
uint8_t SEC_RENDER rec_event(Records *rs, const Round *r);
uint16_t SEC_CORE rec_pack(const Records *rs, uint8_t *buf);
/* 0, or -1 for a record of another version or length, or bad values (rs is then empty). */
int SEC_CORE rec_unpack(Records *rs, const uint8_t *buf, uint32_t len);

#endif
