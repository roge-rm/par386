#!/usr/bin/env python3
"""Runs build/Par386.prc on a Palm IIIx in Frond (frond-68k, OS 3.1).

Boots the ROM through setup once and keeps that launcher session in
build/iiix/ (it holds the ROM, so it stays out of the repo). Each run starts
from the session, installs the PRC, launches Par 386, waits and saves a
screenshot as OUTDIR/end.png. Steps after that (tools/frondsteps.py) press
keys, tap and save more screenshots.

--audio WAV writes the piezo sound to WAV and each tone change to OUTDIR/core.log.

Usage: frond-iiix.py OUTDIR [--seconds N] [--rom ROM] [--prc PRC] [--install PDB ...] [--speed original|uncapped] [--audio WAV] [STEP ...]
"""
import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FROND = Path.home() / 'Projects/palm/frond'
sys.path.insert(0, str(FROND / 'tools'))
import palm_boot  # noqa: E402
import frondsteps  # noqa: E402

ROM = Path('build/firmware/palm/Palm/IIIx/Palm-IIIx-3.1-en.rom')


def session_for(rom):
    """The launcher session for a ROM, booting through setup the first time."""
    folder = ROOT / 'build/iiix'
    folder.mkdir(parents=True, exist_ok=True)
    session = folder / (rom.stem + '-launcher.frond')
    if session.is_file():
        return session
    core = palm_boot.Core(['--rom', rom, '--device', 'palm-iiix'], folder / 'boot-log.txt')
    try:
        palm_boot.iiix_setup(core, folder)
        core.send(f'save-session "{session}"', 'wrote session', 60)
    finally:
        core.close()
    return session


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--seconds', type=float, default=5)
    p.add_argument('--speed', default='original', choices=['original', 'uncapped'])
    p.add_argument('--rom', type=Path, default=ROM)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/Par386.prc')
    p.add_argument('--install', type=Path, action='append', default=[],
                   help='another PRC or PDB to install first, such as a course')
    p.add_argument('--audio', type=Path, help='write the piezo sound to this WAV')
    p.add_argument('steps', nargs='*')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = session_for(a.rom)
    extra = ['--audio-output', a.audio.resolve(), '--tone-log'] if a.audio else []
    core = palm_boot.Core([session, '--device', 'palm-iiix', '--speed', a.speed], out / 'core.log', *extra)
    try:
        core.until('speed-state', 'speed:', 'speed', 30)
        if a.audio:
            core.send('audio-on')
        for f in a.install + [a.prc]:
            reply = core.send(f'install "{f.resolve()}"', 'install', 60)
            if 'successful' not in reply:
                raise SystemExit(f'install {f}: {reply.strip()}')
        core.launch('Par 386')
        time.sleep(a.seconds)
        core.frame(out / 'end.png')
        frondsteps.run(core, out, a.steps)
    finally:
        core.close()
        for f in out.glob('console-*.replay'):
            f.unlink()


if __name__ == '__main__':
    main()
