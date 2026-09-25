"""Render the animated Tripo monster review gallery (Source/CiresTeamSurvival/CireMonsterGallery.cpp).

Offscreen 1920x1080 captures land in Saved/MonsterGallery/<stamp>/: two lineups, the elite pack with its
Pack Leader, every archetype at windup/contact/follow-through plus its special clip, the B variants at release,
walk/run frames, deaths, and a live wave fighting a champion in the town through the gameplay camera framing.
Usage: python Tools/RunMonsterGallery.py [--only lineup_melee,town,...]
Only the editor process started here is stopped. Captures still need visual review.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", default="")
    parser.add_argument("--no-fab", action="store_true", help="fab-integration: hide the local Fab packs (before/after captures)")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/MonsterGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireMonsterGallery",
               "-CireTripoChampions", "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if args.no_fab:
        command += ["-CireNoFabVFX", "-CireNoFabAnim", "-CireNoFabCreatures"]
    if args.only:
        command.append(f"-CireMonsterGalleryOnly={args.only}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=900)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for line in text.splitlines():
        if re.search(r"CIRE_MONSTER_GALLERY_(PASS|FAIL|CAPTURE|CHECK_FAIL|WAVE)|CIRE_MONSTER_ART_FALLBACK|Fatal error|Ensure condition failed", line):
            print(line.strip())
    passed = code == 0 and "CIRE_MONSTER_GALLERY_PASS" in text
    print("MONSTER GALLERY", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
