"""Steps for the Frond runners: keys, taps, waits and screenshots.

Each step is KIND:ARG, such as key:down, tap:80,60 (logical 160x160 units,
scaled to the device), wait:1.5 or snap:menu (saves OUTDIR/menu.png).
"""
import time


def run(core, out, steps, scale=1):
    for step in steps:
        kind, _, arg = step.partition(':')
        if kind == 'key':
            core.key(out, arg)
        elif kind == 'tap':
            x, y = (int(v) * scale for v in arg.split(','))
            core.tap(out, x, y)
        elif kind == 'wait':
            time.sleep(float(arg))
        elif kind == 'snap':
            core.frame(out / f'{arg}.png')
        else:
            raise SystemExit(f'unknown step {step}')
