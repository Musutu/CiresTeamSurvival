"""ui-themes: slice the ChatGPT UI-kit sheets into nine-slice-ready theme art and import it.

Sources (generated for Eric via ChatGPT, see Content/UI/Themes/LICENSES.md):
  Art/UI/Themes/ChatGPT/<key>_frames.png  2x2 sheet: panel frame, tooltip frame, minimap border, card frame
  Art/UI/Themes/ChatGPT/<key>_parts.png   rows: 4 button/ring pieces, 2 bar frames, banner + divider,
                                          crest ornament + bar fill + panel texture swatch
Pieces are found as connected alpha components (the sheets' grids are not pixel exact), sorted into
rows, matched to the expected order, then rebuilt:
  * nine-slice frames: four corners + a narrow straight edge strip taken between the corner and the
    edge centre (so centre ornaments never stretch), transparent centre;
  * horizontal strips (bar/cast frames, banner, divider): two caps + a narrow middle strip;
  * slots, ring, ornament: cropped square;
  * bar fill: desaturated gloss normalised for tinting; panel fill: the swatch mirrored so it tiles.
Everything of a theme is packed into one atlas (T_<Theme>_Atlas, padded, edge-extruded) plus the
tileable fill (T_<Theme>_Fill). Piece rectangles and slice fractions are written into
Content/Data/UIThemes.json (palette and other hand-authored fields are kept).

Outputs: Content/UI/Themes/<Theme>/src/*.png (individual slices, atlas, fill) and, with the import
step, /Game/UI/Themes/<Theme>/T_<Theme>_Atlas + T_<Theme>_Fill (UI group, mips, never streamed).

Needs Pillow + numpy (--pylib DIR adds a directory holding them).
  python Tools/BuildUIThemes.py [--pylib DIR] [--no-import] [--theme GildedCitadel]
"""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "Art/UI/Themes/ChatGPT"
OUT = ROOT / "Content/UI/Themes"
DATA = ROOT / "Content/Data/UIThemes.json"
STAGING = ROOT / "Saved/UIThemesBuilder"
EDITOR = "F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"

THEMES = [("GildedCitadel", "gilded"), ("Ironbound", "ironbound"), ("ArcaneVeil", "arcane"), ("VerdantBloom", "verdant")]
FRAME_ORDER = ["Panel", "Tooltip", "Minimap", "Card"]
PART_ROWS = [["Slot", "SlotPassive", "SlotUltimate", "Ring"], ["BarFrame", "CastFrame"], ["Banner", "Divider"],
             ["Ornament", "BarFill", "FillSwatch"]]
NINE = {"Panel": .30, "Tooltip": .26, "Minimap": .28, "Card": .34}   # corner as a fraction of the shorter side
STRIPS = {"BarFrame": 64, "CastFrame": 64, "Banner": 112, "Divider": 40}  # rebuilt height in pixels
SQUARES = {"Slot": 192, "SlotPassive": 192, "SlotUltimate": 224, "Ring": 256}
CORNER_LOGICAL = {"Panel": 30, "Tooltip": 20, "Minimap": 30, "Card": 14}
PAD = 6


def load_libs(pylib):
    if pylib:
        sys.path.insert(0, str(pylib))
    global np, Image
    import numpy as np  # noqa: F401
    from PIL import Image  # noqa: F401


# ---------------------------------------------------------------------------------------------
# Background removal and component detection
# ---------------------------------------------------------------------------------------------
def to_rgba(im):
    """Real alpha when the sheet has it; else key out a flat (or checkerboard) background."""
    a = np.asarray(im.convert("RGBA")).astype(np.float32) / 255
    if a[..., 3].min() < .9:
        return a
    rgb = a[..., :3]
    border = np.concatenate([rgb[:8].reshape(-1, 3), rgb[-8:].reshape(-1, 3), rgb[:, :8].reshape(-1, 3), rgb[:, -8:].reshape(-1, 3)])
    # Up to two background colours (checkerboard): cluster the border by brightness.
    lum = border.mean(1)
    cols = [border[lum <= np.median(lum)].mean(0), border[lum > np.median(lum)].mean(0)]
    d = np.min([np.abs(rgb - c).max(-1) for c in cols], axis=0)
    alpha = np.clip((d - .06) / .16, 0, 1)
    out = np.dstack([rgb, alpha])
    return out


def components(alpha, min_frac=.002, s=3, reach=1):
    """Connected components of the alpha mask (on an s-pixel grid, dilated by reach cells to join filigree)."""
    h, w = alpha.shape
    small = alpha[: h // s * s, : w // s * s].reshape(h // s, s, w // s, s).max((1, 3)) > .25
    grow = small.copy()
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            grow |= np.roll(np.roll(small, dy, 0), dx, 1)
    labels = np.zeros(grow.shape, np.int32)
    boxes = []
    for y0 in range(grow.shape[0]):
        for x0 in range(grow.shape[1]):
            if not grow[y0, x0] or labels[y0, x0]:
                continue
            n = len(boxes) + 1
            stack = [(y0, x0)]
            labels[y0, x0] = n
            ys, xs = [y0, y0], [x0, x0]
            count = 0
            while stack:
                y, x = stack.pop()
                count += 1
                ys[0], ys[1], xs[0], xs[1] = min(ys[0], y), max(ys[1], y), min(xs[0], x), max(xs[1], x)
                for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                    if 0 <= ny < grow.shape[0] and 0 <= nx < grow.shape[1] and grow[ny, nx] and not labels[ny, nx]:
                        labels[ny, nx] = n
                        stack.append((ny, nx))
            boxes.append((xs[0] * s, ys[0] * s, (xs[1] + 1) * s, (ys[1] + 1) * s, count))
    area = grow.size
    boxes = [b for b in boxes if b[4] > area * min_frac]
    # Drop boxes fully inside another box (e.g. inner lines of a frame that did not join).
    keep = []
    for b in boxes:
        if not any(o is not b and o[0] <= b[0] and o[1] <= b[1] and o[2] >= b[2] and o[3] >= b[3] for o in boxes):
            keep.append(b)
    return [(x0, y0, x1, y1) for x0, y0, x1, y1, _ in keep]


def rows_of(boxes):
    """Group boxes into rows by vertical centre overlap, each row sorted left to right."""
    boxes = sorted(boxes, key=lambda b: (b[1] + b[3]) / 2)
    rows = []
    for b in boxes:
        cy = (b[1] + b[3]) / 2
        for row in rows:
            r0, r1 = min(o[1] for o in row), max(o[3] for o in row)
            if r0 <= cy <= r1:
                row.append(b)
                break
        else:
            rows.append([b])
    return [sorted(r, key=lambda b: b[0]) for r in rows]


def crop(rgba, box, pad=2):
    x0, y0, x1, y1 = box
    h, w = rgba.shape[:2]
    sub = rgba[max(0, y0 - pad): min(h, y1 + pad), max(0, x0 - pad): min(w, x1 + pad)]
    # Tighten to the actual alpha extent.
    m = sub[..., 3] > .06
    ys, xs = np.where(m)
    return sub[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]


def img(a):
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8), "RGBA")


def arr(im):
    return np.asarray(im).astype(np.float32) / 255


def resize(a, w, h):
    # Premultiplied resize avoids dark fringes.
    pm = a.copy()
    pm[..., :3] *= pm[..., 3:4]
    r = arr(img(pm).resize((max(1, int(w)), max(1, int(h))), Image.LANCZOS))
    r[..., :3] /= np.maximum(r[..., 3:4], 1e-4)
    return np.clip(r, 0, 1)


# ---------------------------------------------------------------------------------------------
# Rebuilders
# ---------------------------------------------------------------------------------------------
def nine(a, corner_frac, target_corner=96, strip=8):
    h, w = a.shape[:2]
    c = int(round(min(w, h) * corner_frac))
    # Straight edge strips sampled between the corner and the edge centre (skips centre crests).
    sx = int(c + (w / 2 - c) * .3)
    sy = int(c + (h / 2 - c) * .3)
    top = np.concatenate([a[:c, :c], a[:c, sx: sx + strip], a[:c, w - c:]], 1)
    mid = np.concatenate([a[sy: sy + strip, :c], np.zeros((strip, strip, 4), np.float32), a[sy: sy + strip, w - c:]], 1)
    bot = np.concatenate([a[h - c:, :c], a[h - c:, sx: sx + strip], a[h - c:, w - c:]], 1)
    out = np.concatenate([top, mid, bot], 0)
    k = target_corner / c
    out = resize(out, out.shape[1] * k, out.shape[0] * k)
    return out, target_corner / out.shape[0]


def strip_piece(a, height, strip=8):
    h, w = a.shape[:2]
    k = height / h
    a = resize(a, w * k, height)
    h, w = a.shape[:2]
    cap = int(min(w * .22, h * 2.2))
    sx = int(cap + (w / 2 - cap) * .45)
    out = np.concatenate([a[:, :cap], a[:, sx: sx + strip], a[:, w - cap:]], 1)
    return out, cap / out.shape[1]


def square(a, size):
    h, w = a.shape[:2]
    s = max(h, w)
    canvas = np.zeros((s, s, 4), np.float32)
    canvas[(s - h) // 2: (s - h) // 2 + h, (s - w) // 2: (s - w) // 2 + w] = a
    return resize(canvas, size, size)


def bar_fill(a):
    h, w = a.shape[:2]
    core = a[int(h * .2): int(h * .8), int(w * .3): int(w * .7)]
    lum = core[..., :3].mean(-1).mean(1)  # vertical profile
    lum = (lum - lum.min()) / max(1e-4, lum.max() - lum.min())
    prof = .62 + .38 * lum
    col = np.repeat(prof[:, None], 32, 1)
    out = np.dstack([col, col, col, np.ones_like(col)])
    return resize(out, 64, 32)


def tile_fill(a, size=512):
    h, w = a.shape[:2]
    core = a[int(h * .08): int(h * .92), int(w * .08): int(w * .92)].copy()
    core[..., 3] = 1
    half = resize(core, size // 2, size // 2)
    top = np.concatenate([half, half[:, ::-1]], 1)
    return np.concatenate([top, top[::-1]], 0)


def bleed(a, iterations=6):
    """Spread colour into transparent pixels so filtering never pulls in black."""
    rgb, al = a[..., :3].copy(), a[..., 3]
    known = al > .02
    for _ in range(iterations):
        acc = np.zeros_like(rgb)
        n = np.zeros(al.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * k[..., None]
            n += k
        fill = (~known) & (n > 0)
        rgb[fill] = acc[fill] / n[fill][:, None]
        known = known | fill
    return np.dstack([rgb, al])


def pack(pieces, size=2048):
    """Shelf packer. pieces: name -> array. Returns atlas array and name -> [x, y, w, h]."""
    atlas = np.zeros((size, size, 4), np.float32)
    rects = {}
    x = y = PAD
    shelf = 0
    for name, a in sorted(pieces.items(), key=lambda kv: -kv[1].shape[0]):
        h, w = a.shape[:2]
        if x + w + PAD > size:
            x, y, shelf = PAD, y + shelf + 2 * PAD, 0
        if y + h + PAD > size:
            raise SystemExit(f"atlas overflow at {name}")
        b = bleed(a)
        # Extrude the border colour (alpha 0) into the padding.
        ext = np.pad(b, ((PAD, PAD), (PAD, PAD), (0, 0)), mode="edge")
        ext[:PAD, :, 3] = ext[-PAD:, :, 3] = 0
        ext[:, :PAD, 3] = ext[:, -PAD:, 3] = 0
        atlas[y - PAD: y + h + PAD, x - PAD: x + w + PAD] = ext
        rects[name] = [x, y, w, h]
        x += w + 2 * PAD
        shelf = max(shelf, h)
    used = y + shelf + PAD
    final = 1 << int(np.ceil(np.log2(max(used, 256))))
    return atlas[:final], rects, (size, final)


# ---------------------------------------------------------------------------------------------
def slice_theme(theme, key):
    frames_path, parts_path = ART / f"{key}_frames.png", ART / f"{key}_parts.png"
    for p in (frames_path, parts_path):
        if not p.exists():
            raise SystemExit(f"missing source sheet {p}")
    out_dir = OUT / theme / "src"
    out_dir.mkdir(parents=True, exist_ok=True)
    pieces, meta = {}, {}

    frames = to_rgba(Image.open(frames_path))
    boxes = [b for row in rows_of(components(frames[..., 3])) for b in row]
    if len(boxes) != 4:
        raise SystemExit(f"{frames_path.name}: expected 4 frame pieces, found {len(boxes)} {boxes}")
    for name, box in zip(FRAME_ORDER, boxes):
        a, slice_frac = nine(crop(frames, box), NINE[name])
        pieces[name] = a
        meta[name] = {"slice": round(slice_frac, 4), "corner": CORNER_LOGICAL[name]}

    parts = to_rgba(Image.open(parts_path))
    rows = rows_of(components(parts[..., 3], .0006))
    if [len(r) for r in rows] != [len(r) for r in PART_ROWS]:
        raise SystemExit(f"{parts_path.name}: expected rows {[len(r) for r in PART_ROWS]}, found {[len(r) for r in rows]}")
    fill = None
    for names, row in zip(PART_ROWS, rows):
        for name, box in zip(names, row):
            a = crop(parts, box)
            if name in SQUARES:
                pieces[name] = square(a, SQUARES[name])
                meta[name] = {"slice": 0, "corner": 0}
            elif name in STRIPS:
                s, frac = strip_piece(a, STRIPS[name])
                pieces[name] = s
                meta[name] = {"slice": round(frac, 4), "corner": 0}
            elif name == "Ornament":
                h, w = a.shape[:2]
                k = 96 / h
                pieces[name] = resize(a, w * k, 96)
                meta[name] = {"slice": 0, "corner": 0}
            elif name == "BarFill":
                pieces[name] = bar_fill(a)
                meta[name] = {"slice": 0, "corner": 0}
            elif name == "FillSwatch":
                fill = tile_fill(a)
    for name, a in pieces.items():
        img(a).save(out_dir / f"T_{theme}_{name}.png")
    atlas, rects, size = pack(pieces)
    img(atlas).save(out_dir / f"T_{theme}_Atlas.png")
    img(fill).convert("RGB").save(out_dir / f"T_{theme}_Fill.png")
    for name in rects:
        meta[name]["rect"] = rects[name]
    print(f"CIRE_UITHEME_SLICED {theme} atlas={size} pieces={len(rects)}")
    return meta, size


def update_json(results):
    data = json.loads(DATA.read_text(encoding="utf-8"))
    for theme in data["themes"]:
        if theme["id"] not in results:
            continue
        meta, size = results[theme["id"]]
        theme["atlas"] = f"/Game/UI/Themes/{theme['id']}/T_{theme['id']}_Atlas"
        theme["fill"] = f"/Game/UI/Themes/{theme['id']}/T_{theme['id']}_Fill"
        theme["atlasSize"] = list(size)
        pieces = theme.setdefault("pieces", {})
        for name, m in meta.items():
            old = pieces.get(name, {})
            old["rect"] = m["rect"]
            old["slice"] = m["slice"]
            old["corner"] = m["corner"] if name in CORNER_LOGICAL else old.get("corner", m["corner"])
            pieces[name] = old
    text = json.dumps(data, indent=2)
    # Keep number lists (colours, rects) on one line.
    text = re.sub(r"\[\s*([-0-9.,\s]+?)\s*\]", lambda m: "[" + ", ".join(v.strip() for v in m.group(1).split(",")) + "]", text)
    DATA.write_text(text + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------------------------
# Import (runs inside the editor)
# ---------------------------------------------------------------------------------------------
def build(unreal):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    tasks, targets = [], []
    for theme, _ in THEMES:
        for kind in ("Atlas", "Fill"):
            png = OUT / theme / "src" / f"T_{theme}_{kind}.png"
            if not png.exists():
                continue
            t = unreal.AssetImportTask()
            t.filename = str(png)
            t.destination_path = f"/Game/UI/Themes/{theme}"
            t.destination_name = png.stem
            t.automated = True
            t.replace_existing = True
            t.save = True
            tasks.append(t)
            targets.append((f"/Game/UI/Themes/{theme}/{png.stem}", kind))
    tools.import_asset_tasks(tasks)
    for path, kind in targets:
        texture = library.load_asset(path)
        if not texture:
            raise RuntimeError("Theme texture import failed: " + path)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("never_stream", True)
        texture.set_editor_property("srgb", True)
        wrap = unreal.TextureAddress.TA_WRAP if kind == "Fill" else unreal.TextureAddress.TA_CLAMP
        texture.set_editor_property("address_x", wrap)
        texture.set_editor_property("address_y", wrap)
        library.save_loaded_asset(texture, only_if_is_dirty=False)
    unreal.log(f"CIRE_UITHEMES_IMPORT_PASS textures={len(targets)}")


def import_assets():
    (STAGING / "Config").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "Tools/ContentBuilder/ContentBuilder.uproject", STAGING / "UIThemesBuilder.uproject")
    shutil.copy2(ROOT / "Tools/ContentBuilder/Config/DefaultEngine.ini", STAGING / "Config/DefaultEngine.ini")
    shutil.rmtree(STAGING / "Content/UI/Themes", ignore_errors=True)
    logs = STAGING / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / "UIThemes.log"
    command = [EDITOR, str(STAGING / "UIThemesBuilder.uproject"), "-unattended", "-RenderOffscreen", "-nosplash", "-nosound",
               "-nop4", "-NoLiveCoding", f"-ExecutePythonScript={Path(__file__).resolve()}", f"-abslog={log}"]
    with (logs / "UIThemes-console.log").open("w", encoding="utf-8") as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=1800,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if "CIRE_UITHEMES_IMPORT_PASS" not in log.read_text(encoding="utf-8", errors="replace"):
        raise SystemExit(f"UI theme import failed: {log}")
    for theme, _ in THEMES:
        source, destination = STAGING / "Content/UI/Themes" / theme, OUT / theme
        destination.mkdir(parents=True, exist_ok=True)
        for asset in source.glob("*.uasset"):
            target = destination / asset.name
            if target.exists():
                os.chmod(target, 0o666)
            shutil.copy2(asset, target)
            print("CIRE_UITHEMES_COPIED", target)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pylib", type=Path)
    parser.add_argument("--theme", action="append", help="Only these theme ids (default: all with sheets)")
    parser.add_argument("--no-import", action="store_true")
    parser.add_argument("--import-only", action="store_true")
    args = parser.parse_args()
    if not args.import_only:
        load_libs(args.pylib)
        results = {}
        for theme, key in THEMES:
            if args.theme and theme not in args.theme:
                continue
            if not (ART / f"{key}_frames.png").exists():
                print(f"CIRE_UITHEME_SKIPPED {theme} (no sheets yet)")
                continue
            results[theme] = slice_theme(theme, key)
        update_json(results)
    if not args.no_import:
        import_assets()


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        main()
    else:
        try:
            build(unreal)
        except Exception as error:  # report and still quit so the process never hangs
            unreal.log_error("CIRE_UITHEMES_IMPORT_FAIL " + str(error))
        unreal.SystemLibrary.quit_editor()
