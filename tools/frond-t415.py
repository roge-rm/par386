#!/usr/bin/env python3
"""Runs build/Par386.prc on a Sony Clie T415 in Frond (frond-68k, OS 4.1, 320x320 through the Sony HR library).

Boots the ROM through setup once (palm_boot's iiix-hires recipe, the IIIx
setup with frames halved and taps doubled) and keeps that launcher session in
build/frond/t415/ (it holds the ROM, so it stays out of the repo). Each run
starts from the session, installs the PRC, launches Par 386, waits, saves
OUTDIR/end.png and then runs the steps (tools/frondsteps.py; taps are in
160x160 units and doubled here).

Usage: frond-t415.py OUTDIR [--seconds N] [--rom ROM] [--prc PRC] [--install PDB ...] [--speed original|uncapped] [STEP ...]
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

# Frond qualifies the T415 on the T400 ROM (same OS 4.1); the CLIE-T415/extracted dump
# isn't taken yet ("unsupported 68k ROM (card header version 5)").
ROM = Path('build/firmware/palm/Sony/CLIE-T400/Sony-Clie-T400.rom')
DEVICE = 'sony-clie-t415'


def session_for(rom):
    """The launcher session for a ROM, booting through setup the first time."""
    folder = ROOT / 'build/frond/t415'
    folder.mkdir(parents=True, exist_ok=True)
    session = folder / (rom.stem + '-launcher.frond')
    if session.is_file():
        return session
    core = palm_boot.Core(['--rom', rom, '--device', DEVICE], folder / 'boot-log.txt')
    try:
        palm_boot.iiix_hires_setup(core, folder)
        core.send(f'save-session "{session}"', 'wrote session', 60)
    finally:
        core.close()
        for f in folder.glob('console-*.replay'):
            f.unlink()
    return session


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--seconds', type=float, default=5)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/Par386.prc')
    p.add_argument('--install', type=Path, action='append', default=[],
                   help='another PRC or PDB to install first, such as a course')
    p.add_argument('--speed', default='original', choices=['original', 'uncapped'])
    p.add_argument('--rom', type=Path, default=ROM)
    p.add_argument('steps', nargs='*')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = session_for(a.rom)
    core = palm_boot.Core([session, '--device', DEVICE, '--speed', a.speed], out / 'core.log')
    try:
        core.until('speed-state', 'speed:', 'speed', 30)
        for f in a.install + [a.prc]:
            reply = core.send(f'install "{f.resolve()}"', 'install', 60)
            if 'successful' not in reply:
                raise SystemExit(f'install {f}: {reply.strip()}')
        core.launch('Par 386')
        time.sleep(a.seconds)
        core.frame(out / 'end.png')
        frondsteps.run(core, out, a.steps, scale=2)
    finally:
        core.close()
        for f in out.glob('console-*.replay'):
            f.unlink()


if __name__ == '__main__':
    main()
