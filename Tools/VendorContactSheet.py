"""vendors: tile a Tools/RunVendorGallery.py capture folder into review sheets (one per body, plus the stalls/shop).

sheet_<body>.png: row 1 = the 4-view turnaround + face + back of head, row 2 = idle (full body, both hands, right
hand side, left hand side), row 3 = the same for the greet gesture. sheet_town.png: stalls, wide, nameplate, shop tabs.
Needs Pillow in Saved/pylib (see Tools/ProcessVendorArt.py).
Usage: python Tools/VendorContactSheet.py <gallery dir> [--width 480]
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Saved/pylib"))
from PIL import Image, ImageDraw  # noqa: E402


def tile(files, out, width):
    if not files:
        return
    rows = []
    for row in files:
        ims = []
        for f in row:
            im = Image.open(f).convert("RGB")
            im = im.resize((width, round(im.height * width / im.width)))
            ImageDraw.Draw(im).text((8, 6), re.sub(r"^\d+_", "", f.stem), fill=(255, 230, 120))
            ims.append(im)
        rows.append(ims)
    h = sum(max(i.height for i in r) for r in rows)
    w = max(len(r) for r in rows) * width
    sheet = Image.new("RGB", (w, h), (12, 12, 14))
    y = 0
    for r in rows:
        for i, im in enumerate(r):
            sheet.paste(im, (i * width, y))
        y += max(im.height for im in r)
    sheet.save(out)
    print(out)


def main():
    folder = Path(sys.argv[1])
    width = int(sys.argv[sys.argv.index("--width") + 1]) if "--width" in sys.argv else 480
    shots = sorted(folder.glob("*.png"))
    shots = [s for s in shots if not s.name.startswith("sheet_")]
    bodies = {}
    town = []
    for s in shots:
        m = re.match(r"\d+_(.+?)_(ref|idle|greet)_(.+)$", s.stem)
        if m:
            bodies.setdefault(m.group(1), {}).setdefault(m.group(2), []).append(s)
        else:
            town.append(s)
    for body, kinds in bodies.items():
        tile([kinds[k] for k in ("ref", "idle", "greet") if k in kinds], folder / f"sheet_{body}.png", width)
    if town:
        tile([town[i:i + 4] for i in range(0, len(town), 4)], folder / "sheet_town.png", width)


if __name__ == "__main__":
    main()
