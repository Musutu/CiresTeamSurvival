"""vendors: cut Eric's ChatGPT vendor art (Art/Vendors) into game textures.

* Art/Vendors/cts_vendor_<id>_sign.png -> Content/UI/Vendors/src/T_VendorSign_<Id>.png
  The flat grey studio background is keyed out (flood fill from the border, soft 1 px edge), the
  board is trimmed to its bounds and scaled to 1024 px wide (RGBA).
* The sign's central emblem -> Content/UI/Vendors/src/T_VendorEmblem_<Id>.png (256 px square,
  keyed) for the shop's vendor tabs, the minimap and the nameplate crest.
* Art/Vendors/cts_vendor_<id>_ref.png -> Saved/VendorViews/<id>_{front,right,back}.png: the Tripo
  multi-view inputs (front, right profile, back), each padded to a square on the sheet grey.

Needs Pillow:  <UE python> -m pip install --target Saved/pylib pillow
Run:           F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/ProcessVendorArt.py
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Saved/pylib"))
from PIL import Image, ImageFilter  # noqa: E402

ART = ROOT / "Art/Vendors"
OUT = ROOT / "Content/UI/Vendors/src"
VIEWS = ROOT / "Saved/VendorViews"
VENDORS = {"arcane": "Arcane", "armory": "Armory", "weaponsmith": "Weaponsmith"}
# Emblem discs on the 1536x1024 signs (centre x, centre y, radius): the book medallion, the gauntlet
# shield and the crossed-blades medallion. Cut as a soft-edged disc (the UI rings it in gold).
EMBLEM = {"arcane": (768, 300, 250), "armory": (770, 378, 262), "weaponsmith": (768, 392, 236)}


def disc(im, cx, cy, r):
    crop = im.crop((cx - r, cy - r, cx + r, cy + r)).convert("RGBA")
    size = 2 * r
    mask = Image.new("L", (size * 4, size * 4), 0)
    from PIL import ImageDraw
    ImageDraw.Draw(mask).ellipse((6, 6, size * 4 - 6, size * 4 - 6), fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    alpha = crop.getchannel("A")
    crop.putalpha(Image.composite(alpha, Image.new("L", crop.size, 0), mask))
    return crop


def key_background(im, tolerance=38):
    """Alpha-key the flat studio grey that touches the border (flood fill), with a 1 px soft edge."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    bg = px[4, 4][:3]
    seen = bytearray(w * h)
    stack = [(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)] + [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    close = lambda p: abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]) <= tolerance
    mask = Image.new("L", (w, h), 255)
    mp = mask.load()
    while stack:
        x, y = stack.pop()
        i = y * w + x
        if seen[i]:
            continue
        seen[i] = 1
        if not close(px[x, y]):
            continue
        mp[x, y] = 0
        if x > 0: stack.append((x - 1, y))
        if x < w - 1: stack.append((x + 1, y))
        if y > 0: stack.append((x, y - 1))
        if y < h - 1: stack.append((x, y + 1))
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    im.putalpha(mask)
    return im


def signs():
    OUT.mkdir(parents=True, exist_ok=True)
    for vid, name in VENDORS.items():
        src = ART / f"cts_vendor_{vid}_sign.png"
        if not src.exists():
            print("missing", src)
            continue
        full = key_background(Image.open(src))
        emblem = disc(full, *EMBLEM[vid])
        board = full.crop(full.getbbox())
        board = board.resize((1024, max(1, round(board.height * 1024 / board.width))), Image.LANCZOS)
        board.save(OUT / f"T_VendorSign_{name}.png")
        emblem.resize((256, 256), Image.LANCZOS).save(OUT / f"T_VendorEmblem_{name}.png")
        print("sign", vid, board.size)


def views():
    VIEWS.mkdir(parents=True, exist_ok=True)
    for vid in VENDORS:
        src = ART / f"cts_vendor_{vid}_ref.png"
        if not src.exists():
            continue
        im = Image.open(src).convert("RGB")
        w, h = im.size
        px = im.load()
        bg = px[5, 5]
        fg = lambda x, y: sum(abs(px[x, y][i] - bg[i]) for i in range(3)) > 40
        cols = [sum(fg(x, y) for y in range(0, h, 3)) for x in range(w)]
        runs, start = [], None
        for x, c in enumerate(cols + [0]):
            if c > 2 and start is None: start = x
            if c <= 2 and start is not None: runs.append((start, x)); start = None
        merged = []
        for r in runs:
            if merged and r[0] - merged[-1][1] < 25: merged[-1] = (merged[-1][0], r[1])
            else: merged.append(r)
        merged = [r for r in merged if r[1] - r[0] > 60]
        if len(merged) == 2:  # profile and back touching: split at the emptiest column
            a, b = merged[1]
            k = min(range(a + (b - a) // 3, b - (b - a) // 3), key=lambda x: cols[x])
            merged = [merged[0], (a, k), (k, b)]
        for view, (a, b) in zip(("front", "right", "back"), merged):
            rows = [y for y in range(h) if any(fg(x, y) for x in range(a, b, 2))]
            crop = im.crop((a, max(0, rows[0] - 20), b, min(h, rows[-1] + 20)))
            side = int(max(crop.size) * 1.08)
            square = Image.new("RGB", (side, side), bg)
            square.paste(crop, ((side - crop.width) // 2, (side - crop.height) // 2))
            square.save(VIEWS / f"{vid}_{view}.png")
        print("views", vid, merged)


if __name__ == "__main__":
    signs()
    if "--views" in sys.argv:
        views()
