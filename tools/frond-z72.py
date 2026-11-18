#!/usr/bin/env python3
"""Runs build/Par386.prc on a Zire 72 in Frond (OS 5.2, 320x320 double density).

Boots the NOR through setup once and keeps that launcher session in
build/z72/ (it holds the firmware, so it stays out of the repo). Each run
starts from the session, installs the PRC, launches Par 386, waits, saves
OUTDIR/end.png and then runs the steps (tools/frondsteps.py; taps are in
160x160 units and doubled here).

Usage: frond-z72.py OUTDIR [--seconds N] [--nor NOR] [--prc PRC] [STEP ...]
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

NOR = Path('build/firmware/palm/Palm/Zire-72/Palm-Zire72-nor.bin')
DEVICE = 'palm-zire-72'


def session_for(nor):
    """The launcher session for a NOR, booting through setup the first time."""
    folder = ROOT / 'build/z72'
    folder.mkdir(parents=True, exist_ok=True)
    session = folder / (nor.stem + '-launcher.frond')
    machine = folder / (nor.stem + '-launcher')
    if machine.is_dir():
        return machine
    if session.is_file():
        return session
    core = palm_boot.Core(['--device', DEVICE, nor], folder / 'boot-log.txt')
    try:
        palm_boot.zire_setup(core, folder)
        core.send(f'save-session "{session}"', 'wrote session', 60)
    finally:
        core.close()
        for f in folder.glob('console-*.replay'):
            f.unlink()
    return machine if machine.is_dir() else session


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--seconds', type=float, default=5)
    p.add_argument('--nor', type=Path, default=NOR)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/Par386.prc')
    p.add_argument('steps', nargs='*')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = session_for(a.nor)
    core = palm_boot.Core([session, '--device', DEVICE], out / 'core.log')
    try:
        # The ARM core has no speed-state; install fails until Palm OS is up, so retry.
        end = time.monotonic() + 120
        while True:
            reply = core.send(f'install "{a.prc.resolve()}"', 'installation', 60)
            if 'successful' in reply:
                break
            if time.monotonic() > end:
                raise SystemExit(f'install: {reply.strip()}')
            time.sleep(2)
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
