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
    3: dict(sky=((110, 150, 206), (232, 226, 226)), far=('volcano', (70, 104, 140), 3),
            grass=(98, 158, 70), rough=(64, 116, 50),
            items=[(mkstyle.OBJ_LANDMARK, 0, 172, 30, 22), (OBJ_TREE, 0, 26, 42, 22),
                   (OBJ_PINE, 0, 196, 46, 18), (OBJ_TREE, 1, 58, 33, 10)]),
    4: dict(sky=((80, 140, 214), (236, 232, 218)), far=('sandhills', (120, 136, 150), 9),
            grass=(126, 164, 76), rough=(96, 110, 60),
            green=(146, 34, 24, 4), bunkers=[(138, 39, 20, 3.5), (170, 35, 9, 2.5), (62, 36, 12, 2.5)],
            items=[(OBJ_TREE, 0, 24, 42, 26), (mkstyle.OBJ_LANDMARK, 0, 174, 30, 18),
                   (OBJ_TREE, 1, 196, 46, 22), (mkstyle.OBJ_ANIMAL, 0, 40, 33, 6),
                   (OBJ_BUSH, 0, 90, 34, 6), (OBJ_TREE, 0, 104, 29, 9)]),
    5: dict(sky=((104, 144, 198), (244, 216, 168)), far=('escarpment', (128, 136, 150), 10),
            grass=(112, 158, 64), rough=(92, 112, 56), river=(37, 2.6),
            items=[(OBJ_TREE, 0, 24, 44, 24), (mkstyle.OBJ_LANDMARK, 0, 194, 34, 20),
                   (OBJ_TREE, 1, 150, 30, 8), (OBJ_TREE, 0, 64, 31, 9), (OBJ_TREE, 0, 120, 28, 5),
                   (mkstyle.OBJ_ANIMAL, 1, 82, 38, 5), (mkstyle.OBJ_ANIMAL, 0, 132, 41, 3)]),
    6: dict(sky=((40, 90, 180), (190, 210, 230)), far=('rockies', (110, 120, 136), 22),
            grass=(86, 154, 66), rough=(46, 92, 44), river=(41, 1.8),
            items=[(OBJ_PINE, 0, 16, 48, 30), (OBJ_PINE, 1, 196, 50, 32), (OBJ_PINE, 0, 182, 34, 12),
                   (OBJ_PINE, 1, 36, 32, 10), (OBJ_PINE, 0, 124, 30, 8), (OBJ_PINE, 1, 84, 30, 8),
                   (OBJ_TREE, 0, 168, 39, 9), (mkstyle.OBJ_LANDMARK, 0, 148, 29, 14),
                   (mkstyle.OBJ_ANIMAL, 0, 54, 38, 5)]),
    7: dict(sky=((87, 120, 166), (211, 214, 223)), far=('hills', (89, 112, 96), 10),
            grass=(98, 153, 65), rough=(113, 108, 75), bunkers=[(152, 38, 16, 3)],
            items=[(OBJ_TREE, 0, 24, 47, 29), (OBJ_TREE, 0, 183, 38, 20),
                   (OBJ_BUSH, 0, 198, 55, 9), (OBJ_BUSH, 0, 34, 53, 9),
                   (mkstyle.OBJ_HOUSE, 0, 66, 31, 8)]),
    8: dict(sky=((87, 149, 215), (241, 221, 181)), far=('sandhills', (172, 151, 125), 10),
            grass=(83, 154, 66), rough=(220, 184, 122), river=(36, 1.9),
            items=[(mkstyle.OBJ_PALM, 0, 24, 46, 30), (mkstyle.OBJ_PALM, 0, 179, 37, 20),
                   (mkstyle.OBJ_PALM, 0, 193, 40, 24), (OBJ_ROCK, 0, 40, 54, 8)]),
    9: dict(sky=((54, 114, 195), (210, 226, 237)), far=('cliff', (68, 115, 158), 4),
            grass=(100, 155, 64), rough=(124, 126, 72),
            items=[(OBJ_TREE, 0, 22, 43, 25), (OBJ_PINE, 0, 181, 41, 24),
                   (mkstyle.OBJ_WALL, 0, 178, 53, 8), (mkstyle.OBJ_HOUSE, 0, 141, 31, 11)]),
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
    if kind in ('sea', 'cliff', 'volcano'):
        top_y = np.full(W, HORIZON - bh)
    elif kind == 'peak':
        top_y = HORIZON - bh * (0.15 + 0.85 * prof ** 1.3)
    else:
        top_y = HORIZON - bh * (0.4 + 0.6 * prof)
    band = (y >= top_y[None, :]) & (y < HORIZON) & (kind != 'rockies')
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
    elif kind in ('sea', 'cliff', 'volcano'):
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
    elif kind == 'volcano':
        # a snow-capped volcano across the bay: concave sides, a flat top,
        # lit on the left, snow down the gullies, low wooded hills at its foot
        xs = np.arange(W)
        cx = 92
        cone = np.clip(1 - np.abs(xs - cx) / 74, 0, 1) ** 1.7
        cone = np.minimum(cone, 0.8)
        vtop = HORIZON - bh - cone * 27
        body = (y >= vtop[None, :]) & (y < HORIZON - bh) & (cone[None, :] > 0.01)
        side = np.clip((cx - xs) / 30, -1, 1)[None, :]
        body_c = mix((96, 112, 140), haze, 0.3)
        img = np.where(body[..., None], body_c[None, None] * (0.9 + 0.18 * side[..., None]), img)
        tongue = np.clip(1 - np.abs(np.sin((xs - cx) * 0.55)), 0, 1) ** 3
        sline = HORIZON - bh - 0.8 * 27 + 0.8 * 27 * (0.3 + 0.3 * tongue * mkobj.vnoise((1, W), 6, 730 + s)[0])
        snowy = body & (y < sline[None, :])
        img = np.where(snowy[..., None], mix((240, 242, 248), haze, 0.15)[None, None] * (0.9 + 0.1 * side[..., None]), img)
        hp = mkobj.vnoise((1, W), 14, 740 + s)[0]
        htop = HORIZON - bh - 1 - 4 * hp - 3 * np.clip(1 - np.abs(xs - 175) / 50, 0, 1)
        hills = (y >= htop[None, :]) & (y < HORIZON - bh)
        img[hills] = mix((50, 86, 60), haze, 0.35)
        lab[body | hills] = FAR
    elif kind == 'sandhills':
        # far low ranges faint in the haze, low sandy hills patched with
        # scrub in front, and streaks of heat shimmer across them
        img[band] = far
        xs = np.arange(W)
        hp = mkobj.vnoise((1, W), 18, 750 + s)[0]
        htop = HORIZON - 1 - 4 * hp
        hills = (y >= htop[None, :]) & (y < HORIZON)
        sand_c = mix((214, 190, 140), haze, 0.3)
        shade = 0.92 + 0.12 * (mkobj.vnoise((H, W), 3, 760 + s) - 0.5)
        img = np.where(hills[..., None], sand_c[None, None] * shade[..., None], img)
        scrub = hills & (mkobj.vnoise((H, W), 2, 770 + s) > 0.6)
        img[scrub] = mix((110, 112, 70), haze, 0.3)
        shimmer = (y >= HORIZON - bh + 2) & (y < HORIZON) & ((y % 2) == 0) & \
                  (mkobj.vnoise((H, W), 9, 780 + s) > 0.62 + 0 * xs[None, :])
        img[shimmer] = mix(img[shimmer], np.asarray(haze, float), 0.6)
        lab[hills] = FAR
    elif kind == 'escarpment':
        # a far escarpment on the left, flat on top with a rust cliff band
        # under its rim, a granite koppie on the right, low bush between
        # with flat-topped acacias standing out of it
        xs = np.arange(W)
        esc = np.clip((xs - 6) / 8, 0, 1) * np.clip((126 - xs) / 7, 0, 1)
        etop = HORIZON - 1 - esc * (bh + 0.6 * np.sin(xs * 0.17))
        e_m = (y >= etop[None, :]) & (y < HORIZON) & (esc[None, :] > 0.05)
        img[e_m] = far
        cliff = e_m & (y < (etop + (HORIZON - etop) * 0.4)[None, :])
        img[cliff] = mix((176, 112, 72), haze, 0.35)[None, :] * np.broadcast_to(0.94 + 0.1 * (y % 2 == 0), (H, W))[cliff][:, None]
        kop = np.clip(1 - np.abs(xs - 152) / 22, 0, 1) ** 0.6
        ktop = HORIZON - 1 - kop * 6
        k_m = (y >= ktop[None, :]) & (y < HORIZON) & (kop[None, :] > 0.05)
        shade = 0.9 + 0.25 * (mkobj.vnoise((H, W), 2, 790 + s) - 0.5) + np.clip((152 - x) / 60, -0.15, 0.15)
        img = np.where(k_m[..., None], mix((150, 146, 136), haze, 0.3)[None, None] * shade[..., None], img)
        bush = (y >= HORIZON - 1 - (mkobj.vnoise((1, W), 3, 795 + s)[0] > 0.6)[None, :]) & (y < HORIZON)
        img[bush] = mix((70, 84, 48), haze, 0.35)
        for ax in r.choice(np.arange(4, W - 4), 16, replace=False):
            cy = HORIZON - 3 - int(r.integers(0, 2))
            img[cy, ax - 2:ax + 3] = mix((44, 70, 34), haze, 0.3)
            img[cy + 1:HORIZON, ax] = mix((70, 56, 40), haze, 0.3)
            lab[cy:HORIZON, ax - 2:ax + 3] = FAR
        lab[e_m | k_m | bush] = FAR
    elif kind == 'rockies':
        # snow-capped peaks: a horn on the left of centre, a long slab
        # mountain on the right, a pale far range between, lit from the
        # left with the faces right of each ridge in shade, wooded foothills
        # and a dark band of spruce along the bottom
        xs = np.arange(W)
        far_t = HORIZON - 9 - 4 * mkobj.vnoise((1, W), 12, 801 + s)[0] - 2 * np.abs(np.sin(xs * 0.31))
        back = (y >= far_t[None, :]) & (y < HORIZON)
        img[back] = mix((150, 166, 180), haze, 0.45)
        peaks = [(70, 30, 30, bh, 1.15), (150, 20, 46, bh * 0.75, 0.9), (10, 16, 20, bh * 0.55, 1.0),
                 (204, 14, 16, bh * 0.6, 1.0)]
        prof = np.zeros(W)
        lit = np.zeros((H, W), dtype=bool)
        own = np.full(W, -1)
        for k, (cx, hl, hr, ph, sh) in enumerate(peaks):
            dd = np.where(xs < cx, (cx - xs) / hl, (xs - cx) / hr)
            v = ph * np.clip(1 - dd, 0, 1) ** sh
            own = np.where(v > prof, k, own)
            prof = np.maximum(prof, v)
        # the slab mountain's long flat back
        slab = np.where((xs > 112) & (xs < 172), bh * (0.5 + 0.22 * (xs - 112) / 60
                                                         + 0.08 * mkobj.vnoise((1, W), 4, 805 + s)[0]), 0)
        own = np.where(slab > prof, 1, own)
        prof = np.maximum(prof, slab)
        prof = prof + (mkobj.vnoise((1, W), 2, 802 + s)[0]) * 1.2 * (prof > 2)
        mt = HORIZON - 3 - prof
        body = (y >= mt[None, :]) & (y < HORIZON) & (prof[None, :] > 0.5)
        for k, (cx, hl, hr, ph, sh) in enumerate(peaks):
            bx = cx + (y - (HORIZON - 3 - ph)) * 0.35
            lit |= (own[None, :] == k) & (x < bx)
        lit = np.where((own == 1)[None, :] & (x < 166), True, lit)
        rock = np.where(lit[..., None], mix((128, 134, 146), haze, 0.2)[None, None],
                        mix((70, 78, 96), haze, 0.15)[None, None])
        img = np.where(body[..., None], rock, img)
        alt = (HORIZON - 3 - y) / bh
        gul = mkobj.vnoise((H, W), 3, 803 + s)
        snowy = body & (alt[..., None][..., 0] > 0.42 + gul * 0.3 - (np.abs(np.sin(xs * 0.5)) ** 6 * 0.15)[None, :])
        img = np.where((snowy & lit)[..., None], np.asarray((246, 248, 252), float)[None, None], img)
        img = np.where((snowy & ~lit)[..., None], np.asarray((186, 196, 214), float)[None, None], img)
        foot_t = HORIZON - 3 - 2.5 * mkobj.vnoise((1, W), 10, 804 + s)[0] - 1.2
        foot = (y >= foot_t[None, :]) & (y < HORIZON)
        img[foot] = mix((44, 74, 50), haze, 0.3)
        sp = np.zeros(W)
        for cx in np.arange(1, W, 2.5) + r.uniform(-0.6, 0.6, len(np.arange(1, W, 2.5))):
            th = r.uniform(1.5, 3.5)
            sp = np.maximum(sp, th * np.clip(1 - np.abs(xs - cx) / 1.8, 0, 1))
        spruce = (y >= (HORIZON - 1 - sp)[None, :]) & (y < HORIZON)
        img[spruce] = mix((24, 50, 32), haze, 0.15)
        lab[back | body | foot | spruce] = FAR
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
    # a river winding across the ground (middle row, half thickness), with
    # a light glint along it
    if st.get('river'):
        ry, rt = st['river']
        mid = ry + 1.6 * np.sin(x / 23.0 + 0.8)
        wet = (np.abs(y - mid) < rt * (0.8 + 0.4 * (y - HORIZON) / (H - HORIZON))) & ground
        img = np.where(wet[..., None], np.asarray(mix((52, 92, 132), haze, 0.25))[None, None] *
                       (1 + 0.12 * ((y - mid) < -rt * 0.4))[..., None], img)
        bankd = wet & ((y - mid) > rt * 0.5)
        img[bankd] = img[bankd] * 0.85
        lab[wet] = FAR
    # a green (x, y, half width, half height), then big ragged bunkers
    # with a dark lip, cut into it
    if st.get('green'):
        gx_, gy_, grx_, gry_ = st['green']
        on = (np.hypot((x - gx_) / grx_, (y - gy_) / gry_) < 1) & ground
        img = np.where(on[..., None], np.asarray(mix((140, 196, 96), haze, 0.15))[None, None] *
                       (1 + 0.06 * ((x // 6 + y // 2) % 2 == 0))[..., None], img)
        lab[on] = FAIRWAY
    for k, (bx, by, rx, ry) in enumerate(st.get('bunkers', [])):
        a = np.arctan2((y - by) / ry, (x - bx) / rx)
        rag = (1 + 0.16 * np.sin(a * 3 + k) + 0.1 * np.sin(a * 7 + 2 * k)
               + 0.15 * mkobj.vnoise((H, W), 2, 800 + 10 * s + k))
        dd = np.hypot((x - bx) / rx, (y - by) / ry) / rag
        lip = (dd < 1.25) & ground & (y > by - ry * 0.4)
        sand = (dd < 1.0) & ground
        img[lip] = img[lip] * 0.6
        lit = 0.95 + 0.12 * (y - by) / ry
        img = np.where(sand[..., None], np.asarray((236, 222, 180), float)[None, None] * lit[..., None], img)
        lab[lip] = ROUGH
        lab[sand] = FAIRWAY
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
