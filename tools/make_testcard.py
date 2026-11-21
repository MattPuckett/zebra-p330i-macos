#!/usr/bin/env python3
"""Generate a 1030x646 @ 300 dpi test card (testcard.pdf) for the Zebra P330i.

Checks orientation (TOP LEFT / BOTTOM RIGHT), edge registration (border),
panel mapping (C M Y R G B K swatches), resin black (title, triangle, K swatch)
and tonal range (gradients).

Requires Pillow:  python3 -m pip install pillow
Print with:       lp -d Zebra_P330i testcard.pdf
"""
import colorsys
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1030, 646

FONT_CANDIDATES = {
    True: ["/System/Library/Fonts/Supplemental/Arial Bold.ttf",
           "/Library/Fonts/Arial Bold.ttf",
           "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"],
    False: ["/System/Library/Fonts/Supplemental/Arial.ttf",
            "/Library/Fonts/Arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"],
}


def font(size, bold=True):
    for path in FONT_CANDIDATES[bold]:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


im = Image.new("RGB", (W, H), "white")
d = ImageDraw.Draw(im)
d.rectangle([12, 12, W - 13, H - 13], outline="black", width=3)
d.polygon([(30, 30), (130, 30), (30, 130)], fill="black")
d.text((140, 32), "TOP LEFT", font=font(30), fill="black")
d.text((W - 300, H - 62), "BOTTOM RIGHT", font=font(30), fill="black")
d.text((40, 150), "ZEBRA P330i TEST", font=font(64), fill="black")
d.text((42, 228), "macOS CUPS driver  ·  rastertoepcl", font=font(28, False), fill="black")

swatches = [("C", (0, 255, 255)), ("M", (255, 0, 255)), ("Y", (255, 255, 0)),
            ("R", (255, 0, 0)), ("G", (0, 160, 0)), ("B", (0, 0, 255)), ("K", (0, 0, 0))]
for i, (name, rgb) in enumerate(swatches):
    x = 40 + i * 100
    d.rectangle([x, 300, x + 85, 385], fill=rgb)
    d.text((x + 30, 392), name, font=font(28), fill="black")

for x in range(40, W - 40):
    t = (x - 40) / (W - 80)
    r, g, b = colorsys.hsv_to_rgb(t, 1, 1)
    d.line([(x, 450), (x, 500)], fill=(int(r * 255), int(g * 255), int(b * 255)))
    v = int(255 * t)
    d.line([(x, 510), (x, 550)], fill=(v, v, v))

d.text((40, 565), "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz",
       font=font(18, False), fill="black")

im.save("testcard.png", dpi=(300, 300))
im.save("testcard.pdf", resolution=300.0)
print("wrote testcard.pdf (247.2 x 155.04 pt = 1030 x 646 dots @ 300 dpi)")
