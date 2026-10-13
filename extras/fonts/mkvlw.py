# Make TFT_eSPI smooth fonts (.vlw, Processing format version 11) from a TTF file.
# Usage: python mkvlw.py font.ttf size out.vlw "Font Name" "PostScriptName"
import struct, sys
from PIL import Image, ImageDraw, ImageFont

ttf, size, out, name, psname = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5]
font = ImageFont.truetype(ttf, size)
cmap = None
try:
    from fontTools.ttLib import TTFont
    cmap = set(TTFont(ttf).getBestCmap().keys())
except Exception:
    pass

# Same character set as the original fonts: ASCII, degree sign, Greek and Coptic
codes = [9, 10, 13] + list(range(0x20, 0x7F)) + [0xB0] + list(range(0x370, 0x400))
if cmap is not None:
    codes = [c for c in codes if c in (9, 10, 13) or c in cmap]

glyphs = []
for c in codes:
    ch = chr(c)
    adv = int(round(font.getlength(ch))) if c >= 0x20 else 0
    if c <= 0x20:  # whitespace: 1x1 empty bitmap, like Processing does
        glyphs.append((c, 1, 1, adv, 2 * size, -size, b'\x00'))
        continue
    # Draw on a large canvas with the pen at (ox, oy) on the baseline, then crop the ink
    ox, oy = 2 * size, 3 * size
    big = Image.new('L', (6 * size, 5 * size), 0)
    ImageDraw.Draw(big).text((ox, oy), ch, font=font, fill=255, anchor='ls')
    bb = big.getbbox()
    if bb is None:
        continue
    x0, y0, x1, y1 = bb
    im = big.crop(bb)
    glyphs.append((c, y1 - y0, x1 - x0, adv, oy - y0, x0 - ox, im.tobytes()))

gd = {g[0]: g for g in glyphs}
ascent = gd[ord('d')][4]                       # top of 'd' above baseline
descent = gd[ord('p')][1] - gd[ord('p')][4]    # bottom of 'p' below baseline

with open(out, 'wb') as f:
    f.write(struct.pack('>6i', len(glyphs), 11, size, 0, ascent, descent))
    for g in glyphs:
        f.write(struct.pack('>7i', g[0], g[1], g[2], g[3], g[4], g[5], 0))
    for g in glyphs:
        f.write(g[6])
    for s in (name, psname):
        b = s.encode('utf-8')
        f.write(struct.pack('>H', len(b)) + b)
    f.write(b'\x01')  # smooth
print(out, len(glyphs), 'glyphs, ascent', ascent, 'descent', descent)
