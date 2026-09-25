"""hud-art: build each theme atlas from individually generated ChatGPT frame pieces.

Every HUD frame piece was generated one at a time for Eric with ChatGPT image generation (his account,
his art; Content/UI/Themes/LICENSES.md) on a flat key colour, front-on and orthographic. The raw
captures live in Art/UI/Themes/ChatGPT/Pieces/<key>/<piece>.png. This tool:

  * keys the flat background out to alpha (colour unmixing, so gold edges keep no magenta/green/blue
    fringe) and drops stray specks;
  * rebuilds nine-slice frames: four corners plus straight edge strips whose cross-section is the
    median of the whole straight run (so the edges tile cleanly), with the corners feathered into
    that profile at the seam so nothing steps or doubles;
  * rebuilds horizontal 3-slice strips (cast frame, bar frame, title plate, divider) the same way;
  * crops rings and slots square (the portrait ring's opening is placed at 72% of the piece, which is
    what CireUIStyle::PortraitRing assumes) and splits the state sheets (slot, button) into pieces;
  * matches colour and value across a theme's pieces (the metal highlights of every piece are scaled
    toward the main panel frame's);
  * packs the pieces into T_<Theme>_Atlas (Tools/BuildUIThemes.pack), writes rects, slice fractions and
    drawn corner sizes into Content/Data/UIThemes.json and imports the textures with the same editor
    import as Tools/BuildUIThemes.py.

The panel fill, bar fill and crest ornament keep the pieces BuildUIThemes.py sliced from the original
theme sheets (Content/UI/Themes/<Theme>/src) unless a piece file replaces them.

  python Tools/BuildHUDArt.py --pylib <dir with Pillow+numpy> [--theme GildedCitadel] [--no-import]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
PIECES = ROOT / "Art/UI/Themes/ChatGPT/Pieces"
OUT = ROOT / "Content/UI/Themes"
DATA = ROOT / "Content/Data/UIThemes.json"

# Theme id -> (piece folder, background key colour used in the prompts).
THEMES = {
    "GildedCitadel": ("gilded", "magenta"),
    "Ironbound": ("ironbound", "magenta"),
    "ArcaneVeil": ("arcane", "green"),
    "VerdantBloom": ("verdant", "blue"),
}

# Nine-slice frames: corner cell as a fraction of the cropped piece's shorter side, drawn band
# thickness (logical units) the JSON corner is solved for, and the corner cell size in the atlas.
NINE = {
    "Panel": {"corner": .27, "band": 8.5, "px": 144},
    "Tooltip": {"corner": .15, "band": 7.0, "px": 112},
    "Card": {"corner": .30, "band": 9.0, "px": 96},
    "Minimap": {"corner": .25, "band": 14.0, "px": 144},
}
BUTTON_CORNER = 22.0
# Per-theme multiplier on the drawn band thickness (dark iron needs more width to read).
THEME_BAND = {"Ironbound": 1.2}
# Horizontal strips: rebuilt height (px) and cap width as a fraction of the cropped width.
STRIPS = {
    "CastFrame": {"height": 72, "cap": .135},
    "BarFrame": {"height": 64, "cap": .08},
    "Banner": {"height": 128, "cap": .19},
    "Divider": {"height": 48, "cap": .10},
}
SQUARES = {"Slot": 192, "SlotPassive": 192, "SlotUltimate": 224, "Ring": 256, "BuffBorder": 128}
# State sheets: one image, pieces left to right.
SHEETS = {
    "SlotStates": ["Slot", "SlotHover", "SlotPressed", "SlotCooldown", "SlotDisabled"],
    "ButtonStates": ["Button", "ButtonHover", "ButtonPressed", "ButtonDisabled"],
}
SHEET_SQUARE = {"Slot", "SlotHover", "SlotPressed", "SlotCooldown", "SlotDisabled"}
KEEP_FROM_SRC = ["Ornament", "BarFill"]
# Colour grade: the mean of each piece's metal highlights is pulled to this linear-ish sRGB target, so
# every piece of a theme shares one metal (hue from the approved concept, value lifted for depth).
GRADE = {
    "GildedCitadel": (.86, .66, .26),
}
# Faint glow colour around the metal, per theme (None = no halo).
HALO = {
    "GildedCitadel": (1.0, .72, .28),
    "Ironbound": (1.0, .42, .10),
    "ArcaneVeil": (.62, .42, 1.0),
    "VerdantBloom": (1.0, .78, .62),
}
# Pieces that get the halo (frames drawn over the dark world / panels).
HALO_PIECES = {"Panel", "Tooltip", "Minimap", "Ring", "CastFrame", "Banner", "SlotUltimate"}


def load_libs(pylib):
    if pylib:
        sys.path.insert(0, str(pylib))
    global np, Image, BT
    import numpy as np  # noqa: F401
    from PIL import Image  # noqa: F401
    import BuildUIThemes as BT  # noqa: F401
    BT.load_libs(pylib)


# ---------------------------------------------------------------------------------------------
# Keying
# ---------------------------------------------------------------------------------------------
KEYS = {"magenta": (1., 0., 1.), "green": (0., 1., 0.), "blue": (0., 0., 1.)}


def keyness(rgb, key):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    if key == "magenta":
        return np.minimum(r, b) - g
    if key == "green":
        return g - np.maximum(r, b)
    return b - np.maximum(r, g)


def key_out(path, key):
    """RGBA float array with the flat key background removed (unmixed edges, specks dropped)."""
    rgb = np.asarray(Image.open(path).convert("RGB")).astype(np.float32) / 255
    k = keyness(rgb, key)
    border = np.concatenate([k[:6].ravel(), k[-6:].ravel(), k[:, :6].ravel(), k[:, -6:].ravel()])
    kb = float(np.median(border))
    kc = np.array(KEYS[key], np.float32)
    # Actual background colour (ChatGPT's "pure" key is rarely exact).
    bgmask = k > kb * .9
    if bgmask.sum() > 100:
        kc = rgb[bgmask].reshape(-1, 3).mean(0)
    hi, lo = kb * .82, .10
    alpha = np.clip((hi - k) / max(1e-3, hi - lo), 0, 1)
    # Unmix: c = a*f + (1-a)*K.
    a3 = np.maximum(alpha[..., None], 1e-3)
    fg = np.clip((rgb - (1 - alpha[..., None]) * kc) / a3, 0, 1)
    # Residual spill on soft edges: pull the key hue back toward neutral.
    spill = np.clip(keyness(fg, key), 0, None)[..., None]
    if key == "magenta":
        fg = fg - spill * np.array([1, 0, 1], np.float32) * .9
    elif key == "green":
        fg = fg - spill * np.array([0, 1, 0], np.float32) * .9
    else:
        fg = fg - spill * np.array([0, 0, 1], np.float32) * .9
    fg = np.clip(fg, 0, 1)
    a = np.dstack([fg, alpha])
    return despeckle(a)


def despeckle(a, keep_frac=.004):
    """Zero every alpha component smaller than keep_frac of the largest one."""
    m = a[..., 3] > .2
    labels, sizes = label(m)
    if not sizes:
        return a
    big = max(sizes)
    keep = np.zeros(len(sizes) + 1, bool)
    for i, s in enumerate(sizes, 1):
        keep[i] = s >= big * keep_frac
    # Soft edge pixels (alpha <= .2) next to a kept component stay; far ones go.
    solid = keep[labels]
    grow = solid.copy()
    for dy in (-2, -1, 0, 1, 2):
        for dx in (-2, -1, 0, 1, 2):
            grow |= np.roll(np.roll(solid, dy, 0), dx, 1)
    out = a.copy()
    out[..., 3] = np.where(grow, a[..., 3], 0)
    return out


def label(mask):
    """4-connected labelling (iterative flood fill on a numpy mask)."""
    h, w = mask.shape
    labels = np.zeros((h, w), np.int32)
    sizes = []
    ys, xs = np.nonzero(mask)
    flat = mask.ravel()
    lab = labels.ravel()
    for start in (ys * w + xs):
        if lab[start]:
            continue
        n = len(sizes) + 1
        stack = [start]
        lab[start] = n
        count = 0
        while stack:
            p = stack.pop()
            count += 1
            y, x = divmod(p, w)
            for q, ok in ((p - w, y > 0), (p + w, y < h - 1), (p - 1, x > 0), (p + 1, x < w - 1)):
                if ok and flat[q] and not lab[q]:
                    lab[q] = n
                    stack.append(q)
        sizes.append(count)
    return labels, sizes


def tight(a, thresh=.08):
    m = a[..., 3] > thresh
    ys, xs = np.where(m)
    return a[ys.min(): ys.max() + 1, xs.min(): xs.max() + 1]


# ---------------------------------------------------------------------------------------------
# Rebuilders
# ---------------------------------------------------------------------------------------------
def pm(a):
    o = a.copy()
    o[..., :3] *= o[..., 3:4]
    return o


def unpm(o):
    a = o.copy()
    a[..., :3] = np.where(a[..., 3:4] > 1e-4, a[..., :3] / np.maximum(a[..., 3:4], 1e-4), 0)
    return np.clip(a, 0, 1)


def profile(a, axis, lo, hi):
    """Mean cross-section of a straight run (premultiplied), axis 1 = run along x. The mean (not the
    median) keeps sparse detail such as Ironbound's ember cracks as a soft continuous glow."""
    p = pm(a)
    run = p[:, lo:hi] if axis == 1 else p[lo:hi, :]
    return unpm(run.mean(axis=axis, keepdims=True))


def band_thickness(a, side="top"):
    """Opaque band depth (px) of a frame edge at its middle."""
    h, w = a.shape[:2]
    if side == "top":
        col = a[: h // 2, w // 2 - 8: w // 2 + 8, 3].mean(1)
    else:
        col = a[h // 2 - 8: h // 2 + 8, : w // 2, 3].mean(0)
    idx = np.where(col > .5)[0]
    return (idx.max() - idx.min() + 1) if len(idx) else 0, (idx.min() if len(idx) else 0)


def nine(a, corner_frac, target_px, strip=16, feather=.22):
    h, w = a.shape[:2]
    c = int(round(min(w, h) * corner_frac))
    top = profile(a[:c], 1, int(w * .3), int(w * .7))            # (c,1,4)
    bot = profile(a[h - c:], 1, int(w * .3), int(w * .7))
    left = profile(a[:, :c], 0, int(h * .3), int(h * .7))         # (1,c,4)
    right = profile(a[:, w - c:], 0, int(h * .3), int(h * .7))
    corners = {k: pm(v) for k, v in {"tl": a[:c, :c], "tr": a[:c, w - c:], "bl": a[h - c:, :c], "br": a[h - c:, w - c:]}.items()}
    # Feather each corner's inner-facing seams into the straight profiles.
    f = max(2, int(c * feather))
    ramp = np.linspace(0, 1, f, dtype=np.float32)
    def blend_x(cell, prof, towards_right):
        cell = cell.copy()
        pp = pm(prof)[:, 0]
        for i in range(f):
            x = c - f + i if towards_right else f - 1 - i
            t = ramp[i]
            cell[:, x] = cell[:, x] * (1 - t) + pp * t
        return cell
    def blend_y(cell, prof, towards_bottom):
        cell = cell.copy()
        pp = pm(prof)[0]
        for i in range(f):
            y = c - f + i if towards_bottom else f - 1 - i
            t = ramp[i]
            cell[y] = cell[y] * (1 - t) + pp * t
        return cell
    # Only the rail region of each cell is blended: the profiles are zero outside the band, so blending
    # the corner ornament (which sits across the whole cell) is limited to the seam columns.
    tl = blend_y(blend_x(corners["tl"], top, True), left, True)
    tr = blend_y(blend_x(corners["tr"], top, False), right, True)
    bl = blend_y(blend_x(corners["bl"], bot, True), left, False)
    br = blend_y(blend_x(corners["br"], bot, False), right, False)
    tl, tr, bl, br = (unpm(x) for x in (tl, tr, bl, br))
    top_s = np.repeat(top, strip, 1)
    bot_s = np.repeat(bot, strip, 1)
    left_s = np.repeat(left, strip, 0)
    right_s = np.repeat(right, strip, 0)
    mid = np.concatenate([left_s, np.zeros((strip, strip, 4), np.float32), right_s], 1)
    out = np.concatenate([np.concatenate([tl, top_s, tr], 1), mid, np.concatenate([bl, bot_s, br], 1)], 0)
    k = target_px / c
    out = BT.resize(out, round(out.shape[1] * k), round(out.shape[0] * k))
    corner_px = c * k
    return out, corner_px / min(out.shape[:2]), c


def strip_piece(a, height, cap_frac, strip=16):
    h, w = a.shape[:2]
    k = height / h
    a = BT.resize(a, round(w * k), height)
    h, w = a.shape[:2]
    cap = int(round(w * cap_frac))
    mid = profile(a, 1, int(w * .42), int(w * .58)) if cap_frac < .4 else profile(a, 1, cap, int(cap + (w / 2 - cap) * .6) or cap + 1)
    out = np.concatenate([a[:, :cap], np.repeat(mid, strip, 1), a[:, w - cap:]], 1)
    return out, cap / out.shape[1]


def square(a, size, opening=None):
    h, w = a.shape[:2]
    s = max(h, w)
    if opening:
        # Ring: measure the hole radius along the centre row and scale the canvas so it is `opening`
        # of the half size (never clipping the art).
        # Median hole radius over many angles (gem mounts poking into the hole do not count).
        cy, cx = h / 2, w / 2
        radii = []
        for ang in np.radians(np.arange(0, 360, 3)):
            for r in range(4, int(min(h, w) / 2)):
                y, x = int(cy + r * np.sin(ang)), int(cx + r * np.cos(ang))
                if a[y, x, 3] > .5:
                    radii.append(r)
                    break
        ri = float(np.median(radii)) if radii else s * .36
        s = max(s, int(np.ceil(2 * ri / opening)))
    canvas = np.zeros((s, s, 4), np.float32)
    canvas[(s - h) // 2: (s - h) // 2 + h, (s - w) // 2: (s - w) // 2 + w] = a
    return BT.resize(canvas, size, size)


def split_sheet(a, count):
    """Split a horizontal state sheet at the emptiest columns into `count` pieces."""
    cols = a[..., 3].sum(0)
    w = a.shape[1]
    cuts = []
    for i in range(1, count):
        centre = int(w * i / count)
        lo, hi = max(0, centre - w // (count * 3)), min(w, centre + w // (count * 3))
        cuts.append(lo + int(np.argmin(cols[lo:hi])))
    edges = [0] + cuts + [w]
    return [tight(a[:, edges[i]: edges[i + 1]]) for i in range(count)]


# ---------------------------------------------------------------------------------------------
# Colour / value matching
# ---------------------------------------------------------------------------------------------
def metal_stats(a):
    rgb, al = a[..., :3], a[..., 3]
    lum = rgb.mean(-1)
    m = (al > .9) & (lum > np.percentile(lum[al > .9], 70) if (al > .9).any() else al > .9)
    return rgb[m].mean(0) if m.any() else None


def grade_gain(a, ref, strength=1.0, clamp=(.7, 1.35)):
    s = metal_stats(a)
    if s is None or ref is None:
        return np.ones(3, np.float32)
    gain = np.clip(ref / np.maximum(s, 1e-3), *clamp)
    return (1 + (gain - 1) * strength).astype(np.float32)


def apply_gain(a, gain):
    out = a.copy()
    out[..., :3] = np.clip(out[..., :3] * gain, 0, 1)
    return out


def dimension(a, shadow=.55, contrast=.35):
    """Reads as dimensional metal at HUD sizes: recesses (enamel channels, bevel undersides) pushed
    toward near-black and desaturated a little, and an S-curve on the metal so the lit edge and the
    lower bevel separate even when a piece is drawn 15-20 px thick."""
    out = a.copy()
    rgb = out[..., :3]
    lum = rgb.mean(-1, keepdims=True)
    solid = a[..., 3] > .9
    # Recess threshold relative to the piece's own values (dark iron is metal, not a recess).
    t = float(np.percentile(lum[solid], 30)) if solid.any() else .32
    t = min(max(t, .05), .32)
    dark = np.clip((t - lum) / t, 0, 1)                 # 1 in the recesses, 0 on the metal
    grey = np.repeat(lum, 3, -1)
    rgb = rgb * (1 - dark * (1 - shadow))
    rgb = rgb * (1 - dark * .35) + grey * shadow * dark * .35
    # S-curve around the piece's mid value (keeps hue: scale RGB by the luminance ratio).
    l2 = rgb.mean(-1, keepdims=True)
    mid = float(np.percentile(l2[solid], 55)) if solid.any() else .5
    x = np.clip((l2 - mid) / max(mid, 1 - mid), -1, 1)
    curved = l2 + contrast * max(mid, 1 - mid) * x * (1 - np.abs(x)) 
    rgb = rgb * (np.clip(curved, 0, 1) / np.maximum(l2, 1e-3))
    out[..., :3] = np.clip(rgb, 0, 1)
    return out


def halo(a, colour, strength=.22, radius=.012):
    """Soft warm light around the metal (the concept's gold frames glow faintly on the dark panel)."""
    r = max(2, int(min(a.shape[:2]) * radius))
    a = np.pad(a, ((2 * r, 2 * r), (2 * r, 2 * r), (0, 0)))
    h, w = a.shape[:2]
    small = Image.fromarray((a[..., 3] * 255).astype(np.uint8)).resize((max(1, w // 4), max(1, h // 4)), Image.BILINEAR)
    from PIL import ImageFilter
    blur = np.asarray(small.filter(ImageFilter.GaussianBlur(r / 4)).resize((w, h), Image.BILINEAR)).astype(np.float32) / 255
    g = np.clip(blur * 1.6, 0, 1) * strength
    base = np.dstack([np.broadcast_to(np.array(colour, np.float32), (h, w, 3)), g])
    # Composite the piece over its glow (premultiplied "over").
    fa = a[..., 3:4]
    out_a = fa + base[..., 3:4] * (1 - fa)
    out_rgb = (a[..., :3] * fa + base[..., :3] * base[..., 3:4] * (1 - fa)) / np.maximum(out_a, 1e-4)
    return np.dstack([np.clip(out_rgb, 0, 1), out_a])


# ---------------------------------------------------------------------------------------------
def build_theme(theme):
    key, keycol = THEMES[theme]
    src_dir = PIECES / key
    out_dir = OUT / theme / "src"
    out_dir.mkdir(parents=True, exist_ok=True)
    files = {p.stem: p for p in src_dir.glob("*.png")}
    if "Panel" not in files:
        raise SystemExit(f"{theme}: no Panel piece in {src_dir}")
    raw = {name: tight(key_out(p, keycol)) for name, p in files.items()}
    ref = np.array(GRADE[theme], np.float32) if theme in GRADE else metal_stats(raw["Panel"])
    pieces, meta = {}, {}
    for name, a in sorted(raw.items(), key=lambda kv: kv[0] in SHEETS):
        if name in SHEETS:
            # State sheets: one gain from the first (normal) state so the states keep their relative value.
            a = apply_gain(a, grade_gain(split_sheet(a, len(SHEETS[name]))[0], ref))
        else:
            a = apply_gain(a, grade_gain(a, ref))
        a = dimension(a)
        if HALO.get(theme) and name in HALO_PIECES:
            a = halo(a, HALO[theme])
        if name in NINE:
            spec = NINE[name]
            out, frac, c = nine(a, spec["corner"], spec["px"])
            thick, _ = band_thickness(a)
            thick = max(thick, 1)
            # Drawn corner (logical) so the band is spec["band"] units thick: band/c of the corner cell.
            corner = round(spec["band"] * THEME_BAND.get(theme, 1.0) * c / thick, 1)
            pieces[name] = out
            meta[name] = {"slice": round(frac, 4), "corner": min(corner, 128)}
            print(f"  {name}: crop {a.shape[1]}x{a.shape[0]} corner {c}px band {thick}px -> corner {corner} units")
        elif name in STRIPS:
            spec = STRIPS[name]
            out, frac = strip_piece(a, spec["height"], spec["cap"])
            pieces[name] = out
            meta[name] = {"slice": round(frac, 4), "corner": 0}
        elif name in SQUARES:
            pieces[name] = square(a, SQUARES[name], opening=.72 if name == "Ring" else None)
            meta[name] = {"slice": 0, "corner": 0}
        elif name in SHEETS:
            names = SHEETS[name]
            parts = split_sheet(a, len(names))
            for pn, pa in zip(names, parts):
                if pn in SHEET_SQUARE:
                    pieces[pn] = square(pa, 192)
                    meta[pn] = {"slice": 0, "corner": 0}
                else:
                    out, frac, c = nine(pa, NINE["Card"]["corner"], NINE["Card"]["px"])
                    pieces[pn] = out
                    # Buttons have an opaque face, so the rim cannot be measured from alpha: the corner cell
                    # (gem cap + rim) is drawn at a fixed size (halved by CireUIStyle on short buttons).
                    meta[pn] = {"slice": round(frac, 4), "corner": BUTTON_CORNER}
                    if pn == "Button":
                        # The normal button is also the theme's Card (buttons, rows, cards).
                        pieces["Card"], meta["Card"] = out, dict(meta[pn])
        elif name == "Ornament":
            h, w = a.shape[:2]
            pieces[name] = BT.resize(a, w * 128 / h, 128)
            meta[name] = {"slice": 0, "corner": 0}
        else:
            print(f"  (unused piece file {name})")
    # Pieces this pass does not replace keep the previous slices.
    for name in KEEP_FROM_SRC + list(NINE) + list(STRIPS) + ["Slot", "SlotPassive", "SlotUltimate", "Ring"]:
        if name not in pieces:
            png = out_dir / f"T_{theme}_{name}.png"
            if png.exists():
                pieces[name] = BT.arr(Image.open(png).convert("RGBA"))
                meta[name] = None  # keep JSON slice/corner
                print(f"  {name}: kept previous slice")
    for name, a in pieces.items():
        BT.img(a).save(out_dir / f"T_{theme}_{name}.png")
    atlas, rects, size = BT.pack(pieces)
    BT.img(atlas).save(out_dir / f"T_{theme}_Atlas.png")
    print(f"CIRE_HUDART_BUILT {theme} atlas={size} pieces={len(rects)}")
    return {n: (rects[n], meta[n]) for n in rects}, size


def update_json(results):
    data = json.loads(DATA.read_text(encoding="utf-8"))
    for theme in data["themes"]:
        if theme["id"] not in results:
            continue
        pieces_meta, size = results[theme["id"]]
        theme["atlasSize"] = list(size)
        pieces = theme.setdefault("pieces", {})
        for name, (rect, m) in pieces_meta.items():
            old = pieces.get(name, {})
            old["rect"] = rect
            if m is not None:
                old["slice"] = m["slice"]
                old["corner"] = m["corner"]
            pieces[name] = old
    text = json.dumps(data, indent=2)
    text = re.sub(r"\[\s*([-0-9.,\s]+?)\s*\]", lambda m: "[" + ", ".join(v.strip() for v in m.group(1).split(",")) + "]", text)
    DATA.write_text(text + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pylib", type=Path)
    parser.add_argument("--theme", action="append")
    parser.add_argument("--no-import", action="store_true")
    parser.add_argument("--import-only", action="store_true")
    args = parser.parse_args()
    load_libs(args.pylib)
    if args.import_only:
        os.environ["UE_SKIP_UBT_SDK_SETUP"] = "1"
        BT.import_assets()
        return
    results = {}
    for theme in THEMES:
        if args.theme and theme not in args.theme:
            continue
        if not (PIECES / THEMES[theme][0] / "Panel.png").exists():
            print(f"CIRE_HUDART_SKIPPED {theme} (no pieces yet)")
            continue
        print(f"{theme}:")
        results[theme] = build_theme(theme)
    update_json(results)
    if not args.no_import:
        # AutoSDK is off on this machine: without this the editor boot runs Build.bat -Mode=ValidatePlatforms
        # and blocks on the machine-wide Build.bat lock while any other worktree compiles.
        os.environ["UE_SKIP_UBT_SDK_SETUP"] = "1"
        BT.import_assets()


if __name__ == "__main__":
    main()
