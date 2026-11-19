#!/usr/bin/env python3
"""Runs build/Par386.prc on a Tungsten TX in Frond (OS 5.4, 320x480 with the input area).

Starts from a copy of PalmBolo's calibrated TX launcher session in
build/tx/ (firmware, so it stays out of the repo; it's copied
again if the scratch disk loses it). Each run
starts from the session, installs the PRC, launches Par 386, waits, saves
OUTDIR/end.png and then runs the steps (tools/frondsteps.py; taps are in
160x160 units and doubled here).

Usage: frond-tx.py OUTDIR [--seconds N] [--prc PRC] [--install PDB ...] [STEP ...]
"""
import argparse
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FROND = Path.home() / 'Projects/palm/frond'
sys.path.insert(0, str(FROND / 'tools'))
import palm_boot  # noqa: E402
import frondsteps  # noqa: E402

LAUNCHER = Path('build/firmware/tx-launcher')
NOR = None
DEVICE = 'palm-tx'


def session_for(nor):
    """A copy of PalmBolo's calibrated TX launcher session (PalmBolo HANDOFF.md),
    made the first time; the core writes its NAND, so the original isn't used."""
    machine = Path('build/tx/tx-launcher')
    if not (machine / 'launcher.frond').is_file():
        machine.mkdir(parents=True, exist_ok=True)
        for f in ('launcher.frond', 'nand.frondnand'):
            shutil.copyfile(LAUNCHER / f, machine / f)
    return machine


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--seconds', type=float, default=5)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/Par386.prc')
    p.add_argument('--install', type=Path, action='append', default=[],
                   help='another PRC or PDB to install first, such as a course')
    p.add_argument('steps', nargs='*')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = session_for(NOR)
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
        for f in a.install:
            reply = core.send(f'install "{f.resolve()}"', 'installation', 60)
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
