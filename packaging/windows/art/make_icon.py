from PIL import Image, ImageDraw
import sys

BLUE = (0, 69, 140, 255)


def tri(size):
    S = 16                      # supersample for smooth edges
    N = size * S
    im = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = N * 0.06
    h = N - 2 * pad
    w = min(h * 27 / 24, N - 2 * pad)   # aspect from the reference glyph
    h = w * 24 / 27
    cx = N / 2
    top = (N - h) / 2
    base = top + h
    left, right = cx - w / 2, cx + w / 2
    # stroke ~8% of height, thicker at tiny sizes so it stays visible
    sw = h * (0.08 if size >= 48 else 0.11)
    # start mid-base so the open path ends join on a flat edge, not the apex
    d.line([(cx, base), (left, base), (cx, top), (right, base), (cx, base)], fill=BLUE, width=int(sw), joint='curve')
    for f in (1 / 3, 2 / 3):    # two band lines at equal heights
        y = top + h * f
        half = (w / 2) * f
        d.line([(cx - half, y), (cx + half, y)], fill=BLUE, width=int(sw))
    return im.resize((size, size), Image.LANCZOS)


out = sys.argv[1]
sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [tri(s) for s in sizes]
imgs[-1].save(out, format='ICO', sizes=[(s, s) for s in sizes], append_images=imgs[:-1])

# preview on dark and light backgrounds
pv = Image.new('RGBA', (600, 300), (30, 30, 30, 255))
pv.paste((240, 240, 240, 255), (300, 0, 600, 300))
big = tri(200)
pv.alpha_composite(big, (50, 20))
pv.alpha_composite(big, (350, 20))
x = 10
for s, im in zip(sizes[:4], imgs[:4]):
    pv.alpha_composite(im, (x, 300 - s - 6))
    pv.alpha_composite(im, (300 + x, 300 - s - 6))
    x += s + 10
pv.save(out.replace('.ico', '_preview.png'))
