#include "sound.h"

static uint8_t queue[SND_QUEUE];
static uint8_t q_head, q_n, ambient;
static SndHook hook;

#ifndef __palmos__
uint8_t snd_log[SND_LOG_MAX];
uint16_t snd_log_n;

static void SEC_CORE log_add(uint8_t v)
{
	if (snd_log_n < SND_LOG_MAX) snd_log[snd_log_n++] = v;
}
#else
#define log_add(v)
#endif

void snd_play(uint8_t id)
{
	if (id == SND_NONE || id >= SND_COUNT) return;
	log_add(id);
	if (q_n == SND_QUEUE) {
		q_head = (uint8_t)((q_head + 1) & (SND_QUEUE - 1));
		q_n--;
	}
	queue[(q_head + q_n) & (SND_QUEUE - 1)] = id;
	q_n++;
	if (hook) hook();
}

void snd_set_hook(SndHook fn)
{
	hook = fn;
}

uint8_t snd_take(void)
{
	uint8_t id;
	if (!q_n) return SND_NONE;
	id = queue[q_head];
	q_head = (uint8_t)((q_head + 1) & (SND_QUEUE - 1));
	q_n--;
	return id;
}

void snd_ambient(uint8_t on)
{
	on = on ? 1 : 0;
	if (on == ambient) return;
	ambient = on;
	log_add((uint8_t)(0x80 + on));
}

uint8_t snd_ambient_on(void)
{
	return ambient;
}
