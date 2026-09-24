"""Generate the original procedural textures of the WoW-style HUD (pure Python, no deps).

Writes RGBA PNGs to Content/UI/WowUI/Textures/src; Tools/BuildWowUIContent.py imports them
as UI textures (/Game/UI/WowUI/Textures/T_*). Everything is synthesized here from noise and
geometry, so the art is original to the project (see Content/UI/WowUI/LICENSES.md).
"""
from pathlib import Path
import math
import random
import struct
import zlib

OUT = Path(__file__).resolve().parent.parent / "Content/UI/WowUI/Textures/src"


def png(path, w, h, pixels):
    """pixels: list of (r,g,b,a) floats 0..1, row-major."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b, a = pixels[y * w + x]
            raw += bytes(max(0, min(255, int(round(c * 255)))) for c in (r, g, b, a))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def value_noise(size, cells, seed):
    """Tileable smooth value noise in 0..1."""
    rnd = random.Random(seed)
    grid = [[rnd.random() for _ in range(cells)] for _ in range(cells)]
    out = []
    for y in range(size):
        for x in range(size):
            fx, fy = x / size * cells, y / size * cells
            x0, y0 = int(fx) % cells, int(fy) % cells
            x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
            tx, ty = fx - int(fx), fy - int(fy)
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            a = grid[y0][x0] * (1 - tx) + grid[y0][x1] * tx
            b = grid[y1][x0] * (1 - tx) + grid[y1][x1] * tx
            out.append(a * (1 - ty) + b * ty)
    return out


def fbm(size, seed, octaves=((4, .5), (8, .25), (16, .15), (32, .1))):
    total = [0.0] * (size * size)
    for i, (cells, amp) in enumerate(octaves):
        n = value_noise(size, cells, seed + i * 17)
        total = [t + v * amp for t, v in zip(total, n)]
    lo, hi = min(total), max(total)
    return [(t - lo) / (hi - lo) for t in total]


def panel():
    """Dark slate / worn leather background, tileable 256x256."""
    s = 256
    n = fbm(s, 11)
    grain = value_noise(s, 64, 99)
    px = []
    for i, v in enumerate(n):
        g = grain[i]
        base = .055 + .05 * v + .02 * (g - .5)
        px.append((base * .92, base * 1.0, base * 1.18 + .006, 1.0))
    png(OUT / "T_Panel.png", s, s, px)


def border():
    """9-slice frame, 128x128 with 32px corners: dark iron, gold bevel trim, corner rivets."""
    s, c = 128, 32
    px = []
    noise = value_noise(s, 16, 5)
    for y in range(s):
        for x in range(s):
            d = min(x, y, s - 1 - x, s - 1 - y)  # distance to outer edge
            a = 0.0
            col = (0, 0, 0)
            n = noise[y * s + x]
            if d < 11:
                # iron band with bevel: light on top/left edges, dark on bottom/right
                lit = .5 + .5 * ((x < s - 1 - x) * (x == d) + (y < s - 1 - y) * (y == d)) - .35 * ((s - 1 - x == d) + (s - 1 - y == d))
                t = d / 10.0
                iron = .16 + .08 * math.sin(t * math.pi) + .05 * (n - .5)
                if d in (3, 4):  # gold trim line
                    col = (.78 + .1 * n, .58 + .08 * n, .26, )
                elif d == 0 or d == 10:
                    col = (.02, .02, .025)
                else:
                    col = (iron * 1.05 * lit + .02, iron * lit + .02, iron * .95 * lit + .025)
                a = 1.0
            # corner rivet / gem studs
            for cx, cy in ((c // 2 - 7, c // 2 - 7), (s - c // 2 + 6, c // 2 - 7), (c // 2 - 7, s - c // 2 + 6), (s - c // 2 + 6, s - c // 2 + 6)):
                r = math.hypot(x - cx, y - cy)
                if r < 6.5:
                    h = max(0.0, 1 - math.hypot(x - cx + 2, y - cy + 2) / 5.5)
                    col = (.55 + .45 * h, .38 + .4 * h, .14 + .3 * h)
                    a = 1.0
                elif r < 7.8:
                    col, a = (.03, .02, .02), 1.0
            px.append((col[0], col[1], col[2], a))
    png(OUT / "T_Border.png", s, s, px)


def button(name, trim, inner, ornate=False, octagon=False):
    """64x64 action-button frame; transparent centre where the icon shows."""
    s = 64
    px = []
    for y in range(s):
        for x in range(s):
            if octagon:
                dx, dy = abs(x - 31.5), abs(y - 31.5)
                edge = 31.5 - max(dx, dy, (dx + dy) / 1.414 * 1.06)
                d = edge
            else:
                d = min(x, y, s - 1 - x, s - 1 - y)
            a, col = 0.0, (0, 0, 0)
            if d < 0:
                px.append((0, 0, 0, 0))
                continue
            if d < 6:
                lit = 1.0 if (x < 32 and d == x) or (y < 32 and d == y) else .55
                t = d / 5.0
                bright = .45 + .55 * math.sin(t * math.pi)
                col = (trim[0] * bright * lit, trim[1] * bright * lit, trim[2] * bright * lit)
                if d < 1 or d > 5:
                    col = (.02, .015, .01)
                a = 1.0
            elif d < 9:
                # inner shadow fading into the icon
                a = (9 - d) / 3 * .7
                col = inner
            if ornate:
                for cx, cy in ((3, 3), (60, 3), (3, 60), (60, 60)):
                    r = math.hypot(x - cx, y - cy)
                    if r < 5:
                        h = max(0, 1 - math.hypot(x - cx + 1.5, y - cy + 1.5) / 4)
                        col, a = (1.0 * (.6 + .4 * h), .85 * (.55 + .45 * h), .35 + .4 * h), 1.0
            px.append((col[0], col[1], col[2], a))
    png(OUT / f"{name}.png", s, s, px)


def glow():
    s = 64
    px = []
    for y in range(s):
        for x in range(s):
            r = math.hypot(x - 31.5, y - 31.5) / 32
            a = max(0.0, 1 - r) ** 2.2
            px.append((1, 1, 1, a))
    png(OUT / "T_Glow.png", s, s, px)


def gloss():
    """Vertical bar gradient (tinted in code): bright lip, body, darker base."""
    w, h = 4, 64
    px = []
    for y in range(h):
        t = y / (h - 1)
        v = .78 + .22 * math.exp(-((t - .12) / .08) ** 2) - .32 * t
        if t < .04:
            v = 1.0
        px.extend([(v, v, v, 1)] * w)
    png(OUT / "T_Gloss.png", w, h, px)


def icon_bg():
    """Radial vignette behind ability sigils (tinted by school/role)."""
    s = 64
    n = fbm(s, 41, ((4, .6), (8, .3), (16, .1)))
    px = []
    for y in range(s):
        for x in range(s):
            r = math.hypot(x - 26, y - 24) / 44
            v = max(0.0, 1 - r) ** 1.4 * (.75 + .35 * n[y * s + x])
            px.append((v, v, v, 1))
    png(OUT / "T_IconBg.png", s, s, px)


def gem():
    s = 32
    px = []
    for y in range(s):
        for x in range(s):
            r = math.hypot(x - 15.5, y - 15.5)
            if r > 14:
                px.append((0, 0, 0, 0 if r > 15.5 else .9))
                continue
            h = max(0.0, 1 - math.hypot(x - 11, y - 10) / 9)
            v = .35 + .45 * (1 - r / 14) + .6 * h ** 2
            px.append((min(1, v), min(1, v), min(1, v), 1))
    png(OUT / "T_Gem.png", s, s, px)


def header():
    """Ornamental title bar: dark leather with a gold filigree underline."""
    w, h = 256, 32
    n = fbm(64, 77, ((4, .6), (8, .4)))
    px = []
    for y in range(h):
        for x in range(w):
            v = n[(y * 2 % 64) * 64 + (x // 4) % 64]
            fade = min(1.0, x / 40, (w - 1 - x) / 40)
            base = (.10 + .05 * v, .07 + .04 * v, .05 + .02 * v)
            a = .92 * fade
            if y in (h - 4, h - 3):
                base, a = (.85, .64, .28), fade
            elif y == h - 2:
                base, a = (.2, .12, .05), fade
            px.append((base[0], base[1], base[2], a))
    png(OUT / "T_Header.png", w, h, px)


if __name__ == "__main__":
    panel(); border(); glow(); gloss(); icon_bg(); gem(); header()
    button("T_Button", (.80, .62, .30), (0, 0, 0))
    button("T_ButtonUlt", (1.0, .80, .30), (.25, .12, 0), ornate=True)
    button("T_ButtonPassive", (.68, .62, .86), (0, 0, 0), octagon=True)
    print("CIRE_WOWUI_TEXTURES_WRITTEN", OUT)
