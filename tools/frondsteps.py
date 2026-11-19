"""Steps for the Frond runners: keys, taps, waits and screenshots.

Each step is KIND:ARG, such as key:down, tap:80,60 (logical 160x160 units,
scaled to the device), wait:1.5 or snap:menu (saves OUTDIR/menu.png).
replay:FILE sends a Frond input replay (lines of MS COMMAND ARGS, with
{out} standing for OUTDIR and {n} for how many replays have run, from 1)
and waits for its last line, for taps that need
exact timing such as the swing meter. launch:NAME launches an app by its
database name, which leaves the one running. sd:eject, sd:reinsert and
sd:state are the console's sd-eject, sd-reinsert and sd-state.
"""
import time


def run(core, out, steps, scale=1):
    replays = 0
    for step in steps:
        kind, _, arg = step.partition(':')
        if kind == 'key':
            core.key(out, arg)
        elif kind == 'tap':
            x, y = (int(v) * scale for v in arg.split(','))
            core.tap(out, x, y)
        elif kind == 'wait':
            time.sleep(float(arg))
        elif kind == 'replay':
            replays += 1
            lines = [l.replace('{out}', str(out)).replace('{n}', str(replays))
                     for l in open(arg).read().splitlines() if l.strip() and not l.startswith('#')]
            core.replay(out, lines)
            time.sleep(int(lines[-1].split()[0]) / 1000 + 0.5)
        elif kind == 'launch':
            core.launch(arg)
        elif kind == 'sd':
            print(arg, core.send(f'sd-{arg}'))
            time.sleep(2)
        elif kind == 'ctl':
            # a control socket command (frond-tx.py with --sd), such as ctl:reset or ctl:db-list
            print(arg, core.retry(*arg.split(' ', 1)) if arg.startswith(('reset', 'db-list')) else core.send(arg))
            time.sleep(2)
        elif kind == 'snap':
            core.frame(out / f'{arg}.png')
        else:
            raise SystemExit(f'unknown step {step}')
