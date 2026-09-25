"""Render the new-champions review gallery (Source/CiresTeamSurvival/CireNewChampionsGallery.cpp).

Offscreen 1920x1080 captures land in Saved/NewChampionsGallery/<stamp>/: the five new champions on the lane,
close-ups (Gunblade, Witch Slayer, the mounted Huntress, the Aetheri), each champion in combat casting signature
skills, the Aetheri Constructs being placed and fighting (turret, traps, pylons, skitter bombs), and an Aetheri
monster wave deploying its own constructs. Usage: python Tools/RunNewChampionsGallery.py [--only lineup,constructs,...]
Only the editor process started here is stopped. Captures still need visual review.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", default="")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/NewChampionsGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireNewChampionsGallery",
               "-CireTripoChampions", "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if args.only:
        command.append(f"-CireNewChampionsGalleryOnly={args.only}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation)
    try:
        code = child.wait(timeout=900)
    except subprocess.TimeoutExpired:
        child.kill()
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for line in text.splitlines():
        if re.search(r"CIRE_NEW_CHAMPIONS_GALLERY_(PASS|FAIL|CAPTURE|CHECK_FAIL|CAST|DEPLOY|READY)|Fatal error|Ensure condition failed", line):
            print(line.strip())
    passed = code == 0 and "CIRE_NEW_CHAMPIONS_GALLERY_PASS" in text
    print("NEW CHAMPIONS GALLERY", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
