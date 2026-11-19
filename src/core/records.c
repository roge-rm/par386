#include "records.h"

void rec_init(Records *rs)
{
	uint8_t *b = (uint8_t *)rs;
	uint16_t k;
	for (k = 0; k < sizeof(Records); k++) b[k] = 0;
}

static uint8_t SEC_CORE same(const char *a, const char *b)
{
	uint8_t i;
	for (i = 0; i <= RULES_COURSE_MAX; i++) {
		if (a[i] != b[i]) return 0;
		if (!a[i]) return 1;
	}
	return 1;
}

static void SEC_CORE copy(char *d, const char *s, uint8_t n)
{
	uint8_t i;
	for (i = 0; i < n - 1 && s[i]; i++) d[i] = s[i];
	for (; i < n; i++) d[i] = 0;
}

const CourseRecord *rec_find(const Records *rs, const char *course, uint8_t tee)
{
	uint8_t i;
	for (i = 0; i < rs->n; i++)
		if (rs->rec[i].tee == tee && same(rs->rec[i].course, course)) return &rs->rec[i];
	return 0;
}

/* Moves record i to the last slot (the newest), the rest up one; returns its slot. */
static uint8_t SEC_CORE bump(Records *rs, uint8_t i)
{
	CourseRecord t = rs->rec[i];
	for (; i + 1 < rs->n; i++) rs->rec[i] = rs->rec[i + 1];
	rs->rec[i] = t;
	return i;
}

uint8_t rec_round(Records *rs, const Round *r, uint8_t tee)
{
	uint8_t i, set = 0;

	if (MODE_PRACTICE(r->mode)) return 0;
	for (i = 0; i < r->players; i++) {
		const RoundPlayer *p = &r->pl[i];
		CourseRecord *c = (CourseRecord *)rec_find(rs, r->course, tee);
		int16_t n;
		if (p->cpu || rules_played(r, i, 0, r->holes) != r->holes) continue;
		n = rules_strokes(r, i, 0, r->holes);
		if (c && c->holes == r->holes && c->strokes <= (uint16_t)n) continue;
		if (!c) {
			/* the oldest goes when the slots are full */
			if (rs->n < REC_MAX) rs->n++;
			else bump(rs, 0);
			c = &rs->rec[rs->n - 1];
		} else {
			c = &rs->rec[bump(rs, (uint8_t)(c - rs->rec))];
		}
		copy(c->course, r->course, RULES_COURSE_MAX + 1);
		copy(c->name, p->name, RULES_NAME_MAX + 1);
		c->tee = tee;
		c->holes = r->holes;
		c->strokes = (uint16_t)n;
		c->to_par = rules_to_par(r, i);
		set++;
	}
	return set;
}

uint16_t rec_pack(const Records *rs, uint8_t *buf)
{
	uint8_t *b = buf, i, k;

	*b++ = 0;
	*b++ = REC_VERSION;
	*b++ = rs->n;
	for (i = 0; i < rs->n; i++) {
		const CourseRecord *c = &rs->rec[i];
		for (k = 0; k <= RULES_COURSE_MAX; k++) *b++ = (uint8_t)c->course[k];
		*b++ = c->tee;
		*b++ = c->holes;
		*b++ = (uint8_t)(c->strokes >> 8);
		*b++ = (uint8_t)c->strokes;
		*b++ = (uint8_t)((uint16_t)c->to_par >> 8);
		*b++ = (uint8_t)c->to_par;
		for (k = 0; k <= RULES_NAME_MAX; k++) *b++ = (uint8_t)c->name[k];
	}
	return (uint16_t)(b - buf);
}

int rec_unpack(Records *rs, const uint8_t *buf, uint32_t len)
{
	const uint8_t *b = buf + 3;
	uint8_t i, k;

	rec_init(rs);
	if (len < 3 || buf[0] || buf[1] != REC_VERSION || buf[2] > REC_MAX || len != 3 + (uint32_t)buf[2] * REC_ITEM) return -1;
	for (i = 0; i < buf[2]; i++) {
		CourseRecord *c = &rs->rec[i];
		for (k = 0; k <= RULES_COURSE_MAX; k++) c->course[k] = (char)*b++;
		c->tee = *b++;
		c->holes = *b++;
		c->strokes = (uint16_t)((uint16_t)b[0] << 8 | b[1]);
		c->to_par = (int16_t)((uint16_t)b[2] << 8 | b[3]);
		b += 4;
		for (k = 0; k <= RULES_NAME_MAX; k++) c->name[k] = (char)*b++;
		/* names end in a zero */
		if (c->course[RULES_COURSE_MAX] || c->name[RULES_NAME_MAX] || !c->course[0] || !c->holes || c->holes > COURSE_MAX_HOLES) {
			rec_init(rs);
			return -1;
		}
	}
	rs->n = buf[2];
	return 0;
}
