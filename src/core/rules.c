#include "rules.h"
#include "ball.h"

static void SEC_CORE reset_hole(Round *r)
{
	uint8_t i;
	for (i = 0; i < RULES_PLAYERS; i++) {
		RoundPlayer *p = &r->pl[i];
		p->ball.x = p->ball.y = 0;
		p->lie = LIE_TEE;
		p->taken = 0;
		p->holed = 0;
	}
	r->turn = r->order[0];
}

/* "Player 1" and so on. */
static void SEC_CORE default_name(char *n, uint8_t i)
{
	n[0] = 'P'; n[1] = 'l'; n[2] = 'a'; n[3] = 'y'; n[4] = 'e'; n[5] = 'r'; n[6] = ' ';
	n[7] = (char)('1' + i);
	n[8] = 0;
}

void rules_init(Round *r, const char *course, uint8_t players, uint8_t holes, const uint8_t *pars, uint32_t seed)
{
	uint8_t *b = (uint8_t *)r, i;
	uint16_t k;

	for (k = 0; k < sizeof(Round); k++) b[k] = 0;
	for (i = 0; i < RULES_COURSE_MAX && course[i]; i++) r->course[i] = course[i];
	if (players < 1) players = 1;
	if (players > RULES_PLAYERS) players = RULES_PLAYERS;
	if (holes > COURSE_MAX_HOLES) holes = COURSE_MAX_HOLES;
	r->players = players;
	r->holes = holes;
	r->seed = seed;
	for (i = 0; i < holes; i++) r->pars[i] = pars[i];
	for (i = 0; i < RULES_PLAYERS; i++) {
		default_name(r->pl[i].name, i);
		r->order[i] = i;
	}
	reset_hole(r);
}

int rules_next(const Round *r, vec2 pin)
{
	int best = -1;
	fixed far = 0;
	uint8_t k;

	/* tee shots in the order on the tee */
	for (k = 0; k < r->players; k++) {
		const RoundPlayer *p = &r->pl[r->order[k]];
		if (!p->holed && p->taken == 0) return r->order[k];
	}
	/* then the farthest from the hole; a tie goes to the earlier on the tee */
	for (k = 0; k < r->players; k++) {
		const RoundPlayer *p = &r->pl[r->order[k]];
		fixed d;
		if (p->holed) continue;
		d = vec2_len(vec2_sub(p->ball, pin));
		if (best < 0 || d > far) {
			best = r->order[k];
			far = d;
		}
	}
	return best;
}

void rules_hole_out(Round *r, uint8_t i, uint8_t strokes)
{
	RoundPlayer *p = &r->pl[i];
	if (strokes < 1) strokes = 1;
	p->holed = 1;
	p->taken = strokes;
	if (r->hole < COURSE_MAX_HOLES) p->score[r->hole] = strokes;
}

int rules_hole_over(const Round *r)
{
	uint8_t i;
	for (i = 0; i < r->players; i++)
		if (!r->pl[i].holed) return 0;
	return 1;
}

void rules_advance(Round *r)
{
	uint8_t i, j, h = r->hole;

	/* insertion sort by the score on this hole, which keeps ties in order */
	for (i = 1; i < r->players; i++) {
		uint8_t v = r->order[i];
		for (j = i; j > 0 && r->pl[r->order[j - 1]].score[h] > r->pl[v].score[h]; j--)
			r->order[j] = r->order[j - 1];
		r->order[j] = v;
	}
	if (h + 1 >= r->holes) r->over = 1;
	else r->hole = (uint8_t)(h + 1);
	reset_hole(r);
}

int16_t rules_strokes(const Round *r, uint8_t i, uint8_t from, uint8_t to)
{
	int16_t n = 0;
	for (; from < to && from < r->holes; from++) n += r->pl[i].score[from];
	return n;
}

int16_t rules_par(const Round *r, uint8_t from, uint8_t to)
{
	int16_t n = 0;
	for (; from < to && from < r->holes; from++) n += r->pars[from];
	return n;
}

uint8_t rules_played(const Round *r, uint8_t i, uint8_t from, uint8_t to)
{
	uint8_t n = 0;
	for (; from < to && from < r->holes; from++)
		if (r->pl[i].score[from]) n++;
	return n;
}

int16_t rules_to_par(const Round *r, uint8_t i)
{
	int16_t n = 0;
	uint8_t h;
	for (h = 0; h < r->holes; h++)
		if (r->pl[i].score[h]) n += (int16_t)r->pl[i].score[h] - r->pars[h];
	return n;
}

static uint8_t *SEC_CORE put32(uint8_t *b, uint32_t v)
{
	b[0] = (uint8_t)(v >> 24);
	b[1] = (uint8_t)(v >> 16);
	b[2] = (uint8_t)(v >> 8);
	b[3] = (uint8_t)v;
	return b + 4;
}

static uint32_t SEC_CORE get32(const uint8_t *b)
{
	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

/* Copies n bytes of a name, padding with zeros. */
static uint8_t *SEC_CORE put_name(uint8_t *b, const char *s, uint8_t n)
{
	uint8_t i, end = 0;
	for (i = 0; i < n; i++) {
		if (!s[i]) end = 1;
		b[i] = end ? 0 : (uint8_t)s[i];
	}
	return b + n;
}

/* A name of n bytes: plain characters, then zeros, at least one. */
static int SEC_CORE get_name(char *s, const uint8_t *b, uint8_t n, uint8_t need)
{
	uint8_t i, end = 0;
	for (i = 0; i < n; i++) {
		if (!b[i]) end = 1;
		else if (end || b[i] < 32 || b[i] == 127) return -1;
		s[i] = (char)b[i];
	}
	if (!end || (need && !b[0])) return -1;
	return 0;
}

uint16_t rules_pack(const Round *r, uint8_t *buf)
{
	uint8_t *b = buf, i, h;

	*b++ = (uint8_t)(RULES_SAVE_VERSION >> 8);
	*b++ = (uint8_t)RULES_SAVE_VERSION;
	b = put_name(b, r->course, RULES_COURSE_MAX + 1);
	b = put32(b, r->seed);
	*b++ = r->players;
	*b++ = r->holes;
	*b++ = r->hole;
	*b++ = r->over;
	*b++ = r->turn;
	for (i = 0; i < RULES_PLAYERS; i++) *b++ = r->order[i];
	for (h = 0; h < r->holes; h++) *b++ = r->pars[h];
	for (i = 0; i < r->players; i++) {
		const RoundPlayer *p = &r->pl[i];
		b = put_name(b, p->name, RULES_NAME_MAX + 1);
		b = put32(b, (uint32_t)p->ball.x);
		b = put32(b, (uint32_t)p->ball.y);
		*b++ = p->lie;
		*b++ = p->taken;
		*b++ = p->holed;
		for (h = 0; h < r->holes; h++) *b++ = p->score[h];
	}
	return (uint16_t)(b - buf);
}

int rules_unpack(Round *r, const uint8_t *buf, uint32_t len)
{
	const uint8_t *b = buf;
	uint8_t i, h, seen = 0, players, holes;
	uint8_t *z = (uint8_t *)r;
	uint16_t k;

	if (len < 2 + 32 + 4 + 9) return -1;
	if (((uint16_t)b[0] << 8 | b[1]) != RULES_SAVE_VERSION) return -1;
	players = b[2 + 32 + 4];
	holes = b[2 + 32 + 4 + 1];
	if (players < 1 || players > RULES_PLAYERS || holes < 1 || holes > COURSE_MAX_HOLES) return -1;
	if (len != 2 + 32 + 4 + 9 + (uint32_t)holes + (uint32_t)players * (RULES_NAME_MAX + 1 + 11 + holes)) return -1;

	for (k = 0; k < sizeof(Round); k++) z[k] = 0;
	b += 2;
	if (get_name(r->course, b, RULES_COURSE_MAX + 1, 1)) return -1;
	b += RULES_COURSE_MAX + 1;
	r->seed = get32(b);
	b += 4;
	r->players = *b++;
	r->holes = *b++;
	r->hole = *b++;
	r->over = *b++;
	r->turn = *b++;
	if (r->hole >= holes || r->over > 1 || r->turn >= players) return -1;
	for (i = 0; i < RULES_PLAYERS; i++) {
		r->order[i] = *b++;
		/* each player once, in the first players places */
		if (r->order[i] >= RULES_PLAYERS || (seen & (1 << r->order[i]))) return -1;
		if (i < players && r->order[i] >= players) return -1;
		seen = (uint8_t)(seen | (1 << r->order[i]));
	}
	for (h = 0; h < holes; h++) {
		r->pars[h] = *b++;
		if (r->pars[h] < 1 || r->pars[h] > 9) return -1;
	}
	for (i = 0; i < players; i++) {
		RoundPlayer *p = &r->pl[i];
		if (get_name(p->name, b, RULES_NAME_MAX + 1, 1)) return -1;
		b += RULES_NAME_MAX + 1;
		p->ball.x = (fixed)get32(b);
		p->ball.y = (fixed)get32(b + 4);
		b += 8;
		p->lie = *b++;
		p->taken = *b++;
		p->holed = *b++;
		if (p->lie >= LIE_COUNT || p->holed > 1) return -1;
		/* a ball well off any hole */
		if (p->ball.x < -(64L << 16) || p->ball.y < -(64L << 16) ||
		    p->ball.x > ((fixed)(HOLE_MAX_WIDTH + 64) << 16) || p->ball.y > ((fixed)(HOLE_MAX_LENGTH + 64) << 16)) return -1;
		for (h = 0; h < holes; h++) p->score[h] = *b++;
	}
	/* every player holed means the hole was over and rules_advance had run */
	for (i = 0; i < players && r->pl[i].holed; i++) ;
	if (i == players && !r->over) return -1;
	if (r->pl[r->turn].holed) return -1;
	/* no strokes on holes not reached yet */
	for (i = 0; i < players; i++)
		for (h = (uint8_t)(r->hole + 1); h < holes; h++)
			if (r->pl[i].score[h]) return -1;
	/* the players not in the round as rules_init leaves them */
	for (; i < RULES_PLAYERS; i++) {
		default_name(r->pl[i].name, i);
		r->pl[i].lie = LIE_TEE;
	}
	return 0;
}
