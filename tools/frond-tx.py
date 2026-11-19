#!/usr/bin/env python3
"""Runs build/Par386.prc on a Tungsten TX in Frond (OS 5.4, 320x480 with the input area).

Starts from a copy of PalmBolo's calibrated TX launcher session in
build/tx/ (firmware, so it stays out of the repo; it's copied
again if the scratch disk loses it). Each run
starts from the session, installs the PRC, launches Par 386, waits, saves
OUTDIR/end.png and then runs the steps (tools/frondsteps.py; taps are in
160x160 units and doubled here).

Usage: frond-tx.py OUTDIR [--seconds N] [--prc PRC] [--install PDB ...] [--sd IMG] [STEP ...]
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
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


class SockCore:
    """The core run on its desktop control socket, which has the sd-* commands the console lacks.
    It has the same calls as palm_boot.Core that the steps use."""

    def __init__(self, session, log, sd):
        self.dir = tempfile.mkdtemp(prefix='par386-')
        self.listener = socket.socket(socket.AF_UNIX)
        self.listener.bind(self.dir + '/c.sock')
        self.listener.listen(1)
        cmd = [str(palm_boot.core_for(DEVICE)), str(session), '--device', DEVICE, '--no-sound',
               '--desktop-socket', self.dir + '/c.sock']
        if sd:
            cmd += ['--sd', str(sd)]
        if shutil.which('heavy'):
            cmd = ['heavy', 'emulator'] + cmd
        self.log = log.open('w')
        self.process = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                        start_new_session=True,
                                        env={**os.environ, 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy'})
        self.conn, _ = self.listener.accept()
        self.buf = b''
        self.n = 0
        self.snaps = 0
        self.ctl = self

    def line(self):
        while b'\n' not in self.buf:
            self.buf += self.conn.recv(4096)
        line, _, self.buf = self.buf.partition(b'\n')
        return line.decode().split('\t')

    def request(self, command, arg=''):
        self.n += 1
        self.conn.sendall(f'{self.n}\t{command}\t{str(arg).encode().hex()}\n'.encode())
        while True:
            f = self.line()
            if len(f) == 4 and f[0] == str(self.n):
                return f[1] == 'ok', f[2], bytes.fromhex(f[3]).decode()

    def send(self, command, arg=''):
        ok, code, msg = self.request(command, arg)
        return f'{"ok" if ok else "error"} {code} {msg}'

    def retry(self, command, arg='', seconds=120):
        end = time.monotonic() + seconds
        while True:
            ok, code, msg = self.request(command, arg)
            if ok:
                return code, msg
            if code not in ('not-ready', 'asleep') or time.monotonic() > end:
                raise SystemExit(f'{command}: {code} {msg}')
            time.sleep(1)

    def install(self, path):
        self.retry('guest-ready')
        self.retry('install', path)

    def launch(self, name, timeout=60):
        self.retry('launch-app', name, timeout)
        time.sleep(3)

    def replay(self, out, lines):
        self.snaps += 1
        path = out / f'console-{self.snaps}.replay'
        path.write_text('\n'.join(lines) + '\n')
        self.retry('input-replay', path)
        while self.request('replay-state')[1] != 'finished':
            time.sleep(0.1)

    def key(self, out, name, hold_ms=200):
        self.replay(out, [f'0 key-down {name}', f'{hold_ms} key-up {name}'])

    def tap(self, out, x, y, hold_ms=150):
        self.replay(out, [f'0 pen-down {x} {y}', f'{hold_ms} pen-up'])

    def frame(self, path):
        path.unlink(missing_ok=True)
        self.replay(path.parent, [f'0 snapshot "{path}"'])
        time.sleep(0.3)

    def close(self):
        try:
            self.request('quit')
        except OSError:
            pass
        try:
            self.process.wait(30)
        except subprocess.TimeoutExpired:
            palm_boot.stop_guest(self.process)
        shutil.rmtree(self.dir, ignore_errors=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('out', type=Path)
    p.add_argument('--seconds', type=float, default=5)
    p.add_argument('--prc', type=Path, default=ROOT / 'build/Par386.prc')
    p.add_argument('--install', type=Path, action='append', default=[],
                   help='another PRC or PDB to install first, such as a course')
    p.add_argument('--sd', type=Path, help='an SD card image to put in the slot (it is written to)')
    p.add_argument('steps', nargs='*')
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    session = session_for(NOR)
    if a.sd:
        core = SockCore(session, out / 'core.log', a.sd.resolve())
    else:
        core = palm_boot.Core([session, '--device', DEVICE], out / 'core.log')
    try:
        if a.sd:
            core.install(a.prc.resolve())
            for f in a.install:
                core.retry('install', f.resolve())
        else:
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
