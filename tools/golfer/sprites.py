"""Turns the big golfer renders into Palm sprites and debug sheets.

  python3 sprites.py RENDERDIR OUTDIR [BACKGROUND]
RENDERDIR holds f01.png ... and meta.json from render.py. For each sprite
height (20, 40, 60 px for the standing golfer) it writes OUTDIR/<h>/fNN.png:
a paletted PNG with index 0 as the one transparent colour. Also writes
sheet-<h>.png (4x, nearest neighbour) and mock-320.png / mock-160.png over
BACKGROUND (default tools/mockup/r/scene_bg_960.png), whose camera must sit
behind the ball: the ball goes in the centre column on the tee.
"""
import os, sys, glob, json
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
src, out = sys.argv[1], sys.argv[2]
BG = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, '..', 'mockup', 'r', 'scene_bg_960.png')
os.makedirs(out, exist_ok=True)
meta = json.load(open(os.path.join(src, 'meta.json')))
paths = sorted(glob.glob(os.path.join(src, 'f[0-9][0-9].png')))
big = [np.asarray(Image.open(p).convert('RGBA')).astype(np.float64) / 255 for p in paths]
# club alone (render.py), so the thin shaft survives the shrink
clubs = [np.asarray(Image.open(p.replace('/f', '/c')).convert('RGBA')).astype(np.float64) / 255 for p in paths]

SIZES = {20: 16, 40: 32, 60: 48}     # sprite height of the standing golfer: palette size
KEY = (255, 0, 255)                   # transparent colour, palette index 0
rng = np.random.default_rng(386)

# common crop over all frames, so every frame shares the same feet anchor
a_all = np.max([b[..., 3] for b in big + clubs], axis=0)
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


def grain(rgb, mask, amount):
    """Film/video grain: mostly luminance, a little chroma."""
    lum = rng.normal(0, amount, rgb.shape[:2])[..., None]
    chroma = rng.normal(0, amount * 0.35, rgb.shape)
    return np.where(mask[..., None], np.clip(rgb + lum + chroma, 0, 1), rgb)


def palettise(frames, ncol):
    """One palette for all frames of a size; index 0 is the key colour."""
    opaque = np.concatenate([f[f[..., 3] > 0.4][:, :3] for f in frames])
    strip = Image.fromarray((opaque[None] * 255).astype(np.uint8), 'RGB')
    pal_img = strip.quantize(ncol - 1, method=Image.Quantize.MEDIANCUT)
    pal = pal_img.getpalette()[:3 * (ncol - 1)]
    full = list(KEY) + pal
    ref = Image.new('P', (1, 1)); ref.putpalette(full + [0] * (768 - len(full)))
    outs = []
    for f in frames:
        rgb = Image.fromarray((f[..., :3] * 255).astype(np.uint8), 'RGB')
        # map to colours 1.. only (never the key), no dither: the grain does that job
        q = np.asarray(rgb.quantize(palette=ref, dither=Image.Dither.NONE)).copy()
        pal_arr = np.array(full).reshape(-1, 3)
        if (q == 0).any():
            rgbv = np.asarray(rgb)[q == 0].astype(int)
            d = ((rgbv[:, None, :] - pal_arr[None, 1:, :]) ** 2).sum(-1)
            q[q == 0] = d.argmin(1) + 1
        q[f[..., 3] <= 0.4] = 0
        im = Image.fromarray(q.astype(np.uint8), 'P'); im.putpalette(full + [0] * (768 - len(full)))
        im.info['transparency'] = 0
        outs.append(im)
    return outs


sprites = {}
# club alpha above which a sprite pixel becomes club, and contrast boost, per size
CLUB_A = {20: 0.08, 40: 0.10, 60: 0.14}


def thin(a):
    """Pixels that peak across the line: a ridge one pixel wide."""
    p = np.pad(a, 1)
    c = p[1:-1, 1:-1]
    row = (c >= p[1:-1, :-2]) & (c > p[1:-1, 2:])
    col = (c >= p[:-2, 1:-1]) & (c > p[2:, 1:-1])
    return row | col
CONTRAST = {20: 1.25, 40: 1.0, 60: 1.0}

for h, ncol in SIZES.items():
    scale = h / stand_h
    frames = []
    for b, cl in zip(big, clubs):
        c = b[y0:y1, x0:x1]
        s = shrink(c, scale)
        s[..., :3] = np.clip((s[..., :3] - 0.45) * CONTRAST[h] + 0.45, 0, 1)
        k = shrink(cl[y0:y1, x0:x1], scale)
        on = thin(k[..., 3]) & (k[..., 3] > CLUB_A[h])
        s[on] = np.concatenate([k[on][:, :3], np.ones((on.sum(), 1))], 1)
        s[..., :3] = grain(s[..., :3], s[..., 3] > 0.4, 0.035 if h >= 40 else 0.02)
        frames.append(s)
    pims = palettise(frames, ncol)
    d = os.path.join(out, str(h)); os.makedirs(d, exist_ok=True)
    for i, im in enumerate(pims):
        im.save(os.path.join(d, f'f{i + 1:02d}.png'), transparency=0)
    # feet anchor inside the sprite, in sprite pixels
    anchor = (round(((x0 + x1) / 2 - x0) * scale), round((feet_y - y0) * scale))
    ball = ((meta['ball'][0] - x0) * scale, (meta['ball'][1] - y0) * scale)
    sprites[h] = (pims, anchor, ball)

    # contact sheet, 4x nearest neighbour, on a grass green
    w, hh = pims[0].size
    pad = 4
    sheet = Image.new('RGB', ((w + pad) * len(pims) + pad, hh + 2 * pad), (84, 140, 60))
    for i, im in enumerate(pims):
        sheet.paste(im.convert('RGBA'), (pad + i * (w + pad), pad), im.convert('RGBA'))
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(os.path.join(out, f'sheet-{h}.png'))
    print(h, 'px: sprite', pims[0].size, 'colours', ncol, 'anchor', anchor)


# ------------------------------------------------------------ in-game mock
def mock(n, h):
    """Scene shrunk to n x n and quantized like compose.py, address golfer on the tee."""
    scene = Image.open(BG).convert('RGB')
    bg = scene.resize((n, n), Image.LANCZOS)
    pims, anchor, ball = sprites[h]
    spr = pims[0]
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
    ncol = 256 - SIZES[h]
    bg = bg.quantize(min(ncol, 224), dither=Image.Dither.FLOYDSTEINBERG).convert('RGB')
    # the ball
    d = ImageDraw.Draw(bg)
    r = max(0, n // 320)
    d.rectangle([bx - r, tee_y - r, bx, tee_y], fill=(250, 250, 250))
    rgba = spr.convert('RGBA')
    bg.paste(rgba, (ox, oy), rgba)
    return bg


for n, h in ((320, 40), (160, 20)):
    m = mock(n, h)
    m.save(os.path.join(out, f'mock-{n}.png'))
    m.resize((n * (4 if n == 160 else 2),) * 2, Image.NEAREST).save(os.path.join(out, f'mock-{n}-big.png'))
print('done', out)
