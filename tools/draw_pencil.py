# Makes resources/sprites/pencil.png (the rename button): py -3 tools/draw_pencil.py resources/sprites/pencil.png 150
# Draws the rename button's pencil in the game's style: flat colours, a light and a dark band, black outline.
import sys
from PIL import Image, ImageDraw, ImageFilter

K = 4                      # drawn this many times bigger, then scaled down (smooth edges)
L, W = 1000, 230           # pencil length and thickness, horizontal, tip on the left
PAD = 80
OUT = 15                   # outline, in final pixels * K / K
img = Image.new("RGBA", (L + 2 * PAD, W + 2 * PAD), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
top, bot = PAD, PAD + W
mid = (top + bot) / 2
x0 = PAD
CONE, LEAD, BODY_END, FERRULE_END = 270, 95, 760, 850

def band(x1, x2, light, base, dark):
    third = W / 3
    d.rectangle([x1, top, x2, top + third], fill=light)
    d.rectangle([x1, top + third, x2, top + 2 * third], fill=base)
    d.rectangle([x1, top + 2 * third, x2, bot], fill=dark)

# wood cone, then the lead at its point
d.polygon([(x0, mid), (x0 + CONE, top), (x0 + CONE, bot)], fill=(250, 214, 160))
d.polygon([(x0 + CONE * 0.45, top + W * 0.225), (x0 + CONE, top), (x0 + CONE, top + W * 0.33)], fill=(255, 236, 200))
f = LEAD / CONE
d.polygon([(x0, mid), (x0 + LEAD, mid - W / 2 * f), (x0 + LEAD, mid + W / 2 * f)], fill=(70, 70, 80))
# body, ferrule, eraser
band(x0 + CONE, x0 + BODY_END, (255, 236, 110), (255, 200, 30), (230, 150, 10))
band(x0 + BODY_END, x0 + FERRULE_END, (235, 235, 240), (190, 190, 200), (140, 140, 150))
er = Image.new("L", img.size, 0)
ImageDraw.Draw(er).rounded_rectangle([x0 + FERRULE_END - 40, top, x0 + L, bot], radius=70, fill=255)
er.paste(0, [0, 0, x0 + FERRULE_END, img.size[1]])
pink = Image.new("RGBA", img.size, (0, 0, 0, 0))
pd = ImageDraw.Draw(pink)
third = W / 3
pd.rectangle([x0 + FERRULE_END, top, x0 + L, top + third], fill=(255, 170, 190))
pd.rectangle([x0 + FERRULE_END, top + third, x0 + L, top + 2 * third], fill=(250, 120, 150))
pd.rectangle([x0 + FERRULE_END, top + 2 * third, x0 + L, bot], fill=(215, 80, 115))
img.paste(pink, (0, 0), er)
# black lines between the parts
LINE = 16
for x in (CONE, BODY_END, FERRULE_END):
    d.line([(x0 + x, top), (x0 + x, bot)], fill=(0, 0, 0), width=LINE)
d.line([(x0 + LEAD, mid - W / 2 * f), (x0 + LEAD, mid + W / 2 * f)], fill=(0, 0, 0), width=LINE)
for x in (BODY_END + 30, BODY_END + 60):
    d.line([(x0 + x, top + 10), (x0 + x, bot - 10)], fill=(110, 110, 120), width=8)

img = img.rotate(45, resample=Image.BICUBIC, expand=True)
# outline: the shape grown, in black, under it
alpha = img.getchannel("A").point(lambda a: 255 if a > 100 else 0)
grown = alpha.filter(ImageFilter.MaxFilter(2 * (OUT * K // 4) + 1)).filter(ImageFilter.MaxFilter(2 * (OUT * K // 4) + 1))
out = Image.new("RGBA", img.size, (0, 0, 0, 0))
out.paste((0, 0, 0, 255), (0, 0), grown)
out.alpha_composite(img)
out = out.crop(out.getbbox())
size = int(sys.argv[2])
w, h = out.size
s = size / max(w, h)
out = out.resize((round(w * s), round(h * s)), Image.LANCZOS)
out.save(sys.argv[1])
print(out.size)
