/* Plays the core's sounds (core sound.h): tones and short tunes on the
 * piezo through sndCmdFrqOn, which returns at once, stepped on from the
 * event loop; on Palm OS 5 sampled sounds through a sound stream as well.
 * Nothing here waits on a sound. */
#ifndef AUDIO_H
#define AUDIO_H

#include <PalmOS.h>

/* Sets up sound and returns the saved setting (SOUND_*). */
UInt8 AudioOpen(void);
void AudioClose(void);
/* A new setting from the settings screen: saved and used from now on. */
void AudioSetMode(UInt8 mode);
/* Plays what the core queued and steps the tune on. birds: the tee view is
 * up, so the ambience may chirp now and then. busy: frames are running (the
 * meter or a flight), so the stream isn't paused now, as pausing it can
 * hold things up for a fifth of a second. Call it after anything that may
 * queue a sound, and before waiting for an event. */
void AudioTick(Boolean birds, Boolean busy);
/* Ticks to wait for an event before the next AudioTick, or evtWaitForever. */
Int32 AudioWait(void);
#ifdef DEBUG
/* What's in use and how it went, for the debug title: S (samples) or T
 * (tones only), the game volume, the last error, sounds played and stream
 * callbacks so far: "S v64 e0 n3 f120". */
void AudioDescribe(char *t);
#endif

#endif
