"""Draws icon.png: Silly Season's own artwork (no game, EA or F1 assets).

Uso: python -I tools/make_icon.py icon.png
"""
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 512
RED = (225, 6, 0)
WHITE = (245, 245, 247)


def font(names, size):
    for n in names:
        try:
            return ImageFont.truetype(n, size)
        except OSError:
            continue
    return ImageFont.load_default()


img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

# background: a dark rounded square with a vertical gradient
bg = Image.new("RGBA", (S, S))
px = bg.load()
for y in range(S):
    t = y / (S - 1)
    c = tuple(int(a + (b - a) * t) for a, b in zip((32, 34, 44), (10, 11, 15)))
    for x in range(S):
        px[x, y] = c + (255,)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=96, fill=255)
img.paste(bg, (0, 0), mask)
d = ImageDraw.Draw(img)

# the red bar on top, as on the panel
d.rounded_rectangle((0, 0, S - 1, 40), radius=0, fill=RED)
img.putalpha(Image.composite(img.getchannel("A"), Image.new("L", (S, S), 0), mask))
d = ImageDraw.Draw(img)

# two transfer arrows crossing: one goes, one comes
def arrow(y, left_to_right, colour):
    x0, x1, h = 96, 416, 30
    if left_to_right:
        d.rectangle((x0, y - h // 2 + 6, x1 - 50, y + h // 2 - 6), fill=colour)
        d.polygon([(x1 - 60, y - h), (x1, y), (x1 - 60, y + h)], fill=colour)
    else:
        d.rectangle((x0 + 50, y - h // 2 + 6, x1, y + h // 2 - 6), fill=colour)
        d.polygon([(x0 + 60, y - h), (x0, y), (x0 + 60, y + h)], fill=colour)


arrow(118, True, WHITE)
arrow(178, False, RED)

# the monogram
big = font(["seguibl.ttf", "ariblk.ttf", "arialbd.ttf"], 210)
text = "SS"
box = d.textbbox((0, 0), text, font=big)
w, h = box[2] - box[0], box[3] - box[1]
shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ImageDraw.Draw(shadow).text(((S - w) / 2 - box[0] + 6, 222 - box[1] + 8), text, font=big, fill=(0, 0, 0, 160))
img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(6)))
d = ImageDraw.Draw(img)
d.text(((S - w) / 2 - box[0], 222 - box[1]), text, font=big, fill=WHITE)

# the breaking-news strip with the name
d.rounded_rectangle((40, 420, S - 40, 476), radius=10, fill=RED)
small = font(["seguisb.ttf", "arialbd.ttf"], 34)
label = "SILLY SEASON"
lb = d.textbbox((0, 0), label, font=small)
d.text(((S - (lb[2] - lb[0])) / 2 - lb[0], 448 - (lb[3] - lb[1]) / 2 - lb[1]), label, font=small, fill=WHITE)

img.save(sys.argv[1], optimize=True)
print("wrote", sys.argv[1])
