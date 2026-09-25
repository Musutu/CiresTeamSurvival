"""progression-shop: side-by-side review sheets for the Skill Shop against Eric's target image.

For each gallery directory given (Tools/RunProgressionChecks.py --only gallery writes them under
Saved/ShopGallery/<stamp>), writes Saved/ShopGallery/compare/<stamp>_<capture>.png with
Saved/Reference/skills-target.png on the left and the capture on the right at the same height,
plus <stamp>_moments.png: a 2x2 sheet of the purchase moments (seal stamp, scroll flight,
unaffordable error, auto-open after a cleared wave). Needs Pillow.

  python Tools/ComposeShopCompare.py Saved/ShopGallery/<stamp> [more dirs...]
"""
from pathlib import Path
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "Saved/Reference/skills-target.png"
OUT = ROOT / "Saved/ShopGallery/compare"
SIDE_BY_SIDE = ("skill_shop_hover", "skill_shop_seal_stamp", "shop_recommended")
MOMENTS = ("skill_shop_seal_stamp", "skill_shop_scroll_flight", "skill_shop_unaffordable_error", "skill_shop_auto_open_after_wave")


def fit_height(image, height):
    return image.resize((round(image.width * height / image.height), height), Image.LANCZOS)


def label(image, text):
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 12 + 7 * len(text), 22), fill=(0, 0, 0))
    draw.text((6, 5), text, fill=(235, 205, 140))
    return image


def main(directories):
    OUT.mkdir(parents=True, exist_ok=True)
    target = Image.open(TARGET).convert("RGB")
    for directory in map(Path, directories):
        captures = sorted(directory.glob("*.png"))
        for capture in captures:
            if not any(key in capture.stem for key in SIDE_BY_SIDE):
                continue
            shot = Image.open(capture).convert("RGB")
            left = label(fit_height(target, shot.height), "TARGET  skills-target.png")
            right = label(shot.copy(), f"{shot.width}x{shot.height}  {capture.stem}")
            sheet = Image.new("RGB", (left.width + right.width + 12, shot.height), (10, 10, 10))
            sheet.paste(left, (0, 0))
            sheet.paste(right, (left.width + 12, 0))
            path = OUT / f"{directory.name}_{capture.stem}.png"
            sheet.save(path)
            print("CIRE_SHOP_COMPARE", path)
        moments = [c for key in MOMENTS for c in captures if key in c.stem]
        if len(moments) == 4:
            tiles = [label(Image.open(c).convert("RGB"), c.stem) for c in moments]
            w, h = tiles[0].width // 2, tiles[0].height // 2
            sheet = Image.new("RGB", (w * 2 + 8, h * 2 + 8), (10, 10, 10))
            for index, tile in enumerate(tiles):
                sheet.paste(tile.resize((w, h), Image.LANCZOS), ((index % 2) * (w + 8), (index // 2) * (h + 8)))
            path = OUT / f"{directory.name}_moments.png"
            sheet.save(path)
            print("CIRE_SHOP_COMPARE", path)


if __name__ == "__main__":
    main(sys.argv[1:])
