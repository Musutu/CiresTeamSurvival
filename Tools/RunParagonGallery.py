"""Render the Paragon champions review gallery (Source/CiresTeamSurvival/CireParagonGallery.cpp).

Offscreen 1920x1080 captures land in Saved/ParagonGallery/<stamp>/: per installed Paragon hero a three-quarter
close-up (<id>_0_front) and one shot per own ability cast at a training dummy (<id>_<n>_<ability>), with the body /
clip / action state logged per shot (CIRE_PARAGON_GALLERY_POSE). The captures show Epic-licensed content: they stay
in Saved/ and are never committed. Usage: python Tools/RunParagonGallery.py [--only pg_greystone,pg_kwang]
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
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/ParagonGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireParagonGallery",
               "-CireTripoChampions", "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if args.only:
        command.append(f"-CireParagonGalleryOnly={args.only}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=5700)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for line in text.splitlines():
        if re.search(r"CIRE_PARAGON_GALLERY_(PASS|FAIL|CHECK_FAIL|CAST|POSE|READY)|Fatal error|Ensure condition failed", line):
            print(line.strip())
    passed = code == 0 and "CIRE_PARAGON_GALLERY_PASS" in text
    print("PARAGON GALLERY", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
