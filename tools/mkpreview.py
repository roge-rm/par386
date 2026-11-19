#!/usr/bin/env python3
"""Course select previews: a small view down a fairway for each scenery
number, painted with light and haze and dressed with that course style's
sprites (tools/mkstyle.py). Imported by tools/mkart.py, which cuts them to
the palettes and packs them as Pic2/Pic8 PREVIEW_ID + 10 * scenery.

  make() -> {scenery: (RGB image W x H, labels)}, at the 320 screen's size;
  the 160 one is half of it. labels is an H x W array of SKY, FAR, ROUGH,
  FAIRWAY or OBJECT, for cutting the 4-grey version by region.

"""
import numpy as np
from PIL import Image

import mkobj
import mkstyle
from mkobj import OBJ_TREE, OBJ_PINE, OBJ_BUSH, OBJ_ROCK

W, H = 208, 64      # pixels on a 320 screen, 104 x 32 logical units
HORIZON = 26
SKY, FAR, ROUGH, FAIRWAY, OBJECT = range(5)

# Per scenery: sky top and horizon colours, the distant band (kind, colour,
# height), grass, rough and the sprites to stand about as
# (type, variant, x, base y, small height).
STYLES = {
    0: dict(sky=((70, 120, 200), (196, 216, 236)), far=('hills', (70, 110, 70), 7),
            grass=(92, 160, 64), rough=(52, 104, 40),
            items=[(OBJ_TREE, 0, 20, 38, 22), (OBJ_TREE, 0, 186, 40, 26), (OBJ_PINE, 0, 160, 32, 16),
                   (OBJ_TREE, 0, 52, 30, 12)]),
    1: dict(sky=((92, 130, 186), (214, 222, 228)), far=('sea', (70, 104, 140), 3),
            grass=(120, 158, 74), rough=(130, 128, 70),
            items=[(mkstyle.OBJ_LANDMARK, 0, 168, 28, 16), (OBJ_BUSH, 0, 24, 44, 12),
                   (mkstyle.OBJ_HOUSE, 1, 46, 30, 8), (mkstyle.OBJ_ANIMAL, 0, 186, 46, 6)]),
    2: dict(sky=((84, 104, 140), (196, 204, 212)), far=('cliff', (56, 84, 116), 4),
            grass=(86, 150, 62), rough=(70, 110, 52),
            items=[(mkstyle.OBJ_LANDMARK, 0, 176, 17, 11), (OBJ_TREE, 0, 26, 42, 18),
                   (mkstyle.OBJ_HOUSE, 0, 58, 30, 8), (OBJ_BUSH, 0, 192, 48, 10)]),
    3: dict(sky=((110, 150, 206), (232, 226, 226)), far=('peak', (110, 130, 160), 14),
            grass=(98, 158, 70), rough=(64, 116, 50),
            items=[(mkstyle.OBJ_LANDMARK, 0, 172, 30, 22), (OBJ_TREE, 0, 26, 42, 22),
                   (OBJ_PINE, 0, 196, 46, 18), (OBJ_TREE, 1, 58, 33, 10)]),
    4: dict(sky=((80, 140, 214), (224, 228, 220)), far=('scrub', (96, 112, 84), 5),
            grass=(126, 164, 76), rough=(178, 160, 104),
            items=[(OBJ_TREE, 0, 24, 42, 26), (mkstyle.OBJ_LANDMARK, 0, 166, 30, 18),
                   (OBJ_TREE, 1, 192, 44, 22), (mkstyle.OBJ_ANIMAL, 0, 60, 38, 7)]),
    5: dict(sky=((120, 150, 190), (236, 222, 196)), far=('hills', (120, 112, 80), 6),
            grass=(110, 150, 62), rough=(150, 132, 80),
            items=[(mkstyle.OBJ_LANDMARK, 0, 180, 36, 22), (OBJ_TREE, 0, 26, 40, 16),
                   (mkobj_palm := 3, 0, 60, 32, 12), (mkstyle.OBJ_HOUSE, 0, 148, 30, 7)]),
    6: dict(sky=((40, 90, 180), (180, 204, 228)), far=('peak', (110, 120, 136), 20),
            grass=(82, 150, 66), rough=(46, 96, 46),
            items=[(OBJ_PINE, 0, 22, 44, 28), (OBJ_PINE, 1, 190, 46, 30), (OBJ_PINE, 0, 170, 32, 14),
                   (mkstyle.OBJ_LANDMARK, 0, 56, 29, 10)]),
}


def sprite(scenery, typ, variant, h):
    """The style's maker for (typ, variant), else the shared tree, pine,
    bush or rock, drawn h pixels high."""
    for t, v, maker, _ in mkstyle.SETS.get(scenery, []):
        if t == typ and v == variant:
            return maker(h, 9000 + 100 * scenery + 10 * typ + variant)
    shared = {OBJ_TREE: mkobj.tree, OBJ_PINE: mkobj.pine, OBJ_BUSH: mkobj.bush, OBJ_ROCK: mkobj.rock}
    return shared[typ](h, 9000 + typ)


def mix(a, b, t):
    return np.asarray(a, float) * (1 - t) + np.asarray(b, float) * t


def paint(s):
    st = STYLES[s]
    r = np.random.default_rng(500 + s)
    img = np.zeros((H, W, 3))
    lab = np.full((H, W), SKY, dtype=np.uint8)
    y = np.arange(H)[:, None]
    x = np.arange(W)[None, :]
    top, haze = st['sky']
    # sky: a gradient to the haze at the horizon, a few soft clouds
    t = np.clip(y / HORIZON, 0, 1)[..., None] ** 0.8
    img[:] = mix(top, haze, t)
    # clouds: noise stretched sideways into soft streaks
    clouds = np.asarray(Image.fromarray((mkobj.vnoise((H, W // 4), 6, 600 + s) * 255).astype(np.uint8))
                        .resize((W, H), Image.BICUBIC), float) / 255
    cl = np.clip((clouds - 0.6) * 2, 0, 0.4) * np.clip((HORIZON - 4 - y) / 8, 0, 1)
    img = img * (1 - cl[..., None]) + 245 * cl[..., None]
    # the distant band, hazed toward the sky
    kind, col, bh = st['far']
    prof = mkobj.vnoise((1, W), 26, 700 + s)[0]
    if kind == 'peak':
        prof = np.clip(0.7 * mkobj.vnoise((1, W), 40, 700 + s)[0] + 0.3 * mkobj.vnoise((1, W), 9, 710 + s)[0], 0, 1)
    prof = (prof - prof.min()) / max(1e-6, prof.max() - prof.min())
    if kind in ('sea', 'cliff'):
        top_y = np.full(W, HORIZON - bh)
    elif kind == 'peak':
        top_y = HORIZON - bh * (0.15 + 0.85 * prof ** 1.3)
    else:
        top_y = HORIZON - bh * (0.4 + 0.6 * prof)
    band = (y >= top_y[None, :]) & (y < HORIZON)
    lab[band] = FAR
    far = mix(col, haze, 0.35)
    if kind == 'peak':
        # lit on slopes facing the sun on the left, snow on the high parts
        slope = np.gradient(top_y)[None, :].repeat(H, 0)
        lit = np.clip(0.75 - slope * 0.6, 0.45, 1.1)[..., None]
        rock = mix(far, (40, 48, 64), 0.25)[None, None] * lit
        snowy = (y < np.minimum(top_y[None, :] + 3 + 2 * prof[None, :], HORIZON - bh * 0.45)) & band
        img = np.where(band[..., None], rock, img)
        img = np.where(snowy[..., None], mix((236, 240, 248), haze, 0.25)[None, None] * np.clip(lit, 0.8, 1.05), img)
    elif kind in ('sea', 'cliff'):
        img[band] = far
        glint = band & (y > top_y[None, :]) & (r.random((H, W)) > 0.9)
        img[glint] = mix(far, (230, 236, 240), 0.5)
    if kind == 'cliff':
        # a headland across the bay on the right: grass on top over a sheer
        # rock face down to the sea, the lighthouse on its point
        xs = np.arange(W)
        rise = np.clip((xs - 112) / 30, 0, 1) ** 0.7 * (xs < 190)
        ctop = HORIZON - 1 - rise * (9 + 1.2 * np.sin(xs * 0.21))
        head = (y >= ctop[None, :]) & (y < HORIZON) & (rise[None, :] > 0.02)
        face = head & (y >= (ctop + (HORIZON - ctop) * 0.4)[None, :])
        grass_c = mix((70, 104, 66), haze, 0.3)
        rock_c = mix((120, 116, 108), haze, 0.25)
        img[head] = grass_c
        shade = 0.85 + 0.25 * (mkobj.vnoise((H, W), 3, 720 + s) - 0.5) - ((y % 3) == 0) * 0.08
        img = np.where(face[..., None], rock_c[None, None] * shade[..., None], img)
        lab[head] = FAR
    else:
        img[band] = mix(far, haze, 0.2 * (HORIZON - y) / max(1, bh))[band] if False else far
    # ground: rough everywhere, a fairway narrowing to the horizon with mown
    # stripes, light falling off and haze near the horizon
    d = np.clip((y - HORIZON) / (H - HORIZON), 0, 1)
    ground = y >= HORIZON
    half = 6 + 90 * d ** 1.2
    fw = np.abs(x - (W / 2 + 10 * (1 - d))) < half
    rough = mix(st['rough'], st['grass'], 0)
    g = np.where(fw[..., None], np.asarray(st['grass'], float), rough)
    stripe = (np.floor(1.0 / (d + 0.08) * 3) % 2 == 0)[..., None] & fw[..., None]
    g = g * np.where(stripe, 1.08, 0.94)
    g = g * (0.85 + 0.25 * d)[..., None]
    g = g + ((mkobj.vnoise((H, W), 2, 900 + s) - 0.5) * 18)[..., None]
    g = mix(haze, 0, 0)[None, None] * 0 + g
    hz = np.clip(1 - d * 3, 0, 1)[..., None] * 0.45
    g = g * (1 - hz) + np.asarray(haze, float) * hz
    img = np.where(ground[..., None], g, img)
    lab[np.broadcast_to(ground, (H, W))] = ROUGH
    lab[ground & fw] = FAIRWAY
    # the sprites, far ones first, with a little haze on the far ones
    for typ, v, sx, sy, sh in sorted(st['items'], key=lambda it: it[3]):
        idx, ax, ay = sprite(s, typ, v, sh)
        rgb = mkobj.PAL[idx]
        f = np.clip(1 - (sy - HORIZON) / 20, 0, 1) * 0.3
        rgb = rgb * (1 - f) + np.asarray(haze, float) * f
        h, w = idx.shape
        x0, y0 = int(sx - ax), int(sy - ay)
        for j in range(h):
            for i in range(w):
                if idx[j, i] and 0 <= y0 + j < H and 0 <= x0 + i < W:
                    img[y0 + j, x0 + i] = rgb[j, i]
                    lab[y0 + j, x0 + i] = OBJECT
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)), lab


def make():
    return {s: paint(s) for s in STYLES}


if __name__ == '__main__':
    import sys
    from pathlib import Path
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for s, (im, _) in make().items():
        im.resize((W * 3, H * 3), Image.NEAREST).save(out / f'preview-{s}.png')
