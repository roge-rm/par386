#!/usr/bin/env python3
"""Makes the sampled sounds for OS 5 Palms: 8-bit unsigned, 11025 Hz, mono.

Every sound is synthesised here, so there are no recordings to license: club
hits (a sharp transient and a short resonant ring, different for a wood, an
iron and the putter), the ball coming down on grass, rough and sand, a water
splash, the ball rattling into the cup, bird chirps for the tee and applause.
They're cut to 8 bits at 11 kHz with no dither, like digitized sound of the
early 90s. Writes:
  OUT/Snd8-ID.bin   the raw samples, one resource each (IDs in src/palm/audio.c)
  OUT/snd.rcp       PilRC DATA lines for them
and a WAV of each to PREVIEW if given.

Usage: mksound.py OUT [PREVIEW]
"""
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 11025
NYQ = RATE / 2
rng = np.random.default_rng(386)


def t_of(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def band(x, lo, hi):
    """Band-pass by FFT with soft edges; lo 0 is a low-pass, hi None a high-pass."""
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / RATE)
    m = np.ones_like(f)
    if lo > 0:
        m *= 1 / (1 + (lo / np.maximum(f, 1)) ** 4)
    if hi:
        m *= 1 / (1 + (f / hi) ** 4)
    return np.fft.irfft(np.fft.rfft(x) * m, n)


def noise(seconds):
    return rng.uniform(-1, 1, int(seconds * RATE))


def ring(t, freqs, decay):
    """Decaying sines: (Hz, level) pairs, decay in seconds to 1/e."""
    out = np.zeros_like(t)
    for f, a in freqs:
        out += a * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * np.exp(-t / decay)
    return out


def at(dst, src, start):
    """Adds src into dst from sample start, cut at dst's end."""
    i = int(start)
    if i >= len(dst):
        return
    n = min(len(src), len(dst) - i)
    dst[i:i + n] += src[:n]


def norm(x, peak=0.9):
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def club(seconds, click_ms, click_band, body, decay, thump=0.0, thump_hz=140):
    t = t_of(seconds)
    x = ring(t, body, decay)
    c = band(noise(click_ms / 1000), *click_band) * np.exp(-np.arange(int(click_ms * RATE / 1000)) / (click_ms * RATE / 3000))
    at(x, c * 1.6, 0)
    if thump:
        x += thump * np.sin(2 * np.pi * thump_hz * t) * np.exp(-t / 0.03)
    # a little air after the hit
    x += 0.04 * band(noise(seconds), 1500, 4500) * np.exp(-t / 0.08)
    return norm(x)


def wood():
    # persimmon: a hollow "thock", the ring low and short
    return club(0.26, 4, (800, 5000), [(620, 1.0), (1340, 0.6), (2210, 0.35), (3120, 0.15)], 0.035, thump=0.5)


def iron():
    # forged iron: a crisp click with a brighter, longer ring and a divot
    x = club(0.3, 3, (2000, None), [(1870, 0.8), (2950, 0.7), (4180, 0.4), (980, 0.3)], 0.05, thump=0.3, thump_hz=180)
    t = t_of(0.3)
    d = band(noise(0.3), 150, 900) * np.exp(-((t - 0.035) / 0.03) ** 2)
    return norm(x + 0.35 * d)


def putt():
    return norm(club(0.14, 2, (1200, 4500), [(860, 1.0), (1720, 0.4), (2600, 0.15)], 0.02), 0.6)


def land(seconds, low, rustle, level):
    t = t_of(seconds)
    x = low * np.sin(2 * np.pi * 95 * t) * np.exp(-t / 0.025)
    x += band(noise(seconds), 0, 700) * np.exp(-t / 0.02)
    x += rustle * band(noise(seconds), 1500, 4500) * np.exp(-t / 0.05)
    return norm(x, level)


def sand():
    t = t_of(0.34)
    x = 0.8 * np.sin(2 * np.pi * 80 * t) * np.exp(-t / 0.03)
    x += band(noise(0.34), 0, 500) * np.exp(-t / 0.03)
    # the grains: a gritty hiss that falls away
    g = band(noise(0.34), 1200, 4000) * (rng.uniform(0, 1, len(t)) > 0.6)
    x += 0.5 * g * np.exp(-t / 0.09)
    return norm(x, 0.8)


def splash():
    s = 0.75
    t = t_of(s)
    x = np.zeros_like(t)
    # the hit: a burst of noise, bright then darker as it falls back
    b1 = band(noise(s), 300, 4500) * np.exp(-t / 0.05)
    b2 = band(noise(s), 150, 1800) * np.exp(-t / 0.18) * np.minimum(1, t / 0.02)
    x += b1 + 0.8 * b2
    # drops and bubbles: short rising sine chirps
    for _ in range(14):
        start = rng.uniform(0.05, 0.6)
        d = rng.uniform(0.012, 0.03)
        tt = t_of(d)
        f0 = rng.uniform(500, 1200)
        ch = np.sin(2 * np.pi * (f0 * tt + f0 * 2 * tt * tt / d)) * np.exp(-tt / (d / 3))
        at(x, ch * rng.uniform(0.15, 0.35) * np.exp(-start / 0.3), start * RATE)
    return norm(x)


def cup():
    s = 0.62
    t = t_of(s)
    x = np.zeros_like(t)
    # the ball bouncing on the cup's plastic and settling: hits closer and softer
    when, gap, level = 0.0, 0.085, 1.0
    for k in range(8):
        tt = t_of(0.04)
        f = rng.uniform(0.9, 1.1)
        hit = ring(tt, [(2350 * f, 1.0), (3900 * f, 0.6), (1300 * f, 0.5)], 0.009)
        hit[:20] += band(noise(20 / RATE), 2000, None) * 0.8
        at(x, hit * level, when * RATE)
        when += gap
        gap *= 0.68
        level *= 0.72
    # and the last clunk on the bottom
    tt = t_of(0.12)
    at(x, 0.6 * ring(tt, [(420, 1.0), (980, 0.4)], 0.025), (when + 0.05) * RATE)
    return norm(x)


def chirp(d, f0, f1, vib=0.0, vib_hz=0.0):
    """One bird note: a sine swept from f0 to f1 with an FM warble."""
    tt = t_of(d)
    f = f0 + (f1 - f0) * (tt / d) ** 0.7
    if vib:
        f = f + vib * np.sin(2 * np.pi * vib_hz * tt)
    ph = 2 * np.pi * np.cumsum(f) / RATE
    env = np.sin(np.pi * tt / d) ** 1.5
    return np.sin(ph) * env


def birds1():
    # a short song: three rising notes and a quick fall
    x = np.zeros(int(0.5 * RATE))
    at(x, chirp(0.06, 2600, 3900), 0.0 * RATE)
    at(x, chirp(0.06, 2700, 4100), 0.09 * RATE)
    at(x, chirp(0.06, 2800, 4300), 0.18 * RATE)
    at(x, chirp(0.12, 4400, 2300, vib=250, vib_hz=45), 0.28 * RATE)
    return norm(x + 0.01 * noise(0.5), 0.7)


def birds2():
    # a far-off trill
    x = np.zeros(int(0.42 * RATE))
    for k in range(7):
        at(x, chirp(0.035, 3600, 3000, vib=120, vib_hz=80) * (0.6 + 0.4 * np.sin(k / 2)), k * 0.05 * RATE)
    return norm(x + 0.01 * noise(0.42), 0.5)


def applause():
    s = 1.8
    n = int(s * RATE)
    t = t_of(s)
    x = np.zeros(n)
    # rate of claps: swells in 0.35 s, holds, fades out from 1.0 s
    swell = np.minimum(1, t / 0.35) * np.where(t < 1.0, 1, np.exp(-(t - 1.0) / 0.25))
    k = 0
    for i in range(0, n, 40):
        for _ in range(rng.poisson(110 * swell[i] * 40 / RATE)):
            d = rng.uniform(0.006, 0.014)
            c = noise(d) * np.exp(-np.arange(int(d * RATE)) / (d * RATE / 4))
            at(x, c * rng.uniform(0.3, 1.0), i + rng.uniform(0, 40))
            k += 1
    x = band(x, 600, 3800)
    # a room under it
    x += 0.15 * band(noise(s), 200, 2500) * swell
    return norm(x, 0.85)


# resource IDs, as in src/palm/audio.c
SOUNDS = [
    (2001, 'wood', wood),
    (2002, 'iron', iron),
    (2003, 'putt', putt),
    (2004, 'land-fairway', lambda: land(0.14, 0.7, 0.15, 0.55)),
    (2005, 'land-rough', lambda: land(0.18, 0.5, 0.6, 0.5)),
    (2006, 'sand', sand),
    (2007, 'splash', splash),
    (2008, 'cup', cup),
    (2009, 'birds1', birds1),
    (2010, 'birds2', birds2),
    (2011, 'applause', applause),
]


def to8(x):
    # 8-bit unsigned, no dither, with the grit of a cheap DAC
    return np.clip(np.round(np.tanh(x * 1.1) / np.tanh(1.1) * 127 + 128), 0, 255).astype(np.uint8)


def main():
    out = Path(sys.argv[1])
    preview = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    out.mkdir(parents=True, exist_ok=True)
    if preview:
        preview.mkdir(parents=True, exist_ok=True)
    lines = ['/* Made by tools/mksound.py. */']
    total = 0
    for rid, name, make in SOUNDS:
        data = to8(make()).tobytes()
        assert len(data) < 65000
        total += len(data)
        f = out / f'Snd8-{rid}.bin'
        f.write_bytes(data)
        lines.append(f'DATA "Snd8" ID {rid} "{f}"')
        if preview:
            with wave.open(str(preview / f'{name}.wav'), 'wb') as w:
                w.setnchannels(1)
                w.setsampwidth(1)
                w.setframerate(RATE)
                w.writeframes(data)
        print(f'{name:13s} {rid} {len(data):6d} bytes')
    print(f'total {total} bytes')
    (out / 'snd.rcp').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
