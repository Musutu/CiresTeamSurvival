"""World editor gallery (world-editor): the map layout editor's WORLD tab on the real town, scripted and captured
(CireWorldEditGallery.cpp): the tab over a house block, a whole building selected, removed, ghosts, undo.

    python Tools/RunWorldEditGallery.py [--timeout 1200]

Writes no data files. Captures land in Saved/WorldEditGallery/<stamp>/. Only the child process created here is terminated.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe")
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=int, default=1200)
    args = parser.parse_args()
    log = ROOT / "Saved/WorldEditGallery.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireRouteEdit", "-CireTown", "-CireWorldEditGallery",
               "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-windowed", "-ExecCmds=t.MaxFPS 60",
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-abslog={log}"]
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    done = re.search(r"CIRE_WORLD_EDIT_GALLERY_DONE captures=(\d+) dir=(.*)", text)
    print(done.group(0) if done else f"gallery did not finish (exit {code}); log {log}")
    return 0 if done else 1


if __name__ == "__main__":
    raise SystemExit(main())
