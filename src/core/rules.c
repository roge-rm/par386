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
		p->stats.green_hit = 0;
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
	r->stats_available = 1;
	for (i = 0; i < holes; i++) r->pars[i] = pars[i];
	for (i = 0; i < RULES_PLAYERS; i++) {
		default_name(r->pl[i].name, i);
		r->order[i] = i;
	}
	reset_hole(r);
}

void rules_span(Round *r, uint8_t span)
{
	uint8_t h;

	if (r->holes != 18 || r->mode >= MODE_TOURN || (span != SPAN_FRONT && span != SPAN_BACK)) return;
	r->span = span;
	r->holes = 9;
	if (span == SPAN_BACK)
		for (h = 0; h < 9; h++) r->pars[h] = r->pars[h + 9];
	for (h = 9; h < 18; h++) r->pars[h] = 0;
}

int rules_course_matches(const Round *r, const Course *c)
{
	uint8_t h, first = (uint8_t)(rules_hole_number(r, 0) - 1);

	if (c->hole_count != (r->span == SPAN_FULL ? r->holes : 18)) return 0;
	for (h = 0; h < r->holes; h++)
		if (r->pars[h] != c->par[h + first]) return 0;
	return 1;
}

/* Names for computer golfers in each slot. */
static const char cpu_names[RULES_PLAYERS][RULES_NAME_MAX + 1] = { "Hamish", "Angus", "Moira", "Fergus" };
static const char default_profile_name[] = PLAYER_DEFAULT_NAME;

void rules_mode(Round *r, uint8_t mode, const uint8_t *cpu, uint8_t h)
{
	uint8_t i, k;

	r->mode = mode < MODE_COUNT ? mode : MODE_STROKE;
	r->tour_stage = r->mode == MODE_TOURN ? TOUR_FIRST : TOUR_SINGLE;
	for (i = 0; i < r->players; i++) {
		r->pl[i].cpu = cpu && cpu[i] < CPU_LEVELS ? cpu[i] : CPU_HUMAN;
		if (r->pl[i].cpu)
			for (k = 0; k <= RULES_NAME_MAX; k++) r->pl[i].name[k] = cpu_names[i][k];
	}
	if (MODE_PRACTICE(r->mode) && h < r->holes) {
		r->hole = h;
		reset_hole(r);
	}
}

uint8_t rules_profile(Round *r, const PlayerProfile *profile)
{
	uint8_t i, k;

	for (i = 0; i < r->players; i++) {
		if (r->pl[i].cpu) continue;
		for (k = 0; k < sizeof(default_profile_name) && profile->name[k] == default_profile_name[k]; k++) {}
		if (!profile->name[0] || k == sizeof(default_profile_name)) {
			default_name(r->pl[i].name, i);
			return i;
		}
		for (k = 0; k < RULES_NAME_MAX && k < PLAYER_NAME_MAX && profile->name[k]; k++)
			r->pl[i].name[k] = profile->name[k];
		for (; k <= RULES_NAME_MAX; k++) r->pl[i].name[k] = 0;
		return i;
	}
	return RULES_PLAYERS;
}

uint8_t rules_mode_ok(uint8_t mode, uint8_t players)
{
	switch (mode) {
	case MODE_MATCH: return players == 2;
	case MODE_BEST: return players == 4;
	case MODE_SKINS: return players >= 2 && players <= RULES_PLAYERS;
	case MODE_RANGE: case MODE_GREEN: return players == 1;
	}
	return mode < MODE_COUNT && players >= 1 && players <= RULES_PLAYERS;
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

void rules_stat_shot(Round *r, uint8_t i, uint8_t from, uint8_t to, uint8_t result, uint8_t gimme)
{
	RoundPlayer *p = &r->pl[i];
	RoundStats *s = &p->stats;
	uint8_t par = r->pars[r->hole];

	if (!r->stats_available || MODE_PRACTICE(r->mode)) return;
	if (!p->taken && from == LIE_TEE && (par == 4 || par == 5)) {
		s->drives++;
		if (result == SHOT_OK && to == LIE_FAIRWAY) s->fairways++;
	}
	if (!s->green_hit && p->taken + 3 <= par &&
	    (result == SHOT_HOLED || (result == SHOT_OK && to == LIE_GREEN))) {
		s->green_hit = 1;
		s->greens++;
	}
	if (from == LIE_GREEN) s->putts++;
	if (gimme) s->putts++;
	if (result == SHOT_WATER || result == SHOT_OOB) s->penalties++;
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
	if (h + 1 >= r->holes) {
		if (r->tour_stage == TOUR_FIRST) r->tour_stage = TOUR_BREAK;
		else r->over = 1;
	}
	else r->hole = (uint8_t)(h + 1);
	if (r->mode == MODE_MATCH || r->mode == MODE_BEST) {
		/* the match is over once one side can't be caught */
		uint8_t left;
		int8_t up = rules_match(r, &left);
		if (up > (int8_t)left || -up > (int8_t)left) r->over = 1;
	}
	reset_hole(r);
}

uint8_t rules_done(const Round *r)
{
	uint8_t h, i;
	for (h = 0; h < r->holes; h++)
		for (i = 0; i < r->players; i++)
			if (!r->pl[i].score[h]) return h;
	return h;
}

uint8_t rules_side(const Round *r, uint8_t i)
{
	return (uint8_t)(r->mode == MODE_BEST ? i >> 1 : i & 1);
}

uint8_t rules_hole_win(const Round *r, uint8_t h)
{
	uint8_t best[2] = { 255, 255 }, i;

	if (h >= r->holes) return 3;
	for (i = 0; i < r->players; i++) {
		uint8_t v = r->pl[i].score[h], sd = rules_side(r, i);
		if (!v) return 3;
		if (v < best[sd]) best[sd] = v;
	}
	return (uint8_t)(best[0] < best[1] ? 0 : best[1] < best[0] ? 1 : 2);
}

int8_t rules_match(const Round *r, uint8_t *left)
{
	uint8_t h, n = rules_done(r);
	int8_t up = 0;
	for (h = 0; h < n; h++) {
		uint8_t w = rules_hole_win(r, h);
		if (w == 0) up++;
		else if (w == 1) up--;
	}
	*left = (uint8_t)(r->holes - n);
	return up;
}

void rules_skins(const Round *r, uint8_t *won, uint8_t *carry)
{
	uint8_t h, i, n = rules_done(r), c = 0;

	for (i = 0; i < RULES_PLAYERS; i++) won[i] = 0;
	for (h = 0; h < n; h++) {
		uint8_t lo = 255, who = 0, ties = 0;
		for (i = 0; i < r->players; i++) {
			uint8_t v = r->pl[i].score[h];
			if (v < lo) { lo = v; who = i; ties = 0; }
			else if (v == lo) ties++;
		}
		c++;
		if (!ties) {
			won[who] = (uint8_t)(won[who] + c);
			c = 0;
		}
	}
	*carry = c;
}

/* A 32 bit hash for the random draws: field scores and computer golfers' shots. */
static uint32_t SEC_CORE mix(uint32_t r)
{
	r ^= r >> 15;
	r *= 2246822519UL;
	r ^= r >> 13;
	r *= 3266489917UL;
	r ^= r >> 16;
	return r;
}

/* The field's strength: chances out of 256 of a score at or under par - 2,
 * - 1, par, + 1, + 2, and + 3 for the rest, by level, strongest first. */
static const uint8_t field_odds[4][5] = {
	{ 3, 63, 213, 251, 255 },
	{ 2, 50, 200, 248, 255 },
	{ 2, 42, 188, 246, 254 },
	{ 1, 33, 173, 241, 253 },
};

static const char field_names[RULES_FIELD][9] = {
	"MacLeod", "Tanaka", "Kruger", "O'Brien", "Murray", "Berg", "Okafor", "Ferreira"
};

void rules_tour_course(Round *r, const Course *c)
{
	uint8_t h;
	int16_t greens = c->green ? (int16_t)(c->green - GREEN_USUAL) * 3 : 0;

	r->tour_wind = c->wind;
	for (h = 0; h < r->holes; h++) {
		int16_t usual = r->pars[h] <= 3 ? 160 : 250 + (r->pars[h] - 3) * 130;
		int16_t d = (int16_t)(((int32_t)c->yards[h] - usual) / 5) + greens;
		r->tour_difficulty[h] = (int8_t)(d < -24 ? -24 : d > 32 ? 32 : d);
	}
}

void rules_tour_next(Round *r)
{
	uint8_t i, h;

	if (r->tour_stage != TOUR_BREAK) return;
	for (i = 0; i < r->players; i++) {
		RoundStats *s = &r->pl[i].stats;
		r->tour_first[i] = (uint16_t)rules_strokes(r, i, 0, r->holes);
		for (h = 0; h < r->holes; h++) r->pl[i].score[h] = 0;
		s->fairways = s->drives = s->greens = 0;
		s->putts = s->penalties = 0;
		r->order[i] = i;
	}
	r->stats_available = 1;
	r->hole = 0;
	r->tour_stage = TOUR_SECOND;
	reset_hole(r);
}

uint32_t rules_round_seed(const Round *r)
{
	return r->tour_stage == TOUR_SECOND ? r->seed ^ 0x9e3779b9UL : r->seed;
}

int16_t rules_tour_to_par(const Round *r, uint8_t i)
{
	int16_t score = rules_to_par(r, i);
	if (r->tour_stage == TOUR_SECOND)
		score += (int16_t)r->tour_first[i] - rules_par(r, 0, r->holes);
	return score;
}

static int16_t SEC_CORE field_pressure(const Round *r, uint8_t h, uint32_t seed)
{
	uint8_t mph;
	angle dir;

	if (r->tour_stage == TOUR_SINGLE) return 0;
	ball_wind(seed, (uint8_t)(h + 1), r->tour_wind, &mph, &dir);
	return (int16_t)(r->tour_difficulty[h] + 2 * ((int16_t)mph - 10));
}

static uint8_t SEC_CORE field_score(const Round *r, uint8_t g, uint8_t h, uint32_t seed, int16_t pressure)
{
	int16_t v = (int16_t)(mix(seed * 69069UL + (uint32_t)g * 2654435761UL + (uint32_t)h * 40503UL + 1UL) >> 24);
	uint8_t k, par = r->pars[h];

	v += pressure;
	for (k = 0; k < 5 && v >= field_odds[(g >> 1) & 3][k]; k++) {}
	/* no eagles on a par 3: a hole in one is too rare to give out */
	if (k == 0 && par <= 3) k = 1;
	return (uint8_t)(par + k - 2);
}

uint8_t rules_field_score(const Round *r, uint8_t g, uint8_t h)
{
	uint32_t seed = rules_round_seed(r);
	return h < r->holes ? field_score(r, g, h, seed, field_pressure(r, h, seed)) : 0;
}

const char *rules_field_name(uint8_t g)
{
	return field_names[g < RULES_FIELD ? g : 0];
}

static void SEC_CORE field_totals(const Round *r, int16_t *totals, uint8_t done, uint32_t seed)
{
	uint8_t h, g;

	for (h = 0; h < done; h++) {
		int16_t pressure = field_pressure(r, h, seed);
		for (g = 0; g < RULES_FIELD; g++)
			totals[g] += (int16_t)field_score(r, g, h, seed, pressure) - r->pars[h];
	}
}

uint8_t rules_board(const Round *r, BoardLine *lines)
{
	uint8_t n = 0, i, j, done = rules_done(r);
	int16_t field[RULES_FIELD] = {0};
	int16_t par = rules_par(r, 0, done);

	field_totals(r, field, done, rules_round_seed(r));
	if (r->tour_stage == TOUR_SECOND) field_totals(r, field, r->holes, r->seed);
	for (i = 0; i < r->players + RULES_FIELD; i++) {
		BoardLine b;
		b.who = i < r->players ? i : (uint8_t)(RULES_PLAYERS + i - r->players);
		if (i < r->players) {
			b.to_par = rules_strokes(r, i, 0, done) - par;
			if (r->tour_stage == TOUR_SECOND)
				b.to_par += (int16_t)r->tour_first[i] - rules_par(r, 0, r->holes);
		} else b.to_par = field[b.who - RULES_PLAYERS];
		/* insertion: ties keep the order they came in, players first */
		for (j = n; j > 0 && lines[j - 1].to_par > b.to_par; j--) lines[j] = lines[j - 1];
		lines[j] = b;
		n++;
	}
	for (i = 0; i < n; i++) {
		lines[i].pos = (uint8_t)(i && lines[i].to_par == lines[i - 1].to_par ? lines[i - 1].pos : i + 1);
		lines[i].tied = (uint8_t)((i && lines[i].to_par == lines[i - 1].to_par) || (i + 1 < n && lines[i + 1].to_par == lines[i].to_par));
	}
	return n;
}

uint32_t rules_shot_seed(const Round *r, uint8_t i)
{
	return mix(rules_round_seed(r) * 1103515245UL + (uint32_t)r->hole * 2654435761UL + (uint32_t)i * 40503UL +
	           (uint32_t)r->pl[i < RULES_PLAYERS ? i : 0].taken * 97UL + (uint32_t)r->balls * 7919UL + 12345UL);
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
	*b++ = r->mode;
	*b++ = (uint8_t)(r->balls >> 8);
	*b++ = (uint8_t)r->balls;
	*b++ = r->tee;
	for (i = 0; i < r->players; i++) {
		const RoundPlayer *p = &r->pl[i];
		b = put_name(b, p->name, RULES_NAME_MAX + 1);
		b = put32(b, (uint32_t)p->ball.x);
		b = put32(b, (uint32_t)p->ball.y);
		*b++ = p->lie;
		*b++ = p->taken;
		*b++ = p->holed;
		*b++ = p->cpu;
		for (h = 0; h < r->holes; h++) *b++ = p->score[h];
	}
	*b++ = (uint8_t)(r->holeout | (r->repeat << 1));
	if (r->mode == MODE_TOURN) {
		*b++ = r->tour_stage;
		*b++ = r->tour_wind;
		for (h = 0; h < r->holes; h++) *b++ = (uint8_t)(r->tour_difficulty[h] + 24);
		for (i = 0; i < r->players; i++) {
			*b++ = (uint8_t)(r->tour_first[i] >> 8);
			*b++ = (uint8_t)r->tour_first[i];
		}
	}
	*b++ = r->stats_available;
	for (i = 0; i < r->players; i++) {
		const RoundStats *s = &r->pl[i].stats;
		*b++ = s->fairways;
		*b++ = s->drives;
		*b++ = s->greens;
		*b++ = s->green_hit;
		*b++ = (uint8_t)(s->putts >> 8);
		*b++ = (uint8_t)s->putts;
		*b++ = (uint8_t)(s->penalties >> 8);
		*b++ = (uint8_t)s->penalties;
	}
	*b++ = r->span;
	return (uint16_t)(b - buf);
}

int rules_unpack(Round *r, const uint8_t *buf, uint32_t len)
{
	const uint8_t *b = buf;
	uint8_t i, h, seen = 0, players, holes, v2, v3, v4;
	uint8_t *z = (uint8_t *)r;
	uint16_t k, ver, size;

	if (len < 2 + 32 + 4 + 9) return -1;
	ver = (uint16_t)((uint16_t)b[0] << 8 | b[1]);
	if (ver < 1 || ver > RULES_SAVE_VERSION) return -1;
	v2 = ver >= 2;
	v3 = ver >= 3;
	v4 = ver >= 4;
	players = b[2 + 32 + 4];
	holes = b[2 + 32 + 4 + 1];
	if (players < 1 || players > RULES_PLAYERS || holes < 1 || holes > COURSE_MAX_HOLES) return -1;
	size = (uint16_t)(2 + 32 + 4 + 9 + holes + (v2 ? 3 : 0) + v3 + v4 + players * (RULES_NAME_MAX + 1 + 11 + v2 + holes));
	if (len < size) return -1;
	if (ver >= 5 && b[2 + 32 + 4 + 9 + holes] == MODE_TOURN)
		size = (uint16_t)(size + 2 + holes + 2 * players);
	if (ver >= 6) size = (uint16_t)(size + RULES_STATS_SAVE(players));
	if (ver >= 7) size++;
	if (len != size) return -1;

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
	if (v2) {
		r->mode = *b++;
		r->balls = (uint16_t)((uint16_t)b[0] << 8 | b[1]);
		b += 2;
		if (!rules_mode_ok(r->mode, players)) return -1;
	}
	if (v3) {
		r->tee = *b++;
		if (r->tee >= COURSE_MAX_TEES) return -1;
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
		if (v2) p->cpu = *b++;
		if (p->lie >= LIE_COUNT || p->holed > 1 || p->cpu >= CPU_LEVELS) return -1;
		/* a ball well off any hole */
		if (p->ball.x < -(64L << 16) || p->ball.y < -(64L << 16) ||
		    p->ball.x > ((fixed)(HOLE_MAX_WIDTH + 64) << 16) || p->ball.y > ((fixed)(HOLE_MAX_LENGTH + 64) << 16)) return -1;
		for (h = 0; h < holes; h++) p->score[h] = *b++;
	}
	if (v4) {
		uint8_t options = *b++;
		if (options > 3 || ((options & 2) && r->mode != MODE_GREEN)) return -1;
		r->holeout = options & 1;
		r->repeat = options >> 1;
	}
	if (ver >= 5 && r->mode == MODE_TOURN) {
		r->tour_stage = *b++;
		r->tour_wind = *b++;
		if (r->tour_stage > TOUR_SECOND || r->tour_wind > 40) return -1;
		for (h = 0; h < holes; h++) {
			if (*b > 56) return -1;
			r->tour_difficulty[h] = (int8_t)((int16_t)*b++ - 24);
		}
		for (i = 0; i < players; i++) {
			uint16_t total = (uint16_t)((uint16_t)b[0] << 8 | b[1]);
			b += 2;
			if (r->tour_stage == TOUR_SECOND ? total < holes || total > (uint16_t)(holes * 255) : total != 0)
				return -1;
			r->tour_first[i] = total;
		}
		if (r->tour_stage == TOUR_FIRST && r->over) return -1;
		if (r->tour_stage == TOUR_BREAK && (r->over || r->hole != holes - 1 || rules_done(r) != holes))
			return -1;
	}
	if (ver >= 6) {
		r->stats_available = *b++;
		if (r->stats_available > 1) return -1;
		for (i = 0; i < players; i++) {
			RoundStats *s = &r->pl[i].stats;
			s->fairways = *b++;
			s->drives = *b++;
			s->greens = *b++;
			s->green_hit = *b++;
			s->putts = (uint16_t)((uint16_t)b[0] << 8 | b[1]);
			s->penalties = (uint16_t)((uint16_t)b[2] << 8 | b[3]);
			b += 4;
			if (s->fairways > s->drives || s->drives > holes || s->greens > holes ||
			    s->green_hit > 1 || s->putts > holes * 255 || s->penalties > holes * 255) return -1;
		}
	}
	if (ver >= 7) {
		r->span = *b++;
		if (r->span >= SPAN_COUNT || (r->span && (holes != 9 || r->mode >= MODE_TOURN))) return -1;
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
