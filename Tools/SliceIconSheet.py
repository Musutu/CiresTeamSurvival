"""Slice a ChatGPT-generated icon sheet (N x M grid, dark gutters) into square icon PNGs.

Usage (UE Python + Pillow, e.g. PYTHONPATH=Saved/pylib):
  python Tools/SliceIconSheet.py SHEET.png --grid 3x3 --names a,b,c,... --out DIR [--size 256] [--inset 0.02]

The gutter positions are found from the row/column brightness profile near each expected
split (the generator does not always space tiles perfectly), then every cell is cropped,
trimmed by --inset of its size (removes gutter bleed), centered to a square and resized.
Names may be '-' to skip a cell. Sources live in Art/Icons/ChatGPT (see LICENSES.md files).
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageStat


def _profile(img, axis):
    g = img.convert("L")
    w, h = g.size
    if axis == 0:  # columns
        return [ImageStat.Stat(g.crop((x, 0, x + 1, h))).mean[0] for x in range(w)]
    return [ImageStat.Stat(g.crop((0, y, w, y + 1))).mean[0] for y in range(h)]


def _splits(profile, count):
    """Return count+1 boundaries: the darkest band near each expected split."""
    n = len(profile)
    cuts = [0]
    for k in range(1, count):
        guess = n * k / count
        lo, hi = int(guess - n * 0.06), int(guess + n * 0.06)
        best = min(range(lo, hi), key=lambda i: sum(profile[max(0, i - 2):i + 3]))
        # widen to the whole dark band
        thr = profile[best] + 12
        a = best
        while a > lo and profile[a - 1] <= thr:
            a -= 1
        b = best
        while b < hi and profile[b + 1] <= thr:
            b += 1
        cuts.append((a, b))
    cuts.append(n)
    bounds = []
    for k in range(count):
        start = cuts[k] if k == 0 else cuts[k][1] + 1
        end = cuts[k + 1] if k + 1 == count else cuts[k + 1][0]
        bounds.append((start, end))
    return bounds


def slice_sheet(sheet, cols, rows, names, out, size=256, inset=0.02, even=False):
    img = Image.open(sheet).convert("RGB")
    w, h = img.size
    if even:
        xs = [(round(w * i / cols), round(w * (i + 1) / cols)) for i in range(cols)]
        ys = [(round(h * i / rows), round(h * (i + 1) / rows)) for i in range(rows)]
    else:
        xs = _splits(_profile(img, 0), cols)
        ys = _splits(_profile(img, 1), rows)
    out.mkdir(parents=True, exist_ok=True)
    written = []
    for r in range(rows):
        for c in range(cols):
            idx = r * cols + c
            if idx >= len(names) or names[idx] in ("-", ""):
                continue
            x0, x1 = xs[c]
            y0, y1 = ys[r]
            side = min(x1 - x0, y1 - y0)
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            half = side / 2 * (1 - inset * 2)
            cell = img.crop((round(cx - half), round(cy - half), round(cx + half), round(cy + half)))
            cell = cell.resize((size, size), Image.LANCZOS)
            path = out / f"{names[idx]}.png"
            cell.save(path)
            written.append(path)
    return written, xs, ys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("sheet", type=Path)
    p.add_argument("--grid", default="3x3")
    p.add_argument("--names", required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--size", type=int, default=256)
    p.add_argument("--inset", type=float, default=0.02)
    p.add_argument("--even", action="store_true", help="Split evenly instead of detecting gutters")
    a = p.parse_args()
    cols, rows = (int(v) for v in a.grid.lower().split("x"))
    written, xs, ys = slice_sheet(a.sheet, cols, rows, a.names.split(","), a.out, a.size, a.inset, a.even)
    print(f"cols={xs} rows={ys} wrote={len(written)}")


if __name__ == "__main__":
    main()
