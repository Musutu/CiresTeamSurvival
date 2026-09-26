"""champion-hq: run the whole post-transfer pipeline for the HQ bodies (Docs/ArtIntegration.md, "Champion HQ bodies").

  1. Tools/RunChampionHQIntegration.py   move the Bridge imports, HQ material, LODs, clip names
  2. Tools/WriteChampionHQArtRows.py     Content/Data/ChampionArt.hq.json rows
  3. Tools/BuildTripoChampionMotion.py   idle/walk/run BlendSpaces (resampled) for the HQ rows
  4. Tools/RetargetChampionAttacks.py    -CireChampionAttacksAdd: ChampionAttacks02/HQ<Folder> action clips
  5. Tools/WriteChampionHQBindings.py    ChampionArtBindings / ChampionAttacks02 bodies / SummonArt
Each editor step is a separate process; the Bridge editor must be closed. Stops at the first failing step.
Usage: python Tools/RunChampionHQPipeline.py [--only key,key] [--skip-integrate]
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PY = sys.executable
EDITOR = "F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"


def commandlet(script: str, log: str, extra=(), env=None, timeout=3600) -> str:
    cmd = [EDITOR, str(ROOT / "CiresTeamSurvival.uproject"), "-run=pythonscript", "-script=" + str(ROOT / "Tools" / script),
           *extra, "-unattended", "-nullrhi", "-nosplash", "-NoLiveCoding", "-abslog=" + str(ROOT / "Saved" / "Logs" / log)]
    subprocess.run(cmd, env={**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1", **(env or {})}, timeout=timeout,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return (ROOT / "Saved" / "Logs" / log).read_text(encoding="utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", default="")
    parser.add_argument("--skip-integrate", action="store_true")
    args = parser.parse_args()
    env = {"CIRE_CHAMPION_HQ_ONLY": args.only} if args.only else {}
    if not args.skip_integrate:
        r = subprocess.run([PY, str(ROOT / "Tools" / "RunChampionHQIntegration.py")], env={**os.environ, **env}, capture_output=True, text=True)
        print(r.stdout.strip()[-2000:])
        if "errors 0" not in r.stdout:
            print("CIRE_CHAMPION_HQ_PIPELINE FAIL integrate"); return 1
    r = subprocess.run([PY, str(ROOT / "Tools" / "WriteChampionHQArtRows.py")], capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode:
        print(r.stderr); print("CIRE_CHAMPION_HQ_PIPELINE FAIL rows"); return 1
    text = commandlet("BuildTripoChampionMotion.py", "HQMotion.log", env={"CIRE_CHAMPION_ART_FILES": "ChampionArt.hq.json"})
    if "CIRE_TRIPO_CHAMPION_MOTION_PASS" not in text:
        print("CIRE_CHAMPION_HQ_PIPELINE FAIL motion (Saved/TripoChampionMotion.json)"); return 1
    text = commandlet("RetargetChampionAttacks.py", "HQAttacks.log", extra=["-CireChampionAttacksAdd"])
    if "CIRE_CHAMPION_ATTACKS02_PASS" not in text:
        print("CIRE_CHAMPION_HQ_PIPELINE FAIL attacks (Saved/ChampionAttacks02Build.json)"); return 1
    r = subprocess.run([PY, str(ROOT / "Tools" / "WriteChampionHQBindings.py")], capture_output=True, text=True)
    print(r.stdout.strip())
    print("CIRE_CHAMPION_HQ_PIPELINE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
