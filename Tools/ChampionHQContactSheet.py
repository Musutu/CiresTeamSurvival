"""champion-hq: tile gallery captures into a labelled contact sheet (Pillow; pass --pylib if Pillow is not installed).

python Tools/ChampionHQContactSheet.py <gallery dir> [--out sheet.jpg] [--cols 5] [--crop 0.2,0.0,0.8,1.0] [--match _idle]
      [--before <dir> --after <dir>]   before/after mode: pairs files with the same shot name side by side.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dir", nargs="?")
    parser.add_argument("--out")
    parser.add_argument("--cols", type=int, default=5)
    parser.add_argument("--crop", default="0.28,0.05,0.72,1.0")
    parser.add_argument("--match", default="")
    parser.add_argument("--width", type=int, default=380)
    parser.add_argument("--before")
    parser.add_argument("--after")
    parser.add_argument("--pylib")
    args = parser.parse_args()
    if args.pylib:
        sys.path.insert(0, args.pylib)
    from PIL import Image, ImageDraw
    crop = [float(v) for v in args.crop.split(",")]

    def tile(path: Path):
        im = Image.open(path).convert("RGB")
        w, h = im.size
        im = im.crop((int(crop[0] * w), int(crop[1] * h), int(crop[2] * w), int(crop[3] * h)))
        return im.resize((args.width, int(im.size[1] * args.width / im.size[0])))

    if args.before and args.after:
        after = {p.name.split("_", 1)[1]: p for p in sorted(Path(args.after).glob("*.png")) if args.match in p.name}
        before = {p.name.split("_", 1)[1]: p for p in sorted(Path(args.before).glob("*.png"))}
        pairs = [(before.get(k), a, k) for k, a in after.items()]
        cells = []
        for b, a, k in pairs:
            ta = tile(a)
            tb = tile(b) if b else Image.new("RGB", ta.size, (30, 30, 30))
            cell = Image.new("RGB", (ta.size[0] * 2 + 6, ta.size[1] + 22), (12, 12, 12))
            cell.paste(tb, (0, 22)); cell.paste(ta, (ta.size[0] + 6, 22))
            d = ImageDraw.Draw(cell)
            d.text((4, 4), "BEFORE  " + k[:-4], fill=(200, 200, 200)); d.text((ta.size[0] + 10, 4), "AFTER (champion-hq)", fill=(255, 220, 90))
            cells.append(cell)
    else:
        files = [p for p in sorted(Path(args.dir).glob("*.png")) if args.match in p.name and not p.name.endswith((".depth.png", ".figure.png"))]
        cells = []
        for p in files:
            t = tile(p)
            cell = Image.new("RGB", (t.size[0], t.size[1] + 20), (12, 12, 12))
            cell.paste(t, (0, 20))
            ImageDraw.Draw(cell).text((4, 3), p.stem, fill=(255, 220, 90))
            cells.append(cell)
    if not cells:
        print("no captures")
        return 1
    cw = max(c.size[0] for c in cells); ch = max(c.size[1] for c in cells)
    cols = min(args.cols, len(cells)); rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw + (cols - 1) * 4, rows * ch + (rows - 1) * 4), (0, 0, 0))
    for i, c in enumerate(cells):
        sheet.paste(c, ((i % cols) * (cw + 4), (i // cols) * (ch + 4)))
    out = Path(args.out or (Path(args.dir or args.after) / "contact_sheet.jpg"))
    sheet.save(out, quality=90)
    print(out, sheet.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
