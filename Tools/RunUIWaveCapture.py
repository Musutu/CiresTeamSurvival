"""ui-themes: capture the HUD during real wave fights (crowds of monsters, nameplates, cast bars).

Runs the wave soak (-CireWaveSoak, the player becomes a bot) rendered offscreen at 1920x1080 with
-CireUIWaveCapture; the native hook (CireUIWaveCapture.cpp) screenshots whenever 5+ monsters fight
near the player. Captures: Saved/UIWaveCapture/<utc>/wave_fight_NN.png.

  python Tools/RunUIWaveCapture.py [--theme ArcaneVeil] [--shots 4] [--timeout 420]
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


# Skip the editor's ValidatePlatforms Build.bat call (it blocks while other worktrees compile).
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child):
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--theme")
    parser.add_argument("--shots", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=420)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    log = root / "Saved/Logs" / f"UIWaveCapture-{args.theme or 'profile'}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel",
               "-game", "-CireWaveSoak", "-CireWaveSoakCycles=1", "-CireUIWaveCapture", f"-CireUIWaveShots={args.shots}",
               "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4",
               "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"] + ([f"-CireUITheme={args.theme}"] if args.theme else [])
    child = subprocess.Popen(command, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=EDITOR_ENV,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    shots = re.findall(r"CIRE_UI_WAVE_CAPTURE (\S+\.png)", text)
    done = re.search(r"CIRE_UI_WAVE_CAPTURE_PASS captures=(\d+) directory=(.+)", text)
    for s in shots:
        print("capture", s)
    print("PASS" if done else f"INCOMPLETE ({len(shots)} captures)", done.group(2).strip() if done else "")
    return 0 if done else 1


if __name__ == "__main__":
    sys.exit(main())
