/* The game's sounds on the Palm. Tones start with sndCmdFrqOn, which
 * returns at once, and AudioTick starts each next step of a sound from the
 * event loop, so neither the swing meter nor the ball's flight ever waits
 * on one. With the Sound Manager's streams (Palm OS 5) the hits, landings,
 * the splash, the cup, birds and applause are 8-bit 11025 Hz samples
 * (tools/mksound.py) mixed by a stream callback: one effect at a time and
 * quiet birds under it.
 *
 * The stream callback runs on the sound thread, as 68k code through PACE.
 * It only touches the Mix block it's given, with no globals (no A5) and no
 * calls, and lives in the first code segment, which stays locked. */
#include <PalmOS.h>
#include "audio.h"
#include "sound.h"
#include "front.h"

#define kCreator    'P386'
#define kSoundPref  2           /* app preference: the setting, one byte */
#define kSmpType    'Snd8'
#define kSmpFirst   2001        /* Snd8 2001 to 2014, as tools/mksound.py makes them */
#define kSmpCount   14
#define kSmpBirds   2009        /* 2009 and 2010 */
#define kSmpCup     2008
#define kSmpApplause 2011
#define kRate       11025
#define kFrames     512         /* the stream's buffer: about 46 ms */
#define kRom30      sysMakeROMVersion(3, 0, 0, sysROMStageRelease, 0)

/* A tone: Hz (0 is a rest), ms, amplitude 0 to sndMaxAmp before the game volume scales it. */
typedef struct {
	UInt16 freq, ms;
	UInt16 amp;
} Tone;

static const Tone kTones[] = {
	/* 0 SND_SWING */        { 900, 14, 32 },
	/* 1 SND_CLICK */        { 1600, 6, 40 },
	/* 2 SND_HIT_WOOD */     { 2100, 4, 56 }, { 520, 12, 64 },
	/* 4 SND_HIT_IRON */     { 3600, 4, 64 }, { 1800, 10, 56 }, { 2200, 10, 40 },
	/* 7 SND_HIT_PUTT */     { 1300, 8, 40 },
	/* 8 SND_LAND_FAIRWAY */ { 540, 12, 30 },
	/* 9 SND_LAND_ROUGH */   { 720, 6, 20 }, { 0, 10, 0 }, { 470, 14, 17 },
	/* 12 SND_LAND_SAND */   { 1050, 8, 29 }, { 760, 8, 24 }, { 480, 8, 16 },
	/* 15 SND_SPLASH */      { 1700, 12, 48 }, { 1100, 12, 48 }, { 2100, 12, 40 }, { 900, 12, 40 },
	                         { 1500, 12, 32 }, { 700, 20, 32 }, { 450, 30, 24 },
	/* 22 SND_CUP */         { 2300, 6, 48 }, { 0, 40, 0 }, { 2500, 6, 40 }, { 0, 26, 0 }, { 2200, 6, 32 },
	                         { 0, 18, 0 }, { 2400, 6, 24 }, { 0, 12, 0 }, { 500, 30, 40 },
	/* 31 SND_OOB */         { 300, 150, 48 }, { 0, 40, 0 }, { 220, 250, 48 },
	/* 34 SND_HOLED */       { 523, 80, 48 }, { 0, 20, 0 }, { 659, 80, 48 }, { 0, 20, 0 }, { 784, 80, 48 },
	                         { 0, 20, 0 }, { 1047, 200, 48 },
	/* 41 SND_BIRDIE */      { 392, 90, 48 }, { 523, 90, 48 }, { 659, 90, 48 }, { 784, 180, 48 },
	                         { 659, 90, 48 }, { 784, 300, 48 },
	/* 47 SND_BOGEY */       { 392, 180, 48 }, { 0, 30, 0 }, { 370, 180, 48 }, { 0, 30, 0 }, { 349, 450, 48 },
	/* 52 SND_MENU_MOVE */   { 2000, 4, 24 },
	/* 53 SND_MENU_SELECT */ { 1500, 12, 32 },
	/* 54 SND_LAND_PATH */   { 1500, 9, 34 },
	/* 55 SND_LIP */         { 2600, 8, 36 }, { 1600, 8, 24 },
	/* 57 SND_MISHIT */      { 730, 5, 44 }, { 370, 13, 40 },
	/* 59 SND_CLUB_CHANGE */ { 1150, 6, 20 },
	/* 60 SND_ACE */         { 784, 60, 48 }, { 0, 30, 0 }, { 784, 60, 48 }, { 1047, 110, 48 },
	                         { 1319, 110, 48 }, { 1568, 130, 48 }, { 2093, 280, 48 },
	/* 67 end */             { 0, 0, 0 }
};

/* Each sound's first step in kTones, by SND_* id; the next one's start ends it. */
static const UInt8 kStart[SND_COUNT + 1] = {
	0, 0, 1, 2, 4, 7, 8, 9, 12, 15, 22, 31, 34, 41, 47, 52, 53, 54, 55, 57, 59, 60, 67
};

/* The sample for each SND_* id on stream devices, 0 for tones only. */
static const UInt16 kSample[SND_COUNT] = {
	0, 0, 0, 2001, 2002, 2003, 2004, 2005, 2006, 2007, kSmpCup, 0, 0, 0, 0, 0, 0,
	2012, 2013, 2014, 0, 0
};

/* The callback's block. Voice 0 plays effects, voice 1 the birds, a
 * quarter as loud. The main thread posts a sample in slot (seq & 3) and
 * then moves seq on; the callback takes the newest when seq isn't what it
 * last saw (ack), so the two never write the same field. */
typedef struct {
	const UInt8 *ptr[2];        /* callback's: what each voice plays */
	volatile UInt32 len[2], pos[2];
	volatile UInt16 ack[2];
	volatile UInt16 seq[2];     /* main thread's */
	volatile Boolean birds;    /* main thread's: let the current call play */
	UInt16 birdGain;            /* callback's: 128 is the usual quarter level */
	const UInt8 *rptr[2][4];
	UInt32 rlen[2][4];
	volatile UInt32 fills;      /* callbacks so far, for the debug line */
} Mix;

static UInt8 gMode;             /* SOUND_* */
static UInt16 gVolume;          /* the system game volume, 0 to sndMaxAmp */
static UInt16 gTps;
static UInt8 gFx = SND_NONE, gStep, gFinish;
static UInt16 gSample;
static UInt32 gStepEnd;
static Boolean gStream, gBirds;
static Boolean gPaused;         /* the stream is paused (main thread's view) */
static Boolean gPauseSet;       /* both voices are idle and gPauseAt is when to pause */
static UInt32 gPauseAt;
static UInt32 gNextBird;
static SndStreamRef gRef;
static Mix *gMix;
static MemHandle gSmpH[kSmpCount];
static const UInt8 *gSmp[kSmpCount];
static UInt32 gSmpLen[kSmpCount];
#ifdef DEBUG
static Err gErr;                /* the last error from a sound call */
static UInt16 gPlays;
#define NOTE(e) do { Err e_ = (e); if (e_) gErr = e_; } while (0)
#else
#define NOTE(e) ((void)(e))
#endif

/* Fills buf with frames 8-bit samples, 128 the middle. */
static Err AudioFill(void *user, SndStreamRef ch, void *buf, UInt32 frames)
{
	Mix *m = (Mix *)user;
	UInt8 *o = (UInt8 *)buf;
	const UInt8 *a, *b;
	UInt32 i, na, nb;
	UInt16 v, s, gain;
	Boolean fade;
	Int16 x, y;

	(void)ch;
	for (v = 0; v < 2; v++) {
		s = m->seq[v];
		if (s != m->ack[v]) {
			UInt16 r = (UInt16)((s - 1) & 3);
			m->ptr[v] = m->rptr[v][r];
			m->len[v] = m->rlen[v][r];
			m->pos[v] = 0;
			if (v == 1) m->birdGain = 128;
			m->ack[v] = s;
		}
	}
	m->fills++;
	na = m->len[0] - m->pos[0];
	nb = m->len[1] - m->pos[1];
	if (na > frames) na = frames;
	if (nb > frames) nb = frames;
	fade = !m->birds;
	gain = m->birdGain;
	if (fade && nb > gain) nb = gain;
	a = na ? m->ptr[0] + m->pos[0] : NULL;
	b = nb ? m->ptr[1] + m->pos[1] : NULL;
	m->pos[0] += na;
	m->pos[1] += nb;
	for (i = 0; i < frames; i++) {
		x = 0;
		if (i < na) x = (Int16)a[i] - 128;
		if (i < nb) {
			y = (Int16)b[i] - 128;
			if (fade) gain--;
			x += gain == 128 ? y >> 2 : ((Int16)(y * (Int16)gain)) >> 9;
		}
		if (x > 127) x = 127;
		if (x < -128) x = -128;
		o[i] = (UInt8)(x + 128);
	}
	m->birdGain = gain;
	if (!gain) m->pos[1] = m->len[1];
	return errNone;
}

static void Quiet(void)
{
	SndCommandType c;

	MemSet(&c, sizeof(c), 0);
	c.cmd = sndCmdQuiet;
	NOTE(SndDoCmd(0, &c, true));
}

/* Starts step gStep of gFx, or ends it when there are no more. */
static void PlayStep(UInt32 now)
{
	const Tone *t;
	SndCommandType c;
	UInt32 ticks;

	if (gFx == SND_NONE || kStart[gFx] + gStep >= kStart[gFx + 1]) {
		gFx = SND_NONE;
		return;
	}
	t = &kTones[kStart[gFx] + gStep];
	ticks = ((UInt32)t->ms * gTps + 999) / 1000;
	gStepEnd = now + (ticks ? ticks : 1);
	if (t->freq == 0 || t->amp == 0) {
		Quiet();
		return;
	}
	MemSet(&c, sizeof(c), 0);
	c.cmd = sndCmdFrqOn;
	c.param1 = t->freq;
	c.param2 = t->ms;
	c.param3 = (UInt16)(((UInt32)t->amp * gVolume) / sndMaxAmp);
	if (c.param3 == 0) c.param3 = 1;
	NOTE(SndDoCmd(0, &c, true));
}

/* Plays every step of a short sound before returning, with sndCmdFreqDurationAmp. */
static void PlayNow(UInt8 id)
{
	const Tone *t;
	SndCommandType c;
	UInt8 k;

	if (gFx != SND_NONE) Quiet();
	gFx = SND_NONE;
	for (k = kStart[id]; k < kStart[id + 1]; k++) {
		t = &kTones[k];
		MemSet(&c, sizeof(c), 0);
		c.cmd = sndCmdFreqDurationAmp;
		c.param1 = t->freq;
		c.param2 = t->ms;
		c.param3 = (UInt16)(((UInt32)t->amp * gVolume) / sndMaxAmp);
		if (c.param3 == 0) c.param3 = 1;
		NOTE(SndDoCmd(0, &c, true));
	}
}

/* Pauses or resumes the stream so it doesn't mix silence while nothing plays. */
static void StreamPause(Boolean pause)
{
	if (!gStream || gPaused == pause) return;
	NOTE(SndStreamPause(gRef, pause));
	gPaused = pause;
}

static void PostSample(UInt8 v, const UInt8 *data, UInt32 len)
{
	UInt16 s = (UInt16)(gMix->seq[v] & 3);
	gMix->rptr[v][s] = data;
	gMix->rlen[v][s] = len;
	gMix->seq[v] = (UInt16)(gMix->seq[v] + 1);
}

/* Plays resource id (kSmpFirst...) on voice v. */
static Boolean PlaySample(UInt8 v, UInt16 id)
{
	UInt16 k = id - kSmpFirst;

	if (!gStream || k >= kSmpCount || !gSmp[k] || !gSmpLen[k]) return false;
	PostSample(v, gSmp[k], gSmpLen[k]);
	if (v == 0) gSample = id;
	StreamPause(false);
	return true;
}

/* Is voice v still playing? */
static Boolean Busy(UInt8 v)
{
	return gMix->seq[v] != gMix->ack[v] || gMix->pos[v] < gMix->len[v];
}

/* Stops what both voices play: an empty sample in the next slot. */
static void StreamSilence(void)
{
	if (!gMix) return;
	PostSample(0, NULL, 0);
	PostSample(1, NULL, 0);
	gSample = 0;
}

static Boolean CupBusy(void)
{
	return gFx == SND_CUP || (gStream && gSample == kSmpCup && Busy(0));
}

static void Play(UInt8 id)
{
	if (id == SND_NONE || id >= SND_COUNT || gMode == SOUND_OFF || !gVolume) return;
	if (SND_IS_TUNE(id)) {
		if (gMode != SOUND_ON) return;
		/* Advancing the result can bring up the score while the cup is still playing. */
		if (CupBusy()) {
			gFinish = id;
			return;
		}
		gFinish = SND_NONE;
	}
#ifdef DEBUG
	gPlays++;
#endif
	if (id == SND_BIRDIE || id == SND_ACE) PlaySample(0, kSmpApplause);
	if (kSample[id] && PlaySample(0, kSample[id])) return;
	if (id == SND_HIT_WOOD || id == SND_HIT_IRON || id == SND_HIT_PUTT || id == SND_MISHIT) {
		/* The hit plays whole before the shot's physics runs. */
		PlayNow(id);
		return;
	}
	gFx = id;
	gStep = 0;
	PlayStep(TimGetTicks());
}

static void StreamOpen(void)
{
	UInt16 k;
	MemHandle h;
	Err e;

	h = DmGetResource(kSmpType, kSmpFirst);
	if (!h) return;
	DmReleaseResource(h);
	gMix = (Mix *)MemPtrNew(sizeof(Mix));
	if (!gMix) return;
	MemSet(gMix, sizeof(Mix), 0);
	for (k = 0; k < kSmpCount; k++) {
		h = DmGetResource(kSmpType, kSmpFirst + k);
		gSmpH[k] = h;
		if (!h) continue;
		gSmp[k] = (const UInt8 *)MemHandleLock(h);
		gSmpLen[k] = MemHandleSize(h);
	}
	e = SndStreamCreate(&gRef, sndOutput, kRate, sndUInt8, sndMono, AudioFill, gMix, kFrames, false);
	NOTE(e);
	if (e) return;
	/* the system's game volume; failing that our own scale of it */
	if (SndStreamSetVolume(gRef, sndGameVolume) != errNone)
		NOTE(SndStreamSetVolume(gRef, (Int32)gVolume * 1024 / sndMaxAmp));
	e = SndStreamStart(gRef);
	NOTE(e);
	if (e) {
		SndStreamDelete(gRef);
		return;
	}
	gStream = true;
}

static void StreamClose(void)
{
	UInt16 k;

	if (gStream) {
		NOTE(SndStreamStop(gRef));
		NOTE(SndStreamDelete(gRef));
		gStream = false;
	}
	for (k = 0; k < kSmpCount; k++) {
		if (!gSmpH[k]) continue;
		MemHandleUnlock(gSmpH[k]);
		DmReleaseResource(gSmpH[k]);
		gSmpH[k] = NULL;
		gSmp[k] = NULL;
	}
	if (gMix) MemPtrFree(gMix);
	gMix = NULL;
}

UInt8 AudioOpen(void)
{
	UInt32 rom = 0, ver = 0;
	UInt16 size = 1;
	UInt8 mode = SOUND_ON;

	gTps = SysTicksPerSecond();
	if (PrefGetAppPreferences(kCreator, kSoundPref, &mode, &size, true) == noPreferenceFound || size != 1 ||
	    mode >= SOUND_MODES)
		mode = SOUND_ON;
	gMode = mode;
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
	gVolume = rom >= kRom30 ? (UInt16)PrefGetPreference(prefGameSoundVolume) : 0;
	if (gVolume > sndMaxAmp) gVolume = sndMaxAmp;
	/* streams came with the Sound Manager's version feature (Palm OS 5) */
	if (gVolume && FtrGet(sysFileCSoundMgr, sndFtrIDVersion, &ver) == errNone && ver >= sndMgrVersionNum)
		StreamOpen();
	return mode;
}

void AudioClose(void)
{
	if (gFx != SND_NONE) Quiet();
	gFx = SND_NONE;
	gFinish = SND_NONE;
	gSample = 0;
	gPaused = gPauseSet = gBirds = false;
	StreamClose();
}

void AudioSetMode(UInt8 mode)
{
	if (mode >= SOUND_MODES) mode = SOUND_ON;
	gMode = mode;
	PrefSetAppPreferences(kCreator, kSoundPref, 1, &mode, 1, true);
	if (mode != SOUND_ON) gFinish = SND_NONE;
	if (gFx != SND_NONE && (mode == SOUND_OFF || (mode == SOUND_FX && SND_IS_TUNE(gFx)))) {
		Quiet();
		gFx = SND_NONE;
	}
	if (mode != SOUND_ON && gMix) gMix->birds = false;
	if (mode == SOUND_FX && gStream && gSample == kSmpApplause) {
		PostSample(0, NULL, 0);
		gSample = 0;
	}
	if (mode == SOUND_OFF) {
		StreamSilence();
		StreamPause(true);
	}
}

void AudioTick(Boolean birds, Boolean busy)
{
	UInt8 id;
	UInt32 now = TimGetTicks();

	birds = birds && gStream && gMode == SOUND_ON && gVolume;
	if (gMix) gMix->birds = birds;
	while ((id = snd_take()) != SND_NONE) Play(id);
	if (gFx != SND_NONE && (Int32)(now - gStepEnd) >= 0) {
		gStep++;
		PlayStep(now);
	}
	if (gFinish != SND_NONE && !CupBusy()) Play(gFinish);
	/* birds now and then while the tee view is up: the first in 1 to 3 s, then every 3 to 9 s */
	if (birds && !gBirds) gNextBird = now + gTps + (UInt32)SysRandom(0) % (2 * gTps);
	gBirds = birds;
	if (birds && (Int32)(now - gNextBird) >= 0) {
		if (!Busy(1)) PlaySample(1, (UInt16)(kSmpBirds + (SysRandom(0) & 1)));
		gNextBird = now + 3 * gTps + (UInt32)SysRandom(0) % (6 * gTps);
	}
	/* pause when both voices have been idle for 0.2 s, so the last buffer is out first */
	if (gStream && !gPaused) {
		if (busy || (gMode != SOUND_OFF && (Busy(0) || Busy(1)))) {
			gPauseSet = false;
		} else if (!gPauseSet) {
			gPauseSet = true;
			gPauseAt = now + gTps / 5 + 1;
		} else if ((Int32)(now - gPauseAt) >= 0) {
			StreamPause(true);
			gPauseSet = false;
		}
	}
}

Int32 AudioWait(void)
{
	UInt32 now = TimGetTicks(), until = 0;
	Boolean any = false;

	if (gFx != SND_NONE) {
		until = gStepEnd;
		any = true;
	}
	if (gBirds && (!any || (Int32)(gNextBird - until) < 0)) {
		until = gNextBird;
		any = true;
	}
	if (gStream && !gPaused) {
		/* look again when the pause is due, or soon to see the sample end */
		UInt32 at = gPauseSet ? gPauseAt : now + gTps / 10 + 1;
		if (!any || (Int32)(at - until) < 0) until = at;
		any = true;
	}
	if (!any) return evtWaitForever;
	return (Int32)(until - now) > 1 ? (Int32)(until - now) : 1;
}

#ifdef DEBUG
void AudioDescribe(char *t)
{
	if (gStream)
		StrPrintF(t, "S v%d e%d n%d f%ld", (Int16)gVolume, (Int16)gErr, (Int16)gPlays, (Int32)gMix->fills);
	else
		StrPrintF(t, "T v%d e%d n%d", (Int16)gVolume, (Int16)gErr, (Int16)gPlays);
}
#endif
