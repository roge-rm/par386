"""Offline render of the Par 386 title scene, for the art mockup."""
import math, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

R = int(sys.argv[2]) if len(sys.argv) > 2 else 960
CAMX = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0     # camera sideways from the tee centre, yards
OUT = sys.argv[1]
rng = np.random.default_rng(7)
PERM = np.concatenate([rng.permutation(256)] * 2)
GRAD = rng.random(256)

def vnoise(x, z):
    xi = np.floor(x).astype(np.int64); zi = np.floor(z).astype(np.int64)
    fx = x - xi; fz = z - zi
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz)
    def h(i, j): return GRAD[PERM[(PERM[i & 255] + j) & 255]]
    a = h(xi, zi); b = h(xi + 1, zi); c = h(xi, zi + 1); d = h(xi + 1, zi + 1)
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz

def fbm(x, z, oct=5):
    s = 0; a = 0.5; f = 1.0; n = 0
    for i in range(oct):
        s = s + a * vnoise(x * f + i * 17.3, z * f - i * 9.1); n += a; a *= 0.5; f *= 2.03
    return s / n

def mix(a, b, t):
    t = np.asarray(t)[..., None] if np.ndim(t) else t
    return a + (b - a) * t

def C(*v): return np.array(v, dtype=np.float64) / 255.0

W = H = R
YH = int(0.42 * R)          # horizon row
CX = W / 2
F = 1.0 * R                # focal length in pixels
CAMH = 13.0                  # camera height, yards
SUN = np.array([-0.55, 0.62, -0.56]); SUN /= np.linalg.norm(SUN)

img = np.zeros((H, W, 3))
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)

# ---------------- sky ----------------
t = yy[:YH + 2] / YH
sky_stops = [C(26, 54, 132), C(46, 92, 178), C(92, 142, 214), C(158, 190, 232), C(226, 222, 226), C(250, 220, 190)]
def ramp(t, stops):
    n = len(stops) - 1; t = np.clip(t, 0, 1) * n
    i = np.minimum(np.floor(t).astype(int), n - 1); f = (t - i)[..., None]
    S = np.array(stops)
    return S[i] * (1 - f) + S[i + 1] * f
sky = ramp(t ** 1.25, sky_stops)
# cumulus: stretched fbm, lit from the top left
nx = xx[:YH + 2] / R * 5.0; ny = yy[:YH + 2] / R * 11.0
n = fbm(nx, ny, 6)
band = np.exp(-((yy[:YH + 2] / R - 0.36) / 0.09) ** 2) * 0.9 + np.exp(-((yy[:YH + 2] / R - 0.12) / 0.05) ** 2) * 0.35
cov = np.clip((n * band - 0.30) / 0.16, 0, 1)
n2 = fbm(nx - 0.04, ny - 0.10, 6)
lit = np.clip(0.55 + (n2 - n) * 9 + (0.5 - yy[:YH + 2] / R) * 0.2, 0, 1)
cloud = mix(C(196, 198, 222), C(255, 252, 246), lit)
sky = mix(sky, cloud, cov ** 0.8)
img[:YH + 2] = sky[:YH + 2]

# ---------------- distant hills ----------------
cols = np.arange(W) / R
def ridge(base, amp, freq, seed):
    return base - amp * fbm(cols * freq + seed, np.full(W, seed * 0.37), 5)
far = ridge(YH - 0.02 * R, 0.10 * R, 3.0, 3.0)
near = ridge(YH + 0.004 * R, 0.06 * R, 5.0, 11.0)
haze = C(176, 186, 214)
for top, colA, colB, tex in [(far, C(96, 112, 160), C(140, 152, 190), 0.10), (near, C(52, 86, 90), C(96, 124, 136), 0.25)]:
    for x in range(W):
        y0 = int(top[x])
        if y0 >= YH + 4: continue
        ys = np.arange(max(0, y0), YH + 4)
        k = (ys - y0) / max(1, YH + 4 - y0)
        tx = fbm(np.full(len(ys), x / R * 40.0), ys / R * 40.0, 3)
        c = mix(colA, colB, np.clip(k * 0.8 + (tx - 0.5) * tex * 3, 0, 1))
        img[ys, x] = c

# ---------------- ground ----------------
gy = np.arange(YH + 1, H)
Z = F * CAMH / (gy - YH)                        # depth per row
Zg = np.repeat(Z[:, None], W, axis=1)
Xg = (xx[YH + 1:] - CX) * Zg / F + CAMX

def centre(z): return -18 + 0.05 * z
def halfw(z): return np.where((z > 45) & (z < 112), 14 + 3 * np.sin(z / 9.0), np.where(z < 45, 0, 0))
GREEN = (12.0, 176.0)
d_c = Xg - centre(Zg)
fair = np.abs(d_c) < halfw(Zg)
cut = np.abs(d_c) < halfw(Zg) + 3.5
def ell(cx, cz, rx, rz): return ((Xg - cx) / rx) ** 2 + ((Zg - cz) / rz) ** 2
g_e = ell(GREEN[0], GREEN[1], 15, 12)
green = g_e < 1; fringe = g_e < 1.35
bunk = [ell(-6, 172, 5, 6), ell(30, 182, 4, 7), ell(14, 194, 9, 3), ell(-24, 120, 6, 4)]
sand = np.zeros_like(fair); lip = np.zeros_like(fair)
for b in bunk:
    sand |= b < 1; lip |= (b < 1.25) & (b >= 1)
pond_e = ell(22, 140, 40, 17) * (1 + 0.15 * fbm(Xg / 8, Zg / 8, 3))
pond = pond_e < 1
tee = ell(0, 40, 9, 6) < 1

# undulation for shading
def height(x, z): return 1.6 * fbm(x / 45.0, z / 45.0, 4) + 0.5 * fbm(x / 9.0, z / 9.0, 3)
e = 0.6
hx = (height(Xg + e, Zg) - height(Xg - e, Zg)) / (2 * e)
hz = (height(Xg, Zg + e) - height(Xg, Zg - e)) / (2 * e)
nrm = np.stack([-hx, np.ones_like(hx), -hz], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
lam = np.clip(nrm @ SUN, 0, 1)
shade = 0.55 + 0.6 * lam

gn = fbm(Xg / 3.0, Zg / 3.0, 4)          # grass clumps
gf = vnoise(Xg * 1.7, Zg * 1.7)          # fine grass
ground = mix(C(34, 84, 30), C(84, 136, 46), gn * 1.3 - 0.15)                  # rough
ground = np.where(cut[..., None], mix(C(70, 128, 44), C(94, 150, 56), gn), ground)
stripe = (np.floor(Zg / 11.0) % 2)[..., None] * 0.07 - 0.035
fw = mix(C(96, 170, 62), C(120, 188, 74), gf * 0.6 + gn * 0.4) * (1 + stripe)
ground = np.where(fair[..., None], fw, ground)
ground = np.where(tee[..., None], fw * 1.04, ground)
ground = np.where(fringe[..., None], mix(C(92, 168, 62), C(112, 182, 70), gf), ground)
gs = (np.floor(Xg / 3.2) % 2)[..., None] * 0.05 - 0.025
ground = np.where(green[..., None], mix(C(126, 204, 86), C(146, 216, 98), gf) * (1 + gs), ground)
ground = np.where(lip[..., None], ground * 0.78, ground)
sandc = mix(C(220, 196, 140), C(244, 228, 182), gf * 0.5 + gn * 0.5)
ground = np.where(sand[..., None], sandc, ground)
ground = ground * shade[..., None]
# water mirrors the sky above the horizon, broken by ripples
ry = np.clip(YH - (gy[:, None] - YH) * 0.6 - 20 * vnoise(Xg / 2, Zg * 2), 0, YH - 1).astype(int)
water = img[ry, np.clip(xx[YH + 1:].astype(int), 0, W - 1)] * C(150, 175, 205) * 1.1
water = water * (0.85 + 0.25 * vnoise(Xg * 0.8, Zg * 4)[..., None])
ground = np.where(pond[..., None], water, ground)
ground = np.where(((pond_e >= 1) & (pond_e < 1.12))[..., None], C(70, 84, 46), ground)

# tree list
trees = []
tr = np.random.default_rng(3)
for i in range(40):
    trees.append((-70 + i * 4 + tr.random() * 3, 205 + tr.random() * 40, 10 + tr.random() * 7, tr.random() < 0.35))
for i in range(14):
    z = 70 + i * 10 + tr.random() * 6
    trees.append((-58 - tr.random() * 20, z, 9 + tr.random() * 6, tr.random() < 0.4))
    trees.append((75 + tr.random() * 25, z + 4, 9 + tr.random() * 6, tr.random() < 0.4))
# tree shadows on the ground
sh_dir = -np.array([SUN[0], SUN[2]]) / SUN[1]
shadow = np.zeros(Zg.shape)
for (tx, tz, th, pine) in trees:
    cxs = tx + sh_dir[0] * th * 0.6; czs = tz + sh_dir[1] * th * 0.6
    r = th * (0.3 if pine else 0.45)
    dd = ((Xg - cxs) / (r * 1.1)) ** 2 + ((Zg - czs) / (r * 0.9)) ** 2
    shadow = np.maximum(shadow, np.clip(1.2 - dd, 0, 1) * (0.5 + 0.5 * fbm(Xg / 2, Zg / 2, 2)))
ground = ground * (1 - 0.45 * np.clip(shadow, 0, 1))[..., None]

fog = 1 - np.exp(-Zg / 900.0)
ground = mix(ground, haze, fog * 0.85)
img[YH + 1:] = ground

# ---------------- trees ----------------
pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
dr = ImageDraw.Draw(pil)
def proj(x, y, z):
    return CX + F * (x - CAMX) / z, YH + F * (CAMH - y) / z

def draw_tree(tx, tz, th, pine, seed):
    r = np.random.default_rng(seed)
    s = F / tz                                   # pixels per yard
    f = min(0.85, 1 - math.exp(-tz / 900.0))
    hz = np.array([176, 186, 214])
    def col(c):
        c = np.array(c) * (1 - f) + hz * f
        return tuple(int(v) for v in np.clip(c, 0, 255))
    # trunk
    bx, by = proj(tx, 0, tz)
    tw = max(1, th * 0.05 * s)
    dr.polygon([(bx - tw, by), (bx + tw, by), (bx + tw * 0.6, by - th * 0.6 * s), (bx - tw * 0.6, by - th * 0.6 * s)], fill=col((70, 52, 36)))
    dr.line([(bx - tw * 0.6, by), (bx - tw * 0.4, by - th * 0.6 * s)], fill=col((110, 86, 60)), width=max(1, int(tw * 0.5)))
    # canopy: lit leaf clusters, back to front
    pts = []
    if pine:
        for i in range(int(380 if s > 3 else 160)):
            hgt = r.random() ** 0.8
            rad = (1 - hgt) * th * 0.32 + 0.4
            a = r.random() * 6.283
            pts.append((math.cos(a) * rad * math.sqrt(r.random()), th * (0.25 + 0.75 * hgt), math.sin(a) * rad))
    else:
        cr = th * 0.42
        for blob in range(5):
            bc = (r.normal(0, cr * 0.35), th * 0.62 + r.normal(0, cr * 0.25), r.normal(0, cr * 0.3))
            for i in range(int(130 if s > 3 else 50)):
                v = r.normal(0, 1, 3); v /= np.linalg.norm(v)
                d = cr * 0.6 * r.random() ** 0.35
                pts.append((bc[0] + v[0] * d, bc[1] + v[1] * d * 0.85, bc[2] + v[2] * d))
    pts = np.array(pts)
    centre_y = th * (0.6 if pine else 0.62)
    nv = pts - np.array([0, centre_y, 0]); nv /= (np.linalg.norm(nv, axis=1, keepdims=True) + 1e-6)
    li = np.clip(nv @ SUN * 0.8 + 0.25 + r.normal(0, 0.12, len(pts)), 0, 1)
    order = np.argsort(-pts[:, 2])
    dark, mid, lite = ((22, 54, 36), (40, 92, 52), (96, 150, 70)) if pine else ((26, 62, 26), (52, 108, 38), (132, 180, 70))
    br = (0.9 if pine else 1.1) * s
    for i in order:
        px, py = proj(tx + pts[i, 0], pts[i, 1], tz + pts[i, 2])
        l = li[i]
        c = np.array(dark) + (np.array(mid) - np.array(dark)) * min(1, l * 1.6)
        if l > 0.62: c = c + (np.array(lite) - c) * (l - 0.62) * 2.2
        rr = br * (0.55 + 0.5 * r.random())
        if rr < 0.7:
            dr.point((px, py), fill=col(c))
        else:
            dr.ellipse([px - rr, py - rr * 0.85, px + rr, py + rr * 0.85], fill=col(c))

for k, t in enumerate(sorted(trees, key=lambda t: -t[1])):
    draw_tree(*t, seed=k)

# ---------------- pin and flag ----------------
px, py = proj(GREEN[0] + 2, 0, GREEN[1] - 2)
s = F / (GREEN[1] - 2)
top = py - 2.6 * s
dr.line([(px, py), (px, top)], fill=(245, 245, 240), width=max(1, int(0.06 * s)))
dr.polygon([(px, top), (px + 1.3 * s, top + 0.35 * s), (px + 1.2 * s, top + 0.5 * s), (px, top + 0.75 * s)], fill=(214, 30, 34))
dr.ellipse([px - 0.18 * s, py - 0.06 * s, px + 0.18 * s, py + 0.06 * s], fill=(20, 30, 20))

pil.save(f'{OUT}/scene_bg_{R}.png')
img = np.asarray(pil).astype(np.float64) / 255

# ---------------- golfer (ray-marched) ----------------
def sd_caps(p, a, b, r):
    a = np.array(a); b = np.array(b); pa = p - a; ba = b - a
    h = np.clip((pa @ ba) / (ba @ ba), 0, 1)
    return np.linalg.norm(pa - h[..., None] * ba, axis=-1) - r
def sd_ell(p, c, rad):
    q = (p - np.array(c)) / np.array(rad)
    k0 = np.linalg.norm(q, axis=-1); k1 = np.linalg.norm(q / np.array(rad), axis=-1)
    return k0 * (k0 - 1) / (k1 + 1e-9)
def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)

J = dict(
    pel=(0, 0.95, 0), chest=(0.0, 1.30, 0.02), neck=(0.0, 1.50, 0.03), head=(0.02, 1.64, 0.02),
    ls=(-0.19, 1.42, 0.05), rs=(0.18, 1.40, 0.0), le=(-0.33, 1.58, 0.02), re=(-0.02, 1.58, -0.08),
    hands=(-0.20, 1.76, -0.02), lh=(-0.10, 0.93, 0.0), rh=(0.10, 0.93, 0.0),
    lk=(-0.14, 0.50, 0.02), la=(-0.15, 0.08, 0.0), rk=(0.02, 0.50, 0.10), ra=(0.08, 0.16, 0.30))
club_end = (0.58, 1.40, 0.22)

def scene(p):
    d = {}
    torso = sd_ell(p, (0, 1.22, 0.02), (0.20, 0.30, 0.13))
    torso = smin(torso, sd_caps(p, J['ls'], J['rs'], 0.07), 0.06)
    d['shirt'] = smin(smin(torso, sd_caps(p, J['ls'], J['le'], 0.055), 0.04), sd_caps(p, J['rs'], J['re'], 0.055), 0.04)
    d['arm'] = np.minimum(sd_caps(p, J['le'], J['hands'], 0.042), sd_caps(p, J['re'], J['hands'], 0.042))
    d['glove'] = np.linalg.norm(p - np.array(J['hands']), axis=-1) - 0.055
    hips = sd_ell(p, (0, 0.94, 0.0), (0.18, 0.13, 0.12))
    legs = np.minimum(np.minimum(sd_caps(p, J['lh'], J['lk'], 0.075), sd_caps(p, J['lk'], J['la'], 0.06)),
                      np.minimum(sd_caps(p, J['rh'], J['rk'], 0.075), sd_caps(p, J['rk'], J['ra'], 0.06)))
    d['pants'] = smin(hips, legs, 0.05)
    d['shoe'] = np.minimum(sd_caps(p, (-0.16, 0.05, -0.08), (-0.15, 0.05, 0.14), 0.05),
                           sd_caps(p, (0.08, 0.10, 0.26), (0.10, 0.03, 0.42), 0.05))
    d['skin'] = np.minimum(sd_ell(p, J['head'], (0.10, 0.12, 0.11)), sd_caps(p, J['neck'], (0.01, 1.58, 0.02), 0.05))
    cap = np.maximum(sd_ell(p, (0.02, 1.69, 0.02), (0.108, 0.09, 0.115)), -(p[..., 1] - 1.68))
    brim = np.maximum(sd_ell(p, (0.02, 1.685, 0.13), (0.09, 0.012, 0.09)), -(p[..., 2] - 0.06))
    d['cap'] = np.minimum(cap, brim)
    d['club'] = sd_caps(p, J['hands'], club_end, 0.009)
    d['head'] = sd_ell(p, club_end, (0.05, 0.03, 0.03))
    d['belt'] = sd_ell(p, (0, 0.99, 0.0), (0.185, 0.025, 0.125))
    keys = list(d.keys())
    D = np.stack([d[k] for k in keys])
    return D.min(0), D.argmin(0), keys

MAT = dict(shirt=C(196, 40, 46), arm=C(196, 40, 46), glove=C(245, 245, 240), pants=C(150, 150, 160), shoe=C(240, 240, 236),
           skin=C(226, 176, 136), cap=C(240, 240, 236), club=C(200, 200, 210), head=C(70, 70, 80), belt=C(30, 26, 24))

GW, GH = int(0.36 * R), int(0.56 * R)
cam = np.array([0.9, 1.25, -3.6]); look = np.array([0.0, 0.95, 0.0])
fw_ = look - cam; fw_ /= np.linalg.norm(fw_)
rt = np.cross(np.array([0, 1, 0.0]), fw_); rt /= np.linalg.norm(rt); up = np.cross(fw_, rt)
gyy, gxx = np.mgrid[0:GH, 0:GW].astype(np.float64)
u = (gxx - GW / 2) / GH * 2.1; v = -(gyy - GH * 0.5) / GH * 2.1
dirs = fw_ * 1.0 + u[..., None] * rt * 0.5 + v[..., None] * up * 0.5
dirs /= np.linalg.norm(dirs, axis=-1, keepdims=True)
t = np.zeros((GH, GW)); hit = np.zeros((GH, GW), bool)
for i in range(90):
    p = cam + dirs * t[..., None]
    dist, _, _ = scene(p)
    hit |= dist < 0.002
    t = np.where(hit, t, t + dist * 0.9)
    if (t > 8).all(): break
hit &= t < 8
p = cam + dirs * t[..., None]
dist, mid_, keys = scene(p)
eps = 0.002
nrm = np.stack([scene(p + np.array(o))[0] - scene(p - np.array(o))[0] for o in ([eps, 0, 0], [0, eps, 0], [0, 0, eps])], -1)
nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True) + 1e-9
lam = np.clip(nrm @ SUN, 0, 1)
rim = np.clip(1 - np.abs((nrm * dirs).sum(-1)), 0, 1) ** 3
base = np.stack([MAT[k] for k in keys])[mid_]
spec = np.clip((nrm @ (SUN - dirs.mean((0, 1)))), 0, 1) ** 20
col = base * (0.38 + 0.8 * lam)[..., None] + C(150, 170, 220) * 0.18 * (nrm[..., 1:2] * 0.5 + 0.5) + rim[..., None] * 0.15 + spec[..., None] * 0.15
# soft cloth folds
col *= (0.92 + 0.12 * vnoise(gxx / 6, gyy / 14))[..., None]
gimg = np.clip(col, 0, 1)

# place golfer bottom left, with a cast shadow
ox, oy = int(0.02 * R), H - GH + int(0.01 * R)
feet_y = oy + int(np.nonzero(hit.any(1))[0].max()) - int(0.01 * R)
sh = Image.new('L', (W, H)); sd = ImageDraw.Draw(sh)
sd.ellipse([ox + GW * 0.18, feet_y - 0.012 * R, ox + GW * 1.15, feet_y + 0.03 * R], fill=150)
sh = sh.filter(ImageFilter.GaussianBlur(R / 200))
s_arr = np.asarray(sh).astype(np.float64) / 255
img = img * (1 - 0.5 * s_arr[..., None])
reg = img[oy:oy + GH, ox:ox + GW]
hm = hit[:reg.shape[0], :reg.shape[1]]
reg[hm] = gimg[:reg.shape[0], :reg.shape[1]][hm]
img[oy:oy + GH, ox:ox + GW] = reg

Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(f'{OUT}/scene_{R}.png')
print('done')
