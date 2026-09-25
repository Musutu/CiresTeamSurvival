"""Contact sheet of icon PNGs at gameplay sizes, to check consistency, legibility and mapping.

  python Tools/IconContactSheet.py DIR OUT.png [--size 64] [--cols 8]

Each cell shows the icon at --size (plus a 40 px copy) over the HUD slate colour with its
file stem as a caption. Needs Pillow (PYTHONPATH=Saved/pylib).
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("dir", type=Path)
    p.add_argument("out", type=Path)
    p.add_argument("--size", type=int, default=64)
    p.add_argument("--cols", type=int, default=8)
    a = p.parse_args()
    files = sorted(a.dir.glob("*.png"))
    s = a.size
    cw, ch = max(s + 52, 150), s + 26
    rows = (len(files) + a.cols - 1) // a.cols
    sheet = Image.new("RGB", (cw * a.cols, ch * rows), (18, 20, 26))
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(files):
        x, y = (i % a.cols) * cw, (i // a.cols) * ch
        im = Image.open(f).convert("RGBA")
        sheet.paste(im.resize((s, s), Image.LANCZOS), (x + 4, y + 4), im.resize((s, s), Image.LANCZOS))
        small = im.resize((40, 40), Image.LANCZOS)
        sheet.paste(small, (x + s + 8, y + 4), small)
        d.rectangle((x + 3, y + 3, x + s + 4, y + s + 4), outline=(150, 120, 60))
        d.text((x + 4, y + s + 8), f.stem.replace("T_Item_", "").replace("T_", "")[:24], fill=(220, 205, 170))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(a.out)
    print(f"contact sheet {a.out} icons={len(files)}")


if __name__ == "__main__":
    main()
