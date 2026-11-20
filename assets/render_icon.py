from PIL import Image, ImageDraw
from pathlib import Path
import math
S=1024
im=Image.new("RGBA",(S,S),(0,0,0,0))
# Rounded-square teal-to-blue gradient, matching the user-approved artwork.
pix=im.load()
for x in range(S):
    t=x/(S-1)
    c=(int(73+(8-73)*t),int(189+(126-189)*t),int(179+(184-179)*t),255)
    for y in range(S): pix[x,y]=c
mask=Image.new("L",(S,S),0)
ImageDraw.Draw(mask).rounded_rectangle((0,0,S-1,S-1),radius=232,fill=255)
im.putalpha(mask)
d=ImageDraw.Draw(im)
d.ellipse((174,174,850,850),fill="white")
# Smooth wave: sampled Catmull-like sine blend following the supplied logo's path.
pts=[]
for i in range(1200):
    x=130+i*(894/1199)
    y=565 - (47*math.sin((x-130)/42) * math.exp(-((x-390)/240)**2)
             + 43*math.sin((x-455)/35) * math.exp(-((x-720)/230)**2))
    # broad, smooth undulations matching the reference icon
    y=565 - 54*math.sin((x-180)/48)*math.exp(-((x-380)/210)**2) + 48*math.sin((x-500)/43)*math.exp(-((x-760)/260)**2)
    pts.append((x,y))
# Draw gradient stroke in short connected segments for a continuous multi-colour wave.
colors=[(229,241,248),(166,137,194),(151,40,158),(210,44,124),(234,77,92),(241,138,55)]
for i in range(len(pts)-1):
    t=i/(len(pts)-2)*(len(colors)-1)
    k=min(int(t),len(colors)-2); a=t-k
    col=tuple(round(colors[k][j]*(1-a)+colors[k+1][j]*a) for j in range(3))+(255,)
    d.line([pts[i],pts[i+1]],fill=col,width=35)
out = Path(__file__).resolve().parent / "BladeScope.ico"
im.save(str(out),format="ICO",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print(f"generated {out}")
