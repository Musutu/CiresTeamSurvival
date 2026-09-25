"""progression-shop: cut the three Skill Shop scrolls and category crests out of Eric's reference
image (Saved/Reference/skills-target.png, Eric's own image generated for him) and mask the dark
background to soft alpha (glow and wisps kept). Needs Pillow + numpy.
Output: Content/UI/Shop/Scrolls/src/T_Scroll_{Golden,Plain,Prismatic}.png, T_Crest_{Active,Passive,Ultimate}.png
"""
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Saved/Reference/skills-target.png"
OUT = ROOT / "Content/UI/Shop/Scrolls/src"
BG = np.array([0.062, 0.060, 0.064])


def cut(im, box, name, low=0.04, span=0.22, edge=6):
    c = np.asarray(im.crop(box)).astype(float) / 255
    diff = np.clip((c - BG).max(axis=2), 0, 1)
    alpha = np.clip((diff - low) / span, 0, 1) ** 0.9
    h, w = alpha.shape
    ramp = np.minimum.reduce([np.arange(w)[None, :].repeat(h, 0), (w - 1 - np.arange(w))[None, :].repeat(h, 0),
                              np.arange(h)[:, None].repeat(w, 1), (h - 1 - np.arange(h))[:, None].repeat(w, 1)])
    alpha *= np.clip(ramp / edge, 0, 1)  # fade the crop border so stray frame lines vanish
    a3 = np.maximum(alpha[..., None], 1e-3)
    col = np.clip((c - BG * (1 - a3)) / a3, 0, 1)
    Image.fromarray((np.dstack([col, alpha]) * 255).astype(np.uint8), "RGBA").save(OUT / f"{name}.png")
    print(name, box, (w, h))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    im = Image.open(SRC).convert("RGB")
    cut(im, (45, 105, 568, 618), "T_Scroll_Golden", edge=14)
    cut(im, (588, 108, 1092, 618), "T_Scroll_Plain", edge=14)
    cut(im, (1100, 92, 1645, 622), "T_Scroll_Prismatic", edge=14)
    for name, cx in (("T_Crest_Active", 301), ("T_Crest_Passive", 836), ("T_Crest_Ultimate", 1376)):
        cut(im, (cx - 50, 614, cx + 50, 714), name, low=0.05, span=0.3, edge=3)


if __name__ == "__main__":
    main()
