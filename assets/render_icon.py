"""Render a BladeScope Windows icon without Cairo/native SVG dependencies."""
from PIL import Image, ImageDraw
import math

SIZE = 768
im = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
px = im.load()
for y in range(SIZE):
    for x in range(SIZE):
        # Rounded square, radius 125 px.
        r = 125
        cx = min(max(x, r), SIZE-r-1)
        cy = min(max(y, r), SIZE-r-1)
        if (x-cx)**2 + (y-cy)**2 > r*r:
            continue
        t = (x + y) / (2*(SIZE-1))
        px[x,y] = (int(18+13*t), int(54+131*t), int(116+35*t), 255)

d = ImageDraw.Draw(im)
baseline = 380
def bump(x, mu, spread):
    return math.exp(-0.5*((x-mu)/spread)**2)
def wave(x, shift):
    return (64*bump(x,145+shift,22)
            +220*bump(x,240+shift,31)
            -180*bump(x,302+shift,30)
            +110*bump(x,360+shift,34)
            -105*bump(x,425+shift,34)
            +150*bump(x,500+shift,30)
            -112*bump(x,558+shift,31)
            +65*bump(x,620+shift,25))
for color, shift in [("#2598dd",16),("#37ca58",0),("#ff433c",-14),("#ff9824",55)]:
    points = [(x, round(baseline-wave(x,shift))) for x in range(0,SIZE,2)]
    d.line(points, fill=color, width=12, joint="curve")

# Three white rotor blades and tower.
hub=(592,505)
d.polygon([(582,519),(602,519),(610,699),(573,699)], fill="white")
for angle in [-math.pi/2, math.pi/6, 5*math.pi/6]:
    ux,uy=math.cos(angle),math.sin(angle)
    vx,vy=-uy,ux
    pts=[(hub[0]+ux*12+vx*9,hub[1]+uy*12+vy*9),
         (hub[0]+ux*145+vx*3,hub[1]+uy*145+vy*3),
         (hub[0]+ux*160,hub[1]+uy*160),
         (hub[0]+ux*14-vx*8,hub[1]+uy*14-vy*8)]
    d.polygon(pts,fill="white")
d.ellipse((hub[0]-15,hub[1]-15,hub[0]+15,hub[1]+15),fill="white")
im.save("customization/assets/BladeScope.ico",format="ICO",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print("BladeScope.ico generated")
