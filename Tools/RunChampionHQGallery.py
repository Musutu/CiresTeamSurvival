"""champion-hq: render the HQ champion review gallery (Source/CiresTeamSurvival/CireChampionHQGallery.cpp).

Offscreen 1920x1080 captures land in Saved/ChampionHQ/Gallery/<stamp>/ (or --out): a front lineup and, per champion,
idle / run / attack (contact frame) / cast (release frame) / heavy (war-cry peak) close-ups on the real lane.
Usage: python Tools/RunChampionHQGallery.py [--profiles ranger,scholar] [--out DIR] [--no-tripo]
Only the editor process started here is stopped. Captures still need visual review.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--profiles", default="")
    parser.add_argument("--out", default="")
    parser.add_argument("--no-tripo", action="store_true", help="omit -CireTripoChampions (the mannequin 'before' bodies)")
    parser.add_argument("--timeout", type=int, default=1600)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/ChampionHQGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireChampionHQGallery",
               "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if not args.no_tripo:
        command.append("-CireTripoChampions")
    if args.profiles:
        command.append(f"-CireChampionHQProfiles={args.profiles}")
    if args.out:
        command.append(f"-CireChampionHQOut={Path(args.out).resolve()}")
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
        if re.search(r"CIRE_CHAMPION_HQ_GALLERY_(PASS|FAIL|CHECK_FAIL|READY|NOCLIP)|primary body unavailable|Keeping original hero art|Fatal error", line):
            print(line.strip())
    passed = code == 0 and "CIRE_CHAMPION_HQ_GALLERY_PASS" in text
    print("CIRE_CHAMPION_HQ_GALLERY_RUN", "PASS" if passed else "FAIL", "exit", code)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
