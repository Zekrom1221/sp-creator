# Makes logo.png (the mod's icon in Geode): py -3 tools/draw_logo.py logo.png
# A startpos flag over "SP" in the game's style: flat colours, light and dark bands, black outlines.
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

SIZE = 336
K = 4                      # drawn this many times bigger, then scaled down (smooth edges)
S = SIZE * K
OUT = 9 * K                # outline width
FONT = Path(__file__).resolve().parent.parent / "resources" / "fonts" / "Ubuntu-Bold.ttf"

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# Background: a rounded square, lighter at the top.
pad, radius = 10 * K, 70 * K
box = [pad, pad, S - pad, S - pad]
d.rounded_rectangle(box, radius=radius, fill=(0, 0, 0))
inner = [pad + OUT, pad + OUT, S - pad - OUT, S - pad - OUT]
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle(inner, radius=radius - OUT, fill=255)
grad = Image.new("RGBA", (S, S))
gd = ImageDraw.Draw(grad)
top, bottom = (70, 190, 255), (20, 90, 200)
for y in range(S):
    t = y / (S - 1)
    gd.line([(0, y), (S, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
img.paste(grad, (0, 0), mask)

# The flag: a pole, and a cloth in three bands waving to the right.
pole_x, pole_top, pole_bottom = 92 * K, 52 * K, 196 * K
pw = 14 * K
d.rounded_rectangle([pole_x - pw / 2 - OUT, pole_top - OUT, pole_x + pw / 2 + OUT, pole_bottom + OUT], radius=10 * K, fill=(0, 0, 0))
d.rounded_rectangle([pole_x - pw / 2, pole_top, pole_x + pw / 2, pole_bottom], radius=6 * K, fill=(235, 235, 240))
cloth = [(pole_x + pw / 2, pole_top + 6 * K), (176 * K, pole_top - 4 * K), (262 * K, pole_top + 14 * K),
         (262 * K, pole_top + 84 * K), (176 * K, pole_top + 66 * K), (pole_x + pw / 2, pole_top + 76 * K)]
d.polygon(cloth, fill=(0, 0, 0))
d.line(cloth + [cloth[0]], fill=(0, 0, 0), width=2 * OUT, joint="curve")
for px, py in cloth:
    d.ellipse([px - OUT, py - OUT, px + OUT, py + OUT], fill=(0, 0, 0))
cm = Image.new("L", (S, S), 0)
ImageDraw.Draw(cm).polygon(cloth, fill=255)
bands = Image.new("RGBA", (S, S))
bd = ImageDraw.Draw(bands)
y0, y1 = pole_top - 4 * K, pole_top + 84 * K
third = (y1 - y0) / 3
bd.rectangle([0, y0, S, y0 + third], fill=(170, 255, 90))
bd.rectangle([0, y0 + third, S, y0 + 2 * third], fill=(110, 220, 40))
bd.rectangle([0, y0 + 2 * third, S, y1 + 10 * K], fill=(60, 160, 20))
img.paste(bands, (0, 0), cm)

# "SP": white letters, a black outline.
font = ImageFont.truetype(str(FONT), 150 * K)
text = "SP"
l, t, r, b = d.textbbox((0, 0), text, font=font, stroke_width=OUT * 2)
x = (S - (r - l)) / 2 - l
y = 300 * K - b
d.text((x, y), text, font=font, fill=(255, 255, 255), stroke_width=OUT * 2, stroke_fill=(0, 0, 0))

img = img.resize((SIZE, SIZE), Image.LANCZOS)
img.save(sys.argv[1] if len(sys.argv) > 1 else "logo.png")
