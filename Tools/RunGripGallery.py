"""Render the weapon-grip review gallery (Source/CiresTeamSurvival/CireGripGallery.cpp) and build contact sheets.

Every armed champion is posed alone in six states (idle, run, attack windup, attack contact, cast, dodge roll), each
captured full-body and as a close-up of the weapon hand. Captures land in Saved/GripGallery/<stamp>-after (or -before
with --legacy, which renders the old bind-pose grips). With Pillow available (--pylib <dir> or on sys.path) one
contact sheet per champion is written next to them: <nn>_<profile>_sheet.png, full-body row over the hand row.
The metric lines (CIRE_GRIP_GALLERY_METRIC) are copied to metrics.txt for the Docs/WeaponLoadouts.md table.

Usage: python Tools/RunGripGallery.py [--only knight,paladin_holy] [--legacy] [--pylib DIR]
Only the editor process started here is stopped. Captures still need visual review.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}
STATES = ["idle", "run", "windup", "contact", "cast", "roll"]


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def contact_sheets(directory: Path, pylib: str | None) -> list[Path]:
    if pylib:
        sys.path.insert(0, pylib)
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("Pillow not available: no contact sheets (pass --pylib)")
        return []
    groups: dict[str, dict[tuple[int, str], Path]] = {}
    for png in directory.glob("*.png"):
        m = re.fullmatch(r"(\d\d_[a-z0-9_]+?)_(\d)_([a-z]+)_(wide|hand)\.png", png.name)
        if m:
            groups.setdefault(m.group(1), {})[(int(m.group(2)), m.group(4))] = png
    sheets = []
    tile_w, tile_h, label = 480, 270, 22
    for key, tiles in sorted(groups.items()):
        sheet = Image.new("RGB", (tile_w * len(STATES), (tile_h + label) * 2 + 34), (18, 18, 22))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 8), key[3:] + ("  (before: bind-pose grips)" if directory.name.endswith("before") else "  (after)"), fill=(245, 220, 170))
        for row, view in enumerate(("wide", "hand")):
            for col, state in enumerate(STATES):
                path = tiles.get((col, view))
                x, y = col * tile_w, 34 + row * (tile_h + label)
                draw.text((x + 6, y + 4), f"{state} / {view}", fill=(220, 220, 220))
                if path:
                    image = Image.open(path).convert("RGB")
                    if view == "hand":  # centre crop: the hand sits in the middle of the close-up
                        w, h = image.size
                        image = image.crop((w // 6, h // 6, w - w // 6, h - h // 6))
                    sheet.paste(image.resize((tile_w, tile_h)), (x, y + label))
        out = directory / f"{key}_sheet.png"
        sheet.save(out)
        sheets.append(out)
    return sheets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", default="")
    parser.add_argument("--legacy", action="store_true", help="render the old bind-pose grips (before captures)")
    parser.add_argument("--no-fab", action="store_true", help="hide the local Fab packs")
    parser.add_argument("--pylib", default=None, help="directory with Pillow for the contact sheets")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--timeout", type=int, default=1500)
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/GripGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireGripGallery",
               "-CireTripoChampions", "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if args.legacy:
        command.append("-CireLegacyGrips")
    if args.no_fab:
        command.append("-CireNoFab")
    if args.only:
        command.append(f"-CireGripGalleryOnly={args.only}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    metrics = []
    for line in text.splitlines():
        if "CIRE_GRIP_GALLERY_METRIC" in line:
            metrics.append(line[line.index("CIRE_GRIP_GALLERY_METRIC"):].strip())
        elif re.search(r"CIRE_GRIP_GALLERY_(PASS|FAIL|CHECK_FAIL|SKIP|CHAMPION)|Fatal error|Ensure condition failed", line):
            print(line.strip())
    match = re.search(r"CIRE_GRIP_GALLERY_(PASS|FAIL) .*directory=(.+)", text)
    if match:
        directory = Path(match.group(2).strip())
        (directory / "metrics.txt").write_text("\n".join(metrics) + "\n", encoding="utf-8")
        for sheet in contact_sheets(directory, args.pylib):
            print("sheet", sheet)
        print("metrics", directory / "metrics.txt")
    passed = code == 0 and "CIRE_GRIP_GALLERY_PASS" in text
    print("GRIP GALLERY", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
