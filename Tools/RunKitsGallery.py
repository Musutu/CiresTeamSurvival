"""Render the scaling-kits review gallery (Source/CiresTeamSurvival/CireKitsGallery.cpp).

Offscreen 1920x1080 captures land in Saved/KitsGallery/<stamp>/: the Mechanical Tank taunting an enemy that
was hitting the Ranger, a shield BLOCK (floating text + combat log), the level-15 Artillery bomb and the Skill
Shop tooltip with its "Lv 15: +..." line. Usage: python Tools/RunKitsGallery.py [--only mech,shield,...] [--timeout S]
Only the editor process started here is stopped. Captures still need visual review.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
# Skip the editor's ValidatePlatforms Build.bat call (it blocks on the machine-wide Build.bat lock).
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", default="")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/KitsGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireKitsGallery", "-CireTripoChampions",
               "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if args.only:
        command.append(f"-CireKitsGalleryOnly={args.only}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for line in text.splitlines():
        if re.search(r"CIRE_KITS_GALLERY_(PASS|FAIL|CAPTURE|CHECK_FAIL|STAGE|TAUNT|READY)|Fatal error|Ensure condition failed", line):
            print(line.strip())
    passed = code == 0 and "CIRE_KITS_GALLERY_PASS" in text
    print("KITS GALLERY", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
