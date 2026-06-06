import sys
from PIL import Image

# Input: een gerenderde RGBA-PNG (transparant). Output: 256x256 WIT silhouet met alpha,
# zodat de in-game tint (categoriekleur) er netjes op werkt.
src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGBA")
a = im.getchannel("A")
bbox = a.getbbox()
if bbox:
    im = im.crop(bbox)
w, h = im.size
s = max(w, h)
pad = int(s * 0.10)  # kleine marge rondom
s2 = s + pad * 2
canvas = Image.new("RGBA", (s2, s2), (0, 0, 0, 0))
canvas.paste(im, ((s2 - w) // 2, (s2 - h) // 2), im)
canvas = canvas.resize((256, 256), Image.LANCZOS)
alpha = canvas.getchannel("A")
white = Image.new("L", canvas.size, 255)
out = Image.merge("RGBA", (white, white, white, alpha))
out.save(dst)
# sanity: % zichtbare pixels (alpha>20) zonder numpy
hist = alpha.histogram()
total = sum(hist)
vis = (sum(hist[21:]) / total * 100.0) if total else 0.0
print("saved", dst, "visible=%.1f%%" % vis)
