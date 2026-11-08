"""Writes the overlay's two textures into data/ui/: a soft shadow for nine-slice panels and a radial glow.

Both are white with the shape in the alpha channel, so the overlay tints them with any colour. They are drawn
from scratch here: nothing comes from the game.

    python -I tools/make_ui_textures.py
"""
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'data', 'ui')


def shadow():
    # 128 x 128: a rounded rectangle inset by 40 px, blurred. Nine-slice insets of 48 keep the soft edge.
    size, inset, radius = 128, 40, 14
    a = Image.new('L', (size, size), 0)
    ImageDraw.Draw(a).rounded_rectangle((inset, inset, size - inset - 1, size - inset - 1), radius, fill=255)
    a = a.filter(ImageFilter.GaussianBlur(14))
    img = Image.new('RGBA', (size, size), (255, 255, 255, 0))
    img.putalpha(a)
    return img


def glow():
    # 256 x 256: a smooth radial falloff, strongest in the middle and gone at the edge.
    size = 256
    img = Image.new('RGBA', (size, size), (255, 255, 255, 0))
    px = img.load()
    c = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            d = (((x - c) ** 2 + (y - c) ** 2) ** 0.5) / c
            v = max(0.0, 1.0 - d)
            px[x, y] = (255, 255, 255, int(round(255 * v ** 2.2)))
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    shadow().save(os.path.join(OUT, 'shadow.png'), optimize=True)
    glow().save(os.path.join(OUT, 'glow.png'), optimize=True)
    print('wrote', os.path.normpath(OUT))


if __name__ == '__main__':
    main()
