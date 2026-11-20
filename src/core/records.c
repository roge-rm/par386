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

const CourseRecord *rec_find(const Records *rs, const char *course, uint8_t tee, uint8_t span)
{
	uint8_t i;
	for (i = 0; i < rs->n; i++)
		if (rs->rec[i].tee == tee && rs->rec[i].span == span && same(rs->rec[i].course, course)) return &rs->rec[i];
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
		CourseRecord *c = (CourseRecord *)rec_find(rs, r->course, tee, r->span);
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
		c->span = r->span;
		c->strokes = (uint16_t)n;
		c->to_par = rules_to_par(r, i);
		set++;
	}
	return set;
}

static const char prefix[] = "Par386 ";
static const char *const event_names[][2] = {
	{ "Auld Kirk", "Kirk Open" },
	{ "Carrig Head", "Carrig Cup" },
	{ "Sakura Bay", "Sakura Classic" },
	{ "Banksia Heath", "Banksia Open" },
	{ "Crocodile Bend", "Crocodile Cup" },
	{ "Cascade Springs", "Cascade Open" },
	{ "Bracken Mere", "Bracken Trophy" },
	{ "Saffron Dunes", "Saffron Open" },
	{ "Falesia Grove", "Falesia Cup" }
};

const char *rec_event_name(const char *course)
{
	uint8_t i;
	for (i = 0; prefix[i] && course[i] == prefix[i]; i++) {}
	if (!prefix[i]) course += i;
	for (i = 0; i < sizeof(event_names) / sizeof(event_names[0]); i++)
		if (same(course, event_names[i][0])) return event_names[i][1];
	return "Club Open";
}

static uint8_t SEC_RENDER event_index(const Records *rs, const char *course)
{
	uint8_t i;
	for (i = 0; i < rs->events; i++)
		if (same(rs->event[i].course, course)) break;
	return i;
}

uint8_t rec_best_finish(const Records *rs, const char *course)
{
	uint8_t i = event_index(rs, course);
	return i < rs->events ? rs->event[i].finish : 0;
}

uint8_t rec_event(Records *rs, const Round *r)
{
	BoardLine b[RULES_PLAYERS + RULES_FIELD];
	uint8_t i, j, n, finish;
	EventRecord *e;

	if (r->mode != MODE_TOURN || r->tour_stage != TOUR_SECOND || !r->over || rules_done(r) != r->holes)
		return 0;
	n = rules_board(r, b);
	for (i = 0; i < n; i++)
		if (b[i].who < r->players && !r->pl[b[i].who].cpu) break;
	if (i == n) return 0;
	finish = (uint8_t)(b[i].pos | (b[i].tied ? EVENT_TIED : 0));
	j = event_index(rs, r->course);
	if (j < rs->events) {
		uint8_t old = rs->event[j].finish;
		if ((old & EVENT_PLACE) < b[i].pos || ((old & EVENT_PLACE) == b[i].pos && old <= finish)) return 0;
	} else if (rs->events < EVENT_MAX) {
		rs->events++;
	} else {
		for (j = 0; j + 1 < rs->events; j++) rs->event[j] = rs->event[j + 1];
	}
	e = &rs->event[j];
	copy(e->course, r->course, RULES_COURSE_MAX + 1);
	e->finish = finish;
	return 1;
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
		*b++ = c->span;
	}
	*b++ = rs->events;
	for (i = 0; i < rs->events; i++) {
		const EventRecord *e = &rs->event[i];
		for (k = 0; k <= RULES_COURSE_MAX; k++) *b++ = (uint8_t)e->course[k];
		*b++ = e->finish;
	}
	return (uint16_t)(b - buf);
}

int rec_unpack(Records *rs, const uint8_t *buf, uint32_t len)
{
	const uint8_t *b;
	uint32_t rounds_len;
	uint8_t i, k, ver, events = 0;

	rec_init(rs);
	if (len < 3 || buf[0] || buf[1] < 1 || buf[1] > REC_VERSION || buf[2] > REC_MAX) return -1;
	ver = buf[1];
	rounds_len = 3 + (uint32_t)buf[2] * (ver == 1 ? REC_ITEM - 1 : REC_ITEM);
	if (ver >= 3) {
		if (len <= rounds_len) return -1;
		events = buf[rounds_len];
		if (events > EVENT_MAX || len != rounds_len + 1 + (uint32_t)events * EVENT_ITEM) return -1;
	} else if (len != rounds_len) return -1;
	b = buf + 3;
	for (i = 0; i < buf[2]; i++) {
		CourseRecord *c = &rs->rec[i];
		for (k = 0; k <= RULES_COURSE_MAX; k++) c->course[k] = (char)*b++;
		c->tee = *b++;
		c->holes = *b++;
		c->strokes = (uint16_t)((uint16_t)b[0] << 8 | b[1]);
		c->to_par = (int16_t)((uint16_t)b[2] << 8 | b[3]);
		b += 4;
		for (k = 0; k <= RULES_NAME_MAX; k++) c->name[k] = (char)*b++;
		if (ver >= 2) c->span = *b++;
		/* names end in a zero */
		if (c->course[RULES_COURSE_MAX] || c->name[RULES_NAME_MAX] || !c->course[0] || !c->holes ||
		    c->holes > COURSE_MAX_HOLES || c->span >= SPAN_COUNT || (c->span && c->holes != 9)) {
			rec_init(rs);
			return -1;
		}
	}
	if (ver >= 3) b++;
	for (i = 0; i < events; i++) {
		EventRecord *e = &rs->event[i];
		uint8_t place;
		for (k = 0; k <= RULES_COURSE_MAX; k++) e->course[k] = (char)*b++;
		e->finish = *b++;
		place = e->finish & EVENT_PLACE;
		if (e->course[RULES_COURSE_MAX] || !e->course[0] || !place || place > RULES_PLAYERS + RULES_FIELD) {
			rec_init(rs);
			return -1;
		}
	}
	rs->n = buf[2];
	rs->events = events;
	return 0;
}
