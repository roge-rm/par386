"""Turns the big golfer renders into Palm sprites and debug sheets.

  python3 sprites.py RENDERDIR OUTDIR [BACKGROUND] [--female]
RENDERDIR holds f01.png ... and meta.json from render.py. For each sprite
height (20, 40, 60 px for the standing golfer, and 80 for play and the title at 320)
it writes ART/fNN-<h>.png in the repo's art/golfer/: the frame cut straight
to the golfer ramps of the master palette (tools/mkpal.py), saved as a
paletted PNG with the master palette and index 0 clear. All frames of a size
share one canvas, so the feet and the ball sit at the same place in each.
OUTDIR gets sheet-<h>.png (4x, nearest neighbour) and mock-320.png /
mock-160.png over BACKGROUND (default tools/mockup/r/scene_bg_960.png), whose
camera must sit behind the ball: the ball goes in the centre column on the tee.
"""
import os, sys, glob, json
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import mkpal   # noqa: E402

args = sys.argv[1:]
female = args[-1:] == ['--female']
if female:
    args.pop()
src, out = args[:2]
BG = args[2] if len(args) > 2 else os.path.join(HERE, '..', 'mockup', 'r', 'scene_bg_960.png')
ART = os.path.join(ROOT, 'art', 'golfer', 'female' if female else '')
os.makedirs(out, exist_ok=True)
os.makedirs(ART, exist_ok=True)
meta = json.load(open(os.path.join(src, 'meta.json')))
paths = sorted(glob.glob(os.path.join(src, 'f[0-9][0-9].png')))
frame_nums = [int(os.path.basename(path)[1:3]) for path in paths]
if female and frame_nums != [1, 2, 4, 8, 13]:
    raise SystemExit('Render female frames 1,2,4,8,13')
big = [np.asarray(Image.open(p).convert('RGBA')).astype(np.float64) / 255 for p in paths]
# club alone (render.py), so the thin shaft survives the shrink
clubs = [np.asarray(Image.open(os.path.join(src, f'c{n:02d}.png')).convert('RGBA')).astype(np.float64) / 255
         for n in frame_nums]

SIZES = (40, 80) if female else (20, 40, 60, 80)
TITLE_ONLY = {} if female else {80: (1, 4, 13)}
# frames made after those at a size, with their own grain, so the others stay as they were
LATE = {40: (8,), 80: (8,)} if female else {80: (2, 8)}
rng = np.random.default_rng(386)

RGB, RAMPS, _, _ = mkpal.build()
PAL = np.array(RGB, dtype=np.float64)
# the golfer ramps and black; never index 0, which is clear
ALLOWED = []
for name in ('skin', 'shirt', 'trousers', 'cap', 'shoes', 'club', 'grey'):
    s0, k = RAMPS[name]
    ALLOWED += range(s0, s0 + k)
ALLOWED.append(255)
ALLOWED = np.array(ALLOWED)
# the same weights as mkpal.nearest
WEIGHT = np.array([3.0, 4.0, 2.0])

# common crop over all frames, so every frame shares the same feet anchor
crop = [i for i, n in enumerate(frame_nums) if not female or n != 8]
a_all = np.max([b[i][..., 3] for b in (big, clubs) for i in crop], axis=0)
ys, xs = np.nonzero(a_all > 0.02)
y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
a0 = big[0][..., 3]
ay = np.nonzero(a0.max(1) > 0.4)[0]
stand_h = ay.max() - ay.min() + 1      # address pose height in render pixels
feet_y = ay.max() + 1


def shrink(rgba, scale):
    """Area-filter shrink in linear light with premultiplied alpha."""
    lin = rgba.copy()
    lin[..., :3] = lin[..., :3] ** 2.2 * lin[..., 3:4]
    h, w = lin.shape[:2]
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    chans = [np.asarray(Image.fromarray(lin[..., c].astype(np.float32), 'F').resize((nw, nh), Image.BOX))
             for c in range(4)]
    s = np.stack(chans, -1)
    a = s[..., 3:4]
    rgb = np.where(a > 1e-4, s[..., :3] / np.maximum(a, 1e-4), 0) ** (1 / 2.2)
    return np.concatenate([np.clip(rgb, 0, 1), a], -1)


def grain(rgb, mask, amount, g=None):
    """Film/video grain: mostly luminance, a little chroma."""
    g = g or rng
    lum = g.normal(0, amount, rgb.shape[:2])[..., None]
    chroma = g.normal(0, amount * 0.35, rgb.shape)
    return np.where(mask[..., None], np.clip(rgb + lum + chroma, 0, 1), rgb)


CLUB = np.arange(RAMPS['club'][0], RAMPS['club'][0] + RAMPS['club'][1])


def master(f, club):
    """A frame cut to the golfer ramps of the master palette, no dither (the
    grain does that job); 0 where it's clear. The shaft's pixels (club) go
    to the club ramp, which 4-grey screens show black."""
    rgb = f[..., :3] * 255
    d = (((rgb[..., None, :] - PAL[ALLOWED][None, None]) ** 2) * WEIGHT).sum(-1)
    idx = ALLOWED[d.argmin(-1)].astype(np.uint8)
    dc = (((rgb[..., None, :] - PAL[CLUB][None, None]) ** 2) * WEIGHT).sum(-1)
    idx[club] = CLUB[dc.argmin(-1)][club]
    idx[f[..., 3] <= 0.4] = 0
    return idx


def to_png(idx):
    im = Image.fromarray(idx, 'P')
    im.putpalette([c for rgb in RGB for c in rgb])
    im.info['transparency'] = 0
    return im


sprites = {}
# club alpha above which a sprite pixel becomes club, per size
CLUB_A = {20: 0.07, 40: 0.10, 60: 0.14, 80: 0.16}


def thin(a):
    """Pixels that peak across the line: a ridge one pixel wide."""
    p = np.pad(a, 1)
    c = p[1:-1, 1:-1]
    row = (c >= p[1:-1, :-2]) & (c > p[1:-1, 2:])
    col = (c >= p[:-2, 1:-1]) & (c > p[2:, 1:-1])
    return row | col
# contrast round mid grey, more on the small sizes so the shapes read
CONTRAST = {20: 1.35, 40: 1.05, 60: 1.0, 80: 1.0}

for h in SIZES:
    scale = h / stand_h
    frames = [None] * len(big)
    late = LATE.get(h, ())
    order = [i for i, n in enumerate(frame_nums) if n not in late] + [frame_nums.index(n) for n in late]
    late_rng = np.random.default_rng(386 + h)
    for n in order:
        b, cl = big[n], clubs[n]
        frame = frame_nums[n]
        if h in TITLE_ONLY and frame not in TITLE_ONLY[h] and frame not in late:
            continue
        c = b[y0:y1, x0:x1]
        s = shrink(c, scale)
        s[..., :3] = np.clip((s[..., :3] - 0.45) * CONTRAST[h] + 0.45, 0, 1)
        k = shrink(cl[y0:y1, x0:x1], scale)
        on = thin(k[..., 3]) & (k[..., 3] > CLUB_A[h])
        # the shaft as a light line; at 20 px it can't be thinner than a pixel, so it's kept light to stand off the body
        col = k[on][:, :3] if h > 20 else np.maximum(k[on][:, :3], 0.62)
        s[on] = np.concatenate([col, np.ones((on.sum(), 1))], 1)
        s[..., :3] = grain(s[..., :3], s[..., 3] > 0.4, 0.035 if h >= 40 else 0.02, late_rng if frame in late else None)
        frames[n] = master(s, on)
    for frame, idx in zip(frame_nums, frames):
        if idx is not None:
            to_png(idx).save(os.path.join(ART, f'f{frame:02d}-{h}.png'), transparency=0)
    # feet anchor inside the sprite, in sprite pixels
    anchor = (round(((x0 + x1) / 2 - x0) * scale), round((feet_y - y0) * scale))
    ball = ((meta['ball'][0] - x0) * scale, (meta['ball'][1] - y0) * scale)
    sprites[h] = (frames, anchor, ball)
    shown = [f for f in frames if f is not None]

    # contact sheet, 4x nearest neighbour, on a grass green
    hh, w = shown[0].shape
    pad = 4
    sheet = Image.new('RGB', ((w + pad) * len(shown) + pad, hh + 2 * pad), (84, 140, 60))
    for i, idx in enumerate(shown):
        rgba = to_png(idx).convert('RGBA')
        sheet.paste(rgba, (pad + i * (w + pad), pad), rgba)
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(os.path.join(out, f'sheet-{h}.png'))
    print(h, 'px: sprite', (w, hh), 'anchor', anchor)


# ------------------------------------------------------------ in-game mock
def mock(n, h):
    """Scene shrunk to n x n and quantized like compose.py, address golfer on the tee."""
    scene = Image.open(BG).convert('RGB')
    bg = scene.resize((n, n), Image.LANCZOS)
    frames, anchor, ball = sprites[h]
    spr = to_png(frames[0])
    # tee is at 40 yards: row YH + F * CAMH / 40 in render.py, ball on the tee centre
    tee_y = (0.42 + 13.0 / 40) * n
    bx = n * 0.5
    ox = round(bx - ball[0]); oy = round(tee_y - ball[1])
    # soft shadow, cast away from the morning sun (to the right and back)
    sh = Image.new('L', (n * 4, n * 4))
    fx, fy = (ox + anchor[0]) * 4, (oy + anchor[1]) * 4
    ImageDraw.Draw(sh).ellipse([fx - h * 0.9, fy - h * 0.22, fx + h * 1.9, fy + h * 0.30], fill=150)
    sh = sh.filter(ImageFilter.GaussianBlur(h * 0.18)).resize((n, n), Image.BOX)
    arr = np.asarray(bg).astype(np.float64) * (1 - 0.6 * np.asarray(sh)[..., None] / 255)
    bg = Image.fromarray(arr.astype(np.uint8))
    bg = bg.quantize(224, dither=Image.Dither.FLOYDSTEINBERG).convert('RGB')
    # the ball
    d = ImageDraw.Draw(bg)
    r = max(0, n // 320)
    d.rectangle([bx - r, tee_y - r, bx, tee_y], fill=(250, 250, 250))
    rgba = spr.convert('RGBA')
    bg.paste(rgba, (ox, oy), rgba)
    return bg


mock_sizes = ((320, 80), (160, 40)) if female else ((320, 40), (160, 20))
for n, h in mock_sizes:
    m = mock(n, h)
    m.save(os.path.join(out, f'mock-{n}.png'))
    m.resize((n * (4 if n == 160 else 2),) * 2, Image.NEAREST).save(os.path.join(out, f'mock-{n}-big.png'))
print('done', out)
