# Minimal SVG <path> rasterizer -> white-on-transparent PNG icon (no cairo needed).
# Handles M/L/H/V/C/S/Q/T/A/Z (abs+rel), flattens curves+arcs, even-odd fill (holes).
# Usage: py Tools/svg_to_icon.py <in.svg> <out.png> [size]
import sys, re, math
from PIL import Image, ImageDraw, ImageChops

def parse_path(d):
    toks = re.findall(r'[MmLlHhVvCcSsQqTtAaZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?', d)
    i = 0
    def num():
        nonlocal i; v = float(toks[i]); i += 1; return v
    subs = []          # list of point-lists
    cur = []           # current subpath
    x = y = 0.0
    sx = sy = 0.0
    cmd = None
    pcx = pcy = None   # previous cubic ctrl (for S)
    pqx = pqy = None   # previous quad ctrl (for T)
    def moveto(nx, ny):
        nonlocal cur, x, y, sx, sy
        if cur: subs.append(cur)
        cur = [(nx, ny)]; x, y, sx, sy = nx, ny, nx, ny
    def lineto(nx, ny):
        nonlocal x, y; cur.append((nx, ny)); x, y = nx, ny
    def cubic(x1,y1,x2,y2,nx,ny,n=18):
        nonlocal x,y
        for k in range(1,n+1):
            t=k/n; mt=1-t
            px=mt*mt*mt*x+3*mt*mt*t*x1+3*mt*t*t*x2+t*t*t*nx
            py=mt*mt*mt*y+3*mt*mt*t*y1+3*mt*t*t*y2+t*t*t*ny
            cur.append((px,py))
        x,y=nx,ny
    def quad(x1,y1,nx,ny,n=16):
        nonlocal x,y
        for k in range(1,n+1):
            t=k/n; mt=1-t
            px=mt*mt*x+2*mt*t*x1+t*t*nx
            py=mt*mt*y+2*mt*t*y1+t*t*ny
            cur.append((px,py))
        x,y=nx,ny
    def arc(rx,ry,phi,laf,sf,nx,ny):
        nonlocal x,y
        if rx==0 or ry==0: lineto(nx,ny); return
        ph=math.radians(phi); cs=math.cos(ph); sn=math.sin(ph)
        dx=(x-nx)/2; dy=(y-ny)/2
        x1p= cs*dx+sn*dy; y1p=-sn*dx+cs*dy
        rx=abs(rx); ry=abs(ry)
        lam=x1p*x1p/(rx*rx)+y1p*y1p/(ry*ry)
        if lam>1: s=math.sqrt(lam); rx*=s; ry*=s
        num=rx*rx*ry*ry-rx*rx*y1p*y1p-ry*ry*x1p*x1p
        den=rx*rx*y1p*y1p+ry*ry*x1p*x1p
        co=math.sqrt(max(0.0,num/den)) if den>0 else 0.0
        if laf==sf: co=-co
        cxp=co*rx*y1p/ry; cyp=-co*ry*x1p/rx
        cx=cs*cxp-sn*cyp+(x+nx)/2; cy=sn*cxp+cs*cyp+(y+ny)/2
        def ang(ux,uy,vx,vy):
            dot=ux*vx+uy*vy; ln=math.hypot(ux,uy)*math.hypot(vx,vy)
            a=math.acos(max(-1.0,min(1.0,dot/ln))) if ln>0 else 0.0
            return -a if (ux*vy-uy*vx)<0 else a
        t1=ang(1,0,(x1p-cxp)/rx,(y1p-cyp)/ry)
        dt=ang((x1p-cxp)/rx,(y1p-cyp)/ry,(-x1p-cxp)/rx,(-y1p-cyp)/ry)
        if sf==0 and dt>0: dt-=2*math.pi
        if sf==1 and dt<0: dt+=2*math.pi
        n=max(2,int(abs(dt)/(math.pi/16))+1)
        for k in range(1,n+1):
            t=t1+dt*k/n
            px=cx+rx*math.cos(t)*cs-ry*math.sin(t)*sn
            py=cy+rx*math.cos(t)*sn+ry*math.sin(t)*cs
            cur.append((px,py))
        x,y=nx,ny
    while i < len(toks):
        t = toks[i]
        if re.match(r'[A-Za-z]', t): cmd = t; i += 1
        rel = cmd.islower(); C = cmd.upper()
        if C=='M':
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0); moveto(nx,ny); cmd='l' if rel else 'L'
        elif C=='L':
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0); lineto(nx,ny)
        elif C=='H':
            nx=num()+(x if rel else 0); lineto(nx,y)
        elif C=='V':
            ny=num()+(y if rel else 0); lineto(x,ny)
        elif C=='C':
            x1=num()+(x if rel else 0); y1=num()+(y if rel else 0)
            x2=num()+(x if rel else 0); y2=num()+(y if rel else 0)
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0)
            cubic(x1,y1,x2,y2,nx,ny); pcx,pcy=x2,y2
        elif C=='S':
            x1,y1=(2*x-pcx,2*y-pcy) if pcx is not None else (x,y)
            x2=num()+(x if rel else 0); y2=num()+(y if rel else 0)
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0)
            cubic(x1,y1,x2,y2,nx,ny); pcx,pcy=x2,y2
        elif C=='Q':
            x1=num()+(x if rel else 0); y1=num()+(y if rel else 0)
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0)
            quad(x1,y1,nx,ny); pqx,pqy=x1,y1
        elif C=='T':
            x1,y1=(2*x-pqx,2*y-pqy) if pqx is not None else (x,y)
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0)
            quad(x1,y1,nx,ny); pqx,pqy=x1,y1
        elif C=='A':
            rx=num(); ry=num(); phi=num(); laf=num(); sf=num()
            nx=num()+(x if rel else 0); ny=num()+(y if rel else 0)
            arc(rx,ry,phi,laf,sf,nx,ny)
        elif C=='Z':
            cur.append((sx,sy)); x,y=sx,sy
        if C not in ('C','S'): pcx=pcy=None
        if C not in ('Q','T'): pqx=pqy=None
    if cur: subs.append(cur)
    return subs

def main():
    inp, outp = sys.argv[1], sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 256
    svg = open(inp, encoding='utf-8').read()
    vb = re.search(r'viewBox="([\d.\- ]+)"', svg)
    if vb:
        _,_,vw,vh = [float(v) for v in vb.group(1).split()]
    else:
        vw=vh=512.0
    ds = re.findall(r'<path[^>]*\bd="([^"]+)"', svg)
    SS = 4  # supersample
    W = size*SS
    acc = Image.new('1', (W,W), 0)
    for d in ds:
        for sub in parse_path(d):
            if len(sub) < 3: continue
            pts = [((px/vw)*W, (py/vh)*W) for (px,py) in sub]
            m = Image.new('1', (W,W), 0)
            ImageDraw.Draw(m).polygon(pts, fill=1)
            acc = ImageChops.logical_xor(acc, m)   # even-odd -> holes
    a = acc.convert('L').resize((size,size), Image.LANCZOS)
    rgba = Image.new('RGBA', (size,size), (255,255,255,0))
    rgba.putalpha(a)
    rgba.save(outp)
    print('saved', outp, rgba.size, 'alpha', a.getextrema())

if __name__ == '__main__':
    main()
