from PIL import Image, ImageDraw, ImageFont
import math, sys

out = sys.argv[1]
BLUE = (0, 69, 140)
S = 2                               # draw at 2x for high-DPI screens

def triangle(d, cx, top, h, color, width):
    w = h * 27 / 24
    base = top + h
    left, right = cx - w / 2, cx + w / 2
    d.line([(cx, base), (left, base), (cx, top), (right, base), (cx, base)], fill=color, width=width, joint='curve')
    for f in (1 / 3, 2 / 3):
        y = top + h * f
        half = (w / 2) * f
        d.line([(cx - half, y), (cx + half, y)], fill=color, width=width)

def font(size, bold=False):
    for name in (["segoeuib.ttf", "verdanab.ttf"] if bold else ["segoeui.ttf", "verdana.ttf"]):
        try:
            return ImageFont.truetype("C:/Windows/Fonts/" + name, size)
        except OSError:
            pass
    return ImageFont.load_default()

W, H = 164 * S, 314 * S
img = Image.new('RGB', (W, H))
px = img.load()
top, bot = (0, 82, 160), (0, 38, 84)
for y in range(H):
    t = y / (H - 1)
    c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
    for x in range(W):
        px[x, y] = c
d = ImageDraw.Draw(img, 'RGBA')
for k in range(7):
    y0 = H * 0.55 + k * 18 * S
    pts = [(x, y0 + 10 * S * math.sin(x / (38.0 * S) + k * 0.7)) for x in range(0, W + 1, 2)]
    d.line(pts, fill=(255, 255, 255, 30 + 6 * k), width=2 * S, joint="curve")
triangle(d, W / 2, 46 * S, 62 * S, (255, 255, 255), int(5.5 * S))
f = font(17 * S, bold=True)
tw = d.textlength("CakeStudio", font=f)
d.text(((W - tw) / 2, 124 * S), "CakeStudio", font=f, fill=(255, 255, 255))
f2 = font(9 * S)
sub = "Computational Fluid Dynamics"
d.text(((W - d.textlength(sub, font=f2)) / 2, 150 * S), sub, font=f2, fill=(200, 220, 245))
img.save(out + "/banner.png")

H2 = 49 * S
hdr = Image.new('RGB', (H2 * 4, H2 * 4), (255, 255, 255))
triangle(ImageDraw.Draw(hdr), H2 * 2, H2 * 0.55, H2 * 2.9, BLUE, int(H2 * 0.24))
hdr.resize((H2, H2), Image.LANCZOS).save(out + "/header.png")
