import math, random, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import os
_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mock1b.py')).read()
exec(_src[:_src.index('# 16-colour')])  # 5x7 font G

# 3x5 font for small print
T = {
'0':["111","101","101","101","111"],'1':["010","110","010","010","111"],
'2':["111","001","111","100","111"],'3':["111","001","011","001","111"],
'4':["101","101","111","001","001"],'5':["111","100","111","001","111"],
'6':["111","100","111","101","111"],'7':["111","001","010","010","010"],
'8':["111","101","111","101","111"],'9':["111","101","111","001","111"],
'A':["010","101","111","101","101"],'B':["110","101","110","101","110"],
'C':["011","100","100","100","011"],'D':["110","101","101","101","110"],
'E':["111","100","110","100","111"],'F':["111","100","110","100","100"],
'G':["011","100","101","101","011"],'H':["101","101","111","101","101"],
'I':["111","010","010","010","111"],'K':["101","101","110","101","101"],
'L':["100","100","100","100","111"],'M':["101","111","111","101","101"],
'N':["110","101","101","101","101"],'O':["010","101","101","101","010"],
'P':["110","101","110","100","100"],'R':["110","101","110","101","101"],
'S':["011","100","010","001","110"],'T':["111","010","010","010","010"],
'U':["101","101","101","101","111"],'V':["101","101","101","101","010"],
'W':["101","101","111","111","101"],'Y':["101","101","010","010","010"],
' ':["000"]*5,'.':["000","000","000","000","010"],'_':["000","000","000","000","111"],
'-':["000","000","111","000","000"],'+':["000","010","111","010","000"],
}

BAYER = [[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]]
def bay(x, y): return (BAYER[y % 4][x % 4] + 0.5) / 16

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def scale2x_mask(m):
    w, h = m.size; p = m.load(); o = Image.new('L', (w*2, h*2)); q = o.load()
    for y in range(h):
        for x in range(w):
            P0 = p[x, y]; A = p[x, max(0, y-1)]; B = p[min(w-1, x+1), y]
            C = p[max(0, x-1), y]; D = p[x, min(h-1, y+1)]
            q[2*x, 2*y] = A if (C == A and C != D and A != B) else P0
            q[2*x+1, 2*y] = B if (A == B and A != C and B != D) else P0
            q[2*x, 2*y+1] = C if (D == C and D != B and C != A) else P0
            q[2*x+1, 2*y+1] = D if (B == D and B != A and D != C) else P0
    return o

class C:
    """Canvas in 160x160 logical units, drawn at scale S."""
    def __init__(s, S):
        s.S = S; s.N = 160 * S
        s.img = Image.new('RGB', (s.N, s.N)); s.px = s.img.load()
        s.d = ImageDraw.Draw(s.img)
    def p(s, v): return int(round(v * s.S))
    def put(s, X, Y, c):
        if 0 <= X < s.N and 0 <= Y < s.N: s.px[X, Y] = c
    def rect(s, x, y, w, h, c):
        if c is None: return
        x0, y0 = s.p(x), s.p(y)
        s.d.rectangle([x0, y0, max(x0, s.p(x+w)-1), max(y0, s.p(y+h)-1)], fill=c)
    def poly(s, pts, c):
        s.d.polygon([(x*s.S, y*s.S) for x, y in pts], fill=c)
    def ell(s, cx, cy, rx, ry, c, outline=None):
        x0, y0 = (cx-rx)*s.S, (cy-ry)*s.S
        s.d.ellipse([x0, y0, max(x0, (cx+rx)*s.S-1), max(y0, (cy+ry)*s.S-1)], fill=c, outline=outline)
    def line(s, x0, y0, x1, y1, c, w=1):
        s.d.line([x0*s.S, y0*s.S, x1*s.S, y1*s.S], fill=c, width=max(1, int(w*s.S)))
    def mask(s):
        m = Image.new('L', (s.N, s.N)); return m, ImageDraw.Draw(m)
    def grad(s, x, y, w, h, stops, m=None):
        X0, Y0, X1, Y1 = s.p(x), s.p(y), s.p(x+w), s.p(y+h)
        mp = m.load() if m else None
        n = len(stops) - 1
        for Y in range(Y0, Y1):
            t = (Y - Y0) / max(1, Y1 - Y0 - 1) * n
            a = min(n - 1, int(t)); f = t - a
            for X in range(X0, X1):
                if mp and not mp[X, Y]: continue
                s.put(X, Y, stops[a+1] if f > bay(X, Y) else stops[a])
    def text(s, x, y, t, c, shadow=None, font=G, cw=6, sh=1):
        if shadow: s.text(x+sh, y+sh, t, shadow, None, font, cw)
        rows = len(next(iter(font.values())))
        m = Image.new('L', (len(t)*cw + 2, rows + 2)); mp = m.load()
        for i, ch in enumerate(t):
            g = font.get(ch, font[' '])
            for r, row in enumerate(g):
                for col, b in enumerate(row):
                    if b == '1': mp[1 + i*cw + col, 1 + r] = 255
        if s.S == 2: m = scale2x_mask(m)
        s.img.paste(c, (s.p(x) - s.S, s.p(y) - s.S), m)
    def ctext(s, y, t, c, shadow=None, cx=80, font=G, cw=6):
        s.text(cx - (len(t)*cw - 1) / 2, y, t, c, shadow, font, cw)
    def tiny(s, x, y, t, c, shadow=None):
        s.text(x, y, t, c, shadow, T, 4)
    def ctiny(s, y, t, c, cx=80, shadow=None):
        s.ctext(y, t, c, shadow, cx, T, 4)

# ---------- pieces ----------
SKY = [(18,30,96),(30,52,140),(50,84,184),(84,124,220),(136,168,236),(196,204,236),(246,214,180),(255,196,150)]
def sky(c, h=86):
    c.grad(0, 0, 160, h, SKY)
    for cx, cy, w in [(26,60,26),(118,52,30),(70,72,18),(146,74,14)]:
        cloud(c, cx, cy, w)

def cloud(c, cx, cy, w):
    hi, mid, lo = (255,250,246), (232,224,236), (176,172,206)
    rnd = random.Random(int(cx*7+cy))
    blobs = [(cx + rnd.uniform(-w/2, w/2), cy - rnd.uniform(0, w/6), rnd.uniform(w/6, w/3.2)) for _ in range(7)]
    for bx, by, r in blobs: c.ell(bx, by+1.5, r, r*0.55, lo)
    for bx, by, r in blobs: c.ell(bx, by, r*0.9, r*0.5, mid)
    for bx, by, r in blobs: c.ell(bx - r*0.2, by - r*0.15, r*0.6, r*0.33, hi)
    c.rect(cx - w/2 - 4, cy + 2, w + 8, 4, None) if False else None

def hills(c):
    far = (104,116,170); far2 = (128,140,186)
    pts = [(0, 90)] + [(x, 72 - 7*math.sin(x/19+0.6) - 4*math.sin(x/7.1)) for x in range(0, 161, 2)] + [(160, 90)]
    c.poly(pts, far)
    pts = [(0, 90)] + [(x, 78 - 5*math.sin(x/13+2) - 2*math.sin(x/4.3)) for x in range(0, 161, 2)] + [(160, 90)]
    c.poly(pts, far2)
    m, md = c.mask()
    md.polygon([(x*c.S, y*c.S) for x, y in pts], fill=255)
    c.grad(0, 70, 160, 20, [far2, (150,160,196), (90,120,110)], m)

def tree(c, cx, cy, r, rnd, dark=(18,58,30), mid=(34,92,40), lite=(70,136,56)):
    c.rect(cx - r*0.12, cy, r*0.24, r*0.9, (60,40,24))
    for _ in range(9):
        a = rnd.uniform(0, 6.28); d = rnd.uniform(0, r*0.55)
        c.ell(cx + math.cos(a)*d, cy + math.sin(a)*d*0.8, r*0.55, r*0.5, dark)
    for _ in range(7):
        a = rnd.uniform(3.4, 5.6); d = rnd.uniform(0, r*0.5)
        c.ell(cx + math.cos(a)*d - r*0.1, cy + math.sin(a)*d*0.7 - r*0.1, r*0.38, r*0.32, mid)
    for _ in range(5):
        c.ell(cx - r*0.3 + rnd.uniform(-r*0.2, r*0.3), cy - r*0.35 + rnd.uniform(-r*0.2, r*0.2), r*0.18, r*0.15, lite)

def treeline(c):
    rnd = random.Random(4)
    x = -4
    while x < 168:
        r = rnd.uniform(5, 9)
        tree(c, x, 86 - r*0.6, r, rnd)
        x += rnd.uniform(4, 8)

def clubhouse(c):
    x, y = 110, 76
    c.rect(x, y, 40, 14, (232,222,196))
    c.rect(x, y+12, 40, 2, (180,168,140))
    c.poly([(x-3, y+1), (x+8, y-9), (x+32, y-9), (x+43, y+1)], (150,44,36))
    c.poly([(x+8, y-9), (x+32, y-9), (x+30, y-7), (x+10, y-7)], (190,70,52))
    c.rect(x+27, y-14, 4, 6, (120,100,90))
    for i in range(6):
        c.rect(x + 3 + i*6, y + 4, 3, 5, (40,52,90))
        c.rect(x + 3 + i*6, y + 4, 3, 1, (250,250,250))
    for fx, cols in [(x+6, [(250,250,250),(0,90,180)]), (x+36, [(200,30,30),(250,250,250)])]:
        c.line(fx, y-20, fx, y-9, (220,220,220), 0.5)
        c.rect(fx+0.5, y-20, 6, 4, cols[1]); c.line(fx+0.5, y-20, fx+6.5, y-16, cols[0], 0.6)
        c.line(fx+0.5, y-16, fx+6.5, y-20, cols[0], 0.6)

HOR = 86
def course(c):
    # rough, darker far away
    c.grad(0, HOR, 160, 160-HOR, [(36,92,40),(46,112,44),(56,130,48),(66,146,52)])
    rnd = random.Random(9)
    for _ in range(500 * c.S * c.S):
        X = rnd.randrange(c.N); Y = rnd.randrange(c.p(HOR+2), c.N)
        c.put(X, Y, (30,84,34) if rnd.random() < 0.6 else (84,160,64))
    # fairway with mowing stripes in perspective
    m, md = c.mask()
    L = [(64,92),(56,100),(44,112),(30,128),(16,146),(6,160)]
    R = [(98,92),(108,100),(122,112),(140,128),(156,146),(166,160)]
    md.polygon([(x*c.S, y*c.S) for x, y in L + R[::-1]], fill=255)
    mp = m.load()
    for Y in range(c.p(90), c.N):
        band = int(160 * c.S / (Y - c.p(HOR) + 2 * c.S))
        a, b = ((96,176,64), (82,162,56)) if band % 2 else ((84,166,58), (74,150,50))
        for X in range(c.N):
            if mp[X, Y]:
                c.put(X, Y, b if (X * 7 + Y * 13) % 11 == 0 else a)
    # pond
    c.ell(28, 104, 24, 5.5, (30,60,120))
    c.grad(6, 99, 44, 11, [(40,80,150),(64,110,180),(40,80,150)], ell_mask(c, 28, 104, 22.5, 4.8))
    for i in range(14):
        c.rect(10 + (i*7) % 36, 101 + (i*3) % 6, 3, 0.5, (190,210,240))
    for x in [8, 11, 47, 50]:
        c.line(x, 99, x+0.5, 104, (60,90,30), 0.5)
    # green, fringe, bunkers, flag
    c.ell(82, 93, 17, 4, (70,150,50))
    c.ell(82, 93, 14, 3, (120,206,84))
    c.ell(80, 92.4, 8, 1.4, (140,220,100))
    c.ell(104, 96, 8, 2.4, (190,160,100)); c.ell(104, 96.6, 7.5, 1.9, (240,222,170))
    c.ell(60, 95, 5, 1.6, (190,160,100)); c.ell(60, 95.4, 4.6, 1.2, (240,222,170))
    c.line(88, 77, 88, 93, (250,250,250), 0.6)
    c.poly([(88.5, 77), (96, 79.5), (88.5, 82)], (220,30,30))
    c.ell(88.2, 93, 0.8, 0.4, (20,40,20))

def ell_mask(c, cx, cy, rx, ry):
    m, md = c.mask()
    md.ellipse([(cx-rx)*c.S, (cy-ry)*c.S, (cx+rx)*c.S, (cy+ry)*c.S], fill=255)
    return m

def golfer(c, ox=0):
    sk, skd = (234,184,140), (190,136,100)
    shirt, shirtd = (40,64,150), (24,38,96)
    pant, pantd = (214,196,150), (160,140,100)
    c.ell(22+ox, 157, 16, 2.2, (40,96,36))
    c.poly([(16+ox,133),(22+ox,133),(21+ox,155),(15+ox,155)], pant)
    c.poly([(16+ox,133),(18+ox,133),(17+ox,155),(15+ox,155)], pantd)
    c.poly([(22+ox,133),(28+ox,133),(33+ox,152),(28+ox,154)], pantd)
    c.rect(13+ox, 155, 9, 2.5, (250,250,250)); c.rect(13+ox, 156.5, 9, 1, (120,70,40))
    c.poly([(28+ox,152),(34+ox,150),(35+ox,154),(29+ox,155)], (250,250,250))
    c.rect(15.5+ox, 132, 13, 1.6, (30,24,20))
    c.poly([(14+ox,116),(29+ox,115),(30+ox,133),(15+ox,133)], shirt)
    c.poly([(14+ox,116),(19+ox,116),(18+ox,133),(15+ox,133)], shirtd)
    c.line(14+ox, 125, 29+ox, 124, (250,250,250), 0.6)
    # arms up and round in the follow-through
    c.poly([(26+ox,117),(30+ox,119),(16+ox,108),(13+ox,110)], shirt)
    c.poly([(16+ox,118),(20+ox,118),(14+ox,109),(11+ox,111)], shirtd)
    c.ell(12.5+ox, 108.5, 2.4, 2.2, sk)
    # club wrapped behind the neck
    c.line(12+ox, 107, 34+ox, 100, (200,200,210), 0.6)
    c.poly([(33+ox,98.5),(37+ox,99.5),(36+ox,102),(33+ox,101.5)], (60,60,70))
    # head and cap
    c.ell(22.5+ox, 110, 4, 4.4, sk)
    c.ell(24+ox, 111, 1.6, 2.4, skd)
    c.poly([(18.4+ox,109),(19+ox,105.6),(22.5+ox,105),(26.6+ox,106.4),(26.8+ox,109)], (250,250,250))
    c.rect(25+ox, 108, 4, 1, (220,220,220))

def bag(c):
    x, y = 134, 124
    c.line(x+2, y+16, x-3, y+33, (90,90,90), 0.6); c.line(x+12, y+16, x+17, y+33, (90,90,90), 0.6)
    for i, (dx, col) in enumerate([(2,(200,40,40)),(5,(250,250,250)),(8,(30,60,150)),(11,(230,200,40))]):
        c.line(x+dx, y-6+i, x+dx+1, y+2, (170,170,180), 0.6)
        c.ell(x+dx+0.5, y-7+i, 2, 2.6, col)
        c.ell(x+dx+0.5, y-10+i, 0.9, 0.9, (250,250,250))
    c.rect(x, y, 14, 30, (150,30,30))
    c.rect(x, y, 4, 30, (110,20,24)); c.rect(x+11, y, 3, 30, (180,60,50))
    c.rect(x, y+2, 14, 2, (240,236,220)); c.rect(x, y+24, 14, 2, (240,236,220))
    c.rect(x+4, y+9, 7, 9, (120,24,24)); c.rect(x+4, y+9, 7, 1, (200,80,70))
    c.ell(x+7, y+30, 7, 1.6, (90,16,20))

GOLD = [(255,252,214),(255,232,120),(250,196,52),(214,140,26),(140,74,10),(232,178,48),(255,226,120),(255,244,190)]
def logo(c, y=5, size=30):
    f = ImageFont.truetype('/usr/share/fonts/truetype/noto/NotoSerif-ExtraBoldItalic.ttf', int(size * c.S))
    m = Image.new('L', (c.N, c.N)); md = ImageDraw.Draw(m); md.fontmode = '1'
    t = 'PAR 386'
    bb = md.textbbox((0, 0), t, font=f)
    X = (c.N - (bb[2] - bb[0])) // 2 - bb[0]; Y = c.p(y) - bb[1]
    md.text((X, Y), t, font=f, fill=255)
    out = m.filter(ImageFilter.MaxFilter(3 if c.S == 1 else 5))
    sh = Image.new('L', m.size); sh.paste(out, (2*c.S, 2*c.S))
    c.img.paste((10,10,30), (0, 0), sh)
    c.img.paste((70,30,6), (0, 0), out)
    yb = m.getbbox()
    c.grad(0, yb[1]/c.S, 160, (yb[3]-yb[1])/c.S + 0.5, GOLD, m)
    # sparkle
    for sx, sy in [(42, 8), (118, 26)]:
        c.line(sx-3, sy, sx+3, sy, (255,255,255), 0.5); c.line(sx, sy-3, sx, sy+3, (255,255,255), 0.5)
    return yb[3] / c.S

def ribbon(c, y, t):
    w = len(t) * 6 + 14; x = 80 - w/2
    dk, md_, lt = (110,10,20), (170,20,30), (210,40,44)
    for side in (-1, 1):
        ex = x - 9 if side < 0 else x + w - 1
        c.poly([(ex, y+3), (ex+10, y+3), (ex+10, y+13), (ex, y+13), (ex+(4 if side < 0 else 6), y+8)], dk)
    c.poly([(x, y+3), (x+3, y+3), (x+3, y+13)], (70,0,10)); c.poly([(x+w-4, y+3), (x+w-1, y+3), (x+w-4, y+13)], (70,0,10))
    c.rect(x, y, w, 10, md_); c.rect(x, y, w, 1.5, lt); c.rect(x, y+9, w, 1, dk)
    c.ctext(y + 2, t, (255,240,200), (90,0,10))

def title(S):
    c = C(S)
    sky(c); hills(c); treeline(c); clubhouse(c); course(c); bag(c); golfer(c, -7)
    yb = logo(c)
    ribbon(c, yb + 2, 'CHAMPIONSHIP GOLF')
    c.ctext(146, 'PRESS ANY BUTTON', (255,255,255), (0,0,0))
    return c

# ---------- clubhouse menu ----------
def wood(c, x, y, w, h, vertical):
    X0, Y0, X1, Y1 = c.p(x), c.p(y), c.p(x+w), c.p(y+h)
    cols = [(64,30,14),(96,48,22),(122,64,30),(148,84,40)]
    for Y in range(Y0, Y1):
        for X in range(X0, X1):
            u, v = (X, Y) if vertical else (Y, X)
            g = math.sin(u * 0.9 / c.S + math.sin(v * 0.07 / c.S + u * 0.3 / c.S) * 2.2) * 0.5 + 0.5
            k = min(3, int(g * 3 + bay(X, Y)))
            c.put(X, Y, cols[k])

def bevel(c, x, y, w, h, lt, dk, t=1):
    c.rect(x, y, w, t, lt); c.rect(x, y, t, h, lt)
    c.rect(x, y+h-t, w, t, dk); c.rect(x+w-t, y, t, h, dk)

def plaque(c, x, y, w, h, t, tiny=False):
    c.rect(x+1, y+1, w, h, (30,16,6))
    c.grad(x, y, w, h, [(255,240,170),(232,184,70),(190,130,30),(240,196,90)])
    bevel(c, x, y, w, h, (255,250,210), (110,64,10), 0.5)
    for sx in (x+2.5, x+w-3.5):
        c.ell(sx+0.5, y+h/2, 1.2, 1.2, (130,110,90)); c.line(sx-0.3, y+h/2, sx+1.3, y+h/2, (60,50,40), 0.4)
    if tiny: c.ctiny(y + (h-5)/2, t, (70,36,6), cx=x+w/2, shadow=None)
    else:
        c.ctext(y + (h-7)/2 + 0.5, t, (255,244,200), cx=x+w/2 + 0.5)
        c.ctext(y + (h-7)/2, t, (70,36,6), cx=x+w/2)

def felt(c, x, y, w, h):
    c.rect(x, y, w, h, (20,86,44))
    rnd = random.Random(3)
    for _ in range(int(w*h*0.35*c.S*c.S)):
        X = c.p(x) + rnd.randrange(c.p(w)); Y = c.p(y) + rnd.randrange(c.p(h))
        c.put(X, Y, (16,76,40) if rnd.random() < 0.5 else (26,96,50))

def ball(c, x, y):
    c.line(x, y+2, x, y+6, (200,160,90), 1)
    c.poly([(x-2, y+2), (x+2, y+2), (x+0.5, y+3.5), (x-0.5, y+3.5)], (220,180,110))
    c.ell(x, y, 3, 3, (180,180,190)); c.ell(x-0.4, y-0.4, 2.6, 2.6, (250,250,250))
    for dx, dy in [(0.8,0.6),(-0.6,1.2),(1.4,-0.6)]: c.rect(x+dx, y+dy, 0.5, 0.5, (190,190,200))

def dim(c):
    for Y in range(c.N):
        for X in range(c.N):
            r, g, b = c.px[X, Y]
            k = 0.32 if (X//c.S + Y//c.S) % 2 else 0.45
            c.px[X, Y] = (int(r*k), int(g*k), int(b*k*1.1))

def holemap(c, x, y, w, h):
    c.rect(x-1.5, y-1.5, w+3, h+3, (240,236,220)); c.rect(x-0.5, y-0.5, w+1, h+1, (90,80,60))
    c.rect(x, y, w, h, (30,96,40))
    rnd = random.Random(2)
    for _ in range(14):
        c.ell(x + rnd.uniform(1, w-1), y + rnd.uniform(1, h-1), 1.2, 1.2, (16,60,26))
    c.poly([(x+w*0.3, y+h-3), (x+w*0.62, y+h-3), (x+w*0.6, y+h*0.45), (x+w*0.82, y+h*0.2),
            (x+w*0.66, y+h*0.12), (x+w*0.36, y+h*0.4)], (92,170,64))
    c.ell(x+w*0.68, y+h*0.5, w*0.12, h*0.06, (50,90,200))
    c.ell(x+w*0.7, y+h*0.12, w*0.17, h*0.07, (140,220,100))
    c.ell(x+w*0.48, y+h*0.6, 1.2, 1.4, (240,222,170)); c.ell(x+w*0.88, y+h*0.18, 1.2, 1, (240,222,170))
    c.rect(x+w*0.7, y+h*0.08, 0.6, 3, (250,250,250)); c.rect(x+w*0.7+0.6, y+h*0.08, 1.6, 1, (220,30,30))
    c.rect(x+w*0.42, y+h-3.5, 3, 1.5, (250,250,250))
    for i in range(6):
        c.rect(x+w*0.43, y+h*(0.88-i*0.13), 0.5, 1.2, (255,255,120))

def menu(S):
    c = title(S)
    dim(c)
    x, y, w, h = 6, 12, 148, 140
    c.rect(x+3, y+3, w, h, (0,0,0))
    wood(c, x, y, w, 7, False); wood(c, x, y+h-7, w, 7, False)
    wood(c, x, y, 7, h, True); wood(c, x+w-7, y, 7, h, True)
    bevel(c, x, y, w, h, (190,130,70), (40,18,6))
    bevel(c, x+6, y+6, w-12, h-12, (40,18,6), (180,120,64))
    felt(c, x+7, y+7, w-14, h-14)
    plaque(c, 38, 5, 84, 13, 'CLUBHOUSE')
    items = ['PLAY A ROUND', 'PRACTICE RANGE', 'PUTTING GREEN', 'CHOOSE COURSE', 'PLAYERS', 'SETTINGS', 'RECORDS']
    for i, t in enumerate(items):
        yy = 26 + i * 15
        if i == 0:
            c.rect(14, yy - 4, 92, 15, (34,112,60))
            c.rect(14, yy - 4, 92, 1, (60,140,80)); c.rect(14, yy + 10, 92, 1, (10,50,24))
            ball(c, 19, yy + 2)
            c.text(26, yy, t, (255,224,90), (10,40,20))
        else:
            c.text(26, yy, t, (236,226,196), (10,40,20))
    holemap(c, 113, 28, 30, 74)
    c.ctiny(108, 'HOLE 1', (236,226,196), cx=128)
    c.ctiny(115, 'PAR 4', (236,226,196), cx=128)
    c.ctiny(122, '413 YDS', (255,224,90), cx=128)
    plaque(c, 30, 136, 100, 11, 'AULD KIRK LINKS', tiny=True)
    return c

# ---------- scorecard ----------
def leather(c):
    rnd = random.Random(5)
    for Y in range(c.N):
        for X in range(c.N):
            n = rnd.random()
            base = (26,58,36) if n < 0.7 else (20,48,30) if n < 0.9 else (34,70,44)
            c.px[X, Y] = base
    for i in range(0, 160, 4):
        for (a, b) in [((i, 3), (i+2, 3)), ((i, 156), (i+2, 156)), ((3, i), (3, i+2)), ((156, i), (156, i+2))]:
            c.line(a[0], a[1], b[0], b[1], (190,170,110), 0.5)

def pencil(c, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0; L = math.hypot(dx, dy); ux, uy = dx/L, dy/L; nx, ny = -uy, ux
    def seg(a, b, w, col):
        c.poly([(x0+ux*a+nx*w, y0+uy*a+ny*w), (x0+ux*b+nx*w, y0+uy*b+ny*w),
                (x0+ux*b-nx*w, y0+uy*b-ny*w), (x0+ux*a-nx*w, y0+uy*a-ny*w)], col)
    c.poly([(x0+nx*3+2, y0+ny*3+2), (x1+nx*3+2, y1+ny*3+2), (x1+2, y1+2), (x0+2, y0+2)], (120,110,90))
    seg(0, 6, 2.4, (240,140,150)); seg(6, 10, 2.4, (190,190,200)); seg(7, 8, 2.4, (150,150,160))
    seg(10, L-9, 2.4, (240,196,40)); seg(10, L-9, 0.9, (255,226,90)); seg(10, L-9, -1.4, (200,150,20)) if False else None
    c.poly([(x0+ux*(L-9)+nx*2.4, y0+uy*(L-9)+ny*2.4), (x0+ux*(L-9)-nx*2.4, y0+uy*(L-9)-ny*2.4), (x1, y1)], (232,200,150))
    c.poly([(x0+ux*(L-3)+nx*0.8, y0+uy*(L-3)+ny*0.8), (x0+ux*(L-3)-nx*0.8, y0+uy*(L-3)-ny*0.8), (x1, y1)], (60,60,60))

def scorecard(S):
    c = C(S)
    leather(c)
    x, y, w, h = 2, 9, 156, 124
    c.rect(x+2, y+2, w, h, (8,24,14))
    c.grad(x, y, w, h, [(248,242,222),(244,236,212),(236,226,198)])
    c.rect(x, y, w, 18, (22,82,44)); c.rect(x, y+17, w, 1, (200,170,90))
    # crest
    cx = x + 9
    c.poly([(cx-5, y+2.5), (cx+5, y+2.5), (cx+5, y+9), (cx, y+15), (cx-5, y+9)], (220,180,60))
    c.poly([(cx-4, y+3.5), (cx+4, y+3.5), (cx+4, y+8.6), (cx, y+13.6), (cx-4, y+8.6)], (20,50,140))
    c.line(cx-4, y+3.5, cx+3.6, y+11, (250,250,250), 0.8); c.line(cx+4, y+3.5, cx-3.6, y+11, (250,250,250), 0.8)
    c.text(x+18, y+3, 'AULD KIRK LINKS', (250,236,190), (0,30,10))
    c.tiny(x+18, y+11, 'FRONT NINE  EST. 1764', (200,190,140))
    par = [4,4,3,5,4,4,3,4,5]; yds = [413,382,166,521,398,436,184,361,544]
    dan = [4,3,4,6,4,4,2,4,5]; ang = [5,4,3,5,5,4,3,4,4]
    gx = x + 30; cw = 12; gy = y + 21; rh = 11
    rows = [('HOLE', None), ('YDS', yds), ('PAR', par), ('DAN', dan), ('ROSS', ang)]
    line = (170,156,120)
    for r, (lbl, vals) in enumerate(rows):
        ry = gy + r*rh
        if r == 0: c.rect(x+1, ry, w-2, rh, (196,222,180))
        if r == 2: c.rect(x+1, ry, w-2, rh, (236,228,200))
        c.text(x+3, ry+2, lbl, (22,82,44) if r < 3 else (30,30,60))
        for i in range(9):
            cx0 = gx + i*cw
            if r == 0: c.ctext(ry+2, str(i+1), (22,82,44), cx=cx0+cw/2)
            elif r == 1: c.ctiny(ry+3, str(vals[i]), (110,96,70), cx=cx0+cw/2)
            else:
                v = vals[i]; col = (30,30,60) if r > 2 else (22,82,44)
                if r > 2 and v < par[i]: c.ell(cx0+cw/2, ry+5.5, 4.6, 4.6, None, (200,30,30)); col = (200,30,30)
                if r > 2 and v > par[i]:
                    c.d.rectangle([c.p(cx0+1.5), c.p(ry+1.5), c.p(cx0+cw-1.5)-1, c.p(ry+rh-1.5)-1], outline=(40,70,170)); col = (40,70,170)
                c.ctext(ry+2, str(v), col, cx=cx0+cw/2)
        ox = gx + 9*cw
        if r == 0: c.ctiny(ry+3, 'OUT', (22,82,44), cx=ox+(x+w-ox)/2)
        elif r == 1: c.ctiny(ry+3, str(sum(vals)), (110,96,70), cx=ox+(x+w-ox)/2)
        else: c.ctext(ry+2, str(sum(vals)), (30,30,60) if r > 2 else (22,82,44), cx=ox+(x+w-ox)/2)
        c.rect(x+1, ry+rh-0.5, w-2, 0.5, line)
    for i in range(10):
        c.rect(gx + i*cw - 0.5, gy, 0.5, rh*5, line)
    c.rect(x+1, gy, w-2, 0.5, line)
    c.tiny(x+4, gy+5*rh+8, 'MARKER', (110,96,70)); c.rect(x+30, gy+5*rh+12.5, 50, 0.5, (110,96,70))
    c.tiny(x+4, gy+5*rh+19, 'PLAYER', (110,96,70)); c.rect(x+30, gy+5*rh+23.5, 50, 0.5, (110,96,70))
    c.text(x+34, gy+5*rh+15, 'ANGUS', (60,60,120)) if False else None
    pencil(c, 96, 128, 150, 104)
    c.ctext(140, 'DAN  E     ROSS +1', (250,236,190), (0,0,0))
    c.ctiny(150, 'TAP TO CONTINUE', (190,170,110))
    return c

GREYS = [(0,0,0),(85,85,85),(170,170,170),(255,255,255)]
def to4(img):
    out = Image.new('RGB', img.size); p = img.load(); q = out.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            r, g, b = p[x, y]; L = (r*30 + g*59 + b*11) / 100 / 255 * 3
            i = int(L); i = min(3, i + (1 if L - i > bay(x, y) else 0))
            q[x, y] = GREYS[i]
    return out

out = sys.argv[1]
names = [('title', title), ('menu', menu), ('scorecard', scorecard)]
lo = {n: f(1).img for n, f in names}
hi = {n: f(2).img for n, f in names}
Z = 3; cell = 160*Z
sheet = Image.new('RGB', (3*(cell+16)+16, 2*(cell+16)+16), (36,36,36))
for i, (n, _) in enumerate(names):
    g = to4(lo[n])
    sheet.paste(g.resize((cell, cell), Image.NEAREST), (16 + i*(cell+16), 16))
    sheet.paste(lo[n].resize((cell, cell), Image.NEAREST), (16 + i*(cell+16), cell + 32))
    lo[n].resize((cell, cell), Image.NEAREST).save(f'{out}/m1b2-{n}-160-colour.png')
    g.resize((cell, cell), Image.NEAREST).save(f'{out}/m1b2-{n}-160-grey.png')
    hi[n].resize((640, 640), Image.NEAREST).save(f'{out}/m1b2-{n}-320-colour.png')
sheet.save(f'{out}/m1b2-sheet-160.png')
hs = Image.new('RGB', (3*(640+16)+16, 640+32), (36,36,36))
for i, (n, _) in enumerate(names):
    hs.paste(hi[n].resize((640, 640), Image.NEAREST), (16 + i*656, 16))
hs.save(f'{out}/m1b2-sheet-320.png')
