from PIL import Image

# 5x7 glyphs in a 6x8 cell
G = {
'A':["01110","10001","10001","11111","10001","10001","10001"],
'B':["11110","10001","10001","11110","10001","10001","11110"],
'C':["01110","10001","10000","10000","10000","10001","01110"],
'D':["11110","10001","10001","10001","10001","10001","11110"],
'E':["11111","10000","10000","11110","10000","10000","11111"],
'F':["11111","10000","10000","11110","10000","10000","10000"],
'G':["01110","10001","10000","10111","10001","10001","01111"],
'H':["10001","10001","10001","11111","10001","10001","10001"],
'I':["01110","00100","00100","00100","00100","00100","01110"],
'J':["00111","00010","00010","00010","00010","10010","01100"],
'K':["10001","10010","10100","11000","10100","10010","10001"],
'L':["10000","10000","10000","10000","10000","10000","11111"],
'M':["10001","11011","10101","10101","10001","10001","10001"],
'N':["10001","11001","10101","10011","10001","10001","10001"],
'O':["01110","10001","10001","10001","10001","10001","01110"],
'P':["11110","10001","10001","11110","10000","10000","10000"],
'Q':["01110","10001","10001","10001","10101","10010","01101"],
'R':["11110","10001","10001","11110","10100","10010","10001"],
'S':["01111","10000","10000","01110","00001","00001","11110"],
'T':["11111","00100","00100","00100","00100","00100","00100"],
'U':["10001","10001","10001","10001","10001","10001","01110"],
'V':["10001","10001","10001","10001","10001","01010","00100"],
'W':["10001","10001","10001","10101","10101","10101","01010"],
'X':["10001","10001","01010","00100","01010","10001","10001"],
'Y':["10001","10001","01010","00100","00100","00100","00100"],
'Z':["11111","00001","00010","00100","01000","10000","11111"],
'0':["01110","10001","10011","10101","11001","10001","01110"],
'1':["00100","01100","00100","00100","00100","00100","01110"],
'2':["01110","10001","00001","00010","00100","01000","11111"],
'3':["11111","00010","00100","00010","00001","10001","01110"],
'4':["00010","00110","01010","10010","11111","00010","00010"],
'5':["11111","10000","11110","00001","00001","10001","01110"],
'6':["00110","01000","10000","11110","10001","10001","01110"],
'7':["11111","00001","00010","00100","01000","01000","01000"],
'8':["01110","10001","10001","01110","10001","10001","01110"],
'9':["01110","10001","10001","01111","00001","00010","01100"],
' ':["00000"]*7,
'.':["00000","00000","00000","00000","00000","01100","01100"],
',':["00000","00000","00000","00000","01100","00100","01000"],
':':["00000","01100","01100","00000","01100","01100","00000"],
'-':["00000","00000","00000","11111","00000","00000","00000"],
'/':["00001","00010","00010","00100","01000","01000","10000"],
'(':["00010","00100","01000","01000","01000","00100","00010"],
')':["01000","00100","00010","00010","00010","00100","01000"],
'>':["01000","00100","00010","00001","00010","00100","01000"],
'<':["00010","00100","01000","10000","01000","00100","00010"],
"'":["01100","00100","01000","00000","00000","00000","00000"],
'!':["00100","00100","00100","00100","00100","00000","00100"],
'#':["01010","01010","11111","01010","11111","01010","01010"],
'+':["00000","00100","00100","11111","00100","00100","00000"],
'E2':["00000"]*7,
}

# 16-colour VGA-ish palette
P = {
 'blk':(0,0,0),'nav':(0,0,170),'grn':(0,170,0),'cyn':(0,170,170),'red':(170,0,0),
 'mag':(170,0,170),'brn':(170,85,0),'lgr':(170,170,170),'dgr':(85,85,85),
 'lbl':(85,85,255),'lgn':(85,255,85),'lcy':(85,255,255),'lrd':(255,85,85),
 'yel':(255,255,85),'wht':(255,255,255),
 'sky1':(48,72,200),'sky2':(88,120,232),'sky3':(140,170,250),'sky4':(200,214,255),
 'hill1':(24,96,24),'hill2':(40,136,40),'fair':(72,176,56),'sand':(232,208,140),
}

class Scr:
    def __init__(s, w, h, k):
        s.img = Image.new('RGB', (w, h)); s.k = k; s.px = s.img.load(); s.w=w; s.h=h
    def rect(s, x, y, w, h, c):
        k=s.k
        for yy in range(y*k, (y+h)*k):
            for xx in range(x*k, (x+w)*k):
                if 0<=xx<s.w and 0<=yy<s.h: s.px[xx,yy]=P[c] if isinstance(c,str) else c
    def text(s, x, y, t, c, scale=1, shadow=None):
        if shadow: s.text(x+scale, y+scale, t, shadow, scale)
        for i,ch in enumerate(t):
            g=G.get(ch, G[' '])
            for r,row in enumerate(g):
                for col,b in enumerate(row):
                    if b=='1': s.rect(x+(i*6+col)*scale, y+r*scale, scale, scale, c)
    def ctext(s, y, t, c, scale=1, shadow=None, w=160):
        s.text((w-len(t)*6*scale+scale)//2, y, t, c, scale, shadow)
    def box(s, x, y, w, h, title=None):
        # bevelled grey window, blue title bar, like a DOS dialog
        s.rect(x+2,y+2,w,h,'blk')             # drop shadow
        s.rect(x,y,w,h,'lgr')
        s.rect(x,y,w,1,'wht'); s.rect(x,y,1,h,'wht')
        s.rect(x,y+h-1,w,1,'dgr'); s.rect(x+w-1,y,1,h,'dgr')
        s.rect(x+2,y+2,w-4,h-4,'lgr')
        s.rect(x+2,y+2,w-4,1,'dgr'); s.rect(x+2,y+2,1,h-4,'dgr')
        s.rect(x+2,y+h-3,w-4,1,'wht'); s.rect(x+w-3,y+2,1,h-4,'wht')
        if title:
            s.rect(x+3,y+3,w-6,10,'nav')
            s.text(x+(w-len(title)*6)//2+1, y+5, title, 'wht')

def sky(s, top, bot):
    bands=['sky1','sky2','sky3','sky4']
    n=bot-top
    for y in range(top,bot):
        f=(y-top)*3/n
        a=int(f); t=f-a
        for x in range(160):
            # 2x2 ordered dither between bands, like a 16 colour mode would
            th=[[0.125,0.625],[0.875,0.375]][y%2][x%2]
            s.rect(x,y,1,1,bands[min(3,a+(1 if t>th else 0))])

def hills(s):
    import math
    for x in range(160):
        h1=int(100+8*math.sin(x/17.0)+5*math.sin(x/7.3+1))
        h2=int(116+6*math.sin(x/23.0+2)+3*math.sin(x/5.1))
        s.rect(x,h1,1,160-h1,'hill1')
        s.rect(x,h2,1,160-h2,'hill2')
    for y in range(128,160):
        for x in range(160):
            if (x+y)%2==0 or y>134: s.rect(x,y,1,1,'fair')
    # green, flag
    s.rect(98,124,26,4,'lgn'); s.rect(94,125,34,2,'lgn')
    s.rect(111,104,1,21,'wht'); s.rect(112,104,8,5,'red')
    s.rect(30,132,18,3,'sand'); s.rect(28,133,22,1,'sand')

def title(k):
    s=Scr(160*k,160*k,k)
    sky(s,0,110); hills(s)
    s.ctext(14,'PAR 386','yel',3,'red')
    s.ctext(40,'CHAMPIONSHIP GOLF','wht',1,'blk')
    s.ctext(146,'TAP OR PRESS A BUTTON','wht',1,'blk')
    return s

def menu(k):
    s=Scr(160*k,160*k,k)
    sky(s,0,110); hills(s)
    s.text(4,3,'PAR 386','yel',2,'red')
    items=['PLAY A ROUND','PRACTICE RANGE','PRACTICE GREEN','CHOOSE COURSE','PLAYERS','SETTINGS','COURSE RECORDS']
    s.box(20,24,120,98,'MAIN MENU')
    for i,t in enumerate(items):
        y=40+i*11
        if i==0:
            s.rect(24,y-2,112,11,'nav'); s.text(28,y,'>','yel'); s.text(36,y,t,'wht')
        else:
            s.text(36,y,t,'blk')
            s.text(36,y,t[0],'red')
    s.rect(0,150,160,10,'nav')
    s.text(3,152,'UP/DOWN MOVE  A: SELECT','lcy')
    return s

def dialog(k):
    s=Scr(160*k,160*k,k)
    s.rect(0,0,160,160,'nav')
    for y in range(0,160,2):
        for x in range((y//2)%2,160,2): s.rect(x,y,1,1,(0,0,120))
    s.box(4,6,152,122,'PLAYER SETUP')
    rows=[('1','DAN','HUMAN','SCR'),('2','ANGUS','CPU','8'),('3','',"",''),('4','','','')]
    s.text(10,20,'#  NAME     TYPE  HCP','blu' if False else 'nav')
    s.rect(10,28,140,1,'dgr')
    for i,(n,nm,ty,h) in enumerate(rows):
        y=33+i*12
        s.rect(22,y-2,54,11,'wht'); s.rect(22,y-2,54,1,'dgr'); s.rect(22,y-2,1,11,'dgr')
        s.text(10,y,n,'blk'); s.text(24,y,nm if nm else '-----','blk' if nm else 'dgr')
        if ty: s.text(82,y,ty,'blk'); s.text(124,y,h,'blk')
    s.text(10,86,'TEES:  < MIDDLE >','blk')
    s.text(10,97,'MODE:  STROKE PLAY','blk')
    for i,(lbl,x) in enumerate([('OK',22),('CANCEL',88)]):
        w=len(lbl)*6+12
        s.box(x,108,w,14)
        s.text(x+6,111,lbl,'blk'); s.text(x+6,111,lbl[0],'red')
    s.rect(0,150,160,10,'blk')
    s.text(3,152,'COURSE: AULD KIRK LINKS','lgn')
    return s

GREYS=[(0,0,0),(85,85,85),(170,170,170),(255,255,255)]
def to4(img):
    # 4 grey version with 2x2 ordered dither, as on the IIIx
    out=Image.new('RGB',img.size); p=img.load(); q=out.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            r,g,b=p[x,y]; L=(r*30+g*59+b*11)/100/255*3
            th=[[0.125,0.625],[0.875,0.375]][y%2][x%2]
            i=int(L); i=min(3,i+(1 if L-i>th else 0))
            q[x,y]=GREYS[i]
    return out

import sys
def scale2x(img):
    w,h=img.size; p=img.load(); o=Image.new('RGB',(w*2,h*2)); q=o.load()
    for y in range(h):
        for x in range(w):
            P0=p[x,y]; A=p[x,max(0,y-1)]; B=p[min(w-1,x+1),y]; C=p[max(0,x-1),y]; D=p[x,min(h-1,y+1)]
            q[2*x,2*y]=A if (C==A and C!=D and A!=B) else P0
            q[2*x+1,2*y]=B if (A==B and A!=C and B!=D) else P0
            q[2*x,2*y+1]=C if (D==C and D!=B and C!=A) else P0
            q[2*x+1,2*y+1]=D if (B==D and B!=A and D!=C) else P0
    return o
out=sys.argv[1]
screens=[title,menu,dialog]
Z=3
sheet=Image.new('RGB',(3*(160*Z+16)+16, 3*(160*Z+16)+16),(40,40,40))
for i,f in enumerate(screens):
    a=f(1).img
    sheet.paste(to4(a).resize((160*Z,160*Z),Image.NEAREST),(16+i*(160*Z+16),16))
    sheet.paste(a.resize((160*Z,160*Z),Image.NEAREST),(16+i*(160*Z+16),16+160*Z+16))
    h=scale2x(a)
    h.resize((640,640),Image.NEAREST).save(f'{out}/m1b-{f.__name__}-320-colour.png')
    sheet.paste(h.resize((160*Z,160*Z),Image.LANCZOS),(16+i*(160*Z+16),16+2*(160*Z+16)))
    a.resize((480,480),Image.NEAREST).save(f'{out}/m1b-{f.__name__}-160-colour.png')
    to4(a).resize((480,480),Image.NEAREST).save(f'{out}/m1b-{f.__name__}-160-grey.png')
sheet.save(f'{out}/m1b-mockup-sheet.png')
