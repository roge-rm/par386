/* Course records: the best score on each course from each tee by a person
 * who played every hole, in any mode but practice. Kept by the platform
 * as one record (big-endian, starting with REC_VERSION). No platform
 * headers. */
#ifndef RECORDS_H
#define RECORDS_H

#include "rules.h"

#define REC_MAX     12
#define REC_VERSION 1
#define REC_ITEM    (RULES_COURSE_MAX + 1 + 1 + 1 + 2 + 2 + RULES_NAME_MAX + 1)
#define REC_SAVE_MAX (2 + 1 + REC_MAX * REC_ITEM)

typedef struct {
	char course[RULES_COURSE_MAX + 1];  /* the course database's name */
	uint8_t tee, holes;
	uint16_t strokes;
	int16_t to_par;
	char name[RULES_NAME_MAX + 1];
} CourseRecord;

typedef struct {
	uint8_t n;
	CourseRecord rec[REC_MAX];
} Records;

void SEC_CORE rec_init(Records *rs);
/* The record for a course and tee, or NULL. */
const CourseRecord *SEC_CORE rec_find(const Records *rs, const char *course, uint8_t tee);
/* Puts a finished round in, played from tee: each person who played every
 * hole with fewer strokes than the record (or the first time) sets it.
 * Returns how many records were set. Records are kept oldest first; with
 * every slot taken a new course replaces the oldest. */
uint8_t SEC_CORE rec_round(Records *rs, const Round *r, uint8_t tee);
uint16_t SEC_CORE rec_pack(const Records *rs, uint8_t *buf);
/* 0, or -1 for a record of another version or length, or bad values (rs is then empty). */
int SEC_CORE rec_unpack(Records *rs, const uint8_t *buf, uint32_t len);

#endif
