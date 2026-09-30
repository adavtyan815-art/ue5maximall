# Generates T_UI_RadialGlow.png: white, alpha = CSS radial-gradient(ellipse at center, 1 -> 0 at 70%)
# (MaxiMall Salon Home.dc.html, line 24). The brush tint (white, alpha 0.06) supplies the 6 %.
# Plain Python 3, no packages: writes an 8-bit RGBA PNG.
import math, os, struct, zlib

N = 256
R = 0.7 * math.sqrt(2)          # farthest-corner ellipse, 70 % stop
rows = []
for y in range(N):
    row = bytearray([0])        # filter: none
    for x in range(N):
        u = (x + 0.5) / N * 2 - 1
        v = (y + 0.5) / N * 2 - 1
        a = max(0.0, 1.0 - math.sqrt(u * u + v * v) / R)
        row += bytes((255, 255, 255, round(a * 255)))
    rows.append(bytes(row))

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "T_UI_RadialGlow.png")
open(out, "wb").write(png)
print(out)
